#include "sdl_presenter.hpp"
#include "sdl_common.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}

std::uint32_t pixel(SDL_Renderer* renderer, int x, int y) {
    const SDL_Rect rectangle{x, y, 1, 1};
    std::uint32_t value = 0;
    require(SDL_RenderReadPixels(renderer, &rectangle, SDL_PIXELFORMAT_ARGB8888, &value, sizeof(value)) == 0,
            "Presentation fixture must read its owned render target");
    return value & 0x00ffffffu;
}
}

int main(int argc, char** argv) {
    try {
        const bool require_linear = argc == 2 && std::string(argv[1]) == "--require-linear";
        require(argc == 1 || require_linear, "Usage sdl_presenter_tests [--require-linear]");
        using namespace comanchecd;
        using namespace comanchecd::host;
        SdlSubsystem video(SDL_INIT_VIDEO);
        // SDL2 2.0.10 suppresses draws for hidden windows, including dummy render targets.
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Presentation fixture", 0, 0, 1000, 800, 0), SDL_DestroyWindow);
        require(bool(window), "Presentation fixture window must open");
        CockpitImage image;
        image.palette[1] = {63, 0, 0};
        image.palette[2] = {0, 0, 63};
        for (std::size_t y = 0; y < cockpit_height; ++y) {
            for (std::size_t x = 0; x < cockpit_width; ++x) {
                image.pixels[y * cockpit_width + x] = x < cockpit_width / 2 ? 1 : 2;
            }
        }
        const auto retained = image;
        for (const auto scaling : {ImageScaling::nearest, ImageScaling::integer, ImageScaling::linear}) {
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
            SdlPresenter presenter(window.get(), scaling);
            require(std::string(SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY)) == "nearest",
                    "Creating a presenter must preserve the next renderer's filtering hint");
            auto* renderer = SDL_GetRenderer(window.get());
            const auto target = [&](int width, int height) {
                auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                    SDL_TEXTUREACCESS_TARGET, width, height);
                require(texture && SDL_SetRenderTarget(renderer, texture) == 0 &&
                        SDL_RenderSetLogicalSize(renderer, cockpit_width, cockpit_height) == 0,
                        "Fixture must select a sized target with the native logical aspect");
                return std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>(texture, SDL_DestroyTexture);
            };
            auto large = target(1000, 800);
            presenter.present(image);
            if (pixel(renderer, 100, 400) != 0xff0000 || pixel(renderer, 900, 400) != 0x0000ff) {
                std::cerr << "Scaling " << static_cast<int>(scaling) << " left " << std::hex
                          << pixel(renderer, 100, 400) << " right " << pixel(renderer, 900, 400) << std::dec << '\n';
            }
            require(pixel(renderer, 100, 400) == 0xff0000 && pixel(renderer, 900, 400) == 0x0000ff,
                    "All scaling modes must preserve orientation and the six-bit palette");
            require(pixel(renderer, 0, 400) == (scaling == ImageScaling::integer ? 0u : 0xff0000u),
                    "Integer scaling must letterbox between exact native pixel multiples");
            require((SDL_RenderGetIntegerScale(renderer) == SDL_TRUE) == (scaling == ImageScaling::integer),
                    "Integer scaling must be an explicit presentation policy");
            auto small = target(160, 120);
            presenter.present(image);
            require(SDL_RenderGetIntegerScale(renderer) == SDL_FALSE &&
                    pixel(renderer, 5, 60) == 0xff0000 && pixel(renderer, 155, 60) == 0x0000ff,
                    "Small outputs must show the complete image using fractional downscaling");
            auto native = target(cockpit_width, cockpit_height);
            presenter.present(image);
            std::array<std::uint32_t, cockpit_width * cockpit_height> pixels{};
            require(SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(),
                                          cockpit_width * sizeof(std::uint32_t)) == 0,
                    "Native-size fixture must read the complete output");
            for (std::size_t index = 0; index < pixels.size(); ++index) {
                require((pixels[index] & 0x00ffffffu) == (image.pixels[index] == 1 ? 0xff0000u : 0x0000ffu),
                        "Native-size output must preserve every source pixel in each scaling mode");
            }
            auto boundary = target(960, 720);
            presenter.present(image);
            const auto edge = pixel(renderer, 479, 360);
            if (scaling != ImageScaling::linear) {
                require(edge == 0xff0000, "Nearest and integer presentation must retain sharp pixel boundaries");
            }
            else {
                const bool mixed = (edge & 0xff0000u) != 0 && (edge & 0x0000ffu) != 0 && (edge & 0x00ff00u) == 0;
                require(mixed || edge == 0xff0000u,
                        "Driver filtering must preserve interpolated colors or its nearest fallback");
                require(!require_linear || mixed, "This renderer must supply the requested linear interpolation");
            }
            require(image.pixels == retained.pixels && image.palette == retained.palette,
                    "Presentation must preserve its immutable source image and palette");
            const auto size = presenter.spatial_size();
            require(size.first == 960 && size.second == 720, "Spatial rendering must use the actual drawable viewport dimensions");
            std::vector<std::uint32_t> spatial(size.first * size.second);
            for (unsigned y = 0; y < size.second; ++y) {
                for (unsigned x = 0; x < size.first; ++x) { spatial[y * size.first + x] = x % 2 ? 0xff0000ff : 0xffff0000; }
            }
            presenter.present_spatial(spatial, size.first, size.second);
            require(pixel(renderer, 480, 360) == 0xff0000 && pixel(renderer, 481, 360) == 0x0000ff,
                    "Spatial textures must preserve adjacent high-resolution pixels beyond the indexed image grid");
            presenter.present(image);
            require(pixel(renderer, 700, 360) == 0x0000ff, "Returning to native presentation must restore the completed indexed image");
            SDL_SetRenderTarget(renderer, nullptr);
        }
        std::cout << "SDL2 palette, integer/fractional scaling and presentation ownership passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
