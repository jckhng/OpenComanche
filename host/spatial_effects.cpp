#include "spatial_draw.hpp"
#include "spatial_units.hpp"

#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace comanchecd::host::spatial_draw {
namespace {
constexpr unsigned sky_columns = 512, sky_rows = 150;
constexpr double sky_horizon = 118, sky_focal = 200;
constexpr unsigned weather_sectors = 4;
constexpr double particle_center_x = 128, particle_center_y = 137;
constexpr double particle_focal = 100, particle_height_focal = 75;
constexpr double particle_pixel_size = 0.01;

unsigned nearest_color(std::uint32_t color, const Colors& colors) {
    unsigned best = 0, distance = std::numeric_limits<unsigned>::max();
    for (unsigned index = 0; index < colors.base.size(); ++index) {
        unsigned error = 0;
        for (unsigned shift : {0u, 8u, 16u}) {
            const auto delta = int((color >> shift) & 255) - int((colors.base[index] >> shift) & 255);
            error += unsigned(delta * delta);
        }
        if (error < distance) { best = index; distance = error; }
    }
    return best;
}
}

std::uint8_t Colors::sky_index(CameraVector direction) const {
    if (!panorama) { return 255; }
    const auto azimuth = std::atan2(direction.x, -direction.z);
    const auto elevation = std::atan2(direction.y, std::hypot(direction.x, direction.z));
    const auto column = int(std::floor(sky_columns - 8 - azimuth * sky_columns / spatial_units::tau)) & (sky_columns - 1);
    const auto row = int(std::clamp(sky_horizon - elevation * sky_focal, 0.0, double(sky_rows - 1)));
    return (*panorama)[panorama_offset + column * sky_rows + row];
}

void reflections(SpatialImage& image, SpatialImage& reflected, const SpatialFrame& frame, const CameraView& view,
                 const CameraProjection& projection, const EntityPoses& poses, const Colors& colors) {
    if (!frame.effects.reflections) { return; }
    constexpr unsigned first_water_color = 251, water_colors = 4, no_surface = 256;
    constexpr unsigned reflection_downsample = 2;
    constexpr double ripple_cells_per_map_cell = 32;
    constexpr unsigned noise_x_factor = 1664525u, noise_z_factor = 1013904223u;
    if (frame.effects.reflection_colors.size() != water_colors) {
        throw std::invalid_argument("Incomplete observed water color tables");
    }
    const auto& surface = frame.resources->assets.terrain;
    std::vector<unsigned> surfaces(image.pixels.size(), no_surface);
    std::vector<std::uint8_t> water(image.pixels.size());
    std::array<bool, no_surface> visible{};
    for (unsigned y = 0; y < image.height; ++y) {
        for (unsigned x = 0; x < image.width; ++x) {
            const auto pixel = std::size_t(y) * image.width + x;
            if (!std::isfinite(image.depth[pixel])) { continue; }
            const auto point = view.pose.position + projection.ray(x + 0.5, y + 0.5) * image.depth[pixel];
            const auto cell = std::size_t(int(std::floor(point.z)) & (terrain_side - 1)) * terrain_side +
                (int(std::floor(point.x)) & (terrain_side - 1));
            const auto color = surface.color[cell];
            if (color < first_water_color || color >= first_water_color + water_colors) { continue; }
            const auto height = surface.height[cell];
            surfaces[pixel] = height;
            const auto ripple_x = static_cast<unsigned>(int(std::floor(point.x * ripple_cells_per_map_cell)));
            const auto ripple_z = static_cast<unsigned>(int(std::floor(point.z * ripple_cells_per_map_cell)));
            const auto phase = frame.effects.reflection_phase ^ (ripple_x * noise_x_factor + ripple_z * noise_z_factor);
            water[pixel] = static_cast<std::uint8_t>((color - first_water_color) | ((phase & 3) << 2));
            visible[height] = true;
        }
    }
    std::unordered_map<std::uint32_t, std::array<std::uint32_t, water_colors>> remap;
    reflected.width = std::max(1u, image.width / reflection_downsample);
    reflected.height = std::max(1u, image.height / reflection_downsample);
    reflected.pixels.resize(std::size_t(reflected.width) * reflected.height);
    reflected.depth.resize(reflected.pixels.size());
    for (unsigned height = 0; height < visible.size(); ++height) {
        if (!visible[height]) { continue; }
        const auto plane = height * spatial_units::terrain_height;
        auto mirror = view;
        mirror.pose.position.y = 2 * plane - mirror.pose.position.y;
        auto& q = mirror.pose.orientation;
        q.x = -q.x;
        q.z = -q.z;
        mirror.frustum.up = -view.frustum.down;
        mirror.frustum.down = -view.frustum.up;
        const CameraProjection reflected_projection(mirror, reflected.width, reflected.height);
        std::fill(reflected.depth.begin(), reflected.depth.end(), std::numeric_limits<float>::infinity());
        for (unsigned y = 0; y < reflected.height; ++y) {
            for (unsigned x = 0; x < reflected.width; ++x) {
                reflected.pixels[std::size_t(y) * reflected.width + x] =
                    colors.base[colors.sky_index(reflected_projection.ray(x + 0.5, y + 0.5))];
            }
        }
        // Clip the submerged half-space while retaining terrain and model occlusion.
        terrain(reflected, frame, mirror, reflected_projection, colors, plane);
        models(reflected, frame, mirror, reflected_projection, poses, colors, plane);
        for (unsigned y = 0; y < image.height; ++y) {
            for (unsigned x = 0; x < image.width; ++x) {
                const auto pixel = std::size_t(y) * image.width + x;
                if (surfaces[pixel] != height) { continue; }
                const auto sample_x = std::min(reflected.width - 1, x * reflected.width / image.width);
                const auto sample_y = std::clamp(int(reflected.height - 1 - y * reflected.height / image.height) +
                    int(water[pixel] >> 2) - 1, 0, int(reflected.height) - 1);
                const auto sample = reflected.pixels[std::size_t(sample_y) * reflected.width + sample_x];
                auto found = remap.find(sample);
                if (found == remap.end()) {
                    const auto index = nearest_color(sample, colors);
                    std::array<std::uint32_t, water_colors> tinted{};
                    for (unsigned kind = 0; kind < water_colors; ++kind) {
                        tinted[kind] = colors.base[frame.effects.reflection_colors[kind][index]];
                    }
                    found = remap.emplace(sample, tinted).first;
                }
                image.pixels[pixel] = found->second[water[pixel] & 3];
            }
        }
    }
}

void weather(SpatialImage& image, const SpatialFrame& frame, const CameraView& view,
             const CameraProjection& projection, const Colors& colors, WeatherLayer layer) {
    const auto& effects = frame.effects;
    if (effects.particles.empty()) { return; }
    const auto& tables = frame.resources->tables;
    const auto anchor = camera_view(spatial_camera(frame.camera), cockpit_width, cockpit_height);
    const CameraProjection body(anchor, cockpit_width, cockpit_height);
    const auto right = projection.to_world({1, 0, 0}) - view.pose.position;
    const auto up = projection.to_world({0, 1, 0}) - view.pose.position;
    for (const auto& particle : effects.particles) {
        const bool foreground = particle.group == 2;
        if (foreground != (layer == WeatherLayer::foreground)) { continue; }
        if (particle.depth_level < 0 || unsigned(particle.depth_level) >= tables.levels ||
                tables.levels > terrain_distance_capacity || particle.group > 2 || particle.type >= effects.tint.size()) {
            throw std::invalid_argument("Invalid observed weather particle");
        }
        const auto distance = tables.distance[4][particle.depth_level] / spatial_units::distance_table_units_per_cell;
        if (distance <= view.frustum.near_clip) { continue; }
        const auto x = (particle.x - particle_center_x) * distance / particle_focal;
        const auto height = (particle.y - particle_center_y) * distance * spatial_units::terrain_height / particle_height_focal;
        const auto half_width = right * (distance * particle_pixel_size);
        const auto half_height = up * (distance * particle_pixel_size * (foreground ? 1 : 0.5));
        // Extend the recovered forward weather field around the aircraft without extra RNG calls.
        for (unsigned sector = 0; sector < weather_sectors; ++sector) {
            const auto angle = sector * spatial_units::tau / weather_sectors;
            const auto center = body.to_world({x * std::cos(angle) - distance * std::sin(angle), height,
                                               -x * std::sin(angle) - distance * std::cos(angle)});
            const auto local_center = projection.to_camera(center);
            if (-local_center.z <= view.frustum.near_clip) { continue; }
            const auto center_pixel = projection.project(local_center);
            const auto ix = std::clamp(int(center_pixel.x), 0, int(image.width) - 1);
            const auto iy = std::clamp(int(center_pixel.y), 0, int(image.height) - 1);
            const auto original = image.pixels[std::size_t(iy) * image.width + ix];
            const auto color = particle.type == 0 && particle.group != 0 ? particle.solid_color :
                effects.tint[particle.type][nearest_color(original, colors)];
            const auto vertex = [&](CameraVector position) {
                const auto local = projection.to_camera(position);
                return spatial_raster::Vertex{local, 0, 0};
            };
            const auto a = vertex(center - half_width - half_height), b = vertex(center + half_width - half_height);
            const auto c = vertex(center + half_width + half_height), d = vertex(center - half_width + half_height);
            const auto sample = [&](double, double, double depth) {
                return colors.terrain(color, depth);
            };
            spatial_raster::triangle(image, projection, view.frustum, a, b, c, sample);
            spatial_raster::triangle(image, projection, view.frustum, a, c, d, sample);
        }
    }
}
}
