#include "openxr_host.hpp"
#include "openxr_graphics.hpp"
#include "cockpit_scaling.hpp"
#include "presentation_mailbox.hpp"
#include <atomic>
#include <future>
#include <mutex>
#include <thread>
#include <chrono>
#include <optional>
#include <fstream>

namespace comanchecd::host {
namespace {
SdlHostOptions desktop_options(SdlHostOptions options) {
    options.pacing = DisplayPacing::immediate;
    // The XR worker owns headset pacing, including profiles saved by the desktop player.
    options.frame_limit = {FrameLimitMode::unlimited};
    options.renderer = DesktopRenderer::cpu;
    options.cockpit = CockpitPresentation::flat;
    return options;
}
}
class OpenXrHost::Impl {
public:
    Impl(SdlHostOptions options, OpenXrOptions xr) : desktop_(desktop_options(options)), options_(options), xr_(xr),
        loader_(xr.loader), runtime_(loader_.entry()), graphics_(runtime_.capabilities()) {
        if (!xr.enabled || options.input != InputProfile::enhanced) { throw std::invalid_argument("XR requires enhanced mode"); }
        if (xr.renderer == XrRenderer::cpu && options.cockpit == CockpitPresentation::spatial) { cockpit_ = std::make_unique<SpatialCockpit>(); }
        if (!xr.profile.empty()) {
            profile_.open(xr.profile, std::ios::out | std::ios::trunc);
            if (!profile_) { throw std::runtime_error("Cannot open XR profiling CSV"); }
            profile_ << "frame,renderer,scene_serial,width,height,eyes,period_ms,wait_ms,image_wait_ms,render_ms,end_ms,total_ms\n";
        }
        graphics_.release();
        std::promise<void> ready;
        auto initialized = ready.get_future();
        worker_ = std::thread([this, ready = std::move(ready)]() mutable { run(ready); });
        try { initialized.get(); }
        catch (...) { stop_ = true; worker_.join(); throw; }
    }
    ~Impl() {
        stop_ = true;
        if (worker_.joinable()) { worker_.join(); }
    }
    HostInput poll() {
        {
            std::lock_guard<std::mutex> lock(error_mutex_);
            if (error_) { std::rethrow_exception(error_); }
        }
        const bool focused = focused_.load();
        desktop_.set_focus(focused ? HostFocus::active : HostFocus::inactive);
        auto input = desktop_.poll();
        if (!focused) { input.pause = PauseAction::pause; }
        input.quit = input.quit || exit_.load();
        return input;
    }
    double time() const {
        const auto now = playback_ == Playback::running ? Clock::now() : paused_at_;
        return std::chrono::duration<double>(now - epoch_ - paused_duration_).count();
    }
    void update_clock() {
        const auto value = requested_playback_.load();
        if (playback_ != value) {
            if (value == Playback::paused) { paused_at_ = Clock::now(); }
            else { paused_duration_ += Clock::now() - paused_at_; }
            playback_ = value;
        }
    }
    void playback(Playback value) {
        requested_playback_ = value;
        desktop_.set_playback(value);
    }
    void present(std::shared_ptr<const SessionPresentation> frame, const std::string& status) {
        desktop_.present(frame->image, frame->flight, status);
        mailbox_.publish(std::move(frame));
    }
    SdlFlightHost desktop_;
private:
    void run(std::promise<void>& ready) {
        bool initialized = false;
        try {
            graphics_.activate();
            auto session = graphics_.create_session(runtime_, xr_.render_scale);
            ready.set_value();
            initialized = true;
            while (!stop_) {
                const auto status = session->poll();
                focused_ = status.focused;
                exit_ = status.exit_requested;
                if (status.exit_requested) { break; }
                update_clock();
                const auto frame = mailbox_.read();
                if (!frame || !status.running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                render(*session, frame);
            }
            session.reset();
        } catch (...) {
            if (!initialized) { ready.set_exception(std::current_exception()); }
            else { std::lock_guard<std::mutex> lock(error_mutex_); error_ = std::current_exception(); }
        }
        try { graphics_.release(); } catch (...) {}
    }
    void render(OpenXrSession& session, const std::shared_ptr<const SessionPresentation>& frame) {
        const bool spatial = options_.scene_rendering == SceneRendering::spatial && spatial_presentation(*frame, options_.cockpit);
        const bool body = spatial && options_.cockpit == CockpitPresentation::spatial && frame->spatial->cockpit_mode == 1;
        if (spatial) { history_.observe(frame->spatial, frame->cpu_cycles, time()); }
        else { history_.reset(); }
        std::optional<SpatialCamera> camera;
        EntityPoses entities{};
        const auto started = Clock::now();
        unsigned eye_width = 0, eye_height = 0;
        session.submit(body ? OpenXrScene::cockpit : spatial ? OpenXrScene::world : OpenXrScene::panel,
            [&](const OpenXrEye& eye) {
                eye_width = eye.target.width; eye_height = eye.target.height;
                if (!origin_ || tracking_epoch_ != eye.tracking_epoch) {
                    origin_ = eye.head;
                    tracking_epoch_ = eye.tracking_epoch;
                }
                const auto display_time = time();
                if (!camera) {
                    camera = options_.interpolation == CameraInterpolation::enabled ?
                        history_.sample(display_time) : spatial_camera(frame->spatial->camera);
                    entities = options_.interpolation == CameraInterpolation::enabled ?
                        history_.sample_entities(display_time) : entity_poses(*frame->spatial);
                }
                const auto base = camera_view(*camera, eye.target.width, eye.target.height);
                const auto tracked = relative_pose(*origin_, eye.pose);
                const auto view = eye_view(base, tracked, eye.frustum, xr_.world_scale);
                if (xr_.renderer == XrRenderer::gpu) {
                    graphics_.render_eye(eye.target, *frame->spatial, view, entities, frame->image,
                        body ? std::optional<CameraView>(cockpit_view(tracked, eye.frustum)) : std::nullopt);
                    return;
                }
                const auto& world = renderer_.render_world(*frame->spatial, view, entities,
                    frame->image.palette, eye.target.width, eye.target.height);
                if (body) {
                    cockpit_image_ = world;
                    cockpit_->draw(cockpit_image_, cockpit_view(tracked, eye.frustum), frame->image, *frame->spatial);
                    graphics_.upload(eye.target, cockpit_image_);
                } else { graphics_.upload(eye.target, world); }
            },
            [&](const OpenXrTarget& target) {
                SpatialImage panel{target.width, target.height,
                    std::vector<std::uint32_t>(std::size_t(target.width) * target.height, 0), {}};
                const auto mask = spatial && !body ? frame->spatial->scene_mask :
                    std::vector<std::uint8_t>(frame->image.pixels.size(), 0);
                composite_cockpit(panel, frame->image, mask, options_.cockpit_scaling);
                graphics_.upload(target, panel);
            });
        if (profile_.is_open()) {
            const auto timing = session.timing();
            profile_ << profile_frame_++ << ',' << (xr_.renderer == XrRenderer::gpu ? "gpu" : "cpu") << ','
                << (frame->spatial ? frame->spatial->serial : 0) << ',' << eye_width << ',' << eye_height << ','
                << timing.eyes << ',' << timing.period_ms << ',' << timing.wait_ms << ',' << timing.image_wait_ms << ','
                << timing.render_ms << ',' << timing.end_ms << ','
                << std::chrono::duration<double, std::milli>(Clock::now() - started).count() << '\n';
            if (profile_frame_ % 120 == 0) { profile_.flush(); }
            if (!profile_) { throw std::runtime_error("XR profiling CSV write failed"); }
        }
    }
    using Clock = std::chrono::steady_clock;
    std::ofstream profile_;
    std::uint64_t profile_frame_ = 0;
    SdlHostOptions options_;
    OpenXrOptions xr_;
    SdlOpenXrLoader loader_;
    OpenXrRuntime runtime_;
    OpenXrGraphics graphics_;
    PresentationMailbox mailbox_;
    std::thread worker_;
    std::atomic<bool> stop_{false}, focused_{false}, exit_{false};
    std::atomic<Playback> requested_playback_{Playback::paused};
    std::mutex error_mutex_;
    std::exception_ptr error_;
    SpatialRenderer renderer_;
    std::unique_ptr<SpatialCockpit> cockpit_;
    SpatialImage cockpit_image_;
    CameraHistory history_;
    std::optional<CameraPose> origin_;
    std::uint64_t tracking_epoch_ = 0;
    Clock::time_point epoch_ = Clock::now(), paused_at_ = epoch_;
    Clock::duration paused_duration_{};
    Playback playback_ = Playback::paused;
};

OpenXrHost::OpenXrHost(SdlHostOptions options, OpenXrOptions xr) : impl_(std::make_unique<Impl>(options, xr)) {}
OpenXrHost::~OpenXrHost() = default;
HostInput OpenXrHost::poll() { return impl_->poll(); }
PresentationDetail OpenXrHost::presentation_detail() const { return impl_->desktop_.presentation_detail(); }
void OpenXrHost::present_snapshot(std::shared_ptr<const SessionPresentation> frame, const std::string& status) { impl_->present(std::move(frame), status); }
void OpenXrHost::present(const CockpitImage& image, const FlightState& state, const std::string& status) {
    auto frame = std::make_shared<SessionPresentation>();
    frame->image = image;
    frame->flight = state;
    impl_->present(frame, status);
}
void OpenXrHost::set_input_context(InputContext context) { impl_->desktop_.set_input_context(context); }
void OpenXrHost::queue_audio(const std::vector<std::int16_t>& samples) { impl_->desktop_.queue_audio(samples); }
std::size_t OpenXrHost::queued_audio_samples() const { return impl_->desktop_.queued_audio_samples(); }
void OpenXrHost::clear_audio() { impl_->desktop_.clear_audio(); }
void OpenXrHost::set_playback(Playback playback) { impl_->playback(playback); }
void OpenXrHost::idle() { impl_->desktop_.idle(); }
void OpenXrHost::reset_controls() { impl_->desktop_.reset_controls(); }
}
