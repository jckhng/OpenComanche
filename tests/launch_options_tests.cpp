#include "launch_options.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        using namespace comanchecd::host;
        require(launch_arguments(Application::faithful, {}).empty(),
                "Faithful launch must require no mode switch");
        require(launch_arguments(Application::faithful, {"--mode", "faithful"}).empty(),
                "Matching legacy mode must remain accepted");
        require(launch_arguments(Application::enhanced, {}) == std::vector<std::string>{"--mode", "enhanced"},
                "Enhanced launch must select its mode before loading preferences");
        const auto xr = launch_arguments(Application::openxr, {"--xr-world-scale", "10"});
        require(xr == std::vector<std::string>{"--mode", "enhanced", "--xr", "on", "--xr-world-scale", "8",
                                               "--xr-world-scale", "10"},
                "XR launch must enable XR and allow overriding the established playtest world scale");
        for (const auto app : {Application::faithful, Application::enhanced, Application::openxr}) {
            for (const auto args : {std::vector<std::string>{"--mode", "wrong"}, {"--mode"}}) {
                bool rejected = false;
                try {
                    launch_arguments(app, args);
                } catch (const std::invalid_argument&) {
                    rejected = true;
                }
                require(rejected, "Invalid launch modes must fail before loading original resources");
            }
        }
        for (const auto item :
             {std::pair<Application, std::vector<std::string>>{Application::faithful, {"--mode", "enhanced"}},
              {Application::faithful, {"--scale", "linear"}},
              {Application::faithful, {"--xr", "off"}},
              {Application::enhanced, {"--mode", "faithful"}},
              {Application::enhanced, {"--xr", "on"}},
              {Application::enhanced, {"--xr-loader", "loader"}},
              {Application::openxr, {"--xr", "off"}},
              {Application::openxr, {"--mode", "faithful"}}}) {
            bool rejected = false;
            try {
                launch_arguments(item.first, item.second);
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected, "Each binary must reject cross-mode options");
        }
        std::cout << "Fixed faithful, enhanced and OpenXR launch boundaries passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
