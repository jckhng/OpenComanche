#include "audio_impl.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace comanchecd {
using namespace audio_detail;

void MissionAudio::Impl::set_music_level(MusicLevel level, MusicChange change) {
    music_muted_ = level == MusicLevel::off;
    if (music_muted_) {
        silence_music();
    } else {
        music_volume_ = level == MusicLevel::loud ? loud_music_volume :
            change == MusicChange::loaded ? loaded_soft_music_volume : soft_music_volume;
    }
}

void MissionAudio::Impl::change_music(MusicPlaylist playlist) {
    const auto looping = playlist.introduction.empty();
    if (looping) {
        playlist.introduction = std::move(playlist.repeat);
        playlist.repeat.clear();
    }
    cold_music_pending_ = false;
    music_cpu_work([&] { start_music(MusicEntry::external, &playlist, looping ? MusicRepeat::loop : MusicRepeat::once); });
}

void MissionAudio::Impl::stop_music() {
    music_cpu_work([&] {
        if (startup_stop_pending_) {
            execute(CpuOperation::instructions, 1); // 58cf CALL.
            startup_stop_pending_ = false;
        }
        instruction([&] { music_active_ = false; });
        execute(CpuOperation::instructions, 2);
        silence_music();
        execute(CpuOperation::instructions, 1);
    });
}

void MissionAudio::Impl::apply_loaded_music_level(MusicLevel level) {
    if (level == MusicLevel::off) {
        music_cpu_work([&] { silence_music(); });
        music_muted_ = true;
    } else {
        set_music_level(level, MusicChange::loaded);
    }
}

void MissionAudio::Impl::initialize_cold_fm() {
    music_work(3); // Detection CALL, port and first timer word.
    write(4, 0x60);
    music_work(1);
    write(4, 0x80);
    const auto initial_status = scheduler_->read_fm_status() & 0xe0;
    music_work(3);
    write(2, 0xff);
    music_work(1);
    write(4, 0x21);
    music_work(1);
    constexpr unsigned detection_wait_rounds = 19;
    for (unsigned round = 0; round < detection_wait_rounds; ++round) {
        music_work(2 + 2 * opl_delay_iterations_);
    }
    const auto overflow_status = (scheduler_->read_fm_status() & 0xe0) ^ 0xc0;
    music_work(4);
    write(4, 0x60);
    music_work(1);
    write(4, 0x80);
    music_work(5);
    if (initial_status != 0 || overflow_status != 0) {
        throw std::runtime_error("Portable OPL timer detection failed");
    }
    music_work(25); // Detection dispatcher return, caller and initializer dispatcher.
    music_work(26); // Fresh-driver selection and hardware initializer CALL.
    music_work(1);
    write(1, 0x20);
    music_work(1);
    write(8, 0);
    music_work(1);
    write(0xbd, 0xc0);
    music_work(3); // Physical reset CALL, count and first voice.
    for (std::size_t physical = 0; physical < carrier.size(); ++physical) {
        music_work(5);
        write(0x40 | carrier[physical], 63);
        music_work(2);
        write(0xb0 | physical, 0);
        music_work(2);
    }
    music_work(3); // Reset RET and logical programming loop setup.
    for (std::size_t channel = 0; channel < carrier.size(); ++channel) {
        music_work(3);
        instruction([&] { frequency_high_[channel] = 0x1f; });
        music_work(3);
        program(channel, 0);
        music_work(4);
    }
    music_work(5); // Hardware result/RET, caller test and silence CALL.
    instruction([&] { music_active_ = false; });
    music_work(2);
    silence_music();
    music_work(1);
    constexpr unsigned channel_initializer_instructions = 11;
    music_work(2 + initialized_music_channels * channel_initializer_instructions + 1);
    instruction([&] { music_callback_installed_ = true; });
    music_work(2);
}

void MissionAudio::Impl::write(std::uint8_t reg, std::uint8_t value) {
    if (scheduler_) {
        scheduler_->write_fm_timed(reg, value, opl_delay_iterations_, [&] {
            if (trace_policy_ == AudioTracePolicy::retain) { writes_.push_back({tick_, reg, value}); }
        });
        return;
    }
    if (trace_policy_ == AudioTracePolicy::retain) {
        writes_.push_back({tick_, reg, value});
    }
    device_.write_fm(reg, value);
}

void MissionAudio::Impl::music_work(unsigned instructions) {
    if (scheduler_ && music_work_depth_ != 0) { scheduler_->execute(CpuOperation::instructions, instructions); }
}

void MissionAudio::Impl::music_cpu_work(const std::function<void()>& action) {
    ++music_work_depth_;
    try {
        action();
    } catch (...) {
        --music_work_depth_;
        throw;
    }
    --music_work_depth_;
}

void MissionAudio::Impl::program(std::size_t channel, std::uint8_t instrument) {
    music_work(4);
    const auto physical = music_channels.at(channel);
    if (physical >= carrier.size()) {
        music_work(1);
        return;
    }
    const auto& data = tables_.instruments.at(instrument);
    const auto voice_operator = carrier[physical];
    const auto modulation_operator = modulator[physical];
    music_work(10);
    write(0xb0 | physical, frequency_high_[physical] & 0xdf);
    music_work(3);
    write(0x20 | voice_operator, data[0]);
    music_work(3);
    write(0x60 | voice_operator, data[2]);
    music_work(3);
    write(0x80 | voice_operator, data[3]);
    music_work(3);
    write(0xe0 | voice_operator, data[4]);
    music_work(3);
    write(0x20 | modulation_operator, data[5]);
    music_work(4);
    write(0x40 | modulation_operator, data[6] ^ 63);
    music_work(3);
    write(0x60 | modulation_operator, data[7]);
    music_work(3);
    write(0x80 | modulation_operator, data[8]);
    music_work(3);
    write(0xe0 | modulation_operator, data[9]);
    music_work(3);
    write(0xc0 | physical, data[10]);
    level_flags_[physical] = data[1];
    music_work(3);
}

void MissionAudio::Impl::select_program(std::size_t channel, std::uint8_t instrument) {
    if (instrument >= tables_.instruments.size()) {
        throw std::invalid_argument("Unsupported music instrument");
    }
    music_work(12);
    programs_[channel] = instrument;
    cached_programs_[channel] = instrument;
    program(channel, instrument);
    envelope_positions_[channel] = 0;
}

void MissionAudio::Impl::note(std::size_t channel, std::uint8_t pitch, std::uint8_t velocity) {
    music_work(music_muted_ ? 3 : 5);
    const auto physical = music_channels.at(channel);
    if (pitch != 0 && !music_muted_) {
        music_work(16);
        const auto instrument = programs_[channel];
        const auto volume = tables_.volume[instrument];
        const auto scale = static_cast<std::uint8_t>(((unsigned(volume) * music_volume_) >> 8) + 1);
        const auto doubled = static_cast<std::uint8_t>(velocity << 1);
        const auto scaled = static_cast<std::uint8_t>((unsigned(scale) * doubled) >> 8);
        envelope_positions_[channel] = 0;
        current_pitch_[channel] = static_cast<std::uint8_t>((pitch << 1) + tables_.pitch[instrument]);
        last_pitch_[channel] = 255;
        music_work(3);
        if (physical < carrier.size()) {
            music_work(8);
            const auto unclamped_level = (scaled >> 2) + 32;
            if (unclamped_level >= 63) { music_work(1); }
            music_work(5);
            const auto level = std::min<unsigned>((scaled >> 2) + 32, 63);
            write(0x40 | carrier[physical], (level_flags_[physical] | level) ^ 63);
            music_work(5);
            frequency_high_[physical] |= 0x20;
            write(0xb0 | physical, frequency_high_[physical]);
            music_work(1);
        }
        music_work(1);
        return;
    }
    music_work(6);
    if (physical < carrier.size()) {
        music_work(9);
        frequency_high_[physical] &= 0xdf;
        write(0xb0 | physical, frequency_high_[physical]);
    }
    music_work(1);
}

void MissionAudio::Impl::silence_music() {
    music_work(2);
    for (std::size_t channel = 0; channel < initialized_music_channels; ++channel) {
        music_work(8);
        pitch_bend_[channel] = 0;
        note(channel, 0, 0);
        music_work(6);
    }
    music_work(1);
}

void MissionAudio::Impl::start_music(MusicEntry entry, MusicPlaylist* incoming,
                 MusicRepeat repeat) {
    const auto& score = incoming ? incoming->introduction : music_;
    if (score.size() < 50 || score[0] != 'M' || score[1] != 'S') {
        throw std::invalid_argument("Expected MS music stream");
    }
    if (incoming) {
        music_work(4);
        instruction([&] { loop_music_ = repeat == MusicRepeat::loop; });
        music_work(5);
    } else {
        music_work(entry == MusicEntry::external ? 10 : 6);
    }
    if (scheduler_ && music_work_depth_ != 0) { scheduler_->execute(CpuOperation::repeat_copy_bytes, 10); }
    music_work(3);
    for (std::size_t channel = 0; channel < initialized_music_channels; ++channel) {
        const auto program = score.at(32 + channel);
        music_work(1);
        instruction([&] { initial_programs_[channel] = program; });
        instruction([&] { cached_programs_[channel] = 255; });
        music_work(3);
    }
    music_work(1);
    // 2f778 publishes the new decoder cursor. Keep the old allocation alive until then.
    instruction([&] {
        if (incoming) {
            music_ = std::move(incoming->introduction);
            next_music_ = std::move(incoming->repeat);
        }
        position_ = 48;
    });
    music_work(1);
    instruction([&] { music_active_ = false; });
    music_work(2);
    silence_music();
    music_work(4);
    wait_ = 1;
    music_active_ = true;
}

std::uint8_t MissionAudio::Impl::byte() {
    if (position_ >= music_.size()) {
        throw std::invalid_argument("Truncated MS music event");
    }
    return music_[position_++];
}

MissionAudio::Impl::MusicEventResult MissionAudio::Impl::event() {
    music_work(5); // CALL 2f794 and stream/end-marker test.
    if (position_ + 2 <= music_.size() && music_[position_] == 0xff && music_[position_ + 1] == 0x2f) {
        music_work(6); // Parser return, caller test, wait reset and silence call.
        silence_music();
        if (!next_music_.empty()) {
            music_work(7);
            music_ = std::move(next_music_);
            next_music_.clear();
            loop_music_ = true;
        } else if (!loop_music_) {
            music_work(7);
            music_active_ = false;
            wait_ = 1;
            return MusicEventResult::stream_end;
        } else {
            music_work(8);
        }
        start_music(MusicEntry::continuation);
        return MusicEventResult::stream_end;
    }
    music_work(8);
    const auto status = byte();
    const auto channel = status & 15;
    const auto command = status & 0xf0;
    if ((command == 0x90 || command == 0x80 || command == 0xc0 || command == 0xe0) &&
            channel >= music_channels.size()) {
        throw std::invalid_argument("Unsupported MS music channel at " + std::to_string(position_ - 1) +
                                    " status=" + std::to_string(status));
    }
    std::uint8_t pitch = 0, velocity = 0;
    if (command == 0x90) {
        music_work(7);
        pitch = byte();
        velocity = byte();
    } else if (command == 0x80) {
        music_work(8);
        pitch = 0;
    } else if (command == 0xc0) {
        music_work(9);
        initial_programs_[channel] = byte();
        select_program(channel, initial_programs_[channel]);
        music_work(7);
    } else if (command == 0xe0) {
        music_work(15);
        pitch_bend_[channel] = byte();
    } else if (command == 0xb0) {
        music_work(16);
        byte();
        byte();
    } else {
        music_work(command == 0xd0 ? 12 : 16);
        // 2f7fb..2f80c skips one data byte for all remaining status classes.
        byte();
    }
    auto delay = unsigned(byte());
    music_work(3);
    if (delay & 128) {
        music_work(7);
        delay = ((delay & 127) << 7) | byte();
    }
    wait_ = static_cast<std::uint16_t>(delay);
    music_work(9); // Parser return and caller event/channel tests.
    if (command != 0x90 && command != 0x80) {
        music_work(2);
        return MusicEventResult::scheduled;
    }
    channel_link(channel, pitch, velocity);
    std::uint16_t visited = std::uint16_t(1u << channel);
    std::size_t linked = channel;
    for (;;) {
        const auto next = linked < initialized_music_channels ? music_.at(16 + linked) : 0;
        music_work(4);
        if (next == 0) {
            music_work(2);
            return MusicEventResult::scheduled;
        }
        music_work(2);
        linked = next - 1;
        if (linked >= music_channels.size() || (visited & (1u << linked))) {
            throw std::invalid_argument("Invalid or cyclic MS linked channel");
        }
        visited |= std::uint16_t(1u << linked);
        channel_link(linked, pitch, velocity);
    }
}

void MissionAudio::Impl::channel_link(std::size_t channel, std::uint8_t pitch, std::uint8_t velocity) {
    music_work(7);
    if (cached_programs_[channel] != initial_programs_[channel]) {
        music_work(4);
        select_program(channel, initial_programs_[channel]);
        music_work(2);
    }
    music_work(4);
    note(channel, pitch, velocity);
    music_work(2);
}

void MissionAudio::Impl::music_tick() {
    music_work(2);
    if (music_active_) {
        music_work(3);
    }
    if (music_active_ && --wait_ == 0) {
        auto event_result = MusicEventResult::scheduled;
        std::size_t budget = music_.size();
        do {
            if (budget-- == 0) {
                throw std::invalid_argument("MS zero-delay event loop exceeds stream bound");
            }
            event_result = event();
        } while (wait_ == 0);
        if (event_result == MusicEventResult::scheduled) { music_work(1); }
    } else if (music_active_) {
        music_work(1);
    }
    music_work(1);
    for (std::size_t reverse = initialized_music_channels; reverse > 0; --reverse) {
        music_work(8);
        const auto channel = reverse - 1;
        auto pitch = static_cast<std::uint8_t>(current_pitch_[channel] + pitch_bend_[channel]);
        const auto& envelope = tables_.envelopes[programs_[channel]];
        auto& position = envelope_positions_[channel];
        if (envelope.empty()) {
            throw std::invalid_argument("Missing instrument envelope");
        }
        if (envelope.at(position) == 0x81) {
            music_work(9);
            position = 0;
        }
        if (envelope.at(position) != 0x80) {
            music_work(5);
            pitch = static_cast<std::uint8_t>(pitch + envelope.at(position++));
        }
        music_work(3);
        if (pitch == last_pitch_[channel]) {
            music_work(2);
            continue;
        }
        last_pitch_[channel] = pitch;
        music_work(7);
        const auto physical = music_channels[channel];
        if (physical >= carrier.size()) {
            music_work(4);
            continue;
        }
        const auto frequency = tables_.frequencies[pitch];
        frequency_high_[physical] = static_cast<std::uint8_t>((frequency >> 8) | (frequency_high_[physical] & 0xe0));
        music_work(11);
        write(0xa0 | physical, static_cast<std::uint8_t>(frequency));
        music_work(3);
        write(0xb0 | physical, frequency_high_[physical]);
        music_work(4);
    }
    music_work(2);
}
}
