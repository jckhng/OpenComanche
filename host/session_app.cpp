#include "session_app.hpp"
#include "profile.hpp"
#include "settings_profile.hpp"
#include <iostream>

namespace comanchecd::host {
void run_session_app(const std::filesystem::path& installation, const std::filesystem::path& payload,
                     const std::filesystem::path& directory,
                     const std::function<std::unique_ptr<FlightHost>()>& create_host) {
    const auto profile_path = std::filesystem::absolute(directory).lexically_normal();
    for (const auto& root : {installation, payload}) {
        if (profile_path == std::filesystem::absolute(root).lexically_normal() ||
            (std::filesystem::exists(profile_path) && std::filesystem::equivalent(profile_path, root))) {
            throw std::invalid_argument("Use a separate native profile directory");
        }
    }
    const PilotProfile profile(profile_path);
    const SettingsProfile settings(profile_path);
    GameSession session(
        Installation(installation, payload), [&](const auto& record) { profile.write(record); },
        profile.read(), settings.read(), [&](const auto& record) { settings.write(record); },
        SessionStart::cold);
    auto host = create_host();
    std::cout << "Arrows/Enter navigate | Tab/Enter acquires targets | Space fires | E/D throttle | P pauses "
                 "| Alt-A ends mission | Esc leaves debrief\n";
    run_session(session, *host);
}
} // namespace comanchecd::host
