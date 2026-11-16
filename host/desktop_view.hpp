#pragma once
#include "graphics_target.hpp"
#include "camera_view.hpp"

namespace comanchecd::host {
struct SpatialCamera;
enum class ImageScaling { profile_default, nearest, linear, integer };
enum class DisplayPacing { vertical_sync, immediate };
enum class DesktopRenderer { automatic, gpu, cpu };
enum class ViewAspect { profile_default, original, widescreen };
struct DesktopLayout {
    GraphicsViewport viewport;
    unsigned width = 0, height = 0;
};
DesktopLayout desktop_layout(unsigned width, unsigned height, ViewAspect aspect, ImageScaling scaling);
CameraView desktop_camera(const SpatialCamera& camera, unsigned width, unsigned height);
} // namespace comanchecd::host
