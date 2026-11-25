// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/examples/Example.h>
#include <anira/models/rave_magnetic_resonator_piano/RaveMagneticResonatorPiano.h>

#include <array>
#include <string_view>

namespace anira::examples {

/// RAVE magnetic resonator piano (weights CC BY-NC 4.0), a timbre transfer of the input: the
/// forward pass.
class Example : public EffectExample<
                    anira::models::rave_magnetic_resonator_piano::RaveMagneticResonatorPiano> {
public:
    static constexpr std::string_view k_name = "RAVE Magnetic Resonator Piano";
    static constexpr int k_channels = 1;
    static constexpr std::array<Parameter, 0> k_parameters{};

    using EffectExample::EffectExample;
};

}  // namespace anira::examples
