#pragma once
#include "sdl_common.hpp"
#include <cstdint>
#include <vector>

namespace comanchecd::host {
constexpr std::int32_t keyboard_control = 350;
constexpr std::uint8_t maximum_throttle = 15;
std::uint8_t dos_menu_scan(SDL_Scancode key);
void append_dos_key(std::vector<std::uint8_t>& bytes, SDL_Scancode key, bool down);
} // namespace comanchecd::host
