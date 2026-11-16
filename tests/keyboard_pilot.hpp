#pragma once

#include "comanchecd/presentation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace keyboard_test {
// Test-only pilot. It observes completed native flight frames and emits held keys.
class Pilot {
  public:
    comanchecd::FlightControls controls(const comanchecd::FlightState& state,
                                        const comanchecd::SpatialFrame& frame) {
        using namespace comanchecd;
        if (serial_ && *serial_ == frame.serial) {
            return previous_;
        }
        if (serial_ && frame.serial < *serial_) {
            *this = Pilot{};
        }
        serial_ = frame.serial;
        FlightControls result;
        result.throttle = hover_throttle;
        const auto& entities = frame.scene.entities;
        if (live_objective(entities, state.target.slot)) {
            pursuit_ = state.target.slot;
        } else if (!live_objective(entities, pursuit_)) {
            pursuit_ = nearest_objective(state, frame);
        }
        const bool locked = pursuit_ != 0 && state.target.slot == pursuit_;
        std::int64_t desired_speed = 0;
        if (pursuit_ != 0) {
            const auto& objective = entities[pursuit_];
            const auto dx = std::int32_t(objective.x - state.view.x);
            const auto dy = std::int32_t(objective.y - state.view.y);
            auto angle = std::atan2(double(dx), -double(dy));
            if (angle < 0) {
                angle += full_turn;
            }
            const auto error =
                dx == 0 && dy == 0
                    ? 0
                    : std::int32_t(std::uint32_t(angle * heading_units / full_turn) - state.view.heading);
            const auto yaw = std::clamp(error / heading_error_scale - state.yaw_rate / yaw_damping_scale,
                                        -key_limit, key_limit);
            result.horizontal = key_axis(yaw, yaw_carry_);
            const auto distance = std::max(std::abs(std::int64_t(dx)), std::abs(std::int64_t(dy)));
            if (std::abs(std::int64_t(error)) <= alignment_limit) {
                const auto forward_limit = std::min<std::int64_t>(cruise_speed, state.forward_limit);
                desired_speed =
                    std::clamp((distance - stand_off) / approach_frames, -(forward_limit / 2), forward_limit);
            }
        } else {
            yaw_carry_ = 0;
        }
        // Include native drag and brake toward a stand-off speed even after lock is lost.
        const auto speed = std::int64_t(state.forward_speed);
        const auto drag = (speed >> drag_shift) | std::int64_t(speed > 0);
        const auto next_speed = speed + (desired_speed - speed) / response_frames;
        const auto pitch = static_cast<int>(std::clamp((speed - drag - next_speed) / pitch_acceleration,
                                                       std::int64_t(-key_limit), std::int64_t(key_limit)));
        result.vertical = key_axis(pitch, pitch_carry_);
        result.cycle_target = pursuit_ != 0 && !locked &&
                              elapsed(target_request_, frame.serial, target_interval) &&
                              !previous_.cycle_target;
        if (result.cycle_target) {
            target_request_ = frame.serial;
        }
        result.weapon = state.ammunition[2] != 0   ? WeaponSelection::hellfire
                        : state.ammunition[1] != 0 ? WeaponSelection::rockets
                                                   : WeaponSelection::cannon;
        const auto interval = result.weapon == WeaponSelection::hellfire ? missile_interval : rocket_interval;
        result.fire = locked && state.weapon == unsigned(result.weapon) &&
                      (result.weapon == WeaponSelection::cannon ||
                       (!previous_.fire && elapsed(shot_, frame.serial, interval)));
        if (result.fire) {
            shot_ = frame.serial;
        }
        previous_ = result;
        return result;
    }

  private:
    static constexpr int key_limit = 350, hover_throttle = 4;
    static constexpr double full_turn = 6.283185307179586, heading_units = 4294967296.0;
    static constexpr int heading_error_scale = 0x100000, yaw_damping_scale = 0x40000;
    static constexpr std::int64_t alignment_limit = 0x20000000, stand_off = 0x10000000;
    static constexpr std::int64_t cruise_speed = 0x01000000, approach_frames = 16, response_frames = 8;
    static constexpr int drag_shift = 5, pitch_acceleration = 0x800;
    static constexpr unsigned target_interval = 4, missile_interval = 40, rocket_interval = 8;

    static bool live_objective(const std::array<comanchecd::SceneEntity, comanchecd::scene_slots>& entities,
                               std::size_t slot) {
        return slot != 0 && slot < comanchecd::player_slot && entities[slot].active &&
               entities[slot].phase == comanchecd::EntityPhase::normal && entities[slot].target != 0;
    }
    static std::size_t nearest_objective(const comanchecd::FlightState& state,
                                         const comanchecd::SpatialFrame& frame) {
        std::size_t selected = 0;
        double nearest = std::numeric_limits<double>::infinity();
        for (std::size_t slot = 1; slot < comanchecd::player_slot; ++slot) {
            if (!live_objective(frame.scene.entities, slot)) {
                continue;
            }
            const auto& entity = frame.scene.entities[slot];
            const auto dx = std::int32_t(entity.x - state.view.x), dy = std::int32_t(entity.y - state.view.y);
            const auto distance = double(dx) * dx + double(dy) * dy;
            if (distance < nearest) {
                nearest = distance;
                selected = slot;
            }
        }
        return selected;
    }
    static int key_axis(int desired, int& carry) {
        carry += desired;
        const auto key = carry > key_limit / 2 ? key_limit : carry < -key_limit / 2 ? -key_limit : 0;
        carry -= key;
        return key;
    }
    static bool elapsed(const std::optional<std::uint64_t>& previous, std::uint64_t serial,
                        unsigned interval) {
        return !previous || serial - *previous >= interval;
    }
    std::size_t pursuit_ = 0;
    std::optional<std::uint64_t> serial_, target_request_, shot_;
    int yaw_carry_ = 0, pitch_carry_ = 0;
    comanchecd::FlightControls previous_;
};
} // namespace keyboard_test
