#pragma once

#include "comanchecd/flight.hpp"
#include "comanchecd/execution_clock.hpp"

namespace comanchecd::flight_entities {
// Private entity operations share the controller's retained state and ordered sound sink.
constexpr std::uint16_t missile_sound = 4;
constexpr std::size_t object_slots = 60;
constexpr std::uint32_t hit_shake_ticks = 10;

struct SoundBatch {
    std::vector<SoundCue> values;
    SoundCueHandler handler;

    void push_back(const SoundCue& cue) {
        values.push_back(cue);
        if (handler) { handler(cue); }
    }
};

struct Bearing {
    std::uint8_t angle;
    std::uint32_t distance;
};

inline std::int32_t multiply_high(std::int32_t first, std::int32_t second) {
    return static_cast<std::int32_t>((std::int64_t(first) * second) >> 32);
}

// Preserve the original sign-mask XOR, including its one's-complement negative result.
inline std::uint32_t magnitude(std::uint32_t difference) {
    const auto signed_value = static_cast<std::int32_t>(difference);
    return difference ^ static_cast<std::uint32_t>(signed_value >> 31);
}

inline unsigned height_distance(std::uint8_t first, std::uint8_t second) {
    const auto half = (int(first) - int(second)) >> 1;
    return static_cast<unsigned>(half ^ (half >> 31));
}

inline void save_pose(SceneEntity& entity) {
    entity.previous_x = entity.x;
    entity.previous_y = entity.y;
    entity.previous_height = entity.height;
}

inline void move_entity(SceneEntity& entity, const TerrainTables& tables) {
    entity.y -= multiply_high(static_cast<std::int32_t>(entity.speed), tables.sine[entity.angle * 2 + 128]);
    entity.x += multiply_high(static_cast<std::int32_t>(entity.speed), tables.sine[entity.angle * 2]);
}

inline std::uint8_t terrain_height(const SceneEntity& entity, const SceneAssets& assets) {
    return static_cast<std::uint8_t>(assets.terrain.height[std::size_t(entity.y >> 22) * terrain_side + (entity.x >> 22)] << 1);
}

void begin_explosion(SceneEntity& entity, std::size_t payload, const FlightState& state, SoundBatch& sounds,
                     const CpuWorkSink& work = {});

bool spawn_explosion(MissionScene& scene, std::size_t source, std::size_t payload,
                     const FlightState& state, SoundBatch& sounds, const CpuWorkSink& work = {});

Bearing bearing_to(const SceneEntity& entity, std::uint32_t x, std::uint32_t y, const TerrainTables& tables,
                   const CpuWorkSink& work = {});

void enemy_launch(MissionScene& scene, const SceneEntity& source, std::uint8_t target,
                  std::size_t payload_count, const TerrainTables& tables, const CpuWorkSink& work = {});

void attack_allies(MissionScene& scene, const SceneEntity& source, const TerrainTables& tables, std::size_t payload_count,
                   const CpuWorkSink& work = {});

void update_ground(SceneEntity& entity, std::size_t slot, MissionScene& scene, FlightState& player,
                   const SceneAssets& assets, const TerrainTables& tables,
                   std::size_t payload_count, SoundBatch& sounds, ExecutionClock* clock = nullptr);

void update_support_vehicle(SceneEntity& entity, std::size_t slot, MissionScene& scene, FlightState& player,
                            const SceneAssets& assets, const TerrainTables& tables,
                            std::size_t payload_count, SoundBatch& sounds, ExecutionClock* clock = nullptr);

void update_aircraft(SceneEntity& entity, std::size_t slot, MissionScene& scene, FlightState& player,
                     const SceneAssets& assets, const TerrainTables& tables,
                     std::size_t payload_count, SoundBatch& sounds, ExecutionClock* clock = nullptr);

bool launch_wingman(MissionScene& scene, FlightState& player, std::size_t payload,
                    SoundBatch& sounds, const std::function<void(const FlightState&)>& stores,
                    const CpuWorkSink& work);

void update_friendly_aircraft(SceneEntity& entity, std::size_t slot, MissionScene& scene, FlightState& player,
                              const SceneAssets& assets, const TerrainTables& tables,
                              const std::array<std::int32_t, 2>& movement_basis,
                              std::size_t payload_count, SoundBatch& sounds, ExecutionClock* clock = nullptr);

void update_ballistic(SceneEntity& entity, MissionScene& scene, FlightState& player,
                      const SceneAssets& assets, const TerrainTables& tables,
                      std::size_t explosion_payload, SoundBatch& sounds, ExecutionClock* clock = nullptr);

void update_guided_missile(SceneEntity& entity, MissionScene& scene, FlightState& player,
                           const SceneAssets& assets, const TerrainTables& tables,
                           std::size_t explosion_payload, SoundBatch& sounds, ExecutionClock* clock = nullptr);

void update_enemy_missile(SceneEntity& entity, MissionScene& scene, FlightState& player,
                          const SceneAssets& assets, const TerrainTables& tables,
                          std::size_t explosion_payload, SoundBatch& sounds, ExecutionClock* clock = nullptr);

void update_artillery(SceneEntity& entity, MissionScene& scene, FlightState& player,
                      std::size_t explosion_payload, SoundBatch& sounds, const CpuWorkSink& work = {});

} // namespace comanchecd::flight_entities
