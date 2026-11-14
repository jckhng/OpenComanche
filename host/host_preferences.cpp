#include "host_preferences.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace comanchecd::host {
namespace {
constexpr const char* preferences_file = "host-preferences.cfg";
constexpr const char* preferences_backup = "host-preferences.cfg.bak";
constexpr const char* preferences_version = "1";

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) { return {}; }
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { throw std::runtime_error("Cannot read host preferences (" + path.string() + ")"); }
    const std::string result{std::istreambuf_iterator<char>(input), {}};
    if (input.bad()) { throw std::runtime_error("Cannot finish reading host preferences"); }
    return result;
}

std::string mapping_value(const std::string& value) {
    if (value.empty() || value.front() != '"') { return value; }
    std::string result;
    for (std::size_t index = 1; index < value.size(); ++index) {
        auto code = value[index];
        if (code == '"') {
            if (index + 1 != value.size()) { throw std::invalid_argument("Unexpected text after quoted mapping path"); }
            return result;
        }
        if (code == '\\') {
            if (++index == value.size()) { break; }
            code = value[index];
            switch (code) {
            case 'n': code = '\n'; break;
            case 'r': code = '\r'; break;
            case 't': code = '\t'; break;
            case '\\': case '"': break;
            default: throw std::invalid_argument("Invalid mapping path escape");
            }
        }
        result.push_back(code);
    }
    throw std::invalid_argument("Unterminated quoted mapping path");
}

std::string quote_mapping(const std::string& path) {
    std::string result = "\"";
    for (const auto code : path) {
        switch (code) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result.push_back(code); break;
        }
    }
    return result + '"';
}

SdlHostOptions parse_preferences(const std::string& text, const std::filesystem::path& directory) {
    SdlHostOptions options;
    options.input = InputProfile::enhanced;
    std::istringstream input(text);
    std::string line;
    bool version_seen = false;
    unsigned number = 0;
    while (std::getline(input, line)) {
        ++number;
        line = trim(line);
        if (line.empty() || line.front() == '#') { continue; }
        const auto equal = line.find('=');
        if (equal == std::string::npos) { throw std::invalid_argument("Missing '=' in host preferences at line " + std::to_string(number)); }
        const auto key = trim(line.substr(0, equal));
        auto value = trim(line.substr(equal + 1));
        if (key == "version") {
            if (version_seen || value != preferences_version) { throw std::invalid_argument("Unsupported or duplicate host preferences version"); }
            version_seen = true;
            continue;
        }
        if (key == "mode") { throw std::invalid_argument("Select the input mode on the command line"); }
        try {
            if (key == "controller-mappings") { value = mapping_value(value); }
            options = key.compare(0, 5, "bind.") == 0 ?
                parse_sdl_options({"--bind", key.substr(5) + "=" + value}, options) :
                parse_sdl_options({"--" + key, value}, options);
        } catch (const std::invalid_argument& error) {
            throw std::invalid_argument("Host preferences line " + std::to_string(number) + ": " + error.what());
        }
    }
    if (!version_seen) { throw std::invalid_argument("Host preferences require version=1"); }
    if (!options.controller_mappings.empty()) {
        auto path = std::filesystem::path(options.controller_mappings);
        if (path.is_relative()) { path = directory / path; }
        options.controller_mappings = std::filesystem::absolute(path).lexically_normal().string();
    }
    return options;
}

std::string serialize(const SdlHostOptions& options) {
    if (options.input != InputProfile::enhanced) { throw std::invalid_argument("Host preferences require enhanced mode"); }
    validate_controller_tuning(options.controller);
    validate_controller_bindings(options.bindings);
    std::ostringstream output;
    output << "version=" << preferences_version << '\n'
        << "stick-deadzone=" << options.controller.deadzone << '\n'
        << "stick-sensitivity=" << options.controller.sensitivity << '\n'
        << "invert-y=" << (options.controller.pitch == PitchDirection::inverted ? "on" : "off") << '\n';
    if (!options.controller_mappings.empty()) {
        const auto path = std::filesystem::absolute(options.controller_mappings).lexically_normal().string();
        output << "controller-mappings=" << quote_mapping(path) << '\n';
    }
    switch (options.scaling) {
    case ImageScaling::profile_default: case ImageScaling::integer: output << "scale=integer\n"; break;
    case ImageScaling::nearest: output << "scale=nearest\n"; break;
    case ImageScaling::linear: output << "scale=linear\n"; break;
    default: throw std::invalid_argument("Invalid host scaling preference");
    }
    if (options.scene_rendering != SceneRendering::native && options.scene_rendering != SceneRendering::spatial) {
        throw std::invalid_argument("Invalid host scene renderer preference");
    }
    if (options.interpolation != CameraInterpolation::enabled && options.interpolation != CameraInterpolation::disabled) {
        throw std::invalid_argument("Invalid host interpolation preference");
    }
    if (options.cockpit_scaling != CockpitScaling::nearest && options.cockpit_scaling != CockpitScaling::sharp) {
        throw std::invalid_argument("Invalid cockpit scaling preference");
    }
    if (options.cockpit != CockpitPresentation::flat && options.cockpit != CockpitPresentation::spatial) {
        throw std::invalid_argument("Invalid cockpit presentation preference");
    }
    output << "scene-renderer=" << (options.scene_rendering == SceneRendering::native ? "native" : "spatial") << '\n'
        << "interpolation=" << (options.interpolation == CameraInterpolation::enabled ? "on" : "off") << '\n'
        << "cockpit-scale=" << (options.cockpit_scaling == CockpitScaling::sharp ? "sharp" : "nearest") << '\n'
        << "cockpit=" << (options.cockpit == CockpitPresentation::spatial ? "spatial" : "flat") << '\n'
        << "steer-x=" << axis_name(options.bindings.horizontal) << '\n'
        << "steer-y=" << axis_name(options.bindings.vertical) << '\n';
    for (std::size_t button = 0; button < controller_button_count; ++button) {
        output << "bind.menu." << button_name(button) << '=' << action_name(options.bindings.menu[button]) << '\n'
            << "bind.flight." << button_name(button) << '=' << action_name(options.bindings.flight[button]) << '\n';
    }
    output << "bind.flight.lefttrigger=" << action_name(options.bindings.triggers[0]) << '\n'
        << "bind.flight.righttrigger=" << action_name(options.bindings.triggers[1]) << '\n';
    return output.str();
}

// A private sibling directory keeps both staged files on the destination filesystem.
class PendingPreferences {
public:
    explicit PendingPreferences(const std::filesystem::path& parent) {
        static std::atomic<unsigned> counter{0};
        constexpr unsigned maximum_attempts = 32;
        for (unsigned attempt = 0; attempt < maximum_attempts; ++attempt) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            directory_ = parent / (std::string(preferences_file) + ".pending-" + std::to_string(stamp) + "-" + std::to_string(counter++));
            if (std::filesystem::create_directory(directory_)) { return; }
        }
        throw std::runtime_error("Cannot create a unique host preferences staging directory");
    }
    ~PendingPreferences() {
        std::error_code ignored;
        std::filesystem::remove(file(), ignored);
        std::filesystem::remove(backup(), ignored);
        std::filesystem::remove(directory_, ignored);
    }
    std::filesystem::path file() const { return directory_ / "preferences"; }
    std::filesystem::path backup() const { return directory_ / "backup"; }
private:
    std::filesystem::path directory_;
};
}

HostPreferences::HostPreferences(std::filesystem::path directory) : directory_(std::move(directory)) {}

SdlHostOptions HostPreferences::read() const {
    const auto path = directory_ / preferences_file;
    if (std::filesystem::exists(path)) { return parse_preferences(read_text(path), directory_); }
    SdlHostOptions options;
    options.input = InputProfile::enhanced;
    return options;
}

SdlHostOptions HostPreferences::resolve(const std::vector<std::string>& arguments) const {
    auto options = parse_sdl_options(arguments);
    if (options.input == InputProfile::enhanced) { options = parse_sdl_options(arguments, read()); }
    return options;
}

void HostPreferences::write(const SdlHostOptions& options) const {
    const auto text = serialize(options);
    const auto path = directory_ / preferences_file;
    if (std::filesystem::exists(path) && serialize(read()) == text) { return; }
    std::filesystem::create_directories(directory_);
    const PendingPreferences pending(directory_);
    std::ofstream output(pending.file(), std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if (!output) { throw std::runtime_error("Cannot save host preferences"); }
    if (std::filesystem::exists(path)) {
        std::filesystem::copy_file(path, pending.backup());
        std::filesystem::rename(pending.backup(), directory_ / preferences_backup);
    }
    std::filesystem::rename(pending.file(), path);
}
}
