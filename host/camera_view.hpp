#pragma once

namespace comanchecd::host {
struct CameraVector { double x = 0, y = 0, z = 0; };
struct CameraQuaternion { double x = 0, y = 0, z = 0, w = 1; };
struct CameraPose { CameraVector position; CameraQuaternion orientation; };

// Right-handed view space uses +X right, +Y up and -Z forward.
// Projection limits are tangents. Pose positions use the renderer's world units.
struct CameraFrustum {
    double left = -0.8, right = 0.8, up = 0.38, down = -0.82;
    double near_clip = 0.25, far_clip = 512;
};
struct CameraFov { double left, right, up, down; }; // Radians, including signed left/down angles.
struct CameraView { CameraPose pose; CameraFrustum frustum; };
struct CameraPixel { double x, y, depth; };

CameraVector operator+(CameraVector a, CameraVector b);
CameraVector operator-(CameraVector a, CameraVector b);
CameraVector operator*(CameraVector value, double scale);
CameraQuaternion camera_orientation(double yaw, double pitch, double roll);
CameraFrustum camera_frustum(CameraFov angles, double near_clip, double far_clip);
CameraPose relative_pose(const CameraPose& origin, const CameraPose& pose);

// The caller supplies the game-to-metre scale for a tracked reference-space pose.
CameraView eye_view(const CameraView& base, const CameraPose& relative_metres,
                    CameraFrustum frustum, double world_units_per_metre);

class CameraProjection {
public:
    CameraProjection(const CameraView& view, unsigned width, unsigned height);
    CameraVector to_camera(CameraVector world) const;
    CameraVector to_world(CameraVector local) const;
    CameraVector ray(double x, double y) const;
    CameraPixel project(CameraVector local) const;
private:
    CameraVector origin_, right_, up_, back_;
    CameraFrustum frustum_;
    unsigned width_, height_;
};
}
