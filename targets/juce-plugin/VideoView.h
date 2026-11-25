// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "PluginProcessor.h"

#ifdef ANIRA_EXAMPLES_HAS_CAMERA

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

/// A camera example's video, mirrored like a selfie, with what the example draws over it
/// (Overlay): 30 times a second, on the message thread.
// NOLINTNEXTLINE(misc-multiple-inheritance): JUCE's component and timer, as JUCE intends
class VideoView : public juce::Component, private juce::Timer {
public:
    explicit VideoView(PluginProcessor& processor) : m_processor(processor) { startTimerHz(30); }
    ~VideoView() override { stopTimer(); }
    VideoView(const VideoView&) = delete;
    VideoView& operator=(const VideoView&) = delete;
    VideoView(VideoView&&) = delete;
    VideoView& operator=(VideoView&&) = delete;

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xFF101014));
        if (!m_image.isValid()) {
            g.setColour(juce::Colours::white.withAlpha(0.6F));
            g.drawText(
                m_error.empty() ? juce::String("Waiting for the camera...") : juce::String(m_error),
                getLocalBounds(),
                juce::Justification::centred);
            return;
        }
        const juce::Rectangle<float> video = fitted();
        const float scale = video.getWidth() / static_cast<float>(m_image.getWidth());
        g.drawImageTransformed(
            m_image,
            juce::AffineTransform::scale(-scale, scale).translated(video.getRight(), video.getY()));
        const auto at = [&](const anira::examples::Overlay::Point& p) {
            return juce::Point<float>(video.getX() + (p.m_x * video.getWidth()),
                                      video.getY() + (p.m_y * video.getHeight()));
        };
        for (const auto& line : m_overlay.m_lines) {
            g.setColour(juce::Colour(line.m_to.m_colour));
            g.drawLine({at(line.m_from), at(line.m_to)}, line.m_to.m_size);
        }
        for (const auto& point : m_overlay.m_points) {
            g.setColour(juce::Colour(point.m_colour));
            g.fillEllipse(juce::Rectangle<float>(point.m_size, point.m_size).withCentre(at(point)));
        }
        for (const auto& label : m_overlay.m_labels) {
            g.setColour(juce::Colour(label.m_at.m_colour));
            g.setFont(label.m_at.m_size);
            g.drawText(
                juce::String(label.m_text),
                juce::Rectangle<float>(240.0F, label.m_at.m_size * 1.6F).withCentre(at(label.m_at)),
                juce::Justification::centred);
        }
        if (!m_overlay.m_caption.empty()) {
            const auto bar = video.withTop(video.getBottom() - 26.0F);
            g.setColour(juce::Colours::black.withAlpha(0.55F));
            g.fillRect(bar);
            g.setColour(juce::Colours::white);
            g.setFont(14.0F);
            g.drawText(juce::String(m_overlay.m_caption),
                       bar.reduced(10.0F, 0.0F),
                       juce::Justification::centredLeft);
        }
    }

private:
    void timerCallback() override {
        m_error = m_processor.camera_error();
        const anira::examples::Image frame = m_processor.latest_frame();
        if (frame.is_valid() && frame.m_pixels != m_pixels) {
            m_pixels = frame.m_pixels;
            if (m_image.getWidth() != frame.m_width || m_image.getHeight() != frame.m_height) {
                m_image = juce::Image(juce::Image::ARGB,
                                      frame.m_width,
                                      frame.m_height,
                                      false,
                                      juce::SoftwareImageType());
            }
            // JUCE's ARGB is BGRA in memory on little-endian machines: the camera's layout.
            const juce::Image::BitmapData data(m_image, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < frame.m_height; ++y) {
                std::memcpy(data.getLinePointer(y),
                            frame.m_pixels + (static_cast<std::size_t>(y) *
                                              static_cast<std::size_t>(frame.m_stride)),
                            static_cast<std::size_t>(frame.m_width) * 4);
            }
        }
        m_processor.overlay(m_overlay);
        repaint();
    }

    /// The video's area: the frame's aspect, centred.
    [[nodiscard]] juce::Rectangle<float> fitted() const {
        const auto bounds = getLocalBounds().toFloat();
        const float aspect =
            static_cast<float>(m_image.getWidth()) / static_cast<float>(m_image.getHeight());
        const float width = std::min(bounds.getWidth(), bounds.getHeight() * aspect);
        return juce::Rectangle<float>(width, width / aspect).withCentre(bounds.getCentre());
    }

    PluginProcessor& m_processor;
    juce::Image m_image;
    const std::uint8_t* m_pixels = nullptr;
    anira::examples::Overlay m_overlay;
    std::string m_error;
};

#endif
