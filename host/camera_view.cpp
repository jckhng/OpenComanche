#include "camera_view.hpp"

#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace comanchecd::host {
namespace {
double dot(CameraVector a, CameraVector b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
CameraVector cross(CameraVector a, CameraVector b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
void validate(CameraVector value) {
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
        throw std::invalid_argument("Camera coordinates must be finite");
    }
}
CameraQuaternion unit(CameraQuaternion q) {
    const auto length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!std::isfinite(length) || length < 1e-12) { throw std::invalid_argument("Camera orientation must be a finite nonzero quaternion"); }
    return {q.x / length, q.y / length, q.z / length, q.w / length};
}
CameraQuaternion multiply(CameraQuaternion a, CameraQuaternion b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
CameraVector rotate(CameraQuaternion q, CameraVector value) {
    const CameraVector imaginary{q.x, q.y, q.z};
    const auto twice = cross(imaginary, value) * 2;
    return value + twice * q.w + cross(imaginary, twice);
}
void validate(CameraFrustum f) {
    for (const auto value : {f.left, f.right, f.up, f.down, f.near_clip, f.far_clip}) {
        if (!std::isfinite(value)) { throw std::invalid_argument("Camera frustum must be finite"); }
    }
    if (!std::isfinite(f.right - f.left) || !std::isfinite(f.up - f.down) ||
            f.right - f.left < 1e-9 || f.up - f.down < 1e-9 || f.near_clip <= 0 || f.far_clip <= f.near_clip) {
        throw std::invalid_argument("Invalid camera frustum extents or clipping planes");
    }
}
}

CameraVector operator+(CameraVector a, CameraVector b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
CameraVector operator-(CameraVector a, CameraVector b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
CameraVector operator*(CameraVector v, double scale) { return {v.x * scale, v.y * scale, v.z * scale}; }

CameraQuaternion camera_orientation(double yaw, double pitch, double roll) {
    return unit(multiply(multiply({0, std::sin(yaw / 2), 0, std::cos(yaw / 2)},
        {std::sin(pitch / 2), 0, 0, std::cos(pitch / 2)}), {0, 0, std::sin(roll / 2), std::cos(roll / 2)}));
}

CameraFrustum camera_frustum(CameraFov angles, double near_clip, double far_clip) {
    constexpr double half_pi = 1.57079632679489661923;
    for (const auto angle : {angles.left, angles.right, angles.up, angles.down}) {
        if (!std::isfinite(angle) || std::abs(angle) >= half_pi) { throw std::invalid_argument("Camera FOV angles must lie between minus and plus pi/2"); }
    }
    CameraFrustum result{std::tan(angles.left), std::tan(angles.right), std::tan(angles.up), std::tan(angles.down), near_clip, far_clip};
    validate(result);
    return result;
}

CameraView eye_view(const CameraView& base, const CameraPose& relative_metres, CameraFrustum frustum, double scale) {
    validate(base.pose.position);
    validate(relative_metres.position);
    validate(frustum);
    if (!std::isfinite(scale) || scale <= 0) { throw std::invalid_argument("Tracked pose scale must be finite and positive"); }
    const auto orientation = unit(base.pose.orientation);
    CameraView result{{base.pose.position + rotate(orientation, relative_metres.position * scale),
        unit(multiply(orientation, unit(relative_metres.orientation)))}, frustum};
    validate(result.pose.position);
    return result;
}

CameraPose relative_pose(const CameraPose& origin, const CameraPose& pose) {
    validate(origin.position);
    validate(pose.position);
    const auto orientation = unit(origin.orientation);
    const CameraQuaternion inverse{-orientation.x, -orientation.y, -orientation.z, orientation.w};
    return {rotate(inverse, pose.position - origin.position), unit(multiply(inverse, unit(pose.orientation)))};
}

CameraProjection::CameraProjection(const CameraView& view, unsigned width, unsigned height)
    : origin_(view.pose.position), frustum_(view.frustum), width_(width), height_(height) {
    validate(origin_);
    validate(frustum_);
    if (!width || !height) { throw std::invalid_argument("Camera viewport must be nonempty"); }
    const auto q = unit(view.pose.orientation);
    right_ = rotate(q, {1, 0, 0}); up_ = rotate(q, {0, 1, 0}); back_ = rotate(q, {0, 0, 1});
}

CameraVector CameraProjection::to_camera(CameraVector world) const {
    const auto delta = world - origin_;
    return {dot(delta, right_), dot(delta, up_), dot(delta, back_)};
}
CameraVector CameraProjection::to_world(CameraVector local) const {
    return origin_ + right_ * local.x + up_ * local.y + back_ * local.z;
}
CameraVector CameraProjection::ray(double x, double y) const {
    if (!std::isfinite(x) || !std::isfinite(y)) { throw std::invalid_argument("Camera ray coordinates must be finite"); }
    const auto result = right_ * (frustum_.left + (frustum_.right - frustum_.left) * (x / width_)) +
        up_ * (frustum_.up - (frustum_.up - frustum_.down) * (y / height_)) - back_;
    validate(result);
    return result;
}
CameraPixel CameraProjection::project(CameraVector local) const {
    validate(local);
    const auto depth = -local.z;
    if (!std::isfinite(depth) || depth <= 0) { throw std::invalid_argument("Camera projection requires a point in front of its eye"); }
    const CameraPixel result{(local.x / depth - frustum_.left) * width_ / (frustum_.right - frustum_.left),
        (frustum_.up - local.y / depth) * height_ / (frustum_.up - frustum_.down), depth};
    if (!std::isfinite(result.x) || !std::isfinite(result.y)) { throw std::invalid_argument("Projected camera coordinates exceed their finite range"); }
    return result;
}
}
