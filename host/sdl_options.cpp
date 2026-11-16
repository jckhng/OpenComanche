#include "sdl_options.hpp"
#include <stdexcept>

namespace comanchecd::host {
namespace {
int integer_option(const std::string& value) {
    if (value.empty() || value.size() > 5 || value.find_first_not_of("0123456789") != std::string::npos) {
        throw std::invalid_argument("Numeric host options must be decimal integers");
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
        } else if (option == "--volume-master") {
            options.volume.master = integer_option(value);
        } else if (option == "--volume-music") {
            options.volume.music = integer_option(value);
        } else if (option == "--volume-effects") {
            options.volume.effects = integer_option(value);
        } else if (option == "--axis-calibration") {
            set_axis_calibration(options.calibration, value);
        } else if (option == "--button-prompts" && (value == "auto" || value == "on" || value == "off")) {
            options.prompts = value == "auto" ? ButtonPrompts::automatic :
                value == "on" ? ButtonPrompts::enabled : ButtonPrompts::disabled;
        } else if (option == "--fps") {
            options.frame_limit = parse_frame_limit(value);
        } else if (option == "--vsync" && (value == "on" || value == "off")) {
            options.pacing = value == "on" ? DisplayPacing::vertical_sync : DisplayPacing::immediate;
        } else if (option == "--window-size") {
            options.window = parse_window_size(value, options.window);
        } else if (option == "--window-position") {
            options.window.position = parse_window_position(value);
        } else if (option == "--window-mode") {
            options.window.mode = parse_window_mode(value);
        } else if (option == "--renderer" && (value == "auto" || value == "gpu" || value == "cpu")) {
            options.renderer = value == "auto" ? DesktopRenderer::automatic :
                value == "gpu" ? DesktopRenderer::gpu : DesktopRenderer::cpu;
        } else if (option == "--aspect" && (value == "original" || value == "widescreen")) {
            options.aspect = value == "original" ? ViewAspect::original : ViewAspect::widescreen;
        } else if (option == "--scene-renderer" && (value == "native" || value == "spatial")) {
            options.scene_rendering = value == "native" ? SceneRendering::native : SceneRendering::spatial;
        } else if (option == "--interpolation" && (value == "on" || value == "off")) {
            options.interpolation = value == "on" ? CameraInterpolation::enabled : CameraInterpolation::disabled;
        } else if (option == "--cockpit-scale" && (value == "nearest" || value == "sharp")) {
            options.cockpit_scaling = value == "nearest" ? CockpitScaling::nearest : CockpitScaling::sharp;
        } else if (option == "--cockpit" && (value == "flat" || value == "spatial")) {
            options.cockpit = value == "flat" ? CockpitPresentation::flat : CockpitPresentation::spatial;
        } else if (option == "--scale" && (value == "nearest" || value == "linear" || value == "integer")) {
            options.scaling = value == "nearest" ? ImageScaling::nearest :
                value == "linear" ? ImageScaling::linear : ImageScaling::integer;
        } else {
            throw std::invalid_argument("Unknown host option or unsupported value (" + option + ")");
        }
    }
    validate_volume(options.volume);
    validate_frame_limit(options.frame_limit);
    validate_window_placement(options.window);
    validate_controller_calibration(options.calibration);
    validate_controller_tuning(options.controller);
    validate_controller_bindings(options.bindings);
    if (enhanced_options && options.input != InputProfile::enhanced) {
        throw std::invalid_argument("Enhanced host options require --mode enhanced");
    }
    return options;
}
}
