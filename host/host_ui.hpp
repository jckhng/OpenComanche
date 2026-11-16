#pragma once

#include "comanchecd/cockpit.hpp"
#include <string>

namespace comanchecd::host {
enum class UiColor : std::uint8_t { background, panel, text, accent, warning };

enum class ControllerMenuCommand { up, down, left, right, accept, back, clear, close };
CockpitImage ui_image();
void ui_text(CockpitImage& image, int x, int y, const std::string& text, UiColor color = UiColor::text);
void ui_rectangle(CockpitImage& image, int x, int y, unsigned width, unsigned height, UiColor color);

struct ButtonPromptImage {
    static constexpr unsigned width = 320, height = 18;
    std::array<std::uint8_t, width * height * 4> rgba{};
};
ButtonPromptImage button_prompt_image(const std::string& first, const std::string& second);
CockpitImage with_button_prompts(const CockpitImage& source, const ButtonPromptImage& prompts);
void composite_button_prompts(std::vector<std::uint32_t>& pixels, unsigned width, unsigned height,
                              const ButtonPromptImage& prompts);
} // namespace comanchecd::host
