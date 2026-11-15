#pragma once
#include "openxr_session.hpp"
#include "spatial_renderer.hpp"
#include <optional>

namespace comanchecd::host {
class GlScene {
  public:
    GlScene();
    ~GlScene();
    void draw(const OpenXrTarget& target, const SpatialFrame& frame, const CameraView& view,
              const EntityPoses& poses, const CockpitImage& instruments,
              const std::optional<CameraView>& cockpit);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace comanchecd::host
