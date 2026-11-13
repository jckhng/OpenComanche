#include "comanchecd/assets.hpp"
#include "../src/resource_reader.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::vector<std::uint8_t> picture() {
    std::vector<std::uint8_t> file(128, 0);
    file[8] = 0x47;
    file[9] = 1;
    file[10] = 239;
    std::size_t remaining = 328 * 240;
    while (remaining > 0) {
        const auto count = std::min<std::size_t>(remaining, 63);
        file.push_back(static_cast<std::uint8_t>(0xc0 | count));
        file.push_back(0xc5);
        remaining -= count;
    }
    file.push_back(12);
    file.insert(file.end(), 768, 252);
    return file;
}

void rejects(const std::vector<std::uint8_t>& bytes) {
    try {
        comanchecd::decode_frontend_picture(bytes);
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Malformed picture accepted");
}

void check_resource_reader() {
    using comanchecd::resource_reader::little_endian;
    const std::vector<std::uint8_t> bytes{0xaa, 0x12, 0x34, 0x56, 0x78};
    const std::array<std::uint32_t, 5> expected{0, 0x12, 0x3412, 0x563412, 0x78563412};
    for (std::size_t width = 0; width < expected.size(); ++width) {
        require(little_endian(bytes, 1, width, "truncated") == expected[width],
                "Resource integers must preserve byte order and all supported widths");
    }
    require(little_endian({}, 0, 0, "truncated") == 0 &&
            little_endian(bytes, bytes.size(), 0, "truncated") == 0 &&
            little_endian(bytes, bytes.size() - 1, 1, "truncated") == 0x78 &&
            little_endian({255, 255, 255, 255}, 0, 4, "truncated") == 0xffffffffu,
            "Resource reads must preserve empty, final-byte and full-width values");
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    const std::array<std::array<std::size_t, 2>, 5> invalid{{
        {bytes.size(), 1}, {bytes.size() - 1, 2}, {maximum, 2}, {1, maximum}, {maximum, 0}}};
    for (const char* message : {"Truncated resource word", "Truncated audio resource",
            "Truncated cockpit sprite", "Truncated SG resource", "Truncated playback resource"}) {
        for (const auto& range : invalid) {
            bool rejected = false;
            try {
                little_endian(bytes, range[0], range[1], message);
            } catch (const std::invalid_argument& error) {
                require(std::string(error.what()) == message, "Resource readers must retain format-specific errors");
                rejected = true;
            }
            require(rejected, "Resource ranges must reject truncation and size overflow");
        }
    }
}
}

int main() {
    try {
        check_resource_reader();
        const auto file = picture();
        const auto decoded = comanchecd::decode_frontend_picture(file);
        require(decoded.palette[255][2] == 63, "Palette must convert eight-bit channels to six-bit");
        for (const auto& plane : decoded.background) {
            require(plane.size() == comanchecd::background_bytes && plane[19679] == 0xc5 && plane[19680] == 0,
                    "Picture must deinterleave all pixels and retain a zero background tail");
        }
        auto damaged = file;
        damaged.pop_back();
        rejects(damaged);
        damaged = file;
        damaged[128] = 0xc0;
        rejects(damaged);
        damaged = file;
        damaged[10] = 238;
        rejects(damaged);
        damaged.resize(129);
        rejects(damaged);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
