#include "sdl_host.hpp"
#include "sdl_common.hpp"
#include "sdl_options.hpp"

#include <algorithm>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}

void keyboard(Uint32 type, SDL_Scancode key) {
    SDL_Event event{};
    event.type = type;
    event.key.keysym.scancode = key;
    require(SDL_PushEvent(&event) == 1, "Keyboard fixture must enter the SDL2 queue");
}
}

int main() {
#if !SDL_VERSION_ATLEAST(2, 0, 14)
    std::cout << "Virtual controller qualification requires SDL2 2.0.14 or later\n";
    return 77;
#else
    try {
        using namespace comanchecd;
        using namespace comanchecd::host;
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        SdlSubsystem subsystem(SDL_INIT_GAMECONTROLLER);
        for (int index = 0; index < SDL_NumJoysticks(); ++index) {
            if (SDL_IsGameController(index)) {
                std::cout << "Virtual controller isolation requires a process without another active game controller\n";
                return 77;
            }
        }
        const auto device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
            SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
        require(device >= 0, "Virtual SDL2 game controller must attach");
        std::unique_ptr<SDL_Joystick, decltype(&SDL_JoystickClose)> joystick(SDL_JoystickOpen(device), SDL_JoystickClose);
        require(bool(joystick), "Virtual joystick must open");
        char guid[33];
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick.get()), guid, sizeof(guid));
        const auto mapping = std::string(guid) + ",Comanche test controller,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6," +
            "leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14," +
            "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,";
        require(SDL_GameControllerAddMapping(mapping.c_str()) >= 0, "Virtual controller mapping must install");
        require(SDL_JoystickSetVirtualAxis(joystick.get(), SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768) == 0,
                "Trigger must begin released");
        auto host = std::make_unique<SdlFlightHost>(SdlHostOptions{InputProfile::enhanced, {}});
        host->poll();
        const auto button = [&](SDL_GameControllerButton key, bool down) {
            require(SDL_JoystickSetVirtualButton(joystick.get(), key, down ? 1 : 0) == 0, "Virtual button must update");
            return host->poll();
        };
        host->set_input_context(InputContext::menu);
        host->poll();
        auto input = button(SDL_CONTROLLER_BUTTON_A, true);
        require(input.scan_bytes == std::vector<std::uint8_t>{0x1c} && !input.controls.fire,
                "Controller A must send an IRQ-bound Enter in menus");
        keyboard(SDL_KEYDOWN, SDL_SCANCODE_RETURN);
        host->poll();
        input = button(SDL_CONTROLLER_BUTTON_A, false);
        require(std::find(input.scan_bytes.begin(), input.scan_bytes.end(), 0x9c) == input.scan_bytes.end(),
                "Controller release must preserve a physically held keyboard Enter");
        keyboard(SDL_KEYUP, SDL_SCANCODE_RETURN);
        require(host->poll().scan_bytes == std::vector<std::uint8_t>{0x9c}, "The final Enter owner must send its release");
        button(SDL_CONTROLLER_BUTTON_A, true);
        host->set_input_context(InputContext::flight);
        require(!host->poll().controls.fire, "Accept held across mission entry must remain suppressed until release");
        button(SDL_CONTROLLER_BUTTON_A, false);
        input = button(SDL_CONTROLLER_BUTTON_A, true);
        require(input.controls.fire && input.pressed.fire, "Fresh flight fire must provide held and buffered input");
        button(SDL_CONTROLLER_BUTTON_A, false);
        require(button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, true).controls.weapon == WeaponSelection::next,
                "Shoulder press must select the next weapon");
        require(host->poll().controls.weapon == WeaponSelection::unchanged, "Weapon selection must remain transient");
        button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, false);
        require(button(SDL_CONTROLLER_BUTTON_Y, true).pressed.target, "Y must buffer target acquisition");
        button(SDL_CONTROLLER_BUTTON_Y, false);
        require(button(SDL_CONTROLLER_BUTTON_DPAD_UP, true).controls.throttle == 1, "D-pad Up must increase retained throttle");
        button(SDL_CONTROLLER_BUTTON_DPAD_UP, false);
        require(SDL_JoystickSetVirtualAxis(joystick.get(), SDL_CONTROLLER_AXIS_LEFTX, 32767) == 0, "Steering axis must update");
        require(SDL_JoystickSetVirtualAxis(joystick.get(), SDL_CONTROLLER_AXIS_LEFTY, -32768) == 0, "Pitch axis must update");
        input = host->poll();
        require(input.controls.horizontal == 350 && input.controls.vertical == -350,
                "Analog extrema must stay within the recovered steering range");
        SDL_Event focus{};
        focus.type = SDL_WINDOWEVENT;
        focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
        SDL_PushEvent(&focus);
        input = host->poll();
        require(input.pause == PauseAction::pause && input.controls.horizontal == 0 && !input.controls.fire,
                "Focus loss must pause and neutralize controller input");
        focus.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
        SDL_PushEvent(&focus);
        require(host->poll().controls.horizontal == 0, "Returning focus must require neutral axes before steering resumes");
        SDL_JoystickSetVirtualAxis(joystick.get(), SDL_CONTROLLER_AXIS_LEFTX, 0);
        SDL_JoystickSetVirtualAxis(joystick.get(), SDL_CONTROLLER_AXIS_LEFTY, 0);
        host->poll();
        SDL_JoystickSetVirtualAxis(joystick.get(), SDL_CONTROLLER_AXIS_LEFTX, 7999);
        require(host->poll().controls.horizontal == 0, "Stick deadzone must suppress drift");
        SDL_JoystickSetVirtualAxis(joystick.get(), SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767);
        input = host->poll();
        require(input.controls.fire && input.pressed.fire, "Trigger crossing must provide held and buffered fire");
        require(SDL_JoystickDetachVirtual(device) == 0, "Virtual controller must detach");
        input = host->poll();
        require(input.pause == PauseAction::pause && !input.controls.fire && input.controls.horizontal == 0,
                "Disconnect must release actions and pause the active flight");
        const auto replacement = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
            SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
        require(replacement >= 0, "Replacement controller must hot-plug");
        std::unique_ptr<SDL_Joystick, decltype(&SDL_JoystickClose)> replacement_joystick(
            SDL_JoystickOpen(replacement), SDL_JoystickClose);
        require(bool(replacement_joystick), "Replacement controller must open");
        SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
        host->poll();
        SDL_JoystickSetVirtualButton(replacement_joystick.get(), SDL_CONTROLLER_BUTTON_A, 1);
        require(host->poll().controls.fire, "Hot-plugged controller must accept a fresh flight press");
        host.reset();
        {
            SDL_JoystickSetVirtualButton(replacement_joystick.get(), SDL_CONTROLLER_BUTTON_A, 0);
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_LEFTX, 0);
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_LEFTY, 0);
            SdlHostOptions options{InputProfile::enhanced, {}};
            options.controller = {4000, 150, PitchDirection::inverted};
            SdlFlightHost tuned(options);
            tuned.poll();
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_LEFTX, 16000);
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_LEFTY, 16000);
            input = tuned.poll();
            const auto expected = controller_steering(16000, options.controller);
            require(input.controls.horizontal == expected && input.controls.vertical == -expected,
                    "Live SDL2 axes must apply custom sensitivity and pitch direction");
            keyboard(SDL_KEYDOWN, SDL_SCANCODE_RIGHT);
            require(tuned.poll().controls.horizontal == 350,
                    "Keyboard steering must retain precedence over a tuned controller axis");
            keyboard(SDL_KEYUP, SDL_SCANCODE_RIGHT);
            SDL_Event focus{};
            focus.type = SDL_WINDOWEVENT;
            focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
            SDL_PushEvent(&focus);
            require(tuned.poll().controls.horizontal == 0, "Focus loss must clear tuned axes");
            focus.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
            SDL_PushEvent(&focus);
            require(tuned.poll().controls.horizontal == 0,
                    "A deflected tuned stick must remain disarmed after returning focus");
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_LEFTX, 3000);
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_LEFTY, 3000);
            require(tuned.poll().controls.horizontal == 0, "The configured deadzone must rearm neutral axes");
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_LEFTX, 6000);
            require(tuned.poll().controls.horizontal == controller_steering(6000, options.controller),
                    "Tuned steering must resume after a neutral release");
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_LEFTX, 0);
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_LEFTY, 0);
        }
        {
            for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; ++button) {
                SDL_JoystickSetVirtualButton(replacement_joystick.get(), button, 0);
            }
            for (const auto axis : {SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY,
                    SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY}) {
                SDL_JoystickSetVirtualAxis(replacement_joystick.get(), axis, 0);
            }
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
            const auto options = parse_sdl_options({"--mode", "enhanced", "--bind", "menu.a=none",
                "--bind", "menu.b=accept", "--bind", "menu.x=accept", "--bind", "menu.guide=pause",
                "--bind", "flight.a=target", "--bind", "flight.x=fire", "--bind", "flight.b=settings",
                "--bind", "flight.lefttrigger=settings", "--bind", "flight.righttrigger=target",
                "--bind", "flight.leftstick=throttle-up", "--bind", "flight.rightstick=throttle-down",
                "--bind", "flight.guide=pause", "--bind", "flight.dpup=next-weapon",
                "--bind", "flight.dpdown=previous-weapon", "--steer-x", "rightx", "--steer-y", "righty"});
            SdlFlightHost remapped(options);
            remapped.set_input_context(InputContext::menu);
            remapped.poll();
            const auto press = [&](SDL_GameControllerButton button, bool down) {
                require(SDL_JoystickSetVirtualButton(replacement_joystick.get(), button, down ? 1 : 0) == 0,
                        "Remapped controller button must update");
                return remapped.poll();
            };
            require(press(SDL_CONTROLLER_BUTTON_A, true).scan_bytes.empty(), "An unbound menu button must emit no scan byte");
            press(SDL_CONTROLLER_BUTTON_A, false);
            require(press(SDL_CONTROLLER_BUTTON_B, true).scan_bytes == std::vector<std::uint8_t>{0x1c},
                    "A remapped menu accept must reach the canonical key route");
            require(press(SDL_CONTROLLER_BUTTON_X, true).scan_bytes.empty() && press(SDL_CONTROLLER_BUTTON_B, false).scan_bytes.empty(),
                    "Multiple controller owners must retain one logical menu key");
            require(press(SDL_CONTROLLER_BUTTON_X, false).scan_bytes == std::vector<std::uint8_t>{0x9c},
                    "The final remapped owner must release the menu key");
            press(SDL_CONTROLLER_BUTTON_X, true);
            remapped.set_input_context(InputContext::flight);
            require(!remapped.poll().controls.fire, "Remapped menu-held fire must remain suppressed at flight entry");
            press(SDL_CONTROLLER_BUTTON_X, false);
            input = press(SDL_CONTROLLER_BUTTON_X, true);
            require(input.controls.fire && input.pressed.fire, "Remapped fire must retain held and buffered behavior");
            press(SDL_CONTROLLER_BUTTON_X, false);
            input = press(SDL_CONTROLLER_BUTTON_A, true);
            require(input.pressed.target && !input.controls.fire, "Remapped target must replace the original fire action");
            press(SDL_CONTROLLER_BUTTON_A, false);
            require(press(SDL_CONTROLLER_BUTTON_LEFTSTICK, true).controls.throttle == 1, "Remapped throttle-up must update retained power");
            press(SDL_CONTROLLER_BUTTON_LEFTSTICK, false);
            require(press(SDL_CONTROLLER_BUTTON_RIGHTSTICK, true).controls.throttle == 0, "Remapped throttle-down must update retained power");
            press(SDL_CONTROLLER_BUTTON_RIGHTSTICK, false);
            require(press(SDL_CONTROLLER_BUTTON_DPAD_UP, true).controls.weapon == WeaponSelection::next, "Remapped next weapon must route once");
            press(SDL_CONTROLLER_BUTTON_DPAD_UP, false);
            require(press(SDL_CONTROLLER_BUTTON_DPAD_DOWN, true).controls.weapon == WeaponSelection::previous, "Remapped previous weapon must route once");
            press(SDL_CONTROLLER_BUTTON_DPAD_DOWN, false);
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767);
            require(remapped.poll().scan_bytes == std::vector<std::uint8_t>{0x01}, "A trigger-bound settings action must flush its key in the same poll");
            require(press(SDL_CONTROLLER_BUTTON_B, true).scan_bytes.empty(), "Button and trigger may share an owned settings key");
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
            require(remapped.poll().scan_bytes.empty(), "Releasing a shared trigger must retain the button owner");
            require(press(SDL_CONTROLLER_BUTTON_B, false).scan_bytes == std::vector<std::uint8_t>{0x81}, "The last settings owner must release Escape");
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767);
            input = remapped.poll();
            require(input.pressed.target && !input.controls.fire && !remapped.poll().pressed.target,
                    "A remapped trigger target must emit one buffered edge");
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
            remapped.poll();
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_RIGHTX, 32767);
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_RIGHTY, -32768);
            input = remapped.poll();
            require(input.controls.horizontal == 350 && input.controls.vertical == -350, "Selected right-stick axes must steer within the original range");
            press(SDL_CONTROLLER_BUTTON_X, true);
            SDL_Event focus{};
            focus.type = SDL_WINDOWEVENT;
            focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
            SDL_PushEvent(&focus);
            require(!remapped.poll().controls.fire, "Focus loss must release remapped held fire");
            focus.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
            SDL_PushEvent(&focus);
            input = remapped.poll();
            require(!input.controls.fire && input.controls.horizontal == 0, "Focus recovery must suppress remapped holds and deflected axes");
            press(SDL_CONTROLLER_BUTTON_X, false);
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_RIGHTX, 0);
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_RIGHTY, 0);
            remapped.poll();
            require(press(SDL_CONTROLLER_BUTTON_GUIDE, true).pause == PauseAction::toggle, "Remapped pause must reach the host action");
            press(SDL_CONTROLLER_BUTTON_GUIDE, false);
            remapped.set_input_context(InputContext::name_entry);
            require(press(SDL_CONTROLLER_BUTTON_GUIDE, true).pause == PauseAction::none, "Name entry must retain pause suppression after remapping");
            press(SDL_CONTROLLER_BUTTON_GUIDE, false);
        }
        {
            SdlFlightHost faithful;
            faithful.poll();
            SDL_JoystickSetVirtualButton(replacement_joystick.get(), SDL_CONTROLLER_BUTTON_A, 0);
            faithful.poll();
            SDL_JoystickSetVirtualButton(replacement_joystick.get(), SDL_CONTROLLER_BUTTON_A, 1);
            input = faithful.poll();
            require(!input.controls.fire && !input.pressed.fire && input.scan_bytes.empty(),
                    "Faithful input must leave enhanced controller routing disabled");
        }
        {
            for (unsigned button = 0; button < SDL_CONTROLLER_BUTTON_MAX; ++button) {
                SDL_JoystickSetVirtualButton(replacement_joystick.get(), button, 0);
            }
            for (unsigned axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; ++axis) {
                SDL_JoystickSetVirtualAxis(replacement_joystick.get(), axis,
                    axis >= SDL_CONTROLLER_AXIS_TRIGGERLEFT ? -32768 : 0);
            }
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_LEFTX, 1700);
            auto options = parse_sdl_options({"--mode", "enhanced", "--renderer", "cpu", "--fps", "unlimited",
                "--axis-calibration", "leftx=-28000,1700,30000"});
            SdlHostOptions saved;
            unsigned saves = 0;
            SdlFlightHost editor(options, [&](const SdlHostOptions& configuration) { saved = configuration; ++saves; });
            editor.poll();
            require(editor.poll().controls.horizontal == 0, "A calibrated center must rearm without drift");
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_LEFTX, 30000);
            require(editor.poll().controls.horizontal == 350, "Calibrated physical travel must reach full steering");
            SDL_JoystickSetVirtualAxis(replacement_joystick.get(), SDL_CONTROLLER_AXIS_LEFTX, 1700);
            editor.poll();
            editor.set_playback(Playback::running);
            SDL_Event open{};
            open.type = SDL_KEYDOWN;
            open.key.keysym.scancode = SDL_SCANCODE_F3;
            open.key.keysym.mod = KMOD_CTRL;
            SDL_PushEvent(&open);
            open.type = SDL_KEYUP;
            open.key.keysym.mod = 0;
            SDL_PushEvent(&open);
            require(editor.poll().pause == PauseAction::pause, "The live controller editor must request native pause");
            editor.set_playback(Playback::paused);
            const auto press = [&](SDL_GameControllerButton button, bool down) {
                SDL_JoystickSetVirtualButton(replacement_joystick.get(), button, down ? 1 : 0);
                return editor.poll();
            };
            const auto tap = [&](SDL_GameControllerButton button) {
                auto input = press(button, true);
                const auto released = press(button, false);
                require(input.scan_bytes.empty() && released.scan_bytes.empty() && !input.controls.fire && !input.pressed.fire,
                    "Gamepad editor navigation must not reach DOS or fire");
                return input;
            };
            tap(SDL_CONTROLLER_BUTTON_A);
            tap(SDL_CONTROLLER_BUTTON_A);
            tap(SDL_CONTROLLER_BUTTON_B);
            tap(SDL_CONTROLLER_BUTTON_B);
            tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
            tap(SDL_CONTROLLER_BUTTON_A);
            tap(SDL_CONTROLLER_BUTTON_A);
            SDL_Event extra_button{};
            extra_button.type = SDL_CONTROLLERBUTTONDOWN;
            extra_button.cbutton.which = SDL_JoystickInstanceID(replacement_joystick.get());
            extra_button.cbutton.button = static_cast<Uint8>(controller_button_count);
            SDL_PushEvent(&extra_button);
            editor.poll();
            extra_button.type = SDL_CONTROLLERBUTTONUP;
            SDL_PushEvent(&extra_button);
            editor.poll();
            tap(SDL_CONTROLLER_BUTTON_Y);
            tap(SDL_CONTROLLER_BUTTON_B);
            for (unsigned row = 0; row < 9; ++row) { tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN); }
            tap(SDL_CONTROLLER_BUTTON_A);
            require(saves > 0 && saved.bindings.menu[static_cast<std::size_t>(ControllerButton::b)] == HostAction::accept &&
                    saved.calibration.axes[0].center == 1700 &&
                    saved.bindings.flight[static_cast<std::size_t>(ControllerButton::y)] == HostAction::fire &&
                    saved.bindings.triggers[0] == HostAction::none,
                    "Gamepad capture and Apply must persist a complete calibrated configuration");
            require(tap(SDL_CONTROLLER_BUTTON_B).pause == PauseAction::resume,
                    "Fixed editor Back must remain usable after rebinding the native Back button");
            editor.set_input_context(InputContext::menu);
            editor.poll();
            require(press(SDL_CONTROLLER_BUTTON_B, true).scan_bytes == std::vector<std::uint8_t>{0x1c},
                    "The applied button binding must work through normal native menu input");
            require(press(SDL_CONTROLLER_BUTTON_B, false).scan_bytes == std::vector<std::uint8_t>{0x9c},
                    "The applied binding must retain its release ownership");
        }
        require(SDL_JoystickDetachVirtual(replacement) == 0, "Replacement virtual controller must detach");
        std::cout << "Virtual SDL2 controller routing and lifecycle passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
#endif
}
