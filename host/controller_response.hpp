#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>

namespace comanchecd::host {
enum class PitchDirection { normal, inverted };

struct ControllerTuning {
    int deadzone = 8000;
    int sensitivity = 100;
    PitchDirection pitch = PitchDirection::normal;
};

inline void validate_controller_tuning(const ControllerTuning& tuning) {
    constexpr int maximum_deadzone = 32766;
    constexpr int maximum_sensitivity = 400;
    if (tuning.deadzone < 0 || tuning.deadzone > maximum_deadzone ||
            tuning.sensitivity < 1 || tuning.sensitivity > maximum_sensitivity ||
            (tuning.pitch != PitchDirection::normal && tuning.pitch != PitchDirection::inverted)) {
        throw std::invalid_argument("Stick deadzone must be 0..32766 and sensitivity must be 1..400 percent");
    }
}

inline int controller_steering(std::int16_t value, const ControllerTuning& tuning) {
    validate_controller_tuning(tuning);
    constexpr int steering_limit = 350;
    constexpr int normal_sensitivity = 100;
    const int magnitude = std::abs(int(value));
    if (magnitude <= tuning.deadzone) { return 0; }
    const int extent = value < 0 ? 32768 : 32767;
    const auto numerator = std::int64_t(magnitude - tuning.deadzone) * steering_limit * tuning.sensitivity;
    const auto denominator = std::int64_t(extent - tuning.deadzone) * normal_sensitivity;
    const int scaled = static_cast<int>(std::min<std::int64_t>(steering_limit, numerator / denominator));
    return value < 0 ? -scaled : scaled;
}
}
