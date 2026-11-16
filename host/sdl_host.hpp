#pragma once

#include "flight_host.hpp"
#include "controller_response.hpp"
#include "controller_bindings.hpp"
#include "sdl_presenter.hpp"
#include "cockpit_scaling.hpp"
#include "spatial_cockpit.hpp"
#include "display_settings.hpp"
#include "controller_configuration.hpp"
#include "audio_volume.hpp"
#include <functional>

namespace comanchecd::host {
enum class SceneRendering { spatial, native };
enum class CameraInterpolation { enabled, disabled };
enum class HostFocus { active, inactive };

struct SdlHostOptions {
    InputProfile input = InputProfile::faithful;
    std::string controller_mappings;
    ControllerTuning controller;
    ImageScaling scaling = ImageScaling::profile_default;
    SceneRendering scene_rendering = SceneRendering::spatial;
    CameraInterpolation interpolation = CameraInterpolation::enabled;
    ControllerBindings bindings;
    CockpitScaling cockpit_scaling = CockpitScaling::sharp;
    CockpitPresentation cockpit = CockpitPresentation::spatial;
    DisplayPacing pacing = DisplayPacing::vertical_sync;
    DesktopRenderer renderer = DesktopRenderer::automatic;
    ViewAspect aspect = ViewAspect::profile_default;
    FrameLimit frame_limit;
    WindowPlacement window;
    ControllerCalibration calibration;
    ButtonPrompts prompts = ButtonPrompts::automatic;
    VolumeLevels volume;
};

class SdlFlightHost final : public FlightHost {
public:
    using PreferencesWriter = std::function<void(const SdlHostOptions&)>;
    explicit SdlFlightHost(SdlHostOptions options = {}, PreferencesWriter save = {});
    ~SdlFlightHost() override;
    HostInput poll() override;
    PresentationDetail presentation_detail() const override;
    AudioOutputTransform audio_output_transform() const override;
    void present_snapshot(std::shared_ptr<const SessionPresentation> frame, const std::string& status) override;
    void set_input_context(InputContext context) override;
    void present(const CockpitImage& image, const FlightState& state, const std::string& status) override;
    void queue_audio(const std::vector<std::int16_t>& samples) override;
    std::size_t queued_audio_samples() const override;
    void clear_audio() override;
    void set_playback(Playback playback) override;
    void idle() override;
    void reset_controls() override;
    void set_focus(HostFocus focus);

private:
    void update_title(const FlightState& state, const std::string& status);
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace comanchecd::host
