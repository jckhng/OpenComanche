#include "sdl_common.hpp"
#include "gl_desktop_presenter.hpp"
#include "sdl_host.hpp"
#include <SDL_opengl.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
std::array<std::uint8_t, 4> pixel(int x, int y) {
    std::array<std::uint8_t, 4> rgba{};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    require(glGetError() == GL_NO_ERROR, "Desktop default framebuffer must be readable before swapping");
    return rgba;
}
} // namespace

int main() {
    try {
        using namespace comanchecd;
        using namespace comanchecd::host;
        SDL_SetMainReady();
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            return 77;
        }
        const auto* driver = SDL_GetCurrentVideoDriver();
        if (driver && std::strcmp(driver, "dummy") == 0) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return 77;
        }
        {
            GlDesktopPresenter presenter(ImageScaling::integer, ViewAspect::widescreen,
                                         DisplayPacing::immediate);
            SDL_HideWindow(presenter.window());
            CockpitImage image;
            image.palette[1] = {63, 0, 0};
            image.palette[2] = {0, 0, 63};
            image.palette[3] = {32, 32, 32};
            image.palette[4] = {0, 63, 0};
            std::fill_n(image.pixels.begin(), cockpit_width * cockpit_height / 2, 1);
            std::fill(image.pixels.begin() + cockpit_width * cockpit_height / 2, image.pixels.end(), 2);
            presenter.present(image);
            {
                GlScene renderer;
                int width = 0, height = 0;
                SDL_GL_GetDrawableSize(presenter.window(), &width, &height);
                const auto layout =
                    desktop_layout(width, height, ViewAspect::original, ImageScaling::integer);
                const GraphicsTarget back{0, layout.width, layout.height, GraphicsColorSpace::srgb,
                                          layout.viewport};
                renderer.image(back, image, ImageScaling::nearest);
                require(pixel(width / 2, height * 3 / 4)[0] == 255 && pixel(width / 2, height / 4)[2] == 255,
                        "Native GPU menus must preserve top-down rows and palette channels");
                require(pixel(0, height / 2)[0] == 0 && pixel(0, height / 2)[2] == 0,
                        "Native GPU menus must retain centered 4:3 letterboxing in a widescreen window");
                image.pixels.fill(3);
                renderer.image(back, image, ImageScaling::linear);
                require(pixel(width / 2, height / 2)[0] == 129,
                        "Desktop output must retain palette brightness without double sRGB conversion");

                GLuint texture = 0;
                glGenTextures(1, &texture);
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, 64, 48, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                             nullptr);
                const GraphicsTarget target{texture, 64, 48, GraphicsColorSpace::srgb};
                renderer.image(target, image, ImageScaling::nearest);
                std::vector<std::uint8_t> pixels(64 * 48 * 4);
                glBindTexture(GL_TEXTURE_2D, texture);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                require(std::abs(int(pixels[0]) - 129) <= 1,
                        "sRGB texture targets must encode palette brightness once");

                auto resources = std::make_shared<SpatialResources>();
                resources->assets.terrain.height.assign(terrain_side * terrain_side, 0);
                resources->assets.terrain.color.assign(terrain_side * terrain_side, 4);
                resources->scenery.haze = false;
                SpatialFrame frame;
                frame.resources = resources;
                frame.scene_mask.assign(image.pixels.size(), 1);
                frame.camera.height = 40;
                image.palette[255] = {0, 0, 63};
                image.pixels.fill(1);
                const auto view = desktop_camera(spatial_camera(frame.camera), width, height);
                renderer.draw({0, unsigned(width), unsigned(height)}, frame, view, {}, image, {});
                const auto world = pixel(width / 2, height / 2);
                require(
                    world[0] == 0 && (world[1] == 255 || world[2] == 255),
                    "Desktop world rendering must restore instruments and scene state after native images");
                frame.scene_mask.assign(image.pixels.size(), 0);
                renderer.draw({0, unsigned(width), unsigned(height)}, frame, view, {}, image, {},
                              SceneOverlay::sharp);
                require(pixel(width / 2, height / 2)[0] == 255 && pixel(0, height / 2)[0] == 0,
                        "Flat cockpit overlay must preserve 4:3 instruments over the wider world");
                renderer.image(back, image, ImageScaling::nearest);
                renderer.draw(target, frame, desktop_camera(spatial_camera(frame.camera), 64, 48), {}, image,
                              {}, SceneOverlay::nearest);
                require(glGetError() == GL_NO_ERROR,
                        "Native, flat and world transitions must preserve valid GL state");
                glDeleteTextures(1, &texture);
                for (const auto size : {std::pair<int, int>{640, 480}, {1200, 600}, {300, 500}}) {
                    SDL_SetWindowSize(presenter.window(), size.first, size.second);
                    SDL_PumpEvents();
                    const auto dimensions = presenter.spatial_size();
                    presenter.present_scene(
                        frame,
                        desktop_camera(spatial_camera(frame.camera), dimensions.first, dimensions.second), {},
                        image, {}, SceneOverlay::nearest);
                    presenter.present(image);
                }
            }
        }
        {
            SdlHostOptions options;
            options.input = InputProfile::enhanced;
            options.renderer = DesktopRenderer::gpu;
            options.pacing = DisplayPacing::immediate;
            SdlFlightHost host(options);
            host.poll();
            auto resources = std::make_shared<SpatialResources>();
            resources->assets.terrain.height.assign(terrain_side * terrain_side, 0);
            resources->assets.terrain.color.assign(terrain_side * terrain_side, 1);
            resources->scenery.haze = false;
            auto scene = std::make_shared<SpatialFrame>();
            scene->resources = resources;
            scene->camera.height = 40;
            scene->cockpit_mode = 1;
            scene->scene_mask.assign(cockpit_width * cockpit_height, 1);
            auto snapshot = std::make_shared<SessionPresentation>();
            snapshot->spatial = scene;
            snapshot->flying = true;
            snapshot->image.palette[1] = {0, 63, 0};
            snapshot->image.palette[255] = {0, 0, 63};
            host.set_input_context(InputContext::flight);
            host.present_snapshot(snapshot, "GPU host fixture");
            host.present(snapshot->image, {}, "Native menu fixture");
            host.present_snapshot(snapshot, "Restored GPU host fixture");
            SDL_Event event{};
            event.type = SDL_KEYDOWN;
            event.key.keysym.scancode = SDL_SCANCODE_SPACE;
            SDL_PushEvent(&event);
            require(host.poll().pressed.fire, "GPU host must retain buffered keyboard input");
            host.set_focus(HostFocus::inactive);
            const auto released = host.poll();
            require(released.pause == PauseAction::pause && !released.controls.fire,
                    "GPU host focus loss must release controls and request pause");
            require(snapshot->cpu_cycles == 0 && scene->camera.height == 40,
                    "GPU host must consume immutable observations without advancing native time");
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        std::cout << "Desktop GPU palette, widescreen, native transitions, overlay and resizing passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
