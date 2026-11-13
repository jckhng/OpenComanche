#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace comanchecd::cd_executable {
constexpr std::size_t packed_bytes = 67221;
constexpr std::size_t image_bytes = 404738;
constexpr std::uint64_t supported_executable_identity = 0xca98bc78eef27ed8ull;

// Embedded resource addresses are relative to this offset in the unpacked image.
constexpr std::size_t protected_image = 0x1800;
constexpr std::size_t transfer = 0x2f160;

namespace frontend {
constexpr std::size_t font = 0x2a47d;
constexpr std::size_t font_end = 0x2b1e5;
constexpr std::size_t glyph_widths = 0x2a485;
constexpr std::size_t pilot_header = 0x2cd5c;
constexpr std::size_t pilot_header_bytes = 0x12;
constexpr std::size_t editor_prompt = 0x2b411;
constexpr std::size_t editor_prompt_bytes = 54;
constexpr std::size_t row_prefix = 0x2b3fc;
constexpr std::size_t row_tail = 0x2b405;
constexpr std::size_t normal_keys = 0x19fd;
constexpr std::size_t shifted_keys = 0x197d;
constexpr std::size_t main_menu = 0x2d31e;
constexpr std::size_t mission_menu = 0x2d56d;
constexpr std::size_t briefing_choices = 0x2d53d;
constexpr std::size_t loading_prompt = 0x2d6c5;
constexpr std::size_t statistics = 0x2bf57;
constexpr std::size_t statistics_bytes = 130;
constexpr std::size_t locked_prompt = 0x29a9a;
constexpr std::size_t debrief_text = 0x2ba85;
constexpr std::size_t debrief_text_bytes = 255;
constexpr std::size_t debrief_messages = 0x2b90d;
constexpr std::size_t debrief_message_bytes = 47;
constexpr std::size_t gameplay_presets = 0x2e294;
constexpr std::size_t mission_resources = 0x2db34;
constexpr std::size_t campaign_menu = 0x2d3d8;
}

namespace settings {
constexpr std::uint8_t disabled_flag = 0x7b;
constexpr std::size_t record = 0x28d24;
constexpr std::size_t header = 0x29590;
constexpr std::size_t center_marker = 0x297af;
constexpr std::size_t flight_hud = 0x297c4;
constexpr std::size_t heading_tape = 0x297d5;
constexpr std::array<std::size_t, 4> category_menus{0x295d7, 0x2963e, 0x2969f, center_marker};
constexpr std::array<std::size_t, 5> loading_menus{
    category_menus[0], category_menus[1], category_menus[2], category_menus[3], 0x29856};
constexpr std::array<std::size_t, 15> flag_addresses{0x29763, 0x2977d, 0x29670, 0x29679, 0x29688, 0x29691,
    center_marker, flight_hud, heading_tape, 0x297e3, 0x297f1, 0x2980b, 0x29821, 0x2983c, 0x29849};
constexpr std::array<unsigned, 15> flag_fields{41, 42, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40};
constexpr std::size_t about = 0x299e2;
constexpr std::size_t saved = 0x29a81;
}

namespace scene {
constexpr std::size_t previous_x = 0x21ebc;
constexpr std::size_t previous_y = 0x220ec;
constexpr std::size_t previous_height = 0x229af;
constexpr std::size_t animation_frame = 0x22897;
constexpr std::size_t rotor_phase = 0x22e87;
}

namespace cockpit {
constexpr std::size_t panel_y = 0x7b14;
constexpr std::size_t left_panel_x = 0x7b1a;
constexpr std::size_t right_panel_x = 0x7b20;
constexpr std::size_t weapon_flash = 0x7c01;
constexpr std::size_t shake_displacement = 0x16630;
constexpr std::size_t map_scale = 0x1a918;
constexpr std::size_t chaff = 0x1aa29;
constexpr std::size_t flares = 0x1aa2a;
}

namespace terrain {
constexpr std::size_t sine = 0x35f10;
constexpr std::size_t angle_octants = 0x236b1;
constexpr std::size_t angle_ratio = 0x236d1;
constexpr std::size_t sprite_offsets = 0x1e49c;
constexpr std::size_t sprite_steps = 0x1e89c;
constexpr std::size_t sprite_step_bytes = 0x2041;
constexpr std::size_t initial_height_gap = 0x1aa99;
constexpr std::size_t gust = 0x28058;
}

namespace audio {
constexpr std::size_t instruments = 0x30139;
constexpr std::size_t instrument_bytes = 16;
constexpr std::size_t pitch = 0x30efc;
constexpr std::size_t volume = 0x30f7c;
constexpr std::size_t envelopes = 0x31012;
constexpr std::size_t envelope_scan_limit = 512;
constexpr std::size_t frequencies = 0x30939;
constexpr std::size_t sample_conversion = 0x4df9;
constexpr std::size_t soft_sample_conversion = 0x4bf9;
}
}
