#pragma once

#include "openxr_runtime.hpp"
#include "openxr_session.hpp"
#include "spatial_renderer.hpp"

namespace comanchecd::host {
// SDL2 owns the window/context. Native bindings stay inside this driver.
class OpenXrGraphics {
public:
    explicit OpenXrGraphics(const OpenXrCapabilities& requirements);
    ~OpenXrGraphics();
    void activate();
    void release();
    std::unique_ptr<OpenXrSession> create_session(const OpenXrRuntime& runtime, double render_scale);
    void upload(const OpenXrTarget& target, const SpatialImage& image);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
