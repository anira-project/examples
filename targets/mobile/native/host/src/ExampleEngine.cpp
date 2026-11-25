// SPDX-License-Identifier: Apache-2.0

#include <anira/examples/Example.h>
#include <anira/examples/Weights.h>
#include <host/ExampleEngine.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <vector>

ExampleEngine::ExampleEngine() {
    for (std::size_t i = 0; i < m_values.size(); ++i) {
        m_values[i].store(Example::k_parameters[i].m_default);
    }
    m_backend.store(m_backends.default_choice());
    try {
        m_example = std::make_unique<Example>(m_context, anira::examples::weights());
    } catch (const std::exception& error) { m_error = error.what(); }
}

void ExampleEngine::prepare(double sample_rate, std::uint32_t max_block) {
    m_prepared = false;
    std::size_t latency = 0;
    for (std::size_t c = 0; c < m_planes.size(); ++c) {
        m_planes[c].assign(max_block, 0.0F);
        m_channels[c] = m_planes[c].data();
    }
    m_audio = anira::Tensor::from_host_planar<float>(
        std::span<float* const>(m_channels),
        std::array<std::int64_t, 2>{Example::k_channels, static_cast<std::int64_t>(max_block)});
    if (m_example) {
        try {
            m_example->prepare(sample_rate, max_block);
            (void)m_backends.read(*m_example);
            // An analyzer leaves the audio as it is: no latency.
            if constexpr (anira::examples::kind_of<Example>() != anira::examples::Kind::Analyzer) {
                latency = m_example->get_latency();
            }
            m_prepared = true;
        } catch (const std::exception& error) { m_error = error.what(); }
    }
    m_dry_wet.prepare(m_channels.size(), max_block, latency);
    m_latency.store(latency);
    m_rate.store(sample_rate);
    m_chunk = std::max(1.0, sample_rate * 0.032);
    m_chunk_end = m_chunk;
    m_position = 0;
    m_peak = 0.0F;
    m_count.store(0);
}

void ExampleEngine::process(const float* const* in,
                            float* const* out,
                            std::uint32_t frames,
                            std::uint32_t in_channels,
                            std::uint32_t out_channels) noexcept {
    constexpr std::size_t k_channels = Example::k_channels;
    frames = std::min<std::uint32_t>(frames, static_cast<std::uint32_t>(m_planes[0].size()));
    // The host's channels onto the example's: the same, downmixed, or the first repeated.
    for (std::size_t c = 0; c < k_channels; ++c) {
        float* plane = m_planes[c].data();
        if (in_channels == 0) {
            std::fill_n(plane, frames, 0.0F);
        } else if (k_channels == 1 && in_channels > 1) {
            const float scale = 1.0F / static_cast<float>(in_channels);
            for (std::uint32_t i = 0; i < frames; ++i) {
                float sum = 0.0F;
                for (std::uint32_t h = 0; h < in_channels; ++h) { sum += in[h][i]; }
                plane[i] = sum * scale;
            }
        } else {
            std::copy_n(in[std::min<std::size_t>(c, in_channels - 1)], frames, plane);
        }
    }
    record(frames);  // the input's peak, before the model changes it

    const std::size_t choice = m_backend.load();
    const bool runs = m_prepared && m_backends.runs(choice, m_anyway.load());
    constexpr anira::examples::Kind k_kind = anira::examples::kind_of<Example>();
    if constexpr (k_kind == anira::examples::Kind::Analyzer) {
        if (runs) { run(frames, choice); }  // it only reads: the input passes through
    } else {
        if constexpr (k_kind == anira::examples::Kind::Generator) {  // sound from nothing
            for (float* channel : m_channels) { std::fill_n(channel, frames, 0.0F); }
        }
        m_dry_wet.push(m_channels, frames);
        if (runs) {
            run(frames, choice);  // in place
            m_dry_wet.mix(m_channels, frames, m_mix.load());
        } else {
            m_dry_wet.dry(m_channels, frames);
        }
    }

    // The example's channels onto the host's: the same, spread, or averaged.
    for (std::uint32_t h = 0; h < out_channels; ++h) {
        float* target = out[h];
        if (k_channels == 2 && out_channels == 1) {
            for (std::uint32_t i = 0; i < frames; ++i) {
                target[i] = 0.5F * (m_planes[0][i] + m_planes[1][i]);
            }
        } else {
            std::copy_n(m_planes[std::min<std::size_t>(h, k_channels - 1)].data(), frames, target);
        }
    }
}

void ExampleEngine::run(std::uint32_t frames, std::size_t choice) noexcept {
    m_backends.select(*m_example, choice);
    for (std::size_t i = 0; i < m_values.size(); ++i) {
        m_example->set_parameter(i, m_values[i].load());
    }
    m_audio.shape[1] = static_cast<std::int64_t>(frames);
    m_example->process(m_audio, false);
}

void ExampleEngine::record(std::uint32_t frames) noexcept {
    for (std::uint32_t i = 0; i < frames; ++i, ++m_position) {
        for (const std::vector<float>& plane : m_planes) {
            m_peak = std::max(m_peak, std::fabs(plane[i]));
        }
        if (static_cast<double>(m_position + 1) >= m_chunk_end) {
            const std::int64_t count = m_count.load(std::memory_order_relaxed);
            const auto slot = static_cast<std::size_t>(count) % k_timeline_capacity;
            m_peaks[slot].store(m_peak, std::memory_order_relaxed);
            for (std::size_t m = 0; m < k_meters; ++m) {
                m_meter_values[m][slot].store(meter(m), std::memory_order_relaxed);
            }
            m_count.store(count + 1, std::memory_order_release);
            m_peak = 0.0F;
            m_chunk_end += m_chunk;
        }
    }
}

void ExampleEngine::set_parameter(std::size_t index, float value) noexcept {
    if (index >= m_values.size()) { return; }
    const anira::examples::Parameter& p = Example::k_parameters[index];
    m_values[index].store(std::clamp(value, p.m_min, p.m_max));
}

float ExampleEngine::parameter(std::size_t index) const noexcept {
    return index < m_values.size() ? m_values[index].load() : 0.0F;
}

void ExampleEngine::set_backend(std::size_t choice) noexcept {
    if (choice < m_backends.choices().size()) { m_backend.store(choice); }
}

float ExampleEngine::meter(std::size_t index) const noexcept {
    return m_example && m_prepared ? anira::examples::meter_value(*m_example, index) : 0.0F;
}

std::string ExampleEngine::status() const {
    if (!m_error.empty()) { return "The model does not run: " + m_error; }
    return m_backends.verdict_line(m_backend.load());
}

bool ExampleEngine::waits_for_consent() const noexcept {
    return !m_anyway.load() && m_backends.waits_for_consent(m_backend.load());
}

void ExampleEngine::transcript(anira::examples::Transcript& out) const {
    out.m_lines.clear();
    if (m_example) { anira::examples::transcript_of(*m_example, out); }
}

void ExampleEngine::try_nonetheless() noexcept {
    m_backends.try_nonetheless(m_backend.load());
}

void ExampleEngine::timelines(std::vector<float>& peaks,
                              std::array<std::vector<float>, k_meters>& meters,
                              std::uint32_t max_count) const {
    const std::int64_t count = m_count.load(std::memory_order_acquire);
    const auto n =
        std::min<std::int64_t>({count, max_count, static_cast<std::int64_t>(k_timeline_capacity)});
    const std::int64_t start = count - n;
    peaks.resize(static_cast<std::size_t>(n));
    for (std::vector<float>& values : meters) { values.resize(static_cast<std::size_t>(n)); }
    for (std::int64_t k = 0; k < n; ++k) {
        const auto slot = static_cast<std::size_t>(start + k) % k_timeline_capacity;
        peaks[static_cast<std::size_t>(k)] = m_peaks[slot].load(std::memory_order_relaxed);
        for (std::size_t m = 0; m < k_meters; ++m) {
            meters[m][static_cast<std::size_t>(k)] =
                m_meter_values[m][slot].load(std::memory_order_relaxed);
        }
    }
}
