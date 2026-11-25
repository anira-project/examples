// SPDX-License-Identifier: Apache-2.0

#include <NativeAniraJSI.h>
#include <host/ExampleHost.h>
#include <tanh/audio-io/AudioDeviceManager.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

// The device's rate and block: 256 samples, about 5 ms.
constexpr uint32_t k_sample_rate = 48000;
constexpr uint32_t k_requested_buffer_size = 256;
constexpr uint32_t k_num_channels = ExampleEngine::Example::k_channels;

std::string result_json(bool success, const std::string& error = "") {
    if (success) { return R"({"success":true})"; }
    return std::string(R"({"success":false,"error":)") + '"' + error + R"("})";
}

/** A JSON string literal. */
std::string json_string(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out + '"';
}

std::string number(double value) {
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.4g", value);
    return text.data();
}

void append_array(std::string& json, const std::vector<float>& values) {
    json += '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) { json += ','; }
        json += number(values[i]);
    }
    json += ']';
}

}  // namespace

namespace facebook::react {

NativeAniraJSI::NativeAniraJSI(std::shared_ptr<CallInvoker> js_invoker)
    : NativeAniraJSICxxSpec(std::move(js_invoker))
    , m_audio_device_manager(std::make_unique<thl::AudioDeviceManager>())
    , m_host(std::make_unique<ExampleHost>()) {}

NativeAniraJSI::~NativeAniraJSI() {
    stop();
}

jsi::String NativeAniraJSI::startAudio(jsi::Runtime& rt) {
    if (m_audio_running) { return jsi::String::createFromUtf8(rt, result_json(true)); }
    const auto inputs = m_audio_device_manager->enumerate_input_devices();
    const auto outputs = m_audio_device_manager->enumerate_output_devices();
    if (inputs.empty() || outputs.empty()) {
        return jsi::String::createFromUtf8(
            rt,
            result_json(false, "no audio devices found (microphone permission?)"));
    }
    // 48 kHz, which phones run at natively; the example resamples to its model's rate itself.
    if (!m_audio_device_manager->initialise(&inputs[0],
                                            &outputs[0],
                                            k_sample_rate,
                                            k_requested_buffer_size,
                                            k_num_channels,
                                            k_num_channels)) {
        return jsi::String::createFromUtf8(rt,
                                           result_json(false, "audio device initialise failed"));
    }
    m_audio_device_manager->add_duplex_callback(m_host.get());
    if (!m_audio_device_manager->start_duplex()) {
        m_audio_device_manager->remove_duplex_callback(m_host.get());
        m_audio_device_manager->shutdown();
        return jsi::String::createFromUtf8(rt, result_json(false, "start_duplex failed"));
    }
    m_audio_running = true;
    return jsi::String::createFromUtf8(rt, result_json(true));
}

void NativeAniraJSI::stop() {
    if (!m_audio_running) { return; }
    m_audio_device_manager->stop_duplex();
    m_audio_device_manager->remove_duplex_callback(m_host.get());
    m_audio_device_manager->shutdown();
    m_audio_running = false;
}

void NativeAniraJSI::stopAudio(jsi::Runtime& /*rt*/) {
    stop();
}

jsi::String NativeAniraJSI::getState(jsi::Runtime& rt) {
    using Example = ExampleEngine::Example;
    const ExampleEngine& engine = m_host->engine();
    std::string json = "{\"name\":" + json_string(Example::k_name);
    json += ",\"audioRunning\":";
    json += m_audio_running ? "true" : "false";
    json += ",\"sampleRate\":" + number(engine.sample_rate());
    json += ",\"latency\":" + std::to_string(engine.latency());
    json += ",\"parameters\":[";
    for (size_t i = 0; i < Example::k_parameters.size(); ++i) {
        const anira::examples::Parameter& p = Example::k_parameters[i];
        json += (i > 0 ? ",{\"name\":" : "{\"name\":") + json_string(p.m_name) +
                ",\"min\":" + number(p.m_min) + ",\"max\":" + number(p.m_max) +
                ",\"value\":" + number(engine.parameter(i)) + "}";
    }
    json += "],\"meters\":[";
    for (size_t i = 0; i < ExampleEngine::k_meters; ++i) {
        const anira::examples::Meter m = anira::examples::meter_info<Example>(i);
        json += (i > 0 ? ",{\"name\":" : "{\"name\":") + json_string(m.m_name) +
                ",\"min\":" + number(m.m_min) + ",\"max\":" + number(m.m_max) + "}";
    }
    json += "],\"backends\":[";
    const auto& backends = engine.backends();
    for (size_t i = 0; i < backends.size(); ++i) {
        json += (i > 0 ? "," : "") + json_string(backends[i].m_name);
    }
    json += "],\"backend\":" + std::to_string(engine.backend());
    json += ",\"mix\":" + number(engine.mix());
    json += ",\"status\":" + json_string(engine.status());
    json += ",\"waits\":";
    json += engine.waits_for_consent() ? "true" : "false";
    json += '}';
    return jsi::String::createFromUtf8(rt, json);
}

void NativeAniraJSI::setParameter(jsi::Runtime& /*rt*/, double index, double value) {
    if (index >= 0) {
        m_host->engine().set_parameter(static_cast<size_t>(index), static_cast<float>(value));
    }
}

void NativeAniraJSI::setBackend(jsi::Runtime& /*rt*/, double index) {
    if (index >= 0) { m_host->engine().set_backend(static_cast<size_t>(index)); }
}

void NativeAniraJSI::setMix(jsi::Runtime& /*rt*/, double value) {
    m_host->engine().set_mix(static_cast<float>(std::clamp(value, 0.0, 1.0)));
}

void NativeAniraJSI::tryNonetheless(jsi::Runtime& /*rt*/) {
    m_host->engine().try_nonetheless();
}

jsi::String NativeAniraJSI::getTimeline(jsi::Runtime& rt, double maxCount) {
    std::vector<float> peaks;
    std::array<std::vector<float>, ExampleEngine::k_meters> meters;
    m_host->engine().timelines(peaks, meters, static_cast<uint32_t>(std::max(0.0, maxCount)));
    std::string json = R"({"peaks":)";
    append_array(json, peaks);
    json += R"(,"meters":[)";
    for (size_t m = 0; m < meters.size(); ++m) {
        if (m > 0) { json += ','; }
        append_array(json, meters[m]);
    }
    json += "]}";
    return jsi::String::createFromUtf8(rt, json);
}

}  // namespace facebook::react
