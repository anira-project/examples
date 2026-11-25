// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/anira.h>
#include <anira/examples/Example.h>
#include <anira/models/core/HostAudio.h>
#include <anira/models/silero_vad/Vad.h>

#include <algorithm>
#include <anira/anira.hpp>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace anira::examples {

/// Silero VAD (models' silero_vad::Vad, weights MIT) as a speech gate: the model gives the speech
/// probability of every sample, get_latency() late, and the input, delayed by as much, is muted
/// while the probability (with hysteresis below "Threshold") says nobody speaks. The meters show
/// the probability and whether the gate is open.
class Example {
public:
    static constexpr std::string_view k_name = "Silero VAD";
    static constexpr int k_channels = 1;
    static constexpr std::size_t k_handlers = 1;
    static constexpr std::array<Parameter, 1> k_parameters{{
        {.m_id = "threshold",
         .m_name = "Threshold",
         .m_min = 0.0F,
         .m_max = 1.0F,
         .m_default = 0.5F},
    }};
    static constexpr std::array<Meter, 2> k_meters{{
        {.m_id = "probability", .m_name = "Speech probability"},
        {.m_id = "speech", .m_name = "Speech"},
    }};

    Example(anira::Context& context, const models::core::ModelSource& weights)
        : m_vad(context, weights) {}

    void prepare(double sample_rate, std::size_t max_block) {
        m_vad.prepare(sample_rate, max_block);
        m_probability.assign(max_block, 0.0F);
        m_delay.assign(std::max<std::size_t>(m_vad.get_latency(), 1), 0.0F);
        m_attack = smoothing(0.005, sample_rate);
        m_release = smoothing(0.08, sample_rate);
        start();
    }
    void reset() {
        m_vad.reset();
        start();
    }

    anira_status process(const anira::Tensor& audio, bool offline) noexcept {
        float* samples = models::core::channel(audio, 0);
        const std::size_t n = std::min(models::core::samples(audio), m_probability.size());
        if (samples == nullptr) { return ANIRA_ERROR_INVALID_ARGUMENT; }
        m_vad.set_non_realtime(offline);
        const anira_status status = m_vad.process(std::span<const float>(samples, n),
                                                  std::span<float>(m_probability.data(), n));
        // The probability of sample i belongs to the input get_latency() samples earlier: the
        // delayed input, gated by it.
        const float on = m_threshold;
        const float off = std::max(0.0F, on - k_hysteresis);
        const bool delayed = m_vad.get_latency() > 0;
        for (std::size_t i = 0; i < n; ++i) {
            const float p = m_probability[i];
            m_open = m_open ? p >= off : p >= on;
            float sample = samples[i];
            if (delayed) {
                std::swap(sample, m_delay[m_position]);
                m_position = (m_position + 1) % m_delay.size();
            }
            const float target = m_open ? 1.0F : 0.0F;
            m_gain = target + ((m_gain - target) * (target > m_gain ? m_attack : m_release));
            samples[i] = sample * m_gain;
        }
        m_meter_probability.store(n > 0 ? m_probability[n - 1] : 0.0F, std::memory_order_relaxed);
        m_meter_open.store(m_open, std::memory_order_relaxed);
        return status;
    }
    void set_parameter(std::size_t index, float value) noexcept {
        if (index == 0) { m_threshold = std::clamp(value, 0.0F, 1.0F); }
    }

    [[nodiscard]] float meter(std::size_t index) const noexcept {
        return index == 0 ? m_meter_probability.load(std::memory_order_relaxed)
                          : (m_meter_open.load(std::memory_order_relaxed) ? 1.0F : 0.0F);
    }

    [[nodiscard]] std::size_t get_latency() const noexcept { return m_vad.get_latency(); }
    [[nodiscard]] anira::PlanReport plan_report(std::size_t /*handler*/) const {
        return m_vad.plan_report();
    }
    void set_plan(std::size_t /*handler*/, std::uint32_t plan) noexcept { m_vad.set_plan(plan); }

private:
    static constexpr float k_hysteresis = 0.15F;  ///< speech ends this far below the threshold

    static float smoothing(double seconds, double sample_rate) {
        return static_cast<float>(std::exp(-1.0 / (seconds * sample_rate)));
    }
    void start() {
        std::ranges::fill(m_delay, 0.0F);
        m_position = 0;
        m_open = false;
        m_gain = 0.0F;
        m_meter_probability.store(0.0F);
        m_meter_open.store(false);
    }

    models::silero_vad::Vad m_vad;
    std::vector<float> m_probability;
    std::vector<float> m_delay;
    std::size_t m_position = 0;
    float m_threshold = 0.5F;
    bool m_open = false;
    float m_gain = 0.0F;
    float m_attack = 0.0F;
    float m_release = 0.0F;
    std::atomic<float> m_meter_probability{0.0F};
    std::atomic<bool> m_meter_open{false};
};

}  // namespace anira::examples
