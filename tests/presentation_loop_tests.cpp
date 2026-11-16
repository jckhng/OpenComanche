#include "presentation_loop.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
using namespace comanchecd;
using namespace comanchecd::host;
constexpr unsigned minimum_presentations = 12;

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

class DeviceHost final : public FlightHost {
  public:
    HostInput poll() override {
        owner();
        HostInput input;
        input.quit = stop_after_images && images >= stop_after_images;
        if (presents == 1 && !pressed_) {
            pressed_ = true;
            input.scan_bytes = {0x39};
            input.pressed.fire = true;
            input.controls.fire = true;
            input.controls.weapon = WeaponSelection::next;
            input.pause = PauseAction::toggle;
        } else if (presents == 2 && !released_) {
            released_ = true;
            input.scan_bytes = {0xb9};
            input.pause = forced_pause ? PauseAction::pause : PauseAction::toggle;
        }
        return input;
    }
    PresentationDetail presentation_detail() const override {
        return PresentationDetail::spatial;
    }
    void set_input_context(InputContext context) override {
        owner();
        context_ = context;
    }
    void present(const CockpitImage&, const FlightState&, const std::string&) override {
        owner();
        ++images;
    }
    void present_snapshot(std::shared_ptr<const SessionPresentation> frame, const std::string&) override {
        owner();
        if (fail_present) {
            throw std::runtime_error("Device presentation failed");
        }
        if (frame != presented_) {
            require(context_ == input_context(frame->menu, frame->flying),
                    "Input context must be applied before a new snapshot reaches the display");
            presented_ = std::move(frame);
        }
        presents.fetch_add(1);
        changed.notify_all();
    }
    void queue_audio(const std::vector<std::int16_t>& samples) override {
        owner();
        audio.insert(audio.end(), samples.begin(), samples.end());
        delivered.insert(delivered.end(), samples.begin(), samples.end());
    }
    std::size_t queued_audio_samples() const override {
        owner();
        return audio.size();
    }
    void clear_audio() override {
        owner();
        audio.clear();
        ++clears;
    }
    void set_playback(Playback value) override {
        owner();
        playback = value;
        if (fail_pause && value == Playback::paused) {
            throw std::runtime_error("Device pause failed");
        }
    }
    void reset_controls() override {
        owner();
        ++resets;
    }
    void idle() override {
        owner();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    void wait_for_frames() {
        std::unique_lock<std::mutex> lock(mutex_);
        require(changed.wait_for(lock, std::chrono::seconds(2),
                                 [&] { return presents >= minimum_presentations; }),
                "Presentation must continue while the simulation is blocked");
    }
    void owner() const {
        require(std::this_thread::get_id() == owner_, "Device calls must stay on their owner thread");
    }

    std::atomic<unsigned> presents{0};
    std::condition_variable changed;
    std::vector<std::int16_t> audio;
    std::vector<std::int16_t> delivered;
    Playback playback = Playback::paused;
    unsigned images = 0, clears = 0, resets = 0;
    unsigned stop_after_images = 0;
    bool fail_present = false, forced_pause = false, fail_pause = false;

  private:
    std::thread::id owner_ = std::this_thread::get_id();
    std::mutex mutex_;
    InputContext context_ = InputContext::menu;
    std::shared_ptr<const SessionPresentation> presented_;
    bool pressed_ = false, released_ = false;
};
} // namespace

int main() {
    try {
        DeviceHost host;
        run_presentation(host, [&](FlightHost& worker) {
            require(worker.presentation_detail() == PresentationDetail::spatial,
                    "Worker must retain requested observations");
            worker.set_input_context(InputContext::flight);
            worker.set_playback(Playback::running);
            worker.queue_audio({11, 12});
            worker.clear_audio();
            worker.queue_audio({21, 22, 23});
            require(worker.queued_audio_samples() == 3, "Audio clear must discard preceding queue ownership");
            auto frame = std::make_shared<SessionPresentation>();
            frame->flying = true;
            worker.present_snapshot(frame, "Blocked simulation");
            host.wait_for_frames();
            require(worker.queued_audio_samples() == 3,
                    "Delivered and pending sample counts must share queue ownership");
            const auto input = worker.poll();
            require(input.scan_bytes == std::vector<std::uint8_t>{0x39, 0xb9} && input.pressed.fire &&
                        !input.controls.fire && input.controls.weapon == WeaponSelection::next &&
                        input.pause == PauseAction::none,
                    "Press/release order and tapped commands must survive a blocked simulation update");
            require(!worker.poll().pressed.fire, "Buffered taps must be consumed once");
            worker.present({}, {}, "Ordered intermediate image");
            worker.reset_controls();
        });
        require(host.presents >= minimum_presentations && host.images == 1 && host.resets == 1,
                "Ordered image and reset barriers must complete on the device thread");
        require(host.audio.empty() && host.playback == Playback::paused,
                "Shutdown must stop and clear the device queue");
        require(host.delivered.size() >= 3 &&
                    std::vector<std::int16_t>(host.delivered.end() - 3, host.delivered.end()) ==
                        std::vector<std::int16_t>{21, 22, 23},
                "The retained audio generation must reach the device in sample order");

        DeviceHost transitions;
        transitions.forced_pause = true;
        run_presentation(transitions, [&](FlightHost& worker) {
            worker.set_input_context(InputContext::flight);
            auto frame = std::make_shared<SessionPresentation>();
            frame->flying = true;
            worker.present_snapshot(frame, "Input transition");
            transitions.wait_for_frames();
            worker.set_input_context(InputContext::menu);
            const auto input = worker.poll();
            require(
                input.scan_bytes == std::vector<std::uint8_t>{0x39, 0xb9} && !input.pressed.fire &&
                    input.pause == PauseAction::pause,
                "Context changes must retain DOS key releases and forced pause while clearing logical taps");
        });

        DeviceHost settings;
        run_presentation(settings, [&](FlightHost& worker) {
            worker.set_input_context(InputContext::flight);
            auto frame = std::make_shared<SessionPresentation>();
            frame->flying = true;
            frame->menu.mode = FrontendMode::settings;
            worker.present_snapshot(frame, "Settings transition");
            settings.wait_for_frames();
        });

        DeviceHost failing;
        failing.fail_present = true;
        bool device_error = false;
        try {
            run_presentation(failing, [&](FlightHost& worker) {
                worker.present_snapshot(std::make_shared<SessionPresentation>(), "Failing device");
                while (!worker.poll().quit) {
                    worker.idle();
                }
            });
        } catch (const std::runtime_error& error) {
            device_error = std::string(error.what()) == "Device presentation failed";
        }
        require(device_error && failing.audio.empty(), "Device failures must cancel and join the simulation");

        DeviceHost broken;
        bool worker_error = false;
        try {
            run_presentation(broken, [](FlightHost&) { throw std::runtime_error("Simulation failed"); });
        } catch (const std::runtime_error& error) {
            worker_error = std::string(error.what()) == "Simulation failed";
        }
        require(worker_error && broken.audio.empty(),
                "Simulation failures must propagate after device cleanup");

        DeviceHost cleanup;
        cleanup.fail_pause = true;
        bool original_error = false;
        try {
            run_presentation(cleanup, [](FlightHost& worker) {
                worker.queue_audio({31, 32});
                worker.present({}, {}, "Delivered audio before failure");
                throw std::runtime_error("Original simulation failure");
            });
        } catch (const std::runtime_error& error) {
            original_error = std::string(error.what()) == "Original simulation failure";
        }
        require(original_error && cleanup.audio.empty() && cleanup.clears != 0,
                "Cleanup failures must preserve the original error and still clear queued audio");

        DeviceHost closing;
        closing.stop_after_images = 2;
        run_presentation(closing, [](FlightHost& worker) {
            while (!worker.poll().quit) {
                worker.present({}, {}, "Closing during a barrier");
            }
        });
        require(closing.images == 2 && closing.audio.empty(),
                "Window close must release outstanding presentation barriers before joining");
        std::cout << "Independent presentation, owner-thread devices, input buffering, audio epochs and "
                     "shutdown passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
