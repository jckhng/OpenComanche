#include "desktop_view.hpp"
#include "spatial_renderer.hpp"
#include <algorithm>
#include <stdexcept>

namespace comanchecd::host {
DesktopLayout desktop_layout(unsigned width, unsigned height, ViewAspect aspect, ImageScaling scaling) {
    if (!width || !height || aspect == ViewAspect::profile_default ||
        scaling == ImageScaling::profile_default) {
        throw std::invalid_argument("Desktop layout requires resolved options and nonempty dimensions");
    }
    constexpr double original_aspect = double(cockpit_width) / cockpit_height;
    constexpr unsigned maximum_width = 3200, maximum_height = 2400;
    unsigned viewport_width = width, viewport_height = height;
    if (aspect == ViewAspect::original || double(width) / height < original_aspect) {
        viewport_width = std::max(1u, std::min(width, unsigned(height * original_aspect)));
        viewport_height = std::max(1u, unsigned(viewport_width / original_aspect));
        if (scaling == ImageScaling::integer && viewport_width >= cockpit_width) {
            const auto factor = viewport_width / cockpit_width;
            viewport_width = factor * cockpit_width;
            viewport_height = factor * cockpit_height;
        }
    }
    const double scale =
        std::min({1.0, double(maximum_width) / viewport_width, double(maximum_height) / viewport_height});
    return {{int((width - viewport_width) / 2), int((height - viewport_height) / 2), viewport_width,
             viewport_height},
            std::max(1u, unsigned(viewport_width * scale)),
            std::max(1u, unsigned(viewport_height * scale))};
}

CameraView desktop_camera(const SpatialCamera& camera, unsigned width, unsigned height) {
    if (!width || !height) {
        throw std::invalid_argument("Desktop camera requires a nonempty viewport");
    }
    auto result = camera_view(camera, cockpit_width, cockpit_height);
    const double expansion = double(width) * cockpit_height / (double(height) * cockpit_width);
    result.frustum.left *= expansion;
    result.frustum.right *= expansion;
    return result;
}
} // namespace comanchecd::host
