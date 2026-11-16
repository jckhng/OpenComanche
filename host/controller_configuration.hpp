#pragma once

#include "controller_bindings.hpp"
#include "controller_response.hpp"
#include "flight_host.hpp"

namespace comanchecd::host {
constexpr std::size_t controller_stick_axes = 4;
constexpr int controller_trigger_threshold = 3276;
constexpr int controller_axis_minimum = -32768;
constexpr int controller_axis_maximum = 32767;

struct AxisCalibration {
    int minimum = controller_axis_minimum, center = 0, maximum = controller_axis_maximum;
};

struct ControllerCalibration {
    std::array<AxisCalibration, controller_stick_axes> axes{};
};

enum class ButtonPrompts { automatic, enabled, disabled };

struct ControllerConfiguration {
    ControllerTuning tuning;
    ControllerBindings bindings;
    ControllerCalibration calibration;
    ButtonPrompts prompts = ButtonPrompts::automatic;
};

struct ControllerDevice {
    std::int32_t instance = -1;
    std::string name;
    std::array<std::int16_t, controller_stick_axes> axes{};
    std::array<std::int16_t, controller_trigger_count> triggers{};
    std::array<bool, controller_button_count> buttons{};
};

void validate_controller_calibration(const ControllerCalibration& calibration);
void set_axis_calibration(ControllerCalibration& calibration, const std::string& assignment);
std::string axis_calibration_name(const AxisCalibration& calibration);
std::int16_t calibrated_axis(std::int16_t value, const AxisCalibration& calibration);
std::int16_t controller_axis_value(const ControllerDevice& device, ControllerAxis axis,
                                   const ControllerCalibration& calibration);
std::string control_label(std::size_t control);
std::string action_label(HostAction action);
std::string bound_controls(const ControllerBindings& bindings, InputContext context, HostAction action);
} // namespace comanchecd::host
