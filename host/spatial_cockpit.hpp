#pragma once

#include "spatial_renderer.hpp"

namespace comanchecd::host {
enum class CockpitPresentation { flat, spatial };

class CockpitLook {
public:
    void move(double yaw, double pitch);
    void reset();
    CameraPose pose() const;
private:
    double yaw_ = 0, pitch_ = 0;
};

bool spatial_presentation(const SessionPresentation& frame, CockpitPresentation cockpit);
CameraView cockpit_view(const CameraPose& pose, CameraFrustum frustum);

// Enhanced, metre-scaled geometry surrounds the original live instrument artwork.
class SpatialCockpit {
public:
    SpatialCockpit();
    ~SpatialCockpit();
    void draw(SpatialImage& image, const CameraView& eye, const CockpitImage& instruments, const SpatialFrame& frame);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
