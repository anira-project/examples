// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/anira.h>
#include <anira/examples/Example.h>
#include <anira/examples/Transcript.h>
#include <anira/examples/Weights.h>
#include <anira/models/core/HostAudio.h>
#include <anira/models/core/Segment.h>
#include <anira/models/parakeet_tdt/Parakeet.h>
#include <anira/models/silero_vad/Vad.h>
#include <tanh/dsp/utils/Resampler.h>

#include <algorithm>
#include <anira/anira.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace anira::examples {

/// Parakeet TDT 0.6B v3 (models' parakeet_tdt::Parakeet, 25 European languages, weights CC BY 4.0),
/// an analyzer: Silero VAD finds the speech, and a thread of the example's own transcribes each
/// stretch once it ends (accurate, a line per stretch, no revisions). The audio passes through
/// untouched.
class Example {
public:
    static constexpr std::string_view k_name = "Parakeet";
    static constexpr Kind k_kind = Kind::Analyzer;
    static constexpr int k_channels = 1;
    static constexpr std::size_t k_handlers = 0;  // its models pick their engines (jobs)
    static constexpr std::array<Parameter, 0> k_parameters{};
    static constexpr std::array<Meter, 1> k_meters{{
        {.m_id = "probability", .m_name = "Speech probability"},
    }};

    Example(anira::Context& context, const models::core::ModelSource& weights)
        : m_parakeet(context, weights), m_vad(context, anira::examples::weights("silero-vad")) {
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
        const std::scoped_lock lock(m_mutex);  // the worker reads the ring
        m_vad.prepare(sample_rate, max_block);
        m_ring.assign(static_cast<std::size_t>(sample_rate * k_ring_seconds), 0.0F);
        m_written.store(0);
        m_rate = sample_rate;
        m_book.clear();
    }
    void reset() {
        const std::scoped_lock lock(m_mutex);
        m_vad.reset();
        m_written.store(0);
        m_book.clear();
    }

    anira_status process(const anira::Tensor& audio, bool offline) noexcept {
        const float* samples = models::core::channel(audio, 0);
        const std::size_t n = models::core::samples(audio);
        if (samples == nullptr || m_ring.empty()) { return ANIRA_ERROR_INVALID_ARGUMENT; }
        m_vad.set_non_realtime(offline);
        m_vad.process(std::span<const float>(samples, n));
        // The host's audio, for the worker to cut the spans from.
        const std::int64_t written = m_written.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < n; ++i) {
            m_ring[static_cast<std::size_t>(written + static_cast<std::int64_t>(i)) %
                   m_ring.size()] = samples[i];
        }
        m_written.store(written + static_cast<std::int64_t>(n), std::memory_order_release);
        m_meter.store(m_vad.latest().m_value, std::memory_order_relaxed);
        return ANIRA_OK;
    }
    void set_parameter(std::size_t /*index*/, float /*value*/) noexcept {}

    /// Stretches lost to a failed job.
    [[nodiscard]] std::size_t failed() const noexcept { return m_failed.load(); }

    /// UI thread.
    void transcript(Transcript& out) const { m_book.fill(out); }

    [[nodiscard]] float meter(std::size_t /*index*/) const noexcept {
        return m_meter.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t get_latency() const noexcept { return m_vad.get_latency(); }
    [[nodiscard]] anira::PlanReport plan_report(std::size_t /*handler*/) const {
        return m_vad.plan_report();
    }
    void set_plan(std::size_t /*handler*/, std::uint32_t plan) noexcept { m_vad.set_plan(plan); }

private:
    static constexpr double k_ring_seconds = 40.0;  // longer than Parakeet's longest window (31 s)

    /// The worker: every span the VAD ends, cut from the ring, resampled to 16 kHz, transcribed.
    void work() {
        std::uint64_t id = 0;
        while (m_running.load()) {
            {
                const std::scoped_lock lock(m_mutex);
                models::silero_vad::SpeechSpan span;
                while (m_vad.pop(span)) { transcribe(span, id++); }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void transcribe(const models::silero_vad::SpeechSpan& span, std::uint64_t id) {
        const std::int64_t written = m_written.load(std::memory_order_acquire);
        const auto size = static_cast<std::int64_t>(m_ring.size());
        const std::int64_t start = std::max(span.m_start, written - size);
        const std::int64_t end = std::min(span.m_end, written);
        if (end - start < static_cast<std::int64_t>(m_rate * 0.2)) { return; }  // too short
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
            models::core::Segment segment = m_parakeet.transcribe(audio, 0, id);
            segment.m_start_sample = start;  // host samples, as the book counts them
            segment.m_end_sample = end;
            if (!segment.m_text.empty()) { m_book.add(segment, m_rate); }
        } catch (const std::exception&) {
            m_failed.fetch_add(1);  // a failed job: the next stretch starts clean
        }
    }

    models::parakeet_tdt::Parakeet m_parakeet;
    models::silero_vad::Vad m_vad;
    mutable TranscriptBook m_book;
    std::mutex m_mutex;  // the worker against prepare/reset
    std::vector<float> m_ring;
    std::atomic<std::int64_t> m_written{0};
    double m_rate = 48000.0;
    std::atomic<float> m_meter{0.0F};
    std::atomic<bool> m_running{true};
    std::atomic<std::size_t> m_failed{0};  // stretches lost to a failed job
    std::thread m_worker;
};

}  // namespace anira::examples
