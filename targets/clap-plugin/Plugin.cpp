// SPDX-License-Identifier: Apache-2.0

#include "Plugin.h"

#include <anira/examples/Weights.h>

#include <algorithm>
#include <array>
#include <clap/helpers/host-proxy.hh>
#include <clap/helpers/host-proxy.hxx>  // IWYU pragma: keep (the template definitions)
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>  // IWYU pragma: keep (the template definitions)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace anira::examples::clap_plugin {

namespace {

const std::array<const char*, 3> k_features{
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    Example::k_channels == 2 ? CLAP_PLUGIN_FEATURE_STEREO : CLAP_PLUGIN_FEATURE_MONO,
    nullptr};

const clap_plugin_descriptor k_descriptor{
    .clap_version = CLAP_VERSION_INIT,
    .id = ANIRA_EXAMPLE_ID,
    .name = ANIRA_EXAMPLE_PRODUCT,
    .vendor = "anira-project",
    .url = "https://github.com/anira-project/examples",
    .manual_url = "",
    .support_url = "",
    .version = "0.1.0",
    .description = "A model from anira-project/models on anira",
    .features = k_features.data()};

void copy(std::string_view text, char* out, std::size_t size) noexcept {
    const std::size_t n = std::min(text.size(), size - 1);
    std::memcpy(out, text.data(), n);
    out[n] = '\0';
}

}  // namespace

const clap_plugin_descriptor* Plugin::descriptor() noexcept {
    return &k_descriptor;
}

Plugin::Plugin(const clap_host* host)
    : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                            clap::helpers::CheckingLevel::Maximal>(&k_descriptor, host) {
    for (std::size_t i = 0; i < k_example_params; ++i) {
        m_values[i].store(Example::k_parameters[i].m_default);
    }
    m_values[k_example_params].store(100.0);  // Mix
    m_values[k_example_params + 1].store(static_cast<double>(m_backends.default_choice()));
    m_values[k_example_params + 2].store(0.0);  // Try nonetheless
    try {
        m_example = std::make_unique<Example>(m_context, weights());
    } catch (const std::exception& error) { std::fprintf(stderr, "anira: %s\n", error.what()); }
}

std::optional<std::size_t> Plugin::index_of(clap_id id) noexcept {
    if (id < k_example_params) { return id; }
    if (id >= k_mix && id <= k_try_nonetheless) { return k_example_params + (id - k_mix); }
    return std::nullopt;
}

clap_id Plugin::id_of(std::size_t index) noexcept {
    return index < k_example_params ? static_cast<clap_id>(index)
                                    : k_mix + static_cast<clap_id>(index - k_example_params);
}

bool Plugin::activate(double sample_rate,
                      std::uint32_t min_frames,
                      std::uint32_t max_frames) noexcept {
    // The host blocks complete the example's contracts: CLAP names both ends of the range (at
    // least one sample). The latency covers every block of it; latencyGet() hands it to the host.
    (void)min_frames;  // the wrappers take blocks from one sample up
    m_prepared = false;
    m_latency = 0;
    if (m_example) {
        try {
            m_example->prepare(sample_rate, max_frames);
            for (const std::string& line : m_backends.read(*m_example)) {
                std::fprintf(stderr, "%s\n", line.c_str());
            }
            m_latency = static_cast<std::uint32_t>(m_example->get_latency());
            m_prepared = true;
        } catch (const std::exception& error) { std::fprintf(stderr, "anira: %s\n", error.what()); }
    }
    try {
        m_dry_wet.prepare(m_channels.size(), max_frames, m_latency);
        m_audio = anira::Tensor::from_host_planar<float>(
            std::span<float* const>(m_channels),
            std::array<std::int64_t, 2>{Example::k_channels,
                                        static_cast<std::int64_t>(max_frames)});
    } catch (const std::exception& error) {
        std::fprintf(stderr, "anira: %s\n", error.what());
        return false;
    }
    return true;
}

clap_process_status Plugin::process(const clap_process* process) noexcept {
    // The block's parameter changes, at its start.
    const std::uint32_t events = process->in_events->size(process->in_events);
    for (std::uint32_t i = 0; i < events; ++i) {
        handle(process->in_events->get(process->in_events, i));
    }

    const std::size_t samples = process->frames_count;
    float* const* in = process->audio_inputs[0].data32;
    float* const* out = process->audio_outputs[0].data32;
    for (std::size_t c = 0; c < m_channels.size(); ++c) {
        if (out[c] != in[c]) { std::copy_n(in[c], samples, out[c]); }
        m_channels[c] = out[c];
    }
    m_dry_wet.push(m_channels, samples);

    const bool offline = m_offline.load();
    const auto choice = static_cast<std::size_t>(m_values[k_example_params + 1].load());
    const bool anyway = offline || m_values[k_example_params + 2].load() >= 0.5;
    if (m_prepared && m_backends.runs(choice, anyway)) {
        m_backends.select(*m_example, choice);
        for (std::size_t i = 0; i < k_example_params; ++i) {
            m_example->set_parameter(i, static_cast<float>(m_values[i].load()));
        }
        m_audio.shape[1] = static_cast<std::int64_t>(samples);
        m_example->process(m_audio, offline);  // in place
        m_dry_wet.mix(m_channels,
                      samples,
                      static_cast<float>(m_values[k_example_params].load() / 100.0));
    } else {
        m_dry_wet.dry(m_channels, samples);
    }
    return CLAP_PROCESS_CONTINUE;
}

void Plugin::handle(const clap_event_header_t* event) noexcept {
    if (event->space_id != CLAP_CORE_EVENT_SPACE_ID || event->type != CLAP_EVENT_PARAM_VALUE) {
        return;
    }
    const auto* value = reinterpret_cast<const clap_event_param_value*>(event);
    if (const auto index = index_of(value->param_id)) { m_values[*index].store(value->value); }
}

void Plugin::paramsFlush(const clap_input_events* in, const clap_output_events* /*out*/) noexcept {
    const std::uint32_t events = in->size(in);
    for (std::uint32_t i = 0; i < events; ++i) { handle(in->get(in, i)); }
}

bool Plugin::isValidParamId(clap_id id) const noexcept {
    return index_of(id).has_value();
}

std::uint32_t Plugin::paramsCount() const noexcept {
    return static_cast<std::uint32_t>(k_params);
}

bool Plugin::paramsInfo(std::uint32_t index, clap_param_info* info) const noexcept {
    if (index >= k_params) { return false; }
    *info = {};
    info->id = id_of(index);
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    copy("anira", info->module, sizeof(info->module));
    if (index < k_example_params) {
        const Parameter& p = Example::k_parameters[index];
        copy(p.m_name, info->name, sizeof(info->name));
        info->min_value = p.m_min;
        info->max_value = p.m_max;
        info->default_value = p.m_default;
        return true;
    }
    switch (info->id) {
        case k_mix:
            copy("Mix", info->name, sizeof(info->name));
            info->max_value = 100.0;
            info->default_value = 100.0;
            break;
        case k_backend:
            copy("Backend", info->name, sizeof(info->name));
            info->max_value = static_cast<double>(m_backends.choices().size() - 1);
            info->default_value = static_cast<double>(m_backends.default_choice());
            info->flags |= CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
            break;
        default:
            copy("Try nonetheless", info->name, sizeof(info->name));
            info->max_value = 1.0;
            info->flags |= CLAP_PARAM_IS_STEPPED;
            break;
    }
    return true;
}

bool Plugin::paramsValue(clap_id id, double* value) noexcept {
    const auto index = index_of(id);
    if (!index) { return false; }
    *value = m_values[*index].load();
    return true;
}

bool Plugin::paramsValueToText(clap_id id,
                               double value,
                               char* display,
                               std::uint32_t size) noexcept {
    if (size == 0 || !index_of(id)) { return false; }
    std::array<char, 64> text{};
    switch (id) {
        case k_mix: std::snprintf(text.data(), text.size(), "%.0f %%", value); break;
        case k_backend: {
            const auto choice = static_cast<std::size_t>(value);
            if (choice >= m_backends.choices().size()) { return false; }
            std::snprintf(text.data(), text.size(), "%s", m_backends.choices()[choice].m_name);
            break;
        }
        case k_try_nonetheless:
            std::snprintf(text.data(), text.size(), "%s", value >= 0.5 ? "On" : "Off");
            break;
        default: std::snprintf(text.data(), text.size(), "%.2f", value); break;
    }
    copy(text.data(), display, size);
    return true;
}

bool Plugin::paramsTextToValue(clap_id id, const char* display, double* value) noexcept {
    if (!index_of(id)) { return false; }
    const std::string_view text(display);
    switch (id) {
        case k_backend:
            for (std::size_t i = 0; i < m_backends.choices().size(); ++i) {
                if (text == m_backends.choices()[i].m_name) {
                    *value = static_cast<double>(i);
                    return true;
                }
            }
            return false;
        case k_try_nonetheless:
            if (text != "On" && text != "Off") { return false; }
            *value = text == "On" ? 1.0 : 0.0;
            return true;
        default: *value = std::strtod(display, nullptr); return true;
    }
}

std::uint32_t Plugin::audioPortsCount(bool /*is_input*/) const noexcept {
    return 1;
}

bool Plugin::audioPortsInfo(std::uint32_t index,
                            bool /*is_input*/,
                            clap_audio_port_info* info) const noexcept {
    if (index > 0) { return false; }
    info->id = 0;
    copy("main", info->name, sizeof(info->name));
    info->channel_count = Example::k_channels;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = Example::k_channels == 2 ? CLAP_PORT_STEREO : CLAP_PORT_MONO;
    info->in_place_pair = 0;
    return true;
}

bool Plugin::renderSetMode(clap_plugin_render_mode mode) noexcept {
    m_offline.store(mode == CLAP_RENDER_OFFLINE);
    return true;
}

// The state: a tag, then every parameter's value, in order.
namespace {

struct State {
    std::array<char, 8> m_tag{'a', 'n', 'i', 'r', 'a', 'e', 'x', '1'};
};

bool write_all(const clap_ostream* stream, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t done = 0; done < size;) {
        const std::int64_t n = stream->write(stream, bytes + done, size - done);
        if (n <= 0) { return false; }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

bool read_all(const clap_istream* stream, void* data, std::size_t size) noexcept {
    auto* bytes = static_cast<std::uint8_t*>(data);
    for (std::size_t done = 0; done < size;) {
        const std::int64_t n = stream->read(stream, bytes + done, size - done);
        if (n <= 0) { return false; }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

}  // namespace

bool Plugin::stateSave(const clap_ostream* stream) noexcept {
    std::array<double, k_params> values{};
    for (std::size_t i = 0; i < k_params; ++i) { values[i] = m_values[i].load(); }
    const State state;
    return write_all(stream, state.m_tag.data(), state.m_tag.size()) &&
           write_all(stream, values.data(), sizeof(values));
}

bool Plugin::stateLoad(const clap_istream* stream) noexcept {
    State state;
    if (!read_all(stream, state.m_tag.data(), state.m_tag.size()) || state.m_tag != State{}.m_tag) {
        return false;
    }
    std::array<double, k_params> values{};
    if (!read_all(stream, values.data(), sizeof(values))) { return false; }
    std::uint8_t extra = 0;
    if (stream->read(stream, &extra, 1) != 0) { return false; }  // not ours: longer
    for (std::size_t i = 0; i < k_params; ++i) {
        if (!std::isfinite(values[i])) { return false; }
    }
    for (std::size_t i = 0; i < k_params; ++i) { m_values[i].store(values[i]); }
    // The host shows the loaded values.
    if (_host.canUseParams()) { _host.paramsRescan(CLAP_PARAM_RESCAN_VALUES); }
    return true;
}

}  // namespace anira::examples::clap_plugin
