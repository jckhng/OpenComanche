#pragma once

#include "spatial_renderer.hpp"

#include <algorithm>
#include <cmath>

namespace comanchecd::host::spatial_raster {
using Vertex = GeometryVertex;
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
    const auto step_a = -(pc.y - pb.y) * inverse_area;
    const auto step_b = -(pa.y - pc.y) * inverse_area;
    for (int y = y0; y < y1; ++y) {
        auto wa = edge(pb, pc, x0 + 0.5, y + 0.5) * inverse_area;
        auto wb = edge(pc, pa, x0 + 0.5, y + 0.5) * inverse_area;
        for (int x = x0; x < x1; ++x) {
            const auto wc = 1 - wa - wb;
            const auto weight_a = wa, weight_b = wb;
            wa += step_a;
            wb += step_b;
            if (weight_a < -1e-9 || weight_b < -1e-9 || wc < -1e-9) { continue; }
            const auto reciprocal = weight_a * ia + weight_b * ib + wc * ic;
            const auto depth = 1 / reciprocal;
            const auto pixel = std::size_t(y) * image.width + x;
            if (depth > image.depth[pixel] + 0.00001) { continue; }
            const auto color = sample((weight_a * a.u * ia + weight_b * b.u * ib + wc * c.u * ic) * depth,
                (weight_a * a.v * ia + weight_b * b.v * ib + wc * c.v * ic) * depth, depth);
            if (color == 0) { continue; }
            image.pixels[pixel] = color;
            image.depth[pixel] = static_cast<float>(depth);
        }
    }
}

template<class Sample>
void triangle(SpatialImage& image, const CameraProjection& projection, const CameraFrustum& frustum,
              Vertex a, Vertex b, Vertex c, const Sample& sample) {
    if (image.geometry) { image.geometry->triangle(frustum, a, b, c); return; }
    constexpr unsigned frustum_planes = 6, maximum_vertices = 12;
    unsigned crossings = 0;
    for (unsigned plane = 0; plane < frustum_planes; ++plane) {
        const bool outside_a = clip_distance(frustum, a.point, plane) < 0;
        const bool outside_b = clip_distance(frustum, b.point, plane) < 0;
        const bool outside_c = clip_distance(frustum, c.point, plane) < 0;
        if (outside_a && outside_b && outside_c) { return; }
        if (outside_a || outside_b || outside_c) { crossings |= 1u << plane; }
    }
    if (crossings == 0) {
        draw_triangle(image, projection, a, b, c, sample);
        return;
    }
    std::array<Vertex, maximum_vertices> polygon{a, b, c}, clipped{};
    unsigned count = 3;
    for (unsigned plane = 0; plane < frustum_planes && count; ++plane) {
        if ((crossings & (1u << plane)) == 0) { continue; }
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
