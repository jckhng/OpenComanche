#include "flight_entities.hpp"

#include <algorithm>
#include <stdexcept>

namespace comanchecd::flight_entities {
namespace {

void friendly_altitude(SceneEntity& entity, const SceneAssets& assets, unsigned clearance, const CpuWorkSink& work) {
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    const auto height = terrain_height(entity, assets) + clearance;
    const auto desired = std::min<unsigned>(height, 255);
    instructions(10 + unsigned(height > 255));
    instructions(2);
    if (desired == entity.height) {
        return;
    }
    instructions(1);
    if (desired > entity.height) {
        instructions(2);
        if (desired - entity.height > 15) {
            instructions(4);
            entity.x = entity.previous_x;
            entity.y = entity.previous_y;
        }
        instructions(2);
        if (desired - entity.height > 4) {
            instructions(2 + (entity.height > 247 ? 2 : 0));
            entity.height = static_cast<std::uint8_t>(std::min<unsigned>(entity.height + 8, 255));
        }
    } else {
        instructions(2);
        if (static_cast<std::int8_t>(desired - entity.height) < -4) {
            instructions(1);
            entity.height = static_cast<std::uint8_t>(entity.height - 8);
        }
    }
}

bool acknowledge_wingman(SceneEntity& entity, const MissionScene& scene, const FlightState& player,
                         SoundBatch& sounds, const CpuWorkSink& work) {
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    instructions(2);
    if (--entity.random_phase != 0) {
        return false;
    }
    instructions(3);
    if (player.target.slot == scene.wingman) {
        instructions(2);
        sounds.push_back({SampleBank::speech, 12});
        instructions(1);
        return true;
    }
    instructions(2);
    if (player.target.slot == 0) {
        return false;
    }
    instructions(2);
    if (player.weapon != 5) {
        return false;
    }
    instructions(2);
    sounds.push_back({SampleBank::speech, 10});
    return true;
}

}

void update_aircraft(SceneEntity& entity, std::size_t slot, MissionScene& scene, FlightState& player,
                     const SceneAssets& assets, const TerrainTables& tables,
                     std::size_t payload_count, SoundBatch& sounds, ExecutionClock* clock) {
    const CpuWorkSink work = clock ? CpuWorkSink([clock](CpuOperation op, unsigned n) { clock->execute(op, n); }) : CpuWorkSink{};
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    instructions(10);
    entity.rotor_phase = static_cast<std::uint8_t>(entity.rotor_phase - 16);
    save_pose(entity);
    ++entity.random_phase;
    if (entity.damage_stage != 0) {
        instructions(2); // 25bc2 damage-stage gate.
        if (entity.damage_stage == 1) {
            instructions(3);
            ++entity.damage_stage;
            if (player.armor >= 0) {
                sounds.push_back({SampleBank::speech, 1});
            } else {
                instructions(6); // 501d..5030 suppresses the announcement after player death.
            }
        }
        entity.angle = static_cast<std::uint8_t>(entity.angle + 16);
        const auto underflow = entity.height < 4;
        entity.height = static_cast<std::uint8_t>(entity.height - 4);
        instructions(3);
        if (underflow) {
            instructions(1);
            begin_explosion(entity, payload_count - 8, player, sounds, work);
            return;
        }
        const auto ground = terrain_height(entity, assets);
        instructions(11); // 25be8..25c12 terrain lookup and impact gate.
        if (ground >= entity.height) {
            instructions(1);
            begin_explosion(entity, payload_count - 8, player, sounds, work);
            return;
        }
        instructions(2);
        if (entity.damage_stage <= 2) {
            instructions(3);
            if (static_cast<std::uint8_t>(ground + 15) >= entity.height) {
                instructions(2);
                ++entity.damage_stage;
                spawn_explosion(scene, slot, payload_count - 8, player, sounds, work);
                return;
            }
        }
        instructions(1);
        return;
    }
    instructions(2);
    if (entity.attribute10 != 0) {
        instructions(2);
        --entity.attribute10;
        return;
    }
    instructions(19);
    move_entity(entity, tables);
    const auto clearance = terrain_height(entity, assets) + 36u;
    const auto desired = std::min<unsigned>(clearance, 255);
    if (clearance > 255) { instructions(1); }
    if (desired > entity.height) {
        instructions(3);
        const auto difference = desired - entity.height;
        if (difference > 20) {
            instructions(4);
            entity.x = entity.previous_x;
            entity.y = entity.previous_y;
        }
        instructions(2);
        if (difference > 4) {
            instructions(2);
            if (entity.height > 247) { instructions(2); }
            entity.height = static_cast<std::uint8_t>(std::min<unsigned>(entity.height + 8, 255));
        }
    } else if (desired < entity.height) {
        instructions(4);
        if (entity.height - desired > 4) {
            instructions(1);
            entity.height = static_cast<std::uint8_t>(entity.height - 8);
        }
    }
    attack_allies(scene, entity, tables, payload_count, work);
    instructions(5);
    const auto bearing = bearing_to(entity, player.view.x, player.view.y, tables, work);
    const auto range = (((std::uint32_t(scene.preset) ^ 1) << 6) + 185 +
                        (scene.extended_enemy_range ? 110 : 0)) << 22;
    const auto difference = static_cast<std::int8_t>(bearing.angle - entity.angle);
    const auto error = static_cast<std::uint8_t>(difference ^ (difference >> 7));
    instructions(9 + unsigned(scene.extended_enemy_range));
    const auto seed = [&] {
        auto value = scene.random_seed;
        if (clock) { clock->instruction([&] { value = scene.random_seed; }); }
        return value;
    };
    if (bearing.distance >= range) {
        const auto value = seed();
        instructions(4);
        entity.angle = static_cast<std::uint8_t>(entity.angle + (value & 3) - 2);
        return;
    }
    instructions(7);
    if (error < 14) {
        instructions(3);
        if ((entity.random_phase & entity.attribute9) == 0) {
            instructions(4);
            enemy_launch(scene, entity, player_slot, payload_count, tables, work);
        }
        instructions(3 + unsigned(player.view.height < 10));
        const auto minimum = player.view.height < 10 ? 10 : player.view.height - 10;
        instructions(2);
        if (entity.height <= minimum) {
            instructions(4);
            entity.height = static_cast<std::uint8_t>(entity.height > 239 ? 254 : entity.height + 10);
        } else {
            instructions(1);
        }
        return;
    }
    instructions(2);
    if (bearing.distance <= 0x05000000) {
        const auto value = seed();
        instructions(3);
        entity.angle = static_cast<std::uint8_t>(entity.angle + (value & 7));
        return;
    }
    instructions(2);
    if (bearing.distance < 0x19000000) {
        instructions(2);
        if (error < 70) {
            instructions(2);
            if (difference == 0) { instructions(1); return; }
            instructions(4);
            entity.angle = static_cast<std::uint8_t>(entity.angle + (difference < 0 ? -12 : 12));
            return;
        }
    }
    const auto value = seed();
    instructions(4);
    entity.angle = static_cast<std::uint8_t>(entity.angle + (value & 3) - 2);
}

bool launch_wingman(MissionScene& scene, FlightState& player, std::size_t payload,
                    SoundBatch& sounds, const std::function<void(const FlightState&)>& stores,
                    const CpuWorkSink& work) {
    constexpr auto weapon = static_cast<unsigned>(WeaponSelection::wingman);
    const auto instructions = [&](unsigned count) { if (work) { work(CpuOperation::instructions, count); } };
    const auto empty_stores = [&] {
        instructions(3); // 245a0 clears ammunition, sets the redraw flag and calls 7c10.
        player.ammunition[weapon] = 0;
        if (stores) { stores(player); }
        instructions(1);
    };
    instructions(2);
    if (player.ammunition[weapon] == 0) {
        empty_stores();
        return false;
    }
    instructions(2);
    std::size_t slot = scene_slots - 1;
    for (; slot >= player_slot; --slot) {
        instructions(2);
        if (!scene.entities[slot].active) { break; }
        instructions(2);
    }
    if (slot < player_slot) {
        instructions(1);
        return false;
    }
    instructions(5);
    --player.ammunition[weapon];
    if (scene.wingman != 0) { instructions(2); }
    if (scene.wingman == 0 || !scene.entities.at(scene.wingman).active) {
        empty_stores();
        return false;
    }
    instructions(3);
    if (player.weapon == weapon) {
        instructions(2);
        if (stores) { stores(player); }
    }
    instructions(5);
    sounds.push_back({SampleBank::effects, missile_sound, 1});
    const auto& wingman = scene.entities[scene.wingman];
    auto& missile = scene.entities[slot];
    missile.active = true;
    missile.phase = EntityPhase::normal;
    missile.kind = guided_missile_kind;
    missile.random_phase = 0;
    missile.category_color = 0;
    missile.primary_payload = payload;
    missile.homing_target = static_cast<std::uint8_t>(player.target.slot);
    missile.speed = 0x04000000;
    missile.x = wingman.x;
    missile.y = wingman.y;
    missile.height = wingman.height;
    missile.angle = wingman.angle;
    instructions(22); // 245f8..24681 copies the wingman's launch pose and returns.
    return true;
}

void update_friendly_aircraft(SceneEntity& entity, std::size_t slot, MissionScene& scene, FlightState& player,
                              const SceneAssets& assets, const TerrainTables& tables,
                              const std::array<std::int32_t, 2>& movement_basis,
                              std::size_t payload_count, SoundBatch& sounds, ExecutionClock* clock) {
    const CpuWorkSink work = clock ? CpuWorkSink([clock](CpuOperation op, unsigned n) { clock->execute(op, n); }) : CpuWorkSink{};
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    const auto wingman = entity.kind == 4;
    const auto autonomous = !wingman || scene.wingman_alert != 0;
    if (wingman) {
        instructions(2 + (entity.damage_stage == 0 ? 2 : 0));
    }
    if (!wingman || entity.damage_stage == 0) {
        if (wingman && autonomous) {
            instructions(1);
            --scene.wingman_alert;
        }
        instructions(7 + (autonomous ? 2 : 0));
        entity.rotor_phase = static_cast<std::uint8_t>(entity.rotor_phase - 16);
        save_pose(entity);
    }
    if (entity.damage_stage != 0) {
        instructions(2);
        if (entity.damage_stage == 1) {
            instructions(1);
            ++entity.damage_stage;
            if (wingman) {
                instructions(3);
                scene.wingman_lost = true;
                player.ammunition[5] = 0;
                sounds.push_back({SampleBank::speech, 5});
                instructions(1);
            }
        }
        instructions(3);
        entity.angle = static_cast<std::uint8_t>(entity.angle + 16);
        const auto underflow = entity.height < 4;
        entity.height = static_cast<std::uint8_t>(entity.height - 4);
        if (underflow) {
            instructions(1);
            begin_explosion(entity, payload_count - 8, player, sounds, work);
            return;
        }
        const auto ground = terrain_height(entity, assets);
        instructions(11);
        if (ground >= entity.height) {
            instructions(1);
            begin_explosion(entity, payload_count - 8, player, sounds, work);
            return;
        }
        instructions(2);
        if (entity.damage_stage > 2) {
            instructions(1);
            return;
        }
        instructions(3);
        if (static_cast<std::uint8_t>(ground + 15) >= entity.height) {
            instructions(2);
            ++entity.damage_stage;
            spawn_explosion(scene, slot, payload_count - 8, player, sounds, work);
            return;
        }
        instructions(1);
        return;
    }
    auto x = player.view.x, y = player.view.y;
    if (!autonomous) {
        instructions(6);
        if (player.weapon == 5) {
            instructions(3);
        }
        if (player.weapon == 5 && player.target.slot != 0) {
            instructions(6 + unsigned(entity.height > 246));
            const auto& target = scene.entities.at(player.target.slot);
            x = target.x;
            y = target.y;
            entity.height = static_cast<std::uint8_t>(std::min<unsigned>(entity.height + 9, 255));
        }
        constexpr auto formation_offset = static_cast<std::int32_t>(0xee800000);
        // The initial draw precedes the first controller update of 1a954/1a958.
        x += multiply_high(formation_offset, movement_basis[1]);
        y += multiply_high(formation_offset, movement_basis[0]);
        instructions(7);
        const auto bearing = bearing_to(entity, x, y, tables, work);
        const auto difference = static_cast<std::int8_t>(bearing.angle - entity.angle);
        bool acknowledged = false;
        instructions(3 + (difference != 0 ? 3 : 0));
        if (difference > 4 || difference < -4) {
            instructions(3);
            entity.angle = static_cast<std::uint8_t>(entity.angle + (difference < 0 ? -8 : 8));
            entity.random_phase = 15;
        } else {
            acknowledged = acknowledge_wingman(entity, scene, player, sounds, work);
        }
        // 501d replaces ESI with the speech bank before the caller's distance test.
        instructions(2);
        if (!acknowledged && bearing.distance > 0x19000000) {
            instructions(7);
            move_entity(entity, tables);
        }
        friendly_altitude(entity, assets, 34, work);
        instructions(1);
        return;
    }
    instructions(2);
    if (entity.attribute10 != 0) {
        instructions(2);
        --entity.attribute10;
        return;
    }
    instructions(5);
    const auto bearing = bearing_to(entity, x, y, tables, work);
    const auto difference = static_cast<std::int8_t>(bearing.angle - entity.angle);
    instructions(2);
    if (bearing.distance > 0x32000000) {
        instructions(3 + (difference != 0 ? 3 : 0));
        if (difference > 16) {
            instructions(2);
            entity.angle = static_cast<std::uint8_t>(entity.angle + 2);
        } else if (difference < 0 && difference >= -16) {
            // The original branch enters the wingman's acknowledgement/altitude tail.
            const auto acknowledged = acknowledge_wingman(entity, scene, player, sounds, work);
            instructions(2);
            if (!acknowledged) {
                instructions(7);
                move_entity(entity, tables);
            }
            friendly_altitude(entity, assets, 34, work);
            instructions(1);
            return;
        } else if (difference < -16) {
            instructions(1);
            entity.angle = static_cast<std::uint8_t>(entity.angle - 2);
        }
    }
    instructions(7);
    move_entity(entity, tables);
    friendly_altitude(entity, assets, 50, work);
    instructions(4);
    if ((++entity.random_phase & entity.attribute9) != 0) {
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
        const auto delta = static_cast<std::uint8_t>(aim.angle - entity.angle);
        const auto half = static_cast<std::int8_t>((delta >> 1) | (aim.angle < entity.angle ? 128 : 0));
        instructions(6);
        if (static_cast<std::uint8_t>(half ^ (half >> 7)) < 16) {
            instructions(2);
            auto launcher = entity;
            launcher.kind = 11;
            enemy_launch(scene, launcher, static_cast<std::uint8_t>(target_slot), payload_count, tables, work);
            instructions(1);
            return;
        }
        instructions(2);
    }
    instructions(1);
}

} // namespace comanchecd::flight_entities
