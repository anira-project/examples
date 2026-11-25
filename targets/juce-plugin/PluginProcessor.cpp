// SPDX-License-Identifier: Apache-2.0

#include "PluginProcessor.h"

#include <anira/examples/Weights.h>
#ifdef ANIRA_EXAMPLES_HAS_CAMERA
#include <anira/examples/Camera.h>

#include <mutex>
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <string>

#include "PluginEditor.h"

namespace {

constexpr const char* k_backend_id = "backend";
constexpr const char* k_mix_id = "mix";

juce::AudioChannelSet bus() {
    return Example::k_channels == 2 ? juce::AudioChannelSet::stereo()
                                    : juce::AudioChannelSet::mono();
}

}  // namespace

PluginProcessor::PluginProcessor()
    : AudioProcessor(
          BusesProperties().withInput("Input", bus(), true).withOutput("Output", bus(), true))
    , m_parameters(*this, nullptr, "parameters", layout(m_backends)) {
    try {
        m_example = std::make_unique<Example>(m_context, anira::examples::weights());
    } catch (const std::exception& error) {
        m_error = error.what();
        juce::Logger::writeToLog("anira: " + juce::String(m_error));
    }
    for (std::size_t i = 0; i < m_values.size(); ++i) {
        m_values[i] = m_parameters.getRawParameterValue(
            juce::String(std::string(Example::k_parameters[i].m_id)));
    }
    m_backend = m_parameters.getRawParameterValue(k_backend_id);
    m_mix = m_parameters.getRawParameterValue(k_mix_id);
#ifdef ANIRA_EXAMPLES_HAS_CAMERA
    if constexpr (anira::examples::CameraLike<Example>) {
        if (m_example) {
            m_camera = std::make_unique<anira::examples::Camera>(
                [this](const anira::examples::Image& image) {
                    m_example->process_frame(image);
                    const std::scoped_lock lock(m_frame_mutex);
                    m_frame = image;
                },
                [this](const std::string& error) {
                    const std::scoped_lock lock(m_frame_mutex);
                    m_camera_error = error;
                });
        }
    }
#endif
}

PluginProcessor::~PluginProcessor() {
#ifdef ANIRA_EXAMPLES_HAS_CAMERA
    m_camera.reset();  // no frame reaches the example after this
#endif
}

#ifdef ANIRA_EXAMPLES_HAS_CAMERA
anira::examples::Image PluginProcessor::latest_frame() const {
    const std::scoped_lock lock(m_frame_mutex);
    return m_frame;
}

std::string PluginProcessor::camera_error() const {
    const std::scoped_lock lock(m_frame_mutex);
    return m_camera_error;
}

void PluginProcessor::overlay(anira::examples::Overlay& out) const {
    out.clear();
    if constexpr (anira::examples::CameraLike<Example>) {
        if (m_example) { m_example->overlay(out); }
    }
}
#endif

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::layout(
    const anira::examples::Backends& backends) {
    juce::AudioProcessorValueTreeState::ParameterLayout parameters;
    for (const anira::examples::Parameter& p : Example::k_parameters) {
        parameters.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{juce::String(std::string(p.m_id)), 1},
            juce::String(std::string(p.m_name)),
            juce::NormalisableRange<float>{p.m_min, p.m_max},
            p.m_default));
    }
    juce::StringArray names;
    for (const anira::examples::BackendChoice& choice : backends.choices()) {
        names.add(choice.m_name);
    }
    parameters.add(
        std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{k_backend_id, 1},
                                                     "Backend",
                                                     names,
                                                     static_cast<int>(backends.default_choice())));
    if constexpr (anira::examples::kind_of<Example>() != anira::examples::Kind::Analyzer) {
        parameters.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{k_mix_id, 1},
            "Dry/Wet",
            juce::NormalisableRange<float>{0.0F, 1.0F},
            1.0F,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [](float x, int /*length*/) { return juce::String(x * 100.0F, 0) + " %"; })));
    }
    return parameters;
}

void PluginProcessor::prepareToPlay(double sample_rate, int samples_per_block) {
#ifdef ANIRA_EXAMPLES_HAS_CAMERA
    // The camera opens with the first playback, not at construction (a host scanning plugins
    // would turn it on); start() again is a no-op.
    if (m_camera && !m_camera_started) {
        m_camera->start();
        m_camera_started = true;
    }
#endif
    const auto max_block = static_cast<std::size_t>(samples_per_block);
    m_prepared = false;
    std::size_t latency = 0;
    if (m_example) {
        // The host blocks complete the example's contracts: 1 to samples_per_block samples at
        // sample_rate (JUCE names no minimum). The latency covers every block of that range.
        try {
            m_example->prepare(sample_rate, max_block);
            for (const std::string& line : m_backends.read(*m_example)) {
                juce::Logger::writeToLog(juce::String(line));
            }
            // An analyzer leaves the audio as it is: nothing for the host to compensate.
            if constexpr (anira::examples::kind_of<Example>() != anira::examples::Kind::Analyzer) {
                latency = m_example->get_latency();
            }
            m_prepared = true;
        } catch (const std::exception& error) {
            m_error = error.what();
            juce::Logger::writeToLog("anira: " + juce::String(m_error));
        }
    }
    setLatencySamples(static_cast<int>(latency));
    m_dry_wet.prepare(m_channels.size(), max_block, latency);
    m_audio = anira::Tensor::from_host_planar<float>(
        std::span<float* const>(m_channels),
        std::array<std::int64_t, 2>{Example::k_channels, samples_per_block});
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainInputChannelSet() == bus() && layouts.getMainOutputChannelSet() == bus();
}

std::size_t PluginProcessor::backend() const noexcept {
    return static_cast<std::size_t>(m_backend->load());
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/) {
    const juce::ScopedNoDenormals no_denormals;
    const auto samples = static_cast<std::size_t>(buffer.getNumSamples());
    for (std::size_t c = 0; c < m_channels.size(); ++c) {
        m_channels[c] = buffer.getWritePointer(static_cast<int>(c));
    }
    const bool offline = isNonRealtime();
    const std::size_t choice = backend();
    constexpr anira::examples::Kind k_kind = anira::examples::kind_of<Example>();
    if constexpr (k_kind == anira::examples::Kind::Analyzer) {
        // It only reads the audio: the audio stays as it is, whatever runs.
        if (m_prepared && m_backends.runs(choice, offline)) { run(samples, offline, choice); }
        return;
    }
    if constexpr (k_kind == anira::examples::Kind::Generator) {
        buffer.clear();  // a generator makes its sound from nothing
    }
    m_dry_wet.push(m_channels, samples);

    // A choice without a plan here, or not real-time here and not accepted, passes the dry signal
    // through, time-aligned; an offline render (a bounce) runs it whatever its verdict and waits
    // for late blocks, since it does not run against the clock.
    if (m_prepared && m_backends.runs(choice, offline)) {
        run(samples, offline, choice);  // in place
        m_dry_wet.mix(m_channels, samples, m_mix->load());
    } else {
        m_dry_wet.dry(m_channels, samples);
    }
}

void PluginProcessor::run(std::size_t samples, bool offline, std::size_t choice) noexcept {
    m_backends.select(*m_example, choice);
    for (std::size_t i = 0; i < m_values.size(); ++i) {
        m_example->set_parameter(i, m_values[i]->load());
    }
    m_audio.shape[1] = static_cast<std::int64_t>(samples);
    m_example->process(m_audio, offline);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
    return new PluginEditor(*this);
}

juce::String PluginProcessor::status_line() const {
    if (!m_error.empty()) { return "The model does not run: " + juce::String(m_error); }
    return {m_backends.verdict_line(backend())};
}

float PluginProcessor::meter(std::size_t index) const {
    return m_example ? anira::examples::meter_value(*m_example, index) : 0.0F;
}

bool PluginProcessor::waits_for_consent() const {
    return m_backends.waits_for_consent(backend());
}

void PluginProcessor::try_nonetheless() {
    m_backends.try_nonetheless(backend());
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& dest_data) {
    if (const std::unique_ptr<juce::XmlElement> xml = m_parameters.copyState().createXml()) {
        copyXmlToBinary(*xml, dest_data);
    }
}

void PluginProcessor::setStateInformation(const void* data, int size_in_bytes) {
    if (const std::unique_ptr<juce::XmlElement> xml = getXmlFromBinary(data, size_in_bytes)) {
        if (xml->hasTagName(m_parameters.state.getType())) {
            m_parameters.replaceState(juce::ValueTree::fromXml(*xml));
        }
    }
}

// The plugin's instances.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
