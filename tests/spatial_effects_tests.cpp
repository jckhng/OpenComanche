#include "spatial_draw.hpp"
#include "spatial_cockpit.hpp"
#include "spatial_units.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace comanchecd;
using namespace comanchecd::host;
constexpr unsigned width = 320, height = 240;
constexpr std::uint32_t background = 0xff000000, red = 0xffff0000;

void require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}

SpatialImage weather_image(const SpatialFrame& frame, CameraPose pose, float depth = std::numeric_limits<float>::infinity()) {
    SpatialImage image{width, height, std::vector<std::uint32_t>(width * height, background),
                       std::vector<float>(width * height, depth)};
    spatial_draw::Colors colors;
    colors.base[1] = red;
    const CameraView view{pose, {-0.8, 0.8, 0.6, -0.6, 0.25, 512}};
    const CameraProjection projection(view, width, height);
    spatial_draw::weather(image, frame, view, projection, colors, spatial_draw::WeatherLayer::background);
    spatial_draw::weather(image, frame, view, projection, colors, spatial_draw::WeatherLayer::foreground);
    return image;
}

double red_center(const SpatialImage& image) {
    double sum = 0, count = 0;
    for (std::size_t index = 0; index < image.pixels.size(); ++index) {
        if (image.pixels[index] == red) { sum += index % image.width; ++count; }
    }
    require(count > 0, "Weather fixture must contain visible particles");
    return sum / count;
}

void weather_tests() {
    auto resources = std::make_shared<SpatialResources>();
    resources->tables.levels = 1;
    resources->tables.distance[4][0] = 4 * 8388608;
    SpatialFrame frame;
    frame.resources = resources;
    frame.effects.enabled = true;
    frame.effects.tint.resize(2);
    frame.effects.particles.push_back({128, 137, 2, 0, 1, 0});
    const auto original = frame.effects.particles.front();
    for (unsigned sector = 0; sector < 4; ++sector) {
        const auto image = weather_image(frame, {{}, camera_orientation(sector * spatial_units::tau / 4, 0, 0)});
        require(std::count(image.pixels.begin(), image.pixels.end(), red) > 0,
                "Recovered weather must remain visible through front, side and rear tracking");
    }
    const auto left = weather_image(frame, {{-0.15, 0, 0}, {}});
    const auto right = weather_image(frame, {{0.15, 0, 0}, {}});
    require(red_center(left) > red_center(right), "Each eye must project the same particle with correct stereo disparity");
    const auto blocked = weather_image(frame, {}, 1);
    require(std::count(blocked.pixels.begin(), blocked.pixels.end(), red) == 0,
            "World depth must occlude weather without relying on the DOS forward depth buffer");
    frame.effects.particles.front().type = 1;
    frame.effects.tint[1].fill(1);
    require(red_center(weather_image(frame, {})) > 0, "Tinted weather must use its recovered color table");
    require(original.x == frame.effects.particles.front().x && original.y == frame.effects.particles.front().y,
            "Drawing both eyes must preserve observed particle coordinates");
    frame.effects.particles.front().depth_level = 2;
    bool rejected = false;
    try { weather_image(frame, {}); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid particle observations must fail before table access");
}

void water_tests() {
    auto resources = std::make_shared<SpatialResources>();
    resources->assets.terrain.height.assign(terrain_side * terrain_side, 0);
    resources->assets.terrain.color.assign(terrain_side * terrain_side, 251);
    resources->scenery.clouds = CloudMode::panoramic;
    resources->scenery.haze = false;
    MaterializedModel sky;
    sky.bytes.assign(16 + 512 * 150, 0);
    sky.bytes[12] = 16;
    for (unsigned x = 0; x < 512; ++x) {
        for (unsigned y = 0; y < 150; ++y) { sky.bytes[16 + x * 150 + y] = 1 + (x / 8 + y / 8) % 63; }
    }
    resources->assets.models.push_back(sky);
    SpatialFrame frame;
    frame.resources = resources;
    frame.camera.height = 40;
    frame.effects.enabled = frame.effects.reflections = true;
    frame.effects.reflection_colors.resize(4);
    for (auto& table : frame.effects.reflection_colors) {
        for (unsigned index = 0; index < table.size(); ++index) { table[index] = static_cast<std::uint8_t>(index); }
    }
    Palette palette{};
    for (unsigned index = 0; index < palette.size(); ++index) {
        palette[index] = {static_cast<std::uint8_t>(index % 64), 0, static_cast<std::uint8_t>(63 - index % 64)};
    }
    SpatialRenderer renderer;
    auto view = camera_view(spatial_camera(frame.camera), width, height);
    view.pose.orientation = camera_orientation(0, -0.7, 0);
    const auto first = renderer.render_world(frame, view, {}, palette, width, height);
    view.pose.orientation = camera_orientation(0.3, -0.7, 0);
    const auto second = renderer.render_world(frame, view, {}, palette, width, height);
    require(first.pixels != second.pixels, "Water must sample the reflected environment as head orientation changes");
    frame.effects.reflections = false;
    const auto disabled = renderer.render_world(frame, view, {}, palette, width, height);
    require(second.pixels != disabled.pixels, "The recovered reflection option must control spatial water shading");
    view.pose.orientation = camera_orientation(0, -0.7, 0);
    MaterializedModel model;
    model.payload = 5;
    model.bytes.assign(16 + 8 * 8, 200);
    model.bytes[2] = 1;
    model.bytes[8] = model.bytes[9] = 8;
    model.bytes[10] = model.bytes[11] = 4;
    model.bytes[12] = 16;
    model.bytes[13] = model.bytes[14] = model.bytes[15] = 0;
    resources->assets.models.insert(resources->assets.models.begin(), model);
    palette[200] = {63, 63, 0};
    auto& entity = frame.scene.entities[1];
    entity.active = true;
    entity.kind = cannon_round_kind;
    entity.animation_frame = 1;
    entity.primary_payload = 5;
    entity.y = 0u - 16u * 4194304u;
    entity.height = 32;
    const auto count_model = [](const SpatialImage& image) {
        return std::count(image.pixels.begin(), image.pixels.end(), 0xffffff00u);
    };
    require(count_model(renderer.render_world(frame, view, entity_poses(frame), palette, width, height)) == 0,
            "The reflection fixture model must be outside the direct downward view");
    frame.effects.reflections = true;
    require(count_model(renderer.render_world(frame, view, entity_poses(frame), palette, width, height)) > 0,
            "Water must reflect model geometry that is outside the direct eye view");
    const auto reflected_left = renderer.render_world(frame, view, entity_poses(frame), palette, width, height);
    view.pose.position.x += 0.3;
    const auto reflected_right = renderer.render_world(frame, view, entity_poses(frame), palette, width, height);
    require(reflected_left.pixels != reflected_right.pixels,
            "Reflected world models must retain stereo parallax between eye positions");
    resources->assets.models.front().bytes[11] = 20;
    entity.height = 0;
    require(count_model(renderer.render_world(frame, view, entity_poses(frame), palette, width, height)) == 0,
            "Submerged geometry must be clipped from the reflected half-space");
    resources->scenery.clouds = CloudMode::flat;
    require(renderer.render_world(frame, view, {}, palette, width, height).pixels != second.pixels,
            "Flat sky selection must also affect water reflections");
}
}

int main() {
    try {
        weather_tests();
        water_tests();
        std::cout << "Per-eye weather, rear coverage, depth occlusion, palette tint and water reflection passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
