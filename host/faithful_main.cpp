#include "faithful_sdl_host.hpp"
#include "launch_options.hpp"
#include "session_app.hpp"
#include <iostream>

int main(int argc, char** argv) {
    try {
        constexpr const char* usage = "comanche_faithful INSTALLATION PAYLOAD_DIRECTORY PROFILE_DIRECTORY";
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << usage
                      << "\nFaithful DOS reconstruction with SDL2 keyboard, indexed display and audio.\n";
            return 0;
        }
        if (argc < 4) {
            throw std::invalid_argument(usage);
        }
        using namespace comanchecd::host;
        launch_arguments(Application::faithful, {argv + 4, argv + argc});
        run_session_app(argv[1], argv[2], argv[3], [] { return std::make_unique<FaithfulSdlHost>(); });
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
