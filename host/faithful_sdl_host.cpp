#include "faithful_sdl_host.hpp"
#include "sdl_keyboard.hpp"
#include "sdl_audio.hpp"
#include "sdl_presenter.hpp"
#include <algorithm>
#include <array>
#include <sstream>

namespace comanchecd::host {
class FaithfulSdlHost::Impl {
  public:
    Impl() {
        window.reset(SDL_CreateWindow("Comanche CD", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720,
                                      SDL_WINDOW_RESIZABLE));
        if (!window) {
            throw sdl_error("SDL2 window creation failed");
        }
        presenter = std::make_unique<SdlPresenter>(window.get(), ImageScaling::nearest);
    }
    void release_input(HostInput& input) {
        for (std::size_t key = 0; key < held.size(); ++key) {
            if (held[key]) {
                append_dos_key(input.scan_bytes, static_cast<SDL_Scancode>(key), false);
            }
        }
        held.fill(false);
        input.pause = PauseAction::pause;
    }
    HostInput poll() {
        HostInput input;
        const bool flight_input = context == InputContext::flight || context == InputContext::training;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_WINDOWEVENT && event.window.windowID != 0 &&
                event.window.windowID != SDL_GetWindowID(window.get())) {
                continue;
            }
            if (event.type == SDL_QUIT) {
                input.quit = true;
            } else if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    input.quit = true;
                } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                           event.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    release_input(input);
                }
            } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                const auto key = event.key.keysym.scancode;
                if (key < SDL_SCANCODE_UNKNOWN || key >= SDL_NUM_SCANCODES) {
                    continue;
                }
                const bool modified = (event.key.keysym.mod & (KMOD_ALT | KMOD_CTRL)) != 0;
                held[key] = event.type == SDL_KEYDOWN;
                append_dos_key(input.scan_bytes, key, event.type == SDL_KEYDOWN);
                if (event.type == SDL_KEYUP) {
                    continue;
                }
                input.key_pressed = true;
                const auto scan = dos_menu_scan(key);
                if (flight_input && key == SDL_SCANCODE_A && (event.key.keysym.mod & KMOD_ALT) != 0 &&
                    event.key.repeat == 0) {
                    input.abort_mission = true;
                }
                if (scan != 0) {
                    input.keys.push_back({scan, (event.key.keysym.mod & KMOD_SHIFT) != 0 ? PilotShift::both
                                                                                         : PilotShift::none});
                }
                if (key == SDL_SCANCODE_ESCAPE) {
                    input.escape = true;
                }
                if (key == SDL_SCANCODE_P && !modified && context != InputContext::name_entry &&
                    event.key.repeat == 0 && input.pause != PauseAction::pause) {
                    input.pause = PauseAction::toggle;
                }
                if (key == SDL_SCANCODE_R && context == InputContext::training && !modified &&
                    event.key.repeat == 0) {
                    input.reset = true;
                }
                if (!flight_input || modified) {
                    continue;
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
        if (flight_input) {
            input.controls.horizontal =
                keyboard_control * (int(held[SDL_SCANCODE_RIGHT]) - int(held[SDL_SCANCODE_LEFT]));
            input.controls.vertical =
                keyboard_control * (int(held[SDL_SCANCODE_DOWN]) - int(held[SDL_SCANCODE_UP]));
            input.controls.throttle = throttle;
            input.controls.fire = held[SDL_SCANCODE_SPACE];
            input.controls.cycle_target = held[SDL_SCANCODE_TAB] || held[SDL_SCANCODE_RETURN];
            input.controls.chaff = context == InputContext::training && held[SDL_SCANCODE_SEMICOLON];
            input.controls.flare = context == InputContext::training && held[SDL_SCANCODE_APOSTROPHE];
            input.cockpit = displays;
        }
        return input;
    }

    SdlSubsystem subsystem{SDL_INIT_VIDEO | SDL_INIT_EVENTS};
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{nullptr, SDL_DestroyWindow};
    std::unique_ptr<SdlPresenter> presenter;
    SdlAudioOutput audio;
    std::array<bool, SDL_NUM_SCANCODES> held{};
    InputContext context = InputContext::flight;
    std::uint8_t throttle = 0;
    CockpitControls displays;
};

FaithfulSdlHost::FaithfulSdlHost() : impl_(std::make_unique<Impl>()) {}
FaithfulSdlHost::~FaithfulSdlHost() = default;
HostInput FaithfulSdlHost::poll() {
    return impl_->poll();
}
void FaithfulSdlHost::set_input_context(InputContext context) {
    impl_->context = context;
}
void FaithfulSdlHost::present(const CockpitImage& image, const FlightState& state,
                              const std::string& status) {
    impl_->presenter->present(image);
    std::ostringstream title;
    title << "Comanche CD | " << status << " | throttle " << unsigned(impl_->throttle) << "/15 | height "
          << unsigned(state.view.height) << " | armor " << state.armor << " | weapon "
          << unsigned(state.weapon + 1) << " | ammo " << state.ammunition[state.weapon];
    const auto text = title.str();
    if (text != SDL_GetWindowTitle(impl_->window.get())) {
        SDL_SetWindowTitle(impl_->window.get(), text.c_str());
    }
}
void FaithfulSdlHost::queue_audio(const std::vector<std::int16_t>& samples) {
    impl_->audio.queue(samples);
}
std::size_t FaithfulSdlHost::queued_audio_samples() const {
    return impl_->audio.queued_samples();
}
void FaithfulSdlHost::clear_audio() {
    impl_->audio.clear();
}
void FaithfulSdlHost::set_playback(Playback playback) {
    impl_->audio.set_playback(playback);
}
void FaithfulSdlHost::idle() {
    SDL_Delay(1);
}
void FaithfulSdlHost::reset_controls() {
    impl_->held.fill(false);
    impl_->throttle = 0;
    impl_->displays = {};
}
} // namespace comanchecd::host
