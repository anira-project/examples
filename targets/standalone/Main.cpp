// SPDX-License-Identifier: Apache-2.0

// Any example (ANIRA_EXAMPLE_HEADER) as a standalone app: the default audio input through the model
// to the default output (miniaudio, duplex), and a page in a CHOC WebView with the example's
// parameters, the backend choice with its real-time verdict ("Try nonetheless") and the dry/wet
// mix, as the plugins have them.

#include <anira/anira.hpp>
#include ANIRA_EXAMPLE_HEADER
#include <anira/examples/Backends.h>
#include <anira/examples/DryWet.h>
#include <anira/examples/Example.h>
#include <anira/examples/Weights.h>
#include <anira/examples/standalone/Ui.h>
#include <choc/containers/choc_Value.h>
#include <choc/gui/choc_DesktopWindow.h>
#include <choc/gui/choc_MessageLoop.h>
#include <choc/gui/choc_WebView.h>
#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using anira::examples::Example;
static_assert(anira::examples::ExampleLike<Example>);
static_assert(Example::k_channels == 1 || Example::k_channels == 2);

/// The period asked of the device by default; it may give another (macOS's built-in devices often
/// keep 1024).
constexpr std::uint32_t k_period = 256;

/// The rates miniaudio calls standard: a device that takes any rate offers these.
constexpr std::array<std::uint32_t, 8>
    k_standard_rates{16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000};

/// A device list's entries for the page: name and whether it is the system's default.
choc::value::Value device_list(const std::vector<ma_device_info>& devices) {
    auto list = choc::value::createEmptyArray();
    for (const ma_device_info& device : devices) {
        auto entry = choc::value::createObject("Device");
        entry.addMember("name", std::string(device.name));
        entry.addMember("default", device.isDefault != 0);
        list.addArrayElement(entry);
    }
    return list;
}

class App {
public:
    App() {
        for (std::size_t i = 0; i < m_values.size(); ++i) {
            m_values[i].store(Example::k_parameters[i].m_default);
        }
        m_backend.store(m_backends.default_choice());
        try {
            m_example = std::make_unique<Example>(m_context, anira::examples::weights());
        } catch (const std::exception& error) { m_weights_error = error.what(); }
        m_has_context = ma_context_init(nullptr, 0, nullptr, &m_ma) == MA_SUCCESS;
        scan();
    }
    ~App() {
        stop();
        if (m_has_context) { ma_context_uninit(&m_ma); }
    }
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    /// Main thread: the chosen input and output (the system's defaults until the page chooses), the
    /// example prepared for them (a new stream), and the stream started.
    void start() {
        m_error.clear();
        m_prepared = false;
        ma_device_config config = ma_device_config_init(ma_device_type_duplex);
        if (m_input >= 0 && std::cmp_less(m_input, m_inputs.size())) {
            config.capture.pDeviceID = &m_inputs[static_cast<std::size_t>(m_input)].id;
        }
        if (m_output >= 0 && std::cmp_less(m_output, m_outputs.size())) {
            config.playback.pDeviceID = &m_outputs[static_cast<std::size_t>(m_output)].id;
        }
        config.capture.format = ma_format_f32;
        config.capture.channels = Example::k_channels;
        config.playback.format = ma_format_f32;
        config.playback.channels = Example::k_channels;
        config.sampleRate = m_sample_rate;  // 0: the device's own
        config.periodSizeInFrames = m_period;
        config.performanceProfile = ma_performance_profile_low_latency;
        // The device's own periods, one callback each: miniaudio's fixed-size callbacks would cut
        // a longer period into blocks that arrive back to back, a burst anira's latency, planned
        // for one block per period, does not cover.
        config.noFixedSizedCallback = MA_TRUE;
        config.dataCallback = &App::callback;
        config.pUserData = this;
        if (ma_device_init(m_has_context ? &m_ma : nullptr, &config, &m_device) != MA_SUCCESS) {
            m_error = "the audio device did not open";
            return;
        }
        m_open = true;
        m_rate = m_device.sampleRate;
        // The largest block a callback brings: a duplex device whose sides run on their own
        // periods (a Bluetooth headset) brings up to two of the longer one at a time.
        m_block = 2 * std::max({m_period,
                                m_device.capture.internalPeriodSizeInFrames,
                                m_device.playback.internalPeriodSizeInFrames});

        for (std::size_t c = 0; c < m_planes.size(); ++c) {
            m_planes[c].assign(m_block, 0.0F);
            m_channels[c] = m_planes[c].data();
        }
        m_audio = anira::Tensor::from_host_planar<float>(
            std::span<float* const>(m_channels),
            std::array<std::int64_t, 2>{Example::k_channels, m_block});
        if (m_example) {
            try {
                m_example->prepare(m_rate, m_block);
                std::fprintf(stderr,
                             "anira: device %.0f Hz, periods of %u samples\n",
                             m_rate,
                             m_block);
                for (const std::string& line : m_backends.read(*m_example)) {
                    std::fprintf(stderr, "%s\n", line.c_str());
                }
                // An analyzer leaves the audio as it is: no latency.
                if constexpr (anira::examples::kind_of<Example>() !=
                              anira::examples::Kind::Analyzer) {
                    m_latency = m_example->get_latency();
                }
                m_prepared = true;
            } catch (const std::exception& error) { m_error = error.what(); }
        }
        m_dry_wet.prepare(m_channels.size(), m_block, m_latency);
        if (ma_device_start(&m_device) != MA_SUCCESS) {
            m_error = "the audio device did not start";
        }
    }

    void stop() {
        if (m_open) {
            ma_device_uninit(&m_device);  // the callback has returned for good
            m_open = false;
        }
    }

    /// Main thread: the inputs and outputs there are now, and the ones chosen (-1: the default).
    [[nodiscard]] choc::value::Value devices() {
        scan();
        auto devices = choc::value::createObject("Devices");
        devices.addMember("inputs", device_list(m_inputs));
        devices.addMember("outputs", device_list(m_outputs));
        devices.addMember("input", static_cast<std::int64_t>(m_input));
        devices.addMember("output", static_cast<std::int64_t>(m_output));
        devices.addMember("period", static_cast<std::int64_t>(m_period));
        devices.addMember("rate", static_cast<std::int64_t>(m_sample_rate));
        devices.addMember("rates", rates(m_input, m_output));
        return devices;
    }

    /// Main thread: the rates both devices take natively (indices as in devices(); -1 the
    /// default), ascending.
    [[nodiscard]] choc::value::Value rates(int input, int output) {
        const std::vector<std::uint32_t> in = native_rates(ma_device_type_capture, m_inputs, input);
        const std::vector<std::uint32_t> out =
            native_rates(ma_device_type_playback, m_outputs, output);
        auto list = choc::value::createEmptyArray();
        for (const std::uint32_t rate : in) {
            if (std::ranges::find(out, rate) != out.end()) {
                list.addArrayElement(static_cast<std::int64_t>(rate));
            }
        }
        return list;
    }

    /// Main thread: the stream on other devices or another period (indices into the lists the last
    /// devices() gave; -1 the default): stopped, the example prepared again, started.
    void open(int input, int output, std::uint32_t period, std::uint32_t sample_rate) {
        stop();
        m_input = input;
        m_output = output;
        m_period = std::clamp<std::uint32_t>(period, 32, 4096);
        m_sample_rate = sample_rate;
        start();
    }

    /// Main thread, for the page: what it shows.
    [[nodiscard]] choc::value::Value state() const {
        auto state = choc::value::createObject("State");
        state.addMember("name", std::string(Example::k_name));
        state.addMember("analyzer",
                        anira::examples::kind_of<Example>() == anira::examples::Kind::Analyzer);
        state.addMember("rate", m_rate);
        state.addMember("block", static_cast<std::int64_t>(m_block));
        state.addMember("latency", static_cast<std::int64_t>(m_latency));
        auto parameters = choc::value::createEmptyArray();
        for (std::size_t i = 0; i < m_values.size(); ++i) {
            const anira::examples::Parameter& p = Example::k_parameters[i];
            auto parameter = choc::value::createObject("Parameter");
            parameter.addMember("name", std::string(p.m_name));
            parameter.addMember("min", p.m_min);
            parameter.addMember("max", p.m_max);
            parameter.addMember("value", m_values[i].load());
            parameters.addArrayElement(parameter);
        }
        state.addMember("parameters", parameters);
        auto meters = choc::value::createEmptyArray();
        for (std::size_t i = 0; i < anira::examples::meter_count<Example>(); ++i) {
            const anira::examples::Meter info = anira::examples::meter_info<Example>(i);
            auto meter = choc::value::createObject("Meter");
            meter.addMember("name", std::string(info.m_name));
            meter.addMember("min", info.m_min);
            meter.addMember("max", info.m_max);
            meter.addMember("value",
                            m_example ? anira::examples::meter_value(*m_example, i) : 0.0F);
            meters.addArrayElement(meter);
        }
        state.addMember("meters", meters);
        auto backends = choc::value::createEmptyArray();
        for (const anira::examples::BackendChoice& choice : m_backends.choices()) {
            backends.addArrayElement(std::string(choice.m_name));
        }
        state.addMember("backends", backends);
        state.addMember("backend", static_cast<std::int64_t>(m_backend.load()));
        state.addMember("mix", m_mix.load());
        const std::string& error = m_weights_error.empty() ? m_error : m_weights_error;
        state.addMember("status",
                        error.empty() ? m_backends.verdict_line(m_backend.load())
                                      : "The model does not run: " + error);
        state.addMember("waits", m_backends.waits_for_consent(m_backend.load()));
        return state;
    }

    void set_parameter(std::size_t index, float value) {
        if (index < m_values.size()) { m_values[index].store(value); }
    }
    void set_backend(std::size_t choice) {
        if (choice < m_backends.choices().size()) { m_backend.store(choice); }
    }
    void set_mix(float mix) { m_mix.store(std::clamp(mix, 0.0F, 1.0F)); }
    void try_nonetheless() { m_backends.try_nonetheless(m_backend.load()); }

private:
    /// The inputs and outputs the system has now (their ids stay valid in these copies).
    void scan() {
        if (!m_has_context) { return; }
        ma_device_info* outputs = nullptr;
        ma_device_info* inputs = nullptr;
        ma_uint32 output_count = 0;
        ma_uint32 input_count = 0;
        if (ma_context_get_devices(&m_ma, &outputs, &output_count, &inputs, &input_count) !=
            MA_SUCCESS) {
            return;
        }
        m_outputs.assign(outputs, outputs + output_count);
        m_inputs.assign(inputs, inputs + input_count);
    }

    /// The rates a device takes natively, ascending: the formats its driver reports (any rate: the
    /// standard ones). The device is the list's `index`, or the system's default for -1.
    std::vector<std::uint32_t> native_rates(ma_device_type type,
                                            const std::vector<ma_device_info>& devices,
                                            int index) {
        std::vector<std::uint32_t> rates;
        const ma_device_info* chosen = nullptr;
        if (index >= 0 && std::cmp_less(index, devices.size())) {
            chosen = &devices[static_cast<std::size_t>(index)];
        } else {
            const auto it = std::ranges::find_if(devices, [](const ma_device_info& device) {
                return device.isDefault != 0;
            });
            chosen = it == devices.end() ? nullptr : &*it;
        }
        ma_device_info info{};
        if (!m_has_context || ma_context_get_device_info(&m_ma,
                                                         type,
                                                         chosen != nullptr ? &chosen->id : nullptr,
                                                         &info) != MA_SUCCESS) {
            return rates;
        }
        for (ma_uint32 i = 0; i < info.nativeDataFormatCount; ++i) {
            const ma_uint32 rate = info.nativeDataFormats[i].sampleRate;
            if (rate == 0) {
                rates.insert(rates.end(), k_standard_rates.begin(), k_standard_rates.end());
            } else {
                rates.push_back(rate);
            }
        }
        std::ranges::sort(rates);
        rates.erase(std::ranges::unique(rates).begin(), rates.end());
        return rates;
    }

    static void callback(ma_device* device, void* output, const void* input, ma_uint32 frames) {
        static_cast<App*>(device->pUserData)
            ->process(static_cast<float*>(output), static_cast<const float*>(input), frames);
    }

    /// Audio thread: the example on the block in m_audio with the chosen plan and the parameters.
    void run(std::size_t samples, std::size_t choice) noexcept {
        m_backends.select(*m_example, choice);
        for (std::size_t i = 0; i < m_values.size(); ++i) {
            m_example->set_parameter(i, m_values[i].load());
        }
        m_audio.shape[1] = static_cast<std::int64_t>(samples);
        m_example->process(m_audio, false);
    }

    /// Audio thread: interleaved in, interleaved out, one device period (at most m_block; cut
    /// only if a device ever brings more than it said).
    void process(float* out, const float* in, std::size_t frames) noexcept {
        constexpr std::size_t k_channels = Example::k_channels;
        for (std::size_t start = 0; start < frames; start += m_block) {
            const std::size_t samples = std::min<std::size_t>(m_block, frames - start);
            for (std::size_t i = 0; i < samples; ++i) {
                for (std::size_t c = 0; c < k_channels; ++c) {
                    m_planes[c][i] = in[((start + i) * k_channels) + c];
                }
            }
            const std::size_t choice = m_backend.load();
            const bool runs = m_prepared && m_backends.runs(choice, false);
            constexpr anira::examples::Kind k_kind = anira::examples::kind_of<Example>();
            if constexpr (k_kind == anira::examples::Kind::Analyzer) {
                if (runs) { run(samples, choice); }  // it only reads: the input passes through
            } else {
                if constexpr (k_kind == anira::examples::Kind::Generator) {  // sound from nothing
                    for (float* channel : m_channels) { std::fill_n(channel, samples, 0.0F); }
                }
                m_dry_wet.push(m_channels, samples);
                if (runs) {
                    run(samples, choice);  // in place
                    m_dry_wet.mix(m_channels, samples, m_mix.load());
                } else {
                    m_dry_wet.dry(m_channels, samples);
                }
            }
            for (std::size_t i = 0; i < samples; ++i) {
                for (std::size_t c = 0; c < k_channels; ++c) {
                    out[((start + i) * k_channels) + c] = m_planes[c][i];
                }
            }
        }
    }

    anira::Context m_context{anira::ContextConfig{}};
    std::unique_ptr<Example> m_example;  // none when its weights are missing
    std::atomic<std::size_t> m_backend{0};
    double m_rate = 0.0;
    std::size_t m_latency = 0;
    std::array<float*, Example::k_channels> m_channels{};
    std::string m_weights_error;  // why there is no example
    std::string m_error;          // why the stream does not run
    std::vector<ma_device_info> m_inputs;
    std::vector<ma_device_info> m_outputs;
    std::array<std::vector<float>, Example::k_channels> m_planes;
    anira::examples::DryWet m_dry_wet;
    anira::Tensor m_audio{};
    anira::examples::Backends m_backends;
    ma_context m_ma;     // ma_context_init fills it
    ma_device m_device;  // ma_device_init fills it

    std::array<std::atomic<float>, Example::k_parameters.size()> m_values{};
    std::atomic<float> m_mix{1.0F};
    int m_input = -1;  // -1: the system's default
    int m_output = -1;
    std::uint32_t m_period = k_period;
    std::uint32_t m_sample_rate = 0;  // 0: the device's own
    std::uint32_t m_block = k_period;
    bool m_prepared = false;
    bool m_has_context = false;
    bool m_open = false;
};

/// A number argument from the page (JavaScript's double); `fallback` when missing.
double number(const choc::value::ValueView& args, std::uint32_t index, double fallback = 0.0) {
    return args.isArray() && args.size() > index ? args[index].getWithDefault<double>(fallback)
                                                 : fallback;
}

/// The app: the audio running, the window open until it is closed.
int run() {
    choc::messageloop::initialise();
    App app;
    app.start();

    choc::ui::DesktopWindow window({.x = 100, .y = 100, .width = 480, .height = 420});
    window.setWindowTitle(std::string(Example::k_name));
    window.setResizable(true);
    window.setMinimumSize(360, 280);
    window.windowClosed = [] { choc::messageloop::stop(); };

    choc::ui::WebView::Options options;
    options.fetchResource =
        [](const std::string& path) -> std::optional<choc::ui::WebView::Options::Resource> {
        if (path != "/" && path != "/index.html") { return std::nullopt; }
        return choc::ui::WebView::Options::Resource(anira::examples::standalone::k_ui_html,
                                                    "text/html");
    };
    choc::ui::WebView view(options);
    view.bind("aniraState", [&app](const choc::value::ValueView&) { return app.state(); });
    view.bind("aniraSetParameter", [&app](const choc::value::ValueView& args) {
        app.set_parameter(static_cast<std::size_t>(number(args, 0)),
                          static_cast<float>(number(args, 1)));
        return choc::value::Value();
    });
    view.bind("aniraSetBackend", [&app](const choc::value::ValueView& args) {
        app.set_backend(static_cast<std::size_t>(number(args, 0)));
        return choc::value::Value();
    });
    view.bind("aniraSetMix", [&app](const choc::value::ValueView& args) {
        app.set_mix(static_cast<float>(number(args, 0, 1.0)));
        return choc::value::Value();
    });
    view.bind("aniraDevices", [&app](const choc::value::ValueView&) { return app.devices(); });
    view.bind("aniraOpen", [&app](const choc::value::ValueView& args) {
        app.open(static_cast<int>(number(args, 0, -1.0)),
                 static_cast<int>(number(args, 1, -1.0)),
                 static_cast<std::uint32_t>(number(args, 2, k_period)),
                 static_cast<std::uint32_t>(number(args, 3, 0.0)));
        return choc::value::Value();
    });
    view.bind("aniraRates", [&app](const choc::value::ValueView& args) {
        return app.rates(static_cast<int>(number(args, 0, -1.0)),
                         static_cast<int>(number(args, 1, -1.0)));
    });
    view.bind("aniraTryNonetheless", [&app](const choc::value::ValueView&) {
        app.try_nonetheless();
        return choc::value::Value();
    });
    window.setContent(view.getViewHandle());
    window.toFront();

    choc::messageloop::run();
    app.stop();
    return 0;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "anira: %s\n", error.what());
        return 1;
    }
}
