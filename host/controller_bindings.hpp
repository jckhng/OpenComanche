#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace comanchecd::host {
enum class HostAction {
    none, accept, back, up, down, left, right, pause,
    fire, target, chaff, flare, previous_weapon, next_weapon, throttle_up, throttle_down, settings, count
};

enum class ControllerButton { a, b, x, y, back, guide, start, leftstick, rightstick,
    leftshoulder, rightshoulder, dpup, dpdown, dpleft, dpright, count };
enum class ControllerAxis { none, leftx, lefty, rightx, righty };
constexpr std::size_t controller_button_count = static_cast<std::size_t>(ControllerButton::count);
constexpr std::size_t controller_trigger_count = 2;

struct ControllerBindings {
    std::array<HostAction, controller_button_count> menu{}, flight{};
    std::array<HostAction, controller_trigger_count> triggers{HostAction::none, HostAction::fire};
    ControllerAxis horizontal = ControllerAxis::leftx, vertical = ControllerAxis::lefty;
    ControllerBindings();
};

std::string_view action_name(HostAction action);
std::string_view button_name(std::size_t button);
std::string_view axis_name(ControllerAxis axis);
ControllerAxis controller_axis(const std::string& name);
void bind_controller(ControllerBindings& bindings, const std::string& assignment);
void validate_controller_bindings(const ControllerBindings& bindings);
}
