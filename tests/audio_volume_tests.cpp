#include "audio_volume.hpp"
#include "audio_menu.hpp"
#include "comanchecd/audio_scheduler.hpp"
#include "comanchecd/sound_device.hpp"
#include <algorithm>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        using namespace comanchecd;
        using namespace comanchecd::host;
        constexpr std::int64_t unit = 1 << 13;
        const AudioOutputFrame frame{{3000 * unit, -5000 * unit}, {1000 * unit, 2000 * unit}, {4000, -3000}};
        AudioVolume volume;
        const auto transform = volume.transform();
        require(transform(frame) == frame.mixed, "Unity host volume must preserve native PCM exactly");
        volume.set({100, 0, 100});
        require(transform(frame) == std::array<std::int16_t, 2>{1000, 2000},
                "Music mute must retain digital effects and voice");
        volume.set({100, 100, 0});
        require(transform(frame) == std::array<std::int16_t, 2>{3000, -5000},
                "Effects mute must retain FM music");
        volume.set({50, 50, 100});
        require(transform(frame) == std::array<std::int16_t, 2>{1250, -250},
                "Master gain must follow independent source balancing");
        const AudioOutputFrame clipped{
            {60000 * unit, -60000 * unit}, {-20000 * unit, 20000 * unit}, {32767, -32768}};
        volume.set({50, 100, 100});
        require(transform(clipped) == std::array<std::int16_t, 2>{16383, -16384},
                "Master must attenuate the clipped native mix");
        volume.set({100, 50, 100});
        require(transform(clipped) == std::array<std::int16_t, 2>{10000, -10000},
                "Source volume must recover pre-clipping mix headroom");
        volume.set({0, 100, 100});
        require(transform(frame) == std::array<std::int16_t, 2>{0, 0},
                "Master zero must silence both sources");
        bool rejected = false;
        try {
            volume.set({101, 100, 100});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected && volume.levels().master == 0,
                "Invalid volume must preserve the previous configuration");
        {
            const AudioOutputFrame positive{
                {3000 * unit, 3000 * unit}, {1000 * unit, 1000 * unit}, {4000, 4000}};
            volume.set({100, 20, 80});
            std::atomic<bool> failed{false};
            std::thread producer([&] {
                for (unsigned sample = 0; sample < 200000; ++sample) {
                    const auto value = transform(positive);
                    if (value[0] != value[1] || (value[0] != 1400 && value[0] != 2600)) {
                        failed = true;
                    }
                }
            });
            for (unsigned change = 0; change < 10000; ++change) {
                volume.set(change % 2 ? VolumeLevels{100, 20, 80} : VolumeLevels{100, 80, 20});
            }
            producer.join();
            require(
                !failed,
                "Volume updates must publish all three gains atomically across the host/producer boundary");
        }
        {
            AudioVolume unity, muted({0, 100, 100});
            AudioScheduler original(48000), passed(48000, 20000, unity.transform()),
                silent(48000, 20000, muted.transform());
            std::array<unsigned, 3> completions{};
            const std::array<AudioScheduler*, 3> schedulers{&original, &passed, &silent};
            for (unsigned index = 0; index < schedulers.size(); ++index) {
                auto* scheduler = schedulers[index];
                scheduler->configure(AudioChannel::digital,
                                     {true, 11111, {audio_gain_one, audio_gain_one}, 27600});
                scheduler->configure(AudioChannel::fm, {true, 48000, {12288, 12288}, 0});
                scheduler->arm_dma(std::vector<std::uint8_t>(2048, 160));
                scheduler->set_dma_handler([&, index, scheduler] {
                    ++completions[index];
                    scheduler->arm_dma(std::vector<std::uint8_t>(2048, 192));
                });
                scheduler->run_cycles(10000000);
            }
            const auto pcm = original.take_output();
            require(!pcm.empty() && pcm == passed.take_output(),
                    "Unity output hook must preserve scheduled device PCM");
            const auto silence = silent.take_output();
            require(silence.size() == pcm.size() &&
                        std::all_of(silence.begin(), silence.end(),
                                    [](const auto& frame) { return frame[0] == 0 && frame[1] == 0; }),
                    "Muted output must retain every scheduled sample");
            for (const auto* scheduler : {&passed, &silent}) {
                const auto actual = scheduler->accounting(), expected = original.accounting();
                require(scheduler->cycles() == original.cycles() &&
                            scheduler->dma_consumed() == original.dma_consumed() &&
                            scheduler->dma_remaining() == original.dma_remaining() &&
                            actual.dispatches == expected.dispatches &&
                            actual.terminal_charges == expected.terminal_charges &&
                            actual.io_cycles == expected.io_cycles &&
                            actual.stack_segment_credits == expected.stack_segment_credits,
                        "Presentation volume must preserve CPU accounting and DMA ownership");
            }
            require(completions[0] > 0 && completions[0] == completions[1] &&
                        completions[0] == completions[2],
                    "Muted output must continue to deliver every DMA interrupt");
            SoundDevice native_device(48000), unity_device(48000, unity.transform()),
                muted_device(48000, muted.transform());
            for (unsigned index = 0; index < 4096; ++index) {
                for (auto* device : {&native_device, &unity_device, &muted_device}) {
                    device->queue_digital(static_cast<std::uint8_t>(index % 256));
                }
                require(native_device.render_sample() == unity_device.render_sample() &&
                            muted_device.render_sample() == 0,
                        "The non-scheduled device path must share the exact unity and mute contract");
            }
        }
        AudioMenu menu({});
        menu.command(ControllerMenuCommand::left);
        require(menu.levels().master == 95, "Audio sliders must change in five-percent steps");
        menu.command(ControllerMenuCommand::clear);
        menu.command(ControllerMenuCommand::down);
        menu.command(ControllerMenuCommand::left);
        menu.command(ControllerMenuCommand::down);
        menu.command(ControllerMenuCommand::clear);
        require(menu.levels().master == 0 && menu.levels().music == 95 && menu.levels().effects == 0,
                "Audio sliders must remain independent");
        menu.command(ControllerMenuCommand::down);
        menu.command(ControllerMenuCommand::accept);
        require(menu.take_apply_request() && !menu.take_apply_request(),
                "Apply must expose one complete save request");
        menu.save_result("Synthetic failure");
        require(menu.levels().music == 95 && !menu.close_requested(),
                "Failed saves must retain staged audio settings");
        require(menu.image().pixels != comanchecd::CockpitImage{}.pixels,
                "Audio settings must render a visible host-owned page");
        menu.command(ControllerMenuCommand::back);
        require(menu.close_requested(), "Back must request a safe modal close");
        std::cout << "Audio gain separation, clipping, concurrency and menu checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
