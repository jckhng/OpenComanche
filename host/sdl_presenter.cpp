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
    Impl(SDL_Window* window, ImageScaling selected, DisplayPacing pacing, ViewAspect selected_aspect) : scaling(selected), aspect(selected_aspect) {
        if (scaling == ImageScaling::profile_default) {
            throw std::invalid_argument("Resolve the presentation profile before creating its backend");
        }
        const auto flags = SDL_RENDERER_ACCELERATED |
            (pacing == DisplayPacing::vertical_sync ? SDL_RENDERER_PRESENTVSYNC : 0);
        renderer.reset(SDL_CreateRenderer(window, -1, flags));
        if (!renderer) { renderer.reset(SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE)); }
        if (!renderer || SDL_RenderSetLogicalSize(renderer.get(), cockpit_width, cockpit_height) != 0) {
            throw sdl_error("SDL2 renderer creation failed");
        }
        texture.reset(create_texture(renderer.get(), scaling));
        if (!texture) { throw sdl_error("SDL2 texture creation failed"); }
    }

#ifndef COMANCHECD_INDEXED_ONLY
    std::pair<unsigned, unsigned> spatial_size() const {
        int width = 0, height = 0;
        if (SDL_GetRendererOutputSize(renderer.get(), &width, &height) != 0) {
            throw sdl_error("SDL2 output dimensions unavailable");
        }
        const auto layout = desktop_layout(std::max(1, width), std::max(1, height), aspect, scaling);
        return {layout.width, layout.height};
    }

    void present_spatial(const std::vector<std::uint32_t>& pixels, unsigned width, unsigned height) {
        if (!width || !height || pixels.size() != std::size_t(width) * height) {
            throw std::invalid_argument("Invalid spatial output buffer");
        }
        if (!spatial_texture || width != spatial_width || height != spatial_height) {
            spatial_texture.reset(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height));
            if (!spatial_texture) { throw sdl_error("SDL2 spatial texture creation failed"); }
            spatial_width = width; spatial_height = height;
        }
        int output_width = 0, output_height = 0;
        if (SDL_GetRendererOutputSize(renderer.get(), &output_width, &output_height) != 0) {
            throw sdl_error("SDL2 output dimensions unavailable");
        }
        const auto layout = desktop_layout(std::max(1, output_width), std::max(1, output_height), aspect, scaling);
        const SDL_Rect destination{layout.viewport.x, layout.viewport.y,
            int(layout.viewport.width), int(layout.viewport.height)};
        if (SDL_RenderSetIntegerScale(renderer.get(), SDL_FALSE) != 0 ||
                SDL_RenderSetLogicalSize(renderer.get(), 0, 0) != 0 ||
                SDL_UpdateTexture(spatial_texture.get(), nullptr, pixels.data(), width * sizeof(std::uint32_t)) != 0 ||
                SDL_RenderClear(renderer.get()) != 0 || SDL_RenderCopy(renderer.get(), spatial_texture.get(), nullptr, &destination) != 0) {
            throw sdl_error("SDL2 spatial presentation failed");
        }
        SDL_RenderPresent(renderer.get());
    }

#endif

    void present(const CockpitImage& image) {
        if (SDL_RenderSetLogicalSize(renderer.get(), cockpit_width, cockpit_height) != 0) {
            throw sdl_error("SDL2 native presentation dimensions failed");
        }
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
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> spatial_texture{nullptr, SDL_DestroyTexture};
    unsigned spatial_width = 0, spatial_height = 0;
    ImageScaling scaling;
    ViewAspect aspect;
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{nullptr, SDL_DestroyRenderer};
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture{nullptr, SDL_DestroyTexture};
    std::array<std::uint32_t, cockpit_width * cockpit_height> pixels{};
};

SdlPresenter::SdlPresenter(SDL_Window* window, ImageScaling scaling, DisplayPacing pacing, ViewAspect aspect)
    : impl_(std::make_unique<Impl>(window, scaling, pacing, aspect)) {}
SdlPresenter::~SdlPresenter() = default;
void SdlPresenter::present(const CockpitImage& image) { impl_->present(image); }
#ifndef COMANCHECD_INDEXED_ONLY
std::pair<unsigned, unsigned> SdlPresenter::spatial_size() const { return impl_->spatial_size(); }
void SdlPresenter::present_spatial(const std::vector<std::uint32_t>& pixels, unsigned width, unsigned height) {
    impl_->present_spatial(pixels, width, height);
}
#endif
}
