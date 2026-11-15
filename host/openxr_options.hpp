#pragma once

#include <string>
#include <vector>

namespace comanchecd::host {
enum class XrRenderer { gpu, cpu };
struct OpenXrOptions {
    bool enabled = false;
    double world_scale = 0, render_scale = 0.5;
    std::string loader, profile;
    XrRenderer renderer = XrRenderer::gpu;
};
// XR device options are launch-only and never enter persisted host preferences.
OpenXrOptions take_openxr_options(std::vector<std::string>& arguments);
}
