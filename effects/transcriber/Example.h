// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/anira.h>
#include <anira/examples/Example.h>
#include <anira/examples/Transcript.h>
#include <anira/examples/Weights.h>
#include <anira/models/core/HostAudio.h>
#include <anira/models/core/Segment.h>
#include <anira/models/moonshine_streaming/Moonshine.h>
#include <anira/models/nemotron_diarization/Diarizer.h>
#include <anira/models/parakeet_tdt/Parakeet.h>
#include <anira/models/silero_vad/Vad.h>
#include <tanh/dsp/utils/Resampler.h>

#include <algorithm>
#include <anira/anira.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <ranges>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace anira::examples {

/// The speech-to-text app's way: fast and live, then accurate, with speakers, each part on or off.
/// Live: Moonshine tiny in the chosen language (English or German; Auto has no live lines) writes
/// the words while someone speaks. Accurate: once Silero VAD ends the stretch, Parakeet TDT 0.6B v3
/// (25 European languages, detected) transcribes it on a thread of the example's own and its line
/// replaces Moonshine's, which show as not final until then. Speakers: Nemotron 3 Diarization gives
/// every line the speaker whose turn overlaps it most. Both Moonshines are loaded, so the language
/// switches at once. An analyzer: the audio passes through untouched.
class Example {
public:
    static constexpr std::string_view k_name = "Transcriber";
    static constexpr Kind k_kind = Kind::Analyzer;
    static constexpr int k_channels = 1;
    static constexpr std::size_t k_handlers = 0;  // its models pick their engines (jobs)
    static constexpr std::array<std::string_view, 3> k_languages{"Auto", "English", "German"};
    static constexpr std::array<std::string_view, 2> k_off_on{"Off", "On"};
    static constexpr std::array<Parameter, 5> k_parameters{{
        {.m_id = "language",
         .m_name = "Language",
         .m_max = 2.0F,
         .m_default = 0.0F,
         .m_choices = k_languages},
        {.m_id = "live", .m_name = "Live (Moonshine)", .m_default = 1.0F, .m_choices = k_off_on},
        {.m_id = "accurate",
         .m_name = "Accurate (Parakeet)",
         .m_default = 1.0F,
         .m_choices = k_off_on},
        {.m_id = "speakers", .m_name = "Speakers", .m_default = 1.0F, .m_choices = k_off_on},
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
        : m_moonshine{{{context, weights, models::moonshine_streaming::Variant::TinyEn},
                       {context, weights, models::moonshine_streaming::Variant::TinyDe}}}
        , m_parakeet(context, anira::examples::weights("parakeet-tdt-0.6b-v3"))
        , m_diarizer(context, anira::examples::weights("nemotron-3-diarization"))
        , m_vad(context, anira::examples::weights("silero-vad")) {
        m_parakeet.prepare();
        m_worker = std::thread([this] { work(); });
    }
    ~Example() {
        m_running.store(false);
        if (m_worker.joinable()) { m_worker.join(); }
    }
    Example(const Example&) = delete;
    Example& operator=(const Example&) = delete;
    Example(Example&&) = delete;
    Example& operator=(Example&&) = delete;

    void prepare(double sample_rate, std::size_t max_block) {
        const std::scoped_lock lock(m_mutex);  // the worker reads the ring and the VAD
        m_vad.prepare(sample_rate, max_block);
        for (auto& moonshine : m_moonshine) { moonshine.prepare(sample_rate, max_block); }
        m_diarizer.prepare(sample_rate, max_block);
        m_probability.assign(max_block, 0.0F);
        m_ring.assign(static_cast<std::size_t>(sample_rate * k_ring_seconds), 0.0F);
        m_rate = sample_rate;
        start();
    }
    void reset() {
        const std::scoped_lock lock(m_mutex);
        m_vad.reset();
        for (auto& moonshine : m_moonshine) { moonshine.reset(); }
        m_diarizer.reset();
        start();
    }

    anira_status process(const anira::Tensor& audio, bool /*offline*/) noexcept {
        const float* samples = models::core::channel(audio, 0);
        const std::size_t n = std::min(models::core::samples(audio), m_probability.size());
        if (samples == nullptr || m_ring.empty()) { return ANIRA_ERROR_INVALID_ARGUMENT; }
        const std::span<const float> mono(samples, n);
        // The live Moonshine: none for Auto (0), English (1), German (2). A switch ends the
        // previous one's segment.
        const int live = m_live_on ? m_language - 1 : -1;
        if (live != m_live_model && m_live_model >= 0) {
            m_moonshine[static_cast<std::size_t>(m_live_model)].end_segment();
        }
        m_live_model = live;
        if (live >= 0) { m_moonshine[static_cast<std::size_t>(live)].process(mono); }
        if (m_speakers_on.load(std::memory_order_relaxed)) { m_diarizer.process(mono); }
        m_vad.process(mono, std::span<float>(m_probability.data(), n));
        // Moonshine's segment ends where the speech ends.
        const float on = m_threshold;
        const float off = std::max(0.0F, on - 0.15F);
        for (std::size_t i = 0; i < n; ++i) {
            const bool speech = m_speech ? m_probability[i] >= off : m_probability[i] >= on;
            if (m_speech && !speech && live >= 0) {
                m_moonshine[static_cast<std::size_t>(live)].end_segment();
            }
            m_speech = speech;
        }
        // The host's audio, for the worker to cut the VAD's stretches from.
        const std::int64_t written = m_written.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < n; ++i) {
            m_ring[static_cast<std::size_t>(written + static_cast<std::int64_t>(i)) %
                   m_ring.size()] = mono[i];
        }
        m_written.store(written + static_cast<std::int64_t>(n), std::memory_order_release);
        m_meter.store(n > 0 ? m_probability[n - 1] : 0.0F, std::memory_order_relaxed);
        return ANIRA_OK;
    }
    void set_parameter(std::size_t index, float value) noexcept {
        switch (index) {
            case 0: m_language = std::clamp(static_cast<int>(std::lround(value)), 0, 2); break;
            case 1: m_live_on = value >= 0.5F; break;
            case 2: m_accurate_on.store(value >= 0.5F, std::memory_order_relaxed); break;
            case 3: m_speakers_on.store(value >= 0.5F, std::memory_order_relaxed); break;
            case 4: m_threshold = std::clamp(value, 0.0F, 1.0F); break;
            default: break;
        }
    }

    /// UI thread (Moonshine's and the diarizer's reader): Parakeet's lines, Moonshine's where
    /// Parakeet has none yet, each with its speaker.
    void transcript(Transcript& out) const {
        models::core::Segment segment;
        for (std::size_t m = 0; m < m_moonshine.size(); ++m) {
            while (m_moonshine[m].pop(segment)) {
                segment.m_id += static_cast<std::uint64_t>(m) << 48U;  // the two models' ids apart
                m_live.add(segment, m_rate);
            }
        }
        while (m_diarizer.pop(segment)) {
            m_turns.push_back(segment);
            if (m_turns.size() > k_keep) { m_turns.pop_front(); }
        }
        Transcript live;
        m_live.fill(live);
        {
            const std::scoped_lock lock(m_finals_mutex);
            out.m_lines.assign(m_finals.begin(), m_finals.end());
        }
        // Parakeet's lines in their order; then Moonshine's that no Parakeet line covers yet and
        // that are newer than the newest Parakeet line (an older one was replaced, or its stretch
        // was too short to transcribe). Without Parakeet, Moonshine's lines as they are.
        const bool accurate = m_accurate_on.load(std::memory_order_relaxed);
        std::ranges::sort(out.m_lines, {}, &Transcript::Line::m_seconds);
        const double newest = out.m_lines.empty() ? -1.0 : out.m_lines.back().m_end_seconds;
        std::vector<Transcript::Line> pending;
        for (Transcript::Line line : live.m_lines) {
            if (accurate) {
                const bool covered =
                    std::ranges::any_of(out.m_lines, [&](const Transcript::Line& f) {
                        return line.m_end_seconds > f.m_seconds && line.m_seconds < f.m_end_seconds;
                    });
                if (covered || line.m_end_seconds <= newest) { continue; }
                line.m_final = false;  // Parakeet's line is still to come
            }
            pending.push_back(std::move(line));
        }
        std::ranges::sort(pending, {}, &Transcript::Line::m_seconds);
        out.m_lines.insert(out.m_lines.end(), pending.begin(), pending.end());
        if (!m_speakers_on.load(std::memory_order_relaxed)) { return; }
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
        return m_moonshine[0].plan_report();
    }
    void set_plan(std::size_t /*handler*/, std::uint32_t /*plan*/) noexcept {}

private:
    static constexpr double k_ring_seconds = 40.0;  // longer than Parakeet's longest window
    static constexpr std::size_t k_keep = 256;      // lines and turns kept

    void start() {
        m_live_model = -1;
        m_written.store(0);
        m_speech = false;
        m_live.clear();
        m_turns.clear();
        const std::scoped_lock lock(m_finals_mutex);
        m_finals.clear();
    }

    /// The worker: every stretch the VAD ends, cut from the ring, resampled to 16 kHz, transcribed.
    void work() {
        std::uint64_t id = 0;
        while (m_running.load()) {
            {
                const std::scoped_lock lock(m_mutex);
                models::silero_vad::SpeechSpan span;
                while (m_vad.pop(span)) {
                    if (m_accurate_on.load()) { transcribe(span, id++); }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void transcribe(const models::silero_vad::SpeechSpan& span, std::uint64_t id) {
        const std::int64_t written = m_written.load(std::memory_order_acquire);
        const auto size = static_cast<std::int64_t>(m_ring.size());
        const std::int64_t start = std::max(span.m_start, written - size);
        const std::int64_t end = std::min(span.m_end, written);
        if (end - start < static_cast<std::int64_t>(m_rate * 0.2)) { return; }
        std::vector<float> host(static_cast<std::size_t>(end - start));
        for (std::int64_t i = start; i < end; ++i) {
            host[static_cast<std::size_t>(i - start)] = m_ring[static_cast<std::size_t>(i % size)];
        }
        thl::dsp::utils::Resampler resampler;
        resampler.prepare(m_rate, 16000.0, host.size());
        std::vector<float> audio(resampler.max_output(host.size()));
        audio.resize(resampler.process(host, audio));
        audio.resize(std::min(audio.size(), models::parakeet_tdt::k_max_samples));
        try {
            const models::core::Segment segment = m_parakeet.transcribe(audio, 0, id);
            if (segment.m_text.find_first_not_of(" \t\n") == std::string::npos) { return; }
            const std::scoped_lock lock(m_finals_mutex);
            m_finals.push_back({.m_text = segment.m_text,
                                .m_seconds = static_cast<double>(start) / m_rate,
                                .m_end_seconds = static_cast<double>(end) / m_rate});
            if (m_finals.size() > k_keep) { m_finals.pop_front(); }
        } catch (const std::exception&) {
            m_failed.fetch_add(1);  // a failed job: Moonshine's line stays
        }
    }

    mutable std::array<models::moonshine_streaming::Moonshine, 2> m_moonshine;  // English, German
    models::parakeet_tdt::Parakeet m_parakeet;
    mutable models::nemotron_diarization::Diarizer m_diarizer;
    models::silero_vad::Vad m_vad;

    mutable TranscriptBook m_live{k_keep};  // Moonshine's lines
    mutable std::deque<models::core::Segment> m_turns;
    mutable std::mutex m_finals_mutex;
    std::deque<Transcript::Line> m_finals;  // Parakeet's lines

    std::mutex m_mutex;  // the worker against prepare/reset
    std::vector<float> m_ring;
    std::atomic<std::int64_t> m_written{0};
    std::vector<float> m_probability;
    double m_rate = 48000.0;
    float m_threshold = 0.5F;
    int m_language = 0;  // the parameter: Auto, English, German
    bool m_live_on = true;
    std::atomic<bool> m_accurate_on{true};  // the worker reads it
    std::atomic<bool> m_speakers_on{true};  // the UI thread reads it
    int m_live_model = -1;                  // the Moonshine running (-1: none)
    bool m_speech = false;
    std::atomic<float> m_meter{0.0F};
    std::atomic<std::size_t> m_failed{0};
    std::atomic<bool> m_running{true};
    std::thread m_worker;
};

}  // namespace anira::examples
