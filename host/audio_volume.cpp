#include "audio_volume.hpp"
#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>

namespace comanchecd::host {
namespace {
std::uint32_t encode(VolumeLevels levels) {
    return levels.master | (levels.music << 8) | (levels.effects << 16);
}

VolumeLevels decode(std::uint32_t value) {
    return {value & 255u, (value >> 8) & 255u, (value >> 16) & 255u};
}

std::int16_t clip(std::int64_t value) {
    constexpr std::int64_t unit = 1 << AudioOutputFrame::fractional_bits;
    auto pcm = value / unit;
    if (value < 0 && value % unit != 0) {
        --pcm;
    }
    return static_cast<std::int16_t>(std::clamp<std::int64_t>(pcm, std::numeric_limits<std::int16_t>::min(),
                                                              std::numeric_limits<std::int16_t>::max()));
}
} // namespace

void validate_volume(const VolumeLevels& levels) {
    if (levels.master > maximum_volume || levels.music > maximum_volume || levels.effects > maximum_volume) {
        throw std::invalid_argument("Master, music and effects volume must be 0..100 percent");
    }
}

class AudioVolume::State {
  public:
    std::atomic<std::uint32_t> target{encode({})};
};

AudioVolume::AudioVolume(VolumeLevels levels) : state_(std::make_shared<State>()) {
    set(levels);
}

void AudioVolume::set(VolumeLevels levels) {
    validate_volume(levels);
    state_->target.store(encode(levels), std::memory_order_relaxed);
}

VolumeLevels AudioVolume::levels() const {
    return decode(state_->target.load(std::memory_order_relaxed));
}

AudioOutputTransform AudioVolume::transform() const {
    return [state = state_](const AudioOutputFrame& frame) {
        const auto levels = decode(state->target.load(std::memory_order_relaxed));
        if (levels.master == 0) {
            return std::array<std::int16_t, 2>{};
        }
        auto output = frame.mixed;
        if (levels.music != maximum_volume || levels.effects != maximum_volume) {
            for (unsigned channel = 0; channel < output.size(); ++channel) {
                const auto balanced =
                    (frame.fm[channel] * levels.music + frame.digital[channel] * levels.effects) /
                    maximum_volume;
                output[channel] = clip(balanced);
            }
        }
        if (levels.master != maximum_volume) {
            for (auto& sample : output) {
                sample = static_cast<std::int16_t>(std::int64_t(sample) * levels.master / maximum_volume);
            }
        }
        return output;
    };
}
} // namespace comanchecd::host
