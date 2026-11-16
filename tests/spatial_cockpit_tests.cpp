#include "spatial_cockpit.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace comanchecd;
using namespace comanchecd::host;
constexpr std::uint32_t background=0xff123456;
void require(bool value,const char* message) {
    if (!value) { throw std::runtime_error(message); }
}
SpatialImage clear() {
    return {640,480,std::vector<std::uint32_t>(640*480,background),{}};
}
void save(const std::filesystem::path& file,const SpatialImage& image) {
    std::ofstream output(file,std::ios::binary);
    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (auto pixel : image.pixels) {
        const char rgb[]{char(pixel>>16),char(pixel>>8),char(pixel)};
        output.write(rgb,3);
    }
    require(bool(output),"Cockpit preview must be written");
}
}
int main(int argc,char** argv) {
    try {
        CockpitLook look;
        look.move(0.6,-0.2);
        const CameraProjection moved(cockpit_view(look.pose(),{}),640,480);
        require(moved.ray(320,152).x<0,"Desktop yaw must change the view without changing the aircraft");
        look.move(0,100);
        const auto ray=CameraProjection(cockpit_view(look.pose(),{}),640,480).ray(320,152);
        require(ray.y>0.9,"Desktop pitch must clamp near vertical");
        look.reset();
        require(look.pose().orientation.w==1,"Recenter must restore the neutral view");
        bool rejected=false;
        try { look.move(std::numeric_limits<double>::infinity(),0); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected,"Nonfinite look movement must fail");

        auto resources=std::make_shared<SpatialResources>();
        resources->hud_colors[1]=true;
        resources->hud_colors[3]=true;
        SpatialFrame frame;
        frame.resources=resources;
        frame.scene_mask.assign(cockpit_width*cockpit_height,1);
        CockpitImage source;
        source.palette[1]={0,63,0}; source.palette[2]={63,0,0};
        source.palette[3]={0,0,63};
        for (unsigned y=145;y<cockpit_height;++y) {
            for (unsigned x=0;x<cockpit_width;++x) {
                source.pixels[y*cockpit_width+x]=1;
                frame.scene_mask[y*cockpit_width+x]=0;
            }
        }
        source.pixels[70*cockpit_width+160]=1;
        frame.scene_mask[70*cockpit_width+160]=0;
        source.pixels[137*cockpit_width+160]=3;
        frame.scene_mask[137*cockpit_width+160]=0;
        for (const auto x : {64u,288u}) {
            source.pixels[137*cockpit_width+x]=3;
            frame.scene_mask[137*cockpit_width+x]=0;
        }
        std::fill(source.pixels.end()-cockpit_width,source.pixels.end(),2);
        const auto retained=source;
        const auto retained_mask=frame.scene_mask;
        SpatialCockpit cockpit;
        const auto render=[&](CameraPose pose) {
            auto image=clear();
            cockpit.draw(image,cockpit_view(pose,{}),source,frame);
            require(image.depth.size()==image.pixels.size(),"Cockpit must own metre-scaled overlay depth");
            return image;
        };
        const auto front=render({});
        require(std::count(front.pixels.begin(),front.pixels.end(),0xff00ff00)>1000,"Live recovered instrument pixels must appear on the front panel");
        require(front.pixels[470*640+320]!=0xff00ff00,
            "The fitted instrument panel must leave a visible housing margin below its bottom row");
        require(front.pixels[140*640+320]==0xff00ff00,
            "Instrument fitting must preserve the world-aligned native HUD projection");
        require(front.pixels[274*640+320]==0xff0000ff,
            "The dashboard housing must leave the lower HUD visible");
        require(front.pixels[274*640+128]==0xff0000ff && front.pixels[274*640+576]==0xff0000ff,
            "The side consoles must clear the neutral-view speed and altitude HUD regions");
        require(std::count(front.pixels.begin(),front.pixels.end(),0xffff0000)>500,
            "The final native instrument row must remain visible inside the housing");
        for (const auto width : {640u, 854u, 1120u}) {
            auto fitted=SpatialImage{width,480,std::vector<std::uint32_t>(width*480,background),{}};
            auto view=camera_view({},cockpit_width,cockpit_height);
            const auto expansion=double(width)*cockpit_height/(480*cockpit_width);
            view.frustum.left*=expansion; view.frustum.right*=expansion;
            cockpit.draw(fitted,cockpit_view({},view.frustum),source,frame);
            unsigned bottom=0;
            for (unsigned y=290;y<fitted.height;++y) {
                if (std::find(fitted.pixels.begin()+y*width,fitted.pixels.begin()+(y+1)*width,
                        0xff00ff00)!=fitted.pixels.begin()+(y+1)*width) { bottom=y; }
            }
            require(bottom>=456 && bottom<466,
                "4:3, widescreen and ultrawide must retain the whole instrument panel with bottom clearance");
            require(fitted.pixels[470*width+width/2]!=background,
                "The dashboard housing must continue beneath the fitted instruments");
        }
        auto left=render({{},camera_orientation(1.5707963267948966,0,0)});
        auto right=render({{},camera_orientation(-1.5707963267948966,0,0)});
        auto rear=render({{},camera_orientation(3.141592653589793,0.2,0)});
        auto above=render({{},camera_orientation(0,1.2,0)});
        auto below=render({{},camera_orientation(0,-1.2,0)});
        for (const auto* view : {&left,&right,&rear,&above,&below}) {
            const auto count=std::count(view->pixels.begin(),view->pixels.end(),background);
            require(count<view->pixels.size()-1000,"Side, rear, overhead and floor views must contain surrounding cockpit geometry");
        }
        const auto helmet=std::count_if(rear.pixels.begin(),rear.pixels.end(),[](auto pixel) {
            return ((pixel>>16)&255)>95 && ((pixel>>8)&255)>100 && (pixel&255)>90;
        });
        require(helmet>30,"Looking rearward must reveal the raised rear crew helmet");
        const auto left_eye=render({{-0.032,0,0},{}}),right_eye=render({{0.032,0,0},{}});
        require(left_eye.pixels!=right_eye.pixels,"Physical eye separation must produce cockpit stereo parallax");
        source.pixels[180*cockpit_width+100]=2;
        require(render({}).pixels!=front.pixels,"Native instrument changes must update the spatial panel");
        frame.scene_mask[180*cockpit_width+100]=1;
        const auto cutout=render({});
        require(std::count(cutout.pixels.begin(),cutout.pixels.end(),0xffff0000)==
            std::count(front.pixels.begin(),front.pixels.end(),0xffff0000),
            "Native world pixels must reveal the housing through instrument artwork cutouts");
        frame.scene_mask=retained_mask;
        source=retained;
        frame.native_effects=true;
        require(render({}).pixels==front.pixels,"Native effects must preserve the independently rendered world instead of copying a flat scene plane");
        const auto effect_rear=render({{},camera_orientation(3.141592653589793,0.2,0)});
        require(effect_rear.pixels==rear.pixels,"Forward indexed effects must preserve rear look-around geometry");
        SessionPresentation snapshot;
        snapshot.spatial=std::make_shared<SpatialFrame>(frame); snapshot.flight.armor=30;
        require(spatial_presentation(snapshot,CockpitPresentation::spatial) &&
            !spatial_presentation(snapshot,CockpitPresentation::flat),"Healthy spatial cockpits must stay tracked through native-only effects");
        snapshot.flight.crash_rotation=1;
        require(!spatial_presentation(snapshot,CockpitPresentation::spatial),"Crash transitions must retain their indexed presentation");
        require(source.pixels==retained.pixels && frame.scene_mask==retained_mask,"Cockpit drawing must preserve canonical image and coverage observations");
        if (argc==2) {
            std::filesystem::create_directories(argv[1]);
            for (const auto& item : {std::pair<const char*,const SpatialImage*>{"front",&front},{"left",&left},{"right",&right},
                    {"rear",&rear},{"overhead",&above},{"floor",&below}}) {
                save(std::filesystem::path(argv[1])/(std::string(item.first)+".ppm"),*item.second);
            }
        }
        std::cout << "Spatial cockpit views, live instruments, stereo, rear crew and native effect boundaries passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
