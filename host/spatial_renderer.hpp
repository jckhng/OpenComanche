#pragma once

#include "comanchecd/presentation.hpp"
#include "camera_view.hpp"
#include "spatial_geometry.hpp"
#include "cockpit_scaling.hpp"

namespace comanchecd::host {
// Map cells and radians keep host cameras independent of DOS fixed-point storage.
struct SpatialCamera {
    double x = 0, y = 0, height = 0, heading = 0;
    double bank = 0, pitch = 0, shake = 0;
    SceneViewMode mode = SceneViewMode::cockpit;
};

SpatialCamera spatial_camera(const SceneView& view);
CameraView camera_view(const SpatialCamera& camera, unsigned width, unsigned height);

struct EntityPose {
    double x = 0, y = 0, height = 0;
    double heading = 0, rotor_phase = 0;
};
using EntityPoses = std::array<EntityPose, scene_slots>;
EntityPoses entity_poses(const SpatialFrame& frame);

class CameraHistory {
public:
    void observe(std::shared_ptr<const SpatialFrame> frame, std::uint64_t cpu_cycles, double time);
    SpatialCamera sample(double time) const;
    EntityPoses sample_entities(double time) const;
    void reset();
private:
    double blend(double time) const;
    std::shared_ptr<const SpatialFrame> previous_, current_;
    std::uint64_t cycles_ = 0;
    double arrival_ = 0, duration_ = 0;
};

struct SpatialImage {
    unsigned width = 0, height = 0;
    std::vector<std::uint32_t> pixels;
    std::vector<float> depth;
    SpatialGeometry* geometry = nullptr;
};

// CPU presentation backend. Rendering never advances the session or its devices.
class SpatialRenderer {
public:
    const SpatialImage& render(const SpatialFrame& frame, const SpatialCamera& camera,
                              const CockpitImage& overlay, unsigned width, unsigned height);
    const SpatialImage& render(const SpatialFrame& frame, const CameraView& view, const EntityPoses& poses,
                              const CockpitImage& overlay, unsigned width, unsigned height, CockpitScaling scaling);
    const SpatialImage& render_world(const SpatialFrame& frame, const CameraView& view, const EntityPoses& poses,
                                    const Palette& palette, unsigned width, unsigned height);
private:
    SpatialImage image_, reflected_;
};
}
