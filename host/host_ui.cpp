#include "host_ui.hpp"
#include <algorithm>
#include <cctype>
#include <limits>

namespace comanchecd::host {
namespace {
// Authored 5x7 host UI glyphs. Rows use the low five bits, left to right.
using Glyph = std::array<std::uint8_t, 7>;
constexpr std::array<Glyph, 36> glyphs{
    {{14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14},
     {30, 17, 17, 17, 17, 17, 30}, {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
     {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17}, {14, 4, 4, 4, 4, 4, 14},
     {7, 2, 2, 2, 2, 18, 12},      {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
     {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17}, {14, 17, 17, 17, 17, 17, 14},
     {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
     {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},       {17, 17, 17, 17, 17, 17, 14},
     {17, 17, 17, 17, 17, 10, 4},  {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
     {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},     {14, 17, 19, 21, 25, 17, 14},
     {4, 12, 4, 4, 4, 4, 14},      {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
     {2, 6, 10, 18, 31, 2, 2},     {31, 16, 16, 30, 1, 1, 30},   {14, 16, 16, 30, 17, 17, 14},
     {31, 1, 2, 4, 8, 8, 8},       {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14}}};

Glyph glyph(unsigned char value) {
    const auto code = std::toupper(value);
    if (code >= 'A' && code <= 'Z') {
        return glyphs[code - 'A'];
    }
    if (code >= '0' && code <= '9') {
        return glyphs[26 + code - '0'];
    }
    switch (code) {
    case ' ':
        return {};
    case '-':
        return {0, 0, 0, 31, 0, 0, 0};
    case '+':
        return {0, 4, 4, 31, 4, 4, 0};
    case '/':
        return {1, 1, 2, 4, 8, 16, 16};
    case ':':
        return {0, 4, 4, 0, 4, 4, 0};
    case '.':
        return {0, 0, 0, 0, 0, 4, 4};
    case ',':
        return {0, 0, 0, 0, 4, 4, 8};
    case '%':
        return {17, 2, 4, 4, 8, 16, 17};
    case '<':
        return {2, 4, 8, 16, 8, 4, 2};
    case '>':
        return {8, 4, 2, 1, 2, 4, 8};
    case '(':
        return {2, 4, 8, 8, 8, 4, 2};
    case ')':
        return {8, 4, 2, 2, 2, 4, 8};
    default:
        return {14, 17, 1, 2, 4, 0, 4};
    }
}

std::uint8_t nearest_color(const Palette& palette, int r, int g, int b) {
    int distance = std::numeric_limits<int>::max();
    std::uint8_t best = 0;
    for (unsigned index = 0; index < palette.size(); ++index) {
        const auto dr = int(palette[index][0]) - r, dg = int(palette[index][1]) - g,
                   db = int(palette[index][2]) - b;
        const auto candidate = dr * dr + dg * dg + db * db;
        if (candidate < distance) {
            distance = candidate;
            best = static_cast<std::uint8_t>(index);
        }
    }
    return best;
}
} // namespace

CockpitImage ui_image() {
    CockpitImage image;
    image.palette[0] = {2, 6, 7};
    image.palette[1] = {5, 13, 15};
    image.palette[2] = {47, 56, 53};
    image.palette[3] = {17, 56, 42};
    image.palette[4] = {62, 46, 17};
    return image;
}

void ui_text(CockpitImage& image, int x, int y, const std::string& text, UiColor color) {
    for (const unsigned char code : text) {
        const auto shape = glyph(code);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                const auto px = x + column, py = y + row;
                if (px >= 0 && px < int(cockpit_width) && py >= 0 && py < int(cockpit_height) &&
                    (shape[row] & (1 << (4 - column))) != 0) {
                    image.pixels[py * cockpit_width + px] = static_cast<std::uint8_t>(color);
                }
            }
        }
        x += 6;
        if (x >= int(cockpit_width)) {
            break;
        }
    }
}

void ui_rectangle(CockpitImage& image, int x, int y, unsigned width, unsigned height, UiColor color) {
    const auto right = std::min<int>(cockpit_width, x + int(width));
    const auto bottom = std::min<int>(cockpit_height, y + int(height));
    for (int row = std::max(0, y); row < bottom; ++row) {
        for (int column = std::max(0, x); column < right; ++column) {
            image.pixels[row * cockpit_width + column] = static_cast<std::uint8_t>(color);
        }
    }
}

ButtonPromptImage button_prompt_image(const std::string& first, const std::string& second) {
    auto image = ui_image();
    ui_text(image, 2, 1, first);
    ui_text(image, 2, 10, second);
    ButtonPromptImage result;
    for (std::size_t index = 0; index < result.rgba.size() / 4; ++index) {
        const auto& color = image.palette[image.pixels[index]];
        for (unsigned channel = 0; channel < 3; ++channel) {
            result.rgba[index * 4 + channel] = color[channel] * 255u / 63u;
        }
        result.rgba[index * 4 + 3] = 255;
    }
    return result;
}

CockpitImage with_button_prompts(const CockpitImage& source, const ButtonPromptImage& prompts) {
    auto image = source;
    const auto background = nearest_color(source.palette, 2, 6, 7);
    const auto foreground = nearest_color(source.palette, 47, 56, 53);
    for (std::size_t index = 0; index < prompts.rgba.size() / 4; ++index) {
        image.pixels[index] = prompts.rgba[index * 4] > 100 ? foreground : background;
    }
    return image;
}

void composite_button_prompts(std::vector<std::uint32_t>& pixels, unsigned width, unsigned height,
                              const ButtonPromptImage& prompts) {
    const auto scale = std::min(double(width) / cockpit_width, double(height) / cockpit_height);
    const auto panel_width = std::max(1u, static_cast<unsigned>(ButtonPromptImage::width * scale));
    const auto panel_height = std::max(1u, static_cast<unsigned>(ButtonPromptImage::height * scale));
    const auto left = (width - panel_width) / 2;
    for (unsigned y = 0; y < panel_height; ++y) {
        for (unsigned x = 0; x < panel_width; ++x) {
            const auto source = ((y * ButtonPromptImage::height / panel_height) * ButtonPromptImage::width +
                                 x * ButtonPromptImage::width / panel_width) *
                                4;
            pixels[y * width + left + x] = 0xff000000u | (std::uint32_t(prompts.rgba[source]) << 16) |
                                           (std::uint32_t(prompts.rgba[source + 1]) << 8) |
                                           prompts.rgba[source + 2];
        }
    }
}
} // namespace comanchecd::host
