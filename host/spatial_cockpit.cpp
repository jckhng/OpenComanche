#include "spatial_cockpit.hpp"
#include "cockpit_model.hpp"
#include "spatial_raster.hpp"
#include "spatial_units.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace comanchecd::host {
namespace {
constexpr unsigned instrument_top = 145;
constexpr double desktop_pitch_limit = 1.45;
constexpr double hud_distance = 24;
constexpr double native_focal = cockpit_width * spatial_units::focal_ratio;
constexpr double native_horizon = cockpit_height * spatial_units::horizon_ratio;

std::array<std::uint32_t, 256> colors(const Palette& source) {
    std::array<std::uint32_t, 256> result{};
    for (unsigned index=0;index<result.size();++index) {
        const auto& c=source[index];
        result[index]=0xff000000u | ((c[0]*255u/63u)<<16) | ((c[1]*255u/63u)<<8) | (c[2]*255u/63u);
    }
    return result;
}

template<class Sample>
void quad(SpatialImage& image, const CameraView& eye, const CameraProjection& projection,
          const std::array<CameraVector,4>& positions, double u0, double v0, double u1, double v1, const Sample& sample) {
    const spatial_raster::Vertex a{projection.to_camera(positions[0]),u0,v0};
    const spatial_raster::Vertex b{projection.to_camera(positions[1]),u1,v0};
    const spatial_raster::Vertex c{projection.to_camera(positions[2]),u1,v1};
    const spatial_raster::Vertex d{projection.to_camera(positions[3]),u0,v1};
    spatial_raster::triangle(image,projection,eye.frustum,a,b,c,sample);
    spatial_raster::triangle(image,projection,eye.frustum,a,c,d,sample);
}

std::array<CameraVector,4> native_plane(double distance, unsigned top, unsigned bottom) {
    const double x=distance*cockpit_width/(2*native_focal);
    const double y0=distance*(native_horizon-top)/native_focal;
    const double y1=distance*(native_horizon-bottom)/native_focal;
    return {{{-x,y0,-distance},{x,y0,-distance},{x,y1,-distance},{-x,y1,-distance}}};
}
}

void CockpitLook::move(double yaw, double pitch) {
    if (!std::isfinite(yaw) || !std::isfinite(pitch)) { throw std::invalid_argument("Look angles must be finite"); }
    yaw_=std::remainder(yaw_+std::remainder(yaw,spatial_units::tau),spatial_units::tau);
    pitch_=std::clamp(pitch_+std::clamp(pitch,-spatial_units::tau,spatial_units::tau),-desktop_pitch_limit,desktop_pitch_limit);
}
void CockpitLook::reset() { yaw_=pitch_=0; }
CameraPose CockpitLook::pose() const { return {{},camera_orientation(yaw_,pitch_,0)}; }

bool spatial_presentation(const SessionPresentation& frame, CockpitPresentation cockpit) {
    if (!frame.spatial || (frame.spatial->cockpit_mode!=1 && frame.spatial->cockpit_mode!=6 && frame.spatial->cockpit_mode!=7)) {
        return false;
    }
    if (!frame.spatial->native_effects) { return true; }
    if (frame.spatial->effects.enabled) {
        return frame.flight.armor > 0 && !frame.flight.ended && !frame.flight.exit_requested && !frame.flight.crash_rotation;
    }
    return cockpit==CockpitPresentation::spatial && frame.spatial->cockpit_mode==1 &&
        frame.spatial->camera.mode==SceneViewMode::cockpit && frame.flight.armor>0 &&
        !frame.flight.ended && !frame.flight.exit_requested && !frame.flight.crash_rotation;
}

CameraView cockpit_view(const CameraPose& pose, CameraFrustum frustum) {
    frustum.near_clip=0.025;
    frustum.far_clip=64;
    return {pose,frustum};
}

class SpatialCockpit::Impl {
public:
    void draw(SpatialImage& image, const CameraView& eye, const CockpitImage& source, const SpatialFrame& frame) const {
        if (!image.width || !image.height || (!image.geometry && image.pixels.size()!=std::size_t(image.width)*image.height) ||
                frame.scene_mask.size()!=source.pixels.size() || !frame.resources) {
            throw std::invalid_argument("Spatial cockpit requires a complete image, coverage mask and scene resources");
        }
        const CameraProjection projection(eye,image.width,image.height);
        const auto palette=colors(source.palette);
        // Cockpit depth is measured in metres and composites over the completed world.
        if (!image.geometry) { image.depth.assign(image.pixels.size(),std::numeric_limits<float>::infinity()); }
        const auto index=[&](double u,double v) {
            return std::size_t(std::clamp(int(v),0,int(cockpit_height)-1))*cockpit_width+
                std::clamp(int(u),0,int(cockpit_width)-1);
        };
        if (image.geometry) {
            GeometryMaterial material; material.kind=GeometryKind::hud;
            image.geometry->material(material);
        }
        quad(image,eye,projection,native_plane(hud_distance,0,instrument_top),0,0,cockpit_width,instrument_top,
            [&](double u,double v,double) {
                const auto pixel=index(u,v);
                const auto color=source.pixels[pixel];
                return !frame.scene_mask[pixel] && frame.resources->hud_colors[color] ? palette[color] : 0u;
            });
        for (const auto& face : faces_) {
            if (image.geometry) {
                GeometryMaterial material; material.kind=GeometryKind::solid; material.color=face.color;
                image.geometry->material(material);
            }
            quad(image,eye,projection,face.vertices,0,0,1,1,[&](double,double,double) { return face.color; });
        }
        if (image.geometry) {
            GeometryMaterial material; material.kind=GeometryKind::instruments;
            image.geometry->material(material);
        }
        quad(image,eye,projection,native_plane(0.64,instrument_top,cockpit_height),0,instrument_top,cockpit_width,cockpit_height,
            [&](double u,double v,double) { return palette[source.pixels[index(u,v)]]; });
    }
private:
    std::vector<cockpit_model::Face> faces_=cockpit_model::build();
};

SpatialCockpit::SpatialCockpit() : impl_(std::make_unique<Impl>()) {}
SpatialCockpit::~SpatialCockpit()=default;
void SpatialCockpit::draw(SpatialImage& image, const CameraView& eye, const CockpitImage& instruments, const SpatialFrame& frame) {
    impl_->draw(image,eye,instruments,frame);
}
}
