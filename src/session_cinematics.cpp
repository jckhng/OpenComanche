#include "comanchecd/session.hpp"
#include "startup_storage.hpp"

#include <algorithm>
#include <stdexcept>

namespace comanchecd {
namespace {
constexpr std::size_t title_row = 24, title_hold_frames = 40, hangar_loops = 2;
constexpr std::uint64_t fanfare_wait_irqs = 301, title_wait_irqs = 11;

CockpitImage demo_background(const FrontendPicture& picture) {
    CockpitImage image;
    image.palette = picture.palette;
    for (std::size_t y = 0; y < cockpit_height; ++y) {
        for (std::size_t x = 0; x < cockpit_width; ++x) {
            image.pixels[y * cockpit_width + x] = picture.background[x % plane_count].at(1 + y * line_stride + x / plane_count);
        }
    }
    return image;
}
}
void GameSession::begin_startup() {
    cinematics_ = std::make_unique<DemoCinematics>(installation_.load_demo_cinematics());
    start_menu_audio({cinematics_->fanfare, {}});
    const auto fanfare = installation_.load_fanfare();
    audio_->start_fanfare(fanfare);
    demo_index_ = 1;
    demo_phase_ = DemoPhase::fanfare;
    menu_image_ = demo_background(cinematics_->backdrop);
    const auto startup_movie = installation_.load_startup_movie();
    auto padding = startup_depth_padding(fanfare.score, startup_movie);
    demo_wait_ = audio_->load_startup_movie(installation_.load_startup_files(), startup_movie) + fanfare_wait_irqs;
    depth_padding_ = std::make_shared<const std::vector<std::uint8_t>>(std::move(padding));
}

void GameSession::finish_startup() {
    release_flight();
    demo_player_.reset();
    cinematics_.reset();
    demo_phase_ = DemoPhase::none;
    startup_ = startup_skip_ = false;
    demo_wait_ = 0;
    demo_clear_sample_ = 0;
    audio_->enter_pilot_menu(installation_.load_pilot_menu());
    audio_->set_music_level(settings_.music_level(), MusicChange::loaded);
    start_frontend();
}

void GameSession::begin_fanfare_movie() {
    auto& clock = audio_->execution_clock();
    clock.execute(CpuOperation::instructions, 2); // 5bd8 row and hold arguments.
    demo_wait_ = 0;
    demo_phase_ = DemoPhase::animation;
    demo_player_ = std::make_unique<KdvPlayer>(cinematics_->animation, clock);
    clock.execute(CpuOperation::instructions, 3);
    audio_->enter_cinematic(cinematics_->sound_bank, {{}, cinematics_->canyon_music},
                            CinematicLoopStart::after_page_clear);
    demo_clear_sample_ = demo_player_->clear_pages();
    audio_->start_cinematic_loop();
    clock.execute(CpuOperation::instructions, 1 + 4); // Cue index and 19512 input sample.
    demo_sound();
    clock.execute(CpuOperation::instructions, 6);
    clock.instruction([&] { startup_skip_ = startup_skip_ || keyboard_.pending_scan() != 0; });
    clock.execute(CpuOperation::instructions, 1);
    if (startup_ && startup_skip_) { finish_startup(); return; }
    demo_player_->prepare();
}

void GameSession::start_demo_cycle() {
    demo_index_ = (demo_index_ + 1) % 4;
    demo_cue_ = demo_loops_ = 0;
    demo_clear_sample_ = 0;
    demo_player_.reset();
    if ((demo_index_ & 1) != 0) {
        demo_phase_ = DemoPhase::fanfare;
        menu_image_ = demo_background(cinematics_->backdrop);
        audio_->leave_mission({cinematics_->fanfare, {}});
    } else {
        demo_phase_ = DemoPhase::hangar;
        menu_image_ = demo_background(cinematics_->hangar_backdrop);
        audio_->leave_mission({{}, cinematics_->hangar_music});
    }
    audio_->set_music_level(settings_.music_level(), MusicChange::loaded);
    audio_->set_sound_level(settings_.sound_level());
    if (demo_phase_ == DemoPhase::fanfare) {
        demo_wait_ = audio_->irq_ticks() + fanfare_wait_irqs;
    } else {
        demo_player_ = std::make_unique<KdvPlayer>(cinematics_->hangar, audio_->execution_clock());
        demo_player_->prepare();
    }
}

void GameSession::start_demo_flight() {
    const auto frontend = installation_.load_frontend();
    auto resources = frontend.mission_resources;
    for (std::size_t index = 11; index < resources.size(); ++index) {
        resources[index][0] = '-';
    }
    const auto mission = installation_.load_missions(0).missions.at(demo_index_);
    std::copy(mission.resources.begin(), mission.resources.end(), resources.begin());
    FlightMission selected{parse_gameplay(mission, resources, frontend.gameplay_presets, {}), resources,
                           settings_.record(), std::make_shared<DemoRecording>(installation_.load_demo(demo_index_))};
    selected.depth_padding = depth_padding_;
    selected.presentation_detail = presentation_detail_;
    flight_ = std::make_unique<TrainingFlight>(installation_, std::move(selected), audio_, session_irq_.random_seed, reflections_, effects_);
    demo_phase_ = DemoPhase::flight;
    demo_player_.reset();
}

void GameSession::demo_sound() {
    const auto sound = cinematics_->cues.at(demo_cue_++) & 15;
    if (sound != 0) {
        try {
            audio_->play_effect(sound);
        } catch (const std::invalid_argument& error) {
            throw std::invalid_argument("Demo cue " + std::to_string(demo_cue_ - 1) + ": " + error.what());
        }
    }
}

void GameSession::demo_movie_boundary() {
    if (startup_ && startup_skip_) { finish_startup(); return; }
    const auto hangar = demo_phase_ == DemoPhase::hangar;
    const auto& movie = hangar ? cinematics_->hangar :
        demo_phase_ == DemoPhase::animation ? cinematics_->animation : cinematics_->title;
    const auto& pixels = demo_player_->pixels();
    const auto row = hangar ? 0 : title_row;
    if (row + movie.height() > cockpit_height) {
        throw std::invalid_argument("Demo movie exceeds presentation surface");
    }
    std::copy(pixels.begin(), pixels.end(), menu_image_.pixels.begin() + row * cockpit_width);
    menu_image_.palette = movie.palette();
    intermediate_images_.push_back(menu_image_);
    if (!demo_player_->finished()) {
        if (!hangar) {
            demo_sound();
        }
        demo_player_->prepare();
        return;
    }
    if (hangar && ++demo_loops_ < hangar_loops) {
        demo_player_ = std::make_unique<KdvPlayer>(movie, audio_->execution_clock());
        demo_player_->prepare();
    } else if (demo_phase_ == DemoPhase::animation) {
        demo_phase_ = DemoPhase::title_wait;
        demo_wait_ = audio_->irq_ticks() + title_wait_irqs;
        demo_player_.reset();
    } else {
        start_demo_flight();
    }
}

std::vector<std::int16_t> GameSession::render_presentation_audio(std::size_t samples) {
    std::vector<std::int16_t> result;
    result.reserve(samples);
    intermediate_images_.clear();
    for (std::size_t index = 0; index < samples; ++index) {
        auto sample = flight_ ? flight_->render_audio(1) : (startup_ || hardware_input_) ?
            audio_->render_foreground(1, [this] { poll_input(); }) : audio_->render(1);
        result.insert(result.end(), sample.begin(), sample.end());
        ++elapsed_samples_;
        if (demo_clear_sample_ != 0 && elapsed_samples_ >= demo_clear_sample_) {
            menu_image_.pixels.fill(0);
            intermediate_images_.push_back(menu_image_);
            demo_clear_sample_ = 0;
        }
        if (flight_) {
            continue;
        }
        if (!overview_ && !demo_playing()) { continue; }
        if (overview_player_) {
            if (elapsed_samples_ >= overview_player_->ready_sample()) {
                const auto& pixels = overview_player_->pixels();
                std::copy(pixels.begin(), pixels.end(), menu_image_.pixels.begin());
                menu_image_.palette = overview_->palette();
                intermediate_images_.push_back(menu_image_);
                if (overview_player_->finished()) {
                    overview_player_ = std::make_unique<KdvPlayer>(*overview_, audio_->execution_clock());
                }
                overview_player_->prepare();
            }
            continue;
        }
        if (demo_player_) {
            if (elapsed_samples_ >= demo_player_->ready_sample()) {
                demo_movie_boundary();
            }
            continue;
        }
        if (audio_->irq_ticks() < demo_wait_) {
            continue;
        }
        demo_wait_ = 0;
        if (demo_phase_ == DemoPhase::fanfare) {
            begin_fanfare_movie();
            continue;
        } else {
            demo_phase_ = DemoPhase::title;
            demo_player_ = std::make_unique<KdvPlayer>(cinematics_->title, audio_->execution_clock(), title_hold_frames);
        }
        demo_sound();
        if (startup_ && startup_skip_) { finish_startup(); continue; }
        demo_player_->prepare();
    }
    return result;
}
} // namespace comanchecd
