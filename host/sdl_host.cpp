#include "sdl_host.hpp"
#include "sdl_audio.hpp"
#include "sdl_common.hpp"
#include "sdl_controller.hpp"
#include "spatial_renderer.hpp"
#include <chrono>

#include <algorithm>
#include <sstream>

namespace comanchecd::host {
namespace {
constexpr std::int32_t keyboard_control = 350;
constexpr std::uint8_t maximum_throttle = 15;
constexpr double mouse_look_radians_per_pixel = 0.003;

std::uint8_t menu_scan(SDL_Scancode key) {
    constexpr std::array<std::uint8_t, 26> letters{
        0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
        0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};
    if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z) {
        return letters[key - SDL_SCANCODE_A];
    }
    if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_0) {
        return static_cast<std::uint8_t>(2 + key - SDL_SCANCODE_1);
    }
    switch (key) {
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_BACKSPACE: return 0x0e;
    case SDL_SCANCODE_TAB: return 0x0f;
    case SDL_SCANCODE_RETURN: return 0x1c;
    case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_UP: return 0x48;
    case SDL_SCANCODE_LEFT: return 0x4b;
    case SDL_SCANCODE_RIGHT: return 0x4d;
    case SDL_SCANCODE_DOWN: return 0x50;
    case SDL_SCANCODE_DELETE: return 0x53;
    case SDL_SCANCODE_MINUS: return 0x0c;
    case SDL_SCANCODE_EQUALS: return 0x0d;
    case SDL_SCANCODE_LEFTBRACKET: return 0x1a;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1b;
    case SDL_SCANCODE_SEMICOLON: return 0x27;
    case SDL_SCANCODE_APOSTROPHE: return 0x28;
    case SDL_SCANCODE_GRAVE: return 0x29;
    case SDL_SCANCODE_BACKSLASH: return 0x2b;
    case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34;
    case SDL_SCANCODE_SLASH: return 0x35;
    default: return 0;
    }
}

void key_bytes(std::vector<std::uint8_t>& bytes, SDL_Scancode key, bool down) {
    auto scan = menu_scan(key);
    if (scan == 0) {
        switch (key) {
        case SDL_SCANCODE_F1: case SDL_SCANCODE_F2: case SDL_SCANCODE_F3: case SDL_SCANCODE_F4:
        case SDL_SCANCODE_F5: case SDL_SCANCODE_F6: case SDL_SCANCODE_F7: case SDL_SCANCODE_F8:
        case SDL_SCANCODE_F9: case SDL_SCANCODE_F10: scan = 0x3b + key - SDL_SCANCODE_F1; break;
        case SDL_SCANCODE_F11: scan = 0x57; break;
        case SDL_SCANCODE_F12: scan = 0x58; break;
        case SDL_SCANCODE_LSHIFT: scan = 0x2a; break;
        case SDL_SCANCODE_RSHIFT: scan = 0x36; break;
        case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: scan = 0x1d; break;
        case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: scan = 0x38; break;
        default: break;
        }
    }
    if (scan == 0) { return; }
    if (key == SDL_SCANCODE_UP || key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_RIGHT ||
            key == SDL_SCANCODE_DOWN || key == SDL_SCANCODE_DELETE || key == SDL_SCANCODE_RCTRL ||
            key == SDL_SCANCODE_RALT) {
        bytes.push_back(0xe0);
    }
    bytes.push_back(static_cast<std::uint8_t>(scan | (down ? 0 : 0x80)));
}

template<class T, void (*Destroy)(T*)>
using SdlPointer = std::unique_ptr<T, decltype(Destroy)>;
}

class SdlFlightHost::Impl {
public:
    explicit Impl(SdlHostOptions options) : scene_rendering(options.scene_rendering),
        interpolation(options.interpolation), cockpit_scaling(options.cockpit_scaling), cockpit(options.cockpit), profile(options.input) {
        validate_controller_tuning(options.controller);
        validate_controller_bindings(options.bindings);
        const bool enhanced = profile == InputProfile::enhanced;
        if (!enhanced && ((options.scaling != ImageScaling::profile_default && options.scaling != ImageScaling::nearest) ||
                options.pacing != DisplayPacing::vertical_sync ||
                options.controller.deadzone != ControllerTuning{}.deadzone ||
                options.controller.sensitivity != ControllerTuning{}.sensitivity ||
                options.controller.pitch != PitchDirection::normal)) {
            throw std::invalid_argument("Enhanced controls and presentation options require enhanced mode");
        }
        window.reset(SDL_CreateWindow("Comanche CD training preview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                      960, 720, SDL_WINDOW_RESIZABLE | (enhanced ? SDL_WINDOW_ALLOW_HIGHDPI : 0)));
        if (!window) {
            throw sdl_error("SDL2 window creation failed");
        }
        const auto scaling = options.scaling == ImageScaling::profile_default ?
            (enhanced ? ImageScaling::integer : ImageScaling::nearest) : options.scaling;
        presenter = std::make_unique<SdlPresenter>(window.get(), scaling, options.pacing);
        if (enhanced) {
            controller = std::make_unique<SdlController>(options.controller_mappings, options.controller, options.bindings);
            if (cockpit == CockpitPresentation::spatial && scene_rendering == SceneRendering::spatial) {
                spatial_cockpit = std::make_unique<SpatialCockpit>();
            }
        }
    }
    ~Impl() { stop_look(); }

private:
    bool look_event(const SDL_Event& event) {
        if (profile != InputProfile::enhanced || external_focus != HostFocus::active || !look_available ||
                (context != InputContext::flight && context != InputContext::training)) { return false; }
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_MIDDLE) {
            look.reset();
            return true;
        }
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
            looking = true;
            relative_mouse = SDL_SetRelativeMouseMode(SDL_TRUE) == 0;
            return true;
        }
        if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_RIGHT) {
            stop_look();
            return true;
        }
        if (event.type == SDL_MOUSEMOTION && looking) {
            look.move(-event.motion.xrel * mouse_look_radians_per_pixel, -event.motion.yrel * mouse_look_radians_per_pixel);
            return true;
        }
        return false;
    }

    bool presentation_key(const SDL_KeyboardEvent& event) {
        if (profile != InputProfile::enhanced || event.keysym.scancode != SDL_SCANCODE_RETURN) { return false; }
        if (fullscreen_key_held) {
            if (event.type == SDL_KEYUP) { fullscreen_key_held = false; }
            return true;
        }
        if (event.type != SDL_KEYDOWN || (event.keysym.mod & KMOD_ALT) == 0 || held[SDL_SCANCODE_RETURN]) {
            return false;
        }
        fullscreen_key_held = true;
        if (event.repeat == 0) {
            const auto mode = (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) != 0 ? 0u : SDL_WINDOW_FULLSCREEN_DESKTOP;
            if (SDL_SetWindowFullscreen(window.get(), mode) != 0) { throw sdl_error("SDL2 fullscreen change failed"); }
        }
        return true;
    }

public:
    void stop_look() {
        if (relative_mouse) { SDL_SetRelativeMouseMode(SDL_FALSE); }
        looking = relative_mouse = false;
    }
    void release_input(HostInput& input) {
        stop_look();
        for (std::size_t key = 0; key < held.size(); ++key) {
            if (held[key] && !controller_held[key]) { key_bytes(input.scan_bytes, static_cast<SDL_Scancode>(key), false); }
        }
        held.fill(false);
        suppressed.fill(false);
        fullscreen_key_held = false;
        if (controller) { controller->set_focus(false); flush_controller_keys(input); }
        input.pause = PauseAction::pause;
    }

    void flush_controller_keys(HostInput& input) {
        if (!controller) { return; }
        for (const auto& change : controller->take_keys()) {
            controller_held[change.key] = change.down;
            if (change.down || !held[change.key]) { key_bytes(input.scan_bytes, change.key, change.down); }
            if (change.down) {
                input.keys.push_back({menu_scan(change.key)});
                input.escape = input.escape || change.key == SDL_SCANCODE_ESCAPE;
            }
        }
    }

    HostInput poll() {
        HostInput input;
        flush_controller_keys(input);
        if (focus_release_pending) {
            release_input(input);
            focus_release_pending = false;
            if (controller && external_focus == HostFocus::active) { controller->set_focus(true); }
        }
        const bool flight_input = context == InputContext::flight || context == InputContext::training;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (look_event(event)) { continue; }
            if (controller) {
                controller->event(event, input, throttle);
                flush_controller_keys(input);
            }
            if (event.type == SDL_QUIT) {
                input.quit = true;
            } else if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    input.quit = true;
                } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST || event.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    release_input(input);
                } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                    if (controller) { controller->set_focus(external_focus == HostFocus::active); }
                }
            } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                const auto key = event.key.keysym.scancode;
                if (external_focus == HostFocus::inactive && event.type == SDL_KEYDOWN) { continue; }
                if (key < SDL_SCANCODE_UNKNOWN || key >= SDL_NUM_SCANCODES) {
                    continue;
                }
                if (presentation_key(event.key)) { continue; }
                const bool was_held = held[key];
                const bool enhanced = profile == InputProfile::enhanced;
                const bool modified = (event.key.keysym.mod & (KMOD_ALT | KMOD_CTRL)) != 0;
                if (enhanced && event.type == SDL_KEYDOWN &&
                        ((!was_held && event.key.repeat != 0) ||
                         (flight_input && modified &&
                          (key == SDL_SCANCODE_SPACE || key == SDL_SCANCODE_TAB || key == SDL_SCANCODE_RETURN)))) {
                    suppressed[key] = true;
                }
                held[key] = event.type == SDL_KEYDOWN;
                if (event.type == SDL_KEYDOWN && enhanced && suppressed[key]) { continue; }
                if (event.type == SDL_KEYDOWN || !controller_held[key]) {
                    key_bytes(input.scan_bytes, key, event.type == SDL_KEYDOWN);
                }
                if (event.type == SDL_KEYUP) {
                    suppressed[key] = false;
                    continue;
                }
                input.key_pressed = true;
                const auto scan = menu_scan(key);
                if (flight_input && key == SDL_SCANCODE_A && (event.key.keysym.mod & KMOD_ALT) != 0 && event.key.repeat == 0) {
                    input.abort_mission = true;
                }
                if (scan != 0) {
                    input.keys.push_back({scan, (event.key.keysym.mod & KMOD_SHIFT) != 0 ? PilotShift::both : PilotShift::none});
                }
                if (key == SDL_SCANCODE_ESCAPE) {
                    input.escape = true;
                }
                if (key == SDL_SCANCODE_P && !modified && context != InputContext::name_entry &&
                        event.key.repeat == 0 && input.pause != PauseAction::pause) {
                    input.pause = PauseAction::toggle;
                }
                if (key == SDL_SCANCODE_R && context == InputContext::training && !modified && event.key.repeat == 0) {
                    input.reset = true;
                }
                if (!flight_input || modified) {
                    continue;
                }
                if (enhanced && !was_held && event.key.repeat == 0) {
                    input.pressed.fire = input.pressed.fire || key == SDL_SCANCODE_SPACE;
                    input.pressed.target = input.pressed.target || key == SDL_SCANCODE_TAB || key == SDL_SCANCODE_RETURN;
                }
                if (key == SDL_SCANCODE_E) {
                    throttle = std::min<unsigned>(throttle + 1, maximum_throttle);
                } else if (key == SDL_SCANCODE_D && throttle > 0) {
                    --throttle;
                } else if (key == SDL_SCANCODE_LEFTBRACKET && context == InputContext::training) {
                    input.controls.weapon = WeaponSelection::previous;
                } else if (key == SDL_SCANCODE_RIGHTBRACKET && context == InputContext::training) {
                    input.controls.weapon = WeaponSelection::next;
                } else if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F6) {
                    displays.left = static_cast<InstrumentDisplay>(key - SDL_SCANCODE_F1);
                } else if (key >= SDL_SCANCODE_F7 && key <= SDL_SCANCODE_F12) {
                    displays.right = static_cast<InstrumentDisplay>(key - SDL_SCANCODE_F7);
                }
            }
        }
        if (controller && input.pause != PauseAction::none) {
            suppressed = held;
            controller->reset();
            flush_controller_keys(input);
            input.pressed = {};
        }
        if (flight_input) {
            const auto active = [&](SDL_Scancode key) {
                return held[key] && (profile != InputProfile::enhanced || !suppressed[key]);
            };
            input.controls.horizontal = keyboard_control * (int(active(SDL_SCANCODE_RIGHT)) - int(active(SDL_SCANCODE_LEFT)));
            input.controls.vertical = keyboard_control * (int(active(SDL_SCANCODE_DOWN)) - int(active(SDL_SCANCODE_UP)));
            input.controls.throttle = throttle;
            input.controls.fire = active(SDL_SCANCODE_SPACE);
            input.controls.cycle_target = active(SDL_SCANCODE_TAB) || active(SDL_SCANCODE_RETURN);
            input.controls.chaff = context == InputContext::training && held[SDL_SCANCODE_SEMICOLON];
            input.controls.flare = context == InputContext::training && held[SDL_SCANCODE_APOSTROPHE];
            input.cockpit = displays;
        }
        if (controller) { controller->sample(input, throttle); flush_controller_keys(input); }
        return input;
    }

    double presentation_time() const {
        const auto now = playback == Playback::running ? std::chrono::steady_clock::now() : paused_at;
        return std::chrono::duration<double>(now - epoch - paused_duration).count();
    }

    using Clock = std::chrono::steady_clock;
    Clock::time_point epoch = Clock::now(), paused_at = epoch;
    Clock::duration paused_duration{};
    Playback playback = Playback::paused;
    SceneRendering scene_rendering;
    CameraInterpolation interpolation;
    CockpitScaling cockpit_scaling;
    CockpitPresentation cockpit;
    CockpitLook look;
    std::unique_ptr<SpatialCockpit> spatial_cockpit;
    SpatialImage cockpit_image;
    bool look_available = false, looking = false, relative_mouse = false;
    CameraHistory camera_history;
    SpatialRenderer spatial_renderer;
    SdlSubsystem subsystem{SDL_INIT_VIDEO | SDL_INIT_EVENTS};
    SdlPointer<SDL_Window, SDL_DestroyWindow> window{nullptr, SDL_DestroyWindow};
    std::unique_ptr<SdlPresenter> presenter;
    SdlAudioOutput audio;
    std::unique_ptr<SdlController> controller;
    std::array<bool, SDL_NUM_SCANCODES> held{};
    std::array<bool, SDL_NUM_SCANCODES> controller_held{};
    std::array<bool, SDL_NUM_SCANCODES> suppressed{};
    InputProfile profile;
    bool fullscreen_key_held = false;
    HostFocus external_focus = HostFocus::active;
    bool focus_release_pending = false;
    InputContext context = InputContext::flight;
    std::uint8_t throttle = 0;
    CockpitControls displays;
};

SdlFlightHost::SdlFlightHost(SdlHostOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
SdlFlightHost::~SdlFlightHost() = default;
void SdlFlightHost::set_focus(HostFocus focus) {
    if (focus == impl_->external_focus) { return; }
    impl_->external_focus = focus;
    if (focus == HostFocus::inactive) { impl_->focus_release_pending = true; }
    if (impl_->controller && focus == HostFocus::active) { impl_->controller->set_focus(true); }
}
HostInput SdlFlightHost::poll() { return impl_->poll(); }
void SdlFlightHost::set_input_context(InputContext context) {
    if (context != impl_->context) { impl_->stop_look(); }
    if (context != impl_->context && impl_->profile == InputProfile::enhanced) { impl_->suppressed = impl_->held; }
    impl_->context = context;
    if (impl_->controller) { impl_->controller->set_context(context); }
}

void SdlFlightHost::present(const CockpitImage& image, const FlightState& state, const std::string& status) {
    impl_->look_available = false;
    impl_->stop_look();
    impl_->camera_history.reset();
    impl_->presenter->present(image);
    update_title(state, status);
}

PresentationDetail SdlFlightHost::presentation_detail() const {
    return impl_->profile == InputProfile::enhanced && impl_->scene_rendering == SceneRendering::spatial ?
        PresentationDetail::spatial : PresentationDetail::indexed;
}

void SdlFlightHost::present_snapshot(std::shared_ptr<const SessionPresentation> frame, const std::string& status) {
    if (presentation_detail() != PresentationDetail::spatial || !spatial_presentation(*frame, impl_->cockpit)) {
        present(frame->image, frame->flight, status);
        return;
    }
    const auto time = impl_->presentation_time();
    impl_->camera_history.observe(frame->spatial, frame->cpu_cycles, time);
    const auto camera = impl_->interpolation == CameraInterpolation::enabled ?
        impl_->camera_history.sample(time) : spatial_camera(frame->spatial->camera);
    const auto size = impl_->presenter->spatial_size();
    const auto poses = impl_->interpolation == CameraInterpolation::enabled ?
        impl_->camera_history.sample_entities(time) : entity_poses(*frame->spatial);
    const auto view = camera_view(camera, size.first, size.second);
    impl_->look_available = impl_->cockpit == CockpitPresentation::spatial && frame->spatial->cockpit_mode == 1;
    if (impl_->look_available) {
        const auto pose = impl_->look.pose();
        const auto looked = eye_view(view, pose, view.frustum, 1);
        impl_->cockpit_image = impl_->spatial_renderer.render_world(*frame->spatial, looked, poses,
            frame->image.palette, size.first, size.second);
        impl_->spatial_cockpit->draw(impl_->cockpit_image, cockpit_view(pose, view.frustum), frame->image, *frame->spatial);
        impl_->presenter->present_spatial(impl_->cockpit_image.pixels, size.first, size.second);
        update_title(frame->flight, status);
        return;
    }
    impl_->stop_look();
    const auto& image = impl_->spatial_renderer.render(*frame->spatial, view, poses, frame->image,
        size.first, size.second, impl_->cockpit_scaling);
    impl_->presenter->present_spatial(image.pixels, image.width, image.height);
    update_title(frame->flight, status);
}

void SdlFlightHost::update_title(const FlightState& state, const std::string& status) {
    std::ostringstream title;
    title << (impl_->profile == InputProfile::enhanced ? "OpenCommanche enhanced | " : "Comanche CD | ")
          << status << " | throttle " << unsigned(impl_->throttle)
          << "/15 | height " << unsigned(state.view.height) << " | armor " << state.armor
          << " | weapon " << unsigned(state.weapon + 1) << " | ammo " << state.ammunition[state.weapon];
    const auto text = title.str();
    if (text != SDL_GetWindowTitle(impl_->window.get())) {
        SDL_SetWindowTitle(impl_->window.get(), text.c_str());
    }
}
void SdlFlightHost::queue_audio(const std::vector<std::int16_t>& samples) { impl_->audio.queue(samples); }
std::size_t SdlFlightHost::queued_audio_samples() const { return impl_->audio.queued_samples(); }
void SdlFlightHost::clear_audio() { impl_->audio.clear(); }
void SdlFlightHost::set_playback(Playback playback) {
    if (playback != impl_->playback) {
        const auto now = Impl::Clock::now();
        if (playback == Playback::paused) { impl_->paused_at = now; }
        else { impl_->paused_duration += now - impl_->paused_at; }
        impl_->playback = playback;
    }
    impl_->audio.set_playback(playback);
}
void SdlFlightHost::idle() { SDL_Delay(1); }
void SdlFlightHost::reset_controls() {
    impl_->stop_look();
    impl_->look.reset();
    if (impl_->profile == InputProfile::enhanced) { impl_->suppressed = impl_->held; }
    if (impl_->controller) { impl_->controller->reset(); }
    impl_->held.fill(false);
    impl_->throttle = 0;
    impl_->displays = {};
}
} // namespace comanchecd::host
