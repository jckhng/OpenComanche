#include "flight_entities.hpp"

#include <algorithm>
#include <stdexcept>

namespace comanchecd::flight_entities {
namespace {
constexpr std::uint32_t ballistic_gravity = 0x00100000;
constexpr std::uint32_t cannon_contact_distance = 0x01c00000;
constexpr std::uint8_t cannon_end_frame = 7;
constexpr std::uint8_t missile_damage = 80;
constexpr std::uint32_t missile_contact_distance = 0x01400000;
constexpr int missile_turn_limit = 20;
constexpr std::uint8_t missile_descent_age = 6;

void guide_missile(SceneEntity& missile, const SceneEntity& target, const TerrainTables& tables, const CpuWorkSink& work = {}) {
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    instructions(5 + 16);
    const auto dx = static_cast<std::int32_t>(missile.x - target.x);
    const auto dy = static_cast<std::int32_t>(missile.y - target.y);
    auto major = magnitude(static_cast<std::uint32_t>(dx));
    auto minor = magnitude(static_cast<std::uint32_t>(dy));
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
    const auto bearing = static_cast<std::uint8_t>(~((angle + 1) >> 1));
    const auto difference = static_cast<std::uint8_t>(bearing - missile.angle);
    instructions(6 + 2);
    if (difference != 0) {
        const auto half = static_cast<std::uint8_t>((difference >> 1) | (bearing < missile.angle ? 128 : 0));
        // RCR preserves SF from SUB. The branch uses the unhalved byte's sign.
        const auto negative = (difference & 128) != 0;
        const auto amount = negative ? static_cast<std::uint8_t>(-half) : half;
        instructions(2 + (negative ? 5 : 3) + unsigned(amount > missile_turn_limit));
        const auto turn = std::min<int>(amount, missile_turn_limit);
        missile.angle = static_cast<std::uint8_t>(missile.angle + (negative ? -turn : turn));
    }
    instructions(3);
    if (missile.height < 250) {
        instructions(1);
        missile.height = static_cast<std::uint8_t>(missile.height + 3);
    }
    instructions(3);
    if (target.height >= missile.height) {
        instructions(2);
        if (target.height - missile.height > 4) {
            instructions(2 + (missile.height > 247 ? 2 : 0));
            missile.height = static_cast<std::uint8_t>(std::min<unsigned>(missile.height + 8, 255));
        }
        return;
    }
    instructions(4);
    if (missile.random_phase < missile_descent_age) {
        return;
    }
    if (missile.speed == 0) {
        throw std::invalid_argument("Guided missile has zero speed");
    }
    instructions(7);
    const auto duration = (distance << 1) / missile.speed;
    auto descent = unsigned(missile.height - target.height - 1);
    if (duration != 0) {
        instructions(2);
        descent /= duration;
    }
    descent = static_cast<std::uint8_t>(descent + 3);
    instructions(3 + unsigned(missile.height < descent));
    missile.height = static_cast<std::uint8_t>(missile.height < descent ? 0 : missile.height - descent);
}

void cannon_impact(SceneEntity& entity, const CpuWorkSink& work = {}) {
    if (work) { work(CpuOperation::instructions, 8); }
    ++entity.animation_frame;
    entity.x = entity.previous_x;
    entity.y = entity.previous_y;
    entity.height = entity.previous_height;
}

void hit_player(SceneEntity& entity, MissionScene& scene, FlightState& player,
                std::size_t explosion_payload, SoundBatch& sounds, ExecutionClock* clock) {
    const auto work = clock ? CpuWorkSink([&](CpuOperation op, unsigned n) { clock->execute(op, n); }) : CpuWorkSink{};
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    const auto instruction = [&](const std::function<void()>& action) {
        if (clock) { clock->instruction(action); }
        else { action(); }
    };
    bool flare_test = false;
    instruction([&] { flare_test = (scene.random_seed & 12) != 0; });
    instructions(1);
    if (flare_test) { instructions(2); }
    if (flare_test && player.active_flares != 0) {
        instructions(3);
        entity.homing_target = 0;
        return;
    }
    instructions(2);
    if (player.exit_requested != 0) {
        instructions(1);
        return;
    }
    instructions(8); // 2663a..26657 through the sound CALL.
    player.incoming_hit = true;
    player.shake_ticks = hit_shake_ticks;
    sounds.push_back({SampleBank::effects, 5});
    instructions(2);
    instruction([&] { scene.random_seed += player.view.x; });
    std::uint32_t random = 0;
    instruction([&] { random = scene.random_seed & 255; });
    instructions(5);
    --player.system_condition[random & 7];
    --player.system_condition[random >> 5];
    instruction([&] { scene.random_seed = (scene.random_seed >> 8) | (scene.random_seed << 24); });
    instruction([&] { random = scene.random_seed; });
    instructions(4);
    player.armor = static_cast<std::int32_t>(std::uint32_t(player.armor) - (random & 3) - 2);
    if (player.armor < 0) {
        instructions(2);
        player.exit_requested = 1;
    }
    begin_explosion(entity, explosion_payload, player, sounds, work);
}

void missile_contacts(SceneEntity& entity, MissionScene& scene, FlightState& player,
                      std::size_t explosion_payload, SoundBatch& sounds, ExecutionClock* clock = nullptr) {
    const auto work = clock ? CpuWorkSink([&](CpuOperation op, unsigned n) { clock->execute(op, n); }) : CpuWorkSink{};
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    instructions(2);
    if (entity.random_phase < 3) { instructions(1); return; }
    instructions(1);
    for (std::size_t slot = object_slots; slot > 0; --slot) {
        auto& target = scene.entities[slot];
        instructions(2);
        if (!target.active) { instructions(2); continue; }
        instructions(7);
        if (height_distance(entity.height, target.height) > 20) { instructions(2); continue; }
        instructions(6);
        if (magnitude(entity.x - target.x) > missile_contact_distance) { instructions(2); continue; }
        instructions(6);
        if (magnitude(entity.y - target.y) > missile_contact_distance) { instructions(2); continue; }
        instructions(1);
        auto random = scene.random_seed;
        if (clock) { clock->instruction([&] { random = scene.random_seed; }); }
        instructions(1);
        const auto damage = (random & 0xc0) != 0 ? 8u : 6u;
        if (damage == 6) { instructions(3); }
        instructions(2);
        if (target.attribute7 < damage) {
            instructions(1);
            target.damage_stage |= 1;
        }
        target.attribute7 = static_cast<std::uint8_t>(target.attribute7 - damage);
        entity.previous_height = target.height;
        instructions(3);
        begin_explosion(entity, explosion_payload, player, sounds, work);
        return;
    }
    instructions(7);
    if (height_distance(entity.height, player.view.height) > 15) { instructions(1); return; }
    instructions(6);
    if (magnitude(entity.x - player.view.x) > missile_contact_distance) { instructions(1); return; }
    instructions(6);
    if (magnitude(entity.y - player.view.y) > missile_contact_distance) { instructions(1); return; }
    hit_player(entity, scene, player, explosion_payload, sounds, clock);
}

}

void update_ballistic(SceneEntity& entity, MissionScene& scene, FlightState& player,
                      const SceneAssets& assets, const TerrainTables& tables,
                      std::size_t explosion_payload, SoundBatch& sounds, ExecutionClock* clock) {
    const auto work = clock ? CpuWorkSink([&](CpuOperation op, unsigned n) { clock->execute(op, n); }) : CpuWorkSink{};
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    const auto cannon = entity.kind == cannon_round_kind;
    if (cannon) { instructions(2); }
    if (cannon && entity.animation_frame > 1) {
        instructions(3);
        if (++entity.animation_frame > cannon_end_frame) {
            instructions(1);
            entity.active = false;
        }
        instructions(1);
        return;
    }
    instructions(17 + unsigned(cannon));
    entity.previous_x = entity.x;
    entity.previous_y = entity.y;
    entity.previous_height = entity.height;
    if (cannon) {
        ++entity.random_phase;
    }
    entity.y -= multiply_high(static_cast<std::int32_t>(entity.speed), tables.sine[entity.angle * 2 + 128]);
    entity.x += multiply_high(static_cast<std::int32_t>(entity.speed), tables.sine[entity.angle * 2]);
    entity.vertical_velocity = static_cast<std::int32_t>(std::uint32_t(entity.vertical_velocity) - ballistic_gravity);
    const auto old_height = entity.ballistic_height;
    entity.ballistic_height += static_cast<std::uint32_t>(entity.vertical_velocity);
    const auto carry = entity.ballistic_height < old_height;
    instructions(2);
    if (entity.vertical_velocity >= 0 && carry) {
        instructions(2);
        entity.active = false;
        return;
    }
    const auto impact = [&] {
        if (cannon) {
            cannon_impact(entity, work);
        } else {
            begin_explosion(entity, explosion_payload, player, sounds, work);
        }
    };
    if (entity.vertical_velocity < 0 && !carry) {
        if (!cannon) { instructions(1); }
        impact();
        return;
    }
    if (entity.vertical_velocity >= 0) { instructions(1); }
    instructions(13);
    entity.height = static_cast<std::uint8_t>(entity.ballistic_height >> 24);
    const auto position = std::size_t(entity.y >> 22) * terrain_side + (entity.x >> 22);
    const auto ground = static_cast<std::uint8_t>((assets.terrain.height[position] << 1) + 1);
    if (entity.height <= ground) {
        if (cannon) {
            instructions(2);
            entity.animation_frame = 3;
        }
        impact();
        return;
    }
    if (cannon) { instructions(2); }
    if (cannon && (entity.random_phase & 128) != 0) {
        impact();
        return;
    }
    if (cannon) { instructions(2); }
    if (cannon && entity.random_phase <= 1) {
        instructions(1);
        return;
    }
    instructions(1);
    const auto radius = cannon ? cannon_contact_distance : missile_contact_distance;
    const auto height_radius = cannon ? 20u : 15u;
    for (std::size_t slot = object_slots; slot > 0; --slot) {
        auto& target = scene.entities[slot];
        instructions(2);
        if (!target.active) { instructions(2); continue; }
        instructions(7);
        if (height_distance(entity.height, target.height) > height_radius) { instructions(2); continue; }
        instructions(6);
        if (magnitude(entity.x - target.x) > radius) { instructions(2); continue; }
        instructions(6);
        if (magnitude(entity.y - target.y) > radius) { instructions(2); continue; }
        auto damage = 1u;
        if (!cannon) {
            instructions(1);
            auto random = scene.random_seed;
            if (clock) { clock->instruction([&] { random = scene.random_seed; }); }
            instructions(1);
            damage = (random & 0xc0) != 0 ? 6u : 5u;
            if (damage == 5) { instructions(3); }
        }
        instructions(2);
        if (target.attribute7 < damage) {
            instructions(1);
            target.damage_stage |= 1;
        }
        target.attribute7 = static_cast<std::uint8_t>(target.attribute7 - damage);
        if (!cannon) { instructions(1); }
        impact();
        return;
    }
    if (!cannon) { instructions(1); return; }
    instructions(7);
    if (height_distance(entity.height, player.view.height) > 12) { instructions(1); return; }
    instructions(6);
    if (magnitude(entity.x - player.view.x) > missile_contact_distance) { instructions(1); return; }
    instructions(6);
    if (magnitude(entity.y - player.view.y) > missile_contact_distance) { instructions(1); return; }
    instructions(2);
    ++entity.animation_frame;
    hit_player(entity, scene, player, explosion_payload, sounds, clock);
}

void update_guided_missile(SceneEntity& entity, MissionScene& scene, FlightState& player,
                           const SceneAssets& assets, const TerrainTables& tables,
                           std::size_t explosion_payload, SoundBatch& sounds, ExecutionClock* clock) {
    const auto work = clock ? CpuWorkSink([&](CpuOperation op, unsigned n) { clock->execute(op, n); }) : CpuWorkSink{};
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    instructions(8);
    save_pose(entity);
    ++entity.random_phase;
    if ((entity.random_phase & 128) != 0) {
        begin_explosion(entity, explosion_payload, player, sounds, work);
        return;
    }
    instructions(3);
    if (player.target.slot != 0) {
        guide_missile(entity, scene.entities.at(player.target.slot), tables, work);
    }
    instructions(7 + 11);
    move_entity(entity, tables);
    const auto ground = static_cast<std::uint8_t>(terrain_height(entity, assets) + 1);
    if (entity.height <= ground) {
        begin_explosion(entity, explosion_payload, player, sounds, work);
        return;
    }
    instructions(2);
    if (entity.random_phase <= 3) { instructions(1); return; }
    instructions(1);
    for (std::size_t slot = object_slots; slot > 0; --slot) {
        auto& target = scene.entities[slot];
        instructions(2);
        if (!target.active) { instructions(2); continue; }
        instructions(7);
        if (height_distance(entity.height, target.height) > 15) { instructions(2); continue; }
        instructions(6);
        if (magnitude(entity.x - target.x) > missile_contact_distance) { instructions(2); continue; }
        instructions(6);
        if (magnitude(entity.y - target.y) > missile_contact_distance) { instructions(2); continue; }
        instructions(2);
        if (target.attribute7 < missile_damage) {
            instructions(1);
            target.damage_stage |= 1;
        }
        target.attribute7 = static_cast<std::uint8_t>(target.attribute7 - missile_damage);
        entity.previous_height = target.height;
        instructions(3);
        begin_explosion(entity, explosion_payload, player, sounds, work);
        return;
    }
    instructions(1);
}

void update_enemy_missile(SceneEntity& entity, MissionScene& scene, FlightState& player,
                          const SceneAssets& assets, const TerrainTables& tables,
                          std::size_t explosion_payload, SoundBatch& sounds, ExecutionClock* clock) {
    const auto work = clock ? CpuWorkSink([&](CpuOperation op, unsigned n) { clock->execute(op, n); }) : CpuWorkSink{};
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    instructions(8);
    save_pose(entity);
    if ((++entity.random_phase & 128) != 0) {
        begin_explosion(entity, explosion_payload, player, sounds, work);
        return;
    }
    if (entity.kind == stinger_kind) { instructions(3 + (entity.homing_target != 0 ? 2 : 0)); }
    if (entity.kind == stinger_kind && entity.homing_target != 0 && scene.entities.at(entity.homing_target).active) {
        const auto& target = scene.entities[entity.homing_target];
        instructions(5);
        const auto bearing = bearing_to(entity, target.x, target.y, tables, work).angle;
        const auto difference = static_cast<std::uint8_t>(bearing - entity.angle);
        instructions(2);
        if (difference != 0) {
            const auto half = static_cast<std::uint8_t>((difference >> 1) | (bearing < entity.angle ? 128 : 0));
            const auto negative = (difference & 128) != 0;
            const auto amount = negative ? static_cast<std::uint8_t>(-half) : half;
            instructions(2 + (negative ? 5 : 3) + unsigned(amount > 25));
            const auto turn = std::min<unsigned>(amount, 25);
            entity.angle = static_cast<std::uint8_t>(entity.angle + (negative ? -int(turn) : int(turn)));
        }
    }
    instructions(7);
    move_entity(entity, tables);
    const auto ground = terrain_height(entity, assets);
    if (entity.kind == enemy_cannon_kind) {
        instructions(5);
        const auto old_height = entity.ballistic_height;
        entity.ballistic_height += static_cast<std::uint32_t>(entity.vertical_velocity);
        if (entity.vertical_velocity < 0 && entity.ballistic_height >= old_height) {
            begin_explosion(entity, explosion_payload, player, sounds, work);
            return;
        }
        if (entity.vertical_velocity >= 0 && entity.ballistic_height < old_height) {
            instructions(1);
            entity.ballistic_height = 0xffffffff;
        }
        if (entity.vertical_velocity < 0) { instructions(1); }
        instructions(15);
        entity.height = static_cast<std::uint8_t>(entity.ballistic_height >> 24);
        entity.vertical_velocity = static_cast<std::int32_t>(std::uint32_t(entity.vertical_velocity) - 0x02000000);
        if (entity.height <= static_cast<std::uint8_t>(ground + 1)) {
            begin_explosion(entity, explosion_payload, player, sounds, work);
            return;
        }
        instructions(1);
    } else if (entity.kind == enemy_rocket_kind) {
        instructions(11);
        if (entity.height <= static_cast<std::uint8_t>(ground + 1)) {
            begin_explosion(entity, explosion_payload, player, sounds, work);
            return;
        }
        instructions(2);
        --entity.height;
    } else if (entity.kind == climbing_missile_kind) {
        instructions(4 + unsigned(entity.height != scene.entities.at(entity.homing_target).height));
        const auto target_height = scene.entities.at(entity.homing_target).height;
        if (entity.height > target_height) {
            instructions(2);
            const auto underflow = entity.height < 5;
            entity.height = static_cast<std::uint8_t>(entity.height - 5);
            if (underflow) {
                begin_explosion(entity, explosion_payload, player, sounds, work);
                return;
            }
            instructions(1);
        } else if (entity.height < target_height) {
            instructions(2);
            const auto overflow = entity.height > 237;
            entity.height = static_cast<std::uint8_t>(entity.height + 18);
            if (overflow) {
                instructions(3);
                entity.height = 254;
                entity.random_phase = static_cast<std::uint8_t>(entity.random_phase + 10);
                if ((entity.random_phase & 128) != 0) {
                    begin_explosion(entity, explosion_payload, player, sounds, work);
                    return;
                }
            }
        }
        instructions(10);
        if (entity.height < ground) {
            begin_explosion(entity, explosion_payload, player, sounds, work);
            return;
        }
        instructions(1);
    } else {
        instructions(10 + unsigned(ground + 20 > 255));
        auto desired = std::min<unsigned>(ground + 20, 255);
        instructions(3);
        if (entity.homing_target != 0) {
            instructions(2 + unsigned(desired <= scene.entities.at(entity.homing_target).height));
            desired = std::max<unsigned>(desired, scene.entities.at(entity.homing_target).height);
        }
        instructions(2);
        if (desired != entity.height) { instructions(1); }
        if (desired > entity.height) {
            instructions(2);
            if (desired - entity.height > 10) {
                instructions(2);
                const auto overflow = entity.height > 239;
                if (overflow) { instructions(2); }
                entity.height = static_cast<std::uint8_t>(std::min<unsigned>(entity.height + 16, 255));
            }
        } else if (desired < entity.height) {
            instructions(3);
            if (entity.height - desired > 8) {
                instructions(2);
                const auto underflow = entity.height < 16;
                entity.height = static_cast<std::uint8_t>(entity.height - 16);
                if (underflow) {
                    begin_explosion(entity, explosion_payload, player, sounds, work);
                    return;
                }
            }
        }
        instructions(5);
        if (entity.height <= static_cast<std::uint8_t>(ground + 1)) {
            begin_explosion(entity, explosion_payload, player, sounds, work);
            return;
        }
    }
    missile_contacts(entity, scene, player, explosion_payload, sounds, clock);
}

void update_artillery(SceneEntity& entity, MissionScene& scene, FlightState& player,
                      std::size_t explosion_payload, SoundBatch& sounds, const CpuWorkSink& work) {
    const auto instructions = [&](unsigned n) { if (work) { work(CpuOperation::instructions, n); } };
    ++entity.random_phase;
    instructions(4); // 25d90 age increment and first cue gate.
    if (entity.random_phase == 100) {
        instructions(5);
        sounds.push_back({SampleBank::effects, 2});
        return;
    }
    instructions(2);
    if (entity.random_phase == 70) {
        instructions(2);
        sounds.push_back({SampleBank::speech, 9});
        return;
    }
    instructions(2);
    if (entity.random_phase < 120) {
        instructions(1);
        return;
    }
    instructions(6);
    sounds.push_back({SampleBank::effects, 6});
    player.shake_ticks = 5;
    entity.phase = EntityPhase::explosion;
    entity.category_color = 0;
    entity.primary_payload = explosion_payload;
    entity.animation_frame = 1;
    instructions(9);
    constexpr std::uint32_t blast_radius = 0x0c800000;
    for (std::size_t slot = object_slots; slot > 0; --slot) {
        auto& target = scene.entities[slot];
        instructions(2);
        if (!target.active) { instructions(2); continue; }
        instructions(7);
        if (height_distance(entity.height, target.height) > 50) { instructions(2); continue; }
        instructions(6);
        if (magnitude(entity.x - target.x) > blast_radius) { instructions(2); continue; }
        instructions(6);
        if (magnitude(entity.y - target.y) > blast_radius) { instructions(2); continue; }
        constexpr unsigned damage = 100;
        instructions(2);
        if (target.attribute7 < damage) {
            instructions(1);
            target.damage_stage |= 1;
        }
        target.attribute7 = static_cast<std::uint8_t>(target.attribute7 - damage);
        instructions(4);
        spawn_explosion(scene, slot, explosion_payload, player, sounds, work);
        instructions(4); // Restore registers, decrement and loop.
    }
    instructions(7);
    if (height_distance(entity.height, player.view.height) > 50) { instructions(1); return; }
    instructions(6);
    if (magnitude(entity.x - player.view.x) > blast_radius) { instructions(1); return; }
    instructions(6);
    if (magnitude(entity.y - player.view.y) > blast_radius) { instructions(1); return; }
    player.armor = -1;
    player.exit_requested = 1;
    player.shake_ticks = hit_shake_ticks;
    instructions(9);
    sounds.push_back({SampleBank::effects, 5});
    instructions(2);
}

} // namespace comanchecd::flight_entities
