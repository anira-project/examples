// SPDX-License-Identifier: Apache-2.0

#include <host/ExampleHost.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {
constexpr std::uint32_t k_max_channels = 8;
}  // namespace

void ExampleHost::prepare_to_play(uint32_t sample_rate, uint32_t buffer_size) {
    m_in.assign(k_max_channels, std::vector<float>(buffer_size, 0.0F));
    m_out.assign(k_max_channels, std::vector<float>(buffer_size, 0.0F));
    m_in_planes.assign(k_max_channels, nullptr);
    m_out_planes.assign(k_max_channels, nullptr);
    for (std::uint32_t c = 0; c < k_max_channels; ++c) {
        m_in_planes[c] = m_in[c].data();
        m_out_planes[c] = m_out[c].data();
    }
    m_engine.prepare(sample_rate, buffer_size);
}

void ExampleHost::process(float* output_buffer,
                          const float* input_buffer,
                          uint32_t frame_count,
                          uint32_t num_input_channels,
                          uint32_t num_output_channels) {
    if (m_in.empty()) { return; }
    const std::uint32_t frames =
        std::min<std::uint32_t>(frame_count, static_cast<std::uint32_t>(m_in[0].size()));
    const std::uint32_t ins =
        input_buffer != nullptr ? std::min(num_input_channels, k_max_channels) : 0;
    const std::uint32_t outs = std::min(num_output_channels, k_max_channels);
    for (std::uint32_t i = 0; i < frames; ++i) {
        for (std::uint32_t c = 0; c < ins; ++c) {
            m_in[c][i] = input_buffer[(i * num_input_channels) + c];
        }
    }
    m_engine.process(m_in_planes.data(), m_out_planes.data(), frames, ins, outs);
    if (output_buffer == nullptr) { return; }
    for (std::uint32_t i = 0; i < frames; ++i) {
        for (std::uint32_t c = 0; c < num_output_channels; ++c) {
            output_buffer[(i * num_output_channels) + c] = c < outs ? m_out[c][i] : 0.0F;
        }
    }
}
