#include "launch_options.hpp"
#include <stdexcept>

namespace comanchecd::host {
std::vector<std::string> launch_arguments(Application application,
                                          const std::vector<std::string>& arguments) {
    if (arguments.size() % 2 != 0) {
        throw std::invalid_argument("Each launch option requires a value");
    }
    std::vector<std::string> result;
    if (application != Application::faithful) {
        result = {"--mode", "enhanced"};
    }
    if (application == Application::openxr) {
        result.insert(result.end(), {"--xr", "on", "--xr-world-scale", "8"});
    }
    for (std::size_t index = 0; index < arguments.size(); index += 2) {
        const auto& option = arguments[index];
        const auto& value = arguments[index + 1];
        if (option == "--mode") {
            const auto expected = application == Application::faithful ? "faithful" : "enhanced";
            if (value != expected) {
                throw std::invalid_argument(
                    "The executable fixes its mode. Select the corresponding Comanche binary");
            }
            continue;
        }
        if (application == Application::faithful) {
            throw std::invalid_argument("Faithful accepts installation, payload and profile paths. Use "
                                        "comanche_enhanced or comanche_openxr for host options");
        }
        if (option == "--xr" || option.compare(0, 5, "--xr-") == 0) {
            if (application != Application::openxr) {
                throw std::invalid_argument("XR options require comanche_openxr");
            }
            if (option == "--xr" && value != "on") {
                throw std::invalid_argument("comanche_openxr requires XR presentation");
            }
        }
        result.push_back(option);
        result.push_back(value);
    }
    return result;
}
} // namespace comanchecd::host
