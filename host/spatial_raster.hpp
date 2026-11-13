#pragma once

#include "spatial_renderer.hpp"

#include <algorithm>
#include <cmath>

namespace comanchecd::host::spatial_raster {
struct Vertex { CameraVector point; double u = 0, v = 0; };
inline Vertex mix(const Vertex& a, const Vertex& b, double amount) {
    return {a.point + (b.point - a.point) * amount, a.u + (b.u - a.u) * amount, a.v + (b.v - a.v) * amount};
}
inline double edge(const CameraPixel& a, const CameraPixel& b, double x, double y) {
    return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}
inline double clip_distance(const CameraFrustum& f, CameraVector p, unsigned plane) {
    switch (plane) {
    case 0: return -p.z - f.near_clip;
    case 1: return f.far_clip + p.z;
    case 2: return p.x + p.z * f.left;
    case 3: return -p.z * f.right - p.x;
    case 4: return -p.z * f.up - p.y;
    default: return p.y + p.z * f.down;
    }
}

template<class Sample>
void draw_triangle(SpatialImage& image, const CameraProjection& projection, const Vertex& a, const Vertex& b, const Vertex& c,
                   const Sample& sample) {
    const auto pa = projection.project(a.point), pb = projection.project(b.point), pc = projection.project(c.point);
    const auto area = edge(pa, pb, pc.x, pc.y);
    if (std::abs(area) < 1e-9) { return; }
    const auto inverse_area = 1 / area;
    const auto x0 = int(std::clamp(std::floor(std::min({pa.x, pb.x, pc.x})), 0.0, double(image.width)));
    const auto x1 = int(std::clamp(std::ceil(std::max({pa.x, pb.x, pc.x})), 0.0, double(image.width)));
    const auto y0 = int(std::clamp(std::floor(std::min({pa.y, pb.y, pc.y})), 0.0, double(image.height)));
    const auto y1 = int(std::clamp(std::ceil(std::max({pa.y, pb.y, pc.y})), 0.0, double(image.height)));
    const auto ia = 1 / pa.depth, ib = 1 / pb.depth, ic = 1 / pc.depth;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const auto wa = edge(pb, pc, x + 0.5, y + 0.5) * inverse_area;
            const auto wb = edge(pc, pa, x + 0.5, y + 0.5) * inverse_area;
            const auto wc = 1 - wa - wb;
            if (wa < -1e-9 || wb < -1e-9 || wc < -1e-9) { continue; }
            const auto reciprocal = wa * ia + wb * ib + wc * ic;
            const auto depth = 1 / reciprocal;
            const auto pixel = std::size_t(y) * image.width + x;
            if (depth > image.depth[pixel] + 0.00001) { continue; }
            const auto color = sample((wa * a.u * ia + wb * b.u * ib + wc * c.u * ic) * depth,
                (wa * a.v * ia + wb * b.v * ib + wc * c.v * ic) * depth, depth);
            if (color == 0) { continue; }
            image.pixels[pixel] = color;
            image.depth[pixel] = static_cast<float>(depth);
        }
    }
}

template<class Sample>
void triangle(SpatialImage& image, const CameraProjection& projection, const CameraFrustum& frustum,
              Vertex a, Vertex b, Vertex c, const Sample& sample) {
    constexpr unsigned frustum_planes = 6, maximum_vertices = 12;
    std::array<Vertex, maximum_vertices> polygon{a, b, c}, clipped{};
    unsigned count = 3;
    for (unsigned plane = 0; plane < frustum_planes && count; ++plane) {
        unsigned output = 0;
        auto previous = polygon[count - 1];
        auto previous_distance = clip_distance(frustum, previous.point, plane);
        for (unsigned index = 0; index < count; ++index) {
            const auto current = polygon[index];
            const auto distance = clip_distance(frustum, current.point, plane);
            if ((distance >= 0) != (previous_distance >= 0)) {
                clipped[output++] = mix(previous, current, previous_distance / (previous_distance - distance));
            }
            if (distance >= 0) { clipped[output++] = current; }
            previous = current;
            previous_distance = distance;
        }
        polygon = clipped;
        count = output;
    }
    for (unsigned index = 1; index + 1 < count; ++index) {
        draw_triangle(image, projection, polygon[0], polygon[index], polygon[index + 1], sample);
    }
}
}
