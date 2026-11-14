#include "spatial_renderer.hpp"
#include "spatial_units.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace comanchecd::host {
namespace {
using namespace spatial_units;
bool continuous(const SceneEntity& previous, const SceneEntity& current) {
    if (!previous.active || !current.active || previous.kind != current.kind || previous.phase != current.phase ||
            previous.primary_model != current.primary_model || previous.secondary_model != current.secondary_model ||
            previous.primary_payload != current.primary_payload ||
            std::abs(int(current.height) - int(previous.height)) * entity_height > discontinuity_height) { return false; }
    // Moving projectiles publish their preceding pose before advancing. Slot reuse breaks that chain.
    if (current.kind >= guided_missile_kind && current.kind <= enemy_cannon_kind &&
            (current.previous_x != previous.x || current.previous_y != previous.y)) { return false; }
    return std::abs(wrap_delta(double(current.x) / fixed_cell - previous.x / fixed_cell, terrain_side)) <= discontinuity_distance &&
        std::abs(wrap_delta(double(current.y) / fixed_cell - previous.y / fixed_cell, terrain_side)) <= discontinuity_distance;
}
}

SpatialCamera spatial_camera(const SceneView& view) {
    const auto roll_limit = std::max<std::int64_t>(0, 33 - std::abs(std::int64_t(view.pitch)) / 4);
    const auto roll = std::clamp<std::int64_t>(view.roll, -roll_limit, roll_limit);
    return {view.x / fixed_cell, view.y / fixed_cell, view.height * entity_height, view.heading * tau / 4294967296.0,
        std::atan(-view.pitch / 512.0), std::atan(roll / (cockpit_width * focal_ratio)),
        double(view.vertical_pan) / cockpit_height, view.mode};
}

CameraView camera_view(const SpatialCamera& camera, unsigned width, unsigned height) {
    if (!width || !height) { throw std::invalid_argument("Camera viewport must be nonempty"); }
    const double focal = width * focal_ratio;
    const double horizon = height * (horizon_ratio + camera.shake);
    return {{{camera.x, camera.height, camera.y}, camera_orientation(-camera.heading, -camera.pitch, camera.bank)},
        {-0.5 * width / focal, 0.5 * width / focal, horizon / focal, -(height - horizon) / focal, 0.25, 512}};
}

EntityPoses entity_poses(const SpatialFrame& frame) {
    EntityPoses result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto& entity = frame.scene.entities[index];
        result[index] = {entity.x / fixed_cell, entity.y / fixed_cell, entity.height * entity_height,
            entity.angle * byte_angle, entity.rotor_phase * byte_angle};
    }
    return result;
}

void CameraHistory::reset() { previous_.reset(); current_.reset(); cycles_ = 0; duration_ = 0; }

void CameraHistory::observe(std::shared_ptr<const SpatialFrame> frame, std::uint64_t cycles, double time) {
    if (!std::isfinite(time)) { throw std::invalid_argument("Presentation time must be finite"); }
    if (!frame) { reset(); return; }
    if (frame == current_) { return; }
    previous_ = current_;
    if (!current_ || current_->resources != frame->resources || current_->camera.mode != frame->camera.mode ||
            current_->cockpit_mode != frame->cockpit_mode || (frame->native_effects && !frame->effects.enabled) ||
            (current_->native_effects && !current_->effects.enabled) || time < arrival_ ||
            std::abs(int(frame->camera.height) - int(current_->camera.height)) * entity_height > discontinuity_height ||
            std::abs(wrap_delta(double(std::int32_t(frame->camera.x - current_->camera.x)) / fixed_cell, terrain_side)) > discontinuity_distance ||
            std::abs(wrap_delta(double(std::int32_t(frame->camera.y - current_->camera.y)) / fixed_cell, terrain_side)) > discontinuity_distance ||
            frame->serial != current_->serial + 1 || cycles <= cycles_) {
        previous_ = frame;
    }
    current_ = std::move(frame);
    duration_ = previous_ == current_ ? 0 : std::clamp(double(cycles - cycles_) / cycles_per_second, 0.001, 0.25);
    cycles_ = cycles;
    arrival_ = time;
}

double CameraHistory::blend(double time) const {
    if (!current_) { throw std::logic_error("Camera history has no scene"); }
    if (!std::isfinite(time)) { throw std::invalid_argument("Presentation time must be finite"); }
    return duration_ == 0 ? 1 : std::clamp((time - arrival_) / duration_, 0.0, 1.0);
}

SpatialCamera CameraHistory::sample(double time) const {
    const auto alpha = blend(time);
    auto result = spatial_camera(current_->camera);
    const auto from = spatial_camera(previous_->camera);
    result.x = from.x + wrap_delta(result.x - from.x, terrain_side) * alpha;
    result.y = from.y + wrap_delta(result.y - from.y, terrain_side) * alpha;
    result.heading = from.heading + wrap_delta(result.heading - from.heading, tau) * alpha;
    result.height = from.height + (result.height - from.height) * alpha;
    result.bank = from.bank + (result.bank - from.bank) * alpha;
    result.pitch = from.pitch + (result.pitch - from.pitch) * alpha;
    result.shake = from.shake + (result.shake - from.shake) * alpha;
    return result;
}

EntityPoses CameraHistory::sample_entities(double time) const {
    const auto alpha = blend(time);
    auto result = entity_poses(*current_);
    const auto previous = entity_poses(*previous_);
    for (std::size_t index = 0; index < result.size(); ++index) {
        if (!continuous(previous_->scene.entities[index], current_->scene.entities[index])) { continue; }
        const auto& from = previous[index];
        auto& to = result[index];
        to.x = from.x + wrap_delta(to.x - from.x, terrain_side) * alpha;
        to.y = from.y + wrap_delta(to.y - from.y, terrain_side) * alpha;
        to.height = from.height + (to.height - from.height) * alpha;
        to.heading = from.heading + wrap_delta(to.heading - from.heading, tau) * alpha;
        to.rotor_phase = from.rotor_phase + wrap_delta(to.rotor_phase - from.rotor_phase, tau) * alpha;
    }
    return result;
}
}
