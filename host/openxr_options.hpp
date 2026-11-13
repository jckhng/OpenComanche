#pragma once

#include <string>
#include <vector>

namespace comanchecd::host {
struct OpenXrOptions {
    bool enabled = false;
    double world_scale = 0, render_scale = 0.5;
    std::string loader;
};
// XR device options are launch-only and never enter persisted host preferences.
OpenXrOptions take_openxr_options(std::vector<std::string>& arguments);
}
