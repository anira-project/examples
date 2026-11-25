// SPDX-License-Identifier: Apache-2.0

#pragma once

// anira and the example before JUCE: JUCE's `using namespace juce;` leaks juce::var into the global
// namespace, which an engine header beside anira (LibTorch's) then hits under MSVC (error C2872).
// clang-format off
#include <anira/anira.hpp>
#include ANIRA_EXAMPLE_HEADER  // IWYU pragma: export
#include <juce_audio_processors/juce_audio_processors.h>
// clang-format on

#include <anira/examples/Backends.h>  // IWYU pragma: export
#include <anira/examples/DryWet.h>    // IWYU pragma: export
#include <anira/examples/Example.h>   // IWYU pragma: export

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#if __has_include(<anira/examples/Camera.h>)
#include <anira/examples/Camera.h>
#define ANIRA_EXAMPLES_HAS_CAMERA 1
#endif

using Example = anira::examples::Example;
static_assert(anira::examples::ExampleLike<Example>);
static_assert(Example::k_channels == 1 || Example::k_channels == 2);

/// Any example (ANIRA_EXAMPLE_HEADER) as a JUCE plugin, the way anira's JUCE example runs its
/// models: the example's parameters, the backend choice with its real-time verdict ("Try
/// nonetheless") and a dry/wet mix of the dry signal delayed by the reported latency.
class PluginProcessor : public juce::AudioProcessor {
public:
    PluginProcessor();
    ~PluginProcessor() override;
    PluginProcessor(const PluginProcessor&) = delete;
    PluginProcessor& operator=(const PluginProcessor&) = delete;
    PluginProcessor(PluginProcessor&&) = delete;
    PluginProcessor& operator=(PluginProcessor&&) = delete;

    void prepareToPlay(double sample_rate, int samples_per_block) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int /*index*/) override {}
    const juce::String getProgramName(int /*index*/) override { return {}; }
    void changeProgramName(int /*index*/, const juce::String& /*newName*/) override {}

    void getStateInformation(juce::MemoryBlock& dest_data) override;
    void setStateInformation(const void* data, int size_in_bytes) override;

    /// For the editor, on the message thread: why the model does not run (missing weights, a
    /// failed prepare) or the chosen backend's real-time verdict; empty when it plays without a
    /// note. Whether it waits for "Try nonetheless", and the click that accepts it.
    juce::String status_line() const;
    bool waits_for_consent() const;
    /// Any thread: the example's meter `index` (anira::examples::meter_count<Example>()); 0 while
    /// the model does not run.
    float meter(std::size_t index) const;

#ifdef ANIRA_EXAMPLES_HAS_CAMERA
    /// For the editor, on the message thread (a camera example): the newest frame (invalid before
    /// the first), why there is none, and what the example draws over it.
    anira::examples::Image latest_frame() const;
    std::string camera_error() const;
    void overlay(anira::examples::Overlay& out) const;
#endif
    void try_nonetheless();

protected:
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

private:
    juce::AudioProcessorEditor* createEditor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout layout(
        const anira::examples::Backends& backends);
    [[nodiscard]] std::size_t backend() const noexcept;
    /// Audio thread: the example on the block in m_audio with the chosen plan and the parameters.
    void run(std::size_t samples, bool offline, std::size_t choice) noexcept;

    // The library default (half the hardware threads for inference).
    anira::Context m_context{anira::ContextConfig{}};
    std::unique_ptr<Example> m_example;  // none when its weights are missing
    std::string m_error;                 // why there is none, or why prepare failed
    bool m_prepared = false;

    anira::examples::Backends m_backends;
    juce::AudioProcessorValueTreeState m_parameters;
    // The parameters' values (APVTS's atomics: read on the audio thread without a lock).
    std::array<std::atomic<float>*, Example::k_parameters.size()> m_values{};
    std::atomic<float>* m_backend = nullptr;
    std::atomic<float>* m_mix = nullptr;  // none for an analyzer

#ifdef ANIRA_EXAMPLES_HAS_CAMERA
    // The camera of a camera example: its frames to the example (camera thread) and, the newest,
    // to the editor.
    std::unique_ptr<anira::examples::Camera> m_camera;
    mutable std::mutex m_frame_mutex;
    anira::examples::Image m_frame;
    std::string m_camera_error;
    bool m_camera_started = false;
#endif

    anira::examples::DryWet m_dry_wet;
    std::array<float*, Example::k_channels> m_channels{};
    anira::Tensor m_audio{};
};
