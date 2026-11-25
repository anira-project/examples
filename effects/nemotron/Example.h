// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/anira.h>
#include <anira/examples/Example.h>
#include <anira/examples/Transcript.h>
#include <anira/examples/Weights.h>
#include <anira/models/core/HostAudio.h>
#include <anira/models/core/Segment.h>
#include <anira/models/nemotron_asr_streaming/NemotronAsr.h>
#include <anira/models/nemotron_diarization/Diarizer.h>
#include <anira/models/silero_vad/Vad.h>

#include <algorithm>
#include <anira/anira.hpp>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string_view>
#include <vector>

namespace anira::examples {

/// Who says what: Nemotron 3.5 ASR Streaming (40 languages, the language detected, weights
/// OpenMDW-1.1) writes the words, revised every 560 ms, Silero VAD ends a line where the speech
/// ends, and Nemotron 3 Diarization (up to 8 speakers, CC BY 4.0) numbers the speakers; a line gets
/// the speaker whose turn overlaps it most. An analyzer: the audio passes through untouched.
class Example {
public:
    static constexpr std::string_view k_name = "Nemotron";
    static constexpr Kind k_kind = Kind::Analyzer;
    static constexpr int k_channels = 1;
    static constexpr std::size_t k_handlers = 1;  // the ASR encoder
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
        : m_asr(context, weights)
        , m_diarizer(context, anira::examples::weights("nemotron-3-diarization"))
        , m_vad(context, anira::examples::weights("silero-vad")) {}

    void prepare(double sample_rate, std::size_t max_block) {
        m_vad.prepare(sample_rate, max_block);
        m_asr.prepare(sample_rate, max_block);
        m_diarizer.prepare(sample_rate, max_block);
        m_probability.assign(max_block, 0.0F);
        m_rate = sample_rate;
        start();
    }
    void reset() {
        m_vad.reset();
        m_asr.reset();
        m_diarizer.reset();
        start();
    }

    anira_status process(const anira::Tensor& audio, bool /*offline*/) noexcept {
        const float* samples = models::core::channel(audio, 0);
        const std::size_t n = std::min(models::core::samples(audio), m_probability.size());
        if (samples == nullptr) { return ANIRA_ERROR_INVALID_ARGUMENT; }
        const std::span<const float> mono(samples, n);
        m_asr.process(mono);
        m_diarizer.process(mono);
        m_vad.process(mono, std::span<float>(m_probability.data(), n));
        const float on = m_threshold;
        const float off = std::max(0.0F, on - 0.15F);
        for (std::size_t i = 0; i < n; ++i) {
            const bool speech = m_speech ? m_probability[i] >= off : m_probability[i] >= on;
            if (m_speech && !speech) { m_asr.end_segment(); }
            m_speech = speech;
        }
        m_meter.store(n > 0 ? m_probability[n - 1] : 0.0F, std::memory_order_relaxed);
        return ANIRA_OK;
    }
    void set_parameter(std::size_t index, float value) noexcept {
        if (index == 0) { m_threshold = std::clamp(value, 0.0F, 1.0F); }
    }

    /// UI thread (the ASR's and the diarizer's reader): the lines, each with the speaker whose
    /// turn overlaps it most.
    void transcript(Transcript& out) const {
        models::core::Segment segment;
        while (m_asr.pop(segment)) { m_book.add(segment, m_rate); }
        while (m_diarizer.pop(segment)) {
            m_turns.push_back(segment);
            if (m_turns.size() > k_turns) { m_turns.pop_front(); }
        }
        m_book.fill(out);
        for (Transcript::Line& line : out.m_lines) {
            const auto start = static_cast<std::int64_t>(line.m_seconds * m_rate);
            const auto end = static_cast<std::int64_t>(line.m_end_seconds * m_rate);
            std::int64_t best = 0;
            for (const models::core::Segment& turn : m_turns) {
                const std::int64_t overlap =
                    std::min(end, turn.m_end_sample) - std::max(start, turn.m_start_sample);
                if (overlap > best) {
                    best = overlap;
                    line.m_speaker = turn.m_speaker;
                }
            }
        }
    }

    [[nodiscard]] float meter(std::size_t /*index*/) const noexcept {
        return m_meter.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t get_latency() const noexcept { return m_vad.get_latency(); }
    [[nodiscard]] anira::PlanReport plan_report(std::size_t /*handler*/) const {
        return m_asr.plan_report();
    }
    void set_plan(std::size_t /*handler*/, std::uint32_t plan) noexcept { m_asr.set_plan(plan); }

private:
    static constexpr std::size_t k_turns = 256;  // the speaker turns kept for the lines

    void start() {
        m_speech = false;
        m_book.clear();
        m_turns.clear();
    }

    mutable models::nemotron_asr_streaming::NemotronAsr m_asr;
    mutable models::nemotron_diarization::Diarizer m_diarizer;
    models::silero_vad::Vad m_vad;
    mutable TranscriptBook m_book;
    mutable std::deque<models::core::Segment> m_turns;
    std::vector<float> m_probability;
    double m_rate = 48000.0;
    float m_threshold = 0.5F;
    bool m_speech = false;
    std::atomic<float> m_meter{0.0F};
};

}  // namespace anira::examples
