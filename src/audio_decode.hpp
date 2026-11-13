#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace comanchecd::audio_detail {
struct Sample {
    std::vector<std::uint8_t> bytes;
    std::uint32_t step = 0;
};

std::uint32_t integer(const std::vector<std::uint8_t>& bytes, std::size_t position, std::size_t width);
std::vector<Sample> sample_bank(const std::vector<std::uint8_t>& bytes);
}
