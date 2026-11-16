#pragma once
#include <cstdint>

namespace comanchecd::host {
enum class GraphicsColorSpace { srgb, linear };
struct GraphicsViewport {
    int x = 0, y = 0;
    unsigned width = 0, height = 0;
};
struct GraphicsTarget {
    // Texture zero selects the current context's default framebuffer.
    std::uint32_t texture = 0, width = 0, height = 0;
    GraphicsColorSpace color_space = GraphicsColorSpace::srgb;
    GraphicsViewport viewport;
};
} // namespace comanchecd::host
