#pragma once

#include "camera_view.hpp"
#include <openxr/openxr.h>
#include <functional>
#include <cstdint>
#include <memory>

namespace comanchecd::host {
enum class OpenXrColorSpace { srgb, linear };
enum class OpenXrScene { panel, world, cockpit };

struct OpenXrTarget {
    std::uint32_t texture = 0, width = 0, height = 0;
    OpenXrColorSpace color_space = OpenXrColorSpace::srgb;
};
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
