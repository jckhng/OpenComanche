#include "audio_menu.hpp"
#include <algorithm>

namespace comanchecd::host {
namespace {
constexpr int volume_step = 5;
constexpr unsigned bar_width = 120;
constexpr std::array<int, 6> rows{54, 84, 114, 153, 175, 197};
} // namespace

AudioMenu::AudioMenu(VolumeLevels levels) : levels_(levels) {
    validate_volume(levels);
}

unsigned* AudioMenu::selected_volume() {
    switch (row_) {
    case Row::master:
        return &levels_.master;
    case Row::music:
        return &levels_.music;
    case Row::effects:
        return &levels_.effects;
    default:
        return nullptr;
    }
}

void AudioMenu::command(ControllerMenuCommand command) {
    constexpr auto count = static_cast<unsigned>(Row::count);
    if (command == ControllerMenuCommand::back || command == ControllerMenuCommand::close) {
        close_ = true;
        return;
    }
    if (command == ControllerMenuCommand::up) {
        row_ = static_cast<Row>((unsigned(row_) + count - 1) % count);
        return;
    }
    if (command == ControllerMenuCommand::down) {
        row_ = static_cast<Row>((unsigned(row_) + 1) % count);
        return;
    }
    if (auto* value = selected_volume()) {
        if (command == ControllerMenuCommand::left) {
            *value = std::max(0, int(*value) - volume_step);
        }
        if (command == ControllerMenuCommand::right || command == ControllerMenuCommand::accept) {
            *value = std::min(maximum_volume, *value + volume_step);
        }
        if (command == ControllerMenuCommand::clear) {
            *value = 0;
        }
        message_ = "APPLY AND SAVE, THEN CLOSE TO LISTEN";
        return;
    }
    if (command != ControllerMenuCommand::accept) {
        return;
    }
    if (row_ == Row::apply) {
        apply_ = true;
    }
    if (row_ == Row::defaults) {
        levels_ = {};
        message_ = "DEFAULTS STAGED. APPLY TO SAVE";
    }
    if (row_ == Row::close) {
        close_ = true;
    }
}

bool AudioMenu::close_requested() const {
    return close_;
}
bool AudioMenu::take_apply_request() {
    const auto result = apply_;
    apply_ = false;
    return result;
}
void AudioMenu::save_result(const std::string& error) {
    message_ = error.empty() ? "SAVED. CLOSE TO LISTEN" : "SAVE FAILED. CHANGES RETAINED, RETRY APPLY";
}
const VolumeLevels& AudioMenu::levels() const {
    return levels_;
}

CockpitImage AudioMenu::image() const {
    auto image = ui_image();
    ui_rectangle(image, 8, 8, 304, 28, UiColor::panel);
    ui_text(image, 16, 12, "ENHANCED AUDIO", UiColor::accent);
    ui_text(image, 16, 24, "PAUSED - APPLY AND CLOSE TO LISTEN");
    const std::array<std::string, 6> labels{"MASTER",           "MUSIC", "EFFECTS / VOICE", "APPLY AND SAVE",
                                            "RESTORE DEFAULTS", "CLOSE"};
    const std::array<unsigned, 3> values{levels_.master, levels_.music, levels_.effects};
    for (unsigned row = 0; row < labels.size(); ++row) {
        const auto y = rows[row];
        const auto selected = row == unsigned(row_);
        if (selected) {
            ui_rectangle(image, 10, y - 4, 300, 21, UiColor::panel);
        }
        ui_text(image, 16, y, labels[row], selected ? UiColor::accent : UiColor::text);
        if (row < values.size()) {
            ui_rectangle(image, 138, y + 2, bar_width, 5, UiColor::background);
            ui_rectangle(image, 138, y + 2, bar_width * values[row] / maximum_volume, 5, UiColor::accent);
            ui_text(image, 270, y, std::to_string(values[row]) + "%");
        }
    }
    ui_text(image, 16, 137, "FM MUSIC / SAMPLED EFFECTS AND VOICE");
    ui_text(image, 16, 214, message_.substr(0, 48), UiColor::warning);
    ui_text(image, 16, 228, "ARROWS / D-PAD   ENTER / A   ESC / B");
    return image;
}
} // namespace comanchecd::host
