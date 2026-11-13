#pragma once

#include "comanchecd/world_renderer.hpp"
#include <memory>

namespace comanchecd {
enum class PresentationDetail { indexed, spatial };

struct SpatialResources {
    SceneAssets assets;
    TerrainTables tables;
    std::vector<std::uint8_t> model_projection;
    SceneryPolicy scenery;
};

// Immutable world observations at the original world-drawing boundary.
struct SpatialFrame {
    std::shared_ptr<const SpatialResources> resources;
    MissionScene scene;
    SceneView camera;
    SceneCamera native_camera;
    bool native_effects = false;
    std::vector<std::uint8_t> scene_mask;
    std::uint64_t serial = 0;
    std::uint8_t cockpit_mode = 1;
};
}
