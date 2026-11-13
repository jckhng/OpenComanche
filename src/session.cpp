#include "comanchecd/session.hpp"
#include "menu_input.hpp"

namespace comanchecd {
GameSession::GameSession(Installation installation, PilotWriter writer, std::vector<std::uint8_t> pilots,
                         std::vector<std::uint8_t> settings, SettingsWriter settings_writer, SessionStart start)
    : installation_(std::move(installation)),
      settings_(settings.empty() ? installation_.load_menu_settings() : MenuSettings(std::move(settings))),
      settings_writer_(std::move(settings_writer)), pilot_writer_(std::move(writer)), pilot_bytes_(std::move(pilots)),
      startup_(start == SessionStart::cold) {
    if (startup_) {
        begin_startup();
        return;
    }
    start_menu_audio(installation_.load_menu_music());
    start_frontend();
}

void GameSession::start_menu_audio(MusicPlaylist playlist) {
    if (audio_) {
        audio_->leave_mission(std::move(playlist));
    } else {
        AudioEntry entry;
        entry.execution = AudioExecution::scheduled;
        if (startup_) {
            entry.startup = AudioStartup::cold;
            entry.pit_start = PitStart::inherited_square_wave;
            entry.calibration = AudioCalibration::pit2;
            entry.music_start = MusicStart::deferred;
            entry.startup_files = installation_.load_startup_files();
            entry.startup_state = &session_irq_;
            entry.before_music_device = [this](ExecutionClock& clock) {
                clock.execute(CpuOperation::instructions, 3); // PIT returns and keyboard installer CALL.
                keyboard_.initialize(clock);
            };
        }
        entry.interrupt = [this](const CpuWorkSink& work) { session_irq_.interrupt(work); };
        audio_ = std::make_shared<MissionAudio>(installation_.load_audio(), std::move(playlist),
                                               entry, AudioTracePolicy::discard);
        audio_->set_keyboard_handler([this](ExecutionClock& clock, KeyboardPort& port) {
            keyboard_.interrupt(clock, port);
        });
    }
    audio_->set_music_level(settings_.music_level(), MusicChange::loaded);
}

void GameSession::release_flight() {
    if (!flight_) { return; }
    session_irq_ = flight_->interrupt_state();
    flight_.reset();
    flight_notice_ = FlightNotice::none;
}

void GameSession::queue_key_byte(std::uint8_t scan) {
    hardware_input_ = true;
    audio_->queue_key_byte(scan);
}

void GameSession::schedule_key_byte(std::uint64_t cycle, std::uint8_t scan) {
    audio_->schedule_key_byte(cycle, scan);
    hardware_input_ = true;
}

void GameSession::poll_input() {
    auto& clock = audio_->execution_clock();
    if (startup_ && demo_phase_ == DemoPhase::fanfare) {
        // The original CMP/JBE samples the delivered counter in the foreground.
        const auto elapsed = clock.sample_irq_ticks();
        clock.execute(CpuOperation::instructions, 1);
        if (elapsed >= demo_wait_) { begin_fanfare_movie(); }
        return;
    }
    if (startup_) {
        clock.execute(CpuOperation::instructions, 2);
        startup_skip_ = startup_skip_ || keyboard_.pending_scan() != 0;
        return;
    }
    const auto state = frontend_->state();
    if (flight_ && state.mode != FrontendMode::settings) {
        std::uint8_t scan = 0;
        clock.instruction([&] { scan = keyboard_.pending_scan(); });
        if (scan == 0x01) {
            clock.instruction([&] { keyboard_.clear_pending(); });
            consume({scan});
        }
        return;
    }
    const auto list = state.mode == FrontendMode::main || state.mode == FrontendMode::campaign ||
        state.mode == FrontendMode::mission || (state.mode == FrontendMode::pilot && state.pilot_mode == PilotMode::list);
    std::uint8_t scan = 0;
    if (list) { scan = poll_list_key(keyboard_, clock); }
    else {
        clock.instruction([&] { scan = keyboard_.pending_scan(); });
        if (scan != 0) { clock.instruction([&] { keyboard_.clear_pending(); }); }
    }
    if (scan == 0) { return; }
    const auto left = keyboard_.held(0x2a), right = keyboard_.held(0x36);
    const auto input_resets = menu_input_resets_;
    consume({scan, left ? (right ? PilotShift::both : PilotShift::left) : right ? PilotShift::right : PilotShift::none});
    const auto next = frontend_->state();
    // A make delivered after the screen's own reset belongs to that screen.
    if (menu_input_resets_ == input_resets && keyboard_.pending_scan() == scan &&
            (next.mode != state.mode || next.pilot_mode != state.pilot_mode)) {
        clock.instruction([&] { keyboard_.clear_pending(); });
    }
}

void GameSession::advance(const FlightControls& controls, const CockpitControls& displays) {
    if (startup_ && startup_skip_ && flight_) { finish_startup(); return; }
    intermediate_images_.clear();
    if (!flight_ || (frontend_ && frontend_->state().mode == FrontendMode::settings)) {
        return;
    }
    bool settings_requested = false;
    FlightInput input;
    if (hardware_input_ && !demo_playing()) {
        input.take_key = [&](ExecutionClock& clock) { return keyboard_.take_scan(clock); };
        input.alt_down = [&] { return keyboard_.held(0x38); };
        input.settings = [&] { settings_requested = true; };
        input.notice = [&](FlightNotice notice) { flight_notice_ = notice; };
    }
    flight_->advance(demo_playing() ? FlightControls{} : controls, demo_playing() ? CockpitControls{} : displays, input);
    intermediate_images_ = flight_->intermediate_images();
    if (demo_playing()) {
        if (flight_->demo_finished() || flight_->state().ended) {
            release_flight();
            start_demo_cycle();
        }
        return;
    }
    if (settings_requested && !flight_->state().ended) { consume({0x01}); }
    if (flight_->state().ended) { finish_flight(); }
}

void GameSession::finish_flight() {
    std::vector<std::pair<CpuOperation, unsigned>> statistics;
    const auto result = flight_->result([&](CpuOperation operation, unsigned count) {
        statistics.emplace_back(operation, count);
    });
    inactive_state_ = flight_->state();
    release_flight();
    auto& clock = audio_->execution_clock();
    clock.execute(CpuOperation::instructions, 28); // 2bbc8 resumes the sequencer and resets the return pose.
    audio_->stop_mission_samples();
    clock.execute(CpuOperation::instructions, 1); // Result CALL after the DSP stop.
    for (const auto& operation : statistics) { clock.execute(operation.first, operation.second); }
    audio_->clear_mission_samples();
    clock.execute(CpuOperation::instructions, 1 + 3); // 23ad2 clears all entity activity bytes.
    clock.execute(CpuOperation::repeat_store_bytes, scene_slots - 1);
    clock.execute(CpuOperation::instructions, 3 + 1);
    audio_->stop_music();
    clock.execute(CpuOperation::instructions, 21); // Allocator rewind, pointer clears and mission/demo guards.
    frontend_->finish_mission(result, [&] { audio_->save_pilot_record(); });
}

std::vector<std::int16_t> GameSession::render_audio(std::size_t samples) {
    if (overview_ || (demo_playing() && !flight_)) {
        return render_presentation_audio(samples);
    }
    auto result = flight_ ? flight_->render_audio(samples) : hardware_input_ ?
        audio_->render_foreground(samples, [this] { poll_input(); }) : audio_->render(samples);
    if (hardware_input_ && flight_) { poll_input(); }
    elapsed_samples_ += samples;
    return result;
}
const CockpitImage& GameSession::image() const { return flight_ && (!frontend_ || frontend_->state().mode != FrontendMode::settings) ? flight_->cockpit_image() : menu_image_; }
void GameSession::set_presentation_detail(PresentationDetail detail) {
    presentation_detail_ = detail;
    if (flight_) { flight_->set_presentation_detail(detail); }
}
std::shared_ptr<const SessionPresentation> GameSession::presentation() const {
    auto frame = std::make_shared<SessionPresentation>();
    frame->menu = menu_state();
    frame->flight = flight_state();
    frame->flight_notice = flight_notice_;
    frame->image = image();
    frame->cpu_cycles = cpu_cycles();
    frame->irq_ticks = irq_ticks();
    frame->audio_samples = elapsed_samples_;
    frame->random_seed = random_seed();
    frame->flying = flying();
    if (frame->flying && frame->menu.mode != FrontendMode::settings) { frame->spatial = flight_->spatial_frame(); }
    frame->demo_playing = demo_playing();
    frame->quit_requested = quit_requested();
    return frame;
}
std::vector<std::int16_t> GameSession::take_pending_audio() {
    auto result = audio_->take_pending_audio();
    elapsed_samples_ += result.size();
    return result;
}
const std::vector<CockpitImage>& GameSession::intermediate_images() const { return intermediate_images_; }
const FlightState& GameSession::flight_state() const { return flight_ ? flight_->state() : inactive_state_; }
FrontendState GameSession::menu_state() const {
    if (!frontend_) { FrontendState state{}; state.mode = FrontendMode::startup; return state; }
    return frontend_->state();
}
std::uint64_t GameSession::irq_ticks() const { return audio_->irq_ticks(); }
std::uint64_t GameSession::cpu_cycles() const { return audio_->execution_clock().cycles(); }
std::uint32_t GameSession::random_seed() const {
    return flight_ ? flight_->interrupt_state().random_seed : session_irq_.random_seed;
}
bool GameSession::flying() const { return flight_ != nullptr; }
bool GameSession::demo_playing() const { return demo_phase_ != DemoPhase::none; }
bool GameSession::quit_requested() const { return quit_; }
void GameSession::abort_mission() {
    if (demo_playing()) {
        consume({0x01});
        return;
    }
    intermediate_images_.clear();
    if (flight_) {
        flight_->abort();
        finish_flight();
    }
}

} // namespace comanchecd
