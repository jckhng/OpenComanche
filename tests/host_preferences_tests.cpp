#include "host_preferences.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}

class TemporaryProfile {
public:
    TemporaryProfile() : path(std::filesystem::temp_directory_path() /
        ("opencommanche-preferences-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        require(std::filesystem::create_directory(path), "Preference fixture must own a fresh directory");
    }
    ~TemporaryProfile() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    const std::filesystem::path path;
};

std::string text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "Preference fixture must open");
    return {std::istreambuf_iterator<char>(input), {}};
}

void write(const std::filesystem::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << value;
    output.close();
    require(bool(output), "Preference fixture must save");
}
}

int main() {
    try {
        using namespace comanchecd::host;
        const TemporaryProfile fixture;
        const auto directory = fixture.path / "profile";
        const HostPreferences preferences(directory);
        require(preferences.resolve({}).input == InputProfile::faithful && !std::filesystem::exists(directory),
                "Faithful option resolution must preserve its read-only default entry");
        const auto defaults = preferences.resolve({"--mode", "enhanced"});
        require(defaults.input == InputProfile::enhanced && !std::filesystem::exists(directory),
                "An absent preference file must supply the enhanced defaults");
        const auto configured = preferences.resolve({"--mode", "enhanced", "--bind", "flight.a=target",
            "--bind", "flight.x=fire", "--bind", "flight.lefttrigger=settings", "--bind", "menu.b=accept",
            "--steer-x", "rightx", "--steer-y", "righty", "--stick-deadzone", "4000",
            "--stick-sensitivity", "150", "--invert-y", "on", "--scale", "linear",
            "--scene-renderer", "native", "--interpolation", "off"});
        preferences.write(configured);
        const auto path = directory / "host-preferences.cfg";
        const auto original = text(path);
        for (const char* name : {"COMANCHE.NAM", "COMANCHE.MNU", "COMANCHE.CFG"}) { write(directory / name, name); }
        const HostPreferences restarted(directory);
        const auto loaded = restarted.resolve({"--mode", "enhanced"});
        require(loaded.bindings.menu == configured.bindings.menu && loaded.bindings.flight == configured.bindings.flight &&
                loaded.bindings.triggers == configured.bindings.triggers && loaded.bindings.horizontal == ControllerAxis::rightx &&
                loaded.bindings.vertical == ControllerAxis::righty && loaded.controller.deadzone == 4000 &&
                loaded.controller.sensitivity == 150 && loaded.controller.pitch == PitchDirection::inverted &&
                loaded.scaling == ImageScaling::linear && loaded.scene_rendering == SceneRendering::native &&
                loaded.interpolation == CameraInterpolation::disabled,
                "Restart must restore bindings, axes, tuning and presentation preferences");
        require(restarted.resolve({}).input == InputProfile::faithful &&
                restarted.resolve({}).controller.deadzone == ControllerTuning{}.deadzone,
                "Enhanced preferences must retain the explicit enhanced launch boundary");
        auto overridden = restarted.resolve({"--mode", "enhanced", "--stick-sensitivity", "120", "--bind", "flight.a=none"});
        require(overridden.controller.sensitivity == 120 && overridden.controller.deadzone == 4000 &&
                overridden.bindings.flight[static_cast<std::size_t>(ControllerButton::a)] == HostAction::none &&
                overridden.bindings.flight[static_cast<std::size_t>(ControllerButton::x)] == HostAction::fire,
                "CLI overrides must merge with saved bindings and support unbinding");
        preferences.write(overridden);
        require(text(directory / "host-preferences.cfg.bak") == original, "Replacing preferences must retain the prior complete file");
        const auto replacement = text(path);
        preferences.write(preferences.read());
        require(text(path) == replacement && text(directory / "host-preferences.cfg.bak") == original,
                "An unchanged preference launch must preserve its existing backup");

        for (const auto& arguments : std::vector<std::vector<std::string>>{
                {"--bind", "flight.a=fire"}, {"--mode", "enhanced", "--bind", "a=fire"},
                {"--mode", "enhanced", "--bind", "flight.a=accept"}, {"--mode", "enhanced", "--bind", "menu.a=fire"},
                {"--mode", "enhanced", "--bind", "menu.lefttrigger=accept"}, {"--mode", "enhanced", "--bind", "flight.missing=fire"},
                {"--mode", "enhanced", "--bind", "flight.a=missing"}, {"--mode", "enhanced", "--steer-x", "triggerleft"}}) {
            bool rejected = false;
            try { preferences.resolve(arguments); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && text(path) == replacement, "Invalid bindings must preserve the saved preferences");
        }
        for (const std::string invalid : {"version=2\n", "stick-deadzone=4\n", "version=1\nunknown=1\n",
                "version=1\nmode=enhanced\n", "version=1\nstick-deadzone=-1\n", "version=1\nbind.flight.a=up\n",
                "version=1\ncontroller-mappings=\"open\n", "version=1\ncontroller-mappings=\"file\" extra\n"}) {
            write(path, invalid);
            bool rejected = false;
            try { preferences.resolve({"--mode", "enhanced"}); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && text(path) == invalid && preferences.resolve({}).input == InputProfile::faithful,
                    "Malformed enhanced preferences must remain intact and preserve faithful startup");
        }
        const std::string manual = "# Personal controls\r\nversion = 1\r\ncontroller-mappings = maps/gamepad.txt\r\nbind.flight.a = fire\r\n";
        write(path, manual);
        const auto relative = preferences.read();
        require(relative.controller_mappings == std::filesystem::absolute(directory / "maps/gamepad.txt").lexically_normal().string(),
                "Mapping paths in preferences must resolve relative to the native profile");
        preferences.write(relative);
        require(text(path) == manual, "Equivalent manual preferences must preserve comments and line endings");
        auto quoted = relative;
        quoted.controller_mappings = std::filesystem::absolute(directory / "maps folder/gamepad .txt").lexically_normal().string();
        preferences.write(quoted);
        require(preferences.read().controller_mappings == quoted.controller_mappings,
                "Quoted mapping paths must preserve spaces and native path separators");
#ifndef _WIN32
        quoted.controller_mappings = std::filesystem::absolute(directory / "mapping \"quoted\"\nfile ").string();
        preferences.write(quoted);
        require(preferences.read().controller_mappings == quoted.controller_mappings,
                "Quoted POSIX mapping paths must preserve quotes, line breaks and trailing spaces");
#endif
        const auto cleared = preferences.resolve({"--mode", "enhanced", "--controller-mappings", ""});
        require(cleared.controller_mappings.empty(), "An empty mapping override must restore the built-in SDL2 mappings");
        preferences.write(cleared);

        const auto retained = text(path);
        const auto backup = directory / "host-preferences.cfg.bak";
        std::filesystem::remove(backup);
        std::filesystem::create_directory(backup);
        bool failed = false;
        try { preferences.write(configured); } catch (const std::filesystem::filesystem_error&) { failed = true; }
        require(failed && text(path) == retained && preferences.read().input == InputProfile::enhanced,
                "A failed staged save must retain a complete readable preference file");
        std::filesystem::remove(backup);
        preferences.write(configured);
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            require(entry.path().filename().string().find(".pending-") == std::string::npos,
                    "Completed and failed saves must release their private staging files");
        }
        for (const char* name : {"COMANCHE.NAM", "COMANCHE.MNU", "COMANCHE.CFG"}) {
            require(text(directory / name) == name, "Host preferences must preserve the original-format profile records");
        }
        std::cout << "Controller bindings, preference restart, overrides and staged saves passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
