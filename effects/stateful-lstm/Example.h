// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/examples/Example.h>
#include <anira/models/stateful_lstm/StatefulLstm.h>

#include <array>
#include <string_view>

namespace anira::examples {

/// The stateful LSTM (vackva/stateful-lstm), an LSTM whose state anira carries from block to block,
/// compiled into the plugin.
class Example : public EffectExample<anira::models::stateful_lstm::StatefulLstm> {
public:
    static constexpr std::string_view k_name = "Stateful LSTM";
    static constexpr int k_channels = 1;
    static constexpr std::array<Parameter, 0> k_parameters{};

    using EffectExample::EffectExample;
};

}  // namespace anira::examples
