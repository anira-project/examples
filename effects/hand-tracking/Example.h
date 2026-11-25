// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/anira.h>
#include <anira/examples/Example.h>
#include <anira/examples/hands/Hands.h>
#include <anira/models/handtracking/HandTracker.h>

#include <anira/anira.hpp>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace anira::examples {

/// Hand tracking (models' HandTracker, MediaPipe's gesture recognizer, weights Apache-2.0): the
/// camera's hands, landmarks, handedness and gestures drawn over the video. An analyzer: the audio
/// passes through untouched.
class Example {
public:
    static constexpr std::string_view k_name = "Hand Tracking";
    static constexpr Kind k_kind = Kind::Analyzer;
    static constexpr int k_channels = 1;
    static constexpr std::size_t k_handlers = 1;  // the landmark model
    static constexpr bool k_camera = true;
    static constexpr std::array<Parameter, 0> k_parameters{};
    static constexpr std::array<Meter, 1> k_meters{{
        {.m_id = "hands", .m_name = "Hands", .m_min = 0.0F, .m_max = 2.0F},
    }};

    Example(anira::Context& context, const models::core::ModelSource& weights)
        : m_tracker(context, weights), m_report(m_tracker.prepare()) {}

    void prepare(double /*sample_rate*/, std::size_t /*max_block*/) {}
    void reset() {}
    anira_status process(const anira::Tensor& /*audio*/, bool /*offline*/) noexcept {
        return ANIRA_OK;
    }
    void set_parameter(std::size_t /*index*/, float /*value*/) noexcept {}

    /// Camera thread.
    template <class Image>
    void process_frame(const Image& image) noexcept {
        if (image.is_valid()) { m_tracker.process(hands::frame_of(image)); }
    }

    /// UI thread (the tracker's one reader).
    void overlay(Overlay& out) const {
        out.clear();
        const models::handtracking::Hands& hands = m_tracker.latest();
        hands::draw(hands, out);
        m_count.store(static_cast<float>(hands.m_count), std::memory_order_relaxed);
        out.m_caption = std::to_string(hands.m_count) + (hands.m_count == 1 ? " hand" : " hands");
    }

    [[nodiscard]] float meter(std::size_t /*index*/) const noexcept {
        return m_count.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t get_latency() const noexcept { return 0; }
    /// The landmark model's plans: the tracker picks its engines itself.
    [[nodiscard]] anira::PlanReport plan_report(std::size_t /*handler*/) const { return m_report; }
    void set_plan(std::size_t /*handler*/, std::uint32_t /*plan*/) noexcept {}

private:
    mutable models::handtracking::HandTracker m_tracker;
    anira::PlanReport m_report;
    mutable std::atomic<float> m_count{0.0F};
};

}  // namespace anira::examples
