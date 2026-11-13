#include "sdl_presenter.hpp"
#include "sdl_common.hpp"

#include <algorithm>
#include <string>

namespace comanchecd::host {
namespace {
SDL_Texture* create_texture(SDL_Renderer* renderer, ImageScaling scaling) {
    // SDL2 2.0.10 chooses filtering at texture creation. Restore the process hint afterward.
    const auto* previous = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY);
    const std::string saved = previous ? previous : "";
    const bool had_previous = previous != nullptr;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, scaling == ImageScaling::linear ? "1" : "0");
    auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                      cockpit_width, cockpit_height);
    if (had_previous) { SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, saved.c_str()); }
    else {
#if SDL_VERSION_ATLEAST(2, 24, 0)
        SDL_ResetHint(SDL_HINT_RENDER_SCALE_QUALITY);
#else
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#endif
    }
    return texture;
}
}

class SdlPresenter::Impl {
public:
    Impl(SDL_Window* window, ImageScaling selected) : scaling(selected) {
        if (scaling == ImageScaling::profile_default) {
            throw std::invalid_argument("Resolve the presentation profile before creating its backend");
        }
        renderer.reset(SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
        if (!renderer) { renderer.reset(SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE)); }
        if (!renderer || SDL_RenderSetLogicalSize(renderer.get(), cockpit_width, cockpit_height) != 0) {
            throw sdl_error("SDL2 renderer creation failed");
        }
        texture.reset(create_texture(renderer.get(), scaling));
        if (!texture) { throw sdl_error("SDL2 texture creation failed"); }
    }

    void present(const CockpitImage& image) {
        if (scaling == ImageScaling::integer) {
            int width = 0, height = 0;
            if (SDL_GetRendererOutputSize(renderer.get(), &width, &height) != 0 ||
                    SDL_RenderSetIntegerScale(renderer.get(), width >= int(cockpit_width) && height >= int(cockpit_height) ?
                        SDL_TRUE : SDL_FALSE) != 0) {
                throw sdl_error("SDL2 integer scaling failed");
            }
        }
        std::array<std::uint32_t, 256> palette{};
        for (std::size_t index = 0; index < palette.size(); ++index) {
            const auto& color = image.palette[index];
            palette[index] = 0xff000000u | ((color[0] * 255u / 63u) << 16) |
                             ((color[1] * 255u / 63u) << 8) | (color[2] * 255u / 63u);
        }
        std::transform(image.pixels.begin(), image.pixels.end(), pixels.begin(), [&](auto index) { return palette[index]; });
        if (SDL_UpdateTexture(texture.get(), nullptr, pixels.data(), cockpit_width * sizeof(std::uint32_t)) != 0 ||
                SDL_RenderClear(renderer.get()) != 0 || SDL_RenderCopy(renderer.get(), texture.get(), nullptr, nullptr) != 0) {
            throw sdl_error("SDL2 scene presentation failed");
        }
        SDL_RenderPresent(renderer.get());
    }

private:
    ImageScaling scaling;
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{nullptr, SDL_DestroyRenderer};
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture{nullptr, SDL_DestroyTexture};
    std::array<std::uint32_t, cockpit_width * cockpit_height> pixels{};
};

SdlPresenter::SdlPresenter(SDL_Window* window, ImageScaling scaling) : impl_(std::make_unique<Impl>(window, scaling)) {}
SdlPresenter::~SdlPresenter() = default;
void SdlPresenter::present(const CockpitImage& image) { impl_->present(image); }
}
