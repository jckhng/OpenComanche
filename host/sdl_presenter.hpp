#pragma once

#include "comanchecd/cockpit.hpp"
#include <memory>

struct SDL_Window;

namespace comanchecd::host {
enum class ImageScaling { profile_default, nearest, linear, integer };

// This backend consumes completed indexed images without accessing the session clock.
class SdlPresenter {
public:
    SdlPresenter(SDL_Window* window, ImageScaling scaling);
    ~SdlPresenter();
    void present(const CockpitImage& image);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
