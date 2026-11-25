// SPDX-License-Identifier: Apache-2.0

#include "PluginEditor.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include "PluginProcessor.h"

PluginEditor::PluginEditor(PluginProcessor& plugin)
    : AudioProcessorEditor(plugin), m_processor(plugin), m_controls(plugin) {
    addAndMakeVisible(m_controls);
    for (juce::Label& meter : m_meters) {
        meter.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(meter);
    }
    m_status.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_status);
    // The user agrees to run the chosen backend although prepare found it not real-time here.
    m_try_nonetheless.onClick = [this] { m_processor.try_nonetheless(); };
    addChildComponent(m_try_nonetheless);
    setSize(std::max(m_controls.getWidth(), 400),
            m_controls.getHeight() + (static_cast<int>(m_meters.size()) * k_meter_height) +
                k_status_height + k_button_height);
    update();
    startTimerHz(15);
}

PluginEditor::~PluginEditor() {
    stopTimer();
}

void PluginEditor::resized() {
    juce::Rectangle<int> area = getLocalBounds();
    m_controls.setBounds(area.removeFromTop(m_controls.getHeight()));
    for (juce::Label& meter : m_meters) {
        meter.setBounds(area.removeFromTop(k_meter_height).reduced(8, 0));
    }
    m_status.setBounds(area.removeFromTop(k_status_height).reduced(8, 0));
    m_try_nonetheless.setBounds(area.removeFromTop(k_button_height).reduced(8, 2));
}

void PluginEditor::timerCallback() {
    update();
}

void PluginEditor::update() {
    for (std::size_t i = 0; i < m_meters.size(); ++i) {
        const anira::examples::Meter info = anira::examples::meter_info<Example>(i);
        m_meters[i].setText(
            juce::String(std::string(info.m_name)) + ": " + juce::String(m_processor.meter(i), 2),
            juce::dontSendNotification);
    }
    m_status.setText(m_processor.status_line(), juce::dontSendNotification);
    m_try_nonetheless.setVisible(m_processor.waits_for_consent());
}
