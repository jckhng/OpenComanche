#pragma once

#include "flight_host.hpp"
#include "controller_response.hpp"
#include "controller_bindings.hpp"
#include "sdl_common.hpp"

namespace comanchecd::host {
struct ControllerKey {
    SDL_Scancode key;
    bool down;
};

class SdlController {
public:
    explicit SdlController(const std::string& mappings, ControllerTuning tuning = {}, ControllerBindings bindings = {});
    ~SdlController();
    void event(const SDL_Event& event, HostInput& input, std::uint8_t& throttle);
    void sample(HostInput& input, std::uint8_t& throttle);
    void set_context(InputContext context);
    void set_focus(bool focused);
    void reset();
    std::vector<ControllerKey> take_keys();
private:
    void open_available();
    bool flight_input() const;
    void release_keys();
    void release_key(std::size_t source);
    void press_key(std::size_t source, SDL_Scancode key);
    void emit(HostAction action, std::size_t source, HostInput& input, std::uint8_t& throttle);
    SdlSubsystem subsystem_{SDL_INIT_GAMECONTROLLER};
    SDL_GameController* controller_ = nullptr;
    SDL_JoystickID instance_ = -1;
    InputContext context_ = InputContext::flight;
    bool focused_ = true, axes_armed_ = false;
    std::array<bool, controller_trigger_count> trigger_down_{};
    std::array<bool, controller_button_count> held_{}, suppressed_{};
    std::array<SDL_Scancode, controller_button_count + controller_trigger_count> menu_keys_{};
    std::vector<ControllerKey> keys_;
    ControllerTuning tuning_;
    ControllerBindings bindings_;
};
}
