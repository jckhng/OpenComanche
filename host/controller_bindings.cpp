#include "controller_bindings.hpp"

#include <algorithm>
#include <stdexcept>

namespace comanchecd::host {
namespace {
constexpr std::array<std::string_view, static_cast<std::size_t>(HostAction::count)> actions{"none", "accept", "back", "up", "down", "left", "right", "pause",
    "fire", "target", "chaff", "flare", "previous-weapon", "next-weapon", "throttle-up", "throttle-down", "settings"};
constexpr std::array<std::string_view, controller_button_count> buttons{"a", "b", "x", "y", "back", "guide", "start",
    "leftstick", "rightstick", "leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright"};
constexpr std::array<std::string_view, 5> axes{"none", "leftx", "lefty", "rightx", "righty"};

bool menu_action(HostAction action) { return action >= HostAction::none && action <= HostAction::pause; }
bool flight_action(HostAction action) {
    return action == HostAction::none || action == HostAction::pause ||
        (action >= HostAction::fire && action <= HostAction::settings);
}
}

ControllerBindings::ControllerBindings() {
    menu[static_cast<std::size_t>(ControllerButton::a)] = HostAction::accept;
    menu[static_cast<std::size_t>(ControllerButton::b)] = HostAction::back;
    menu[static_cast<std::size_t>(ControllerButton::start)] = HostAction::pause;
    menu[static_cast<std::size_t>(ControllerButton::dpup)] = HostAction::up;
    menu[static_cast<std::size_t>(ControllerButton::dpdown)] = HostAction::down;
    menu[static_cast<std::size_t>(ControllerButton::dpleft)] = HostAction::left;
    menu[static_cast<std::size_t>(ControllerButton::dpright)] = HostAction::right;
    flight[static_cast<std::size_t>(ControllerButton::a)] = HostAction::fire;
    flight[static_cast<std::size_t>(ControllerButton::b)] = HostAction::flare;
    flight[static_cast<std::size_t>(ControllerButton::x)] = HostAction::chaff;
    flight[static_cast<std::size_t>(ControllerButton::y)] = HostAction::target;
    flight[static_cast<std::size_t>(ControllerButton::back)] = HostAction::settings;
    flight[static_cast<std::size_t>(ControllerButton::start)] = HostAction::pause;
    flight[static_cast<std::size_t>(ControllerButton::leftshoulder)] = HostAction::previous_weapon;
    flight[static_cast<std::size_t>(ControllerButton::rightshoulder)] = HostAction::next_weapon;
    flight[static_cast<std::size_t>(ControllerButton::dpup)] = HostAction::throttle_up;
    flight[static_cast<std::size_t>(ControllerButton::dpdown)] = HostAction::throttle_down;
}

std::string_view action_name(HostAction action) { return actions.at(static_cast<std::size_t>(action)); }
std::string_view button_name(std::size_t button) { return buttons.at(button); }
std::string_view axis_name(ControllerAxis axis) { return axes.at(static_cast<std::size_t>(axis)); }

ControllerAxis controller_axis(const std::string& name) {
    const auto found = std::find(axes.begin(), axes.end(), name);
    if (found == axes.end()) { throw std::invalid_argument("Steering axis must be none, leftx, lefty, rightx or righty"); }
    return static_cast<ControllerAxis>(found - axes.begin());
}

void bind_controller(ControllerBindings& bindings, const std::string& assignment) {
    const auto equal = assignment.find('=');
    const auto dot = assignment.find('.');
    if (equal == std::string::npos || dot == std::string::npos || dot >= equal) {
        throw std::invalid_argument("Controller binding must be menu.CONTROL=ACTION or flight.CONTROL=ACTION");
    }
    const auto context = assignment.substr(0, dot);
    const auto control = assignment.substr(dot + 1, equal - dot - 1);
    const auto name = assignment.substr(equal + 1);
    const auto found = std::find(actions.begin(), actions.end(), name);
    if (found == actions.end()) { throw std::invalid_argument("Unknown controller action (" + name + ")"); }
    const auto action = static_cast<HostAction>(found - actions.begin());
    if ((context != "menu" && context != "flight") ||
            (context == "menu" ? !menu_action(action) : !flight_action(action))) {
        throw std::invalid_argument("Controller action is invalid for its input context");
    }
    if (context == "flight" && (control == "lefttrigger" || control == "righttrigger")) {
        bindings.triggers[control == "lefttrigger" ? 0 : 1] = action;
        return;
    }
    const auto button = std::find(buttons.begin(), buttons.end(), control);
    if (button == buttons.end()) { throw std::invalid_argument("Unknown controller button (" + control + ")"); }
    auto& target = context == "menu" ? bindings.menu : bindings.flight;
    target[static_cast<std::size_t>(button - buttons.begin())] = action;
}

void validate_controller_bindings(const ControllerBindings& bindings) {
    if (!std::all_of(bindings.menu.begin(), bindings.menu.end(), menu_action) ||
            !std::all_of(bindings.flight.begin(), bindings.flight.end(), flight_action) ||
            !std::all_of(bindings.triggers.begin(), bindings.triggers.end(), flight_action) ||
            static_cast<std::size_t>(bindings.horizontal) >= axes.size() ||
            static_cast<std::size_t>(bindings.vertical) >= axes.size()) {
        throw std::invalid_argument("Invalid controller bindings");
    }
}
}
