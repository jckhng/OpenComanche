#pragma once

#include "spatial_raster.hpp"
#include <limits>

namespace comanchecd::host::spatial_draw {
struct Colors {
    std::array<std::uint32_t, 256> base{}, quarter{}, half{};
    double quarter_distance = 512, half_distance = 512;
    bool haze = false;
    const std::vector<std::uint8_t>* panorama = nullptr;
    std::size_t panorama_offset = 0;
    std::uint8_t sky_index(CameraVector direction) const;
    std::uint32_t terrain(std::uint8_t index, double depth) const {
        return !haze || depth < quarter_distance ? base[index] : depth < half_distance ? quarter[index] : half[index];
    }
};
double surface_height(const Terrain& terrain, double x, double z);
void terrain(SpatialImage& image, const SpatialFrame& frame, const CameraView& view,
             const CameraProjection& projection, const Colors& colors,
             double minimum_height = -std::numeric_limits<double>::infinity());
enum class WeatherLayer { background, foreground };
void weather(SpatialImage& image, const SpatialFrame& frame, const CameraView& view,
             const CameraProjection& projection, const Colors& colors, WeatherLayer layer);
void models(SpatialImage& image, const SpatialFrame& frame, const CameraView& view,
            const CameraProjection& projection, const EntityPoses& poses, const Colors& colors,
            double minimum_height = -std::numeric_limits<double>::infinity());
void reflections(SpatialImage& image, SpatialImage& reflected, const SpatialFrame& frame, const CameraView& view,
                 const CameraProjection& projection, const EntityPoses& poses, const Colors& colors);
}
