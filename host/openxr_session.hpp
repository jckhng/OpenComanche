#pragma once

#include "camera_view.hpp"
#include "graphics_target.hpp"
#include <openxr/openxr.h>
#include <functional>
#include <cstdint>
#include <memory>

namespace comanchecd::host {
using OpenXrColorSpace = GraphicsColorSpace;
using OpenXrTarget = GraphicsTarget;
enum class OpenXrScene { panel, world, cockpit };

struct OpenXrEye {
    OpenXrTarget target;
    CameraPose pose, head;
    CameraFrustum frustum;
    XrTime display_time = 0;
    std::uint64_t tracking_epoch = 0;
};
struct OpenXrFrameTiming {
    double period_ms = 0, wait_ms = 0, image_wait_ms = 0, render_ms = 0, end_ms = 0;
    unsigned eyes = 0;
};
struct OpenXrStatus { bool running = false, focused = false, exit_requested = false; };

// The runtime and its current graphics context outlive this session.
class OpenXrSession {
public:
    ~OpenXrSession();
    OpenXrStatus poll();
    OpenXrFrameTiming timing() const;
    void submit(OpenXrScene scene, const std::function<void(const OpenXrEye&)>& world,
                const std::function<void(const OpenXrTarget&)>& panel);
private:
    friend class OpenXrRuntime;
    OpenXrSession(PFN_xrGetInstanceProcAddr loader, XrInstance instance, XrSystemId system,
                  const XrBaseInStructure& graphics_binding, double render_scale);
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
