// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/examples/Example.h>
#include <anira/models/guitar_lstm/GuitarLstm.h>

#include <array>
#include <string_view>

namespace anira::examples {

/// GuitarLSTM (faressc/GuitarLSTM), a convolutional LSTM amp model with its pre-processing (a
/// window of past samples per sample), compiled into the plugin.
class Example : public EffectExample<anira::models::guitar_lstm::GuitarLstm> {
public:
    static constexpr std::string_view k_name = "GuitarLSTM";
    static constexpr int k_channels = 1;
    static constexpr std::array<Parameter, 0> k_parameters{};

    using EffectExample::EffectExample;
};

}  // namespace anira::examples
