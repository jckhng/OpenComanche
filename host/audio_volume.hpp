#pragma once

#include "comanchecd/audio_output.hpp"
#include <memory>

namespace comanchecd::host {
constexpr unsigned maximum_volume = 100;

struct VolumeLevels {
    unsigned master = maximum_volume, music = maximum_volume, effects = maximum_volume;
};

void validate_volume(const VolumeLevels& levels);

class AudioVolume {
public:
    explicit AudioVolume(VolumeLevels levels = {});
    void set(VolumeLevels levels);
    VolumeLevels levels() const;
    AudioOutputTransform transform() const;

private:
    class State;
    std::shared_ptr<State> state_;
};
} // namespace comanchecd::host
