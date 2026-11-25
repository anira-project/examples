// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/examples/Example.h>
#include <anira/examples/hands/HandSynth.h>
#include <anira/examples/hands/Hands.h>
#include <anira/models/rave_djembe/RaveDjembe.h>

#include <array>
#include <optional>
#include <string_view>

namespace anira::examples {

/// The djembe (RAVE, weights CC BY-NC 4.0, two latents) played with your hands: the lead hand's
/// openness and height are the two latents, a second hand moves them further.
struct DjembeHands {
    using Decoder = models::rave_djembe::RaveDjembeDecoder;
    static constexpr std::size_t k_latents = 2;
    static constexpr std::array<float, k_latents> k_rest{0.0F, 0.0F};
    static constexpr std::array<Meter, k_latents> k_meters{{
        {.m_id = "z0", .m_name = "z0", .m_min = -3.0F, .m_max = 3.0F},
        {.m_id = "z1", .m_name = "z1", .m_min = -3.0F, .m_max = 3.0F},
    }};
    static constexpr std::string_view k_caption = "Open: z0 · high: z1 · second hand: further";

    static std::array<float, k_latents> map(const hands::Features& lead,
                                            const std::optional<hands::Features>& texture) {
        using hands::map_range;
        std::array<float, k_latents> z{map_range(lead.m_openness, -2.0F, 2.0F),
                                       map_range(lead.m_y, -2.0F, 2.0F)};
        if (texture) {
            z[0] += map_range(texture->m_x, -1.0F, 1.0F);
            z[1] += map_range(texture->m_y, -1.0F, 1.0F);
        }
        return z;
    }
};

class Example : public hands::HandSynth<DjembeHands> {
public:
    static constexpr std::string_view k_name = "Hand Synth (Djembe)";
    using HandSynth::HandSynth;
};

}  // namespace anira::examples
