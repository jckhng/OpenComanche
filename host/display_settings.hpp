#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace comanchecd::host {
enum class FrameLimitMode { profile_default, display, fixed, unlimited };

struct FrameLimit {
    FrameLimitMode mode = FrameLimitMode::profile_default;
    unsigned rate = 60;
};

enum class WindowMode { windowed, maximized, fullscreen };

struct WindowPosition {
    int x = 0, y = 0;
};

struct WindowPlacement {
    unsigned width = 0, height = 0;
    std::optional<WindowPosition> position;
    WindowMode mode = WindowMode::windowed;
};

constexpr unsigned minimum_frame_rate = 15;
constexpr unsigned maximum_frame_rate = 1000;
constexpr unsigned fallback_refresh_rate = 60;
constexpr unsigned minimum_window_width = 320;
constexpr unsigned minimum_window_height = 240;
constexpr unsigned maximum_window_extent = 16384;

void validate_frame_limit(const FrameLimit& limit);
void validate_window_placement(const WindowPlacement& placement);
FrameLimit parse_frame_limit(const std::string& text);
std::string frame_limit_name(const FrameLimit& limit);
WindowPlacement parse_window_size(const std::string& text, WindowPlacement placement);
WindowPosition parse_window_position(const std::string& text);
WindowMode parse_window_mode(const std::string& text);
std::string window_mode_name(WindowMode mode);

// Steady-clock deadlines include time spent rendering and discard missed frames.
class FrameDeadline {
  public:
    using Clock = std::chrono::steady_clock;
    Clock::time_point schedule(Clock::time_point now, unsigned rate);
    void reset();

  private:
    std::optional<Clock::time_point> next_;
    Clock::duration period_{};
};
} // namespace comanchecd::host
