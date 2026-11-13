#include "audio_impl.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace comanchecd {
using namespace audio_detail;

void MissionAudio::Impl::queue_initial_engine(std::uint8_t control_power) {
    if (!scheduler_ || effects_.size() < engine_sound) {
        throw std::logic_error("Initial engine requires scheduled mission samples");
    }
    // 194ea tail-calls 5174. A zero throttle queues the shared silence sample.
    execute(CpuOperation::instructions, control_power == 0 ? 10 : 8);
    execute(CpuOperation::disable_interrupts, 1);
    execute(CpuOperation::instructions, 14);
    const auto silent = voices_[2].current == &silence_;
    const SampleRequest request{digital_samples_, control_power == 0 ? SampleBank::silence : SampleBank::effects,
        control_power == 0 ? std::uint16_t(0) : static_cast<std::uint16_t>(engine_sound), 2, 2, 0, SampleMode::queued};
    auto& voice = voices_[2];
    voice.next = control_power == 0 ? &silence_ : &effects_[engine_sound - 1];
    voice.next_step = voice.next->step;
    voice.next_shift = 2;
    if (trace_policy_ == AudioTracePolicy::retain) { resolved_requests_.push_back(request); }
    execute(CpuOperation::instructions, 2);
    execute(CpuOperation::enable_interrupts, 1);
    execute(CpuOperation::instructions, 1);
    if (silent) {
        execute(CpuOperation::disable_interrupts, 1);
        execute(CpuOperation::instructions, 3 + 7 + 5 + 2 + 3 + 2);
        voice.current = voice.next;
        voice.current_step = voice.next_step;
        voice.shift = voice.next_shift;
        voice.position = 4u << 16;
    }
    if (silent) {
        execute(CpuOperation::enable_interrupts, 1);
        execute(CpuOperation::instructions, 1);
    }
}

void MissionAudio::Impl::play_launch_radio() {
    if (!scheduler_ || speech_.size() < launch_radio) {
        throw std::logic_error("Launch radio requires scheduled mission samples");
    }
    // 501d chooses a free speech voice before entering the shared 50a8 setter.
    execute(CpuOperation::instructions, 3 + 3 + 4);
    unsigned selected = 2;
    if (voices_[2].current != voices_[2].next) {
        execute(CpuOperation::instructions, 3);
        selected = 0;
        if (voices_[0].current != &silence_) {
            execute(CpuOperation::instructions, 3);
            selected = 1;
            if (voices_[1].current != &silence_) {
                execute(CpuOperation::instructions, 2);
                return;
            }
        }
    }
    const auto* sample = &speech_.at(launch_radio - 1);
    execute(CpuOperation::instructions, 5);
    for (const auto& voice : voices_) {
        if (voice.current == sample) {
            execute(CpuOperation::instructions, 2 + 2);
            return;
        }
        execute(CpuOperation::instructions, 2);
    }
    execute(CpuOperation::instructions, 1);
    execute(CpuOperation::disable_interrupts, 1);
    execute(CpuOperation::instructions, 3 + 7 + 1 + 5);
    if (selected != 0) { execute(CpuOperation::instructions, 2); }
    execute(CpuOperation::instructions, selected == 2 ? 3 : 4);
    execute(CpuOperation::instructions, 2);
    apply_request({digital_samples_, SampleBank::speech, launch_radio, static_cast<std::uint8_t>(selected), 0, 0, SampleMode::immediate});
    execute(CpuOperation::enable_interrupts, 1);
    execute(CpuOperation::instructions, 1 + 2);
}

void MissionAudio::Impl::stop_mission_samples() {
    if (!scheduler_) { throw std::logic_error("Mission loading requires scheduled audio"); }
    execute(CpuOperation::instructions, 2); // 5fd6 selector and CALL.
    disable_menu_samples(*scheduler_, digital_enabled_,
        [&](DigitalCommand command) { startup_digital(command); }, [&] { return reset_silence_voices(); });
    digital_enabled_ = false;
}

void MissionAudio::Impl::clear_mission_samples() {
    if (!scheduler_) { throw std::logic_error("Mission return requires scheduled audio"); }
    execute(CpuOperation::instructions, 1);
    clear_sample_voices(*scheduler_, [&] { return reset_silence_voices(); });
}

void MissionAudio::Impl::suspend_engine() {
    if (digital_enabled_) {
        apply_request({digital_samples_, SampleBank::silence, 0, 2, 0, 0, SampleMode::queued});
    }
}

void MissionAudio::Impl::start_cinematic_loop() {
    if (!cinematic_loop_pending_) { throw std::logic_error("No cinematic loop is pending"); }
    if (scheduler_) {
        // 5c0d arguments, queued request 5174, then immediate 50a8 on a silent voice.
        execute(CpuOperation::instructions, 7);
        execute(CpuOperation::disable_interrupts, 1);
        execute(CpuOperation::instructions, 16);
        execute(CpuOperation::enable_interrupts, 1);
        execute(CpuOperation::instructions, 1);
        execute(CpuOperation::disable_interrupts, 1);
        execute(CpuOperation::instructions, 22);
    }
    apply_request({digital_samples_, SampleBank::effects, engine_sound, 2, 1, 0, SampleMode::queued});
    cinematic_loop_pending_ = false;
    if (scheduler_) {
        execute(CpuOperation::enable_interrupts, 1);
        execute(CpuOperation::instructions, 1);
    }
}

void MissionAudio::Impl::resume_engine(std::uint8_t power) {
    if (digital_enabled_) {
        apply_request({digital_samples_, SampleBank::effects, engine_sound, 2, 2,
                       engine_rate(power), SampleMode::queued});
    }
}

void MissionAudio::Impl::play_effect(std::uint16_t sound, std::uint8_t attenuation,
                 EffectPolicy policy, std::uint32_t step) {
    const auto* sample = effect_sample(sound);
    if (sample == nullptr || attenuation >= sample_bias.size()) {
        throw std::invalid_argument("Invalid native effect request");
    }
    execute(CpuOperation::instructions, 5); // 5094 bank lookup.
    execute(CpuOperation::disable_interrupts, 1);
    execute(CpuOperation::instructions, 5); // PUSH, signed policy gate and FE test.
    auto selected = voices_.size();
    if (policy == EffectPolicy::available_first) {
        execute(CpuOperation::instructions, 2);
        for (std::size_t index = 0; index < voices_.size(); ++index) {
            if (index != 0) { execute(CpuOperation::instructions, 1); }
            if (voice_matches(index, &silence_)) { selected = index; break; }
        }
    }
    if (selected == voices_.size()) {
        execute(CpuOperation::instructions, 1);
        for (std::size_t index = 0; index < voices_.size(); ++index) {
            if (index != 0) { execute(CpuOperation::instructions, 1); }
            if (voice_matches(index, sample)) { selected = index; break; }
        }
    }
    if (selected == voices_.size()) {
        execute(CpuOperation::instructions, 2);
        if (attenuation != 0) {
            finish_sample_dispatch();
            return;
        }
        execute(CpuOperation::instructions, 1);
        selected = 0;
    }
    dispatch_sample({digital_samples_, SampleBank::effects, sound, static_cast<std::uint8_t>(selected), attenuation,
                     step, SampleMode::immediate});
}

void MissionAudio::Impl::set_engine_power(std::uint8_t power) {
    voices_[2].next_step = engine_rate(power);
}

void MissionAudio::Impl::play_cue(const SoundCue& cue) {
    if (cue.bank == SampleBank::effects) {
        play_effect(cue.sound, cue.attenuation, cue.policy, cue.step);
        return;
    }
    const auto& bank = cue.bank == SampleBank::speech ? speech_ : announcements_;
    if ((cue.bank != SampleBank::speech && cue.bank != SampleBank::announcements) ||
            cue.sound == 0 || cue.sound > bank.size() || cue.attenuation != 0) {
        throw std::invalid_argument("Unsupported native sound cue");
    }
    execute(CpuOperation::instructions, cue.bank == SampleBank::speech ? 3 + (cue.sound == 1 ? 2 : 0) : 2);
    execute(CpuOperation::instructions, 4); // PUSH ECX, bank test and MOV BL,2.
    const Sample* next = nullptr;
    instruction([&] { next = voices_[2].next; });
    auto selected = std::size_t(2);
    if (!voice_matches(2, next)) {
        execute(CpuOperation::instructions, 1);
        selected = 0;
        if (!voice_matches(0, &silence_)) {
            execute(CpuOperation::instructions, 1);
            selected = 1;
            if (!voice_matches(1, &silence_)) {
                execute(CpuOperation::instructions, 2);
                return;
            }
        }
    }
    const auto* sample = &bank[cue.sound - 1];
    execute(CpuOperation::instructions, 5);
    for (std::size_t index = 0; index < voices_.size(); ++index) {
        if (voice_matches(index, sample)) {
            execute(CpuOperation::instructions, 2);
            return;
        }
    }
    execute(CpuOperation::instructions, 1);
    execute(CpuOperation::disable_interrupts, 1);
    execute(CpuOperation::instructions, 3);
    dispatch_sample({digital_samples_, cue.bank, cue.sound, static_cast<std::uint8_t>(selected), 0, 0, SampleMode::immediate});
    execute(CpuOperation::instructions, 2);
}

bool MissionAudio::Impl::voice_matches(std::size_t index, const Sample* sample) {
    bool matches = false;
    instruction([&] { matches = voices_[index].current == sample; });
    execute(CpuOperation::instructions, 1);
    return matches;
}

void MissionAudio::Impl::finish_sample_dispatch() {
    execute(CpuOperation::instructions, 1);
    execute(CpuOperation::enable_interrupts, 1);
    execute(CpuOperation::instructions, 1);
}

void MissionAudio::Impl::dispatch_sample(const SampleRequest& request) {
    // 50fb..5173 runs with IRQs masked. The caller owns the CLI and routing gates.
    execute(CpuOperation::instructions, 7 + unsigned(request.step == 0) + 5);
    if (request.voice != 0) { execute(CpuOperation::instructions, 2); }
    execute(CpuOperation::instructions, request.voice == 2 ? 3 : 4);
    execute(CpuOperation::instructions, 1);
    apply_request(request);
    finish_sample_dispatch();
}

unsigned MissionAudio::Impl::reset_silence_voices() {
    unsigned changed = 0;
    for (auto& voice : voices_) {
        voice.next = &silence_;
        voice.next_step = 0;
        voice.shift = voice.next_shift = 0;
        if (voice.current == &silence_) { continue; }
        ++changed;
        voice.current = &silence_;
        voice.position = 0;
        voice.current_step = 4u << 16;
        voice.reload_pending = false;
    }
    return changed;
}

const audio_detail::Sample* MissionAudio::Impl::effect_sample(std::uint16_t sound) const {
    if (sound == cinematic_tail_sound && cinematic_tail_) {
        return cinematic_tail_.get();
    }
    return sound != 0 && sound <= effects_.size() ? &effects_[sound - 1] : nullptr;
}

void MissionAudio::Impl::startup_digital(DigitalCommand command) {
    if (command == DigitalCommand::stop) {
        scheduler_->halt_dma();
        digital_enabled_ = dma_running_ = false;
        return;
    }
    digital_enabled_ = true;
    start_dma();
}

void MissionAudio::Impl::apply_request(const SampleRequest& request) {
    const Sample* sample = &silence_;
    if (request.bank == SampleBank::effects) {
        sample = effect_sample(request.sound);
        if (sample == nullptr) {
            throw std::invalid_argument("Sample request exceeds its sound bank");
        }
    } else if (request.bank != SampleBank::silence) {
        const auto& bank = request.bank == SampleBank::effects ? effects_ :
                           request.bank == SampleBank::speech ? speech_ : announcements_;
        if (request.sound == 0 || request.sound > bank.size()) {
            throw std::invalid_argument("Sample request exceeds its sound bank");
        }
        sample = &bank[request.sound - 1];
    }
    auto& voice = voices_[request.voice];
    const auto step = request.step == 0 ? sample->step : request.step;
    if (trace_policy_ == AudioTracePolicy::retain) {
        resolved_requests_.push_back(request);
    }
    if (request.mode == SampleMode::queued) {
        voice.next = sample;
        voice.next_shift = request.attenuation;
        voice.next_step = step;
        if (voice.current != &silence_) {
            return;
        }
    }
    voice.current = sample;
    voice.current_step = step;
    voice.shift = request.attenuation;
    voice.position = 4u << 16;
}

void MissionAudio::Impl::poll_dsp() {
    bool busy;
    do {
        busy = scheduler_->read_dsp_busy();
        scheduler_->execute(CpuOperation::instructions, 3); // JMP, OR and JS.
    } while (busy);
}

void MissionAudio::Impl::begin_dma() {
    // 21d0..22be, active primary IRQ7 path. Port ordering affects DSP polling.
    const auto instructions = [this](unsigned count) { scheduler_->execute(CpuOperation::instructions, count); };
    const auto port_write = [this] { scheduler_->execute(CpuOperation::port_write_byte, 1); };
    instructions(6);
    port_write();
    scheduler_->execute(CpuOperation::port_read_byte, 1);
    instructions(12);
    scheduler_->execute(CpuOperation::port_read_byte, 1);
    instructions(2);
    poll_dsp();
    instructions(1);
    scheduler_->write_dsp_timed(dsp_pause_command);
    instructions(2);
    port_write();
    instructions(2);
    port_write();
    instructions(3);
    port_write();
    instructions(6);
    port_write();
    instructions(2);
    port_write();
    instructions(5);
    port_write();
    instructions(4);
    port_write();
    instructions(2);
    port_write();
    instructions(2);
    port_write();
    instructions(3);
    poll_dsp();
    instructions(1);
    scheduler_->write_dsp_timed(dsp_single_cycle_command);
    poll_dsp();
    instructions(1);
    scheduler_->write_dsp_timed((dma_half_bytes - 1) & 255);
    poll_dsp();
    instructions(1);
    scheduler_->write_dsp_timed((dma_half_bytes - 1) >> 8, [this] {
        scheduler_->arm_dma(dma_buffers_[producer_half_]);
    });
    instructions(2);
    port_write();
    instructions(1); // CALL 5420.
}

void MissionAudio::Impl::start_dma() {
    if (!scheduler_ || !digital_enabled_ || dma_running_) { return; }
    if (!digital_config_.enabled) {
        digital_config_.enabled = true;
        scheduler_->configure(AudioChannel::digital, digital_config_);
    }
    for (auto& half : dma_buffers_) { std::fill(half.begin(), half.end(), 128); }
    producer_half_ = 0;
    scheduler_->arm_dma(dma_buffers_[producer_half_]);
    dma_running_ = true;
}

void MissionAudio::Impl::produce_dma() {
    begin_dma();
    producer_half_ ^= 1;
    constexpr unsigned producer_prologue_instructions = 7, producer_return_and_irq_tail = 9;
    scheduler_->execute(CpuOperation::instructions, producer_prologue_instructions + producer_setup_instructions);
    for (auto& sample : dma_buffers_[producer_half_]) {
        while (request_cursor_ < requests_.size() && requests_[request_cursor_].sample == digital_samples_) {
            apply_request(requests_[request_cursor_++]);
        }
        std::uint64_t work = 0;
        sample = digital_enabled_ && sound_level_ != SoundLevel::off ? mix_sample(&work) : 128;
        if (!work) { work = producer_sample_instructions; }
        scheduler_->execute(CpuOperation::instructions, static_cast<unsigned>(work));
        ++digital_samples_;
        if (trace_policy_ == AudioTracePolicy::retain) { digital_.push_back(sample); }
    }
    scheduler_->execute(CpuOperation::instructions, producer_return_and_irq_tail);
}

std::uint8_t MissionAudio::Impl::mix_sample(std::uint64_t* work) {
    if (work) { *work += producer_sample_instructions; }
    unsigned sum = 0;
    for (auto& voice : voices_) {
        if (voice.reload_pending || (voice.position >> 16) >= voice.current->bytes.size()) {
            if (work) {
                *work += voice_reload_instructions;
                if (voice.current != voice.next) { *work += changed_voice_instructions; }
            }
            voice.reload_pending = false;
            voice.current = voice.next;
            voice.position = 4u << 16;
            voice.shift = voice.next_shift;
            voice.current_step = voice.next_step;
        }
        sum += (voice.current->bytes.at(voice.position >> 16) >> voice.shift) + sample_bias[voice.shift];
        voice.position += voice.current_step;
    }
    const auto cursor = echo_cursor_++ % echo_samples;
    sum += echo_[cursor];
    ++sum;
    const auto value = static_cast<std::uint8_t>(sum >> 2);
    echo_[cursor] = value;
    const auto& conversion = sound_level_ == SoundLevel::soft ? tables_.soft_sample_conversion : tables_.sample_conversion;
    return conversion[(unsigned(value) << 1) | ((sum >> 1) & 1)];
}
}
