#include "sdl_host.hpp"
#include "sdl_audio.hpp"
#include "sdl_common.hpp"
#include "sdl_controller.hpp"
#include "sdl_keyboard.hpp"
#include "sdl_display.hpp"
#include "spatial_renderer.hpp"
#ifdef COMANCHECD_OPENGL_DESKTOP
#include "gl_desktop_presenter.hpp"
#endif
#include <chrono>

#include <algorithm>
#include <sstream>

namespace comanchecd::host {
namespace {
constexpr double mouse_look_radians_per_pixel = 0.003;
constexpr auto preferences_debounce = std::chrono::milliseconds(250);
constexpr auto preferences_retry = std::chrono::seconds(2);
constexpr std::array<unsigned, 9> frame_rate_presets{30, 60, 75, 90, 120, 144, 165, 240, 360};

template<class T, void (*Destroy)(T*)>
using SdlPointer = std::unique_ptr<T, decltype(Destroy)>;
}

class SdlFlightHost::Impl {
public:
    Impl(SdlHostOptions options, PreferencesWriter save) : scene_rendering(options.scene_rendering),
        interpolation(options.interpolation), cockpit_scaling(options.cockpit_scaling), cockpit(options.cockpit),
        profile(options.input), settings(options), save_preferences(std::move(save)) {
        validate_frame_limit(options.frame_limit);
        validate_window_placement(options.window);
        if (options.pacing != DisplayPacing::vertical_sync && options.pacing != DisplayPacing::immediate) {
            throw std::invalid_argument("Invalid display pacing");
        }
        validate_controller_tuning(options.controller);
        validate_controller_bindings(options.bindings);
        const bool enhanced = profile == InputProfile::enhanced;
        if (!enhanced && ((options.scaling != ImageScaling::profile_default && options.scaling != ImageScaling::nearest) ||
                options.pacing != DisplayPacing::vertical_sync || options.renderer != DesktopRenderer::automatic ||
                options.frame_limit.mode != FrameLimitMode::profile_default || options.window.width != 0 ||
                options.window.position || options.window.mode != WindowMode::windowed ||
                (options.aspect != ViewAspect::profile_default && options.aspect != ViewAspect::original) ||
                options.controller.deadzone != ControllerTuning{}.deadzone ||
                options.controller.sensitivity != ControllerTuning{}.sensitivity ||
                options.controller.pitch != PitchDirection::normal)) {
            throw std::invalid_argument("Enhanced controls and presentation options require enhanced mode");
        }
        const auto scaling = options.scaling == ImageScaling::profile_default ?
            (enhanced ? ImageScaling::integer : ImageScaling::nearest) : options.scaling;
        settings.scaling = scaling;
        const auto aspect = options.aspect == ViewAspect::profile_default ?
            (enhanced ? ViewAspect::widescreen : ViewAspect::original) : options.aspect;
        settings.aspect = aspect;
        if (enhanced && options.renderer != DesktopRenderer::cpu) {
#ifdef COMANCHECD_OPENGL_DESKTOP
            try {
                gpu = std::make_unique<GlDesktopPresenter>(scaling, aspect, options.pacing);
            } catch (const std::runtime_error& error) {
                if (options.renderer == DesktopRenderer::gpu) { throw; }
                SDL_Log("Desktop GPU unavailable, using CPU renderer (%s)", error.what());
            }
#else
            if (options.renderer == DesktopRenderer::gpu) {
                throw std::runtime_error("GPU desktop support requires a build with COMANCHECD_BUILD_OPENGL=ON and OpenGL");
            }
#endif
        }
        if (!gpu_available()) {
            window.reset(SDL_CreateWindow("Comanche CD training preview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                enhanced && aspect == ViewAspect::widescreen ? 1280 : 960, 720,
                SDL_WINDOW_RESIZABLE | (enhanced ? SDL_WINDOW_ALLOW_HIGHDPI : 0)));
            if (!window) { throw sdl_error("SDL2 window creation failed"); }
            presenter = std::make_unique<SdlPresenter>(window.get(), scaling, options.pacing, aspect);
        }
        if (enhanced) {
            display = std::make_unique<SdlDisplay>(output_window(), options.frame_limit, options.window);
            request_save();
            controller = std::make_unique<SdlController>(options.controller_mappings, options.controller, options.bindings);
            if (!gpu_available() && cockpit == CockpitPresentation::spatial && scene_rendering == SceneRendering::spatial) {
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

    void request_save() {
        preferences_dirty = true;
        save_at = Clock::now() + preferences_debounce;
    }

    void save_if_due(bool force) {
        if (!display || !save_preferences || !preferences_dirty || (!force && Clock::now() < save_at)) {
            return;
        }
        settings.window = display->placement();
        try {
            save_preferences(settings);
            preferences_dirty = false;
            preferences_error.clear();
        } catch (const std::exception& error) {
            preferences_error = "preferences save failed";
            SDL_Log("Host preferences save failed (%s)", error.what());
            save_at = Clock::now() + preferences_retry;
        }
    }

    void cycle_frame_limit() {
        auto limit = settings.frame_limit;
        if (limit.mode == FrameLimitMode::profile_default || limit.mode == FrameLimitMode::display) {
            limit = {FrameLimitMode::fixed, frame_rate_presets.front()};
        } else if (limit.mode == FrameLimitMode::unlimited) {
            limit = {FrameLimitMode::display};
        } else {
            const auto next = std::upper_bound(frame_rate_presets.begin(), frame_rate_presets.end(), limit.rate);
            limit = next == frame_rate_presets.end() ? FrameLimit{FrameLimitMode::unlimited} :
                FrameLimit{FrameLimitMode::fixed, *next};
        }
        display->set_limit(limit);
        settings.frame_limit = limit;
    }

    void toggle_vsync() {
        const auto pacing = settings.pacing == DisplayPacing::vertical_sync ?
            DisplayPacing::immediate : DisplayPacing::vertical_sync;
#ifdef COMANCHECD_OPENGL_DESKTOP
        if (gpu) {
            gpu->set_pacing(pacing);
        } else
#endif
        {
            // SDL 2.0.10 changes renderer pacing by recreating its presentation resources.
            presenter.reset();
            try {
                presenter = std::make_unique<SdlPresenter>(window.get(), settings.scaling, pacing, settings.aspect);
            } catch (...) {
                presenter = std::make_unique<SdlPresenter>(window.get(), settings.scaling, settings.pacing, settings.aspect);
                throw;
            }
        }
        settings.pacing = pacing;
        display->reset_deadline();
    }

    bool presentation_key(const SDL_KeyboardEvent& event) {
        if (!display) {
            return false;
        }
        const auto key = event.keysym.scancode;
        if (presentation_held[key]) {
            if (event.type == SDL_KEYUP) {
                presentation_held[key] = false;
            }
            return true;
        }
        const auto modifiers = event.keysym.mod;
        const bool fullscreen = key == SDL_SCANCODE_RETURN && (modifiers & KMOD_ALT) != 0;
        const bool display_control = (key == SDL_SCANCODE_F1 || key == SDL_SCANCODE_F2) &&
            (modifiers & KMOD_CTRL) != 0 && (modifiers & KMOD_ALT) == 0;
        if (event.type != SDL_KEYDOWN || (!fullscreen && !display_control) || held[key]) {
            return false;
        }
        presentation_held[key] = true;
        if (event.repeat != 0) {
            return true;
        }
        try {
            if (fullscreen) {
                display->toggle_fullscreen();
            } else if (key == SDL_SCANCODE_F1) {
                cycle_frame_limit();
            } else {
                toggle_vsync();
            }
            presentation_error.clear();
            request_save();
        } catch (const std::exception& error) {
            if (!gpu_available() && !presenter) { throw; }
            presentation_error = "display change failed";
            SDL_Log("Host display change failed (%s)", error.what());
        }
        return true;
    }

public:
    bool gpu_available() const {
#ifdef COMANCHECD_OPENGL_DESKTOP
        return gpu != nullptr;
#else
        return false;
#endif
    }
    SDL_Window* output_window() const {
#ifdef COMANCHECD_OPENGL_DESKTOP
        if (gpu) { return gpu->window(); }
#endif
        return window.get();
    }
    void present_image(const CockpitImage& image) {
#ifdef COMANCHECD_OPENGL_DESKTOP
        if (gpu) { gpu->present(image); return; }
#endif
        presenter->present(image);
    }
    std::pair<unsigned, unsigned> spatial_size() const {
#ifdef COMANCHECD_OPENGL_DESKTOP
        if (gpu) { return gpu->spatial_size(); }
#endif
        return presenter->spatial_size();
    }
    void stop_look() {
        if (relative_mouse) { SDL_SetRelativeMouseMode(SDL_FALSE); }
        looking = relative_mouse = false;
    }
    void release_input(HostInput& input) {
        stop_look();
        for (std::size_t key = 0; key < held.size(); ++key) {
            if (held[key] && !controller_held[key]) { append_dos_key(input.scan_bytes, static_cast<SDL_Scancode>(key), false); }
        }
        held.fill(false);
        suppressed.fill(false);
        if (controller) { controller->set_focus(false); flush_controller_keys(input); }
        input.pause = PauseAction::pause;
    }

    void flush_controller_keys(HostInput& input) {
        if (!controller) { return; }
        for (const auto& change : controller->take_keys()) {
            controller_held[change.key] = change.down;
            if (change.down || !held[change.key]) { append_dos_key(input.scan_bytes, change.key, change.down); }
            if (change.down) {
                input.keys.push_back({dos_menu_scan(change.key)});
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
            if (event.type == SDL_WINDOWEVENT && event.window.windowID != 0 &&
                    event.window.windowID != SDL_GetWindowID(output_window())) { continue; }
            if (display && event.type == SDL_WINDOWEVENT && display->event(event.window)) {
                request_save();
            }
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
                    append_dos_key(input.scan_bytes, key, event.type == SDL_KEYDOWN);
                }
                if (event.type == SDL_KEYUP) {
                    suppressed[key] = false;
                    continue;
                }
                input.key_pressed = true;
                const auto scan = dos_menu_scan(key);
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
        save_if_due(input.quit);
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
#ifdef COMANCHECD_OPENGL_DESKTOP
    std::unique_ptr<GlDesktopPresenter> gpu;
#endif
    SdlAudioOutput audio;
    std::unique_ptr<SdlController> controller;
    std::array<bool, SDL_NUM_SCANCODES> held{};
    std::array<bool, SDL_NUM_SCANCODES> controller_held{};
    std::array<bool, SDL_NUM_SCANCODES> suppressed{};
    InputProfile profile;
    std::array<bool, SDL_NUM_SCANCODES> presentation_held{};
    SdlHostOptions settings;
    PreferencesWriter save_preferences;
    std::unique_ptr<SdlDisplay> display;
    bool preferences_dirty = false;
    Clock::time_point save_at{};
    std::string preferences_error, presentation_error;
    HostFocus external_focus = HostFocus::active;
    bool focus_release_pending = false;
    InputContext context = InputContext::flight;
    std::uint8_t throttle = 0;
    CockpitControls displays;
};

SdlFlightHost::SdlFlightHost(SdlHostOptions options, PreferencesWriter save)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(save))) {}
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
    if (impl_->display) { impl_->display->wait(); }
    impl_->present_image(image);
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
    if (impl_->display) { impl_->display->wait(); }
    const auto time = impl_->presentation_time();
    impl_->camera_history.observe(frame->spatial, frame->cpu_cycles, time);
    const auto camera = impl_->interpolation == CameraInterpolation::enabled ?
        impl_->camera_history.sample(time) : spatial_camera(frame->spatial->camera);
    const auto size = impl_->spatial_size();
    const auto poses = impl_->interpolation == CameraInterpolation::enabled ?
        impl_->camera_history.sample_entities(time) : entity_poses(*frame->spatial);
    const auto view = desktop_camera(camera, size.first, size.second);
    impl_->look_available = impl_->cockpit == CockpitPresentation::spatial && frame->spatial->cockpit_mode == 1;
#ifdef COMANCHECD_OPENGL_DESKTOP
    if (impl_->gpu) {
        std::optional<CameraView> body;
        auto world = view;
        if (impl_->look_available) {
            const auto pose = impl_->look.pose();
            world = eye_view(view, pose, view.frustum, 1);
            body = cockpit_view(pose, view.frustum);
        } else { impl_->stop_look(); }
        const auto overlay = body ? SceneOverlay::none :
            impl_->cockpit_scaling == CockpitScaling::sharp ? SceneOverlay::sharp : SceneOverlay::nearest;
        impl_->gpu->present_scene(*frame->spatial, world, poses, frame->image, body, overlay);
        update_title(frame->flight, status);
        return;
    }
#endif
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
    impl_->cockpit_image = impl_->spatial_renderer.render_world(*frame->spatial, view, poses,
        frame->image.palette, size.first, size.second);
    composite_cockpit(impl_->cockpit_image, frame->image, frame->spatial->scene_mask,
        impl_->cockpit_scaling, OverlayLayout::centered);
    impl_->presenter->present_spatial(impl_->cockpit_image.pixels, size.first, size.second);
    update_title(frame->flight, status);
}

void SdlFlightHost::update_title(const FlightState& state, const std::string& status) {
    std::ostringstream title;
    title << (impl_->profile == InputProfile::enhanced ? (impl_->gpu_available() ? "OpenCommanche enhanced GPU | " : "OpenCommanche enhanced CPU | ") : "Comanche CD | ")
          << status;
    if (impl_->display) {
        title << " | FPS " << impl_->display->limit_label()
              << " | vsync " << (impl_->settings.pacing == DisplayPacing::vertical_sync ? "on" : "off")
              << " (Ctrl+F1/F2)";
        if (!impl_->preferences_error.empty()) { title << " | " << impl_->preferences_error; }
        if (!impl_->presentation_error.empty()) { title << " | " << impl_->presentation_error; }
    }
    title << " | throttle " << unsigned(impl_->throttle)
          << "/15 | height " << unsigned(state.view.height) << " | armor " << state.armor
          << " | weapon " << unsigned(state.weapon + 1) << " | ammo " << state.ammunition[state.weapon];
    const auto text = title.str();
    if (text != SDL_GetWindowTitle(impl_->output_window())) {
        SDL_SetWindowTitle(impl_->output_window(), text.c_str());
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
        if (impl_->display) { impl_->display->reset_deadline(); }
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
