#include "audio_impl.hpp"

#include "comanchecd/pilot.hpp"
#include "startup_timing.hpp"
#include "flight_work.hpp"
#include "fanfare_timing.hpp"
#include "movie_timing.hpp"
#include "menu_entry_timing.hpp"
#include "startup_resource.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <deque>
#include <cmath>

namespace comanchecd {
using namespace audio_detail;

std::uint32_t engine_rate(std::uint8_t power) {
    if (power > 200) {
        throw std::invalid_argument("Engine power exceeds the controller range");
    }
    return 0x4000u + std::min<std::uint32_t>(std::uint32_t(power) << 8, 0x8000u);
}

MissionAudio::Impl::Impl(AudioTables tables, const std::vector<GameplayPayload>& payloads, AudioEntry entry,
     std::vector<SampleRequest> requests, AudioTracePolicy trace_policy)
    : tables_(std::move(tables)), device_(sample_rate), irq_clock_(entry.irq_phase),
      trace_policy_(trace_policy), requests_(std::move(requests)) {
    if (payloads.size() < 17) {
        throw std::invalid_argument("Missing mission audio payloads");
    }
    effects_ = sample_bank(payloads[0].bytes);
    speech_ = sample_bank(payloads[payloads.size() - 2].bytes);
    announcements_ = sample_bank(payloads[payloads.size() - 4].bytes);
    music_ = payloads[payloads.size() - 5].bytes;
    if (music_.size() < 50 || music_[0] != 'M' || music_[1] != 'S') {
        throw std::invalid_argument("Expected MS music stream");
    }
    if (effects_.size() < engine_sound || speech_.size() < launch_radio) {
        throw std::invalid_argument("Missing initial engine or radio sample");
    }
    silence_.bytes.assign(15, 128);
    for (auto& voice : voices_) {
        voice.current = voice.next = &silence_;
    }
    voices_[2].current = &speech_[launch_radio - 1];
    voices_[2].next = &effects_[engine_sound - 1];
    voices_[2].current_step = voices_[2].current->step;
    voices_[2].next_step = engine_rate(entry.engine_power);
    voices_[2].next_shift = 2;
    for (std::size_t index = 0; index < requests_.size(); ++index) {
        if (requests_[index].voice >= voices_.size() || requests_[index].attenuation >= sample_bias.size() ||
                requests_[index].bank > SampleBank::silence || requests_[index].mode > SampleMode::queued ||
                (index > 0 && requests_[index].sample < requests_[index - 1].sample)) {
            throw std::invalid_argument("Invalid sampled sound request order or voice");
        }
    }
    initialize(entry);
}

MissionAudio::Impl::Impl(AudioTables tables, MusicPlaylist playlist, AudioEntry entry, AudioTracePolicy trace_policy)
    : tables_(std::move(tables)), device_(sample_rate),
      irq_clock_(entry.irq_phase), trace_policy_(trace_policy) {
    music_ = std::move(playlist.introduction);
    next_music_ = std::move(playlist.repeat);
    loop_music_ = false;
    if (music_.empty()) {
        music_ = std::move(next_music_);
        next_music_.clear();
        loop_music_ = true;
    }
    digital_enabled_ = false;
    silence_.bytes.assign(15, 128);
    for (auto& voice : voices_) {
        voice.current = voice.next = &silence_;
        voice.reload_pending = entry.startup_files.has_value();
    }
    initialize(entry);
}

void MissionAudio::Impl::set_sound_level(SoundLevel level) { sound_level_ = level; }

void MissionAudio::Impl::set_interrupt_handler(std::function<void()> handler) { timer_observer_ = std::move(handler); }

void MissionAudio::Impl::set_keyboard_handler(std::function<void(ExecutionClock&, KeyboardPort&)> handler) {
    if (!scheduler_) { throw std::logic_error("Keyboard IRQs require scheduled audio"); }
    if (!handler) { throw std::invalid_argument("Keyboard interrupt receiver is empty"); }
    scheduler_->set_keyboard_handler([this, handler = std::move(handler)] { handler(*this, *this); });
}

void MissionAudio::Impl::queue_key_byte(std::uint8_t scan) {
    if (!scheduler_) { throw std::logic_error("Keyboard IRQs require scheduled audio"); }
    scheduler_->queue_key_byte(scan);
}

void MissionAudio::Impl::schedule_key_byte(std::uint64_t cycle, std::uint8_t scan) {
    if (!scheduler_) { throw std::logic_error("Keyboard arrivals require scheduled audio"); }
    scheduler_->schedule_key_byte(cycle, scan);
}

std::uint8_t MissionAudio::Impl::read() { return scheduler_->read_keyboard(); }

void MissionAudio::Impl::acknowledge() { scheduler_->acknowledge_keyboard(); }

std::uint64_t MissionAudio::Impl::add_interrupt_handler(std::function<void()> handler) {
    if (!handler || ++interrupt_token_ == 0) {
        throw std::invalid_argument("Invalid timer interrupt subscription");
    }
    interrupt_handlers_.emplace_back(interrupt_token_, std::move(handler));
    return interrupt_token_;
}

void MissionAudio::Impl::remove_interrupt_handler(std::uint64_t token) {
    interrupt_handlers_.erase(std::remove_if(interrupt_handlers_.begin(), interrupt_handlers_.end(),
        [=](const auto& entry) { return entry.first == token; }), interrupt_handlers_.end());
}

void MissionAudio::Impl::enter_mission(const std::vector<GameplayPayload>& payloads, std::uint8_t power) {
    if (payloads.size() < 17) {
        throw std::invalid_argument("Missing mission audio payloads");
    }
    auto effects = sample_bank(payloads[0].bytes);
    auto speech = sample_bank(payloads[payloads.size() - 2].bytes);
    auto announcements = sample_bank(payloads[payloads.size() - 4].bytes);
    if (effects.size() < engine_sound || speech.size() < launch_radio) {
        throw std::invalid_argument("Missing initial engine or radio sample");
    }
    const auto step = engine_rate(power);
    silence_.bytes.assign(15, 128);
    for (auto& voice : voices_) {
        voice = Voice{};
        voice.current = voice.next = &silence_;
    }
    effects_ = std::move(effects);
    speech_ = std::move(speech);
    announcements_ = std::move(announcements);
    cinematic_tail_.reset();
    voices_[2].current = &speech_[launch_radio - 1];
    voices_[2].next = &effects_[engine_sound - 1];
    voices_[2].current_step = voices_[2].current->step;
    voices_[2].next_step = step;
    voices_[2].next_shift = 2;
    digital_enabled_ = true;
    start_dma();
    change_music({{}, payloads[payloads.size() - 5].bytes});
}

void MissionAudio::Impl::begin_mission_loading() {
    digital_enabled_ = false;
    stop_music();
}

void MissionAudio::Impl::prepare_mission_audio(const std::vector<GameplayPayload>& payloads, const StartupFiles& files) {
    if (!scheduler_ || payloads.size() < 17 || digital_enabled_) {
        throw std::logic_error("Mission audio settings require stopped scheduled samples");
    }
    auto effects = sample_bank(payloads[0].bytes);
    auto speech = sample_bank(payloads[payloads.size() - 2].bytes);
    auto announcements = sample_bank(payloads[payloads.size() - 4].bytes);
    if (effects.size() < engine_sound || speech.size() < launch_radio) {
        throw std::invalid_argument("Missing initial engine or radio sample");
    }
    for (const auto& voice : voices_) {
        if (voice.current != &silence_ || voice.next != &silence_) {
            throw std::logic_error("Mission sound banks require cleared voices");
        }
    }
    effects_ = std::move(effects);
    speech_ = std::move(speech);
    announcements_ = std::move(announcements);
    cinematic_tail_.reset();
    apply_startup_settings(*scheduler_, files, SettingsPass::post_start,
        [&](MusicLevel level) { apply_loaded_music_level(level); },
        [&](DigitalCommand command) { startup_digital(command); });
}

void MissionAudio::Impl::save_pilot_record() {
    if (!scheduler_) { throw std::logic_error("Pilot save requires scheduled audio"); }
    constexpr unsigned filename_bytes = 12, filename_byte_work = 12;
    execute(CpuOperation::instructions, 3 + 4 + filename_bytes * filename_byte_work + 8);
    scheduler_->file_metadata(DosFileCall::create);
    execute(CpuOperation::instructions, 6);
    scheduler_->write_file(pilot_record_bytes);
    execute(CpuOperation::instructions, 6);
    scheduler_->file_metadata(DosFileCall::close);
    execute(CpuOperation::instructions, 2);
}

void MissionAudio::Impl::present_debrief(MusicPlaylist playlist, unsigned height) {
    if (!scheduler_) { throw std::logic_error("Debrief presentation requires scheduled audio"); }
    execute(CpuOperation::instructions, 3); // 2beaa score arguments and CALL.
    leave_mission(std::move(playlist));
    execute(CpuOperation::instructions, 1);
    draw_loading_picture(*this, height);
    execute(CpuOperation::instructions, 1);
    flight_work::present(*this);
    execute(CpuOperation::instructions, 1);
    apply_loading_palette(*this);
    execute(CpuOperation::instructions, 1);
    copy_loading_page(*this, height);
}

GameplayPayload MissionAudio::Impl::load_mission_payload(const StartupResource& resource, PayloadKind kind, unsigned filename_bytes) {
    if (!scheduler_) { throw std::logic_error("Mission loading requires scheduled audio"); }
    return kind == PayloadKind::picture ?
        read_loading_picture(*scheduler_, *this, resource, filename_bytes, &mission_input_) :
        read_startup_resource(*scheduler_, *this, resource, filename_bytes, &mission_input_);
}

void MissionAudio::Impl::load_menu_picture(const StartupResource& resource, unsigned filename_bytes) {
    if (!scheduler_) { throw std::logic_error("Menu picture loading requires scheduled audio"); }
    prepare_menu_picture(*scheduler_, *this, resource, filename_bytes);
}

void MissionAudio::Impl::load_campaign(const StartupCampaign& campaign) {
    if (!scheduler_) { throw std::logic_error("Campaign loading requires scheduled audio"); }
    execute(CpuOperation::instructions, 8);
    read_campaign(*scheduler_, campaign);
}

void MissionAudio::Impl::start_fanfare(const FanfareAssets& assets) {
    if (!scheduler_ || !cold_music_pending_) {
        throw std::logic_error("Fanfare startup requires deferred cold music");
    }
    stop_music();
    auto score = prepare_fanfare(*scheduler_, *this, assets);
    change_music({std::move(score), {}});
    startup_movie_pending_ = true;
}

std::uint64_t MissionAudio::Impl::load_startup_movie(const StartupFiles& files, const StartupMovieAssets& assets) {
    if (!scheduler_ || !startup_movie_pending_) {
        throw std::logic_error("Startup movie loading must follow cold fanfare entry");
    }
    std::uint64_t origin = 0;
    prepare_startup_movie(*scheduler_, *this, files, assets,
        [&](MusicLevel level) { apply_loaded_music_level(level); },
        [&](DigitalCommand command) { startup_digital(command); }, [&] { origin = irq_ticks(); });
    startup_movie_pending_ = false;
    startup_menu_pending_ = true;
    return origin;
}

void MissionAudio::Impl::enter_pilot_menu(const PilotMenuAssets& assets) {
    if (!scheduler_ || !startup_menu_pending_) {
        throw std::logic_error("Pilot-menu loading requires completed startup movie loading");
    }
    prepare_pilot_menu(*scheduler_, *this, assets, digital_enabled_,
        [&](DigitalCommand command) { startup_digital(command); },
        [&] { return reset_silence_voices(); },
        [&] { change_music({assets.scores[0].encoded, assets.scores[1].encoded}); });
    startup_menu_pending_ = false;
}

void MissionAudio::Impl::read_file(unsigned bytes) {
    if (scheduler_) { scheduler_->read_file(bytes); }
}

void MissionAudio::Impl::enter_cinematic(const std::vector<std::uint8_t>& bank, MusicPlaylist playlist, CinematicLoopStart loop) {
    auto effects = sample_bank(bank);
    if (effects.size() < engine_sound) {
        throw std::invalid_argument("Missing cinematic loop sample");
    }
    silence_.bytes.assign(15, 128);
    leave_mission(std::move(playlist));
    effects_ = std::move(effects);
    // TANIM.SND's final CR indexes past SND1's directory into sample bytes.
    // The qualified DOSBox-X call reads an unmapped FFFF/FFFF sample header.
    if (effects_.size() == 7 && bank.size() >= cinematic_tail_sound * 4 + 4 &&
            integer(bank, cinematic_tail_sound * 4, 4) == cinematic_tail_offset) {
        cinematic_tail_ = std::make_unique<Sample>(Sample{std::vector<std::uint8_t>(unmapped_sample_length, 255), 0xffff});
    }
    digital_enabled_ = true;
    start_dma();
    cinematic_loop_pending_ = true;
    if (loop == CinematicLoopStart::immediate) { start_cinematic_loop(); }
}

void MissionAudio::Impl::leave_mission(MusicPlaylist playlist) {
    digital_enabled_ = false;
    digital_value_ = 128;
    for (auto& voice : voices_) {
        voice = Voice{};
        voice.current = voice.next = &silence_;
    }
    cinematic_tail_.reset();
    cinematic_loop_pending_ = false;
    change_music(std::move(playlist));
}

std::vector<std::int16_t> MissionAudio::Impl::take_pending_audio() {
    std::vector<std::int16_t> result(scheduled_output_.begin(), scheduled_output_.end());
    scheduled_output_.clear();
    if (scheduler_) {
        const auto available = scheduler_->take_output();
        result.reserve(result.size() + available.size());
        for (const auto& sample : available) { result.push_back(sample[0]); }
    }
    return result;
}

std::vector<std::int16_t> MissionAudio::Impl::render_foreground(std::size_t count, const std::function<void()>& foreground) {
    if (!scheduler_ || !foreground) { throw std::invalid_argument("Foreground rendering requires a scheduled clock and CPU work"); }
    return render_scheduled(count, {}, foreground);
}

std::vector<std::int16_t> MissionAudio::Impl::render(std::size_t count, const std::function<void()>& interrupt) {
    if (scheduler_) { return render_scheduled(count, interrupt); }
    std::vector<std::int16_t> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        if (irq_clock_.advance(1) != 0) {
            ++tick_;
            music_tick();
            if (interrupt) {
                interrupt();
            } else {
                notify_interrupt();
            }
        }
        digital_phase_ += Sb16Playback::source_rate;
        while (digital_phase_ >= sample_rate) {
            digital_phase_ -= sample_rate;
            while (request_cursor_ < requests_.size() && requests_[request_cursor_].sample == digital_samples_) {
                apply_request(requests_[request_cursor_++]);
            }
            digital_value_ = digital_enabled_ && sound_level_ != SoundLevel::off ? mix_sample() : 128;
            device_.queue_digital(digital_value_);
            ++digital_samples_;
            if (trace_policy_ == AudioTracePolicy::retain) {
                digital_.push_back(digital_value_);
            }
        }
        result.push_back(device_.render_sample());
    }
    return result;
}

const std::vector<OplWrite>& MissionAudio::Impl::writes() const { return writes_; }

const std::vector<MusicIrqWork>& MissionAudio::Impl::music_irq_work() const { return music_irq_work_; }

const std::vector<std::uint8_t>& MissionAudio::Impl::digital_pcm() const { return digital_; }

const std::vector<SampleRequest>& MissionAudio::Impl::sample_requests() const { return resolved_requests_; }

std::uint64_t MissionAudio::Impl::irq_ticks() const { return tick_; }

ExecutionClock& MissionAudio::Impl::execution_clock() {
    if (!scheduler_) { throw std::logic_error("CPU work requires scheduled audio"); }
    return *this;
}

void MissionAudio::Impl::execute(CpuOperation operation, unsigned count) {
    if (collecting_irq_work_) {
        irq_state_work_.emplace_back(operation, count);
        return;
    }
    if (scheduler_) { scheduler_->execute(operation, count); }
}

void MissionAudio::Impl::instruction(const std::function<void()>& action) {
    if (scheduler_) {
        scheduler_->instruction(action);
    } else {
        action();
    }
}

bool MissionAudio::Impl::read_retrace() { return scheduler_->read_retrace(); }

std::uint64_t MissionAudio::Impl::cycles() const { return scheduler_->cycles(); }

std::uint64_t MissionAudio::Impl::sample_irq_ticks() {
    std::uint64_t value = 0;
    scheduler_->instruction([&] { value = tick_; });
    return value;
}

void MissionAudio::Impl::initialize(const AudioEntry& entry) {
    if (entry.startup_files && (entry.startup != AudioStartup::cold ||
            entry.execution != AudioExecution::scheduled || digital_enabled_)) {
        throw std::invalid_argument("Startup files require cold scheduled music-only entry");
    }
    if (entry.calibration == AudioCalibration::pit2 && (entry.startup != AudioStartup::cold || digital_enabled_)) {
        throw std::invalid_argument("PIT2 calibration requires cold music-only startup");
    }
    if (entry.execution != AudioExecution::scheduled &&
        (entry.startup == AudioStartup::cold || entry.before_music_device || entry.cpu_observer || entry.pit_start != PitStart::periodic)) {
        throw std::invalid_argument("Cold device startup requires scheduled execution");
    }
    music_callback_installed_ = entry.startup != AudioStartup::cold;
    if (entry.interrupt) {
        timer_observer_ = [this, interrupt = entry.interrupt] {
            interrupt([this](CpuOperation operation, unsigned count) { execute(operation, count); });
        };
    }
    if (entry.execution == AudioExecution::scheduled) {
        if (entry.irq_phase != 0 || entry.opl_delay_iterations == 0 || entry.opl_delay_iterations > 1000000) {
            throw std::invalid_argument("Unsupported scheduled audio entry timing");
        }
        scheduler_ = std::make_unique<AudioScheduler>(sample_rate);
        scheduler_->set_cpu_observer(entry.cpu_observer);
        scheduler_->enable_interrupts(false);
        scheduler_->set_interrupt_delay(838);
        opl_delay_iterations_ = entry.opl_delay_iterations;
        scheduler_->configure(AudioChannel::fm, {entry.startup != AudioStartup::cold, sample_rate,
            {audio_gain_one * 3 / 2, audio_gain_one * 3 / 2}, 0});
        const double interval = 1.0 / sample_rate;
        const double tau = 1.0 / (5555 * 2 * 3.14159265358979323846);
        // SB16 keeps its mixer channel enabled while DMA is halted.
        digital_config_ = {true, Sb16Playback::source_rate,
            {audio_gain_one, audio_gain_one}, static_cast<std::int32_t>(interval / (tau + interval) * 65536)};
        scheduler_->configure(AudioChannel::digital, digital_config_);
        for (auto& half : dma_buffers_) { half.assign(dma_half_bytes, 128); }
        start_dma();
        scheduler_->set_dma_handler([this] {
            if (!digital_enabled_) {
                dma_running_ = false;
                return;
            }
            produce_dma();
        });
        if (entry.calibration == AudioCalibration::pit2) {
            opl_delay_iterations_ = calibrate_opl_delay(*scheduler_);
            // Timer descriptor setup and writer prefix through the high-byte OUT.
            scheduler_->execute(CpuOperation::instructions, 29);
        }
        const auto timer = [this] {
            scheduler_->execute(CpuOperation::instructions, 2);
            scheduler_->execute(CpuOperation::port_write_byte, 1);
            scheduler_->execute(CpuOperation::disable_interrupts, 1);
            scheduler_->execute(CpuOperation::instructions, 7);
            ++tick_;
            const auto before = scheduler_->accounting();
            const auto first_write = writes_.size();
            if (music_callback_installed_) {
                music_cpu_work([&] { music_tick(); });
            } else {
                scheduler_->execute(CpuOperation::instructions, 1); // Uninstalled callback is RET.
            }
            if (trace_policy_ == AudioTracePolicy::retain) {
                auto work = scheduler_->accounting();
                work.dispatches -= before.dispatches;
                work.string_iterations -= before.string_iterations;
                work.repeat_restarts -= before.repeat_restarts;
                work.special_stosb_iterations -= before.special_stosb_iterations;
                work.terminal_charges -= before.terminal_charges;
                work.io_cycles -= before.io_cycles;
                music_irq_work_.push_back({tick_, first_write, writes_.size(), work});
            }
            scheduler_->execute(CpuOperation::instructions, 7); // Counter/input test and RNG through ROR.
            irq_state_work_.clear();
            scheduler_->instruction([this] {
                // Receivers commit canonical state in the RNG store. Their
                // reported shake work retires after the shared four-instruction test.
                collecting_irq_work_ = true;
                try {
                    if (active_interrupt_) { active_interrupt_(); }
                    else { notify_interrupt(); }
                } catch (...) {
                    collecting_irq_work_ = false;
                    throw;
                }
                collecting_irq_work_ = false;
            });
            scheduler_->execute(CpuOperation::instructions, 4);
            for (const auto& work : irq_state_work_) { scheduler_->execute(work.first, work.second); }
            scheduler_->execute(CpuOperation::instructions, 8); // Saved registers and IRET.
        };
        if (entry.calibration == AudioCalibration::pit2) {
            scheduler_->program_pit(mission_pit_divisor, timer, entry.pit_start);
        } else {
            scheduler_->set_pit(mission_pit_divisor, timer, entry.pit_start);
        }
        if (entry.startup == AudioStartup::cold) { scheduler_->enable_interrupts(true); }
        if (entry.before_music_device) { entry.before_music_device(*this); }
    }
    echo_ = entry.echo;
    echo_cursor_ = entry.echo_cursor;
    music_volume_ = entry.music_volume;
    if (entry.startup == AudioStartup::cold) {
        if (entry.startup_files) { pre_fm_startup(*scheduler_, *entry.startup_files); }
        music_cpu_work([&] { initialize_cold_fm(); });
        if (entry.startup_files && entry.startup_files->campaigns) {
            prepare_startup_campaigns(*scheduler_, *entry.startup_files);
            prepare_startup_video(*scheduler_);
            scheduler_->execute(CpuOperation::instructions, 1); // 5834 presenter CALL.
            flight_work::present(*this);
            if (entry.startup_files->loading) {
                prepare_startup_loading(*scheduler_, *entry.startup_files, entry.startup_state,
                    [&](MusicLevel level) { apply_loaded_music_level(level); }, {},
                    [&](DigitalCommand command) { startup_digital(command); });
                startup_stop_pending_ = entry.music_start == MusicStart::deferred;
            }
        }
        cold_music_pending_ = entry.music_start == MusicStart::deferred;
        if (entry.music_start == MusicStart::immediate) { start_music(); }
        return;
    }
    write(1, 0x20);
    write(8, 0);
    write(0xbd, 0xc0);
    for (std::size_t channel = 0; channel < carrier.size(); ++channel) {
        write(0x40 | carrier[channel], 63);
        write(0xb0 | channel, 0);
        if (entry.frequency_high[channel] > 0x1f) {
            throw std::invalid_argument("Audio entry frequency cache must have key-off state");
        }
        frequency_high_[channel] = entry.frequency_high[channel];
        program(channel, 0);
    }
    music_callback_installed_ = true;
    if (entry.music_start == MusicStart::immediate) {
        start_music();
    } else {
        music_active_ = false;
    }
    if (scheduler_) { scheduler_->enable_interrupts(true); }
}

void MissionAudio::Impl::notify_interrupt() {
    if (!interrupt_handlers_.empty()) {
        const auto handler = interrupt_handlers_.back().second;
        handler();
    } else if (timer_observer_) {
        timer_observer_();
    }
}

std::vector<std::int16_t> MissionAudio::Impl::render_scheduled(std::size_t count, const std::function<void()>& interrupt,
                                        const std::function<void()>& foreground) {
    std::vector<std::int16_t> result;
    result.reserve(count);
    active_interrupt_ = interrupt;
    try {
        while (result.size() < count) {
            auto available = scheduler_->take_output();
            for (const auto& sample : available) { scheduled_output_.push_back(sample[0]); }
            if (!scheduled_output_.empty()) {
                result.push_back(scheduled_output_.front());
                scheduled_output_.pop_front();
                continue;
            }
            scheduled_phase_ += dos_cycles_per_second;
            scheduled_target_ += scheduled_phase_ / sample_rate;
            scheduled_phase_ %= sample_rate;
            while (scheduler_->cycles() < scheduled_target_) {
                if (!foreground) { scheduler_->run_cycles(scheduled_target_ - scheduler_->cycles()); break; }
                const auto before = scheduler_->cycles();
                foreground();
                if (scheduler_->cycles() == before) { throw std::logic_error("Foreground callback did not advance its clock"); }
            }
        }
    } catch (...) {
        active_interrupt_ = {};
        throw;
    }
    active_interrupt_ = {};
    return result;
}

MissionAudio::MissionAudio(AudioTables tables, const std::vector<GameplayPayload>& payloads, AudioEntry entry,
                           std::vector<SampleRequest> requests, AudioTracePolicy trace_policy)
    : impl_(std::make_unique<Impl>(std::move(tables), payloads, entry, std::move(requests), trace_policy)) {}
MissionAudio::MissionAudio(AudioTables tables, MusicPlaylist playlist, AudioEntry entry, AudioTracePolicy trace_policy)
    : impl_(std::make_unique<Impl>(std::move(tables), std::move(playlist), entry, trace_policy)) {}
void MissionAudio::set_music_level(MusicLevel level, MusicChange change) { impl_->set_music_level(level, change); }
std::vector<std::int16_t> MissionAudio::take_pending_audio() { return impl_->take_pending_audio(); }
void MissionAudio::set_sound_level(SoundLevel level) { impl_->set_sound_level(level); }
void MissionAudio::set_interrupt_handler(std::function<void()> handler) { impl_->set_interrupt_handler(std::move(handler)); }
void MissionAudio::set_keyboard_handler(std::function<void(ExecutionClock&, KeyboardPort&)> handler) {
    impl_->set_keyboard_handler(std::move(handler));
}
void MissionAudio::queue_key_byte(std::uint8_t scan) { impl_->queue_key_byte(scan); }
void MissionAudio::schedule_key_byte(std::uint64_t cycle, std::uint8_t scan) { impl_->schedule_key_byte(cycle, scan); }
std::uint64_t MissionAudio::add_interrupt_handler(std::function<void()> handler) {
    return impl_->add_interrupt_handler(std::move(handler));
}
void MissionAudio::remove_interrupt_handler(std::uint64_t token) { impl_->remove_interrupt_handler(token); }
ExecutionClock& MissionAudio::execution_clock() { return impl_->execution_clock(); }
CpuWorkSink MissionAudio::cpu_work() {
    return [state = impl_.get()](CpuOperation operation, unsigned count) { state->execute(operation, count); };
}
void MissionAudio::enter_mission(const std::vector<GameplayPayload>& payloads, std::uint8_t power) {
    impl_->enter_mission(payloads, power);
}
void MissionAudio::prepare_mission_audio(const std::vector<GameplayPayload>& payloads, const StartupFiles& files) {
    impl_->prepare_mission_audio(payloads, files);
}
void MissionAudio::queue_initial_engine(std::uint8_t control_power) { impl_->queue_initial_engine(control_power); }
void MissionAudio::play_launch_radio() { impl_->play_launch_radio(); }
void MissionAudio::begin_mission_loading() { impl_->begin_mission_loading(); }
void MissionAudio::stop_mission_samples() { impl_->stop_mission_samples(); }
void MissionAudio::clear_mission_samples() { impl_->clear_mission_samples(); }
void MissionAudio::save_pilot_record() { impl_->save_pilot_record(); }
void MissionAudio::present_debrief(MusicPlaylist playlist, unsigned height) { impl_->present_debrief(std::move(playlist), height); }
GameplayPayload MissionAudio::load_mission_payload(const StartupResource& resource, PayloadKind kind, unsigned filename_bytes) {
    return impl_->load_mission_payload(resource, kind, filename_bytes);
}
void MissionAudio::load_menu_picture(const StartupResource& resource, unsigned filename_bytes) {
    impl_->load_menu_picture(resource, filename_bytes);
}
void MissionAudio::load_campaign(const StartupCampaign& campaign) { impl_->load_campaign(campaign); }
void MissionAudio::stop_music() { impl_->stop_music(); }
void MissionAudio::start_fanfare(const FanfareAssets& assets) { impl_->start_fanfare(assets); }
std::uint64_t MissionAudio::load_startup_movie(const StartupFiles& files, const StartupMovieAssets& assets) {
    return impl_->load_startup_movie(files, assets);
}
void MissionAudio::enter_pilot_menu(const PilotMenuAssets& assets) { impl_->enter_pilot_menu(assets); }
void MissionAudio::change_music(MusicPlaylist playlist) { impl_->change_music(std::move(playlist)); }
void MissionAudio::leave_mission(MusicPlaylist playlist) { impl_->leave_mission(std::move(playlist)); }
void MissionAudio::enter_cinematic(const std::vector<std::uint8_t>& bank, MusicPlaylist playlist, CinematicLoopStart loop) {
    impl_->enter_cinematic(bank, std::move(playlist), loop);
}
void MissionAudio::start_cinematic_loop() { impl_->start_cinematic_loop(); }
void MissionAudio::suspend_engine() { impl_->suspend_engine(); }
void MissionAudio::resume_engine(std::uint8_t power) { impl_->resume_engine(power); }
MissionAudio::~MissionAudio() = default;
MissionAudio::MissionAudio(MissionAudio&&) noexcept = default;
MissionAudio& MissionAudio::operator=(MissionAudio&&) noexcept = default;
std::vector<std::int16_t> MissionAudio::render(std::size_t samples, const std::function<void()>& interrupt) {
    return impl_->render(samples, interrupt);
}
void MissionAudio::play_effect(std::uint16_t sound, std::uint8_t attenuation) { impl_->play_effect(sound, attenuation); }
void MissionAudio::play_cue(const SoundCue& cue) { impl_->play_cue(cue); }
void MissionAudio::set_engine_power(std::uint8_t power) { impl_->set_engine_power(power); }
std::uint64_t MissionAudio::irq_ticks() const { return impl_->irq_ticks(); }
std::vector<std::int16_t> MissionAudio::render_foreground(std::size_t samples, const std::function<void()>& foreground) {
    return impl_->render_foreground(samples, foreground);
}
const std::vector<OplWrite>& MissionAudio::writes() const { return impl_->writes(); }
const std::vector<MusicIrqWork>& MissionAudio::music_irq_work() const { return impl_->music_irq_work(); }
const std::vector<std::uint8_t>& MissionAudio::digital_pcm() const { return impl_->digital_pcm(); }
const std::vector<SampleRequest>& MissionAudio::sample_requests() const { return impl_->sample_requests(); }
} // namespace comanchecd
