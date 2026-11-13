#include "sdl_options.hpp"
#include <stdexcept>

namespace comanchecd::host {
namespace {
int integer_option(const std::string& value) {
    if (value.empty() || value.size() > 5 || value.find_first_not_of("0123456789") != std::string::npos) {
        throw std::invalid_argument("Controller tuning values must be decimal integers");
    }
    return std::stoi(value);
}
}

SdlHostOptions parse_sdl_options(const std::vector<std::string>& arguments, SdlHostOptions options) {
    if (arguments.size() % 2 != 0) { throw std::invalid_argument("Each host option requires a value"); }
    bool enhanced_options = false;
    for (std::size_t index = 0; index < arguments.size(); index += 2) {
        const auto& option = arguments[index];
        const auto& value = arguments[index + 1];
        if (option == "--mode" && (value == "faithful" || value == "enhanced")) {
            options.input = value == "faithful" ? InputProfile::faithful : InputProfile::enhanced;
            continue;
        }
        enhanced_options = true;
        if (option == "--controller-mappings") {
            options.controller_mappings = value;
        } else if (option == "--stick-deadzone") {
            options.controller.deadzone = integer_option(value);
        } else if (option == "--bind") {
            bind_controller(options.bindings, value);
        } else if (option == "--steer-x") {
            options.bindings.horizontal = controller_axis(value);
        } else if (option == "--steer-y") {
            options.bindings.vertical = controller_axis(value);
        } else if (option == "--stick-sensitivity") {
            options.controller.sensitivity = integer_option(value);
        } else if (option == "--invert-y" && (value == "on" || value == "off")) {
            options.controller.pitch = value == "on" ? PitchDirection::inverted : PitchDirection::normal;
        } else if (option == "--scene-renderer" && (value == "native" || value == "spatial")) {
            options.scene_rendering = value == "native" ? SceneRendering::native : SceneRendering::spatial;
        } else if (option == "--interpolation" && (value == "on" || value == "off")) {
            options.interpolation = value == "on" ? CameraInterpolation::enabled : CameraInterpolation::disabled;
        } else if (option == "--cockpit-scale" && (value == "nearest" || value == "sharp")) {
            options.cockpit_scaling = value == "nearest" ? CockpitScaling::nearest : CockpitScaling::sharp;
        } else if (option == "--scale" && (value == "nearest" || value == "linear" || value == "integer")) {
            options.scaling = value == "nearest" ? ImageScaling::nearest :
                value == "linear" ? ImageScaling::linear : ImageScaling::integer;
        } else {
            throw std::invalid_argument("Unknown host option or unsupported value (" + option + ")");
        }
    }
    validate_controller_tuning(options.controller);
    validate_controller_bindings(options.bindings);
    if (enhanced_options && options.input != InputProfile::enhanced) {
        throw std::invalid_argument("Controller and presentation options require --mode enhanced");
    }
    return options;
}
}
