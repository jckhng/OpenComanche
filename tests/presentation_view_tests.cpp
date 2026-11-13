#include "spatial_renderer.hpp"
#include "spatial_units.hpp"
#include "spatial_raster.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace comanchecd;
using namespace comanchecd::host;
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
bool near(double a, double b) { return std::abs(a - b) < 1e-7; }

void projections() {
    CameraView view{{{4, 5, 6}, {}}, {-1, 1, 1, -1, 0.1, 100}};
    CameraProjection projection(view, 200, 100);
    auto point = projection.project(projection.to_camera({4, 5, -4}));
    require(near(point.x, 100) && near(point.y, 50) && near(point.depth, 10), "Identity camera must project along negative Z");
    view.frustum = {-0.5, 1.5, 1, -0.5, 0.1, 100};
    point = CameraProjection(view, 200, 100).project({0, 0, -10});
    require(near(point.x, 50) && near(point.y, 200.0 / 3), "Asymmetric frusta must preserve the off-axis principal point");
    view.pose.orientation = camera_orientation(0.6, -0.3, 0.4);
    const CameraProjection rotated(view, 200, 100);
    for (const auto pixel : {std::pair<double, double>{0, 0}, {200, 100}, {75.25, 60.5}}) {
        const auto world = view.pose.position + rotated.ray(pixel.first, pixel.second) * 10;
        point = rotated.project(rotated.to_camera(world));
        require(near(point.x, pixel.first) && near(point.y, pixel.second) && near(point.depth, 10),
                "Full-pose view rays and projection must round-trip");
    }
    const auto frustum = camera_frustum({-0.6, 0.7, 0.5, -0.4}, 0.1, 100);
    const CameraPose eye{{0.032, 0.01, -0.02}, camera_orientation(0.1, 0.2, -0.1)};
    view.pose.orientation = camera_orientation(1.5707963267948966, 0, 0);
    const auto composed = eye_view(view, eye, frustum, 4);
    require(near(composed.pose.position.x, 3.92) && near(composed.pose.position.y, 5.04) && near(composed.pose.position.z, 5.872),
            "Tracked metre offsets must rotate into the base pose with explicit world scaling");
    CameraView symmetric{{}, {-1, 1, 1, -1, 0.1, 100}};
    const auto left = eye_view(symmetric, {{-0.032, 0, 0}, {}}, symmetric.frustum, 1);
    const auto right = eye_view(symmetric, {{0.032, 0, 0}, {}}, symmetric.frustum, 1);
    require(CameraProjection(left, 200, 100).project(CameraProjection(left, 200, 100).to_camera({0, 0, -10})).x > 100 &&
            CameraProjection(right, 200, 100).project(CameraProjection(right, 200, 100).to_camera({0, 0, -10})).x < 100,
            "Per-eye translations must produce opposite stereo parallax");
    const CameraProjection rolled({{{}, camera_orientation(0, 0, 1.5707963267948966)}, symmetric.frustum}, 200, 100);
    point = rolled.project(rolled.to_camera({1, 0, -10}));
    require(near(point.x, 100) && near(point.y, 55), "Camera roll must rotate projected world geometry");
    for (const auto invalid : {CameraView{{{}, {0, 0, 0, 0}}, symmetric.frustum},
            CameraView{{{std::numeric_limits<double>::quiet_NaN(), 0, 0}, {}}, symmetric.frustum},
            CameraView{{}, {1, -1, 1, -1, 0.1, 100}}, CameraView{{}, {-1, 1, 1, -1, 0, 100}}}) {
        bool rejected = false;
        try { CameraProjection checked(invalid, 200, 100); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Invalid poses and frusta must fail before rendering");
    }
}

void entities() {
    auto first = std::make_shared<SpatialFrame>();
    first->resources = std::make_shared<SpatialResources>();
    auto& a = first->scene.entities[1];
    a.active = true; a.kind = 1; a.x = 0xff000000; a.angle = 255; a.rotor_phase = 254; a.height = 40;
    auto second = std::make_shared<SpatialFrame>(*first);
    second->serial = 1;
    auto& b = second->scene.entities[1];
    b.x = 0x01000000; b.angle = 1; b.rotor_phase = 2; b.height = 60;
    CameraHistory history;
    history.observe(first, 100, 0);
    history.observe(second, 1000100, 1);
    const auto middle = history.sample_entities(1.025)[1];
    require(near(middle.x, 1024) && near(middle.height, 9.375) && near(middle.heading, spatial_units::tau) &&
            near(middle.rotor_phase, spatial_units::tau), "Entity position, height and rotations must interpolate across wrap seams");
    require(first->scene.entities[1].x == 0xff000000 && second->scene.entities[1].x == 0x01000000,
            "Presentation interpolation must preserve immutable canonical snapshots");
    auto replacement = std::make_shared<SpatialFrame>(*second);
    replacement->scene.entities[1].primary_model = 3;
    history.reset(); history.observe(first, 100, 0); history.observe(replacement, 1000100, 1);
    require(near(history.sample_entities(1)[1].x, 4), "Model identity transitions must snap");
    replacement->scene.entities[1].primary_model = first->scene.entities[1].primary_model;
    replacement->scene.entities[1].phase = EntityPhase::explosion;
    history.reset(); history.observe(first, 100, 0); history.observe(replacement, 1000100, 1);
    require(near(history.sample_entities(1)[1].x, 4), "Explosion transitions must snap");
    replacement->scene.entities[1].phase = EntityPhase::normal;
    first->scene.entities[1].active = false;
    history.reset(); history.observe(first, 100, 0); history.observe(replacement, 1000100, 1);
    require(near(history.sample_entities(1)[1].x, 4), "Newly active entities must snap");
    first->scene.entities[1].active = true;
    first->scene.entities[1].kind = replacement->scene.entities[1].kind = guided_missile_kind;
    replacement->scene.entities[1].previous_x = first->scene.entities[1].x;
    history.reset(); history.observe(first, 100, 0); history.observe(replacement, 1000100, 1);
    require(near(history.sample_entities(1.025)[1].x, 1024), "Continuous projectile poses must interpolate");
    replacement = std::make_shared<SpatialFrame>(*replacement);
    replacement->scene.entities[1].previous_x = 0;
    history.reset(); history.observe(first, 100, 0); history.observe(replacement, 1000100, 1);
    require(near(history.sample_entities(1)[1].x, 4), "A broken projectile pose chain must snap reused slots");
    replacement->serial = 3;
    history.reset(); history.observe(first, 100, 0); history.observe(replacement, 1000100, 1);
    require(near(history.sample_entities(1)[1].x, 4), "Skipped observation frames must reset entity history");
}

void cockpit() {
    CockpitImage overlay;
    overlay.palette[1] = {63, 63, 63};
    std::vector<std::uint8_t> mask(cockpit_width * cockpit_height, 1);
    for (unsigned i = 10; i <= 12; ++i) { overlay.pixels[i * cockpit_width + i] = 1; mask[i * cockpit_width + i] = 0; }
    mask[20 * cockpit_width + 20] = 0;
    const auto retained = overlay;
    SpatialImage nearest{640, 480, std::vector<std::uint32_t>(640 * 480, 0xffff0000), {}};
    auto sharp = nearest;
    composite_cockpit(nearest, overlay, mask, CockpitScaling::nearest);
    composite_cockpit(sharp, overlay, mask, CockpitScaling::sharp);
    require(nearest.pixels[21 * 640 + 22] == 0xffff0000 && sharp.pixels[21 * 640 + 22] == 0xffffffff,
            "Sharp cockpit scaling must reconstruct masked diagonal edges");
    require(sharp.pixels[40 * 640 + 40] == 0xff000000 && sharp.pixels[0] == 0xffff0000,
            "Opaque black HUD pixels and transparent scene pixels must retain separate coverage");
    for (const auto size : {std::pair<unsigned, unsigned>{320, 240}, {160, 120}, {319, 239}}) {
        SpatialImage native{size.first, size.second, std::vector<std::uint32_t>(size.first * size.second, 0xffff0000), {}};
        auto enhanced = native;
        composite_cockpit(native, overlay, mask, CockpitScaling::nearest);
        composite_cockpit(enhanced, overlay, mask, CockpitScaling::sharp);
        require(native.pixels == enhanced.pixels, "Native-sized and downscaled overlays must preserve every nearest sample");
    }
    require(overlay.pixels == retained.pixels && overlay.palette == retained.palette, "Scaling must preserve the native cockpit source");
}

void raster_views() {
    auto resources = std::make_shared<SpatialResources>();
    resources->assets.terrain.height.assign(terrain_side * terrain_side, 0);
    resources->assets.terrain.color.assign(terrain_side * terrain_side, 1);
    SpatialFrame frame; frame.resources = resources;
    Palette palette{}; palette[1] = {0, 63, 0}; palette[2] = {63, 0, 0}; palette[255] = {0, 0, 63};
    const auto poses = entity_poses(frame);
    CameraView view{{{0, 8, 0}, camera_orientation(0, 1.5707963267948966, 0)}, {-0.6, 0.6, 0.6, -0.6, 0.1, 100}};
    SpatialRenderer renderer;
    auto image = renderer.render_world(frame, view, poses, palette, 160, 160);
    require(std::all_of(image.pixels.begin(), image.pixels.end(), [](auto pixel) { return pixel == 0xff0000ff; }),
            "A full-pose upward view must clip the ground");
    view.pose.orientation = camera_orientation(0, -1.5707963267948966, 0.7);
    image = renderer.render_world(frame, view, poses, palette, 160, 160);
    require(std::all_of(image.pixels.begin(), image.pixels.end(), [](auto pixel) { return pixel == 0xff00ff00; }),
            "A rolled downward view must cover the ground without terrain ring cracks");
    view.pose.position.y = 2;
    view.frustum.far_clip = 4;
    image = renderer.render_world(frame, view, poses, palette, 160, 160);
    require(std::all_of(image.pixels.begin(), image.pixels.end(), [](auto pixel) { return pixel == 0xff00ff00; }),
            "Short eye-view clipping ranges must retain the first terrain detail ring");
    for (unsigned z = 0; z < terrain_side; ++z) {
        for (unsigned x = 0; x < terrain_side; ++x) { resources->assets.terrain.height[z * terrain_side + x] = (x * 17 + z * 13) % 32; }
    }
    view.pose.position.y = 64;
    view.frustum.far_clip = 200;
    image = renderer.render_world(frame, view, poses, palette, 160, 160);
    require(std::all_of(image.pixels.begin(), image.pixels.end(), [](auto pixel) { return pixel == 0xff00ff00; }),
            "Stitched terrain rings must cover nonplanar ground across detail boundaries");
    SpatialImage clipped{100, 100, std::vector<std::uint32_t>(10000, 0), std::vector<float>(10000, std::numeric_limits<float>::infinity())};
    CameraView clip_view{{}, {-1, 1, 1, -1, 0.25, 10}};
    spatial_raster::triangle(clipped, CameraProjection(clip_view, 100, 100), clip_view.frustum,
        {{-1, -1, -0.01}, 0, 0}, {{1, -1, -2}, 1, 0}, {{0, 1, -2}, 0.5, 1},
        [](double, double, double) { return 0xffff0000u; });
    require(std::count(clipped.pixels.begin(), clipped.pixels.end(), 0xffff0000u) > 0,
            "Triangles crossing the near plane must clip into visible geometry");
    for (const auto depth : clipped.depth) { require(std::isinf(depth) || (depth >= 0.24999 && depth <= 10), "Raster depth must obey the view clipping interval"); }
    std::fill(resources->assets.terrain.height.begin(), resources->assets.terrain.height.end(), 0);
    MaterializedModel model;
    model.payload = 5; model.bytes.assign(16 + 64, 2);
    model.bytes[0] = 'S'; model.bytes[1] = 'G'; model.bytes[2] = 1;
    model.bytes[8] = model.bytes[9] = 8; model.bytes[10] = model.bytes[11] = 4;
    model.bytes[12] = 16; model.bytes[13] = model.bytes[14] = model.bytes[15] = 0;
    resources->assets.models.push_back(model);
    auto& target = frame.scene.entities[1];
    target.active = true; target.kind = cannon_round_kind; target.animation_frame = 1;
    target.primary_payload = 5; target.height = 40; target.y = 0u - 16u * 4194304u;
    view = {{{0, 7.5, 0}, {}}, {-1, 1, 1, -1, 0.1, 100}};
    const auto left_view = eye_view(view, {{-0.032, 0, 0}, {}}, view.frustum, 8);
    const auto right_view = eye_view(view, {{0.032, 0, 0}, {}}, view.frustum, 8);
    const auto left_image = renderer.render_world(frame, left_view, entity_poses(frame), palette, 160, 160);
    const auto right_image = renderer.render_world(frame, right_view, entity_poses(frame), palette, 160, 160);
    const auto center = [&](const SpatialImage& rendered) {
        double total = 0, count = 0;
        for (std::size_t index = 0; index < rendered.pixels.size(); ++index) {
            if (rendered.pixels[index] == 0xffff0000) { total += index % rendered.width; ++count; }
        }
        require(count > 0, "Per-eye sprite fixture must remain visible");
        return total / count;
    };
    require(center(left_image) > center(right_image), "World-only per-eye rendering must move models with eye translation");
    view.frustum.left = -0.5; view.frustum.right = 1.5;
    image = renderer.render_world(frame, view, entity_poses(frame), palette, 160, 160);
    require(center(image) < 60, "World-only rendering must apply the asymmetric eye projection to models");
}
}

int main() {
    try { projections(); entities(); cockpit(); raster_views(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
