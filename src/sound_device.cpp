#include "comanchecd/sound_device.hpp"
#include "comanchecd/audio.hpp"
#include "dbopl.h"

#include <algorithm>
#include <deque>

namespace comanchecd {
namespace {
constexpr unsigned mixer_volume_shift = 13;
constexpr std::int32_t fm_volume = (1 << mixer_volume_shift) * 3 / 2;
constexpr bool opl3_capable = true;
}

class SoundDevice::Impl {
public:
    Impl(unsigned rate, AudioOutputTransform output) : digital_(rate), fm_(opl3_capable), output_(std::move(output)) {
        static const bool tables_ready = [] {
            DBOPL::InitTables();
            return true;
        }();
        (void)tables_ready;
        fm_.Setup(rate);
    }

    void write_fm(std::uint8_t reg, std::uint8_t value) { fm_.WriteReg(reg, value); }

    void queue_digital(std::uint8_t sample) {
        const auto block = digital_.render_accumulator({sample});
        samples_.insert(samples_.end(), block.begin(), block.end());
    }

    std::int16_t render_sample() {
        std::int32_t fm;
        fm_.GenerateBlock2(1, &fm);
        const auto fm_value = std::int64_t(fm) * fm_volume;
        std::int64_t digital = 0;
        if (!samples_.empty()) {
            digital = samples_.front();
            samples_.pop_front();
        }
        // Both channels retain fractional gain/filter bits until final clipping.
        const auto mixed = static_cast<std::int16_t>(std::clamp<std::int64_t>((fm_value + digital) >> mixer_volume_shift, -32768, 32767));
        if (output_) { return output_({{fm_value, fm_value}, {digital, digital}, {mixed, mixed}})[0]; }
        return mixed;
    }

private:
    Sb16Playback digital_;
    DBOPL::Chip fm_;
    std::deque<std::int32_t> samples_;
    AudioOutputTransform output_;
};

SoundDevice::SoundDevice(unsigned rate, AudioOutputTransform output) : impl_(std::make_unique<Impl>(rate, std::move(output))) {}
SoundDevice::~SoundDevice() = default;
void SoundDevice::write_fm(std::uint8_t reg, std::uint8_t value) { impl_->write_fm(reg, value); }
void SoundDevice::queue_digital(std::uint8_t sample) { impl_->queue_digital(sample); }
std::int16_t SoundDevice::render_sample() { return impl_->render_sample(); }
} // namespace comanchecd
