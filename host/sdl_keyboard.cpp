#include "sdl_keyboard.hpp"
#include <array>

namespace comanchecd::host {
std::uint8_t dos_menu_scan(SDL_Scancode key) {
    constexpr std::array<std::uint8_t, 26> letters{0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                                   0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                                   0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};
    if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z) {
        return letters[key - SDL_SCANCODE_A];
    }
    if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_0) {
        return static_cast<std::uint8_t>(2 + key - SDL_SCANCODE_1);
    }
    switch (key) {
    case SDL_SCANCODE_ESCAPE:
        return 0x01;
    case SDL_SCANCODE_BACKSPACE:
        return 0x0e;
    case SDL_SCANCODE_TAB:
        return 0x0f;
    case SDL_SCANCODE_RETURN:
        return 0x1c;
    case SDL_SCANCODE_SPACE:
        return 0x39;
    case SDL_SCANCODE_UP:
        return 0x48;
    case SDL_SCANCODE_LEFT:
        return 0x4b;
    case SDL_SCANCODE_RIGHT:
        return 0x4d;
    case SDL_SCANCODE_DOWN:
        return 0x50;
    case SDL_SCANCODE_DELETE:
        return 0x53;
    case SDL_SCANCODE_MINUS:
        return 0x0c;
    case SDL_SCANCODE_EQUALS:
        return 0x0d;
    case SDL_SCANCODE_LEFTBRACKET:
        return 0x1a;
    case SDL_SCANCODE_RIGHTBRACKET:
        return 0x1b;
    case SDL_SCANCODE_SEMICOLON:
        return 0x27;
    case SDL_SCANCODE_APOSTROPHE:
        return 0x28;
    case SDL_SCANCODE_GRAVE:
        return 0x29;
    case SDL_SCANCODE_BACKSLASH:
        return 0x2b;
    case SDL_SCANCODE_COMMA:
        return 0x33;
    case SDL_SCANCODE_PERIOD:
        return 0x34;
    case SDL_SCANCODE_SLASH:
        return 0x35;
    default:
        return 0;
    }
}

void append_dos_key(std::vector<std::uint8_t>& bytes, SDL_Scancode key, bool down) {
    auto scan = dos_menu_scan(key);
    if (scan == 0) {
        switch (key) {
        case SDL_SCANCODE_F1:
        case SDL_SCANCODE_F2:
        case SDL_SCANCODE_F3:
        case SDL_SCANCODE_F4:
        case SDL_SCANCODE_F5:
        case SDL_SCANCODE_F6:
        case SDL_SCANCODE_F7:
        case SDL_SCANCODE_F8:
        case SDL_SCANCODE_F9:
        case SDL_SCANCODE_F10:
            scan = 0x3b + key - SDL_SCANCODE_F1;
            break;
        case SDL_SCANCODE_F11:
            scan = 0x57;
            break;
        case SDL_SCANCODE_F12:
            scan = 0x58;
            break;
        case SDL_SCANCODE_LSHIFT:
            scan = 0x2a;
            break;
        case SDL_SCANCODE_RSHIFT:
            scan = 0x36;
            break;
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL:
            scan = 0x1d;
            break;
        case SDL_SCANCODE_LALT:
        case SDL_SCANCODE_RALT:
            scan = 0x38;
            break;
        default:
            break;
        }
    }
    if (scan == 0) {
        return;
    }
    if (key == SDL_SCANCODE_UP || key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_RIGHT ||
        key == SDL_SCANCODE_DOWN || key == SDL_SCANCODE_DELETE || key == SDL_SCANCODE_RCTRL ||
        key == SDL_SCANCODE_RALT) {
        bytes.push_back(0xe0);
    }
    bytes.push_back(static_cast<std::uint8_t>(scan | (down ? 0 : 0x80)));
}

} // namespace comanchecd::host
