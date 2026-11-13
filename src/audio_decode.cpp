#include "audio_decode.hpp"

#include <stdexcept>

namespace comanchecd::audio_detail {
std::uint32_t integer(const std::vector<std::uint8_t>& bytes, std::size_t position, std::size_t width) {
    if (position > bytes.size() || width > bytes.size() - position) {
        throw std::invalid_argument("Truncated audio resource");
    }
    std::uint32_t result = 0;
    for (std::size_t byte = 0; byte < width; ++byte) {
        result |= std::uint32_t(bytes[position + byte]) << (byte * 8);
    }
    return result;
}

std::vector<Sample> sample_bank(const std::vector<std::uint8_t>& bytes) {
    if (integer(bytes, 0, 2) != 0x314b) {
        throw std::invalid_argument("Expected K1 sampled sound bank");
    }
    const auto count = integer(bytes, 2, 2);
    if (count > 256 || 4 + count * 4 > bytes.size()) {
        throw std::invalid_argument("Invalid sampled sound directory");
    }
    std::vector<Sample> result;
    for (std::size_t index = 0; index < count; ++index) {
        const auto start = integer(bytes, 4 + index * 4, 4);
        const auto size = integer(bytes, start, 2);
        const auto step = integer(bytes, start + 2, 2);
        if (size <= 4 || start > bytes.size() || size > bytes.size() - start) {
            throw std::invalid_argument("Invalid sampled sound range");
        }
        result.push_back({{bytes.begin() + start, bytes.begin() + start + size}, step});
    }
    return result;
}
}
