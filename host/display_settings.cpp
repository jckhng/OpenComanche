#include "display_settings.hpp"
#include <stdexcept>
#include <utility>

namespace comanchecd::host {
namespace {
constexpr int maximum_window_coordinate = 1000000;

int decimal(const std::string& text) {
    const auto offset = !text.empty() && text.front() == '-' ? 1u : 0u;
    if (text.size() <= offset || text.size() - offset > 7 ||
        text.find_first_not_of("0123456789", offset) != std::string::npos) {
        throw std::invalid_argument("Invalid decimal display setting");
    }
    return std::stoi(text);
}

std::pair<std::string, std::string> split(const std::string& text, char separator) {
    const auto at = text.find(separator);
    if (at == std::string::npos || text.find(separator, at + 1) != std::string::npos) {
        throw std::invalid_argument("Invalid display setting pair");
    }
    return {text.substr(0, at), text.substr(at + 1)};
}
} // namespace

void validate_frame_limit(const FrameLimit& limit) {
    switch (limit.mode) {
    case FrameLimitMode::profile_default:
    case FrameLimitMode::display:
    case FrameLimitMode::unlimited:
        return;
    case FrameLimitMode::fixed:
        if (limit.rate >= minimum_frame_rate && limit.rate <= maximum_frame_rate) {
            return;
        }
        break;
    }
    throw std::invalid_argument("FPS must be auto, unlimited, or 15..1000");
}

void validate_window_placement(const WindowPlacement& placement) {
    if ((placement.width != 0 || placement.height != 0) &&
        (placement.width < minimum_window_width || placement.height < minimum_window_height ||
         placement.width > maximum_window_extent || placement.height > maximum_window_extent)) {
        throw std::invalid_argument("Window size must be 320x240 through 16384x16384");
    }
    if (placement.position && (placement.position->x < -maximum_window_coordinate ||
                               placement.position->x > maximum_window_coordinate ||
                               placement.position->y < -maximum_window_coordinate ||
                               placement.position->y > maximum_window_coordinate)) {
        throw std::invalid_argument("Window position exceeds the supported desktop range");
    }
    window_mode_name(placement.mode);
}

FrameLimit parse_frame_limit(const std::string& text) {
    if (text == "auto") {
        return {FrameLimitMode::display};
    }
    if (text == "unlimited") {
        return {FrameLimitMode::unlimited};
    }
    FrameLimit limit{FrameLimitMode::fixed, static_cast<unsigned>(decimal(text))};
    validate_frame_limit(limit);
    return limit;
}

std::string frame_limit_name(const FrameLimit& limit) {
    validate_frame_limit(limit);
    switch (limit.mode) {
    case FrameLimitMode::fixed:
        return std::to_string(limit.rate);
    case FrameLimitMode::unlimited:
        return "unlimited";
    default:
        return "auto";
    }
}

WindowPlacement parse_window_size(const std::string& text, WindowPlacement placement) {
    const auto values = split(text, 'x');
    placement.width = static_cast<unsigned>(decimal(values.first));
    placement.height = static_cast<unsigned>(decimal(values.second));
    if (placement.width == 0 || placement.height == 0) {
        throw std::invalid_argument("Window dimensions must be positive");
    }
    validate_window_placement(placement);
    return placement;
}

WindowPosition parse_window_position(const std::string& text) {
    const auto values = split(text, ',');
    WindowPosition position{decimal(values.first), decimal(values.second)};
    WindowPlacement placement;
    placement.position = position;
    validate_window_placement(placement);
    return position;
}

WindowMode parse_window_mode(const std::string& text) {
    if (text == "windowed") {
        return WindowMode::windowed;
    }
    if (text == "maximized") {
        return WindowMode::maximized;
    }
    if (text == "fullscreen") {
        return WindowMode::fullscreen;
    }
    throw std::invalid_argument("Window mode must be windowed, maximized, or fullscreen");
}

std::string window_mode_name(WindowMode mode) {
    switch (mode) {
    case WindowMode::windowed:
        return "windowed";
    case WindowMode::maximized:
        return "maximized";
    case WindowMode::fullscreen:
        return "fullscreen";
    }
    throw std::invalid_argument("Invalid window mode");
}

FrameDeadline::Clock::time_point FrameDeadline::schedule(Clock::time_point now, unsigned rate) {
    if (rate == 0) {
        reset();
        return now;
    }
    const auto period =
        std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / rate));
    if (!next_ || period != period_ || now > *next_ + period) {
        next_ = now;
        period_ = period;
    }
    const auto due = *next_;
    *next_ += period;
    return due;
}

void FrameDeadline::reset() {
    next_.reset();
}
} // namespace comanchecd::host
