#include "spatial_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace comanchecd::host {
namespace {
constexpr double tau = 6.2831853071795864769;
constexpr double fixed_cell = 4194304.0;
constexpr double height_scale = 0.375;
constexpr double focal_ratio = 0.625;
constexpr double horizon_ratio = 76.0 / cockpit_height;
constexpr double sprite_width = 0.25, sprite_height = 0.125;
constexpr unsigned maximum_dimension = 4096;
constexpr unsigned sky_columns = 512, sky_rows = 150;
constexpr unsigned sprite_descriptor_bytes = 8;
constexpr unsigned sprite_mirror = 128, sprite_frame_mask = 127;
constexpr double camera_reset_distance = 128;
constexpr double emulated_cycles_per_second = 20000000.0;
constexpr std::size_t maximum_pixels = 4096 * 2160;

double wrap_delta(double delta, double span) { return delta - std::floor(delta / span + 0.5) * span; }

std::uint32_t rgba(const Palette& palette, std::uint8_t index) {
    const auto& c = palette[index];
    return 0xff000000u | ((c[0] * 255u / 63u) << 16) | ((c[1] * 255u / 63u) << 8) | (c[2] * 255u / 63u);
}

std::uint32_t integer(const std::vector<std::uint8_t>& bytes, std::size_t offset, unsigned count) {
    if (offset > bytes.size() || count > bytes.size() - offset) { throw std::invalid_argument("Truncated spatial sprite"); }
    std::uint32_t result = 0;
    for (unsigned i = 0; i < count; ++i) { result |= std::uint32_t(bytes[offset + i]) << (8 * i); }
    return result;
}

struct Projection {
    double sine, cosine, focal, center, horizon, bank, eye;
    double row(double x, double z, double distance) const {
        return horizon + bank * (x - center) + (eye - z) * focal / distance;
    }
};

double terrain_height(const Terrain& terrain, double x, double y) {
    const auto ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const double fx = x - std::floor(x), fy = y - std::floor(y);
    const auto at = [&](int dx, int dy) {
        return terrain.height[std::size_t((iy + dy) & (terrain_side - 1)) * terrain_side + ((ix + dx) & (terrain_side - 1))];
    };
    return ((at(0, 0) * (1 - fx) + at(1, 0) * fx) * (1 - fy) +
            (at(0, 1) * (1 - fx) + at(1, 1) * fx) * fy) * height_scale;
}

void models(SpatialImage& image, const SpatialFrame& frame, const SpatialCamera& camera,
            const Projection& projection, const std::array<std::uint32_t, 256>& palette, double far_clip) {
    const auto& assets = frame.resources->assets;
    const auto& tables = frame.resources->tables;
    for (const auto& entity : frame.scene.entities) {
        const bool explosion = entity.phase == EntityPhase::explosion;
        if (!entity.active || (!explosion && entity.kind == artillery_kind) ||
                (!explosion && entity.kind == 0 && camera.mode == SceneViewMode::cockpit)) { continue; }
        const double dx = wrap_delta(entity.x / fixed_cell - camera.x, terrain_side);
        const double dy = wrap_delta(entity.y / fixed_cell - camera.y, terrain_side);
        const double depth = dx * projection.sine - dy * projection.cosine;
        if (depth < 0.25 || depth > far_clip) { continue; }
        const double lateral = dx * projection.cosine + dy * projection.sine;
        const double center = projection.center + lateral * projection.focal / depth;
        const double z = entity.height * 0.5 * height_scale;
        auto screen_y = static_cast<std::uint8_t>(std::clamp(137.0 +
            (entity.height * 0.5 - camera.height / height_scale - 1) * 75.0 / depth, 0.0, 255.0));
        const bool player = entity.kind == 0 && !explosion;
        const bool animated = explosion || entity.kind == cannon_round_kind || entity.kind == enemy_cannon_kind ||
                              entity.kind == flare_kind || entity.kind == chaff_kind;
        if (!frame.resources->model_projection.empty() && tables.levels <= terrain_distance_capacity) {
            const auto multiply = [](std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>((std::int64_t(a) * b) >> 32); };
            const auto x = static_cast<std::int32_t>(entity.x - frame.camera.x) >> 1;
            const auto y = static_cast<std::int32_t>(entity.y - frame.camera.y) >> 1;
            const auto native_depth = static_cast<std::int32_t>(std::uint32_t(multiply(frame.native_camera.sine, x)) -
                std::uint32_t(multiply(frame.native_camera.cosine, y)));
            const auto end = tables.distance[4].begin() + tables.levels;
            const auto level = std::lower_bound(tables.distance[4].begin(), end, std::uint32_t(std::max(0, native_depth)),
                [](auto value, auto distance) { return (value >> 3) < distance; }) - tables.distance[4].begin();
            if (level < tables.levels) {
                const auto eye_height = animated ? frame.camera.height : frame.camera.height & 0xfe;
                const auto relative_height = (unsigned(static_cast<std::uint8_t>(~eye_height)) + entity.height) >> 1;
                screen_y = frame.resources->model_projection.at(level * scene_buffer_side + relative_height);
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
            const auto model = std::find_if(assets.models.begin(), assets.models.end(),
                [&](const auto& item) { return item.payload == payload; });
            if (model == assets.models.end()) { continue; }
            auto selected = entity.animation_frame;
            if (!animated) {
                if (model->metadata.size() < 50) { continue; }
                const auto difference_x = static_cast<std::int32_t>(frame.camera.x - entity.x);
                const auto difference_y = static_cast<std::int32_t>(frame.camera.y - entity.y);
                auto ax = std::uint32_t(difference_x ^ (difference_x >> 31));
                auto ay = std::uint32_t(difference_y ^ (difference_y >> 31));
                unsigned octant = unsigned(difference_y < 0) * 4 + unsigned(difference_x < 0) * 2;
                if (ax <= ay) { ++octant; std::swap(ax, ay); }
                auto angle = tables.angle_octants[octant];
                if ((ax >> 10) != 0) { angle ^= tables.angle_ratio[std::min<std::size_t>(ay / (ax >> 10), 1024)]; }
                angle = (angle + 1) >> 1;
                auto facing = static_cast<std::uint8_t>(angle + entity.angle + 4) >> 3;
                if (secondary) {
                    const int rotor = reverse_rotor ? -int(entity.rotor_phase) + 4 : entity.rotor_phase + (secondary_first ? 4 : 0);
                    facing = (facing + (static_cast<std::uint8_t>(rotor) >> 3)) & 31;
                }
                selected = static_cast<std::uint8_t>(model->metadata[2 + facing] + model->metadata[34 + ((player ? pitched : screen_y) >> 4)]);
            }
            const auto number = selected & sprite_frame_mask;
            const auto& bytes = model->bytes;
            if (bytes.size() < 8 || number == 0 || number > bytes[2]) { continue; }
            const auto entry = std::size_t(number) * sprite_descriptor_bytes;
            const auto width = integer(bytes, entry, 1), height = integer(bytes, entry + 1, 1);
            const auto ox = integer(bytes, entry + 2, 1);
            const auto oy = static_cast<std::int8_t>(integer(bytes, entry + 3, 1) - (reverse_rotor ? 8 : 0));
            const auto source = integer(bytes, entry + 4, 4);
            if (!width || !height || source > bytes.size() || width * height > bytes.size() - source) {
                throw std::invalid_argument("Spatial sprite exceeds its allocation");
            }
            const bool mirror = (selected & sprite_mirror) != 0;
            const double left = center + (mirror ? double(ox) - width : -double(ox)) * sprite_width * projection.focal / depth;
            const double right = left + width * sprite_width * projection.focal / depth;
            const double bottom_z = z - oy * sprite_height;
            const double top_z = bottom_z + height * sprite_height;
            const int x0 = int(std::clamp(std::floor(left), 0.0, double(image.width)));
            const int x1 = int(std::clamp(std::ceil(right), 0.0, double(image.width)));
            for (int x = x0; x < x1; ++x) {
                const double top = projection.row(x + 0.5, top_z, depth);
                const double bottom = projection.row(x + 0.5, bottom_z, depth);
                const int y0 = int(std::clamp(std::floor(top), 0.0, double(image.height)));
                const int y1 = int(std::clamp(std::ceil(bottom), 0.0, double(image.height)));
                auto sx = std::clamp(int((x + 0.5 - left) * width / (right - left)), 0, int(width) - 1);
                if (mirror) { sx = int(width) - 1 - sx; }
                for (int y = y0; y < y1; ++y) {
                    const auto sy = std::clamp(int((bottom - y - 0.5) * height / (bottom - top)), 0, int(height) - 1);
                    const auto color = bytes[source + sx * height + sy];
                    const auto pixel = std::size_t(y) * image.width + x;
                    if (color != 0 && depth <= image.depth[pixel] + 0.001) {
                        image.pixels[pixel] = palette[color];
                        image.depth[pixel] = static_cast<float>(depth);
                    }
                }
            }
        }
    }
}
}

SpatialCamera spatial_camera(const SceneView& view) {
    const auto roll_limit = std::max<std::int64_t>(0, 33 - std::abs(std::int64_t(view.pitch)) / 4);
    const auto roll = std::clamp<std::int64_t>(view.roll, -roll_limit, roll_limit);
    return {view.x / fixed_cell, view.y / fixed_cell, view.height * 0.5 * height_scale, view.heading * tau / 4294967296.0,
            std::atan(-view.pitch / 512.0), std::atan(roll / (cockpit_width * focal_ratio)),
            double(view.vertical_pan) / cockpit_height, view.mode};
}

void CameraHistory::reset() { previous_.reset(); current_.reset(); cycles_ = 0; duration_ = 0; }

void CameraHistory::observe(std::shared_ptr<const SpatialFrame> frame, std::uint64_t cycles, double time) {
    if (!frame) { reset(); return; }
    if (frame == current_) { return; }
    previous_ = current_;
    if (!current_ || current_->resources != frame->resources || current_->camera.mode != frame->camera.mode ||
            current_->cockpit_mode != frame->cockpit_mode ||
            std::abs(wrap_delta(double(std::int32_t(frame->camera.x - current_->camera.x)) / fixed_cell, terrain_side)) > camera_reset_distance ||
            std::abs(wrap_delta(double(std::int32_t(frame->camera.y - current_->camera.y)) / fixed_cell, terrain_side)) > camera_reset_distance || frame->serial != current_->serial + 1 || cycles <= cycles_) {
        previous_ = frame;
    }
    current_ = std::move(frame);
    duration_ = previous_ == current_ ? 0 : std::clamp(double(cycles - cycles_) / emulated_cycles_per_second, 0.001, 0.25);
    cycles_ = cycles;
    arrival_ = time;
}

SpatialCamera CameraHistory::sample(double time) const {
    if (!current_) { throw std::logic_error("Camera history has no scene"); }
    auto result = spatial_camera(current_->camera);
    const auto from = spatial_camera(previous_->camera);
    const double alpha = duration_ == 0 ? 1 : std::clamp((time - arrival_) / duration_, 0.0, 1.0);
    result.x = from.x + wrap_delta(result.x - from.x, terrain_side) * alpha;
    result.y = from.y + wrap_delta(result.y - from.y, terrain_side) * alpha;
    result.heading = from.heading + wrap_delta(result.heading - from.heading, tau) * alpha;
    result.height = from.height + (result.height - from.height) * alpha;
    result.bank = from.bank + (result.bank - from.bank) * alpha;
    result.pitch = from.pitch + (result.pitch - from.pitch) * alpha;
    result.shake = from.shake + (result.shake - from.shake) * alpha;
    return result;
}

const SpatialImage& SpatialRenderer::render(const SpatialFrame& frame, const SpatialCamera& camera,
    const CockpitImage& overlay, unsigned width, unsigned height) {
    if (!frame.resources || !width || !height || width > maximum_dimension || height > maximum_dimension ||
            std::size_t(width) * height > maximum_pixels || frame.scene_mask.size() != cockpit_width * cockpit_height) {
        throw std::invalid_argument("Invalid spatial presentation dimensions or scene");
    }
    for (const auto value : {camera.x, camera.y, camera.height, camera.heading, camera.bank, camera.pitch, camera.shake}) {
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<int>::max() / 4) {
            throw std::invalid_argument("Invalid spatial camera coordinate");
        }
    }
    const auto& terrain = frame.resources->assets.terrain;
    if (terrain.height.size() != terrain_side * terrain_side || terrain.color.size() != terrain.height.size()) {
        throw std::invalid_argument("Spatial renderer requires complete terrain maps");
    }
    image_.width = width; image_.height = height;
    image_.pixels.resize(std::size_t(width) * height);
    image_.depth.assign(image_.pixels.size(), std::numeric_limits<float>::infinity());
    std::array<std::uint32_t, 256> palette{};
    for (unsigned i = 0; i < palette.size(); ++i) { palette[i] = rgba(overlay.palette, i); }
    const Projection projection{std::sin(camera.heading), std::cos(camera.heading), width * focal_ratio, width * 0.5,
        height * (horizon_ratio + camera.shake) - std::tan(camera.pitch) * width * focal_ratio,
        std::tan(camera.bank), camera.height};
    const auto& assets = frame.resources->assets;
    const auto* sky = assets.models.empty() ? nullptr : &assets.models.back().bytes;
    const auto sky_source = sky && sky->size() >= 16 ? integer(*sky, 12, 4) : 0;
    const bool panorama = sky && sky_source <= sky->size() && sky->size() - sky_source == sky_columns * sky_rows;
    const auto& tables = frame.resources->tables;
    const auto& scenery = frame.resources->scenery;
    const auto fog_color = palette[255];
    const auto fogged = [&](std::uint32_t color, unsigned weight) {
        std::uint32_t result = 0xff000000u;
        for (unsigned shift : {0u, 8u, 16u}) {
            const auto source = (color >> shift) & 255, fog = (fog_color >> shift) & 255;
            result |= ((source * (4 - weight) + fog * weight + 2) / 4) << shift;
        }
        return result;
    };
    std::array<std::uint32_t, 256> quarter{}, half{};
    for (unsigned i = 0; i < palette.size(); ++i) { quarter[i] = fogged(palette[i], 1); half[i] = fogged(palette[i], 2); }
    const auto fog_distance = [&](unsigned fraction) {
        return tables.levels > 0 && tables.levels <= terrain_distance_capacity ?
            tables.distance[4][std::min(tables.levels - 1, tables.levels - (tables.levels >> fraction))] / 8388608.0 : 512.0;
    };
    const double quarter_distance = fog_distance(2), half_distance = fog_distance(3);
    const double far_clip = tables.levels > 0 && tables.levels <= terrain_distance_capacity ?
        std::clamp(tables.distance[4][tables.levels - 1] / 8388608.0, 16.0, 512.0) : 512.0;
    for (unsigned x = 0; x < width; ++x) {
        const double horizon = projection.horizon + projection.bank * (x + 0.5 - projection.center);
        const int sky_column = int(std::floor(double(sky_columns - 8) - camera.heading * sky_columns / tau - (double(x) / width - 0.5) * 160)) & 511;
        for (unsigned y = 0; y < height; ++y) {
            const int row = int(std::clamp(42 + (y + 0.5 - (horizon - height * horizon_ratio)) * cockpit_height / height, 0.0, 149.0));
            image_.pixels[std::size_t(y) * width + x] = palette[panorama && scenery.clouds == CloudMode::panoramic ? (*sky)[sky_source + sky_column * sky_rows + row] : 255];
        }
        int visible = height;
        const double lateral = (x + 0.5 - projection.center) / projection.focal;
        const double ray_x = projection.sine + lateral * projection.cosine;
        const double ray_y = -projection.cosine + lateral * projection.sine;
        for (double distance = 0.75; distance <= far_clip && visible > 0; distance += 0.35 + distance * 0.006) {
            const double world_x = camera.x + ray_x * distance, world_y = camera.y + ray_y * distance;
            const auto z = terrain_height(terrain, world_x, world_y);
            const int top = int(std::clamp(std::ceil(projection.row(x + 0.5, z, distance)), 0.0, double(height)));
            if (top >= visible) { continue; }
            const auto cell = std::size_t(int(std::floor(world_y)) & (terrain_side - 1)) * terrain_side +
                              (int(std::floor(world_x)) & (terrain_side - 1));
            const auto color_index = terrain.color[cell];
            const auto color = !scenery.haze || distance < quarter_distance ? palette[color_index] :
                distance < half_distance ? quarter[color_index] : half[color_index];
            for (int y = top; y < visible; ++y) {
                const auto pixel = std::size_t(y) * width + x;
                image_.pixels[pixel] = color;
                image_.depth[pixel] = static_cast<float>(distance);
            }
            visible = top;
        }
    }
    models(image_, frame, camera, projection, palette, far_clip);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const auto source = (std::size_t(y) * cockpit_height / height) * cockpit_width + std::size_t(x) * cockpit_width / width;
            if (frame.scene_mask[source] == 0) { image_.pixels[std::size_t(y) * width + x] = palette[overlay.pixels[source]]; }
        }
    }
    return image_;
}
}
