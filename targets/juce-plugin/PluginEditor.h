// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>

#include "PluginProcessor.h"
#include "VideoView.h"

/// The generic controls of the parameters, the example's meters, the line that says whether the
/// chosen backend keeps up on this machine (what prepare measured) or why the model does not run,
/// and "Try nonetheless", shown while a backend that is not real-time here waits for the user's
/// consent.
// NOLINTNEXTLINE(misc-multiple-inheritance): JUCE's editor and timer, as JUCE intends
class PluginEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PluginEditor(PluginProcessor& plugin);
    ~PluginEditor() override;
    PluginEditor(const PluginEditor&) = delete;
    PluginEditor& operator=(const PluginEditor&) = delete;
    PluginEditor(PluginEditor&&) = delete;
    PluginEditor& operator=(PluginEditor&&) = delete;

    void resized() override;

private:
    /// 15 Hz, on the message thread: the meters, the line and the button follow the processor.
    void timerCallback() override;
    void update();

    PluginProcessor& m_processor;
#ifdef ANIRA_EXAMPLES_HAS_CAMERA
    VideoView m_video;  // a camera example's video and overlay
    static constexpr int k_video_height = 405;
#endif
    juce::GenericAudioProcessorEditor m_controls;
    std::array<juce::Label, anira::examples::meter_count<Example>()> m_meters;
    // A speech example's transcript, newest last.
    static constexpr bool k_text = anira::examples::TextLike<Example>;
    juce::TextEditor m_text;
    anira::examples::Transcript m_transcript;
    static constexpr int k_text_height = 220;
    juce::Label m_status;
    juce::TextButton m_try_nonetheless{"Try nonetheless"};

    static constexpr int k_meter_height = 24;
    static constexpr int k_status_height = 48;
    static constexpr int k_button_height = 28;
};
