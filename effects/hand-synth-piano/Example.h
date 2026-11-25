// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/examples/Example.h>
#include <anira/examples/hands/HandSynth.h>
#include <anira/examples/hands/Hands.h>
#include <anira/models/rave_magnetic_resonator_piano/RaveMagneticResonatorPiano.h>

#include <array>
#include <optional>
#include <string_view>

namespace anira::examples {

/// The magnetic resonator piano (RAVE, weights CC BY-NC 4.0) played with your hands, the workshop's
/// mapping: open is loud, high is bright, a second hand adds texture.
struct PianoHands {
    using Decoder = models::rave_magnetic_resonator_piano::RaveMagneticResonatorPianoDecoder;
    static constexpr std::size_t k_latents = 8;
    // z0 near silent, the rest in the middle.
    static constexpr std::array<float, k_latents>
        k_rest{-3.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    static constexpr std::array<Meter, k_latents> k_meters{{
        {.m_id = "z0", .m_name = "z0 loudness", .m_min = -3.0F, .m_max = 3.0F},
        {.m_id = "z1", .m_name = "z1 body", .m_min = -3.0F, .m_max = 3.0F},
        {.m_id = "z2", .m_name = "z2 brightness", .m_min = -3.0F, .m_max = 3.0F},
        {.m_id = "z3", .m_name = "z3 colour", .m_min = -3.0F, .m_max = 3.0F},
        {.m_id = "z4", .m_name = "z4 swell", .m_min = -3.0F, .m_max = 3.0F},
        {.m_id = "z5", .m_name = "z5 tilt", .m_min = -3.0F, .m_max = 3.0F},
        {.m_id = "z6", .m_name = "z6 dark/bright", .m_min = -3.0F, .m_max = 3.0F},
        {.m_id = "z7", .m_name = "z7 resonance", .m_min = -3.0F, .m_max = 3.0F},
    }};
    static constexpr std::string_view k_caption =
        "Open: loud · high: bright · pinch: body · second hand: texture";

    static std::array<float, k_latents> map(const hands::Features& lead,
                                            const std::optional<hands::Features>& texture) {
        using hands::map_range;
        std::array<float, k_latents> z{};
        z[0] = map_range(lead.m_openness, -2.5F, 2.0F);  // loudness: a fist is quiet
        z[1] = map_range(lead.m_pinch, 0.5F, -3.0F);     // body: a pinch, darker and louder
        z[2] = map_range(lead.m_y, -2.5F, 3.0F);         // brightness: low dark, high bright
        z[3] = map_range(lead.m_x, -2.5F, 2.5F);         // colour
        z[5] = map_range(lead.m_roll, -3.0F, 3.0F);      // tilt
        if (texture) {
            z[4] = map_range(texture->m_y, -2.5F, 3.0F);        // swell
            z[6] = map_range(texture->m_x, -3.0F, 3.0F);        // dark/bright
            z[7] = map_range(texture->m_openness, 0.0F, 3.0F);  // resonance
        }
        return z;
    }
};

class Example : public hands::HandSynth<PianoHands> {
public:
    static constexpr std::string_view k_name = "Hand Synth (Magnetic Resonator Piano)";
    using HandSynth::HandSynth;
};

}  // namespace anira::examples
