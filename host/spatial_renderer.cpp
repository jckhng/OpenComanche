#include "spatial_renderer.hpp"
#include "spatial_draw.hpp"
#include "spatial_units.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace comanchecd::host {
namespace {
constexpr unsigned maximum_dimension = 4096;
constexpr std::size_t maximum_pixels = 4096 * 2160;
constexpr unsigned sky_columns = 512, sky_rows = 150;
constexpr double maximum_scene_distance = 512;

std::uint32_t rgba(const Palette& palette, std::uint8_t index) {
    const auto& c = palette[index];
    return 0xff000000u | ((c[0] * 255u / 63u) << 16) | ((c[1] * 255u / 63u) << 8) | (c[2] * 255u / 63u);
}

spatial_draw::Colors colors(const SpatialFrame& frame, const Palette& palette) {
    spatial_draw::Colors result;
    result.haze = frame.resources->scenery.haze;
    const auto fog = rgba(palette, 255);
    const auto blend = [&](std::uint32_t color, unsigned weight) {
        std::uint32_t output = 0xff000000u;
        for (unsigned shift : {0u, 8u, 16u}) {
            const auto source = (color >> shift) & 255, target = (fog >> shift) & 255;
            output |= ((source * (4 - weight) + target * weight + 2) / 4) << shift;
        }
        return output;
    };
    for (unsigned index = 0; index < result.base.size(); ++index) {
        result.base[index] = rgba(palette, index);
        result.quarter[index] = blend(result.base[index], 1);
        result.half[index] = blend(result.base[index], 2);
    }
    const auto& tables = frame.resources->tables;
    const auto distance = [&](unsigned fraction) {
        return tables.levels > 0 && tables.levels <= terrain_distance_capacity ?
            tables.distance[4][std::min(tables.levels - 1, tables.levels - (tables.levels >> fraction))] / spatial_units::distance_table_units_per_cell : maximum_scene_distance;
    };
    result.quarter_distance = distance(2);
    result.half_distance = distance(3);
    return result;
}

void sky(SpatialImage& image, const SpatialFrame& frame, const CameraProjection& projection, const spatial_draw::Colors& palette) {
    const auto& models = frame.resources->assets.models;
    const auto* bytes = models.empty() ? nullptr : &models.back().bytes;
    std::size_t source = 0;
    if (bytes && bytes->size() >= 16) {
        for (unsigned i = 0; i < 4; ++i) { source |= std::size_t((*bytes)[12 + i]) << (8 * i); }
    }
    const bool panorama = bytes && source <= bytes->size() && bytes->size() - source == sky_columns * sky_rows &&
        frame.resources->scenery.clouds == CloudMode::panoramic;
    if (!panorama) { std::fill(image.pixels.begin(), image.pixels.end(), palette.base[255]); return; }
    constexpr double sky_horizon = 118, sky_focal = 200;
    const auto block = std::max(1u, std::min(image.width / unsigned(cockpit_width), image.height / unsigned(cockpit_height)));
    for (unsigned y = 0; y < image.height; y += block) {
        for (unsigned x = 0; x < image.width; x += block) {
            const auto ray = projection.ray(x + block * 0.5, y + block * 0.5);
            const auto azimuth = std::atan2(ray.x, -ray.z);
            const auto elevation = std::atan2(ray.y, std::hypot(ray.x, ray.z));
            const auto column = int(std::floor(sky_columns - 8 - azimuth * sky_columns / spatial_units::tau)) & (sky_columns - 1);
            const auto row = int(std::clamp(sky_horizon - elevation * sky_focal, 0.0, double(sky_rows - 1)));
            const auto color = palette.base[(*bytes)[source + column * sky_rows + row]];
            for (unsigned dy = y; dy < std::min(y + block, image.height); ++dy) {
                std::fill_n(image.pixels.begin() + std::size_t(dy) * image.width + x, std::min(block, image.width - x), color);
            }
        }
    }
}
}

const SpatialImage& SpatialRenderer::render_world(const SpatialFrame& frame, const CameraView& supplied, const EntityPoses& poses,
    const Palette& palette, unsigned width, unsigned height) {
    if (!frame.resources || !width || !height || width > maximum_dimension || height > maximum_dimension ||
            std::size_t(width) * height > maximum_pixels) { throw std::invalid_argument("Invalid spatial presentation dimensions or scene"); }
    const auto& terrain = frame.resources->assets.terrain;
    if (terrain.height.size() != terrain_side * terrain_side || terrain.color.size() != terrain.height.size()) {
        throw std::invalid_argument("Spatial renderer requires complete terrain maps");
    }
    auto view = supplied;
    if (!std::isfinite(view.pose.position.x) || !std::isfinite(view.pose.position.z) ||
            !std::isfinite(view.pose.position.y) || std::abs(view.pose.position.y) > std::numeric_limits<int>::max() / 4) {
        throw std::invalid_argument("Invalid spatial camera position");
    }
    view.pose.position.x = std::remainder(view.pose.position.x, double(terrain_side));
    view.pose.position.z = std::remainder(view.pose.position.z, double(terrain_side));
    const CameraProjection projection(view, width, height);
    for (std::size_t index = 0; index < poses.size(); ++index) {
        if (!frame.scene.entities[index].active) { continue; }
        const auto& pose = poses[index];
        for (const auto value : {pose.x, pose.y, pose.height, pose.heading, pose.rotor_phase}) {
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<int>::max() / 4) {
                throw std::invalid_argument("Invalid spatial entity pose");
            }
        }
    }
    image_.width = width; image_.height = height;
    image_.pixels.resize(std::size_t(width) * height);
    image_.depth.assign(image_.pixels.size(), std::numeric_limits<float>::infinity());
    const auto palette_tables = colors(frame, palette);
    if (view.pose.position.y < spatial_draw::surface_height(terrain, view.pose.position.x, view.pose.position.z)) {
        const auto cell = std::size_t(int(std::floor(view.pose.position.z)) & (terrain_side - 1)) * terrain_side +
            (int(std::floor(view.pose.position.x)) & (terrain_side - 1));
        std::fill(image_.pixels.begin(), image_.pixels.end(), palette_tables.base[terrain.color[cell]]);
        std::fill(image_.depth.begin(), image_.depth.end(), static_cast<float>(view.frustum.near_clip));
        return image_;
    }
    sky(image_, frame, projection, palette_tables);
    const auto& tables = frame.resources->tables;
    const auto scene_distance = tables.levels > 0 && tables.levels <= terrain_distance_capacity ?
        std::clamp(tables.distance[4][tables.levels - 1] / spatial_units::distance_table_units_per_cell, 16.0, maximum_scene_distance) : maximum_scene_distance;
    view.frustum.far_clip = std::min(view.frustum.far_clip, scene_distance);
    spatial_draw::terrain(image_, frame, view, projection, palette_tables);
    spatial_draw::models(image_, frame, view, projection, poses, palette_tables);
    return image_;
}

const SpatialImage& SpatialRenderer::render(const SpatialFrame& frame, const CameraView& view, const EntityPoses& poses,
    const CockpitImage& overlay, unsigned width, unsigned height, CockpitScaling scaling) {
    if (frame.scene_mask.size() != overlay.pixels.size()) { throw std::invalid_argument("Invalid cockpit scene mask"); }
    render_world(frame, view, poses, overlay.palette, width, height);
    composite_cockpit(image_, overlay, frame.scene_mask, scaling);
    return image_;
}

const SpatialImage& SpatialRenderer::render(const SpatialFrame& frame, const SpatialCamera& camera,
    const CockpitImage& overlay, unsigned width, unsigned height) {
    return render(frame, camera_view(camera, width, height), entity_poses(frame), overlay, width, height, CockpitScaling::nearest);
}
}
