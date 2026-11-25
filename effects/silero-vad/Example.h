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
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace anira::examples {

/// Silero VAD (models' silero_vad::Vad, weights MIT), an analyzer: the probability that someone
/// speaks, and whether they do (with hysteresis below "Threshold"), as of the audio get_latency()
/// samples earlier. The audio passes through untouched.
class Example {
public:
    static constexpr std::string_view k_name = "Silero VAD";
    static constexpr Kind k_kind = Kind::Analyzer;
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
        start();
    }
    void reset() {
        m_vad.reset();
        start();
    }

    anira_status process(const anira::Tensor& audio, bool offline) noexcept {
        const float* samples = models::core::channel(audio, 0);
        const std::size_t n = std::min(models::core::samples(audio), m_probability.size());
        if (samples == nullptr) { return ANIRA_ERROR_INVALID_ARGUMENT; }
        m_vad.set_non_realtime(offline);
        const anira_status status = m_vad.process(std::span<const float>(samples, n),
                                                  std::span<float>(m_probability.data(), n));
        const float on = m_threshold;
        const float off = std::max(0.0F, on - k_hysteresis);
        for (std::size_t i = 0; i < n; ++i) {
            m_speech = m_speech ? m_probability[i] >= off : m_probability[i] >= on;
        }
        m_meter_probability.store(n > 0 ? m_probability[n - 1] : 0.0F, std::memory_order_relaxed);
        m_meter_speech.store(m_speech, std::memory_order_relaxed);
        return status;
    }
    void set_parameter(std::size_t index, float value) noexcept {
        if (index == 0) { m_threshold = std::clamp(value, 0.0F, 1.0F); }
    }

    [[nodiscard]] float meter(std::size_t index) const noexcept {
        return index == 0 ? m_meter_probability.load(std::memory_order_relaxed)
                          : (m_meter_speech.load(std::memory_order_relaxed) ? 1.0F : 0.0F);
    }

    /// How far its results trail the audio (an analyzer: the targets report none to the host).
    [[nodiscard]] std::size_t get_latency() const noexcept { return m_vad.get_latency(); }
    [[nodiscard]] anira::PlanReport plan_report(std::size_t /*handler*/) const {
        return m_vad.plan_report();
    }
    void set_plan(std::size_t /*handler*/, std::uint32_t plan) noexcept { m_vad.set_plan(plan); }

private:
    static constexpr float k_hysteresis = 0.15F;  ///< speech ends this far below the threshold

    void start() {
        m_speech = false;
        m_meter_probability.store(0.0F);
        m_meter_speech.store(false);
    }

    models::silero_vad::Vad m_vad;
    std::vector<float> m_probability;
    float m_threshold = 0.5F;
    bool m_speech = false;
    std::atomic<float> m_meter_probability{0.0F};
    std::atomic<bool> m_meter_speech{false};
};

}  // namespace anira::examples
