#include "spatial_draw.hpp"
#include "spatial_units.hpp"

#include <limits>
#include <stdexcept>

namespace comanchecd::host::spatial_draw {
namespace {
constexpr double sprite_width = 0.25, sprite_height = 0.125;
constexpr unsigned descriptor_bytes = 8, mirror_flag = 128, frame_mask = 127;

std::uint32_t integer(const std::vector<std::uint8_t>& bytes, std::size_t offset, unsigned count) {
    if (offset > bytes.size() || count > bytes.size() - offset) { throw std::invalid_argument("Truncated spatial sprite"); }
    std::uint32_t result = 0;
    for (unsigned i = 0; i < count; ++i) { result |= std::uint32_t(bytes[offset + i]) << (8 * i); }
    return result;
}
}

void models(SpatialImage& image, const SpatialFrame& frame, const CameraView& view,
            const CameraProjection& projection, const EntityPoses& poses, const Colors& colors) {
    using namespace spatial_units;
    const auto& assets = frame.resources->assets;
    const auto& tables = frame.resources->tables;
    const auto forward = projection.to_world({0, 0, -1}) - view.pose.position;
    const double heading = std::atan2(forward.x, -forward.z);
    const CameraVector right{std::cos(heading), 0, std::sin(heading)};
    for (std::size_t index = 0; index < frame.scene.entities.size(); ++index) {
        const auto& entity = frame.scene.entities[index];
        const auto& pose = poses[index];
        const bool explosion = entity.phase == EntityPhase::explosion;
        if (!entity.active || (!explosion && entity.kind == artillery_kind) ||
                (!explosion && entity.kind == 0 && frame.camera.mode == SceneViewMode::cockpit)) { continue; }
        const double dx = wrap_delta(pose.x - view.pose.position.x, terrain_side);
        const double dz = wrap_delta(pose.y - view.pose.position.z, terrain_side);
        const CameraVector center{view.pose.position.x + dx, pose.height, view.pose.position.z + dz};
        const auto depth = std::max(0.25, dx * std::sin(heading) - dz * std::cos(heading));
        auto screen_y = static_cast<std::uint8_t>(std::clamp(137.0 +
            (pose.height / terrain_height - view.pose.position.y / terrain_height - 1) * 75 / depth, 0.0, 255.0));
        const bool player = entity.kind == 0 && !explosion;
        const bool animated = explosion || entity.kind == cannon_round_kind || entity.kind == enemy_cannon_kind ||
            entity.kind == flare_kind || entity.kind == chaff_kind;
        if (tables.levels > 0 && tables.levels <= terrain_distance_capacity &&
                frame.resources->model_projection.size() >= std::size_t(tables.levels) * scene_buffer_side) {
            const auto end = tables.distance[4].begin() + tables.levels;
            const auto level = std::lower_bound(tables.distance[4].begin(), end, depth * distance_table_units_per_cell) - tables.distance[4].begin();
            if (level < tables.levels) {
                auto eye_height = unsigned(std::lround(std::clamp(view.pose.position.y / entity_height, 0.0, 255.0)));
                if (!animated) { eye_height &= 0xfe; }
                const auto entity_z = unsigned(std::lround(std::clamp(pose.height / entity_height, 0.0, 255.0)));
                const auto relative = (255 - eye_height + entity_z) >> 1;
                screen_y = frame.resources->model_projection[level * scene_buffer_side + relative];
            }
        }
        const bool counter_rotor = !explosion && (entity.kind == 1 || entity.kind == 6);
        const bool layered = !explosion && (player || counter_rotor || entity.kind == 2 || entity.kind == 4 ||
            entity.kind == 5 || entity.kind == 7 || entity.kind == 8 || entity.kind == 11 || entity.kind == 15 ||
            entity.kind == 16 || entity.kind == 17);
        const auto pitched = static_cast<std::uint8_t>(screen_y + (player ? frame.camera.body_roll : 0));
        const bool secondary_first = pitched > 130;
        const unsigned layers = counter_rotor ? 3 : layered ? 2 : 1;
        for (unsigned layer = 0; layer < layers; ++layer) {
            const bool reverse_rotor = counter_rotor && layer == 1;
            const bool secondary = layered && (counter_rotor ? layer != (secondary_first ? 2u : 0u) :
                ((layer == 0) == secondary_first));
            const auto slot = secondary ? entity.secondary_model : entity.primary_model;
            const bool use_slot = secondary || entity.primary_payload == std::numeric_limits<std::size_t>::max();
            if (use_slot && slot >= assets.model_payloads.size()) { continue; }
            const auto payload = use_slot ? assets.model_payloads[slot] : entity.primary_payload;
            const auto model = std::find_if(assets.models.begin(), assets.models.end(), [&](const auto& item) { return item.payload == payload; });
            if (model == assets.models.end()) { continue; }
            auto selected = entity.animation_frame;
            if (!animated) {
                if (model->metadata.size() < 50) { continue; }
                auto ax = std::abs(dx), az = std::abs(dz);
                unsigned octant = unsigned(dz > 0) * 4 + unsigned(dx > 0) * 2;
                if (ax <= az) { ++octant; std::swap(ax, az); }
                auto angle = tables.angle_octants[octant];
                if (ax > 0) { angle ^= tables.angle_ratio[std::min<std::size_t>(std::size_t(az / ax * 1024), 1024)]; }
                angle = (angle + 1) >> 1;
                const auto entity_angle = static_cast<std::uint8_t>(std::lround(std::remainder(pose.heading, tau) / byte_angle));
                auto facing = (unsigned(angle) + entity_angle + 4) >> 3;
                facing &= 31;
                if (secondary) {
                    const auto phase = int(std::lround(std::remainder(pose.rotor_phase, tau) / byte_angle));
                    const int rotor = reverse_rotor ? -phase + 4 : phase + (secondary_first ? 4 : 0);
                    facing = (facing + (static_cast<std::uint8_t>(rotor) >> 3)) & 31;
                }
                selected = static_cast<std::uint8_t>(model->metadata[2 + facing] + model->metadata[34 + ((player ? pitched : screen_y) >> 4)]);
            }
            const auto number = selected & frame_mask;
            const auto& bytes = model->bytes;
            if (bytes.size() < 8 || number == 0 || number > bytes[2]) { continue; }
            const auto entry = std::size_t(number) * descriptor_bytes;
            const auto width = integer(bytes, entry, 1), height = integer(bytes, entry + 1, 1);
            const auto ox = integer(bytes, entry + 2, 1);
            const auto oy = static_cast<std::int8_t>(integer(bytes, entry + 3, 1) - (reverse_rotor ? 8 : 0));
            const auto source = integer(bytes, entry + 4, 4);
            if (!width || !height || source > bytes.size() || width * height > bytes.size() - source) {
                throw std::invalid_argument("Spatial sprite exceeds its allocation");
            }
            const bool mirror = (selected & mirror_flag) != 0;
            const auto left = center + right * ((mirror ? double(ox) - width : -double(ox)) * sprite_width);
            const auto bottom = left + CameraVector{0, -oy * sprite_height, 0};
            const auto vertex = [&](double u, double v) {
                return spatial_raster::Vertex{projection.to_camera(bottom + right * (u * sprite_width) + CameraVector{0, v * sprite_height, 0}), u, v};
            };
            const auto sample = [&](double u, double v, double) {
                auto x = std::clamp(int(u), 0, int(width) - 1);
                if (mirror) { x = int(width) - 1 - x; }
                const auto y = std::clamp(int(v), 0, int(height) - 1);
                const auto color = bytes[source + x * height + y];
                return color ? colors.base[color] : 0u;
            };
            const auto a = vertex(0, 0), b = vertex(width, 0), c = vertex(width, height), d = vertex(0, height);
            spatial_raster::triangle(image, projection, view.frustum, a, b, c, sample);
            spatial_raster::triangle(image, projection, view.frustum, a, c, d, sample);
        }
    }
}
}
