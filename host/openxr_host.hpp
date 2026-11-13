#pragma once

#include "sdl_host.hpp"
#include "openxr_options.hpp"

namespace comanchecd::host {
// The desktop SDL2 adapter retains input/audio ownership and original tick pacing.
class OpenXrHost final : public FlightHost {
public:
    OpenXrHost(SdlHostOptions options, OpenXrOptions xr);
    ~OpenXrHost() override;
    HostInput poll() override;
    PresentationDetail presentation_detail() const override;
    void present_snapshot(std::shared_ptr<const SessionPresentation> frame, const std::string& status) override;
    void present(const CockpitImage& image, const FlightState& state, const std::string& status) override;
    void set_input_context(InputContext context) override;
    void queue_audio(const std::vector<std::int16_t>& samples) override;
    std::size_t queued_audio_samples() const override;
    void clear_audio() override;
    void set_playback(Playback playback) override;
    void idle() override;
    void reset_controls() override;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
