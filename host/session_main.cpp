#include "sdl_host.hpp"
#include "sdl_options.hpp"
#include "profile.hpp"
#include "settings_profile.hpp"
#include "host_preferences.hpp"
#include "openxr_options.hpp"
#ifdef COMANCHECD_OPENXR_RUNTIME
#include "openxr_runtime.hpp"
#include "openxr_host.hpp"
#endif

#include <iostream>

int main(int argc, char** argv) {
    try {
        constexpr const char* usage =
            "comanche_viewer INSTALLATION PAYLOAD_DIRECTORY PROFILE_DIRECTORY"
            " [--mode faithful|enhanced] [--controller-mappings FILE]"
            " [--bind menu.CONTROL=ACTION|flight.CONTROL=ACTION]"
            " [--steer-x none|leftx|lefty|rightx|righty] [--steer-y none|leftx|lefty|rightx|righty]"
            " [--stick-deadzone 0..32766] [--stick-sensitivity 1..400]"
            " [--invert-y on|off] [--scale nearest|linear|integer] [--renderer auto|gpu|cpu] [--aspect original|widescreen] [--scene-renderer native|spatial] [--interpolation on|off]"
            " [--cockpit-scale nearest|sharp] [--cockpit flat|spatial]"
            " [--xr on|off] [--xr-world-scale CELLS_PER_METRE] [--xr-render-scale 0.1..1] [--xr-loader FILE] [--xr-renderer gpu|cpu] [--xr-profile CSV]";
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << usage << "\ncomanche_viewer --openxr-info [--xr-loader FILE] (OpenXR-enabled build)\n";
            return 0;
        }
        if ((argc == 2 || (argc == 4 && std::string(argv[2]) == "--xr-loader")) && std::string(argv[1]) == "--openxr-info") {
#ifdef COMANCHECD_OPENXR_RUNTIME
            const comanchecd::host::SdlOpenXrLoader loader(argc == 4 ? argv[3] : "");
            const comanchecd::host::OpenXrRuntime runtime(loader.entry());
            const auto& capabilities = runtime.capabilities();
            std::cout << capabilities.runtime_name << ' ' << XR_VERSION_MAJOR(capabilities.runtime_version) << '.' <<
                XR_VERSION_MINOR(capabilities.runtime_version) << '.' << XR_VERSION_PATCH(capabilities.runtime_version) <<
                "\nOpenGL " << XR_VERSION_MAJOR(capabilities.minimum_opengl) << '.' <<
                XR_VERSION_MINOR(capabilities.minimum_opengl) << " through " << XR_VERSION_MAJOR(capabilities.maximum_opengl) << '.' <<
                XR_VERSION_MINOR(capabilities.maximum_opengl) << '\n';
            return 0;
#else
            throw std::invalid_argument("Configure COMANCHECD_BUILD_OPENXR=ON with OpenXR SDK headers for runtime admission checks");
#endif
        }
        if (argc < 4 || (argc - 4) % 2 != 0) {
            throw std::invalid_argument(usage);
        }
        const auto profile_path = std::filesystem::absolute(argv[3]).lexically_normal();
        for (const auto root : {argv[1], argv[2]}) {
            if (profile_path == std::filesystem::absolute(root).lexically_normal() ||
                    (std::filesystem::exists(profile_path) && std::filesystem::equivalent(profile_path, root))) {
                throw std::invalid_argument("Use a separate native profile directory");
            }
        }
        const comanchecd::host::HostPreferences preferences(profile_path);
        auto arguments = std::vector<std::string>(argv + 4, argv + argc);
        const auto xr = comanchecd::host::take_openxr_options(arguments);
        const auto options = preferences.resolve(arguments);
        if (xr.enabled && options.input != comanchecd::host::InputProfile::enhanced) {
            throw std::invalid_argument("XR requires --mode enhanced");
        }
#ifndef COMANCHECD_OPENXR_RUNTIME
        if (xr.enabled) { throw std::invalid_argument("Configure COMANCHECD_BUILD_OPENXR=ON for headset presentation"); }
#endif
        const comanchecd::host::PilotProfile profile(profile_path);
        const comanchecd::host::SettingsProfile settings(profile_path);
        comanchecd::GameSession session(comanchecd::Installation(argv[1], argv[2]),
            [&](const auto& record) { profile.write(record); }, profile.read(), settings.read(),
            [&](const auto& record) { settings.write(record); }, comanchecd::SessionStart::cold);
        std::unique_ptr<comanchecd::host::FlightHost> host;
#ifdef COMANCHECD_OPENXR_RUNTIME
        if (xr.enabled) { host = std::make_unique<comanchecd::host::OpenXrHost>(options, xr); }
#endif
        if (!host) { host = std::make_unique<comanchecd::host::SdlFlightHost>(options); }
        if (options.input == comanchecd::host::InputProfile::enhanced) { preferences.write(options); }
        std::cout << "Arrows/Enter navigate | Tab/Enter acquires targets | Space fires | E/D throttle | P pauses | Alt-A ends mission | Esc leaves debrief\n";
        comanchecd::host::run_session(session, *host);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
