#include "spatial_renderer.hpp"
#include "spatial_cockpit.hpp"
#include "training_route.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}

void save(const std::filesystem::path& path, const comanchecd::host::SpatialImage& image) {
    std::ofstream stream(path, std::ios::binary);
    stream << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (const auto pixel : image.pixels) {
        const char rgb[]{char(pixel >> 16), char(pixel >> 8), char(pixel)};
        stream.write(rgb, sizeof(rgb));
    }
    if (!stream) { throw std::runtime_error("Cannot write spatial qualification image"); }
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 1) { return 0; }
        require(argc == 3 || argc == 4, "Usage spatial_capture_tests INSTALLATION PAYLOAD_DIRECTORY [OUTPUT_DIRECTORY]");
        using namespace comanchecd;
        const auto selected = [&](const Installation& installation, PresentationDetail detail) {
            const auto frontend = installation.load_frontend();
            const auto definition = installation.load_missions(0).missions.at(0);
            auto resources = frontend.mission_resources;
            std::copy(definition.resources.begin(), definition.resources.end(), resources.begin());
            FlightMission mission{parse_gameplay(definition, resources, frontend.gameplay_presets, {}), resources};
            mission.presentation_detail = detail;
            return mission;
        };
        Installation indexed_installation(argv[1], argv[2]), spatial_installation(argv[1], argv[2]);
        AudioEntry entry;
        entry.execution = AudioExecution::scheduled;
        entry.music_start = MusicStart::deferred;
        auto indexed_audio = std::make_shared<MissionAudio>(indexed_installation.load_audio(), indexed_installation.load_menu_music(), entry, AudioTracePolicy::discard);
        auto spatial_audio = std::make_shared<MissionAudio>(spatial_installation.load_audio(), spatial_installation.load_menu_music(), entry, AudioTracePolicy::discard);
        IrqState indexed_irq{training_scene_seed}, spatial_irq{training_scene_seed};
        TrainingFlight indexed(indexed_installation, selected(indexed_installation, PresentationDetail::indexed), indexed_audio,
                               training_scene_seed, {}, {}, &indexed_irq);
        TrainingFlight observed(spatial_installation, selected(spatial_installation, PresentationDetail::spatial), spatial_audio,
                                training_scene_seed, {}, {}, &spatial_irq);
        require(!indexed.spatial_frame() && observed.spatial_frame(), "Only the requested mission may allocate spatial observations");
        const auto first = observed.spatial_frame();
        require(first->serial == 0 && first->scene_mask.size() == cockpit_width * cockpit_height,
                "Spatial observation must be ready at the first playable mission frame");
        const auto verify = [&] {
            const auto left = indexed.interrupt_state(), right = observed.interrupt_state();
            require(indexed.cockpit_image().pixels == observed.cockpit_image().pixels &&
                    indexed.cockpit_image().palette == observed.cockpit_image().palette &&
                    indexed_audio->execution_clock().cycles() == spatial_audio->execution_clock().cycles() && indexed_audio->irq_ticks() == spatial_audio->irq_ticks() &&
                    left.random_seed == right.random_seed && left.shake_offset == right.shake_offset &&
                    indexed.state().armor == observed.state().armor && indexed.state().ammunition == observed.state().ammunition,
                    "Spatial observation must preserve native images, CPU timing, IRQ, RNG and combat state");
            require(indexed_audio->take_pending_audio() == spatial_audio->take_pending_audio(),
                    "Spatial observation must preserve carried mixed PCM");
        };
        verify();
        const training_test::Route route(indexed_installation);
        host::SpatialRenderer renderer;
        host::SpatialCockpit cockpit;
        require(std::any_of(first->resources->hud_colors.begin(), first->resources->hud_colors.end(), [](bool color) { return color; }),
                "Spatial resources must retain palette membership from recovered HUD glyphs");
        host::CameraHistory history;
        history.observe(first, spatial_audio->execution_clock().cycles(), 0);
        double render_seconds = 0;
        unsigned rendered = 0, compared = 0, weather_frames = 0, reflection_frames = 0;
        for (unsigned tick = 0; tick < training_test::victory_tick_limit && !indexed.state().ended; ++tick) {
            const auto controls = route.controls(indexed.state(), tick);
            indexed.advance(controls);
            observed.advance(controls);
            require(indexed.render_audio(training_samples_per_tick) == observed.render_audio(training_samples_per_tick),
                    "Enhanced rendering must preserve the complete ordered mixed WAV sample stream");
            verify();
            ++compared;
            const auto snapshot = observed.spatial_frame();
            history.observe(snapshot, spatial_audio->execution_clock().cycles(), (tick + 1) / double(training_ticks_per_second));
            require(snapshot && snapshot->resources == first->resources && (indexed.state().ended || snapshot->serial == tick + 1),
                    "Mission frames must share immutable asset ownership with monotonically advancing identities");
            require(snapshot->effects.enabled && snapshot->effects.particles.size() <= 288,
                    "Spatial snapshots must contain bounded draw-boundary effect observations");
            weather_frames += unsigned(!snapshot->effects.particles.empty());
            reflection_frames += unsigned(snapshot->effects.reflections);
            if (!indexed.state().ended && indexed.state().armor > 0 && !indexed.state().exit_requested && !indexed.state().crash_rotation) {
                SessionPresentation presentation;
                presentation.spatial = snapshot;
                presentation.flight = observed.state();
                require(host::spatial_presentation(presentation, host::CockpitPresentation::flat),
                        "Observed weather and reflections must retain per-eye world rendering in flat and spatial cockpit modes");
            }
            if (!indexed.state().ended && ((tick < 128 && tick % 16 == 0) || tick % 128 == 0)) {
                ++rendered;
                const auto start = std::chrono::steady_clock::now();
                const auto& image = renderer.render(*snapshot, host::spatial_camera(snapshot->camera), observed.cockpit_image(), 960, 720);
                render_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                for (unsigned y = 0; y < image.height; ++y) {
                    for (unsigned x = 0; x < image.width; ++x) {
                        const auto source = std::size_t(y / 3) * cockpit_width + x / 3;
                        if (snapshot->scene_mask[source] != 0) { continue; }
                        const auto& c = observed.cockpit_image().palette[observed.cockpit_image().pixels[source]];
                        const auto color = 0xff000000u | ((c[0] * 255u / 63u) << 16) | ((c[1] * 255u / 63u) << 8) | (c[2] * 255u / 63u);
                        require(image.pixels[std::size_t(y) * image.width + x] == color,
                                "Every native cockpit and HUD pixel must survive high-resolution world compositing");
                    }
                }
                if (argc == 4) {
                    std::filesystem::create_directories(argv[3]);
                    save(std::filesystem::path(argv[3]) / ("enhanced-" + std::to_string(tick) + ".ppm"), image);
                    host::SpatialImage mask;
                    mask.width = cockpit_width; mask.height = cockpit_height;
                    for (const auto coverage : snapshot->scene_mask) { mask.pixels.push_back(coverage ? 0xffffffffu : 0xff000000u); }
                    save(std::filesystem::path(argv[3]) / ("mask-" + std::to_string(tick) + ".ppm"), mask);
                    host::SpatialImage native;
                    native.width = cockpit_width; native.height = cockpit_height;
                    for (const auto pixel : observed.cockpit_image().pixels) {
                        const auto& c = observed.cockpit_image().palette[pixel];
                        native.pixels.push_back(0xff000000u | ((c[0] * 255u / 63u) << 16) | ((c[1] * 255u / 63u) << 8) | (c[2] * 255u / 63u));
                    }
                    save(std::filesystem::path(argv[3]) / ("native-" + std::to_string(tick) + ".ppm"), native);
                }
                const auto display_time = (tick + 1.5) / double(training_ticks_per_second);
                const auto view = host::camera_view(history.sample(display_time), 960, 720);
                const auto& enhanced = renderer.render(*snapshot, view, history.sample_entities(display_time),
                    observed.cockpit_image(), 960, 720, host::CockpitScaling::sharp);
                require(enhanced.width == 960 && enhanced.height == 720, "Interpolated sharp presentation must produce the selected viewport");
                if (argc == 4) { save(std::filesystem::path(argv[3]) / ("sharp-interpolated-" + std::to_string(tick) + ".ppm"), enhanced); }
                auto cockpit_image = renderer.render_world(*snapshot, view, history.sample_entities(display_time),
                    observed.cockpit_image().palette, 960, 720);
                cockpit.draw(cockpit_image, host::cockpit_view({}, view.frustum), observed.cockpit_image(), *snapshot);
                if (argc == 4) { save(std::filesystem::path(argv[3]) / ("spatial-cockpit-" + std::to_string(tick) + ".ppm"), cockpit_image); }
                verify();
            }
        }
        require(first->serial == 0 && first->camera.x != observed.spatial_frame()->camera.x,
                "Retained scene snapshots must survive subsequent simulation updates");
        require(indexed.state().ended && observed.state().ended && indexed.objectives_remaining() == 0 &&
                observed.objectives_remaining() == 0, "Both capture modes must reach natural training victory");
        std::cout << compared << " continuous frames through victory preserve CPU, IRQ, RNG, images, combat and mixed PCM\n"
                  << "Effect observation frames weather " << weather_frames << " reflections " << reflection_frames << '\n'
                  << "960x720 mean render milliseconds " << render_seconds * 1000 / rendered << '\n';
        observed.set_presentation_detail(PresentationDetail::indexed);
        require(!observed.spatial_frame() && first->resources->assets.terrain.height.size() == terrain_side * terrain_side,
                "Disabling observation must preserve asset ownership for outstanding readers");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
