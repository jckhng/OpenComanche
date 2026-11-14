#include "cockpit_model.hpp"
#include "spatial_units.hpp"
#include <algorithm>
#include <cmath>

namespace comanchecd::host::cockpit_model {
namespace {
constexpr std::uint32_t structure = 0xff30373cu, trim = 0xff171d22u;
constexpr std::uint32_t cushion = 0xff41463au, uniform = 0xff586048u, helmet = 0xffa8ad9du;
constexpr std::uint32_t visor = 0xff101c21u, strap = 0xffa39a7fu;

CameraVector cross(CameraVector a, CameraVector b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
CameraVector unit(CameraVector value) {
    return value * (1 / std::sqrt(value.x*value.x + value.y*value.y + value.z*value.z));
}
std::uint32_t shade(std::uint32_t color, double level) {
    std::uint32_t result = 0xff000000;
    for (unsigned shift : {0u, 8u, 16u}) {
        result |= std::uint32_t(std::lround(((color >> shift) & 255) * level)) << shift;
    }
    return result;
}

class Builder {
public:
    void quad(CameraVector a, CameraVector b, CameraVector c, CameraVector d, std::uint32_t color) {
        faces.push_back({{a,b,c,d},color});
    }
    void block(CameraVector center, CameraVector x, CameraVector y, CameraVector z, std::uint32_t color) {
        const std::array<CameraVector, 8> v{center-x-y-z, center+x-y-z, center+x+y-z, center-x+y-z,
            center-x-y+z, center+x-y+z, center+x+y+z, center-x+y+z};
        quad(v[0],v[1],v[2],v[3],shade(color,0.88));
        quad(v[5],v[4],v[7],v[6],shade(color,0.74));
        quad(v[4],v[0],v[3],v[7],shade(color,0.78));
        quad(v[1],v[5],v[6],v[2],shade(color,0.92));
        quad(v[3],v[2],v[6],v[7],color);
        quad(v[4],v[5],v[1],v[0],shade(color,0.62));
    }
    void box(CameraVector center, CameraVector size, std::uint32_t color) {
        block(center,{size.x/2,0,0},{0,size.y/2,0},{0,0,size.z/2},color);
    }
    void beam(CameraVector a, CameraVector b, double width, double height, std::uint32_t color) {
        const auto along = b-a;
        const auto axis = unit(along);
        const auto across = unit(cross(axis,std::abs(axis.y)>0.9 ? CameraVector{1,0,0} : CameraVector{0,1,0}));
        block((a+b)*0.5,across*(width/2),cross(axis,across)*(height/2),along*0.5,color);
    }
    void ellipsoid(CameraVector center, CameraVector radius, std::uint32_t color) {
        constexpr unsigned rings=6, segments=12;
        const auto point = [&](unsigned ring,unsigned segment) {
            const auto latitude = spatial_units::tau*(double(ring)/rings-0.5)*0.5;
            const auto longitude = spatial_units::tau*segment/segments;
            return center+CameraVector{radius.x*std::cos(latitude)*std::sin(longitude),radius.y*std::sin(latitude),
                radius.z*std::cos(latitude)*std::cos(longitude)};
        };
        for (unsigned ring=0;ring<rings;++ring) {
            for (unsigned segment=0;segment<segments;++segment) {
                const auto level=0.68+0.22*ring/rings+0.08*std::cos(spatial_units::tau*segment/segments);
                quad(point(ring,segment),point(ring,segment+1),point(ring+1,segment+1),point(ring+1,segment),shade(color,level));
            }
        }
    }
    void seat(double z, double rise) {
        box({0,-0.67+rise,z},{0.48,0.14,0.47},cushion);
        box({0,-0.37+rise,z+0.2},{0.48,0.58,0.12},cushion);
        box({0,-0.14+rise,z+0.23},{0.28,0.18,0.11},trim);
        for (const auto side : {-1.0,1.0}) {
            beam({side*0.13,-0.14+rise,z+0.125},{side*0.2,-0.56+rise,z+0.125},0.037,0.018,strap);
        }
    }
    std::vector<Face> faces;
};
}

std::vector<Face> build() {
    Builder mesh;
    // Authored proportions approximate a tandem crew station around a front-seat eye origin.
    mesh.box({0,-0.91,0.15},{1.26,0.12,3.3},trim);
    for (const auto side : {-1.0,1.0}) {
        mesh.box({side*0.63,-0.67,0.1},{0.12,0.52,3.15},structure);
        mesh.box({side*0.49,-0.5,-0.04},{0.24,0.14,0.94},trim);
        mesh.box({side*0.49,-0.26,1.2},{0.24,0.14,0.87},trim);
        mesh.beam({side*0.64,-0.36,-1.4},{side*0.64,-0.36,1.8},0.06,0.065,structure);
        const std::array<CameraVector,4> front{{{side*0.56,-0.3,-0.78},{side*0.47,0.55,-0.63},
            {side*0.44,0.62,0.43},{side*0.61,-0.32,0.55}}};
        for (unsigned index=0;index+1<front.size();++index) { mesh.beam(front[index],front[index+1],0.035,0.045,structure); }
        mesh.beam(front.back(),front.front(),0.035,0.045,structure);
        mesh.beam({side*0.61,-0.32,0.55},{side*0.45,0.78,0.94},0.04,0.055,structure);
        mesh.beam({side*0.45,0.78,0.94},{side*0.45,0.78,1.77},0.04,0.055,structure);
        mesh.beam({side*0.45,0.78,1.77},{side*0.61,-0.14,1.94},0.04,0.055,structure);
        mesh.beam({side*0.61,-0.14,1.94},{side*0.61,-0.32,0.55},0.035,0.045,structure);
        for (unsigned bank=0;bank<2;++bank) {
            const auto z=bank ? 1.22 : -0.02;
            const auto y=bank ? -0.18 : -0.42;
            for (unsigned row=0;row<3;++row) {
                for (unsigned column=0;column<4;++column) {
                    mesh.box({side*(0.44+row*0.04),y,z-0.26+column*0.14},{0.021,0.018,0.029},row==0 ? 0xffa2a89b : 0xff666b62);
                }
            }
        }
        mesh.beam({side*0.38,-0.53,-0.08},{side*0.38,-0.31,-0.16},0.035,0.04,trim);
        mesh.box({side*0.38,-0.29,-0.16},{0.055,0.09,0.06},trim);
        mesh.box({side*0.14,-0.82,-0.66},{0.17,0.08,0.25},0xff51575b);
    }
    mesh.beam({-0.47,0.55,-0.63},{0.47,0.55,-0.63},0.035,0.045,structure);
    mesh.beam({-0.44,0.62,0.43},{0.44,0.62,0.43},0.035,0.06,structure);
    mesh.beam({-0.45,0.78,0.94},{0.45,0.78,0.94},0.035,0.06,structure);
    mesh.beam({-0.45,0.78,1.77},{0.45,0.78,1.77},0.035,0.06,structure);
    mesh.beam({0,0.55,-0.63},{0,0.62,0.43},0.028,0.025,trim);
    mesh.beam({0,0.62,0.43},{0,0.78,0.94},0.028,0.025,trim);
    mesh.box({0,-0.47,-0.75},{1.18,0.46,0.16},trim);
    mesh.box({0,-0.24,-0.77},{1.22,0.055,0.24},structure);
    mesh.box({0,-0.06,0.76},{0.66,0.35,0.18},trim);
    mesh.box({0,0.67,1.93},{1.15,0.23,0.13},trim);
    mesh.box({0,-0.26,1.96},{1.22,1.6,0.12},structure);
    mesh.seat(0.05,0);
    mesh.seat(1.3,0.24);
    // A static rear crew figure supplies occupancy and scale without simulation behavior.
    mesh.ellipsoid({0,-0.14,1.3},{0.22,0.32,0.15},uniform);
    mesh.ellipsoid({0,0.27,1.3},{0.14,0.17,0.14},helmet);
    mesh.box({0,0.285,1.168},{0.22,0.085,0.035},visor);
    mesh.ellipsoid({0,0.155,1.17},{0.07,0.07,0.04},0xff555c54);
    for (const auto side : {-1.0,1.0}) {
        mesh.beam({side*0.19,-0.05,1.3},{side*0.28,-0.32,1.08},0.12,0.13,uniform);
        mesh.beam({side*0.28,-0.32,1.08},{side*0.38,-0.18,0.97},0.09,0.1,uniform);
        mesh.ellipsoid({side*0.38,-0.18,0.95},{0.055,0.045,0.075},trim);
        mesh.beam({side*0.1,-0.4,1.25},{side*0.14,-0.54,0.88},0.17,0.17,uniform);
        mesh.beam({side*0.14,-0.54,0.88},{side*0.15,-0.74,0.64},0.12,0.12,uniform);
        mesh.beam({side*0.1,0.03,1.14},{side*0.16,-0.36,1.12},0.033,0.014,strap);
    }
    return std::move(mesh.faces);
}
}
