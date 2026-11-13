#include "openxr_graphics.hpp"
#include "sdl_common.hpp"
#include <SDL_opengl.h>
#include <array>
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
