#include "spatial_renderer.hpp"
#include "spatial_draw.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}

void camera_tests() {
    using namespace comanchecd;
    using namespace comanchecd::host;
    auto resources = std::make_shared<SpatialResources>();
    auto first = std::make_shared<SpatialFrame>();
    first->resources = resources;
    first->camera.x = 0xff000000;
    first->camera.heading = 0xff000000;
    auto second = std::make_shared<SpatialFrame>(*first);
    second->serial = 1;
    second->camera.x = 0x01000000;
    second->camera.heading = 0x01000000;
    second->camera.height = 100;
    CameraHistory history;
    history.observe(first, 100, 0);
    history.observe(second, 1000100, 1);
    const auto middle = history.sample(1.025);
    require(std::abs(middle.x - 1024) < 0.0001 && std::abs(middle.heading - 6.283185307179586) < 0.0001 &&
            std::abs(middle.height - 9.375) < 0.0001, "Camera interpolation must cross map and heading seams by the shortest path");
    history.observe(second, 5000000, 10);
    require(std::abs(history.sample(1.025).height - 9.375) < 0.0001,
            "Repeated observations must retain the original display interval");
    require(history.sample(20).height == 18.75 && history.sample(0).height == 0,
            "Interpolation must clamp without extrapolating simulation state");
    auto changed = std::make_shared<SpatialFrame>(*second);
    changed->serial = 2;
    changed->camera.mode = SceneViewMode::chase;
    changed->camera.height = 150;
    history.observe(changed, 2000100, 2);
    require(history.sample(2).height == 28.125, "Camera mode transitions must snap immediately");
    changed = std::make_shared<SpatialFrame>(*changed);
    changed->resources = std::make_shared<SpatialResources>();
    changed->camera.height = 200;
    changed->serial = 3;
    history.observe(changed, 3000100, 3);
    require(history.sample(3).height == 37.5, "Mission and resource transitions must reset camera history");
    changed = std::make_shared<SpatialFrame>(*changed);
    changed->serial = 4;
    changed->camera.x += 0x80000000u;
    history.observe(changed, 4000100, 4);
    require(std::abs(history.sample(4).x - spatial_camera(changed->camera).x) < 0.0001,
            "Large camera teleports must snap without flying through intervening terrain");
    history.reset();
    bool rejected = false;
    try { history.sample(0); } catch (const std::logic_error&) { rejected = true; }
    require(rejected, "An empty history must reject sampling");
}

void terrain_anchor_tests() {
    using namespace comanchecd;
    using namespace comanchecd::host;
    struct Capture final : SpatialGeometry {
        std::vector<std::pair<double, double>> positions;
        void material(const GeometryMaterial&) override {}
        void weather_background() override {}
        void triangle(const CameraFrustum&, const GeometryVertex& a, const GeometryVertex& b,
                      const GeometryVertex& c) override {
            for (const auto* vertex : {&a, &b, &c}) { positions.emplace_back(vertex->u, vertex->v); }
        }
    };
    auto resources = std::make_shared<SpatialResources>();
    resources->assets.terrain.height.assign(terrain_side * terrain_side, 0);
    resources->assets.terrain.color.assign(terrain_side * terrain_side, 1);
    SpatialFrame frame;
    frame.resources = resources;
    frame.camera.x = frame.camera.y = 128u * 4194304u;
    frame.camera.height = 40;
    auto view = camera_view(spatial_camera(frame.camera), 128, 128);
    view.frustum.far_clip = 64;
    const auto capture = [&](double x) {
        auto tracked = view;
        tracked.pose.position.x = x;
        Capture result;
        SpatialImage image{128, 128, {}, {}, &result};
        spatial_draw::terrain(image, frame, tracked, CameraProjection(tracked, 128, 128), {});
        return result.positions;
    };
    require(capture(127.99) == capture(128.01),
            "Head movement across a cell boundary must preserve terrain topology and texture coordinates");
}

void renderer_tests() {
    using namespace comanchecd;
    using namespace comanchecd::host;
    auto resources = std::make_shared<SpatialResources>();
    auto& terrain = resources->assets.terrain;
    terrain.height.assign(terrain_side * terrain_side, 0);
    terrain.color.assign(terrain.height.size(), 1);
    SpatialFrame frame;
    frame.resources = resources;
    frame.scene_mask.assign(cockpit_width * cockpit_height, 1);
    frame.camera.x = frame.camera.y = 0x80000000;
    frame.camera.height = 40;
    CockpitImage overlay;
    overlay.palette[1] = {0, 63, 0};
    overlay.palette[2] = {63, 0, 0};
    overlay.palette[255] = {0, 0, 63};
    SpatialRenderer renderer;
    auto image = renderer.render(frame, spatial_camera(frame.camera), overlay, 960, 720);
    require(image.pixels.size() == 960 * 720 && image.pixels[0] == 0xff0000ff && image.pixels.back() == 0xff00ff00,
            "Spatial terrain must rasterize at the requested dimensions with independent sky and ground");
    require(std::isfinite(image.depth.back()) && std::isinf(image.depth.front()), "The spatial depth buffer must distinguish terrain from sky");
    MaterializedModel model;
    model.payload = 5;
    model.bytes.assign(16 + 8 * 8, 2);
    model.bytes[0] = 'S'; model.bytes[1] = 'G'; model.bytes[2] = 1;
    model.bytes[8] = model.bytes[9] = 8;
    model.bytes[10] = model.bytes[11] = 4;
    model.bytes[12] = 16; model.bytes[13] = model.bytes[14] = model.bytes[15] = 0;
    for (unsigned row = 0; row < 8; ++row) { model.bytes[16 + row] = 0; }
    resources->assets.models.push_back(model);
    auto& entity = frame.scene.entities[1];
    entity.active = true; entity.kind = cannon_round_kind;
    entity.animation_frame = 1; entity.primary_payload = 5;
    entity.x = frame.camera.x; entity.y = frame.camera.y - 16 * 4194304;
    entity.height = frame.camera.height;
    image = renderer.render(frame, spatial_camera(frame.camera), overlay, 960, 720);
    const auto red_count = std::count(image.pixels.begin(), image.pixels.end(), 0xffff0000u);
    require(red_count > 100, "SG world billboards must render directly into the high-resolution depth buffer");
    const auto red = std::find(image.pixels.begin(), image.pixels.end(), 0xffff0000u) - image.pixels.begin();
    const auto nx = (red % 960) / 3, ny = (red / 960) / 3;
    const auto centroid = [](const SpatialImage& value) {
        double sum = 0, count = 0;
        for (std::size_t i = 0; i < value.pixels.size(); ++i) {
            if (value.pixels[i] == 0xffff0000u) { sum += i % value.width; ++count; }
        }
        require(count > 0, "Sprite fixture must contain visible opaque texels");
        return sum / count;
    };
    const auto normal_center = centroid(image);
    entity.animation_frame = 129;
    const auto& mirrored = renderer.render(frame, spatial_camera(frame.camera), overlay, 960, 720);
    require(centroid(mirrored) < normal_center && std::count(mirrored.pixels.begin(), mirrored.pixels.end(), 0xffff0000u) == red_count,
            "Mirrored SG frames must reverse column-major pixels while preserving transparent texels");
    entity.kind = 12;
    resources->assets.models.front().metadata.assign(50, 0);
    std::fill_n(resources->assets.models.front().metadata.begin() + 2, 32, 129);
    require(centroid(renderer.render(frame, spatial_camera(frame.camera), overlay, 960, 720)) < normal_center,
            "Directional models must select SG frames through their recovered facing and elevation tables");
    entity.kind = cannon_round_kind;
    entity.animation_frame = 1;
    frame.scene_mask[ny * cockpit_width + nx] = 0;
    overlay.pixels[ny * cockpit_width + nx] = 1;
    const auto& composited = renderer.render(frame, spatial_camera(frame.camera), overlay, 960, 720);
    require(composited.pixels[red] == 0xff00ff00, "The native cockpit coverage mask must preserve HUD pixels even when their color matches terrain");
    frame.scene_mask.assign(cockpit_width * cockpit_height, 1);
    std::fill(terrain.height.begin(), terrain.height.end(), 100);
    const auto& hidden = renderer.render(frame, spatial_camera(frame.camera), overlay, 960, 720);
    require(std::count(hidden.pixels.begin(), hidden.pixels.end(), 0xffff0000u) == 0,
            "Terrain depth must occlude world sprites");
    entity.y = frame.camera.y + 16 * 4194304;
    std::fill(terrain.height.begin(), terrain.height.end(), 0);
    const auto& behind = renderer.render(frame, spatial_camera(frame.camera), overlay, 960, 720);
    require(std::count(behind.pixels.begin(), behind.pixels.end(), 0xffff0000u) == 0,
            "Models behind the camera must be clipped");
    for (const auto& size : {std::pair<unsigned, unsigned>{1, 1}, {319, 239}, {1280, 960}}) {
        require(renderer.render(frame, spatial_camera(frame.camera), overlay, size.first, size.second).pixels.size() ==
                std::size_t(size.first) * size.second, "Spatial output must support small and noninteger display sizes");
    }
    bool rejected = false;
    try { renderer.render(frame, spatial_camera(frame.camera), overlay, 0, 720); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid spatial dimensions must fail before allocation");
}
}

int main() {
    try { camera_tests(); terrain_anchor_tests(); renderer_tests(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
