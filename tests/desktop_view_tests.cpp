#include "desktop_view.hpp"
#include "spatial_renderer.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        using namespace comanchecd::host;
        const auto wide = desktop_layout(1920, 1080, ViewAspect::widescreen, ImageScaling::integer);
        require(wide.viewport.width == 1920 && wide.viewport.height == 1080 && wide.width == 1920,
                "Widescreen flight must fill the drawable independently of native integer scaling");
        const auto native = desktop_layout(1920, 1080, ViewAspect::original, ImageScaling::integer);
        require(native.viewport.width == 1280 && native.viewport.height == 960 && native.viewport.x == 320 &&
                    native.viewport.y == 60,
                "Native presentation must preserve centered integer-scaled 4:3 pixels");
        const auto portrait = desktop_layout(600, 1000, ViewAspect::widescreen, ImageScaling::nearest);
        require(portrait.width == 600 && portrait.height == 450 && portrait.viewport.y == 275,
                "Portrait windows must preserve the reference vertical view");
        const auto tiny = desktop_layout(1, 1, ViewAspect::original, ImageScaling::integer);
        require(tiny.width == 1 && tiny.height == 1, "Tiny windows require a nonempty drawable");
        const auto large = desktop_layout(7680, 2160, ViewAspect::widescreen, ImageScaling::nearest);
        require(large.width == 3200 && large.height == 900 && large.viewport.width == 7680,
                "Render limits must preserve ultrawide aspect and full physical output");
        const auto reference = desktop_camera({}, 800, 600);
        const auto expanded = desktop_camera({}, 1600, 600);
        require(std::abs(reference.frustum.up - expanded.frustum.up) < 1e-12 &&
                    std::abs(reference.frustum.down - expanded.frustum.down) < 1e-12 &&
                    std::abs(expanded.frustum.right - 2 * reference.frustum.right) < 1e-12,
                "Widescreen must extend the horizontal field of view while retaining vertical framing");
        const auto legacy = camera_view({}, 800, 600);
        require(std::abs(legacy.frustum.up - reference.frustum.up) < 1e-12 &&
                    std::abs(legacy.frustum.right - reference.frustum.right) < 1e-12,
                "Reference desktop framing must retain the existing 4:3 camera");
        comanchecd::CockpitImage artwork;
        artwork.palette[1] = {63, 0, 0};
        artwork.pixels.fill(1);
        SpatialImage image{1280, 720, std::vector<std::uint32_t>(1280 * 720, 0xff123456u), {}};
        std::vector<std::uint8_t> mask(artwork.pixels.size(), 0);
        composite_cockpit(image, artwork, mask, CockpitScaling::sharp, OverlayLayout::centered);
        require(image.pixels[360 * 1280] == 0xff123456u && image.pixels[360 * 1280 + 159] == 0xff123456u &&
                    image.pixels[360 * 1280 + 160] == 0xffff0000u &&
                    image.pixels[360 * 1280 + 1119] == 0xffff0000u &&
                    image.pixels[360 * 1280 + 1120] == 0xff123456u,
                "CPU flat instruments must occupy a centered 4:3 region over the widescreen world");
        std::cout << "Desktop layout and widescreen perspective passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
