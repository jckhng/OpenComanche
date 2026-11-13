#pragma once

#include "comanchecd/audio.hpp"
#include "comanchecd/sound_device.hpp"
#include "comanchecd/audio_scheduler.hpp"
#include "audio_decode.hpp"
#include "startup_timing.hpp"
#include "loading_work.hpp"

#include <deque>

namespace comanchecd {
namespace audio_detail {
enum class MusicEntry { external, continuation };
enum class MusicRepeat { once, loop };
constexpr std::size_t echo_samples = 32;
constexpr std::size_t engine_sound = 3;
constexpr std::size_t launch_radio = 6;
constexpr std::uint16_t cinematic_tail_sound = 13;
constexpr std::uint32_t cinematic_tail_offset = 0x7f7f8184;
constexpr std::size_t unmapped_sample_length = 0xffff;
constexpr std::size_t initialized_music_channels = 10;
constexpr unsigned dma_half_bytes = 2048;
constexpr std::uint8_t dsp_pause_command = 0xd0, dsp_single_cycle_command = 0x14;
constexpr unsigned producer_setup_instructions = 5;
constexpr unsigned producer_sample_instructions = 44;
constexpr unsigned voice_reload_instructions = 17;
constexpr unsigned changed_voice_instructions = 2;
constexpr unsigned dos_cycles_per_second = 20000000;
constexpr std::uint8_t loud_music_volume = 250, soft_music_volume = 115;
constexpr std::uint8_t loaded_soft_music_volume = 100;
constexpr std::array<std::uint8_t, 16> music_channels{0, 1, 2, 3, 4, 5, 6, 7, 99, 8, 99, 99, 99, 99, 99, 99};
constexpr std::array<std::uint8_t, 9> modulator{0, 1, 2, 8, 9, 10, 16, 17, 18};
constexpr std::array<std::uint8_t, 9> carrier{3, 4, 5, 11, 12, 13, 19, 20, 21};
constexpr std::array<std::uint8_t, 8> sample_bias{0, 64, 96, 112, 120, 124, 126, 126};
}

class MissionAudio::Impl final : public ExecutionClock, public KeyboardPort {
    using Sample = audio_detail::Sample;
    using MusicEntry = audio_detail::MusicEntry;
    using MusicRepeat = audio_detail::MusicRepeat;

public:
    Impl(AudioTables tables, const std::vector<GameplayPayload>& payloads, AudioEntry entry,
         std::vector<SampleRequest> requests, AudioTracePolicy trace_policy);

    Impl(AudioTables tables, MusicPlaylist playlist, AudioEntry entry, AudioTracePolicy trace_policy);

    void set_music_level(MusicLevel level, MusicChange change);

    void set_sound_level(SoundLevel level);
    void set_interrupt_handler(std::function<void()> handler);
    void set_keyboard_handler(std::function<void(ExecutionClock&, KeyboardPort&)> handler);
    void queue_key_byte(std::uint8_t scan);
    void schedule_key_byte(std::uint64_t cycle, std::uint8_t scan);
    std::uint8_t read() override;
    void acknowledge() override;
    std::uint64_t add_interrupt_handler(std::function<void()> handler);
    void remove_interrupt_handler(std::uint64_t token);

    void change_music(MusicPlaylist playlist);

    void enter_mission(const std::vector<GameplayPayload>& payloads, std::uint8_t power);

    void begin_mission_loading();

    void prepare_mission_audio(const std::vector<GameplayPayload>& payloads, const StartupFiles& files);

    void queue_initial_engine(std::uint8_t control_power);

    void play_launch_radio();

    void stop_mission_samples();

    void clear_mission_samples();

    void save_pilot_record();

    void present_debrief(MusicPlaylist playlist, unsigned height);

    GameplayPayload load_mission_payload(const StartupResource& resource, PayloadKind kind, unsigned filename_bytes);

    void load_menu_picture(const StartupResource& resource, unsigned filename_bytes);

    void load_campaign(const StartupCampaign& campaign);

    void stop_music();

    void start_fanfare(const FanfareAssets& assets);

    std::uint64_t load_startup_movie(const StartupFiles& files, const StartupMovieAssets& assets);

    void enter_pilot_menu(const PilotMenuAssets& assets);

    void read_file(unsigned bytes) override;

    void suspend_engine();

    void enter_cinematic(const std::vector<std::uint8_t>& bank, MusicPlaylist playlist, CinematicLoopStart loop);

    void start_cinematic_loop();

    void leave_mission(MusicPlaylist playlist);

    void resume_engine(std::uint8_t power);

    std::vector<std::int16_t> take_pending_audio();

    std::vector<std::int16_t> render_foreground(std::size_t count, const std::function<void()>& foreground);

    std::vector<std::int16_t> render(std::size_t count, const std::function<void()>& interrupt);

    const std::vector<OplWrite>& writes() const;
    const std::vector<MusicIrqWork>& music_irq_work() const;
    const std::vector<std::uint8_t>& digital_pcm() const;
    const std::vector<SampleRequest>& sample_requests() const;
    std::uint64_t irq_ticks() const;

    ExecutionClock& execution_clock();

    void execute(CpuOperation operation, unsigned count) override;
    void instruction(const std::function<void()>& action) override;
    bool read_retrace() override;
    std::uint64_t cycles() const override;
    std::uint64_t sample_irq_ticks() override;

    void play_effect(std::uint16_t sound, std::uint8_t attenuation,
                     EffectPolicy policy = EffectPolicy::available_first, std::uint32_t step = 0);

    void set_engine_power(std::uint8_t power);

    void play_cue(const SoundCue& cue);

private:
    bool voice_matches(std::size_t index, const Sample* sample);

    void finish_sample_dispatch();

    void dispatch_sample(const SampleRequest& request);

    unsigned reset_silence_voices();

    const Sample* effect_sample(std::uint16_t sound) const;
    void apply_loaded_music_level(MusicLevel level);

    void startup_digital(DigitalCommand command);

    void initialize(const AudioEntry& entry);

    void initialize_cold_fm();

    struct Voice {
        const Sample* current = nullptr;
        const Sample* next = nullptr;
        std::uint32_t position = 4u << 16;
        std::uint32_t current_step = 0, next_step = 0;
        std::uint8_t shift = 0, next_shift = 0;
        bool reload_pending = false;
    };

    void write(std::uint8_t reg, std::uint8_t value);

    void apply_request(const SampleRequest& request);

    void music_work(unsigned instructions);
    void notify_interrupt();
    void music_cpu_work(const std::function<void()>& action);

    void program(std::size_t channel, std::uint8_t instrument);

    void select_program(std::size_t channel, std::uint8_t instrument);

    void note(std::size_t channel, std::uint8_t pitch, std::uint8_t velocity);

    void silence_music();

    void start_music(MusicEntry entry = MusicEntry::external, MusicPlaylist* incoming = nullptr,
                     MusicRepeat repeat = MusicRepeat::once);

    std::uint8_t byte();

    enum class MusicEventResult { scheduled, stream_end };

    MusicEventResult event();

    void channel_link(std::size_t channel, std::uint8_t pitch, std::uint8_t velocity);

    void music_tick();

    void poll_dsp();

    void begin_dma();

    void start_dma();

    void produce_dma();

    std::vector<std::int16_t> render_scheduled(std::size_t count, const std::function<void()>& interrupt,
                                            const std::function<void()>& foreground = {});

    std::uint8_t mix_sample(std::uint64_t* work = nullptr);

    AudioTables tables_;
    std::unique_ptr<AudioScheduler> scheduler_;
    AudioChannelConfig digital_config_;
    bool dma_running_ = false;
    std::array<std::vector<std::uint8_t>, 2> dma_buffers_;
    unsigned producer_half_ = 0, opl_delay_iterations_ = 0;
    std::uint64_t scheduled_target_ = 0, scheduled_phase_ = 0;
    std::deque<std::int16_t> scheduled_output_;
    std::function<void()> active_interrupt_;
    std::function<void()> timer_observer_;
    SoundDevice device_;
    IrqClock irq_clock_;
    std::uint64_t tick_ = 0, digital_phase_ = 0;
    std::uint8_t digital_value_ = 128;
    std::vector<Sample> effects_, speech_, announcements_;
    std::unique_ptr<Sample> cinematic_tail_;
    Sample silence_;
    std::array<Voice, 3> voices_{};
    std::array<std::uint8_t, audio_detail::echo_samples> echo_{};
    std::size_t echo_cursor_ = 0;
    std::vector<std::uint8_t> music_, next_music_, digital_;
    bool loop_music_ = true, music_active_ = true, music_muted_ = false, digital_enabled_ = true;
    bool music_callback_installed_ = true;
    bool cold_music_pending_ = false;
    SoundLevel sound_level_ = SoundLevel::loud;
    std::vector<OplWrite> writes_;
    std::vector<MusicIrqWork> music_irq_work_;
    unsigned music_work_depth_ = 0;
    std::uint64_t interrupt_token_ = 0;
    std::vector<std::pair<std::uint64_t, std::function<void()>>> interrupt_handlers_;
    std::vector<std::pair<CpuOperation, unsigned>> irq_state_work_;
    bool collecting_irq_work_ = false;
    bool startup_stop_pending_ = false, startup_movie_pending_ = false;
    bool startup_menu_pending_ = false, cinematic_loop_pending_ = false;
    loading_work::InputBuffer mission_input_{};
    std::array<std::uint8_t, audio_detail::music_channels.size()> programs_{}, cached_programs_{}, initial_programs_{}, current_pitch_{}, last_pitch_{}, pitch_bend_{};
    std::array<std::size_t, audio_detail::music_channels.size()> envelope_positions_{};
    std::array<std::uint8_t, 9> frequency_high_{}, level_flags_{};
    std::uint8_t music_volume_ = 250;
    std::size_t position_ = 0;
    std::uint16_t wait_ = 1;
    AudioTracePolicy trace_policy_;
    std::uint64_t digital_samples_ = 0;
    std::vector<SampleRequest> requests_;
    std::vector<SampleRequest> resolved_requests_;
    std::size_t request_cursor_ = 0;
};
}
