#pragma once

#include "comanchecd/cockpit.hpp"
#include <memory>
#include <utility>

struct SDL_Window;

namespace comanchecd::host {
enum class ImageScaling { profile_default, nearest, linear, integer };

// This backend consumes completed indexed images without accessing the session clock.
class SdlPresenter {
public:
    SdlPresenter(SDL_Window* window, ImageScaling scaling);
    ~SdlPresenter();
    void present(const CockpitImage& image);
    std::pair<unsigned, unsigned> spatial_size() const;
    void present_spatial(const std::vector<std::uint32_t>& pixels, unsigned width, unsigned height);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
