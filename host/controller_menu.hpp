#pragma once

#include "controller_configuration.hpp"
#include "host_ui.hpp"

namespace comanchecd::host {

class ControllerMenu {
  public:
    explicit ControllerMenu(ControllerConfiguration settings);
    void update(const ControllerDevice& device, double seconds);
    void command(ControllerMenuCommand command);
    void suspend();
    void capture(std::size_t control);
    bool capturing() const;
    bool close_requested() const;
    bool take_apply_request();
    void save_result(const std::string& error);
    const ControllerConfiguration& settings() const;
    CockpitImage image() const;

  private:
    enum class Page { overview, menu_bindings, flight_bindings, calibration };
    enum class Row {
        menu_bindings,
        flight_bindings,
        calibration,
        horizontal,
        vertical,
        deadzone,
        sensitivity,
        pitch,
        prompts,
        defaults,
        apply,
        close,
        count
    };
    enum class CalibrationStage { ready, centering, range, finished };
    void change(int direction);
    void accept();
    void finish_calibration();
    std::vector<HostAction> actions() const;
    ControllerConfiguration settings_;
    ControllerDevice device_;
    Page page_ = Page::overview;
    unsigned row_ = 0, binding_row_ = 0;
    bool capturing_ = false, capture_armed_ = false, close_ = false, apply_ = false;
    std::string message_;
    double seconds_ = 0, center_started_ = 0, capture_started_ = 0;
    CalibrationStage calibration_stage_ = CalibrationStage::ready;
    unsigned center_samples_ = 0;
    std::array<std::int64_t, controller_stick_axes> centers_{};
    std::array<int, controller_stick_axes> noise_min_{}, noise_max_{}, minimum_{}, maximum_{};
    ControllerCalibration candidate_;
};
} // namespace comanchecd::host
