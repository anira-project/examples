// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <host/ExampleEngine.h>
#include <tanh/audio-io/AudioIODeviceCallback.h>

#include <cstdint>
#include <vector>

/**
 * ExampleHost — the audio device's callback (tanh-lib's AudioDeviceManager, duplex):
 * interleaved microphone in, interleaved output out, through the ExampleEngine.
 */
class ExampleHost : public thl::AudioIODeviceCallback {
public:
    ExampleHost() = default;
    ~ExampleHost() override = default;
    ExampleHost(const ExampleHost&) = delete;
    ExampleHost& operator=(const ExampleHost&) = delete;
    ExampleHost(ExampleHost&&) = delete;
    ExampleHost& operator=(ExampleHost&&) = delete;

    ExampleEngine& engine() { return m_engine; }

    void prepare_to_play(uint32_t sample_rate, uint32_t buffer_size) override;
    void process(float* output_buffer,
                 const float* input_buffer,
                 uint32_t frame_count,
                 uint32_t num_input_channels,
                 uint32_t num_output_channels) override;

private:
    ExampleEngine m_engine;
    std::vector<std::vector<float>> m_in;  // the device's channels, planar
    std::vector<std::vector<float>> m_out;
    std::vector<const float*> m_in_planes;
    std::vector<float*> m_out_planes;
};
