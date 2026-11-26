// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/anira.h>
#include <anira/examples/Example.h>
#include <anira/examples/Transcript.h>
#include <anira/examples/Weights.h>
#include <anira/models/core/HostAudio.h>
#include <anira/models/core/Segment.h>
#include <anira/models/moonshine_streaming/Moonshine.h>
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

/// Moonshine streaming (models' moonshine_streaming::Moonshine, tiny English, weights MIT), an
/// analyzer: the words while someone speaks, revised every 0.5 s, each line final once its segment
/// ends. Silero VAD ends a segment where the speech ends. The audio passes through untouched.
class Example {
public:
    static constexpr std::string_view k_name = "Moonshine";
    static constexpr Kind k_kind = Kind::Analyzer;
    static constexpr int k_channels = 1;
    static constexpr std::size_t k_handlers = 0;  // its models pick their engines (jobs)
    static constexpr std::array<Parameter, 1> k_parameters{{
        {.m_id = "threshold",
         .m_name = "Speech threshold",
         .m_min = 0.0F,
         .m_max = 1.0F,
         .m_default = 0.5F},
    }};
    static constexpr std::array<Meter, 1> k_meters{{
        {.m_id = "probability", .m_name = "Speech probability"},
    }};

    Example(anira::Context& context, const models::core::ModelSource& weights)
        : m_moonshine(context, weights, models::moonshine_streaming::Variant::TinyEn)
        , m_vad(context, anira::examples::weights("silero-vad")) {}

    void prepare(double sample_rate, std::size_t max_block) {
        m_vad.prepare(sample_rate, max_block);
        m_moonshine.prepare(sample_rate, max_block);
        m_probability.assign(max_block, 0.0F);
        m_rate = sample_rate;
        m_speech = false;
        m_book.clear();
    }
    void reset() {
        m_vad.reset();
        m_moonshine.reset();
        m_speech = false;
        m_book.clear();
    }

    anira_status process(const anira::Tensor& audio, bool /*offline*/) noexcept {
        const float* samples = models::core::channel(audio, 0);
        const std::size_t n = std::min(models::core::samples(audio), m_probability.size());
        if (samples == nullptr) { return ANIRA_ERROR_INVALID_ARGUMENT; }
        m_moonshine.process(std::span<const float>(samples, n));
        m_vad.process(std::span<const float>(samples, n),
                      std::span<float>(m_probability.data(), n));
        // The segment ends where the speech ends (the VAD's hysteresis below the threshold).
        const float on = m_threshold;
        const float off = std::max(0.0F, on - 0.15F);
        for (std::size_t i = 0; i < n; ++i) {
            const bool speech = m_speech ? m_probability[i] >= off : m_probability[i] >= on;
            if (m_speech && !speech) { m_moonshine.end_segment(); }
            m_speech = speech;
        }
        m_meter.store(n > 0 ? m_probability[n - 1] : 0.0F, std::memory_order_relaxed);
        return ANIRA_OK;
    }
    void set_parameter(std::size_t index, float value) noexcept {
        if (index == 0) { m_threshold = std::clamp(value, 0.0F, 1.0F); }
    }

    /// UI thread (Moonshine's reader): the revisions so far, then the lines.
    void transcript(Transcript& out) const {
        models::core::Segment segment;
        while (m_moonshine.pop(segment)) { m_book.add(segment, m_rate); }
        m_book.fill(out);
    }

    [[nodiscard]] float meter(std::size_t /*index*/) const noexcept {
        return m_meter.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t get_latency() const noexcept { return m_vad.get_latency(); }
    [[nodiscard]] anira::PlanReport plan_report(std::size_t /*handler*/) const {
        return m_moonshine.plan_report();
    }
    void set_plan(std::size_t /*handler*/, std::uint32_t plan) noexcept {
        m_moonshine.set_plan(plan);
    }

private:
    mutable models::moonshine_streaming::Moonshine m_moonshine;
    models::silero_vad::Vad m_vad;
    mutable TranscriptBook m_book;
    std::vector<float> m_probability;
    double m_rate = 48000.0;
    float m_threshold = 0.5F;
    bool m_speech = false;
    std::atomic<float> m_meter{0.0F};
};

}  // namespace anira::examples
