#include "openxr_options.hpp"
#include <cmath>
#include <stdexcept>

namespace comanchecd::host {
namespace {
double number(const std::string& value) {
    std::size_t end = 0;
    const auto result = std::stod(value, &end);
    if (end != value.size() || !std::isfinite(result) || result <= 0) {
        throw std::invalid_argument("XR scales must be finite positive numbers");
    }
    return result;
}
}
OpenXrOptions take_openxr_options(std::vector<std::string>& arguments) {
    if (arguments.size() % 2) { throw std::invalid_argument("Each host option requires a value"); }
    OpenXrOptions result;
    std::vector<std::string> retained;
    bool scaled = false;
    for (std::size_t index = 0; index < arguments.size(); index += 2) {
        const auto& key = arguments[index];
        const auto& value = arguments[index + 1];
        if (key == "--xr") {
            if (value != "on" && value != "off") { throw std::invalid_argument("XR requires on or off"); }
            result.enabled = value == "on";
        } else if (key == "--xr-world-scale") {
            result.world_scale = number(value);
            scaled = true;
        } else if (key == "--xr-render-scale") {
            result.render_scale = number(value);
            scaled = true;
        } else if (key == "--xr-loader") {
            if (value.empty()) { throw std::invalid_argument("XR loader path must be nonempty"); }
            result.loader = value;
            scaled = true;
        } else {
            retained.push_back(key);
            retained.push_back(value);
        }
    }
    if (result.render_scale < 0.1 || result.render_scale > 1) { throw std::invalid_argument("XR render scale must be between 0.1 and 1"); }
    if (result.enabled && result.world_scale == 0) { throw std::invalid_argument("XR requires an explicit --xr-world-scale in map cells per metre"); }
    if (scaled && !result.enabled) { throw std::invalid_argument("XR scale options require --xr on"); }
    arguments = std::move(retained);
    return result;
}
}
