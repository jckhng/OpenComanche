#pragma once
#include "gl_scene.hpp"
#include <memory>
#include <utility>

struct SDL_Window;

namespace comanchecd::host {
// Owns a desktop context and consumes immutable presentation snapshots.
class GlDesktopPresenter {
  public:
    GlDesktopPresenter(ImageScaling scaling, ViewAspect aspect, DisplayPacing pacing);
    ~GlDesktopPresenter();
    SDL_Window* window() const;
    std::pair<unsigned, unsigned> spatial_size() const;
    void present(const CockpitImage& image);
    void present_scene(const SpatialFrame& frame, const CameraView& view, const EntityPoses& poses,
                       const CockpitImage& instruments, const std::optional<CameraView>& cockpit,
                       SceneOverlay overlay);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace comanchecd::host
