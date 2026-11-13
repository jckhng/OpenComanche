#pragma once

#include <cmath>

namespace comanchecd::host::spatial_units {
constexpr double tau = 6.2831853071795864769;
constexpr double fixed_cell = 4194304.0;
constexpr double distance_table_units_per_cell = fixed_cell * 2;
constexpr double terrain_height = 0.375;
constexpr double entity_height = terrain_height * 0.5;
constexpr double byte_angle = tau / 256;
constexpr double focal_ratio = 0.625;
constexpr double horizon_ratio = 76.0 / 240;
constexpr double discontinuity_distance = 128;
constexpr double discontinuity_height = 32;
constexpr double cycles_per_second = 20000000;
inline double wrap_delta(double delta, double span) { return delta - std::floor(delta / span + 0.5) * span; }
}
