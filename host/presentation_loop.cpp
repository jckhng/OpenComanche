#include "presentation_loop.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>

namespace comanchecd::host {
namespace {
enum class Delivery { queued, barrier };

class SimulationHost final : public FlightHost {
  public:
    explicit SimulationHost(FlightHost& host) : host_(host), detail_(host.presentation_detail()) {}

    PresentationDetail presentation_detail() const override {
        return detail_;
    }
    HostInput poll() override {
        std::lock_guard<std::mutex> lock(mutex_);
        consumed_input_ = received_input_;
        auto result = std::move(input_);
        input_ = {};
        input_.controls = result.controls;
        input_.controls.weapon = WeaponSelection::unchanged;
        input_.cockpit = result.cockpit;
        input_.quit = result.quit;
        result.quit = result.quit || stopping_;
        return result;
    }
    void set_input_context(InputContext context) override {
        if (context_ == context) {
            return;
        }
        post(
            [this, context] {
                host_.set_input_context(context);
                clear_input();
            },
            Delivery::barrier);
        context_ = context;
    }
    void reset_controls() override {
        post(
            [this] {
                host_.reset_controls();
                clear_input();
            },
            Delivery::barrier);
    }
    void present(const CockpitImage& image, const FlightState& state, const std::string& status) override {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto sequence = ++sequence_;
        post_locked(
            [this, image, state, status, sequence] {
                host_.present(image, state, status);
                std::lock_guard<std::mutex> lock(mutex_);
                image_sequence_ = sequence;
            },
            Delivery::barrier, lock);
    }
    void present_snapshot(std::shared_ptr<const SessionPresentation> frame,
                          const std::string& status) override {
        set_input_context(input_context(frame->menu, frame->flying));
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
        frame_ = std::move(frame);
        status_ = status;
        frame_sequence_ = ++sequence_;
    }
    void queue_audio(const std::vector<std::int16_t>& samples) override {
        if (samples.empty()) {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
        const auto epoch = audio_epoch_;
        pending_samples_ += samples.size();
        post_locked(
            [this, samples, epoch] {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (epoch != audio_epoch_) {
                        return;
                    }
                }
                host_.queue_audio(samples);
                const auto queued = host_.queued_audio_samples();
                std::lock_guard<std::mutex> lock(mutex_);
                if (epoch == audio_epoch_) {
                    pending_samples_ -= samples.size();
                    device_samples_ = queued;
                }
            },
            Delivery::queued, lock);
    }
    std::size_t queued_audio_samples() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return device_samples_ + pending_samples_;
    }
    void clear_audio() override {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto epoch = ++audio_epoch_;
        pending_samples_ = device_samples_ = 0;
        post_locked(
            [this, epoch] {
                host_.clear_audio();
                std::lock_guard<std::mutex> lock(mutex_);
                applied_audio_epoch_ = epoch;
                if (epoch == audio_epoch_) {
                    device_samples_ = 0;
                }
            },
            Delivery::barrier, lock);
    }
    void set_playback(Playback value) override {
        post([this, value] { host_.set_playback(value); }, Delivery::barrier);
    }
    void idle() override {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait_for(lock, std::chrono::milliseconds(1),
                          [this] { return stopping_ || received_input_ != consumed_input_; });
    }

    void receive(HostInput input) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto weapon = input.controls.weapon == WeaponSelection::unchanged ? input_.controls.weapon
                                                                                : input.controls.weapon;
        input_.controls = input.controls;
        input_.controls.weapon = weapon;
        input_.cockpit = input.cockpit;
        input_.scan_bytes.insert(input_.scan_bytes.end(), input.scan_bytes.begin(), input.scan_bytes.end());
        input_.keys.insert(input_.keys.end(), input.keys.begin(), input.keys.end());
        input_.pressed.fire |= input.pressed.fire;
        input_.pressed.target |= input.pressed.target;
        input_.pressed.chaff |= input.pressed.chaff;
        input_.pressed.flare |= input.pressed.flare;
        input_.quit |= input.quit;
        input_.escape |= input.escape;
        input_.reset |= input.reset;
        input_.abort_mission |= input.abort_mission;
        input_.key_pressed |= input.key_pressed;
        if (input.pause == PauseAction::pause) {
            input_.pause = PauseAction::pause;
        } else if (input_.pause != PauseAction::pause) {
            if (input.pause == PauseAction::resume) {
                input_.pause = PauseAction::resume;
            } else if (input.pause == PauseAction::toggle) {
                input_.pause = input_.pause == PauseAction::toggle ? PauseAction::none :
                    input_.pause == PauseAction::resume ? PauseAction::pause : PauseAction::toggle;
            }
        }
        ++received_input_;
        changed_.notify_all();
    }
    void service() {
        std::deque<Command> commands;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            commands.swap(commands_);
        }
        for (auto& command : commands) {
            command.action();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                completed_command_ = command.sequence;
            }
            changed_.notify_all();
        }
        std::uint64_t epoch;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            epoch = audio_epoch_;
        }
        const auto queued = host_.queued_audio_samples();
        std::lock_guard<std::mutex> lock(mutex_);
        if (epoch == audio_epoch_ && epoch == applied_audio_epoch_) {
            device_samples_ = queued;
        }
    }
    void render() {
        std::shared_ptr<const SessionPresentation> frame;
        std::string status;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (frame_sequence_ <= image_sequence_) {
                return;
            }
            frame = frame_;
            status = status_;
        }
        if (frame) {
            host_.present_snapshot(std::move(frame), status);
        }
    }
    void finish(std::exception_ptr error) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = error;
        finished_ = true;
        changed_.notify_all();
    }
    bool finished() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return finished_;
    }
    std::exception_ptr error() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return error_;
    }
    void cancel() {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        commands_.clear();
        changed_.notify_all();
    }

  private:
    struct Command {
        std::uint64_t sequence = 0;
        std::function<void()> action;
    };
    void clear_input() {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto quit = input_.quit;
        const auto pause = input_.pause;
        auto scan_bytes = std::move(input_.scan_bytes);
        input_ = {};
        input_.scan_bytes = std::move(scan_bytes);
        input_.quit = quit;
        if (pause == PauseAction::pause || pause == PauseAction::resume) {
            input_.pause = pause;
        }
    }
    void post(std::function<void()> action, Delivery delivery) {
        std::unique_lock<std::mutex> lock(mutex_);
        post_locked(std::move(action), delivery, lock);
    }
    void post_locked(std::function<void()> action, Delivery delivery, std::unique_lock<std::mutex>& lock) {
        if (stopping_) {
            return;
        }
        const auto command = ++next_command_;
        commands_.push_back({command, std::move(action)});
        if (delivery == Delivery::barrier) {
            changed_.wait(lock, [this, command] { return completed_command_ >= command || stopping_; });
        }
    }

    FlightHost& host_;
    PresentationDetail detail_;
    std::optional<InputContext> context_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    HostInput input_;
    std::deque<Command> commands_;
    std::shared_ptr<const SessionPresentation> frame_;
    std::string status_;
    std::exception_ptr error_;
    std::uint64_t next_command_ = 0, completed_command_ = 0;
    std::uint64_t sequence_ = 0, frame_sequence_ = 0, image_sequence_ = 0;
    std::uint64_t audio_epoch_ = 0, applied_audio_epoch_ = 0;
    std::uint64_t received_input_ = 0, consumed_input_ = 0;
    std::size_t device_samples_ = 0, pending_samples_ = 0;
    bool stopping_ = false, finished_ = false;
};
} // namespace

void run_presentation(FlightHost& host, const std::function<void(FlightHost&)>& simulate) {
    SimulationHost bridge(host);
    std::thread simulation([&] {
        std::exception_ptr error;
        try {
            simulate(bridge);
        } catch (...) {
            error = std::current_exception();
        }
        bridge.finish(error);
    });
    std::exception_ptr error;
    try {
        while (!bridge.finished()) {
            bridge.service();
            auto input = host.poll();
            if (input.quit) {
                break;
            }
            bridge.receive(std::move(input));
            bridge.render();
            host.idle();
        }
    } catch (...) {
        error = std::current_exception();
    }
    bridge.cancel();
    simulation.join();
    if (!error) {
        error = bridge.error();
    }
    try {
        host.set_playback(Playback::paused);
    } catch (...) {
        if (!error) {
            error = std::current_exception();
        }
    }
    try {
        host.clear_audio();
    } catch (...) {
        if (!error) {
            error = std::current_exception();
        }
    }
    if (error) {
        std::rethrow_exception(error);
    }
}
} // namespace comanchecd::host
