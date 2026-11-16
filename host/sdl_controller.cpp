#include "sdl_controller.hpp"

#include <algorithm>
#include <cstdlib>

namespace comanchecd::host {
namespace {
constexpr std::uint8_t maximum_throttle = 15;
constexpr std::array<SDL_GameControllerButton, controller_button_count> buttons{
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE, SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT};
constexpr std::array<SDL_GameControllerAxis, controller_trigger_count> triggers{
    SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_CONTROLLER_AXIS_TRIGGERRIGHT};

Sint16 axis(SDL_GameController* controller, ControllerAxis selected) {
    switch (selected) {
    case ControllerAxis::leftx: return SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    case ControllerAxis::lefty: return SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
    case ControllerAxis::rightx: return SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
    case ControllerAxis::righty: return SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY);
    default: return 0;
    }
}

SDL_Scancode menu_key(HostAction action) {
    switch (action) {
    case HostAction::accept: return SDL_SCANCODE_RETURN;
    case HostAction::back: case HostAction::settings: return SDL_SCANCODE_ESCAPE;
    case HostAction::up: return SDL_SCANCODE_UP;
    case HostAction::down: return SDL_SCANCODE_DOWN;
    case HostAction::left: return SDL_SCANCODE_LEFT;
    case HostAction::right: return SDL_SCANCODE_RIGHT;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}
}

SdlController::SdlController(const std::string& mappings, ControllerTuning tuning, ControllerBindings bindings,
                             ControllerCalibration calibration)
    : tuning_(tuning), bindings_(bindings), calibration_(calibration) {
    validate_controller_calibration(calibration_);
    validate_controller_tuning(tuning_);
    validate_controller_bindings(bindings_);
    if (!mappings.empty() && SDL_GameControllerAddMappingsFromFile(mappings.c_str()) < 0) {
        throw sdl_error("SDL2 controller mappings failed");
    }
    open_available();
}

SdlController::~SdlController() {
    if (controller_) { SDL_GameControllerClose(controller_); }
}

void SdlController::open_available() {
    if (controller_) { return; }
    for (int index = 0; index < SDL_NumJoysticks(); ++index) {
        if (!SDL_IsGameController(index)) { continue; }
        controller_ = SDL_GameControllerOpen(index);
        if (!controller_) { continue; }
        instance_ = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller_));
        for (std::size_t button = 0; button < held_.size(); ++button) {
            held_[button] = SDL_GameControllerGetButton(controller_, buttons[button]) != 0;
        }
        reset();
        return;
    }
}

bool SdlController::flight_input() const { return context_ == InputContext::flight || context_ == InputContext::training; }

void SdlController::release_key(std::size_t source) {
    const auto key = menu_keys_[source];
    menu_keys_[source] = SDL_SCANCODE_UNKNOWN;
    if (key != SDL_SCANCODE_UNKNOWN && std::find(menu_keys_.begin(), menu_keys_.end(), key) == menu_keys_.end()) {
        keys_.push_back({key, false});
    }
}

void SdlController::press_key(std::size_t source, SDL_Scancode key) {
    if (std::find(menu_keys_.begin(), menu_keys_.end(), key) == menu_keys_.end()) { keys_.push_back({key, true}); }
    menu_keys_[source] = key;
}

void SdlController::release_keys() {
    for (std::size_t source = 0; source < menu_keys_.size(); ++source) { release_key(source); }
}

void SdlController::reset() {
    release_keys();
    suppressed_ = held_;
    axes_armed_ = false;
    trigger_down_.fill(false);
}

void SdlController::set_context(InputContext context) {
    if (context == context_) { return; }
    reset();
    context_ = context;
}

void SdlController::set_focus(bool focused) { focused_ = focused; reset(); }

std::vector<ControllerKey> SdlController::take_keys() {
    auto keys = std::move(keys_);
    keys_.clear();
    return keys;
}

void SdlController::emit(HostAction action, std::size_t source, HostInput& input, std::uint8_t& throttle) {
    if (action == HostAction::pause) {
        if (context_ != InputContext::name_entry && input.pause != PauseAction::pause) { input.pause = PauseAction::toggle; }
        return;
    }
    const auto key = menu_key(action);
    if (key != SDL_SCANCODE_UNKNOWN) { press_key(source, key); return; }
    switch (action) {
    case HostAction::fire: input.pressed.fire = true; break;
    case HostAction::target: input.pressed.target = true; break;
    case HostAction::chaff: input.pressed.chaff = true; break;
    case HostAction::flare: input.pressed.flare = true; break;
    case HostAction::previous_weapon: input.controls.weapon = WeaponSelection::previous; break;
    case HostAction::next_weapon: input.controls.weapon = WeaponSelection::next; break;
    case HostAction::throttle_up: throttle = std::min<unsigned>(throttle + 1, maximum_throttle); break;
    case HostAction::throttle_down: if (throttle) { --throttle; } break;
    default: break;
    }
}

void SdlController::event(const SDL_Event& event, HostInput& input, std::uint8_t& throttle) {
    if (event.type == SDL_CONTROLLERDEVICEADDED) { open_available(); return; }
    if (controller_ && event.type == SDL_CONTROLLERDEVICEREMOVED && event.cdevice.which == instance_) {
        reset();
        SDL_GameControllerClose(controller_);
        controller_ = nullptr;
        instance_ = -1;
        held_.fill(false);
        suppressed_.fill(false);
        if (flight_input()) { input.pause = PauseAction::pause; }
        open_available();
        return;
    }
    if (!controller_ || (event.type != SDL_CONTROLLERBUTTONDOWN && event.type != SDL_CONTROLLERBUTTONUP) ||
            event.cbutton.which != instance_) { return; }
    const auto found = std::find(buttons.begin(), buttons.end(), static_cast<SDL_GameControllerButton>(event.cbutton.button));
    if (found == buttons.end()) { return; }
    const auto button = static_cast<std::size_t>(found - buttons.begin());
    const bool down = event.type == SDL_CONTROLLERBUTTONDOWN;
    const bool previous = held_[button];
    held_[button] = down;
    if (!down) {
        suppressed_[button] = false;
        release_key(button);
        return;
    }
    if (previous || suppressed_[button] || !focused_) { return; }
    input.key_pressed = true;
    emit(flight_input() ? bindings_.flight[button] : bindings_.menu[button], button, input, throttle);
}

ControllerDevice SdlController::snapshot() const {
    ControllerDevice device;
    if (!controller_) { return device; }
    device.instance = instance_;
    const auto* name = SDL_GameControllerName(controller_);
    device.name = name ? name : "SDL2 game controller";
    for (std::size_t index = 0; index < device.axes.size(); ++index) {
        device.axes[index] = axis(controller_, static_cast<ControllerAxis>(index + 1));
    }
    for (std::size_t index = 0; index < triggers.size(); ++index) {
        device.triggers[index] = SDL_GameControllerGetAxis(controller_, triggers[index]);
    }
    for (std::size_t index = 0; index < buttons.size(); ++index) {
        device.buttons[index] = SDL_GameControllerGetButton(controller_, buttons[index]) != 0;
    }
    return device;
}

void SdlController::configure(ControllerTuning tuning, ControllerBindings bindings, ControllerCalibration calibration) {
    validate_controller_tuning(tuning);
    validate_controller_bindings(bindings);
    validate_controller_calibration(calibration);
    reset();
    tuning_ = tuning;
    bindings_ = bindings;
    calibration_ = calibration;
}

void SdlController::sample(HostInput& input, std::uint8_t& throttle) {
    if (!controller_ || !focused_ || !flight_input()) { return; }
    const auto calibrated = [&](ControllerAxis selected) -> std::int16_t {
        return selected == ControllerAxis::none ? 0 :
            calibrated_axis(axis(controller_, selected), calibration_.axes[static_cast<std::size_t>(selected) - 1]);
    };
    const auto x = calibrated(bindings_.horizontal);
    const auto y = calibrated(bindings_.vertical);
    std::array<bool, controller_trigger_count> pressed{};
    for (std::size_t trigger = 0; trigger < triggers.size(); ++trigger) {
        pressed[trigger] = bindings_.triggers[trigger] != HostAction::none &&
            SDL_GameControllerGetAxis(controller_, triggers[trigger]) > controller_trigger_threshold;
    }
    if (!axes_armed_) {
        axes_armed_ = std::abs(int(x)) <= tuning_.deadzone && std::abs(int(y)) <= tuning_.deadzone &&
            std::none_of(pressed.begin(), pressed.end(), [](bool down) { return down; });
    }
    if (axes_armed_) {
        if (input.controls.horizontal == 0) { input.controls.horizontal = controller_steering(x, tuning_); }
        if (input.controls.vertical == 0) {
            const auto pitch = controller_steering(y, tuning_);
            input.controls.vertical = tuning_.pitch == PitchDirection::inverted ? -pitch : pitch;
        }
        for (std::size_t trigger = 0; trigger < triggers.size(); ++trigger) {
            const auto source = controller_button_count + trigger;
            if (pressed[trigger] && !trigger_down_[trigger]) { emit(bindings_.triggers[trigger], source, input, throttle); }
            if (!pressed[trigger] && trigger_down_[trigger]) { release_key(source); }
            input.controls.fire = input.controls.fire || (pressed[trigger] && bindings_.triggers[trigger] == HostAction::fire);
            trigger_down_[trigger] = pressed[trigger];
        }
    }
    for (std::size_t button = 0; button < held_.size(); ++button) {
        input.controls.fire = input.controls.fire ||
            (bindings_.flight[button] == HostAction::fire && held_[button] && !suppressed_[button]);
    }
    input.controls.throttle = throttle;
}
}
