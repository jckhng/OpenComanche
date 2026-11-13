#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr_platform.h>
#include "openxr_runtime.hpp"
#include "openxr_session.hpp"
#include "openxr_options.hpp"

#include <array>
#include <cstring>
#include <cstdint>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace comanchecd::host;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
template<class Handle> Handle handle(std::uintptr_t value) { return reinterpret_cast<Handle>(value); }
const auto instance = handle<XrInstance>(1);
const auto session = handle<XrSession>(2);
struct Mock {
    unsigned sessions = 0, spaces = 0, chains = 0, destroyed_sessions = 0, destroyed_spaces = 0, destroyed_chains = 0;
    unsigned begun = 0, ended = 0, waits = 0, frames = 0, releases = 0, draws = 0;
    unsigned frame_stage = 0, chain_stage = 0, last_layers = 0;
    bool render = true, tracking = true, timeout = false, failed_chain = false, invalid_index = false;
    bool reference_change = false;
    std::deque<XrSessionState> events{XR_SESSION_STATE_READY, XR_SESSION_STATE_FOCUSED};
} *mock;

XrResult XRAPI_CALL extensions(const char*, std::uint32_t capacity, std::uint32_t* count, XrExtensionProperties* out) {
    *count = 1;
    if (capacity) { std::strcpy(out[0].extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME); }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL create_instance(const XrInstanceCreateInfo*, XrInstance* out) { *out = instance; return XR_SUCCESS; }
XrResult XRAPI_CALL destroy_instance(XrInstance) { return XR_SUCCESS; }
XrResult XRAPI_CALL properties(XrInstance, XrInstanceProperties* out) {
    std::strcpy(out->runtimeName, "Session test"); return XR_SUCCESS;
}
XrResult XRAPI_CALL system_id(XrInstance, const XrSystemGetInfo*, XrSystemId* out) { *out = 3; return XR_SUCCESS; }
XrResult XRAPI_CALL graphics(XrInstance, XrSystemId, XrGraphicsRequirementsOpenGLKHR* out) {
    out->minApiVersionSupported = XR_MAKE_VERSION(3, 3, 0);
    out->maxApiVersionSupported = XR_MAKE_VERSION(4, 6, 0);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL enumerate_views(XrInstance, XrSystemId, XrViewConfigurationType type,
    std::uint32_t capacity, std::uint32_t* count, XrViewConfigurationView* out) {
    require(type == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, "Session must enumerate stereo views");
    *count = 2;
    if (capacity) {
        for (unsigned i = 0; i < 2; ++i) {
            require(out[i].type == XR_TYPE_VIEW_CONFIGURATION_VIEW, "View outputs must be typed");
            out[i].recommendedImageRectWidth = 1600; out[i].recommendedImageRectHeight = 1200;
            out[i].maxImageRectWidth = 2048; out[i].maxImageRectHeight = 2048;
            out[i].recommendedSwapchainSampleCount = out[i].maxSwapchainSampleCount = 4;
        }
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL blends(XrInstance, XrSystemId, XrViewConfigurationType, std::uint32_t capacity,
    std::uint32_t* count, XrEnvironmentBlendMode* out) {
    *count = 1; if (capacity) { *out = XR_ENVIRONMENT_BLEND_MODE_OPAQUE; } return XR_SUCCESS;
}
XrResult XRAPI_CALL create_session(XrInstance owner, const XrSessionCreateInfo* info, XrSession* out) {
    require(owner == instance && info->systemId == 3 && info->next && info->type == XR_TYPE_SESSION_CREATE_INFO,
        "Session creation must retain its admitted system and graphics binding");
    ++mock->sessions; *out = session; return XR_SUCCESS;
}
XrResult XRAPI_CALL destroy_session(XrSession owner) {
    require(owner == session, "Session destruction must retain ownership");
    ++mock->destroyed_sessions; return XR_SUCCESS;
}
XrResult XRAPI_CALL create_space(XrSession, const XrReferenceSpaceCreateInfo* info, XrSpace* out) {
    require(info->poseInReferenceSpace.orientation.w == 1, "Reference spaces require an identity pose");
    ++mock->spaces; *out = handle<XrSpace>(mock->spaces + 2); return XR_SUCCESS;
}
XrResult XRAPI_CALL destroy_space(XrSpace) { ++mock->destroyed_spaces; return XR_SUCCESS; }
XrResult XRAPI_CALL formats(XrSession, std::uint32_t capacity, std::uint32_t* count, std::int64_t* out) {
    *count = 2; if (capacity) { out[0] = 0x8058; out[1] = 0x8c43; } return XR_SUCCESS;
}
XrResult XRAPI_CALL create_chain(XrSession, const XrSwapchainCreateInfo* info, XrSwapchain* out) {
    require(info->format == 0x8c43 && info->arraySize == 1 && info->sampleCount == 1 && info->faceCount == 1,
        "CPU uploads require single-sample sRGB 2D swapchains");
    require(info->width == (mock->chains < 2 ? 800u : 640u) && info->height == (mock->chains < 2 ? 600u : 480u),
        "Eye scale and independent panel resolution must reach swapchain creation");
    if (mock->failed_chain && mock->chains == 2) { return XR_ERROR_RUNTIME_FAILURE; }
    ++mock->chains; *out = handle<XrSwapchain>(10 + mock->chains); return XR_SUCCESS;
}
XrResult XRAPI_CALL destroy_chain(XrSwapchain) { ++mock->destroyed_chains; return XR_SUCCESS; }
XrResult XRAPI_CALL images(XrSwapchain, std::uint32_t capacity, std::uint32_t* count, XrSwapchainImageBaseHeader* out) {
    *count = 2;
    if (capacity) {
        auto* gl = reinterpret_cast<XrSwapchainImageOpenGLKHR*>(out);
        for (unsigned i = 0; i < 2; ++i) {
            require(gl[i].type == XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR, "Swapchain images must carry the OpenGL type");
            gl[i].image = 100 + i;
        }
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL poll(XrInstance, XrEventDataBuffer* out) {
    if (mock->reference_change) {
        mock->reference_change = false;
        auto* changed = reinterpret_cast<XrEventDataReferenceSpaceChangePending*>(out);
        changed->type = XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING;
        changed->session = session;
        changed->referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        changed->changeTime = 12345;
        return XR_SUCCESS;
    }
    if (mock->events.empty()) { return XR_EVENT_UNAVAILABLE; }
    auto* changed = reinterpret_cast<XrEventDataSessionStateChanged*>(out);
    changed->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED; changed->session = session;
    changed->state = mock->events.front(); mock->events.pop_front(); return XR_SUCCESS;
}
XrResult XRAPI_CALL begin_session(XrSession, const XrSessionBeginInfo* info) {
    require(info->primaryViewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, "Begin session must select stereo");
    ++mock->begun; return XR_SUCCESS;
}
XrResult XRAPI_CALL end_session(XrSession) { ++mock->ended; return XR_SUCCESS; }
XrResult XRAPI_CALL wait_frame(XrSession, const XrFrameWaitInfo*, XrFrameState* out) {
    require(mock->frame_stage == 0, "Each frame must wait exactly once before beginning");
    mock->frame_stage = 1; ++mock->waits;
    out->shouldRender = mock->render; out->predictedDisplayTime = 12345; out->predictedDisplayPeriod = 11111;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL begin_frame(XrSession, const XrFrameBeginInfo*) {
    require(mock->frame_stage == 1, "Begin frame must follow wait frame");
    mock->frame_stage = 2; return XR_SUCCESS;
}
XrResult XRAPI_CALL end_frame(XrSession, const XrFrameEndInfo* info) {
    require(mock->frame_stage == 2 && mock->chain_stage == 0 && info->displayTime == 12345 &&
        info->environmentBlendMode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE, "End frame must balance image ownership and use predicted display time");
    mock->last_layers = info->layerCount;
    if (info->layerCount == 2) {
        const auto* projection = reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[0]);
        require(projection->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && projection->viewCount == 2 &&
            projection->space == handle<XrSpace>(3), "Stereo projection must use the located local reference space");
        for (unsigned i = 0; i < 2; ++i) {
            require(projection->views[i].type == XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW &&
                projection->views[i].subImage.swapchain == handle<XrSwapchain>(11 + i) &&
                projection->views[i].subImage.imageRect.extent.width == 800 &&
                projection->views[i].pose.position.x == (i ? 0.032f : -0.032f),
                "Projection views must retain their per-eye pose and released swapchain");
        }
    }
    if (info->layerCount) {
        const auto* panel = reinterpret_cast<const XrCompositionLayerQuad*>(info->layers[info->layerCount - 1]);
        require(panel->type == XR_TYPE_COMPOSITION_LAYER_QUAD && panel->subImage.swapchain == handle<XrSwapchain>(13) &&
            panel->pose.orientation.w == 1 && panel->size.width / panel->size.height > 1.33f,
            "Cockpit/menu composition must use its independent 4:3 quad");
    }
    mock->frame_stage = 0; ++mock->frames; return XR_SUCCESS;
}
XrResult XRAPI_CALL locate(XrSession, const XrViewLocateInfo* info, XrViewState* state,
    std::uint32_t capacity, std::uint32_t* count, XrView* out) {
    require(info->displayTime == 12345 && capacity == 2 && info->space == handle<XrSpace>(3),
        "Views must be located in local space at predicted display time");
    *count = 2;
    state->viewStateFlags = mock->tracking ? XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT : 0;
    for (unsigned i = 0; i < 2; ++i) {
        out[i].pose.orientation.w = 1; out[i].pose.position.x = i ? 0.032f : -0.032f;
        out[i].fov = {-0.8f, 0.9f, 0.7f, -0.6f};
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL head(XrSpace, XrSpace, XrTime time, XrSpaceLocation* out) {
    require(time == 12345, "Head and eyes must share predicted time");
    out->locationFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    out->pose.orientation.w = 1; return XR_SUCCESS;
}
XrResult XRAPI_CALL acquire(XrSwapchain, const XrSwapchainImageAcquireInfo*, std::uint32_t* index) {
    require(mock->frame_stage == 2 && mock->chain_stage == 0, "Acquisition requires an open frame and a released preceding image");
    mock->chain_stage = 1; *index = mock->invalid_index ? 3 : 1; return XR_SUCCESS;
}
XrResult XRAPI_CALL wait_image(XrSwapchain, const XrSwapchainImageWaitInfo* info) {
    require(mock->chain_stage == 1 && info->timeout == XR_INFINITE_DURATION, "Image wait must follow acquisition");
    if (mock->timeout) { mock->timeout = false; return XR_TIMEOUT_EXPIRED; }
    mock->chain_stage = 2; return XR_SUCCESS;
}
XrResult XRAPI_CALL release(XrSwapchain, const XrSwapchainImageReleaseInfo*) {
    require(mock->chain_stage == 2, "Only a successfully waited image may be released");
    mock->chain_stage = 0; ++mock->releases; return XR_SUCCESS;
}
XrResult XRAPI_CALL entry(XrInstance, const char* name, PFN_xrVoidFunction* out) {
#define PROCEDURE(api, implementation) if (std::strcmp(name, #api) == 0) { *out = reinterpret_cast<PFN_xrVoidFunction>(implementation); return XR_SUCCESS; }
    PROCEDURE(xrEnumerateInstanceExtensionProperties, extensions)
    PROCEDURE(xrCreateInstance, create_instance)
    PROCEDURE(xrDestroyInstance, destroy_instance)
    PROCEDURE(xrGetInstanceProperties, properties)
    PROCEDURE(xrGetSystem, system_id)
    PROCEDURE(xrGetOpenGLGraphicsRequirementsKHR, graphics)
    PROCEDURE(xrEnumerateViewConfigurationViews, enumerate_views)
    PROCEDURE(xrEnumerateEnvironmentBlendModes, blends)
    PROCEDURE(xrCreateSession, create_session)
    PROCEDURE(xrDestroySession, destroy_session)
    PROCEDURE(xrCreateReferenceSpace, create_space)
    PROCEDURE(xrDestroySpace, destroy_space)
    PROCEDURE(xrEnumerateSwapchainFormats, formats)
    PROCEDURE(xrCreateSwapchain, create_chain)
    PROCEDURE(xrDestroySwapchain, destroy_chain)
    PROCEDURE(xrEnumerateSwapchainImages, images)
    PROCEDURE(xrPollEvent, poll)
    PROCEDURE(xrBeginSession, begin_session)
    PROCEDURE(xrEndSession, end_session)
    PROCEDURE(xrWaitFrame, wait_frame)
    PROCEDURE(xrBeginFrame, begin_frame)
    PROCEDURE(xrEndFrame, end_frame)
    PROCEDURE(xrLocateViews, locate)
    PROCEDURE(xrLocateSpace, head)
    PROCEDURE(xrAcquireSwapchainImage, acquire)
    PROCEDURE(xrWaitSwapchainImage, wait_image)
    PROCEDURE(xrReleaseSwapchainImage, release)
#undef PROCEDURE
    *out = nullptr; return XR_ERROR_FUNCTION_UNSUPPORTED;
}
void options() {
    std::vector<std::string> arguments{"--mode", "enhanced", "--xr", "on", "--xr-world-scale", "8", "--xr-render-scale", "0.5"};
    const auto selected = take_openxr_options(arguments);
    require(selected.enabled && selected.world_scale == 8 && selected.render_scale == 0.5 &&
        arguments == std::vector<std::string>{"--mode", "enhanced"}, "XR options must remain launch-only");
    for (auto invalid : std::vector<std::vector<std::string>>{{"--xr", "on"}, {"--xr", "yes"},
        {"--xr", "on", "--xr-world-scale", "nan"}, {"--xr", "on", "--xr-world-scale", "8junk"},
        {"--xr-world-scale", "8"}, {"--xr", "on", "--xr-world-scale", "8", "--xr-render-scale", "2"}}) {
        bool rejected = false;
        try { take_openxr_options(invalid); } catch (const std::exception&) { rejected = true; }
        require(rejected, "Invalid XR launch options must fail before device initialization");
    }
}
}

int main() {
    try {
        options();
        for (unsigned scenario = 0; scenario < 7; ++scenario) {
            Mock state; mock = &state;
            state.failed_chain = scenario == 1; state.render = scenario != 2;
            state.tracking = scenario != 3; state.timeout = scenario == 4; state.invalid_index = scenario == 6;
            bool rejected = false;
            {
                OpenXrRuntime runtime(entry);
                try {
                    const XrBaseInStructure binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR, nullptr};
                    auto active = runtime.create_session(binding, 0.5);
                    require(state.waits == 0, "Frame calls must await session readiness");
                    active->submit(OpenXrScene::panel, {}, [](const auto&) {});
                    require(state.waits == 0, "Idle sessions must not wait for frames");
                    const auto status = active->poll();
                    require(status.running && status.focused && state.begun == 1, "READY/FOCUSED must begin one stereo session");
                    active->submit(OpenXrScene::world, [&](const OpenXrEye& eye) {
                        require(eye.target.texture == 101 && eye.target.width == 800 && eye.display_time == 12345 &&
                            eye.frustum.left < 0 && eye.frustum.right > 0, "Draw callback must receive the waited texture and tracked frustum");
                        ++state.draws;
                        if (scenario == 5) { throw std::runtime_error("Synthetic draw failure"); }
                    }, [&](const auto& target) { require(target.width == 640, "Panel must use its own image"); });
                    require(state.frames == 1 && state.last_layers == (!state.render ? 0u : state.tracking ? 2u : 1u),
                        "Hidden frames and invalid tracking must omit world submission");
                    if (scenario == 0) {
                        state.reference_change = true;
                        active->poll();
                        active->submit(OpenXrScene::world, [&](const OpenXrEye& eye) {
                            require(eye.tracking_epoch == 1, "Reference-space changes must reset the tracked origin at display time");
                        }, [](const auto&) {});
                    }
                    state.events = {XR_SESSION_STATE_STOPPING, XR_SESSION_STATE_IDLE};
                    require(!active->poll().running && state.ended == 1, "STOPPING must end the running session");
                    active->submit(OpenXrScene::panel, {}, {});
                    require(state.waits == (scenario == 0 ? 2u : 1u), "Stopped sessions must not wait for frames");
                    state.events = {XR_SESSION_STATE_READY, XR_SESSION_STATE_VISIBLE};
                    require(active->poll().running && state.begun == 2, "A stopped session must restart on READY");
                    active->submit(OpenXrScene::panel, {}, [](const auto&) {});
                    state.events = {XR_SESSION_STATE_EXITING};
                    require(active->poll().exit_requested, "Runtime exit must reach the host");
                } catch (const std::runtime_error&) { rejected = true; }
            }
            require(rejected == (scenario == 1 || scenario == 5 || scenario == 6), "Session failure must match its injected cause");
            require(state.sessions == state.destroyed_sessions && state.spaces == state.destroyed_spaces && state.chains == state.destroyed_chains,
                "Partial construction and normal exit must release every owned resource");
            require(state.frame_stage == 0 && state.chain_stage == 0, "Draw failures must release waited images and end an empty frame");
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
