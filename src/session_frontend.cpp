#include "comanchecd/session.hpp"
#include <stdexcept>
#include <utility>

namespace comanchecd {
void GameSession::start_frontend() {
    auto assets = installation_.load_frontend();
    if (!pilot_bytes_.empty()) {
        assets.pilot_record = PilotStorage(std::move(pilot_bytes_)).record();
    }
    frontend_ = std::make_unique<Frontend>(std::move(assets), [&](FrontendScene scene) {
        auto picture = scene == FrontendScene::debrief || scene == FrontendScene::debrief_success ?
            installation_.load_debrief(frontend_->mission_presentation().resources, scene, *audio_) :
            scene == FrontendScene::campaign ? installation_.load_campaign_picture(*audio_) : installation_.load_picture(scene);
        menu_image_.palette = picture.palette;
        return picture;
    }, std::move(pilot_writer_), [&](const FrontendState&, const PilotController&, const MenuRenderer& renderer) {
        menu_back_page_ = renderer.back_page();
        for (std::size_t y = 0; y < cockpit_height; ++y) {
            for (std::size_t x = 0; x < cockpit_width; ++x) {
                menu_image_.pixels[y * cockpit_width + x] = renderer.surface().read(x % plane_count,
                    renderer.back_page() + 1 + y * line_stride + x / plane_count);
            }
        }
    }, [&](std::uint8_t campaign) { return installation_.load_missions(campaign, *audio_, CampaignLoad::catalog); }, FrontendPage::first,
       &audio_->execution_clock(), [this] { keyboard_.clear_pending(); ++menu_input_resets_; },
       [&](std::uint8_t campaign) { return installation_.load_missions(campaign, *audio_, CampaignLoad::briefing); });
    frontend_->start();
}

void GameSession::consume(PilotKey key) {
    if (key.scan != 0) { flight_notice_ = FlightNotice::none; }
    if (startup_) {
        if (key.scan != 0) { startup_skip_ = true; }
        return;
    }
    if (quit_) {
        return;
    }
    if (demo_playing()) {
        if (key.scan != 0) {
            release_flight();
            demo_player_.reset();
            cinematics_.reset();
            demo_phase_ = DemoPhase::none;
            demo_clear_sample_ = 0;
            intermediate_images_.clear();
            frontend_->consume(key);
            start_menu_audio(installation_.load_menu_music());
        }
        return;
    }
    if (flight_ && frontend_->state().mode != FrontendMode::settings) {
        if (key.scan == 0x01) {
            const auto background = flight_->begin_settings();
            menu_image_.palette = background.palette;
            frontend_->open_flight_settings(settings_, background);
            audio_->suspend_engine();
            audio_->set_music_level(MusicLevel::off);
        }
        return;
    }
    const auto previous_mode = frontend_->state().mode;
    const auto request = frontend_->consume(key);
    if (previous_mode == FrontendMode::overview && frontend_->state().mode != FrontendMode::overview) {
        overview_player_.reset();
        overview_.reset();
        intermediate_images_.clear();
    }
    if (previous_mode == FrontendMode::debrief && frontend_->state().mode == FrontendMode::main) {
        start_menu_audio(installation_.load_menu_music());
    }
    handle_frontend_request(request);
}

void GameSession::handle_frontend_request(FrontendRequest request) {
    if (request == FrontendRequest::launch_mission) {
        FlightMission mission{frontend_->gameplay_setup(), frontend_->mission_presentation().resources, settings_.record()};
        mission.depth_padding = depth_padding_;
        mission.back_page = menu_back_page_;
        mission.presentation_detail = presentation_detail_;
        flight_ = std::make_unique<TrainingFlight>(installation_, std::move(mission), audio_, session_irq_.random_seed, reflections_, effects_, &session_irq_);
    } else if (request == FrontendRequest::overview) {
        auto movie = std::make_unique<KdvMovie>(installation_.load_overview());
        frontend_->open_requested_page();
        overview_ = std::move(movie);
        overview_player_ = std::make_unique<KdvPlayer>(*overview_, audio_->execution_clock());
        overview_player_->prepare();
    } else if (request == FrontendRequest::demo) {
        cinematics_ = std::make_unique<DemoCinematics>(installation_.load_demo_cinematics());
        frontend_->open_requested_page();
        demo_index_ = 0;
        start_demo_cycle();
    } else if (request == FrontendRequest::statistics || request == FrontendRequest::credits ||
               request == FrontendRequest::mission_locked) {
        frontend_->open_requested_page();
    } else if (request == FrontendRequest::quit_prompt) {
        frontend_->open_settings(settings_);
        audio_->set_music_level(MusicLevel::off);
    } else if (request == FrontendRequest::calibration_unavailable) {
        flight_notice_ = FlightNotice::unavailable_calibration;
    } else if (request == FrontendRequest::input_mode_unavailable) {
        flight_notice_ = FlightNotice::unavailable_input_mode;
    } else if (request == FrontendRequest::settings_changed || request == FrontendRequest::settings_save ||
               request == FrontendRequest::settings_closed || request == FrontendRequest::settings_abort) {
        apply_frontend_settings(request);
    } else if (request == FrontendRequest::exit) {
        quit_ = true;
    } else if (request != FrontendRequest::none) {
        throw std::invalid_argument("This main-menu action has not been recovered");
    }
}

void GameSession::apply_frontend_settings(FrontendRequest request) {
    constexpr std::array<std::size_t, 3> unsupported_fields{27, 41, 42};
    for (const auto field : unsupported_fields) {
        if (settings_.record()[field] != frontend_->settings().record()[field]) {
            throw std::invalid_argument("This display or joystick setting is not yet integrated into native flight");
        }
    }
    const auto previous_music = settings_.music_level();
    settings_ = frontend_->settings();
    if (flight_) {
        flight_->apply_settings(settings_);
    }
    audio_->set_sound_level(settings_.sound_level());
    if (request == FrontendRequest::settings_closed) {
        audio_->set_music_level(settings_.music_level(), MusicChange::loaded);
        if (flight_) {
            audio_->resume_engine(flight_->state().engine_power);
        }
    } else if (previous_music != settings_.music_level()) {
        audio_->set_music_level(settings_.music_level());
    }
    if (request == FrontendRequest::settings_save) {
        if (!settings_writer_) {
            throw std::logic_error("Settings persistence requires a profile writer");
        }
        settings_writer_(settings_.record());
    }
    if (request == FrontendRequest::settings_abort) {
        abort_mission();
    }
}
} // namespace comanchecd
