// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "PluginProcessor.h"

/// The generic controls of the parameters, the line that says whether the chosen backend keeps up
/// on this machine (what prepare measured) or why the model does not run, and "Try nonetheless",
/// shown while a backend that is not real-time here waits for the user's consent.
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
    /// 4 Hz, on the message thread: the line and the button follow the processor's state.
    void timerCallback() override;
    void update();

    PluginProcessor& m_processor;
    juce::GenericAudioProcessorEditor m_controls;
    juce::Label m_status;
    juce::TextButton m_try_nonetheless{"Try nonetheless"};

    static constexpr int k_status_height = 48;
    static constexpr int k_button_height = 28;
};
