#include "sdl_host.hpp"
#include "host_preferences.hpp"
#include "launch_options.hpp"
#include "session_app.hpp"
#include <iostream>

int main(int argc, char** argv) {
    try {
        constexpr const char* usage =
            "comanche_enhanced INSTALLATION PAYLOAD_DIRECTORY PROFILE_DIRECTORY"
            " [--controller-mappings FILE] [--bind menu.CONTROL=ACTION|flight.CONTROL=ACTION]"
            " [--steer-x none|leftx|lefty|rightx|righty] [--steer-y none|leftx|lefty|rightx|righty]"
            " [--stick-deadzone 0..32766] [--stick-sensitivity 1..400] [--invert-y on|off]"
            " [--scale nearest|linear|integer] [--renderer auto|gpu|cpu] [--aspect original|widescreen]"
            " [--scene-renderer native|spatial] [--interpolation on|off]"
            " [--cockpit-scale nearest|sharp] [--cockpit flat|spatial]";
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << usage << "\nEnhanced SDL2 desktop controls and rendering.\n";
            return 0;
        }
        if (argc < 4) { throw std::invalid_argument(usage); }
        using namespace comanchecd::host;
        const HostPreferences preferences(std::filesystem::absolute(argv[3]).lexically_normal());
        const auto options = preferences.resolve(launch_arguments(Application::enhanced, {argv + 4, argv + argc}));
        run_session_app(argv[1], argv[2], argv[3], [&] {
            auto host = std::make_unique<SdlFlightHost>(options);
            preferences.write(options);
            return host;
        });
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
