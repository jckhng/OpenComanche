#pragma once

#include "comanchecd/menu_renderer.hpp"
#include <vector>

namespace comanchecd {
struct WeatherParticle {
    std::uint8_t x = 0, y = 0, group = 0, type = 0, solid_color = 0;
    std::int32_t depth_level = -1;
};

// Optional draw-boundary metadata. Reading it never advances a device or RNG.
struct EffectObservation {
    bool enabled = false, reflections = false;
    std::uint32_t reflection_phase = 0;
    std::vector<ColorMap> tint, reflection_colors;
    std::vector<WeatherParticle> particles;
};
}
