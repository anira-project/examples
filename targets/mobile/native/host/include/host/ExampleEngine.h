// SPDX-License-Identifier: Apache-2.0

#pragma once

// clang-format off
#include <anira/anira.hpp>
#include ANIRA_EXAMPLE_HEADER  // IWYU pragma: export
// clang-format on
#include <anira/examples/Backends.h>
#include <anira/examples/DryWet.h>
#include <anira/examples/Example.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * ExampleEngine — any example of the examples repository (ANIRA_EXAMPLE_HEADER, chosen in
 * app.json) the way every target runs it: its parameters, the backend choice with anira's
 * real-time verdict ("Try nonetheless"), the dry/wet mix with the dry signal delayed by the
 * latency, and its meters. The app's audio device (ExampleHost) and the AUv3 both run it.
 *
 * For the UI it keeps two timelines, one entry per 32 ms of audio: the input's peak and every
 * meter's value at the end of that stretch.
 */
class ExampleEngine {
public:
    using Example = anira::examples::Example;
    static constexpr std::size_t k_meters = anira::examples::meter_count<Example>();
    static constexpr std::size_t k_timeline_capacity = 512;

    ExampleEngine();
    ExampleEngine(const ExampleEngine&) = delete;
    ExampleEngine& operator=(const ExampleEngine&) = delete;
    ExampleEngine(ExampleEngine&&) = delete;
    ExampleEngine& operator=(ExampleEngine&&) = delete;

    /** Main thread, no process() running: a new stream at this rate and largest block. */
    void prepare(double sample_rate, std::uint32_t max_block);

    /** Audio thread: planar host audio, `in` and `out` may alias; any channel counts (the
     *  example's own are downmixed or spread). */
    void process(const float* const* in,
                 float* const* out,
                 std::uint32_t frames,
                 std::uint32_t in_channels,
                 std::uint32_t out_channels) noexcept;

    // Any thread.
    void set_parameter(std::size_t index, float value) noexcept;
    [[nodiscard]] float parameter(std::size_t index) const noexcept;
    void set_backend(std::size_t choice) noexcept;
    [[nodiscard]] std::size_t backend() const noexcept { return m_backend.load(); }
    void set_mix(float mix) noexcept { m_mix.store(mix); }
    [[nodiscard]] float mix() const noexcept { return m_mix.load(); }
    void set_try_nonetheless(bool on) noexcept { m_anyway.store(on); }
    [[nodiscard]] float meter(std::size_t index) const noexcept;
    [[nodiscard]] double sample_rate() const noexcept { return m_rate.load(); }
    [[nodiscard]] std::size_t latency() const noexcept { return m_latency.load(); }

    // Main / UI thread.
    [[nodiscard]] const std::vector<anira::examples::BackendChoice>& backends() const noexcept {
        return m_backends.choices();
    }
    /** Why the model does not run, or the chosen backend's verdict; empty when it plays. */
    [[nodiscard]] std::string status() const;
    [[nodiscard]] bool waits_for_consent() const noexcept;
    void try_nonetheless() noexcept;
    /** The last `max_count` entries: peaks[k] and meters[m][k] describe the same 32 ms. */
    void timelines(std::vector<float>& peaks,
                   std::array<std::vector<float>, k_meters>& meters,
                   std::uint32_t max_count) const;

private:
    void record(std::uint32_t frames) noexcept;

    anira::Context m_context{anira::ContextConfig{}};
    std::unique_ptr<Example> m_example;  // none when its weights are missing
    std::string m_error;
    bool m_prepared = false;
    anira::examples::Backends m_backends;

    std::array<std::atomic<float>, Example::k_parameters.size()> m_values{};
    std::atomic<std::size_t> m_backend{0};
    std::atomic<float> m_mix{1.0F};
    std::atomic<bool> m_anyway{false};
    std::atomic<double> m_rate{0.0};
    std::atomic<std::size_t> m_latency{0};

    anira::examples::DryWet m_dry_wet;
    std::array<std::vector<float>, Example::k_channels> m_planes;
    std::array<float*, Example::k_channels> m_channels{};
    anira::Tensor m_audio{};

    double m_chunk = 1.0;  // host samples per timeline entry
    double m_chunk_end = 1.0;
    std::int64_t m_position = 0;
    float m_peak = 0.0F;
    std::array<std::atomic<float>, k_timeline_capacity> m_peaks{};
    std::array<std::array<std::atomic<float>, k_timeline_capacity>, k_meters> m_meter_values{};
    std::atomic<std::int64_t> m_count{0};
};
