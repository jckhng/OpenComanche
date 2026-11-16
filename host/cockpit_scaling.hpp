#pragma once

#include "comanchecd/cockpit.hpp"

namespace comanchecd::host {
struct SpatialImage;
enum class CockpitScaling { nearest, sharp };
enum class OverlayLayout { full, centered };
void composite_cockpit(SpatialImage& image, const CockpitImage& overlay,
                       const std::vector<std::uint8_t>& scene_mask, CockpitScaling scaling,
                       OverlayLayout layout = OverlayLayout::full);
}
