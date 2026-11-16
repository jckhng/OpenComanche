#pragma once

#include "camera_view.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace comanchecd::host::cockpit_model {
constexpr unsigned instrument_top = 145;
struct Face { std::array<CameraVector, 4> vertices; std::uint32_t color; };
std::array<CameraVector, 4> instrument_panel();
std::vector<Face> build();
}
