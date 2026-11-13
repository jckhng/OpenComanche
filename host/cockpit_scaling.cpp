#include "cockpit_scaling.hpp"
#include "spatial_renderer.hpp"

#include <algorithm>
#include <stdexcept>

namespace comanchecd::host {
void composite_cockpit(SpatialImage& image, const CockpitImage& overlay,
                       const std::vector<std::uint8_t>& mask, CockpitScaling scaling) {
    if (!image.width || !image.height || image.pixels.size() != std::size_t(image.width) * image.height ||
            mask.size() != overlay.pixels.size() || (scaling != CockpitScaling::nearest && scaling != CockpitScaling::sharp)) {
        throw std::invalid_argument("Invalid cockpit composition dimensions or scaling");
    }
    std::array<std::uint32_t, 256> palette{};
    for (std::size_t index = 0; index < palette.size(); ++index) {
        const auto& c = overlay.palette[index];
        palette[index] = 0xff000000u | ((c[0] * 255u / 63u) << 16) | ((c[1] * 255u / 63u) << 8) | (c[2] * 255u / 63u);
    }
    const bool sharp = scaling == CockpitScaling::sharp && image.width >= 2 * cockpit_width && image.height >= 2 * cockpit_height;
    const unsigned source_width = cockpit_width * (sharp ? 2 : 1);
    const unsigned source_height = cockpit_height * (sharp ? 2 : 1);
    std::vector<std::uint32_t> source(std::size_t(source_width) * source_height);
    const auto at = [&](int x, int y) {
        const auto index = std::size_t(std::clamp(y, 0, int(cockpit_height) - 1)) * cockpit_width +
            std::clamp(x, 0, int(cockpit_width) - 1);
        return mask[index] ? 0u : palette[overlay.pixels[index]];
    };
    for (unsigned y = 0; y < cockpit_height; ++y) {
        for (unsigned x = 0; x < cockpit_width; ++x) {
            const auto center = at(x, y);
            if (!sharp) { source[std::size_t(y) * source_width + x] = center; continue; }
            const auto up = at(x, int(y) - 1), down = at(x, y + 1), left = at(int(x) - 1, y), right = at(x + 1, y);
            std::array<std::uint32_t, 4> pixels{center, center, center, center};
            // Scale2x compares masked colors, so world pixels participate as transparent texels.
            if (up != down && left != right) {
                pixels = {left == up ? left : center, up == right ? right : center,
                    left == down ? left : center, down == right ? right : center};
            }
            const auto row = std::size_t(y * 2) * source_width + x * 2;
            source[row] = pixels[0]; source[row + 1] = pixels[1];
            source[row + source_width] = pixels[2]; source[row + source_width + 1] = pixels[3];
        }
    }
    for (unsigned y = 0; y < image.height; ++y) {
        const auto source_row = std::size_t(y) * source_height / image.height * source_width;
        for (unsigned x = 0; x < image.width; ++x) {
            const auto color = source[source_row + std::size_t(x) * source_width / image.width];
            if (color != 0) { image.pixels[std::size_t(y) * image.width + x] = color; }
        }
    }
}
}
