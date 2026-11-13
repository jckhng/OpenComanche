#include "flight_entities.hpp"

#include <algorithm>
#include <stdexcept>

namespace comanchecd::flight_entities {
namespace {

void explosion_sound(const SceneEntity& entity, const FlightState& state, SoundBatch& sounds,
                     const CpuWorkSink& work = {}) {
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    auto x = magnitude(state.view.x - entity.x);
    auto y = magnitude(state.view.y - entity.y);
    instructions(16 + unsigned(x >= y));
    if (x < y) {
        std::swap(x, y);
    }
    const auto attenuation = (x + (y >> 1)) >> 29;
    if (attenuation <= 4) {
        instructions(6);
        sounds.push_back({SampleBank::effects, 1, static_cast<std::uint8_t>(attenuation)});
        instructions(1);
    }
    instructions(1);
}

}

void begin_explosion(SceneEntity& entity, std::size_t payload, const FlightState& state, SoundBatch& sounds,
                     const CpuWorkSink& work) {
    if (work) { work(CpuOperation::instructions, 12); }
    entity.phase = EntityPhase::explosion;
    entity.primary_payload = payload;
    entity.category_color = 0;
    entity.animation_frame = 1;
    entity.x = entity.previous_x;
    entity.y = entity.previous_y;
    entity.height = entity.previous_height;
    explosion_sound(entity, state, sounds, work);
}

bool spawn_explosion(MissionScene& scene, std::size_t source, std::size_t payload,
                     const FlightState& state, SoundBatch& sounds, const CpuWorkSink& work) {
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    instructions(3);
    for (std::size_t slot = scene_slots - 1; slot >= player_slot; --slot) {
        auto& entity = scene.entities[slot];
        instructions(2);
        if (entity.active) {
            instructions(2);
            continue;
        }
        instructions(14);
        entity.active = true;
        entity.phase = EntityPhase::explosion;
        entity.primary_payload = payload;
        entity.category_color = 0;
        entity.animation_frame = 1;
        entity.x = scene.entities[source].x;
        entity.y = scene.entities[source].y;
        entity.height = scene.entities[source].height;
        explosion_sound(scene.entities[source], state, sounds, work);
        return true;
    }
    instructions(1);
    return false;
}

Bearing bearing_to(const SceneEntity& entity, std::uint32_t x, std::uint32_t y, const TerrainTables& tables,
                   const CpuWorkSink& work) {
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    instructions(16);
    const auto dx = static_cast<std::int32_t>(entity.x - x);
    const auto dy = static_cast<std::int32_t>(entity.y - y);
    auto major = magnitude(std::uint32_t(dx)), minor = magnitude(std::uint32_t(dy));
    auto octant = unsigned(dy < 0) * 4 + unsigned(dx < 0) * 2;
    if (major <= minor) {
        instructions(2);
        ++octant;
        std::swap(major, minor);
    }
    const auto distance = major + (minor >> 1);
    auto angle = tables.angle_octants[octant];
    instructions(6);
    if ((major >> 10) != 0) {
        instructions(4);
        angle ^= tables.angle_ratio.at(minor / (major >> 10));
    }
    instructions(6);
    return {static_cast<std::uint8_t>(~((angle + 1) >> 1)), distance};
}

void enemy_launch(MissionScene& scene, const SceneEntity& source, std::uint8_t target,
                  std::size_t payload_count, const TerrainTables& tables, const CpuWorkSink& work) {
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    instructions(3);
    for (std::size_t slot = scene_slots - 1; slot >= player_slot; --slot) {
        auto& projectile = scene.entities[slot];
        instructions(2);
        if (projectile.active) {
            instructions(2);
            continue;
        }
        instructions(21);
        projectile.active = true;
        projectile.phase = EntityPhase::normal;
        projectile.random_phase = 0;
        projectile.category_color = 8;
        if (source.kind != 10 && source.kind != 14 && source.kind != 15 && source.kind != 16) {
            projectile.homing_target = target;
        }
        projectile.kind = source.kind == 2 || source.kind == 13 ? climbing_missile_kind :
            source.kind == 5 || source.kind == 6 || source.kind == 8 || source.kind == 11 || source.kind == 12 || source.kind == 17 ?
            stinger_kind : enemy_rocket_kind;
        projectile.primary_payload = payload_count - (projectile.kind == climbing_missile_kind ? 12 :
                                                    projectile.kind == stinger_kind ? 11 : 10);
        projectile.speed = 0x04000000;
        projectile.x = source.x;
        projectile.y = source.y;
        projectile.height = source.height;
        projectile.angle = source.angle;
        if (source.kind == 10 || source.kind == 14 || source.kind == 15 || source.kind == 16) {
            projectile.kind = source.kind == 10 ? cannon_round_kind : enemy_cannon_kind;
            projectile.primary_payload = payload_count - 9;
            projectile.animation_frame = 1;
            projectile.ballistic_height = std::uint32_t(source.height) << 24;
            if (source.kind == 10) {
                instructions(3);
                projectile.category_color = 0;
                projectile.vertical_velocity = 0x06000000;
            } else {
                instructions(24);
                const auto& victim = scene.entities.at(target);
                const auto bearing = bearing_to(source, victim.x, victim.y, tables, work);
                projectile.angle = bearing.angle;
                projectile.speed = 0x03800000;
                const auto distance = bearing.distance >> 1;
                if (distance == 0) {
                    throw std::invalid_argument("Enemy cannon target has zero firing distance");
                }
                const auto reciprocal = static_cast<std::int32_t>(0xffffffffu / distance);
                const auto height_delta = static_cast<std::int32_t>(
                    std::uint32_t((int(victim.height) - int(source.height)) >> 1) << 24);
                projectile.vertical_velocity = static_cast<std::int32_t>(
                    distance + std::uint32_t((std::int64_t(height_delta) * reciprocal) >> 7));
            }
        }
        return;
    }
    instructions(2);
}

void attack_allies(MissionScene& scene, const SceneEntity& source, const TerrainTables& tables, std::size_t payload_count,
                   const CpuWorkSink& work) {
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    instructions(3); // CALL and the enable check.
    if (!scene.attack_allies) {
        instructions(1);
        return;
    }
    instructions(3);
    if ((source.random_phase & source.attribute9) != 0) {
        instructions(1);
        return;
    }
    instructions(1);
    for (std::size_t slot = object_slots; slot > 0; --slot) {
        const auto& target = scene.entities[slot];
        instructions(2);
        if (!target.active) {
            instructions(2);
            continue;
        }
        instructions(2);
        if (target.category_color != 9) {
            instructions(2);
            if (target.category_color != 6) {
                instructions(2);
                continue;
            }
        }
        instructions(6);
        const auto bearing = bearing_to(source, target.x, target.y, tables, work);
        instructions(3);
        if (bearing.distance >= 0x2bc00000) {
            instructions(2);
            continue;
        }
        instructions(2);
        if (bearing.distance <= 0x0a000000) {
            instructions(2);
            continue;
        }
        const auto difference = static_cast<std::uint8_t>(bearing.angle - source.angle);
        const auto half = static_cast<std::int8_t>((difference >> 1) | (bearing.angle < source.angle ? 128 : 0));
        instructions(6);
        if (static_cast<std::uint8_t>(half ^ (half >> 7)) >= 16) {
            instructions(2);
            continue;
        }
        instructions(2);
        if (slot == scene.wingman) {
            instructions(2);
            if (scene.wingman_alert == 0) {
                instructions(2);
                scene.wingman_alert = 20;
                return;
            }
        }
        instructions(3);
        enemy_launch(scene, source, static_cast<std::uint8_t>(slot), payload_count, tables, work);
        instructions(1);
        return;
    }
    instructions(1);
}

} // namespace comanchecd::flight_entities
