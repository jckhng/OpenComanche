#include "sdl_host.hpp"
#include "sdl_common.hpp"
#include <iostream>

namespace {
using namespace comanchecd::host;
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
void key(Uint32 type, SDL_Scancode code, Uint16 modifiers = 0) {
    SDL_Event event{};
    event.type = type;
    event.key.keysym.scancode = code;
    event.key.keysym.mod = modifiers;
    require(SDL_PushEvent(&event) == 1, "Controller editor input must enter SDL");
}
HostInput tap(SdlFlightHost& host, SDL_Scancode code, Uint16 modifiers = 0) {
    key(SDL_KEYDOWN, code, modifiers);
    key(SDL_KEYUP, code);
    return host.poll();
}
void neutral(const HostInput& input) {
    require(input.scan_bytes.empty() && input.keys.empty() && !input.key_pressed && !input.escape &&
                !input.controls.fire && !input.pressed.fire && input.controls.horizontal == 0 &&
                input.controls.vertical == 0,
            "Editor navigation must stay outside native command input");
}
} // namespace

int main() {
    try {
        using namespace comanchecd::host;
        SdlHostOptions options;
        options.input = InputProfile::enhanced;
        options.renderer = DesktopRenderer::cpu;
        options.frame_limit.mode = FrameLimitMode::unlimited;
        options.pacing = DisplayPacing::immediate;
        {
            SdlFlightHost host(options, [](const auto&) {});
            host.poll();
            host.set_playback(Playback::running);
            key(SDL_KEYDOWN, SDL_SCANCODE_SPACE);
            require(host.poll().controls.fire, "Regression must start with a held native action");
            auto input = tap(host, SDL_SCANCODE_F3, KMOD_CTRL);
            require(input.pause == PauseAction::pause &&
                        input.scan_bytes == std::vector<std::uint8_t>{0xb9} && !input.controls.fire,
                    "Opening the editor must release held actions and request native pause");
            neutral(tap(host, SDL_SCANCODE_RETURN));
            neutral(tap(host, SDL_SCANCODE_ESCAPE));
            neutral(tap(host, SDL_SCANCODE_ESCAPE));
            require(host.poll().pause == PauseAction::none,
                    "The editor must issue one pause request while waiting for acknowledgment");
            host.present({}, {}, "Flight");
            auto* window = SDL_GetWindowFromID(1);
            require(window && std::string(SDL_GetWindowTitle(window)).find("Controller settings") !=
                                  std::string::npos,
                    "A close request must wait for the simulation's pause acknowledgment");
            host.set_playback(Playback::paused);
            input = host.poll();
            neutral(input);
            require(input.pause == PauseAction::resume,
                    "A running session must resume explicitly after acknowledged close");
            key(SDL_KEYUP, SDL_SCANCODE_SPACE);
            neutral(host.poll());

            for (const auto shortcut : {SDL_SCANCODE_F3, SDL_SCANCODE_F4}) {
                for (const auto context :
                     {InputContext::menu, InputContext::name_entry, InputContext::flight}) {
                    host.set_input_context(context);
                    host.set_playback(Playback::running);
                    require(tap(host, shortcut, KMOD_CTRL).pause == PauseAction::pause,
                            "Controller setup must pause every native input context");
                    host.set_playback(Playback::paused);
                    input = tap(host, SDL_SCANCODE_ESCAPE);
                    neutral(input);
                    require(input.pause == PauseAction::resume,
                            "Name entry must retain its host-owned resume route");
                }
                host.set_playback(Playback::paused);
                tap(host, shortcut, KMOD_CTRL);
                host.set_playback(Playback::paused);
                require(tap(host, SDL_SCANCODE_ESCAPE).pause == PauseAction::pause,
                        "An already paused session must retain pause on editor close");

                host.set_playback(Playback::running);
                tap(host, shortcut, KMOD_CTRL);
                host.set_playback(Playback::paused);
                SDL_Event focus{};
                focus.type = SDL_WINDOWEVENT;
                focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
                SDL_PushEvent(&focus);
                host.poll();
                focus.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
                SDL_PushEvent(&focus);
                host.poll();
                require(tap(host, SDL_SCANCODE_ESCAPE).pause == PauseAction::pause,
                        "Focus loss during editing must preserve pause after closing");
            }
        }
        {
            unsigned attempts = 0;
            SdlHostOptions saved;
            SdlFlightHost storage(options, [&](const SdlHostOptions& candidate) {
                saved = candidate;
                if (++attempts == 1) {
                    throw std::runtime_error("Synthetic write failure");
                }
            });
            storage.poll();
            storage.set_playback(Playback::running);
            tap(storage, SDL_SCANCODE_F3, KMOD_CTRL);
            storage.set_playback(Playback::paused);
            for (unsigned row = 0; row < 5; ++row) {
                neutral(tap(storage, SDL_SCANCODE_DOWN));
            }
            neutral(tap(storage, SDL_SCANCODE_RIGHT));
            for (unsigned row = 0; row < 5; ++row) {
                neutral(tap(storage, SDL_SCANCODE_DOWN));
            }
            neutral(tap(storage, SDL_SCANCODE_RETURN));
            require(attempts == 1 && saved.controller.deadzone == options.controller.deadzone + 512,
                    "Apply must offer one complete staged controller configuration to persistence");
            neutral(tap(storage, SDL_SCANCODE_RETURN));
            require(attempts == 2 && saved.controller.deadzone == options.controller.deadzone + 512,
                    "A failed save must retain staged changes for explicit retry");
            require(tap(storage, SDL_SCANCODE_F3, KMOD_CTRL).pause == PauseAction::resume,
                    "The editor shortcut must also close its active screen");
        }
        SdlHostOptions audio_saved;
        {
            unsigned audio_attempts = 0;
            SdlFlightHost audio(options, [&](const SdlHostOptions& candidate) {
                audio_saved = candidate;
                if (++audio_attempts == 1) {
                    throw std::runtime_error("Synthetic audio save failure");
                }
            });
            audio.poll();
            const comanchecd::AudioOutputFrame frame{{8192000, 8192000}, {8192000, 8192000}, {2000, 2000}};
            const auto output = audio.audio_output_transform();
            require(output(frame)[0] == 2000,
                    "The enhanced host must expose unity startup output by default");
            audio.set_playback(Playback::running);
            require(tap(audio, SDL_SCANCODE_F4, KMOD_CTRL).pause == PauseAction::pause,
                    "Audio setup must request native pause");
            audio.set_playback(Playback::paused);
            neutral(tap(audio, SDL_SCANCODE_LEFT));
            neutral(tap(audio, SDL_SCANCODE_DOWN));
            neutral(tap(audio, SDL_SCANCODE_DELETE));
            neutral(tap(audio, SDL_SCANCODE_DOWN));
            neutral(tap(audio, SDL_SCANCODE_LEFT));
            neutral(tap(audio, SDL_SCANCODE_DOWN));
            require(output(frame)[0] == 2000, "Staged volume must wait for successful Apply");
            neutral(tap(audio, SDL_SCANCODE_RETURN));
            require(audio_attempts == 1 && output(frame)[0] == 2000,
                    "A failed save must preserve live gains");
            neutral(tap(audio, SDL_SCANCODE_RETURN));
            require(audio_attempts == 2 && audio_saved.volume.master == 95 && audio_saved.volume.music == 0 &&
                        audio_saved.volume.effects == 95 && output(frame)[0] == 902,
                    "Apply retry must atomically publish all gains after saving");
            require(tap(audio, SDL_SCANCODE_ESCAPE).pause == PauseAction::resume,
                    "Closing audio setup must restore a running session");
            audio.set_playback(Playback::running);
            tap(audio, SDL_SCANCODE_F4, KMOD_CTRL);
            audio.set_playback(Playback::paused);
            tap(audio, SDL_SCANCODE_DELETE);
            tap(audio, SDL_SCANCODE_ESCAPE);
            require(audio_attempts == 2 && output(frame)[0] == 902,
                    "Closing without Apply must discard staged edits");
        }
        {
            const comanchecd::AudioOutputFrame frame{{8192000, 8192000}, {8192000, 8192000}, {2000, 2000}};
            audio_saved.window = {};
            SdlFlightHost restarted(audio_saved, [](const auto&) {});
            restarted.poll();
            require(restarted.audio_output_transform()(frame)[0] == 902,
                    "Restored preferences must take effect before session startup");
        }
        std::cout << "Controller UI pause handoff, input ownership and save retry passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
