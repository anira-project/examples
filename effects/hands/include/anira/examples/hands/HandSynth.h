// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/anira.h>
#include <anira/examples/Example.h>
#include <anira/examples/Weights.h>
#include <anira/examples/hands/Hands.h>
#include <anira/models/core/HostAudio.h>
#include <anira/models/core/Latest.h>
#include <anira/models/handtracking/HandTracker.h>

#include <algorithm>
#include <anira/anira.hpp>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace anira::examples::hands {

/// The workshop's step 10 as an example: the camera's hands drive a RAVE decoder (models'
/// <Name>Decoder, a generator). `Instrument` names the decoder, its latents, the latents at rest
/// and the mapping, `map(lead, texture)` from the hands' features to the latents. The audio thread
/// reads the tracker's newest hands every block, smooths the mapping's latents and hands the
/// decoder a frame whenever one is due.
template <class Instrument>
class HandSynth {
public:
    using Decoder = typename Instrument::Decoder;
    using Latents = std::array<float, Instrument::k_latents>;

    static constexpr int k_channels = 1;
    static constexpr std::size_t k_handlers = 1;  // the decoder (the tracker runs on its own)
    static constexpr bool k_camera = true;
    static constexpr Kind k_kind = Kind::Generator;
    static constexpr std::array<Parameter, 3> k_parameters{{
        {.m_id = "smoothing",
         .m_name = "Smoothing (ms)",
         .m_min = 0.0F,
         .m_max = 500.0F,
         .m_default = 80.0F},
        {.m_id = "spread", .m_name = "Spread", .m_min = 0.5F, .m_max = 2.0F, .m_default = 1.0F},
        {.m_id = "swap", .m_name = "Swap hands", .m_min = 0.0F, .m_max = 1.0F, .m_default = 0.0F},
    }};
    static constexpr std::array<Meter, Instrument::k_latents> k_meters = Instrument::k_meters;

    HandSynth(anira::Context& context, const models::core::ModelSource& weights)
        : m_decoder(context, weights)
        , m_tracker(context, anira::examples::weights("mediapipe-gesture-recognizer"))
        , m_controls(Instrument::k_rest) {
        m_tracker.prepare();
    }

    void prepare(double sample_rate, std::size_t max_block) {
        m_decoder.prepare(sample_rate, max_block);
        m_rate = sample_rate;
    }
    void reset() { m_decoder.reset(); }

    anira_status process(const anira::Tensor& audio, bool offline) noexcept {
        const std::size_t n = models::core::samples(audio);
        // The newest hands (the audio thread is the tracker's one reader), for the UI as well.
        const models::handtracking::Hands& hands = m_tracker.latest();
        m_hands.write(hands);
        const typename Controls<Latents>::Settings settings{.m_smoothing_ms = m_values[0],
                                                            .m_spread = m_values[1],
                                                            .m_swap_hands = m_values[2] >= 0.5F};
        m_controls.update(hands,
                          m_aspect.load(std::memory_order_relaxed),
                          static_cast<double>(n) / m_rate,
                          settings,
                          [](const Features& lead, const std::optional<Features>& texture) {
                              return Instrument::map(lead, texture);
                          });
        m_lead.store(m_controls.lead_slot(), std::memory_order_relaxed);
        const Latents& latents = m_controls.latents();
        for (std::size_t i = 0; i < latents.size(); ++i) {
            m_meters[i].store(latents[i], std::memory_order_relaxed);
        }
        for (std::size_t due = m_decoder.frames_due(n); due > 0; --due) {
            m_decoder.push_data(std::span<const float>(latents.data(), latents.size()));
        }
        return offline ? m_decoder.pop_data_wait(audio, k_offline_timeout_ms)
                       : m_decoder.pop_data(audio);
    }
    void set_parameter(std::size_t index, float value) noexcept {
        if (index < m_values.size()) { m_values[index] = value; }
    }

    /// Camera thread.
    template <class Image>
    void process_frame(const Image& image) noexcept {
        if (!image.is_valid()) { return; }
        m_aspect.store(static_cast<float>(image.m_width) / static_cast<float>(image.m_height),
                       std::memory_order_relaxed);
        m_tracker.process(frame_of(image));
    }

    /// UI thread: the hands (the lead hand green) and the latents.
    void overlay(Overlay& out) const {
        out.clear();
        const models::handtracking::Hands& hands = m_hands.read();
        draw(hands, out, m_lead.load(std::memory_order_relaxed));
        out.m_caption = hands.m_count == 0 ? "Show a hand: open is loud, high is bright"
                                           : std::string(Instrument::k_caption);
    }

    [[nodiscard]] float meter(std::size_t index) const noexcept {
        return index < m_meters.size() ? m_meters[index].load(std::memory_order_relaxed) : 0.0F;
    }

    [[nodiscard]] std::size_t get_latency() const noexcept { return m_decoder.get_latency(); }
    [[nodiscard]] anira::PlanReport plan_report(std::size_t /*handler*/) const {
        return m_decoder.plan_report();
    }
    void set_plan(std::size_t /*handler*/, std::uint32_t plan) noexcept {
        m_decoder.set_plan(plan);
    }

private:
    Decoder m_decoder;
    mutable models::handtracking::HandTracker m_tracker;
    Controls<Latents> m_controls;
    mutable models::core::Latest<models::handtracking::Hands> m_hands;  // audio -> UI
    std::array<float, k_parameters.size()> m_values{80.0F, 1.0F, 0.0F};
    std::array<std::atomic<float>, Instrument::k_latents> m_meters{};
    std::atomic<int> m_lead{-1};
    std::atomic<float> m_aspect{16.0F / 9.0F};
    double m_rate = 48000.0;
};

}  // namespace anira::examples::hands
