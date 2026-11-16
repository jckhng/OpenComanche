#pragma once

#include "audio_volume.hpp"
#include "host_ui.hpp"

namespace comanchecd::host {
class AudioMenu {
public:
    explicit AudioMenu(VolumeLevels levels);
    void command(ControllerMenuCommand command);
    bool close_requested() const;
    bool take_apply_request();
    void save_result(const std::string& error);
    const VolumeLevels& levels() const;
    CockpitImage image() const;

private:
    enum class Row { master, music, effects, apply, defaults, close, count };
    unsigned* selected_volume();
    VolumeLevels levels_;
    Row row_ = Row::master;
    bool close_ = false, apply_ = false;
    std::string message_ = "X / DEL MUTES THE SELECTED CHANNEL";
};
} // namespace comanchecd::host
