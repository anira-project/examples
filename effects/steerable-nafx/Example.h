// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/examples/Example.h>
#include <anira/models/steerable_nafx/SteerableNafx.h>

#include <array>
#include <string_view>

namespace anira::examples {

/// steerable-nafx (faressc/steerable-nafx), a temporal convolutional network modelling an effect,
/// compiled into the plugin.
class Example : public EffectExample<anira::models::steerable_nafx::SteerableNafx> {
public:
    static constexpr std::string_view k_name = "Steerable NAFX";
    static constexpr int k_channels = 1;
    static constexpr std::array<Parameter, 0> k_parameters{};

    using EffectExample::EffectExample;
};

}  // namespace anira::examples
