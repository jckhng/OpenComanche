#pragma once
#include "graphics_target.hpp"
#include "desktop_view.hpp"
#include "spatial_renderer.hpp"
#include <optional>

namespace comanchecd::host {
enum class SceneOverlay { none, nearest, sharp };
class GlScene {
  public:
    GlScene();
    ~GlScene();
    void draw(const GraphicsTarget& target, const SpatialFrame& frame, const CameraView& view,
              const EntityPoses& poses, const CockpitImage& instruments,
              const std::optional<CameraView>& cockpit, SceneOverlay overlay = SceneOverlay::none);
    void image(const GraphicsTarget& target, const CockpitImage& image, ImageScaling scaling);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace comanchecd::host
