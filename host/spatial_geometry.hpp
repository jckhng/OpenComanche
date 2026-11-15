#pragma once

#include "camera_view.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace comanchecd::host {
struct GeometryVertex {
    CameraVector point;
    double u = 0, v = 0;
};
enum class GeometryKind { terrain, sprite, solid, instruments, hud, weather };
struct GeometryMaterial {
    GeometryKind kind = GeometryKind::solid;
    const std::vector<std::uint8_t>* bytes = nullptr;
    std::size_t source = 0;
    unsigned width = 0, height = 0;
    bool mirror = false;
    double minimum_height = -std::numeric_limits<double>::infinity();
    double bottom = 0, vertical_scale = 0;
    std::uint32_t color = 0;
    unsigned weather_type = 0;
    bool weather_solid = false;
};

// A presentation backend consumes the same geometry as the software rasterizer.
class SpatialGeometry {
  public:
    virtual ~SpatialGeometry() = default;
    virtual void material(const GeometryMaterial& material) = 0;
    virtual void triangle(const CameraFrustum& frustum, const GeometryVertex& a, const GeometryVertex& b,
                          const GeometryVertex& c) = 0;
    virtual void weather_background() = 0;
};
} // namespace comanchecd::host
