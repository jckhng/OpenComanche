#pragma once

#include "display_settings.hpp"
#include <SDL.h>

namespace comanchecd::host {
// SDL window state and software pacing stay on the device-owning thread.
class SdlDisplay {
  public:
    SdlDisplay(SDL_Window* window, FrameLimit limit, WindowPlacement placement);
    bool event(const SDL_WindowEvent& event);
    void toggle_fullscreen();
    void set_limit(FrameLimit limit);
    void wait();
    void reset_deadline();
    const WindowPlacement& placement() const;
    std::string limit_label() const;

  private:
    bool capture();
    void refresh_rate();
    SDL_Window* window_;
    FrameLimit limit_;
    WindowPlacement placement_;
    WindowMode before_fullscreen_ = WindowMode::windowed;
    unsigned refresh_rate_ = fallback_refresh_rate;
    FrameDeadline deadline_;
};
} // namespace comanchecd::host
