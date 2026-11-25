// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace anira::examples {

/// The dry signal delayed by the model's latency, mixed with the wet one: a block's dry samples are
/// pushed before the model overwrites them, then mixed into the block after it (or given alone,
/// for Bypass). The mix ramps over a block.
class DryWet {
public:
    /// Main thread: `channels` lines for blocks of at most `max_block` samples and the latency.
    void prepare(std::size_t channels, std::size_t max_block, std::size_t latency) {
        m_size = latency + std::max<std::size_t>(max_block, 1);
        m_lines.assign(channels, std::vector<float>(m_size, 0.0F));
        m_write = 0;
        m_read = (m_size - latency) % m_size;
    }

    /// Audio thread: the block before the model runs on it.
    void push(std::span<float* const> channels, std::size_t samples) noexcept {
        std::size_t write = m_write;
        for (std::size_t c = 0; c < m_lines.size() && c < channels.size(); ++c) {
            write = m_write;
            for (std::size_t i = 0; i < samples; ++i) {
                m_lines[c][write] = channels[c][i];
                write = (write + 1) % m_size;
            }
        }
        m_write = write;
    }

    /// Audio thread: the wet block in `channels` mixed with the delayed dry one, `mix` 1 all wet.
    void mix(std::span<float* const> channels, std::size_t samples, float mix) noexcept {
        mix = std::clamp(mix, 0.0F, 1.0F);
        const float step = samples > 0 ? (mix - m_mix) / static_cast<float>(samples) : 0.0F;
        std::size_t read = m_read;
        for (std::size_t c = 0; c < m_lines.size() && c < channels.size(); ++c) {
            read = m_read;
            float wet = m_mix;
            for (std::size_t i = 0; i < samples; ++i) {
                wet += step;
                channels[c][i] = (wet * channels[c][i]) + ((1.0F - wet) * m_lines[c][read]);
                read = (read + 1) % m_size;
            }
        }
        m_read = read;
        m_mix = mix;
    }

    /// Audio thread: the delayed dry block alone (Bypass).
    void dry(std::span<float* const> channels, std::size_t samples) noexcept {
        std::size_t read = m_read;
        for (std::size_t c = 0; c < m_lines.size() && c < channels.size(); ++c) {
            read = m_read;
            for (std::size_t i = 0; i < samples; ++i) {
                channels[c][i] = m_lines[c][read];
                read = (read + 1) % m_size;
            }
        }
        m_read = read;
    }

private:
    std::vector<std::vector<float>> m_lines;
    std::size_t m_size = 1;
    std::size_t m_write = 0;
    std::size_t m_read = 0;
    float m_mix = 1.0F;
};

}  // namespace anira::examples
