#include "host_preferences.hpp"
#include "sdl_common.hpp"
#include "sdl_display.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void key(Uint32 type, SDL_Scancode code, Uint16 modifiers = 0, Uint8 repeat = 0) {
    SDL_Event event{};
    event.type = type;
    event.key.keysym.scancode = code;
    event.key.keysym.mod = modifiers;
    event.key.repeat = repeat;
    require(SDL_PushEvent(&event) == 1, "Display key must enter the SDL event queue");
}

void close_event() {
    SDL_Event event{};
    event.type = SDL_QUIT;
    require(SDL_PushEvent(&event) == 1, "Close must enter the SDL event queue");
}

class TemporaryProfile {
  public:
    TemporaryProfile()
        : path(std::filesystem::temp_directory_path() /
               ("opencomanche-display-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        require(std::filesystem::create_directory(path), "Fixture must own its profile");
    }
    ~TemporaryProfile() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    const std::filesystem::path path;
};
} // namespace

int main() {
    try {
        using namespace comanchecd::host;
        const TemporaryProfile fixture;
        const HostPreferences preferences(fixture.path);
        auto options = parse_sdl_options({"--mode", "enhanced", "--renderer", "cpu", "--fps", "77", "--vsync",
                                          "off", "--window-size", "640x480", "--window-position", "40,40"});
        unsigned saves = 0;
        SdlHostOptions saved;
        {
            SdlFlightHost host(options, [&](const SdlHostOptions& updated) {
                preferences.write(updated);
                saved = updated;
                ++saves;
            });
            host.poll();
            host.set_input_context(InputContext::flight);
            key(SDL_KEYDOWN, SDL_SCANCODE_F1, KMOD_CTRL);
            key(SDL_KEYDOWN, SDL_SCANCODE_F1, KMOD_CTRL, 1);
            // Release Ctrl before F1. The matching release must remain host-owned.
            key(SDL_KEYUP, SDL_SCANCODE_F1);
            const auto input = host.poll();
            require(input.scan_bytes.empty() && input.keys.empty() && !input.key_pressed &&
                        input.pause == PauseAction::none &&
                        input.cockpit.left == comanchecd::InstrumentDisplay::map,
                    "FPS hotkeys and repeats must stay outside DOS keyboard, pause and cockpit state");
            require(saves == 0, "Preference changes must debounce instead of writing per event");
            key(SDL_KEYDOWN, SDL_SCANCODE_F2, KMOD_CTRL);
            key(SDL_KEYDOWN, SDL_SCANCODE_F2, KMOD_CTRL, 1);
            key(SDL_KEYUP, SDL_SCANCODE_F2);
            require(host.poll().scan_bytes.empty(), "Vsync changes must consume both edges");
            comanchecd::CockpitImage image;
            comanchecd::FlightState state;
            host.present(image, state, "display test");
            auto* window = SDL_GetWindowFromID(1);
            require(window != nullptr, "The fixture owns its SDL window");
            require(std::string(SDL_GetWindowTitle(window)).find("FPS 90") != std::string::npos,
                    "The title must expose the active FPS limit");
            SDL_SetWindowSize(window, 700, 500);
            SDL_SetWindowPosition(window, 80, 60);
            host.poll();
            close_event();
            require(host.poll().quit && saves == 1 && saved.frame_limit.rate == 90 &&
                        saved.pacing == DisplayPacing::vertical_sync && saved.window.width == 700 &&
                        saved.window.height == 500 && saved.window.position->x == 80 &&
                        saved.window.position->y == 60,
                    "Close must flush one complete debounced preference update");
            key(SDL_KEYDOWN, SDL_SCANCODE_F1);
            key(SDL_KEYUP, SDL_SCANCODE_F1);
            require(host.poll().scan_bytes == std::vector<std::uint8_t>{0x3b, 0xbb},
                    "Unmodified cockpit keys must preserve their DOS byte sequence");
        }
        const auto loaded = preferences.resolve({"--mode", "enhanced"});
        require(loaded.frame_limit.mode == FrameLimitMode::fixed && loaded.frame_limit.rate == 90 &&
                    loaded.pacing == DisplayPacing::vertical_sync && loaded.window.width == 700 &&
                    loaded.window.height == 500 && loaded.window.position->x == 80,
                "Restart must restore FPS, vsync and normal window bounds");
        {
            SdlFlightHost host(loaded);
            host.poll();
            // SDL reinitialization restarts the window ID sequence.
            auto* window = SDL_GetWindowFromID(1);
            int width = 0, height = 0, x = 0, y = 0;
            require(window != nullptr, "Restart must create a window");
            SDL_GetWindowSize(window, &width, &height);
            SDL_GetWindowPosition(window, &x, &y);
            require(width == 700 && height == 500 && x == 80 && y == 60,
                    "Saved geometry must be applied to the SDL window");
        }
        {
            unsigned attempts = 0;
            options.window.position = WindowPosition{999999, -999999};
            SdlFlightHost host(options, [&](const SdlHostOptions& updated) {
                saved = updated;
                if (++attempts == 1) {
                    throw std::runtime_error("Synthetic storage failure");
                }
            });
            close_event();
            require(host.poll().quit && attempts == 1, "Save failure must preserve normal close handling");
            close_event();
            require(host.poll().quit && attempts == 2 && saved.window.position->x != 999999 &&
                        saved.window.position->y != -999999,
                    "Pending saves must retry and recover disconnected-monitor positions");
        }
        {
            SdlSubsystem video(SDL_INIT_VIDEO);
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Window placement test", 40, 40, 640, 480, SDL_WINDOW_RESIZABLE),
                SDL_DestroyWindow);
            require(window != nullptr, "The placement fixture must create its SDL window");
            SdlDisplay display(window.get(), {FrameLimitMode::unlimited}, {});
            const auto normal = display.placement();
            display.toggle_fullscreen();
            require(display.placement().mode == WindowMode::fullscreen &&
                        display.placement().width == normal.width &&
                        display.placement().height == normal.height &&
                        display.placement().position->x == normal.position->x &&
                        display.placement().position->y == normal.position->y,
                    "Fullscreen must preserve the normal window rectangle");
            display.toggle_fullscreen();
            require(display.placement().mode == WindowMode::windowed &&
                        display.placement().width == normal.width &&
                        display.placement().height == normal.height,
                    "Leaving fullscreen must restore the normal size");
        }
        // Version 1 files created before display preferences must keep loading.
        std::ofstream(fixture.path / "host-preferences.cfg", std::ios::trunc) << "version=1\nscale=integer\n";
        require(preferences.read().frame_limit.mode == FrameLimitMode::profile_default,
                "Legacy preferences must inherit automatic enhanced pacing");
        std::cout << "Display hotkeys, persistence and restart passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
