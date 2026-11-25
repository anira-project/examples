// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/anira.h>
#include <anira/examples/Example.h>
#include <anira/models/core/Latents.h>
#include <anira/models/rave_funk_drum/RaveFunkDrum.h>

#include <anira/anira.hpp>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace anira::examples {

/// RAVE funk drum (weights CC BY-NC 4.0) as an encoder and a decoder handler, as anira's RAVE
/// example: the encoder turns the input into a latent frame per 2048 samples (at 44.1 kHz), the
/// "Latent 0" to "Latent 3" offsets move it in the latent space, and the decoder turns it back
/// into audio.
class Example {
public:
    static constexpr std::string_view k_name = "RAVE Funk Drum Latents";
    static constexpr int k_channels = 1;
    static constexpr std::size_t k_handlers = 2;  // the encoder, the decoder
    static constexpr std::array<Parameter, models::rave_funk_drum::k_latents> k_parameters{{
        {.m_id = "latent_0", .m_name = "Latent 0", .m_min = -1.0F, .m_max = 1.0F},
        {.m_id = "latent_1", .m_name = "Latent 1", .m_min = -1.0F, .m_max = 1.0F},
        {.m_id = "latent_2", .m_name = "Latent 2", .m_min = -1.0F, .m_max = 1.0F},
        {.m_id = "latent_3", .m_name = "Latent 3", .m_min = -1.0F, .m_max = 1.0F},
    }};

    Example(anira::Context& context, const models::core::ModelSource& weights)
        : m_encoder(context, weights), m_decoder(context, weights) {}

    void prepare(double sample_rate, std::size_t max_block) {
        m_encoder.prepare(sample_rate, max_block);
        m_decoder.prepare(sample_rate, max_block);
        // A frame is popped once its hop of audio is in, get_latency() frames late; the decoder
        // sounds it get_latency() samples after its push.
        const double hop =
            static_cast<double>(m_encoder.hop()) * sample_rate / m_encoder.model_rate();
        m_latency = static_cast<std::size_t>(
                        std::lround(static_cast<double>(m_encoder.get_latency() + 1) * hop)) +
                    m_decoder.get_latency();
    }
    void reset() {
        m_encoder.reset();
        m_decoder.reset();
    }

    anira_status process(const anira::Tensor& audio, bool offline) noexcept {
        m_encoder.push_data(audio);
        while (m_encoder.frames_due() > 0) {
            // A missed frame still takes its place in the stream (anira's miss policy filled it).
            models::core::Latents frame;
            const anira_status popped = offline
                                            ? m_encoder.pop_data_wait(frame, k_offline_timeout_ms)
                                            : m_encoder.pop_data(frame);
            if (popped != ANIRA_OK && popped != ANIRA_MISSED) { break; }
            for (std::size_t i = 0; i < m_offsets.size(); ++i) {
                frame.m_values[i] += m_offsets[i];
            }
            m_decoder.push_data(std::span<const float>(frame.m_values.data(), m_offsets.size()));
        }
        return offline ? m_decoder.pop_data_wait(audio, k_offline_timeout_ms)
                       : m_decoder.pop_data(audio);
    }
    void set_parameter(std::size_t index, float value) noexcept {
        if (index < m_offsets.size()) { m_offsets[index] = value; }
    }

    [[nodiscard]] std::size_t get_latency() const noexcept { return m_latency; }
    [[nodiscard]] anira::PlanReport plan_report(std::size_t handler) const {
        return handler == 0 ? m_encoder.plan_report() : m_decoder.plan_report();
    }
    void set_plan(std::size_t handler, std::uint32_t plan) noexcept {
        if (handler == 0) {
            m_encoder.set_plan(plan);
        } else {
            m_decoder.set_plan(plan);
        }
    }

private:
    models::rave_funk_drum::RaveFunkDrumEncoder m_encoder;
    models::rave_funk_drum::RaveFunkDrumDecoder m_decoder;
    std::array<float, models::rave_funk_drum::k_latents> m_offsets{};
    std::size_t m_latency = 0;
};

}  // namespace anira::examples
