#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace comanchecd {
// Completed device output. Source accumulators retain 13 fractional PCM bits.
struct AudioOutputFrame {
    // Sources include native gain/filtering. Mixed is the canonical clipped PCM.
    static constexpr unsigned fractional_bits = 13;
    std::array<std::int64_t, 2> fm{}, digital{};
    std::array<std::int16_t, 2> mixed{};
};

// Presentation-only processing, called once per output frame on the producer.
using AudioOutputTransform = std::function<std::array<std::int16_t, 2>(const AudioOutputFrame&)>;
} // namespace comanchecd
