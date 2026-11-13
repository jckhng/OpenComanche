#include "flight_entities.hpp"

#include <algorithm>
#include <stdexcept>

namespace comanchecd::flight_entities {
namespace {

void ground_movement(SceneEntity& entity, const SceneAssets& assets, const TerrainTables& tables, unsigned turn_extra,
                     const CpuWorkSink& work) {
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    const auto original_angle = entity.angle;
    unsigned attempts = 0;
    const auto attempt = [&](std::uint8_t angle) {
        entity.x = entity.previous_x;
        entity.y = entity.previous_y;
        entity.angle = angle;
        move_entity(entity, tables);
        const auto ground = unsigned(terrain_height(entity, assets)) + 9;
        instructions((attempts++ == 0 ? 19 : 23) + unsigned(ground > 255));
        return entity.height == std::min<unsigned>(ground, 255);
    };
    if (attempt(original_angle)) {
        instructions(turn_extra != 0 ? 2 : 1);
        entity.avoidance_turn = 0;
        return;
    }
    instructions(4);
    bool turn_right = (entity.avoidance_turn & 128) == 0;
    if (turn_right) {
        instructions(1);
        if (entity.avoidance_turn == 0) {
            instructions(3);
            turn_right = attempt(static_cast<std::uint8_t>(original_angle + 32));
            if (!turn_right) {
                instructions(3);
                const auto left = attempt(static_cast<std::uint8_t>(original_angle - 32));
                if (!left) {
                    instructions(3);
                    turn_right = attempt(static_cast<std::uint8_t>(original_angle + 64));
                }
            }
        }
    }
    instructions(6 + turn_extra + unsigned(!turn_right) + 5);
    const auto amount = (entity.speed >> 21) + turn_extra;
    entity.angle = static_cast<std::uint8_t>(original_angle + (turn_right ? int(amount) : -int(amount)));
    entity.avoidance_turn = turn_right ? 1 : 255;
    entity.x = entity.previous_x;
    entity.y = entity.previous_y;
}

}

void update_ground(SceneEntity& entity, std::size_t slot, MissionScene& scene, FlightState& player,
                   const SceneAssets& assets, const TerrainTables& tables,
                   std::size_t payload_count, SoundBatch& sounds, ExecutionClock* clock) {
    const CpuWorkSink work = clock ? CpuWorkSink([clock](CpuOperation op, unsigned n) { clock->execute(op, n); }) : CpuWorkSink{};
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    const auto turret = entity.kind == 2 || entity.kind == 5 || entity.kind == 15;
    instructions(turret ? 2 : 4);
    if (entity.damage_stage != 0) {
        instructions(2);
        if (entity.damage_stage == 1) {
            instructions(3);
            ++entity.damage_stage;
            spawn_explosion(scene, slot, payload_count - 8, player, sounds, work);
            if (player.armor >= 0) {
                sounds.push_back({SampleBank::speech, 1});
            }
            instructions(4);
        } else {
            instructions(3);
            if (++entity.damage_stage == 15) {
                begin_explosion(entity, payload_count - 8, player, sounds, work);
            } else {
                instructions(1);
            }
        }
        return;
    }
    ++entity.random_phase;
    save_pose(entity);
    instructions(7 + 2);
    if (!turret && entity.attribute10 != 0) {
        instructions(1);
        --entity.attribute10;
    } else if (entity.attribute7 > (turret ? 6 : 2)) {
        instructions(2);
        if (entity.attribute10 != 0) {
            instructions(1);
            --entity.attribute10;
        } else {
            ground_movement(entity, assets, tables, turret ? 1 : 0, work);
        }
    } else if (!turret) {
        instructions(2);
    }
    if (!turret) {
        instructions(1);
        return;
    }
    instructions(3);
    entity.angle = static_cast<std::uint8_t>(entity.angle + entity.rotor_phase);
    entity.height = static_cast<std::uint8_t>(entity.height + 6);
    attack_allies(scene, entity, tables, payload_count, work);
    entity.angle = static_cast<std::uint8_t>(entity.angle - entity.rotor_phase);
    entity.height = static_cast<std::uint8_t>(entity.height - 6);
    instructions(8);
    const auto bearing = bearing_to(entity, player.view.x, player.view.y, tables, work);
    auto range = (std::uint32_t(player.view.height) << 21) + 0x25800000;
    if (entity.projected_x != 0) {
        range += 0x11800000;
    }
    if (scene.extended_enemy_range) {
        range += 0x1b800000;
    }
    instructions(9 + unsigned(entity.projected_x != 0) + unsigned(scene.extended_enemy_range));
    if (bearing.distance >= range) {
        instructions(1);
        return;
    }
    bool chaff_gate = false;
    if (clock) { clock->instruction([&] { chaff_gate = (scene.random_seed & 12) != 0; }); }
    else { chaff_gate = (scene.random_seed & 12) != 0; }
    instructions(1);
    if (chaff_gate) {
        instructions(2);
        if (player.active_chaff != 0) { instructions(1); return; }
    }
    instructions(10);
    player.enemy_tracking = true;
    const auto difference = static_cast<std::int8_t>(bearing.angle - static_cast<std::uint8_t>(entity.angle + entity.rotor_phase));
    const auto error = static_cast<std::uint8_t>(difference ^ (difference >> 7));
    if (error < 13) {
        instructions(3);
        if ((entity.random_phase & entity.attribute9) == 0) {
            instructions(8);
            entity.angle = static_cast<std::uint8_t>(entity.angle + entity.rotor_phase);
            entity.height = static_cast<std::uint8_t>(entity.height + 6);
            enemy_launch(scene, entity, player_slot, payload_count, tables, work);
            entity.angle = static_cast<std::uint8_t>(entity.angle - entity.rotor_phase);
            entity.height = static_cast<std::uint8_t>(entity.height - 6);
            instructions(4);
        }
    }
    instructions(2);
    if (difference != 0) {
        instructions(4);
        const auto turn = error < 13 ? 1 : 2;
        entity.rotor_phase = static_cast<std::uint8_t>(entity.rotor_phase + (difference < 0 ? -turn : turn));
    } else {
        instructions(1);
    }
}

void update_support_vehicle(SceneEntity& entity, std::size_t slot, MissionScene& scene, FlightState& player,
                            const SceneAssets& assets, const TerrainTables& tables,
                            std::size_t payload_count, SoundBatch& sounds, ExecutionClock* clock) {
    const CpuWorkSink work = clock ? CpuWorkSink([clock](CpuOperation op, unsigned n) { clock->execute(op, n); }) : CpuWorkSink{};
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    const auto original_angle = entity.angle;
    instructions(1); // Shared 25580 movement/damage CALL.
    update_ground(entity, slot, scene, player, assets, tables, payload_count, sounds, clock);
    instructions(2);
    if (entity.damage_stage != 0) {
        if (entity.kind >= 12 && entity.kind <= 14) {
            instructions(2);
            if (entity.height != 0) {
                instructions(1);
                --entity.height;
            }
        }
        instructions(1);
        return;
    }
    if (entity.kind == 10) {
        instructions(5);
        const auto bearing = bearing_to(entity, player.view.x, player.view.y, tables, work);
        auto seed = scene.random_seed;
        if (clock) { clock->instruction([&] { seed = scene.random_seed; }); }
        instructions(4);
        if (bearing.distance >= (seed & 0x1fc00000) + 0x05000000) {
            instructions(1);
            return;
        }
        instructions(3);
        if ((entity.random_phase & entity.attribute9) != 0) {
            instructions(1);
            return;
        }
        instructions(4);
        const auto angle = entity.angle;
        entity.angle = bearing.angle;
        enemy_launch(scene, entity, player_slot, payload_count, tables, work);
        entity.angle = angle;
        instructions(3);
        return;
    }
    if (entity.kind >= 16) {
        instructions(3);
        if ((entity.random_phase & entity.attribute9) != 0) {
            instructions(1);
            return;
        }
        instructions(1);
        for (std::size_t target_slot = object_slots; target_slot > 0; --target_slot) {
            const auto& target = scene.entities[target_slot];
            instructions(2);
            if (!target.active) {
                instructions(2);
                continue;
            }
            instructions(2 + (target.category_color != 7 ? 2 : 0));
            if (target.category_color != 7 && target.category_color != 8) {
                instructions(2);
                continue;
            }
            instructions(6);
            const auto aim = bearing_to(entity, target.x, target.y, tables, work);
            instructions(3 + (aim.distance < 0x28000000 ? 2 : 0));
            if (aim.distance <= 0x0c800000 || aim.distance >= 0x28000000) {
                instructions(2);
                continue;
            }
            const auto angle = static_cast<std::uint8_t>(entity.angle - entity.rotor_phase);
            const auto delta = static_cast<std::uint8_t>(aim.angle - angle);
            const auto half = static_cast<std::int8_t>((delta >> 1) | (aim.angle < angle ? 128 : 0));
            const auto error = static_cast<std::uint8_t>(half ^ (half >> 7));
            instructions(8);
            if (error >= 16) {
                instructions(2);
                continue;
            }
            instructions(2);
            if (error != 0) {
                instructions(4);
                entity.rotor_phase = static_cast<std::uint8_t>(entity.rotor_phase + (half < 0 ? -1 : 1));
            }
            instructions(8);
            entity.attribute10 = 5;
            entity.angle = static_cast<std::uint8_t>(entity.angle + entity.rotor_phase);
            entity.height = static_cast<std::uint8_t>(entity.height + 6);
            enemy_launch(scene, entity, static_cast<std::uint8_t>(target_slot), payload_count, tables, work);
            entity.angle = static_cast<std::uint8_t>(entity.angle - entity.rotor_phase);
            entity.height = static_cast<std::uint8_t>(entity.height - 6);
            instructions(5);
            return;
        }
        instructions(1);
        return;
    }
    instructions(1); // attack_allies owns its CALL.
    entity.height = static_cast<std::uint8_t>(entity.height + 8);
    attack_allies(scene, entity, tables, payload_count, work);
    entity.height = static_cast<std::uint8_t>(entity.height - 8);
    instructions(6);
    const auto bearing = bearing_to(entity, player.view.x, player.view.y, tables, work);
    const auto range = (((std::uint32_t(scene.preset) ^ 1) << 6) + 185 +
                        (scene.extended_enemy_range ? 110 : 0)) << 22;
    instructions(9 + unsigned(scene.extended_enemy_range));
    if (bearing.distance >= range) {
        instructions(1);
        return;
    }
    instructions(2);
    if (bearing.distance > 0x05000000) {
        instructions(8);
        player.enemy_tracking = true;
        const auto difference = static_cast<std::int8_t>(bearing.angle - entity.angle);
        if (static_cast<std::uint8_t>(difference ^ (difference >> 7)) < 10) {
            instructions(3);
            if ((entity.random_phase & entity.attribute9) == 0) {
                instructions(5);
                entity.height = static_cast<std::uint8_t>(entity.height + 8);
                enemy_launch(scene, entity, player_slot, payload_count, tables, work);
                entity.height = static_cast<std::uint8_t>(entity.height - 8);
                entity.attribute10 = 5;
                instructions(2);
            }
            instructions(1);
            return;
        }
    }
    instructions(2);
    if (entity.avoidance_turn == 0) {
        instructions(5);
        entity.angle = static_cast<std::uint8_t>(original_angle - (entity.speed >> 21));
    }
    instructions(1);
}

} // namespace comanchecd::flight_entities
