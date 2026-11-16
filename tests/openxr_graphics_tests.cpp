#include "openxr_graphics.hpp"
#include "sdl_common.hpp"
#include <SDL_opengl.h>
#include <array>
#include <algorithm>
#include "spatial_cockpit.hpp"
#include "cockpit_model.hpp"
#include <cstring>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
}
int main() {
    try {
        SDL_SetMainReady();
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            std::cout << "SDL2 video unavailable for real OpenGL checks\n";
            return 77;
        }
        const auto* driver = SDL_GetCurrentVideoDriver();
        if (driver && std::strcmp(driver, "dummy") == 0) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            std::cout << "SDL2 dummy video lacks OpenGL\n";
            return 77;
        }
        {
            using namespace comanchecd::host;
            OpenXrCapabilities requirements;
            requirements.minimum_opengl = XR_MAKE_VERSION(3, 2, 0);
            requirements.maximum_opengl = XR_MAKE_VERSION(3, 2, 0);
            OpenXrGraphics graphics(requirements);
            GLuint texture = 0;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            SpatialImage image{2, 2, {0xffff0000u, 0xff00ff00u, 0xff0000ffu, 0u}, {}};
            graphics.upload({texture, 2, 2, OpenXrColorSpace::srgb}, image);
            glBindTexture(GL_TEXTURE_2D, texture);
            std::array<std::uint8_t, 16> pixels{};
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            require(pixels == std::array<std::uint8_t, 16>{0, 0, 255, 255, 0, 0, 0, 0, 255, 0, 0, 255, 0, 255, 0, 255},
                "GL uploads must preserve channels/alpha and flip native top-down rows");
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            image.pixels.assign(4, 0xff808080u);
            graphics.upload({texture, 2, 2, OpenXrColorSpace::linear}, image);
            glBindTexture(GL_TEXTURE_2D, texture);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            require(pixels[0] == 55 && pixels[1] == 55 && pixels[2] == 55 && pixels[3] == 255,
                "Linear XR formats must receive decoded sRGB values with retained alpha");
            bool rejected = false;
            try { graphics.upload({texture, 3, 2, OpenXrColorSpace::srgb}, image); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && glGetError() == GL_NO_ERROR, "Invalid upload sizes must fail before GL writes");
            constexpr unsigned side = 256;
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            const OpenXrTarget target{texture, side, side, OpenXrColorSpace::srgb};
            auto resources = std::make_shared<comanchecd::SpatialResources>();
            resources->assets.terrain.height.assign(comanchecd::terrain_side * comanchecd::terrain_side, 0);
            resources->assets.terrain.color.assign(resources->assets.terrain.height.size(), 1);
            resources->scenery.haze = false;
            comanchecd::SpatialFrame scene;
            scene.resources = resources;
            scene.scene_mask.assign(comanchecd::cockpit_width * comanchecd::cockpit_height, 1);
            scene.camera.height = 40;
            comanchecd::CockpitImage instruments;
            instruments.palette[1] = {0, 63, 0};
            instruments.palette[2] = {63, 0, 0};
            instruments.palette[3] = {63, 63, 0};
            instruments.palette[255] = {0, 0, 63};
            auto view = camera_view(spatial_camera(scene.camera), side, side);
            const auto read = [&] {
                std::vector<std::uint32_t> result(side * side);
                glBindTexture(GL_TEXTURE_2D, texture);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, result.data());
                require(glGetError() == GL_NO_ERROR, "GPU eye readback must succeed");
                return result;
            };
            graphics.render_eye(target, scene, view, {}, instruments);
            const auto world = read();
            require(std::count(world.begin(), world.end(), 0xff00ff00u) > side * side / 4 &&
                    std::count(world.begin(), world.end(), 0xff0000ffu) > side * side / 4,
                    "GPU terrain and sky must preserve native palette colors and depth coverage");
            resources = std::make_shared<comanchecd::SpatialResources>(*resources);
            for (unsigned z = 0; z < comanchecd::terrain_side; ++z) {
                for (unsigned x = 0; x < comanchecd::terrain_side; ++x) {
                    resources->assets.terrain.height[z * comanchecd::terrain_side + x] = (x / 7 + z / 5) % 40;
                }
            }
            scene.resources = resources;
            graphics.render_eye(target, scene, view, {}, instruments);
            const auto fixed_terrain = read();
            scene.camera.x += 4194304u;
            graphics.render_eye(target, scene, view, {}, instruments);
            require(read() == fixed_terrain,
                    "Aircraft movement must never deform terrain observed from an unchanged world pose");
            scene.camera.x = 0;
            resources = std::make_shared<comanchecd::SpatialResources>(*resources);
            resources->assets.terrain.height.assign(resources->assets.terrain.height.size(), 0);
            scene.resources = resources;
            comanchecd::MaterializedModel sprite;
            sprite.payload = 5;
            sprite.bytes.assign(16 + 8 * 8, 2);
            sprite.bytes[2] = 1;
            sprite.bytes[8] = sprite.bytes[9] = 8;
            sprite.bytes[10] = sprite.bytes[11] = 4;
            sprite.bytes[12] = 16;
            sprite.bytes[13] = sprite.bytes[14] = sprite.bytes[15] = 0;
            resources = std::make_shared<comanchecd::SpatialResources>(*resources);
            resources->assets.models.push_back(sprite);
            scene.resources = resources;
            auto& entity = scene.scene.entities[1];
            entity.active = true;
            entity.kind = comanchecd::cannon_round_kind;
            entity.animation_frame = 1;
            entity.primary_payload = 5;
            entity.y = 0u - 8u * 4194304u;
            entity.height = 40;
            graphics.render_eye(target, scene, view, entity_poses(scene), instruments);
            auto models = read();
            require(std::count(models.begin(), models.end(), 0xffff0000u) > 30,
                    "GPU SG models must use the shared directional sprite geometry and native texels");
            scene.effects.enabled = true;
            scene.effects.tint.resize(2);
            scene.effects.particles.push_back({128, 137, 2, 0, 3, 0});
            resources = std::make_shared<comanchecd::SpatialResources>(*resources);
            resources->tables.levels = 1;
            resources->tables.distance[4][0] = 4 * 8388608;
            scene.resources = resources;
            entity.active = false;
            view.pose.orientation = camera_orientation(3.141592653589793, 0, 0);
            graphics.render_eye(target, scene, view, entity_poses(scene), instruments);
            auto weather = read();
            require(std::count(weather.begin(), weather.end(), 0xffffff00u) > 0,
                    "GPU weather must retain rear-facing coverage");
            scene.effects.particles.clear();
            scene.effects.reflections = true;
            scene.effects.reflection_colors.resize(4);
            for (auto& table : scene.effects.reflection_colors) { table.fill(3); }
            resources = std::make_shared<comanchecd::SpatialResources>(*resources);
            resources->assets.terrain.color.assign(resources->assets.terrain.height.size(), 251);
            scene.resources = resources;
            view.pose.orientation = camera_orientation(0, -0.6, 0);
            graphics.render_eye(target, scene, view, entity_poses(scene), instruments);
            auto water = read();
            require(std::count(water.begin(), water.end(), 0xffffff00u) > side * side / 8,
                    "GPU mirrored water must apply the recovered water remap");
            view = camera_view(spatial_camera(scene.camera), side, side);
            instruments.pixels.fill(2);
            std::fill(scene.scene_mask.begin() + cockpit_model::instrument_top * comanchecd::cockpit_width,
                      scene.scene_mask.end(), 0);
            resources = std::make_shared<comanchecd::SpatialResources>(*resources);
            resources->hud_colors[2] = true;
            scene.resources = resources;
            scene.effects.reflections = false;
            graphics.render_eye(target, scene, view, {}, instruments, cockpit_view({}, view.frustum));
            const auto centered = read();
            require(std::count(centered.begin(), centered.end(), 0xffff0000u) > side * side / 8,
                    "GPU cockpit must retain live native instrument pixels");
            const CameraPose rear_pose{{}, camera_orientation(3.141592653589793, 0.2, 0)};
            graphics.render_eye(target, scene, eye_view(view, rear_pose, view.frustum, 8), {}, instruments,
                                cockpit_view(rear_pose, view.frustum));
            require(read() != centered, "GPU look-around must draw surrounding cockpit geometry");
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            resources = std::make_shared<comanchecd::SpatialResources>(*resources);
            resources->assets.terrain.color.assign(resources->assets.terrain.color.size(), 1);
            scene.resources = resources;
            instruments.palette[1] = {32, 32, 32};
            graphics.render_eye({texture, side, side, OpenXrColorSpace::linear}, scene, view, {}, instruments);
            const auto linear = read();
            const auto gray = std::count_if(linear.begin(), linear.end(), [](std::uint32_t pixel) {
                const auto value = pixel & 255;
                return value >= 55 && value <= 57 && ((pixel >> 8) & 255) == value &&
                    ((pixel >> 16) & 255) == value && (pixel >> 24) == 255;
            });
            require(gray > side * side / 8, "GPU linear XR targets must receive decoded sRGB colors");
            require(glGetError() == GL_NO_ERROR, "GPU scene switching and resource replacement must preserve GL state");
            glDeleteTextures(1, &texture);
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        std::cout << "Real SDL2/OpenGL texture upload checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
