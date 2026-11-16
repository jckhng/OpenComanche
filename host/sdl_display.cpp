#include "sdl_display.hpp"
#include "sdl_common.hpp"
#include <algorithm>

namespace comanchecd::host {
namespace {
WindowMode window_mode(Uint32 flags) {
    if ((flags & SDL_WINDOW_FULLSCREEN) != 0) {
        return WindowMode::fullscreen;
    }
    return (flags & SDL_WINDOW_MAXIMIZED) != 0 ? WindowMode::maximized : WindowMode::windowed;
}

SDL_Rect restore_bounds(SDL_Window* window, const WindowPlacement& placement) {
    int width = 0, height = 0, x = 0, y = 0;
    SDL_GetWindowSize(window, &width, &height);
    SDL_GetWindowPosition(window, &x, &y);
    SDL_Rect result{placement.position ? placement.position->x : x,
                    placement.position ? placement.position->y : y,
                    placement.width ? static_cast<int>(placement.width) : width,
                    placement.height ? static_cast<int>(placement.height) : height};
    SDL_Rect primary{};
    for (int display = 0; display < SDL_GetNumVideoDisplays(); ++display) {
        SDL_Rect bounds{};
        if (SDL_GetDisplayUsableBounds(display, &bounds) != 0) {
            continue;
        }
        if (display == 0) {
            primary = bounds;
        }
        // Keep enough of the title bar reachable after a monitor is disconnected.
        const SDL_Rect title{result.x, result.y, result.w, 32};
        SDL_Rect intersection{};
        if (SDL_IntersectRect(&title, &bounds, &intersection) && intersection.w >= 64 &&
            intersection.h >= 16) {
            result.w = std::min(result.w, std::max<int>(minimum_window_width, bounds.w));
            result.h = std::min(result.h, std::max<int>(minimum_window_height, bounds.h));
            return result;
        }
    }
    if (primary.w > 0 && primary.h > 0) {
        result.w = std::min(result.w, std::max<int>(minimum_window_width, primary.w));
        result.h = std::min(result.h, std::max<int>(minimum_window_height, primary.h));
        result.x = primary.x + (primary.w - result.w) / 2;
        result.y = primary.y + (primary.h - result.h) / 2;
    }
    return result;
}
} // namespace

SdlDisplay::SdlDisplay(SDL_Window* window, FrameLimit limit, WindowPlacement placement)
    : window_(window), limit_(limit), placement_(placement) {
    validate_frame_limit(limit);
    validate_window_placement(placement);
    SDL_SetWindowMinimumSize(window_, minimum_window_width, minimum_window_height);
    const auto bounds = restore_bounds(window_, placement_);
    SDL_SetWindowSize(window_, bounds.w, bounds.h);
    SDL_SetWindowPosition(window_, bounds.x, bounds.y);
    placement_.width = bounds.w;
    placement_.height = bounds.h;
    placement_.position = WindowPosition{bounds.x, bounds.y};
    if (placement_.mode == WindowMode::fullscreen) {
        if (SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
            throw sdl_error("SDL2 fullscreen restore failed");
        }
    } else if (placement_.mode == WindowMode::maximized) {
        SDL_MaximizeWindow(window_);
    }
    refresh_rate();
}

bool SdlDisplay::capture() {
    const auto flags = SDL_GetWindowFlags(window_);
    if ((flags & SDL_WINDOW_MINIMIZED) != 0) {
        return false;
    }
    const auto mode = window_mode(flags);
    bool changed = mode != placement_.mode;
    placement_.mode = mode;
    if (mode == WindowMode::windowed) {
        int x = 0, y = 0, width = 0, height = 0;
        SDL_GetWindowPosition(window_, &x, &y);
        SDL_GetWindowSize(window_, &width, &height);
        changed = changed || !placement_.position || placement_.position->x != x ||
                  placement_.position->y != y || placement_.width != static_cast<unsigned>(width) ||
                  placement_.height != static_cast<unsigned>(height);
        placement_.width = width;
        placement_.height = height;
        placement_.position = WindowPosition{x, y};
    }
    return changed;
}

bool SdlDisplay::event(const SDL_WindowEvent& event) {
    switch (event.event) {
    case SDL_WINDOWEVENT_MOVED:
    case SDL_WINDOWEVENT_SIZE_CHANGED:
    case SDL_WINDOWEVENT_RESIZED:
    case SDL_WINDOWEVENT_MAXIMIZED:
    case SDL_WINDOWEVENT_RESTORED:
        refresh_rate();
        deadline_.reset();
        return capture();
    case SDL_WINDOWEVENT_FOCUS_GAINED:
    case SDL_WINDOWEVENT_MINIMIZED:
        deadline_.reset();
        break;
    }
    return false;
}

void SdlDisplay::toggle_fullscreen() {
    capture();
    if (placement_.mode == WindowMode::fullscreen) {
        if (SDL_SetWindowFullscreen(window_, 0) != 0) {
            throw sdl_error("SDL2 fullscreen change failed");
        }
        // Restore explicitly for SDL backends that retain the desktop-sized rectangle.
        SDL_RestoreWindow(window_);
        SDL_SetWindowSize(window_, placement_.width, placement_.height);
        SDL_SetWindowPosition(window_, placement_.position->x, placement_.position->y);
        if (before_fullscreen_ == WindowMode::maximized) {
            SDL_MaximizeWindow(window_);
        }
    } else {
        before_fullscreen_ = placement_.mode;
        if (SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
            throw sdl_error("SDL2 fullscreen change failed");
        }
    }
    capture();
    refresh_rate();
    deadline_.reset();
}

void SdlDisplay::set_limit(FrameLimit limit) {
    validate_frame_limit(limit);
    limit_ = limit;
    deadline_.reset();
}

void SdlDisplay::refresh_rate() {
    SDL_DisplayMode mode{};
    const auto display = SDL_GetWindowDisplayIndex(window_);
    refresh_rate_ = display >= 0 && SDL_GetCurrentDisplayMode(display, &mode) == 0 && mode.refresh_rate > 0
                        ? std::min<unsigned>(mode.refresh_rate, maximum_frame_rate)
                        : fallback_refresh_rate;
}

void SdlDisplay::wait() {
    const auto rate = limit_.mode == FrameLimitMode::unlimited ? 0u
                      : limit_.mode == FrameLimitMode::fixed   ? limit_.rate
                                                               : refresh_rate_;
    const auto due = deadline_.schedule(FrameDeadline::Clock::now(), rate);
    while (FrameDeadline::Clock::now() < due) {
        SDL_Delay(1);
    }
}

void SdlDisplay::reset_deadline() {
    deadline_.reset();
}

const WindowPlacement& SdlDisplay::placement() const {
    return placement_;
}

std::string SdlDisplay::limit_label() const {
    const auto label = frame_limit_name(limit_);
    return label == "auto" ? "auto/" + std::to_string(refresh_rate_) : label;
}
} // namespace comanchecd::host
