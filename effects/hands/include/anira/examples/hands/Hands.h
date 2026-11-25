// SPDX-License-Identifier: Apache-2.0

#pragma once

// What the hand examples share (from the workshop's step 10, without JUCE): five features per hand
// from models' HandTracker, which hand leads, the smoothing of a mapping's latents, and the hands
// drawn into an Overlay.

#include <anira/examples/Example.h>
#include <anira/models/handtracking/HandTracker.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>

namespace anira::examples::hands {

namespace ht = anira::models::handtracking;

/// What a hand does, each feature in [0, 1], seen like a mirror (move your hand right, x grows).
struct Features {
    float m_x = 0.5F;         ///< 0 left edge, 1 right edge
    float m_y = 0.5F;         ///< 0 bottom, 1 top
    float m_openness = 0.0F;  ///< 0 fist, 1 open palm
    float m_pinch = 0.0F;     ///< 0 thumb and index apart, 1 touching
    float m_roll = 0.5F;      ///< 0 tilted left, 0.5 upright, 1 tilted right
};

/// The straight line from at_0 (feature 0) to at_1 (feature 1).
inline float map_range(float feature, float at_0, float at_1) noexcept {
    return at_0 + ((at_1 - at_0) * feature);
}

/// Pairs of landmarks that draw a hand's skeleton.
inline constexpr std::array<std::pair<int, int>, 21> k_skeleton{
    {{0, 1},   {1, 2},   {2, 3},   {3, 4},    // thumb
     {0, 5},   {5, 6},   {6, 7},   {7, 8},    // index
     {5, 9},   {9, 10},  {10, 11}, {11, 12},  // middle
     {9, 13},  {13, 14}, {14, 15}, {15, 16},  // ring
     {13, 17}, {17, 18}, {18, 19}, {19, 20},  // pinky
     {0, 17}}};                               // palm

namespace detail {

// MediaPipe's hand landmarks.
constexpr int k_wrist = 0;
constexpr int k_thumb_tip = 4;
constexpr int k_index_mcp = 5;
constexpr int k_index_tip = 8;
constexpr int k_middle_mcp = 9;
constexpr int k_middle_tip = 12;
constexpr int k_ring_mcp = 13;
constexpr int k_ring_tip = 16;
constexpr int k_pinky_mcp = 17;
constexpr int k_pinky_tip = 20;
// Pose ratios (relative to the palm's length) onto [0, 1].
constexpr float k_fist_ratio = 1.0F;
constexpr float k_open_ratio = 1.75F;
constexpr float k_pinch_closed_ratio = 0.2F;
constexpr float k_pinch_open_ratio = 0.8F;
constexpr float k_roll_range = 1.0F;    // radians each side of upright
constexpr float k_edge_margin = 0.15F;  // x and y reach their ends this far inside the frame

inline float normalize(float value, float at_0, float at_1) noexcept {
    return std::clamp((value - at_0) / (at_1 - at_0), 0.0F, 1.0F);
}
// Mirrored and aspect-corrected: x grows to the right of the screen, y downwards.
inline ht::Vec2 screen(const ht::Hand& hand, int index, float aspect) noexcept {
    const ht::Vec2& p = hand.m_landmarks[static_cast<std::size_t>(index)];
    return {.m_x = (1.0F - p.m_x) * aspect, .m_y = p.m_y};
}
inline float distance(ht::Vec2 a, ht::Vec2 b) noexcept {
    return std::hypot(a.m_x - b.m_x, a.m_y - b.m_y);
}

}  // namespace detail

/// The hand's features; `aspect` is the frame's width / height (landmarks are normalized per axis).
inline Features features(const ht::Hand& hand, float aspect) noexcept {
    using namespace detail;
    const ht::Vec2 wrist = screen(hand, k_wrist, aspect);
    const ht::Vec2 middle_mcp = screen(hand, k_middle_mcp, aspect);
    const float palm = std::max(distance(wrist, middle_mcp), 1e-4F);
    ht::Vec2 centre{};
    for (const int index : {k_wrist, k_index_mcp, k_middle_mcp, k_ring_mcp, k_pinky_mcp}) {
        const ht::Vec2& p = hand.m_landmarks[static_cast<std::size_t>(index)];
        centre.m_x += (1.0F - p.m_x) / 5.0F;
        centre.m_y += p.m_y / 5.0F;
    }
    float tips = 0.0F;
    for (const int tip : {k_index_tip, k_middle_tip, k_ring_tip, k_pinky_tip}) {
        tips += distance(screen(hand, tip, aspect), wrist) / 4.0F;
    }
    const float pinch =
        distance(screen(hand, k_thumb_tip, aspect), screen(hand, k_index_tip, aspect));
    const float roll = std::atan2(middle_mcp.m_x - wrist.m_x, wrist.m_y - middle_mcp.m_y);
    return {.m_x = normalize(centre.m_x, k_edge_margin, 1.0F - k_edge_margin),
            .m_y = normalize(1.0F - centre.m_y, k_edge_margin, 1.0F - k_edge_margin),
            .m_openness = normalize(tips / palm, k_fist_ratio, k_open_ratio),
            .m_pinch = normalize(pinch / palm, k_pinch_open_ratio, k_pinch_closed_ratio),
            .m_roll = normalize(roll, -k_roll_range, k_roll_range)};
}

/// The hands drawn into `overlay`: skeleton, joints, and handedness, gesture and presence at the
/// wrist; `highlight` (a slot) brighter. Mirrored like the video.
inline void draw(const ht::Hands& hands, Overlay& overlay, int highlight = -1) {
    for (std::size_t h = 0; h < hands.m_count; ++h) {
        const ht::Hand& hand = hands.m_hands[h];
        const bool lead = hand.m_slot == highlight;
        const std::uint32_t colour = lead ? 0xFF40C057U : 0xFF4C6EF5U;
        const auto at = [&](int index, float size) {
            const ht::Vec2& p = hand.m_landmarks[static_cast<std::size_t>(index)];
            return Overlay::Point{.m_x = 1.0F - p.m_x,
                                  .m_y = p.m_y,
                                  .m_size = size,
                                  .m_colour = colour};
        };
        for (const auto& [from, to] : k_skeleton) {
            overlay.m_lines.push_back({.m_from = at(from, 3.0F), .m_to = at(to, 3.0F)});
        }
        for (std::size_t i = 0; i < hand.m_landmarks.size(); ++i) {
            overlay.m_points.push_back(at(static_cast<int>(i), 7.0F));
        }
        std::string text = hand.m_handedness_right > 0.5F ? "right" : "left";
        if (hand.m_gesture > 0 &&
            static_cast<std::size_t>(hand.m_gesture) < ht::k_gesture_labels.size()) {
            text +=
                std::string(" · ") + ht::k_gesture_labels[static_cast<std::size_t>(hand.m_gesture)];
        }
        Overlay::Point label = at(detail::k_wrist, 14.0F);
        label.m_y = std::min(label.m_y + 0.05F, 0.98F);
        label.m_colour = 0xFFFFFFFFU;
        overlay.m_labels.push_back({.m_at = label, .m_text = std::move(text)});
    }
}

/// Which hand plays which role, and the smoothed latents of a mapping (`Latents`: std::array of
/// float), from one thread (the audio thread: no allocation). The lead hand is your right one
/// (swap: the left); a single hand always leads; a second one is the texture hand.
template <class Latents>
class Controls {
public:
    struct Settings {
        float m_smoothing_ms = 80.0F;  ///< one pole while a hand plays
        float m_spread = 1.0F;         ///< widens or narrows every feature around its middle
        bool m_swap_hands = false;
    };

    explicit Controls(const Latents& rest) : m_rest(rest), m_latents(rest), m_targets(rest) {}

    /// `map(lead, texture)` gives the targets while a lead hand plays; without one they glide to
    /// rest. `dt`: seconds since the last call.
    template <class Map>
    void update(const ht::Hands& hands,
                float aspect,
                double dt,
                const Settings& settings,
                Map&& map) {
        const ht::Hand* lead = nullptr;
        const ht::Hand* texture = nullptr;
        for (std::size_t i = 0; i < hands.m_count; ++i) {
            const ht::Hand& hand = hands.m_hands[i];
            if ((hand.m_handedness_right > 0.5F) != settings.m_swap_hands && lead == nullptr) {
                lead = &hand;
            } else if (texture == nullptr) {
                texture = &hand;
            }
        }
        if (lead == nullptr) { std::swap(lead, texture); }
        m_lead_slot = lead != nullptr ? lead->m_slot : -1;

        const std::array<const ht::Hand*, 2> incoming{lead, texture};
        for (std::size_t role = 0; role < 2; ++role) {
            if (incoming[role] != nullptr) {
                m_features[role] = spread(features(*incoming[role], aspect), settings.m_spread);
                m_missing_for[role] = 0.0;
            } else {
                m_missing_for[role] += dt;
            }
            // A hand that drops out for less than k_hold keeps playing.
            if (m_missing_for[role] >= k_hold) { m_features[role].reset(); }
        }
        const std::optional<Features>& lead_features = m_features[0];
        if (lead_features.has_value()) {
            m_targets = map(*lead_features, m_features[1]);
        } else {
            m_targets = m_rest;
        }
        const double tau_ms = lead_features.has_value() ? settings.m_smoothing_ms : k_release_ms;
        const auto coefficient =
            tau_ms <= 0.0 ? 1.0F : static_cast<float>(1.0 - std::exp(-dt * 1000.0 / tau_ms));
        for (std::size_t i = 0; i < m_latents.size(); ++i) {
            m_latents[i] += (m_targets[i] - m_latents[i]) * coefficient;
        }
    }

    [[nodiscard]] const Latents& latents() const noexcept { return m_latents; }
    [[nodiscard]] bool playing() const noexcept { return m_features[0].has_value(); }
    [[nodiscard]] int lead_slot() const noexcept { return m_lead_slot; }

private:
    static constexpr double k_hold = 0.2;          // seconds a dropout is bridged
    static constexpr double k_release_ms = 600.0;  // the glide to rest

    static Features spread(Features f, float amount) noexcept {
        for (float* v : {&f.m_x, &f.m_y, &f.m_openness, &f.m_pinch, &f.m_roll}) {
            *v = 0.5F + ((*v - 0.5F) * amount);
        }
        return f;
    }

    Latents m_rest;
    Latents m_latents;
    Latents m_targets;
    std::array<std::optional<Features>, 2> m_features;
    std::array<double, 2> m_missing_for{1e9, 1e9};
    int m_lead_slot = -1;
};

/// A camera Image as the tracker's Frame (BGRA, the image kept alive while the tracker holds it).
template <class Image>
ht::Frame frame_of(const Image& image) {
    ht::Frame frame;
    frame.m_pixels = image.m_pixels;
    frame.m_width = image.m_width;
    frame.m_height = image.m_height;
    frame.m_stride = image.m_stride;
    frame.m_pixel_stride = 4;
    frame.m_format = ht::PixelFormat::Bgra;
    frame.m_keep_alive = image.m_keep_alive;
    return frame;
}

}  // namespace anira::examples::hands
