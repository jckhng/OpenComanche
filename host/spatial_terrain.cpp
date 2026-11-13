#include "spatial_draw.hpp"
#include "spatial_units.hpp"

#include <algorithm>
#include <cmath>

namespace comanchecd::host::spatial_draw {
double surface_height(const Terrain& terrain, double x, double z) {
    const auto ix = int(std::floor(x)), iz = int(std::floor(z));
    const double fx = x - std::floor(x), fz = z - std::floor(z);
    const auto at = [&](int dx, int dz) {
        return terrain.height[std::size_t((iz + dz) & (terrain_side - 1)) * terrain_side + ((ix + dx) & (terrain_side - 1))];
    };
    return ((at(0, 0) * (1 - fx) + at(1, 0) * fx) * (1 - fz) +
        (at(0, 1) * (1 - fx) + at(1, 1) * fx) * fz) * spatial_units::terrain_height;
}

void terrain(SpatialImage& image, const SpatialFrame& frame, const CameraView& view,
             const CameraProjection& projection, const Colors& colors) {
    const auto& terrain = frame.resources->assets.terrain;
    const double center_x = std::floor(view.pose.position.x), center_z = std::floor(view.pose.position.z);
    const auto vertex = [&](double x, double z) {
        const auto world_x = center_x + x, world_z = center_z + z;
        return spatial_raster::Vertex{projection.to_camera({world_x, surface_height(terrain, world_x, world_z), world_z}), world_x, world_z};
    };
    const auto sample = [&](double x, double z, double depth) {
        const auto cell = std::size_t(int(std::floor(z)) & (terrain_side - 1)) * terrain_side + (int(std::floor(x)) & (terrain_side - 1));
        return colors.terrain(terrain.color[cell], depth);
    };
    constexpr int ring_cells = 16;
    const auto maximum_step = std::max(1, int(std::ceil(view.frustum.far_clip / ring_cells)));
    for (int step = 1; step < maximum_step * 2; step *= 2) {
        const int outer = step * ring_cells, inner = step == 1 ? 0 : outer / 2;
        for (int z = -outer; z < outer; z += step) {
            for (int x = -outer; x < outer; x += step) {
                if (inner && x >= -inner && x + step <= inner && z >= -inner && z + step <= inner) { continue; }
                const bool top = inner && z == inner && x >= -inner && x + step <= inner;
                const bool right = inner && x + step == -inner && z >= -inner && z + step <= inner;
                const bool bottom = inner && z + step == -inner && x >= -inner && x + step <= inner;
                const bool left = inner && x == inner && z >= -inner && z + step <= inner;
                const auto a = vertex(x, z), b = vertex(x + step, z), c = vertex(x + step, z + step), d = vertex(x, z + step);
                if (!(top || right || bottom || left)) {
                    spatial_raster::triangle(image, projection, view.frustum, a, b, c, sample);
                    spatial_raster::triangle(image, projection, view.frustum, a, c, d, sample);
                    continue;
                }
                // Coarse inner edges include the neighboring fine ring's vertices.
                std::array<spatial_raster::Vertex, 8> edge{};
                unsigned count = 0;
                edge[count++] = a;
                if (top) { edge[count++] = vertex(x + step * 0.5, z); }
                edge[count++] = b;
                if (right) { edge[count++] = vertex(x + step, z + step * 0.5); }
                edge[count++] = c;
                if (bottom) { edge[count++] = vertex(x + step * 0.5, z + step); }
                edge[count++] = d;
                if (left) { edge[count++] = vertex(x, z + step * 0.5); }
                const auto center = vertex(x + step * 0.5, z + step * 0.5);
                for (unsigned index = 0; index < count; ++index) {
                    spatial_raster::triangle(image, projection, view.frustum, center, edge[index], edge[(index + 1) % count], sample);
                }
            }
        }
    }
}
}
