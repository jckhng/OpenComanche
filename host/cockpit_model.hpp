#pragma once

#include "camera_view.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace comanchecd::host::cockpit_model {
struct Face { std::array<CameraVector, 4> vertices; std::uint32_t color; };
std::vector<Face> build();
}
