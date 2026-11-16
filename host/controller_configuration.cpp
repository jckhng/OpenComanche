#include "controller_configuration.hpp"
#include <charconv>
#include <algorithm>

namespace comanchecd::host {
void validate_controller_calibration(const ControllerCalibration& calibration) {
    for (const auto& axis : calibration.axes) {
        if (axis.minimum < controller_axis_minimum || axis.maximum > controller_axis_maximum || axis.minimum >= axis.center ||
            axis.center >= axis.maximum) {
            throw std::invalid_argument(
                "Axis calibration requires -32768 <= minimum < center < maximum <= 32767");
        }
    }
}

void set_axis_calibration(ControllerCalibration& calibration, const std::string& assignment) {
    const auto equal = assignment.find('=');
    if (equal == std::string::npos) {
        throw std::invalid_argument("Axis calibration requires AXIS=MINIMUM,CENTER,MAXIMUM");
    }
    const auto axis = controller_axis(assignment.substr(0, equal));
    if (axis == ControllerAxis::none) {
        throw std::invalid_argument("Choose a physical stick axis for calibration");
    }
    std::array<int, 3> values{};
    auto start = equal + 1;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto end = index + 1 == values.size() ? assignment.size() : assignment.find(',', start);
        if (end == std::string::npos || end == start) {
            throw std::invalid_argument("Axis calibration requires three signed decimal values");
        }
        const auto parsed =
            std::from_chars(assignment.data() + start, assignment.data() + end, values[index]);
        if (parsed.ec != std::errc{} || parsed.ptr != assignment.data() + end) {
            throw std::invalid_argument("Invalid axis calibration value");
        }
        start = end + 1;
    }
    auto candidate = calibration;
    candidate.axes[static_cast<std::size_t>(axis) - 1] = {values[0], values[1], values[2]};
    validate_controller_calibration(candidate);
    calibration = candidate;
}

std::string axis_calibration_name(const AxisCalibration& calibration) {
    return std::to_string(calibration.minimum) + ',' + std::to_string(calibration.center) + ',' +
           std::to_string(calibration.maximum);
}

std::int16_t calibrated_axis(std::int16_t value, const AxisCalibration& calibration) {
    const auto offset = int(value) - calibration.center;
    const auto extent =
        offset < 0 ? calibration.center - calibration.minimum : calibration.maximum - calibration.center;
    const auto scaled = std::int64_t(offset) * (offset < 0 ? -controller_axis_minimum : controller_axis_maximum) / extent;
    return static_cast<std::int16_t>(std::clamp<std::int64_t>(scaled, controller_axis_minimum, controller_axis_maximum));
}

std::int16_t controller_axis_value(const ControllerDevice& device, ControllerAxis axis,
                                   const ControllerCalibration& calibration) {
    if (axis == ControllerAxis::none) {
        return 0;
    }
    const auto index = static_cast<std::size_t>(axis) - 1;
    return calibrated_axis(device.axes.at(index), calibration.axes.at(index));
}

std::string control_label(std::size_t control) {
    constexpr std::array<const char*, controller_button_count + controller_trigger_count> labels{
        "A",  "B",  "X",    "Y",      "BACK",   "GUIDE",   "START", "LS", "RS",
        "LB", "RB", "D-UP", "D-DOWN", "D-LEFT", "D-RIGHT", "LT",    "RT"};
    return labels.at(control);
}

std::string action_label(HostAction action) {
    std::string result(action_name(action));
    std::replace(result.begin(), result.end(), '-', ' ');
    return result;
}

std::string bound_controls(const ControllerBindings& bindings, InputContext context, HostAction action) {
    const bool flight = context == InputContext::flight || context == InputContext::training;
    const auto& buttons = flight ? bindings.flight : bindings.menu;
    std::string result;
    const auto append = [&](std::size_t control) {
        if (!result.empty()) {
            result += '/';
        }
        result += control_label(control);
    };
    for (std::size_t index = 0; index < buttons.size(); ++index) {
        if (buttons[index] == action) {
            append(index);
        }
    }
    if (flight) {
        for (std::size_t index = 0; index < bindings.triggers.size(); ++index) {
            if (bindings.triggers[index] == action) {
                append(controller_button_count + index);
            }
        }
    }
    return result.empty() ? "UNBOUND" : result;
}
} // namespace comanchecd::host
