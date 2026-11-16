#include "display_settings.hpp"
#include "sdl_options.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        using namespace comanchecd::host;
        const auto configured =
            parse_sdl_options({"--mode", "enhanced", "--fps", "144", "--vsync", "off", "--window-size",
                               "1600x900", "--window-position", "-1600,40", "--window-mode", "maximized"});
        require(configured.frame_limit.mode == FrameLimitMode::fixed && configured.frame_limit.rate == 144 &&
                    configured.pacing == DisplayPacing::immediate && configured.window.width == 1600 &&
                    configured.window.height == 900 && configured.window.position->x == -1600 &&
                    configured.window.position->y == 40 && configured.window.mode == WindowMode::maximized,
                "Display options must retain independent pacing and signed multi-monitor geometry");
        require(parse_sdl_options({"--mode", "enhanced", "--fps", "auto"}).frame_limit.mode ==
                        FrameLimitMode::display &&
                    parse_sdl_options({"--mode", "enhanced", "--fps", "unlimited"}).frame_limit.mode ==
                        FrameLimitMode::unlimited,
                "Automatic and unlimited pacing must have explicit modes");
        for (const auto& pair : std::vector<std::vector<std::string>>{{"--fps", "0"},
                                                                      {"--fps", "14"},
                                                                      {"--fps", "1001"},
                                                                      {"--fps", "60hz"},
                                                                      {"--vsync", "adaptive"},
                                                                      {"--window-size", "0x720"},
                                                                      {"--window-size", "1280X720"},
                                                                      {"--window-size", "9999999999x720"},
                                                                      {"--window-position", "1,2,3"},
                                                                      {"--window-position", "--2,0"},
                                                                      {"--window-mode", "minimized"}}) {
            bool rejected = false;
            try {
                parse_sdl_options({"--mode", "enhanced", pair[0], pair[1]});
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected, "Invalid display values must be rejected before changing devices");
        }
        bool faithful_rejected = false;
        try {
            parse_sdl_options({"--fps", "60"});
        } catch (const std::invalid_argument&) {
            faithful_rejected = true;
        }
        require(faithful_rejected, "Enhanced pacing must retain its explicit launch boundary");

        using namespace std::chrono;
        FrameDeadline deadline;
        const auto start = FrameDeadline::Clock::time_point{};
        require(deadline.schedule(start, 100) == start, "The first frame must be immediate");
        require(deadline.schedule(start + milliseconds(3), 100) == start + milliseconds(10),
                "Rendering time must consume the existing frame budget");
        require(deadline.schedule(start + milliseconds(70), 100) == start + milliseconds(70),
                "A stall must discard missed deadlines");
        require(deadline.schedule(start + milliseconds(71), 50) == start + milliseconds(71),
                "Changing the limit must reset its deadline");
        require(deadline.schedule(start + milliseconds(72), 0) == start + milliseconds(72),
                "Unlimited rendering must never wait");
        deadline.reset();
        require(deadline.schedule(start + milliseconds(73), 50) == start + milliseconds(73),
                "Resetting after a display transition must discard the prior schedule");
        std::cout << "Display settings and frame deadlines passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
