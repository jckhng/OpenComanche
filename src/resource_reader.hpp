#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace comanchecd::resource_reader {
// Callers supply widths up to four bytes and retain their format-specific error.
inline std::uint32_t little_endian(const std::vector<std::uint8_t>& bytes, std::size_t offset,
                                   std::size_t width, const char* truncated_message) {
    if (offset > bytes.size() || width > bytes.size() - offset) {
        throw std::invalid_argument(truncated_message);
    }
    std::uint32_t result = 0;
    for (std::size_t byte = 0; byte < width; ++byte) {
        result |= std::uint32_t(bytes[offset + byte]) << (byte * 8);
    }
    return result;
}
}
