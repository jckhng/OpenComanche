#include "sdl_host.hpp"
#include "sdl_common.hpp"
#include "pending_input.hpp"
#include "sdl_options.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}

void key(Uint32 type, SDL_Scancode code, Uint8 repeat = 0, Uint16 modifiers = 0) {
    SDL_Event event{};
    event.type = type;
    event.key.keysym.scancode = code;
    event.key.keysym.mod = modifiers;
    event.key.repeat = repeat;
    require(SDL_PushEvent(&event) == 1, "Enhanced keyboard event must enter SDL2");
}

void tap(SDL_Scancode code, Uint16 modifiers = 0) {
    key(SDL_KEYDOWN, code, 0, modifiers);
    key(SDL_KEYUP, code, 0, modifiers);
}

void configuration() {
    using namespace comanchecd::host;
    require(parse_sdl_options({}).input == InputProfile::faithful, "The default CLI profile must remain faithful");
    const auto options = parse_sdl_options({"--scale", "linear", "--stick-deadzone", "4000",
        "--stick-sensitivity", "150", "--invert-y", "on", "--mode", "enhanced"});
    require(options.input == InputProfile::enhanced && options.scaling == ImageScaling::linear &&
            options.controller.deadzone == 4000 && options.controller.sensitivity == 150 &&
            options.controller.pitch == PitchDirection::inverted,
            "Enhanced options must parse independently of option order");
    for (const auto& arguments : std::vector<std::vector<std::string>>{
            {"--scale", "linear"}, {"--scene-renderer", "spatial"}, {"--interpolation", "on"},
            {"--mode", "enhanced", "--scene-renderer", "mesh"}, {"--mode", "enhanced", "--interpolation", "yes"}, {"--mode", "enhanced", "--stick-deadzone", "32767"},
            {"--mode", "enhanced", "--stick-deadzone", "-1"},
            {"--mode", "enhanced", "--stick-sensitivity", "0"},
            {"--mode", "enhanced", "--stick-sensitivity", "401"},
            {"--mode", "enhanced", "--stick-sensitivity", "100x"},
            {"--mode", "enhanced", "--invert-y", "maybe"},
            {"--mode", "enhanced", "--scale", "stretch"}, {"--mode"}}) {
        bool rejected = false;
        try { parse_sdl_options(arguments); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Invalid or inactive enhanced options must be rejected");
    }
    const auto spatial = parse_sdl_options({"--mode", "enhanced", "--scene-renderer", "spatial", "--interpolation", "on"});
    const auto native = parse_sdl_options({"--mode", "enhanced", "--scene-renderer", "native", "--interpolation", "off"});
    require(spatial.scene_rendering == SceneRendering::spatial && spatial.interpolation == CameraInterpolation::enabled &&
            native.scene_rendering == SceneRendering::native && native.interpolation == CameraInterpolation::disabled,
            "Enhanced scene and interpolation selections must reach the host boundary");
    ControllerTuning original;
    for (int value = -32768; value <= 32767; ++value) {
        const int magnitude = std::abs(value);
        const int extent = value < 0 ? 32768 : 32767;
        const int expected = magnitude <= original.deadzone ? 0 :
            (magnitude - original.deadzone) * 350 / (extent - original.deadzone) * (value < 0 ? -1 : 1);
        require(controller_steering(static_cast<std::int16_t>(value), original) == expected,
                "Default controller tuning must preserve every original SDL2 axis value");
    }
    for (const auto tuning : {ControllerTuning{0, 50}, ControllerTuning{4000, 150}, ControllerTuning{32766, 400}}) {
        int previous = -350;
        for (int value = -32768; value <= 32767; ++value) {
            const auto actual = controller_steering(static_cast<std::int16_t>(value), tuning);
            require(actual >= previous && actual >= -350 && actual <= 350,
                    "Tuned axes must remain monotonic and bounded at both signed endpoints");
            previous = actual;
        }
        require(controller_steering(0, tuning) == 0, "Neutral must remain neutral for all tuning");
    }
}
}

int main() {
    try {
        using namespace comanchecd::host;
        configuration();
        SdlFlightHost host(SdlHostOptions{InputProfile::enhanced, {}});
        host.poll();
        tap(SDL_SCANCODE_SPACE);
        auto input = host.poll();
        require(input.pressed.fire && !input.controls.fire,
                "Enhanced keyboard taps must survive release before the next simulation tick");
        PendingFlightInput pending;
        pending.queue(input);
        require(pending.apply(host.poll().controls).fire && !pending.apply(host.poll().controls).fire,
                "A buffered enhanced keyboard tap must be consumed once");
        tap(SDL_SCANCODE_TAB);
        require(host.poll().pressed.target, "Enhanced Tab taps must buffer targeting");
        tap(SDL_SCANCODE_RETURN);
        require(host.poll().pressed.target, "Enhanced Enter taps must buffer targeting");
        key(SDL_KEYDOWN, SDL_SCANCODE_SPACE, 0, KMOD_CTRL);
        input = host.poll();
        require(!input.pressed.fire && !input.controls.fire,
                "Modified enhanced action keys must preserve modifier precedence");
        key(SDL_KEYUP, SDL_SCANCODE_SPACE);
        host.poll();
        host.set_input_context(InputContext::menu);
        key(SDL_KEYDOWN, SDL_SCANCODE_SPACE);
        require(!host.poll().pressed.fire, "Menu keys must retain menu semantics");
        host.set_input_context(InputContext::flight);
        require(!host.poll().controls.fire, "A menu-held action must require release before flight");
        key(SDL_KEYUP, SDL_SCANCODE_SPACE);
        host.poll();
        key(SDL_KEYDOWN, SDL_SCANCODE_SPACE);
        input = host.poll();
        require(input.controls.fire && input.pressed.fire, "A fresh enhanced press must rearm after a context change");
        key(SDL_KEYDOWN, SDL_SCANCODE_SPACE, 1);
        require(!host.poll().pressed.fire, "Key repeat must preserve held fire without adding a new buffered edge");
        tap(SDL_SCANCODE_P);
        input = host.poll();
        require(input.pause == PauseAction::toggle && !input.controls.fire && !input.pressed.fire,
                "Pause must suppress held enhanced actions until release");
        key(SDL_KEYUP, SDL_SCANCODE_SPACE);
        host.poll();
        SDL_Event focus{};
        focus.type = SDL_WINDOWEVENT;
        focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
        key(SDL_KEYDOWN, SDL_SCANCODE_SPACE);
        SDL_PushEvent(&focus);
        input = host.poll();
        require(input.pause == PauseAction::pause && !input.pressed.fire && !input.controls.fire,
                "Focus loss must cancel a buffered enhanced press in the same poll");
        key(SDL_KEYDOWN, SDL_SCANCODE_SPACE, 1);
        require(!host.poll().controls.fire, "Returning focus must ignore repeat-only input until release");
        key(SDL_KEYUP, SDL_SCANCODE_SPACE);
        host.poll();
        tap(SDL_SCANCODE_SPACE);
        require(host.poll().pressed.fire, "A released enhanced action must accept a fresh press");
        host.set_input_context(InputContext::name_entry);
        tap(SDL_SCANCODE_P);
        input = host.poll();
        require(input.pause == PauseAction::none && !input.pressed.fire && !input.pressed.target &&
                input.scan_bytes == std::vector<std::uint8_t>{0x19, 0x99},
                "Enhanced name entry must preserve raw text and its context-specific pause binding");
        host.set_input_context(InputContext::flight);
        auto* window = SDL_GetWindowFromID(1);
        require(window != nullptr, "Enhanced fixture must own its SDL2 window");
        tap(SDL_SCANCODE_RETURN, KMOD_ALT);
        input = host.poll();
        require((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0 && input.scan_bytes.empty() &&
                !input.pressed.target && !input.controls.cycle_target,
                "Enhanced fullscreen must consume Enter without targeting or accepting a menu");
        tap(SDL_SCANCODE_RETURN, KMOD_ALT);
        host.poll();
        require((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0,
                "The enhanced fullscreen shortcut must restore windowed presentation");
        host.poll();
        key(SDL_KEYDOWN, SDL_SCANCODE_RETURN);
        host.poll();
        key(SDL_KEYUP, SDL_SCANCODE_RETURN, 0, KMOD_ALT);
        input = host.poll();
        require(input.scan_bytes == std::vector<std::uint8_t>{0x9c} && !input.controls.cycle_target,
                "Adding Alt after an ordinary Enter must preserve its canonical release");

        key(SDL_KEYDOWN, SDL_SCANCODE_LEFT);
        host.poll();
        host.set_focus(HostFocus::inactive);
        input = host.poll();
        require(input.pause == PauseAction::pause && input.controls.horizontal == 0 &&
                input.scan_bytes == std::vector<std::uint8_t>{0xe0, 0xcb},
                "XR focus loss must release native keys before pausing");
        key(SDL_KEYDOWN, SDL_SCANCODE_SPACE);
        require(!host.poll().pressed.fire, "Unfocused headset input must suppress new keyboard presses");
        host.set_focus(HostFocus::active);
        input = host.poll();
        require(!input.controls.fire && input.controls.horizontal == 0, "Focus recovery must retain released controls");

        auto resources = std::make_shared<comanchecd::SpatialResources>();
        resources->assets.terrain.height.assign(comanchecd::terrain_side * comanchecd::terrain_side, 0);
        resources->assets.terrain.color.assign(comanchecd::terrain_side * comanchecd::terrain_side, 1);
        auto scene = std::make_shared<comanchecd::SpatialFrame>();
        scene->resources = resources;
        scene->camera.height = 40;
        scene->scene_mask.assign(comanchecd::cockpit_width * comanchecd::cockpit_height, 1);
        auto snapshot = std::make_shared<comanchecd::SessionPresentation>();
        snapshot->image.palette[1] = {0, 63, 0};
        snapshot->image.palette[255] = {0, 0, 63};
        snapshot->spatial = scene;
        snapshot->flying = true;
        snapshot->cpu_cycles = 100;
        host.present_snapshot(snapshot, "View fixture");
        auto next_scene = std::make_shared<comanchecd::SpatialFrame>(*scene);
        next_scene->serial = 1;
        next_scene->camera.x = 4194304;
        auto next_snapshot = std::make_shared<comanchecd::SessionPresentation>(*snapshot);
        next_snapshot->spatial = next_scene;
        next_snapshot->cpu_cycles = 1000100;
        host.present_snapshot(next_snapshot, "Interpolated view fixture");
        host.set_playback(Playback::paused);
        host.present_snapshot(next_snapshot, "Paused view fixture");
        require(scene->camera.x == 0 && next_scene->camera.x == 4194304 && snapshot->cpu_cycles == 100,
                "The live SDL2 presentation path must consume immutable views without advancing their clock");
        std::cout << "Enhanced keyboard buffering and input boundaries passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
