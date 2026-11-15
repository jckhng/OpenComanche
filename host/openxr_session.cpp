#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr_platform.h>
#include "openxr_session.hpp"
#include <chrono>
#include "spatial_units.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace comanchecd::host {
namespace {
constexpr std::int64_t srgb_alpha8 = 0x8c43, rgba8 = 0x8058;
constexpr unsigned eye_count = 2, maximum_eye_dimension = 1024;
constexpr unsigned panel_width = 640, panel_height = 480;
constexpr float panel_distance = 1.2f, menu_width = 1.2f;
constexpr unsigned maximum_runtime_items = 4096;

void check(XrResult result, const char* operation) {
    if (XR_FAILED(result)) { throw std::runtime_error(std::string(operation) + " failed with OpenXR result " + std::to_string(result)); }
}
CameraPose pose(const XrPosef& value) {
    return {{value.position.x, value.position.y, value.position.z},
        {value.orientation.x, value.orientation.y, value.orientation.z, value.orientation.w}};
}
}

class OpenXrSession::Impl {
public:
    Impl(PFN_xrGetInstanceProcAddr loader, XrInstance instance) : loader_(loader), instance_(instance) {}
    ~Impl() {
        for (auto& chain : eyes_) { if (chain.handle) { destroy_swapchain_(chain.handle); } }
        if (panel_.handle) { destroy_swapchain_(panel_.handle); }
        if (view_space_) { destroy_space_(view_space_); }
        if (local_space_) { destroy_space_(local_space_); }
        if (session_) { destroy_session_(session_); }
    }

    void initialize(XrSystemId system, const XrBaseInStructure& binding, double scale) {
        if (!std::isfinite(scale) || scale < 0.1 || scale > 1) { throw std::invalid_argument("XR render scale must be between 0.1 and 1"); }
        destroy_session_ = procedure<PFN_xrDestroySession>("xrDestroySession");
        destroy_space_ = procedure<PFN_xrDestroySpace>("xrDestroySpace");
        destroy_swapchain_ = procedure<PFN_xrDestroySwapchain>("xrDestroySwapchain");
        poll_event_ = procedure<PFN_xrPollEvent>("xrPollEvent");
        begin_session_ = procedure<PFN_xrBeginSession>("xrBeginSession");
        end_session_ = procedure<PFN_xrEndSession>("xrEndSession");
        wait_frame_ = procedure<PFN_xrWaitFrame>("xrWaitFrame");
        begin_frame_ = procedure<PFN_xrBeginFrame>("xrBeginFrame");
        end_frame_ = procedure<PFN_xrEndFrame>("xrEndFrame");
        locate_views_ = procedure<PFN_xrLocateViews>("xrLocateViews");
        locate_space_ = procedure<PFN_xrLocateSpace>("xrLocateSpace");
        acquire_ = procedure<PFN_xrAcquireSwapchainImage>("xrAcquireSwapchainImage");
        wait_image_ = procedure<PFN_xrWaitSwapchainImage>("xrWaitSwapchainImage");
        release_ = procedure<PFN_xrReleaseSwapchainImage>("xrReleaseSwapchainImage");
        const auto enumerate_views = procedure<PFN_xrEnumerateViewConfigurationViews>("xrEnumerateViewConfigurationViews");
        std::uint32_t count = 0;
        check(enumerate_views(instance_, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr), "xrEnumerateViewConfigurationViews");
        if (count != eye_count) { throw std::runtime_error("XR requires primary stereo with two views"); }
        std::array<XrViewConfigurationView, eye_count> configurations{};
        for (auto& item : configurations) { item.type = XR_TYPE_VIEW_CONFIGURATION_VIEW; }
        check(enumerate_views(instance_, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye_count, &count, configurations.data()), "xrEnumerateViewConfigurationViews");
        if (count != eye_count) { throw std::runtime_error("XR stereo view count changed"); }
        const auto enumerate_blends = procedure<PFN_xrEnumerateEnvironmentBlendModes>("xrEnumerateEnvironmentBlendModes");
        check(enumerate_blends(instance_, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr), "xrEnumerateEnvironmentBlendModes");
        if (!count || count > maximum_runtime_items) { throw std::runtime_error("Invalid XR blend mode count"); }
        std::vector<XrEnvironmentBlendMode> blends(count);
        const auto capacity = count;
        check(enumerate_blends(instance_, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, capacity, &count, blends.data()), "xrEnumerateEnvironmentBlendModes");
        if (count > capacity || std::find(blends.begin(), blends.begin() + count, XR_ENVIRONMENT_BLEND_MODE_OPAQUE) == blends.begin() + count) {
            throw std::runtime_error("XR requires an opaque environment blend mode");
        }
        XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
        info.next = &binding;
        info.systemId = system;
        check(procedure<PFN_xrCreateSession>("xrCreateSession")(instance_, &info, &session_), "xrCreateSession");
        const auto create_space = procedure<PFN_xrCreateReferenceSpace>("xrCreateReferenceSpace");
        XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        space.poseInReferenceSpace.orientation.w = 1;
        space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        check(create_space(session_, &space, &local_space_), "xrCreateReferenceSpace LOCAL");
        space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        check(create_space(session_, &space, &view_space_), "xrCreateReferenceSpace VIEW");
        const auto enumerate_formats = procedure<PFN_xrEnumerateSwapchainFormats>("xrEnumerateSwapchainFormats");
        check(enumerate_formats(session_, 0, &count, nullptr), "xrEnumerateSwapchainFormats");
        if (!count || count > maximum_runtime_items) { throw std::runtime_error("Invalid XR swapchain format count"); }
        std::vector<std::int64_t> formats(count);
        const auto format_capacity = count;
        check(enumerate_formats(session_, format_capacity, &count, formats.data()), "xrEnumerateSwapchainFormats");
        if (count > format_capacity) { throw std::runtime_error("XR swapchain formats exceeded capacity"); }
        formats.resize(count);
        if (std::find(formats.begin(), formats.end(), srgb_alpha8) != formats.end()) { format_ = srgb_alpha8; }
        else if (std::find(formats.begin(), formats.end(), rgba8) != formats.end()) { format_ = rgba8; }
        else { throw std::runtime_error("XR runtime lacks RGBA8 and sRGB8-alpha8 swapchains"); }
        for (unsigned index = 0; index < eye_count; ++index) {
            const auto& config = configurations[index];
            if (!config.recommendedImageRectWidth || !config.recommendedImageRectHeight ||
                    !config.maxImageRectWidth || !config.maxImageRectHeight) { throw std::runtime_error("Invalid XR eye dimensions"); }
            const auto factor = std::min({scale, double(maximum_eye_dimension) / config.recommendedImageRectWidth,
                double(maximum_eye_dimension) / config.recommendedImageRectHeight,
                double(config.maxImageRectWidth) / config.recommendedImageRectWidth,
                double(config.maxImageRectHeight) / config.recommendedImageRectHeight});
            create_chain(eyes_[index], std::max(1u, unsigned(config.recommendedImageRectWidth * factor)),
                std::max(1u, unsigned(config.recommendedImageRectHeight * factor)));
        }
        create_chain(panel_, panel_width, panel_height);
    }

    OpenXrStatus poll() {
        for (;;) {
            XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
            const auto result = poll_event_(instance_, &event);
            if (result == XR_EVENT_UNAVAILABLE) { break; }
            check(result, "xrPollEvent");
            if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) { status_.exit_requested = true; }
            if (event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
                const auto& changed = reinterpret_cast<const XrEventDataReferenceSpaceChangePending&>(event);
                if (changed.session == session_ && changed.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL) {
                    reference_changes_.push_back(changed.changeTime);
                }
            }
            if (event.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) { continue; }
            const auto& changed = reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
            if (changed.session != session_) { continue; }
            state_ = changed.state;
            status_.focused = state_ == XR_SESSION_STATE_FOCUSED;
            if (state_ == XR_SESSION_STATE_READY && !status_.running) {
                XrSessionBeginInfo info{XR_TYPE_SESSION_BEGIN_INFO};
                info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                check(begin_session_(session_, &info), "xrBeginSession");
                status_.running = true;
            }
            if (state_ == XR_SESSION_STATE_STOPPING && status_.running) {
                check(end_session_(session_), "xrEndSession");
                status_.running = false;
            }
            if (state_ == XR_SESSION_STATE_EXITING || state_ == XR_SESSION_STATE_LOSS_PENDING) { status_.exit_requested = true; }
        }
        return status_;
    }

    void submit(OpenXrScene scene, const std::function<void(const OpenXrEye&)>& world,
                const std::function<void(const OpenXrTarget&)>& panel) {
        timing_ = {};
        if (!status_.running || status_.exit_requested) { return; }
        XrFrameState frame{XR_TYPE_FRAME_STATE};
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
        const auto wait_started = std::chrono::steady_clock::now();
        check(wait_frame_(session_, &wait, &frame), "xrWaitFrame");
        timing_.wait_ms = milliseconds(wait_started);
        timing_.period_ms = frame.predictedDisplayPeriod / 1000000.0;
        while (!reference_changes_.empty() && frame.predictedDisplayTime >= reference_changes_.front()) {
            ++tracking_epoch_;
            cockpit_origin_.reset();
            reference_changes_.pop_front();
        }
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
        check(begin_frame_(session_, &begin), "xrBeginFrame");
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.displayTime = frame.predictedDisplayTime;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        bool frame_open = true;
        try {
            std::array<XrCompositionLayerProjectionView, eye_count> projection_views{};
            XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
            XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
            std::array<const XrCompositionLayerBaseHeader*, 2> layers{};
            if (frame.shouldRender) {
                bool world_visible = false;
                if (scene != OpenXrScene::panel) {
                    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
                    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    locate.displayTime = frame.predictedDisplayTime;
                    locate.space = local_space_;
                    XrViewState view_state{XR_TYPE_VIEW_STATE};
                    std::array<XrView, eye_count> views{};
                    for (auto& view : views) { view.type = XR_TYPE_VIEW; }
                    std::uint32_t count = 0;
                    check(locate_views_(session_, &locate, &view_state, eye_count, &count, views.data()), "xrLocateViews");
                    XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
                    check(locate_space_(view_space_, local_space_, frame.predictedDisplayTime, &head), "xrLocateSpace");
                    constexpr auto valid = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
                    constexpr auto head_valid = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
                    world_visible = count == eye_count && (view_state.viewStateFlags & valid) == valid &&
                        (head.locationFlags & head_valid) == head_valid;
                    if (world_visible) {
                        if (!cockpit_origin_) { cockpit_origin_ = pose(head.pose); }
                        for (unsigned index = 0; index < eye_count; ++index) {
                            ++timing_.eyes;
                            OpenXrEye eye;
                            eye.pose = pose(views[index].pose);
                            eye.head = pose(head.pose);
                            eye.frustum = camera_frustum({views[index].fov.angleLeft, views[index].fov.angleRight,
                                views[index].fov.angleUp, views[index].fov.angleDown}, 0.25, 512);
                            eye.display_time = frame.predictedDisplayTime;
                            eye.tracking_epoch = tracking_epoch_;
                            draw(eyes_[index], [&](const auto& target) { eye.target = target; world(eye); });
                            auto& output = projection_views[index];
                            output.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                            output.pose = views[index].pose;
                            output.fov = views[index].fov;
                            output.subImage = subimage(eyes_[index]);
                        }
                        projection.space = local_space_;
                        projection.viewCount = eye_count;
                        projection.views = projection_views.data();
                        layers[end.layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
                    }
                }
                // A spatial cockpit is already in the eye images. Tracking loss keeps the screen fallback.
                if (scene != OpenXrScene::cockpit || !world_visible) {
                    draw(panel_, panel);
                    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                    quad.space = view_space_;
                    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                    quad.subImage = subimage(panel_);
                    quad.pose.orientation.w = 1;
                    quad.pose.position.z = -panel_distance;
                    quad.size = {menu_width, menu_width * 0.75f};
                    if (world_visible && cockpit_origin_) {
                        // Place the original 4:3 panel at its native angular size in aircraft space.
                        const auto width = panel_distance / spatial_units::focal_ratio;
                        const auto height = width * 0.75;
                        const auto center_y = height * (spatial_units::horizon_ratio - 0.5);
                        const auto cockpit = eye_view({*cockpit_origin_, {}}, {{0, center_y, -panel_distance}, {}}, {}, 1).pose;
                        quad.space = local_space_;
                        quad.pose.position = {float(cockpit.position.x), float(cockpit.position.y), float(cockpit.position.z)};
                        quad.pose.orientation = {float(cockpit.orientation.x), float(cockpit.orientation.y),
                            float(cockpit.orientation.z), float(cockpit.orientation.w)};
                        quad.size = {float(width), float(height)};
                    }
                    layers[end.layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
                }
                end.layers = layers.data();
            }
            frame_open = false;
            const auto end_started = std::chrono::steady_clock::now();
            check(end_frame_(session_, &end), "xrEndFrame");
            timing_.end_ms = milliseconds(end_started);
        } catch (...) {
            if (frame_open) {
                end.layerCount = 0;
                end.layers = nullptr;
                end_frame_(session_, &end);
            }
            throw;
        }
    }

    OpenXrFrameTiming timing() const { return timing_; }
private:
    static double milliseconds(std::chrono::steady_clock::time_point started) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    }
    struct Chain {
        XrSwapchain handle = XR_NULL_HANDLE;
        std::uint32_t width = 0, height = 0;
        std::vector<XrSwapchainImageOpenGLKHR> images;
    };
    template<class Procedure>
    Procedure procedure(const char* name) const {
        PFN_xrVoidFunction function = nullptr;
        check(loader_(instance_, name, &function), name);
        if (!function) { throw std::runtime_error(std::string("Missing OpenXR function ") + name); }
        return reinterpret_cast<Procedure>(function);
    }
    void create_chain(Chain& chain, unsigned width, unsigned height) {
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.format = format_;
        info.sampleCount = info.faceCount = info.arraySize = info.mipCount = 1;
        info.width = width;
        info.height = height;
        check(procedure<PFN_xrCreateSwapchain>("xrCreateSwapchain")(session_, &info, &chain.handle), "xrCreateSwapchain");
        chain.width = width;
        chain.height = height;
        const auto enumerate = procedure<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages");
        std::uint32_t count = 0;
        check(enumerate(chain.handle, 0, &count, nullptr), "xrEnumerateSwapchainImages");
        if (!count || count > maximum_runtime_items) { throw std::runtime_error("Invalid XR swapchain image count"); }
        chain.images.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
        const auto capacity = count;
        check(enumerate(chain.handle, capacity, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(chain.images.data())), "xrEnumerateSwapchainImages");
        if (!count || count > capacity) { throw std::runtime_error("XR image enumeration changed beyond capacity"); }
        chain.images.resize(count);
    }
    XrSwapchainSubImage subimage(const Chain& chain) const {
        return {chain.handle, {{0, 0}, {std::int32_t(chain.width), std::int32_t(chain.height)}}, 0};
    }
    void draw(Chain& chain, const std::function<void(const OpenXrTarget&)>& callback) {
        const auto wait_started = std::chrono::steady_clock::now();
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        std::uint32_t index = 0;
        check(acquire_(chain.handle, &acquire, &index), "xrAcquireSwapchainImage");
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = XR_INFINITE_DURATION;
        XrResult result;
        do { result = wait_image_(chain.handle, &wait); } while (result == XR_TIMEOUT_EXPIRED);
        check(result, "xrWaitSwapchainImage");
        timing_.image_wait_ms += milliseconds(wait_started);
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        try {
            if (index >= chain.images.size()) { throw std::runtime_error("XR acquired image index exceeds swapchain"); }
            const auto render_started = std::chrono::steady_clock::now();
            callback({chain.images[index].image, chain.width, chain.height,
                format_ == srgb_alpha8 ? OpenXrColorSpace::srgb : OpenXrColorSpace::linear});
            timing_.render_ms += milliseconds(render_started);
        } catch (...) {
            release_(chain.handle, &release);
            throw;
        }
        check(release_(chain.handle, &release), "xrReleaseSwapchainImage");
    }
    PFN_xrGetInstanceProcAddr loader_;
    XrInstance instance_;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace local_space_ = XR_NULL_HANDLE, view_space_ = XR_NULL_HANDLE;
    XrSessionState state_ = XR_SESSION_STATE_UNKNOWN;
    OpenXrStatus status_;
    OpenXrFrameTiming timing_;
    std::deque<XrTime> reference_changes_;
    std::optional<CameraPose> cockpit_origin_;
    std::uint64_t tracking_epoch_ = 0;
    std::int64_t format_ = 0;
    std::array<Chain, eye_count> eyes_;
    Chain panel_;
    PFN_xrDestroySession destroy_session_ = nullptr;
    PFN_xrDestroySpace destroy_space_ = nullptr;
    PFN_xrDestroySwapchain destroy_swapchain_ = nullptr;
    PFN_xrPollEvent poll_event_ = nullptr;
    PFN_xrBeginSession begin_session_ = nullptr;
    PFN_xrEndSession end_session_ = nullptr;
    PFN_xrWaitFrame wait_frame_ = nullptr;
    PFN_xrBeginFrame begin_frame_ = nullptr;
    PFN_xrEndFrame end_frame_ = nullptr;
    PFN_xrLocateViews locate_views_ = nullptr;
    PFN_xrLocateSpace locate_space_ = nullptr;
    PFN_xrAcquireSwapchainImage acquire_ = nullptr;
    PFN_xrWaitSwapchainImage wait_image_ = nullptr;
    PFN_xrReleaseSwapchainImage release_ = nullptr;
};

OpenXrSession::OpenXrSession(PFN_xrGetInstanceProcAddr loader, XrInstance instance, XrSystemId system,
    const XrBaseInStructure& binding, double scale) : impl_(std::make_unique<Impl>(loader, instance)) {
    impl_->initialize(system, binding, scale);
}
OpenXrSession::~OpenXrSession() = default;
OpenXrStatus OpenXrSession::poll() { return impl_->poll(); }
OpenXrFrameTiming OpenXrSession::timing() const { return impl_->timing(); }
void OpenXrSession::submit(OpenXrScene scene, const std::function<void(const OpenXrEye&)>& world,
    const std::function<void(const OpenXrTarget&)>& panel) { impl_->submit(scene, world, panel); }
}
