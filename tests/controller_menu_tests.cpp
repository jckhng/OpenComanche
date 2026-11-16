#include "controller_menu.hpp"
#include "sdl_options.hpp"
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
        const AxisCalibration calibrated{-28000, 1700, 30000};
        require(calibrated_axis(-28000, calibrated) == -32768 && calibrated_axis(1700, calibrated) == 0 &&
                    calibrated_axis(30000, calibrated) == 32767 &&
                    calibrated_axis(32767, calibrated) == 32767,
                "Calibration must remove center bias, normalize both extents and clamp overtravel");
        for (int value = -32768; value <= 32767; value += 127) {
            require(calibrated_axis(static_cast<std::int16_t>(value), {}) == value,
                    "Default calibration must preserve every sampled SDL value exactly");
        }
        auto options = parse_sdl_options({"--mode", "enhanced", "--axis-calibration",
                                          "leftx=-28000,1700,30000", "--button-prompts", "on"});
        require(options.calibration.axes[0].center == 1700 && options.prompts == ButtonPrompts::enabled,
                "Calibration and prompt preferences must share the host option validator");
        for (const std::string invalid : {"leftx=0,0,32767", "none=-1,0,1", "lefty=-32769,0,32767",
                                          "leftx=-32768,0,32768", "rightx=-1,0,1,2", "leftx=-1,no,1"}) {
            bool rejected = false;
            try {
                set_axis_calibration(options.calibration, invalid);
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected && options.calibration.axes[0].center == 1700,
                    "Invalid calibration must preserve the previous valid configuration");
        }
        const auto prompts = button_prompt_image("A FIRE", "RT FIRE  CTRL+F3 SETUP");
        auto original = ui_image();
        original.pixels.fill(3);
        const auto decorated = with_button_prompts(original, prompts);
        require(
            decorated.palette == original.palette &&
                std::equal(decorated.pixels.begin() + ButtonPromptImage::width * ButtonPromptImage::height,
                           decorated.pixels.end(),
                           original.pixels.begin() + ButtonPromptImage::width * ButtonPromptImage::height),
            "Native button hints must preserve the source palette and pixels beyond the prompt strip");
        require(bound_controls(options.bindings, InputContext::flight, HostAction::fire) == "A/RT",
                "Button prompts must resolve every configured owner for a displayed action");
        ControllerDevice device;
        device.instance = 1;
        device.name = "Synthetic controller";
        ControllerMenu menu({});
        menu.update(device, 0);
        menu.command(ControllerMenuCommand::accept);
        menu.command(ControllerMenuCommand::accept);
        menu.update(device, 0.1);
        menu.capture(static_cast<std::size_t>(ControllerButton::x));
        require(menu.settings().bindings.menu[static_cast<std::size_t>(ControllerButton::x)] ==
                    HostAction::accept,
                "Menu binding capture must assign a fresh physical button");
        menu.command(ControllerMenuCommand::clear);
        require(menu.settings().bindings.menu[static_cast<std::size_t>(ControllerButton::x)] ==
                        HostAction::none &&
                    menu.settings().bindings.menu[static_cast<std::size_t>(ControllerButton::a)] ==
                        HostAction::none,
                "Clear must remove all bindings for the selected action");
        menu.command(ControllerMenuCommand::back);
        menu.command(ControllerMenuCommand::down);
        menu.command(ControllerMenuCommand::accept);
        menu.command(ControllerMenuCommand::accept);
        menu.update(device, 0.2);
        menu.capture(controller_button_count + 1);
        require(menu.settings().bindings.triggers[1] == HostAction::fire,
                "Flight actions must support trigger capture");
        menu.command(ControllerMenuCommand::back);
        menu.command(ControllerMenuCommand::down);
        menu.command(ControllerMenuCommand::accept);
        menu.command(ControllerMenuCommand::accept);
        device.axes[0] = 1700;
        device.axes[1] = -900;
        for (unsigned sample = 0; sample < 20; ++sample) {
            menu.update(device, 0.3 + sample * 0.1);
        }
        device.axes[0] = -28000;
        device.axes[1] = -30000;
        menu.update(device, 2.4);
        device.axes[0] = 30000;
        device.axes[1] = 29000;
        menu.update(device, 2.5);
        menu.command(ControllerMenuCommand::accept);
        require(menu.settings().calibration.axes[0].center == 1700 &&
                    menu.settings().calibration.axes[1].center == -900,
                "Center and range sampling must retain independent physical-axis calibration");
        require(menu.settings().tuning.deadzone >= 1024 && menu.settings().tuning.deadzone < 8000,
                "Quiet calibration must derive a bounded noise margin");
        require(menu.image().pixels != comanchecd::CockpitImage{}.pixels,
                "The controller page must produce a visible host-owned image");
        device.instance = -1;
        menu.update(device, 3);
        menu.command(ControllerMenuCommand::back);
        require(!menu.close_requested(), "Back from calibration must return to the overview");
        menu.command(ControllerMenuCommand::back);
        require(menu.close_requested(), "Back from overview must request a safe close");
        ControllerMenu guarded({});
        device.instance = 2;
        guarded.update(device, 0);
        guarded.command(ControllerMenuCommand::accept);
        guarded.command(ControllerMenuCommand::accept);
        device.buttons[0] = true;
        guarded.update(device, 0.1);
        guarded.capture(2);
        require(guarded.settings().bindings.menu[2] == HostAction::none,
                "A held Accept must leave binding capture disarmed");
        device.buttons[0] = false;
        guarded.update(device, 0.2);
        guarded.suspend();
        require(!guarded.capturing(), "Focus loss must cancel capture without changing bindings");
        guarded.command(ControllerMenuCommand::accept);
        guarded.update(device, 0.3);
        device.instance = 3;
        guarded.update(device, 0.4);
        require(!guarded.capturing(), "A replacement controller must cancel the old capture");
        guarded.command(ControllerMenuCommand::accept);
        guarded.update(device, 9);
        require(!guarded.capturing(), "Binding capture must leave its waiting state on timeout");
        ControllerConfiguration inverted;
        inverted.tuning.pitch = PitchDirection::inverted;
        ControllerMenu feedback(inverted);
        device.axes = {0, 32767, 0, 0};
        feedback.update(device, 0);
        const auto actual = feedback.image();
        auto expected = ui_image();
        ui_text(expected, 16, 194, "LIVE STEER X 0%  Y -100%");
        require(std::equal(actual.pixels.begin()+194*comanchecd::cockpit_width,
                    actual.pixels.begin()+201*comanchecd::cockpit_width,
                    expected.pixels.begin()+194*comanchecd::cockpit_width),
                "Live steering feedback must include the configured pitch inversion");
        std::cout << "Controller editor, calibration and capture passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
