#include "controller_menu.hpp"
#include <cmath>

namespace comanchecd::host {
namespace {
constexpr int calibration_travel = 8192;
constexpr int minimum_calibrated_deadzone = 1024;
constexpr int maximum_calibrated_deadzone = 12000;
constexpr int noise_margin = 512;
constexpr unsigned visible_binding_rows = 10;

std::string upper(std::string text) {
    for (auto& value : text) {
        if (value >= 'a' && value <= 'z') {
            value -= 'a' - 'A';
        }
    }
    return text;
}

std::string prompt_mode(ButtonPrompts mode) {
    switch (mode) {
    case ButtonPrompts::automatic:
        return "AUTO";
    case ButtonPrompts::enabled:
        return "ON";
    case ButtonPrompts::disabled:
        return "OFF";
    }
    throw std::invalid_argument("Invalid button prompt mode");
}
} // namespace

ControllerMenu::ControllerMenu(ControllerConfiguration settings) : settings_(std::move(settings)) {
    validate_controller_tuning(settings_.tuning);
    validate_controller_bindings(settings_.bindings);
    validate_controller_calibration(settings_.calibration);
    prompt_mode(settings_.prompts);
}

std::vector<HostAction> ControllerMenu::actions() const {
    if (page_ == Page::menu_bindings) {
        return {HostAction::accept, HostAction::back,  HostAction::up,   HostAction::down,
                HostAction::left,   HostAction::right, HostAction::pause};
    }
    return {HostAction::fire,        HostAction::target,          HostAction::chaff,
            HostAction::flare,       HostAction::previous_weapon, HostAction::next_weapon,
            HostAction::throttle_up, HostAction::throttle_down,   HostAction::settings,
            HostAction::pause};
}

void ControllerMenu::update(const ControllerDevice& device, double seconds) {
    seconds_ = seconds;
    if (device.instance != device_.instance) {
        capturing_ = false;
        calibration_stage_ = CalibrationStage::ready;
        message_ = device.instance < 0 ? "Controller disconnected" : "Controller connected";
    }
    device_ = device;
    if (device.instance < 0) {
        return;
    }
    if (capturing_) {
        if (seconds - capture_started_ > 8) {
            capturing_ = false;
            message_ = "Binding capture timed out";
        } else if (!capture_armed_) {
            capture_armed_ =
                std::none_of(device.buttons.begin(), device.buttons.end(), [](bool held) { return held; }) &&
                std::none_of(device.triggers.begin(), device.triggers.end(),
                             [](int value) { return value > controller_trigger_threshold; });
        } else if (page_ == Page::flight_bindings) {
            for (std::size_t trigger = 0; trigger < device.triggers.size(); ++trigger) {
                if (device.triggers[trigger] > controller_trigger_threshold) {
                    capture(controller_button_count + trigger);
                    break;
                }
            }
        }
    }
    if (calibration_stage_ == CalibrationStage::centering) {
        for (std::size_t axis = 0; axis < controller_stick_axes; ++axis) {
            centers_[axis] += device.axes[axis];
            noise_min_[axis] = std::min<int>(noise_min_[axis], device.axes[axis]);
            noise_max_[axis] = std::max<int>(noise_max_[axis], device.axes[axis]);
        }
        ++center_samples_;
        if (seconds - center_started_ >= 1 && center_samples_ >= 8) {
            for (std::size_t axis = 0; axis < controller_stick_axes; ++axis) {
                candidate_.axes[axis].center = static_cast<int>(centers_[axis] / center_samples_);
                minimum_[axis] = maximum_[axis] = candidate_.axes[axis].center;
            }
            calibration_stage_ = CalibrationStage::range;
            message_ = "Sweep sticks, then press A / Enter";
        }
    } else if (calibration_stage_ == CalibrationStage::range) {
        for (std::size_t axis = 0; axis < controller_stick_axes; ++axis) {
            minimum_[axis] = std::min<int>(minimum_[axis], device.axes[axis]);
            maximum_[axis] = std::max<int>(maximum_[axis], device.axes[axis]);
        }
    }
}

void ControllerMenu::capture(std::size_t control) {
    if (!capturing_ || !capture_armed_ || control >= controller_button_count + controller_trigger_count) {
        return;
    }
    if (control >= controller_button_count && page_ == Page::menu_bindings) {
        message_ = "Menu actions use buttons";
        return;
    }
    const auto action = actions().at(binding_row_);
    if (control < controller_button_count) {
        auto& buttons = page_ == Page::menu_bindings ? settings_.bindings.menu : settings_.bindings.flight;
        buttons[control] = action;
    } else {
        settings_.bindings.triggers[control - controller_button_count] = action;
    }
    capturing_ = false;
    message_ = control_label(control) + " bound. Apply to save";
}

void ControllerMenu::finish_calibration() {
    auto calibration = settings_.calibration;
    int noise = 0;
    for (const auto selected : {settings_.bindings.horizontal, settings_.bindings.vertical}) {
        if (selected == ControllerAxis::none) {
            continue;
        }
        const auto axis = static_cast<std::size_t>(selected) - 1;
        const auto center = candidate_.axes[axis].center;
        if (center - minimum_[axis] < calibration_travel || maximum_[axis] - center < calibration_travel) {
            message_ = "Move each selected axis fully both ways";
            return;
        }
        calibration.axes[axis] = {minimum_[axis], center, maximum_[axis]};
        noise = std::max(noise, std::abs(int(calibrated_axis(static_cast<std::int16_t>(noise_min_[axis]),
                                                             calibration.axes[axis]))));
        noise = std::max(noise, std::abs(int(calibrated_axis(static_cast<std::int16_t>(noise_max_[axis]),
                                                             calibration.axes[axis]))));
    }
    if (noise + noise_margin > maximum_calibrated_deadzone) {
        calibration_stage_ = CalibrationStage::ready;
        message_ = "Center was unstable. Release sticks and retry";
        return;
    }
    settings_.calibration = calibration;
    settings_.tuning.deadzone = std::max(minimum_calibrated_deadzone, noise + noise_margin);
    calibration_stage_ = CalibrationStage::finished;
    message_ = "Calibrated. Back, then Apply to save";
}

void ControllerMenu::change(int direction) {
    switch (static_cast<Row>(row_)) {
    case Row::horizontal:
    case Row::vertical: {
        auto& axis = static_cast<Row>(row_) == Row::horizontal ? settings_.bindings.horizontal
                                                               : settings_.bindings.vertical;
        axis = static_cast<ControllerAxis>((int(axis) + direction + 5) % 5);
        break;
    }
    case Row::deadzone:
        settings_.tuning.deadzone = std::clamp(settings_.tuning.deadzone + direction * 512, 0, 32766);
        break;
    case Row::sensitivity:
        settings_.tuning.sensitivity = std::clamp(settings_.tuning.sensitivity + direction * 5, 1, 400);
        break;
    case Row::pitch:
        settings_.tuning.pitch = settings_.tuning.pitch == PitchDirection::normal ? PitchDirection::inverted
                                                                                  : PitchDirection::normal;
        break;
    case Row::prompts:
        settings_.prompts = static_cast<ButtonPrompts>((int(settings_.prompts) + direction + 3) % 3);
        break;
    default:
        return;
    }
    message_ = "Apply to save changes";
}

void ControllerMenu::accept() {
    if (page_ == Page::menu_bindings || page_ == Page::flight_bindings) {
        if (device_.instance < 0) {
            message_ = "Connect an SDL2 game controller";
            return;
        }
        capturing_ = true;
        capture_armed_ = false;
        capture_started_ = seconds_;
        message_ = "Release buttons, then press the new control";
        return;
    }
    if (page_ == Page::calibration) {
        if (device_.instance < 0) {
            message_ = "Connect an SDL2 game controller";
            return;
        }
        if (calibration_stage_ == CalibrationStage::range) {
            finish_calibration();
            return;
        }
        if (calibration_stage_ == CalibrationStage::centering) {
            return;
        }
        if (settings_.bindings.horizontal == ControllerAxis::none &&
            settings_.bindings.vertical == ControllerAxis::none) {
            message_ = "Choose steering axes before calibrating";
            return;
        }
        candidate_ = settings_.calibration;
        centers_.fill(0);
        for (std::size_t axis = 0; axis < controller_stick_axes; ++axis) {
            noise_min_[axis] = noise_max_[axis] = device_.axes[axis];
        }
        center_samples_ = 0;
        center_started_ = seconds_;
        calibration_stage_ = CalibrationStage::centering;
        message_ = "Keep both sticks released for one second";
        return;
    }
    switch (static_cast<Row>(row_)) {
    case Row::menu_bindings:
        page_ = Page::menu_bindings;
        binding_row_ = 0;
        break;
    case Row::flight_bindings:
        page_ = Page::flight_bindings;
        binding_row_ = 0;
        break;
    case Row::calibration:
        page_ = Page::calibration;
        calibration_stage_ = CalibrationStage::ready;
        break;
    case Row::defaults:
        settings_ = {};
        message_ = "Defaults staged. Apply to save";
        break;
    case Row::apply:
        apply_ = true;
        break;
    case Row::close:
        close_ = true;
        break;
    default:
        change(1);
        break;
    }
}

void ControllerMenu::command(ControllerMenuCommand command) {
    if (command == ControllerMenuCommand::close) {
        close_ = true;
        return;
    }
    if (command == ControllerMenuCommand::back) {
        if (capturing_) {
            capturing_ = false;
            message_ = "Binding capture cancelled";
            return;
        }
        if (page_ == Page::overview) {
            close_ = true;
            return;
        }
        page_ = Page::overview;
        calibration_stage_ = CalibrationStage::ready;
        return;
    }
    if (capturing_) {
        return;
    }
    if (command == ControllerMenuCommand::accept) {
        accept();
        return;
    }
    if (page_ == Page::calibration) {
        return;
    }
    const bool overview = page_ == Page::overview;
    auto& row = overview ? row_ : binding_row_;
    const auto count = overview ? static_cast<unsigned>(Row::count) : static_cast<unsigned>(actions().size());
    if (command == ControllerMenuCommand::up) {
        row = (row + count - 1) % count;
    }
    if (command == ControllerMenuCommand::down) {
        row = (row + 1) % count;
    }
    if (overview && command == ControllerMenuCommand::left) {
        change(-1);
    }
    if (overview && command == ControllerMenuCommand::right) {
        change(1);
    }
    if (!overview && command == ControllerMenuCommand::clear) {
        const auto action = actions().at(row);
        auto& buttons = page_ == Page::menu_bindings ? settings_.bindings.menu : settings_.bindings.flight;
        for (auto& binding : buttons) {
            if (binding == action) {
                binding = HostAction::none;
            }
        }
        if (page_ == Page::flight_bindings) {
            for (auto& binding : settings_.bindings.triggers) {
                if (binding == action) {
                    binding = HostAction::none;
                }
            }
        }
        message_ = "Action cleared. Apply to save";
    }
}

void ControllerMenu::suspend() {
    capturing_ = false;
    calibration_stage_ = CalibrationStage::ready;
    message_ = "Focus lost. Retry calibration or binding";
}

bool ControllerMenu::capturing() const {
    return capturing_;
}
bool ControllerMenu::close_requested() const {
    return close_;
}
bool ControllerMenu::take_apply_request() {
    const auto result = apply_;
    apply_ = false;
    return result;
}
void ControllerMenu::save_result(const std::string& error) {
    message_ = error.empty() ? "Saved" : "Save failed. Changes retained, retry Apply";
}
const ControllerConfiguration& ControllerMenu::settings() const {
    return settings_;
}

CockpitImage ControllerMenu::image() const {
    auto image = ui_image();
    ui_rectangle(image, 8, 8, 304, 28, UiColor::panel);
    ui_text(image, 16, 12, "ENHANCED CONTROLLER", UiColor::accent);
    ui_text(image, 16, 24, (device_.instance < 0 ? "NO CONTROLLER CONNECTED" : device_.name).substr(0, 48));
    if (page_ == Page::overview) {
        const std::array<std::string, static_cast<unsigned>(Row::count)> rows{
            "Menu bindings",
            "Flight bindings",
            "Calibrate sticks",
            "Horizontal axis     " + upper(std::string(axis_name(settings_.bindings.horizontal))),
            "Vertical axis       " + upper(std::string(axis_name(settings_.bindings.vertical))),
            "Deadzone            " + std::to_string(settings_.tuning.deadzone),
            "Sensitivity         " + std::to_string(settings_.tuning.sensitivity) + "%",
            "Invert pitch        " +
                std::string(settings_.tuning.pitch == PitchDirection::inverted ? "ON" : "OFF"),
            "Button prompts      " + prompt_mode(settings_.prompts),
            "Restore defaults",
            "Apply and save",
            "Close"};
        for (unsigned row = 0; row < rows.size(); ++row) {
            const auto y = 44 + int(row) * 12;
            if (row == row_) {
                ui_rectangle(image, 10, y - 2, 300, 11, UiColor::panel);
            }
            ui_text(image, 16, y, (row == row_ ? "> " : "  ") + rows[row],
                    row == row_ ? UiColor::accent : UiColor::text);
        }
        const auto x = controller_steering(
            controller_axis_value(device_, settings_.bindings.horizontal, settings_.calibration),
            settings_.tuning);
        auto y = controller_steering(
            controller_axis_value(device_, settings_.bindings.vertical, settings_.calibration),
            settings_.tuning);
        if (settings_.tuning.pitch == PitchDirection::inverted) { y = -y; }
        ui_text(image, 16, 194,
                "LIVE STEER X " + std::to_string(x * 100 / 350) + "%  Y " + std::to_string(y * 100 / 350) +
                    "%");
        ui_text(image, 16, 224, "ARROWS / D-PAD MOVE  ENTER / A  ESC / B");
    } else if (page_ == Page::calibration) {
        ui_text(image, 16, 48, "STICK CENTER AND TRAVEL", UiColor::accent);
        ui_text(image, 16, 64, "1. RELEASE BOTH STICKS, PRESS A / ENTER");
        ui_text(image, 16, 76, "2. WAIT, THEN SWEEP SELECTED AXES FULLY");
        ui_text(image, 16, 88, "3. PRESS A / ENTER TO FINISH");
        for (unsigned axis = 0; axis < controller_stick_axes; ++axis) {
            const auto label = std::string(axis_name(static_cast<ControllerAxis>(axis + 1)));
            ui_text(image, 16, 112 + axis * 16,
                    label + " " + std::to_string(device_.axes[axis]) + "   " +
                        std::to_string(minimum_[axis]) + " / " + std::to_string(maximum_[axis]));
        }
        ui_text(image, 16, 224, "A / ENTER CONFIRM   B / ESC BACK");
    } else {
        const auto flight = page_ == Page::flight_bindings;
        ui_text(image, 16, 45, flight ? "FLIGHT ACTIONS" : "MENU ACTIONS", UiColor::accent);
        const auto list = actions();
        const auto first = binding_row_ >= visible_binding_rows ? binding_row_ - visible_binding_rows + 1 : 0;
        for (unsigned row = first; row < std::min<unsigned>(list.size(), first + visible_binding_rows);
             ++row) {
            const auto y = 62 + int(row - first) * 13;
            if (row == binding_row_) {
                ui_rectangle(image, 10, y - 2, 300, 12, UiColor::panel);
            }
            ui_text(image, 16, y, action_label(list[row]),
                    row == binding_row_ ? UiColor::accent : UiColor::text);
            ui_text(image, 152, y,
                    bound_controls(settings_.bindings, flight ? InputContext::flight : InputContext::menu,
                                   list[row])
                        .substr(0, 26));
        }
        ui_text(image, 16, 194,
                capturing_ ? "PRESS CONTROL. ESC CANCELS. 8S TIMEOUT"
                           : "A / ENTER ADD CONTROL   X / DEL CLEAR");
        ui_text(image, 16, 224, "D-PAD MOVE   B / ESC BACK");
    }
    ui_text(image, 16, 210, message_.substr(0, 48), UiColor::warning);
    return image;
}
} // namespace comanchecd::host
