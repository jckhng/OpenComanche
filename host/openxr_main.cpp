#include "host_preferences.hpp"
#include "launch_options.hpp"
#include "session_app.hpp"
#include "openxr_host.hpp"
#include "openxr_runtime.hpp"
#include <iostream>

int main(int argc, char** argv) {
    try {
        constexpr const char* usage =
            "comanche_openxr INSTALLATION PAYLOAD_DIRECTORY PROFILE_DIRECTORY"
            " [--xr-world-scale CELLS_PER_METRE] [--xr-render-scale 0.1..1]"
            " [--xr-loader FILE] [--xr-renderer gpu|cpu] [--xr-profile CSV] [enhanced SDL2 options]";
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << usage
                      << "\nOpenXR headset presentation. Default world scale is 8 map cells per metre.\n"
                      << "comanche_openxr --openxr-info [--xr-loader FILE]\n"
                      << "See PREVIEW.md for SDL2 control and presentation options.\n";
            return 0;
        }
        using namespace comanchecd::host;
        if ((argc == 2 || (argc == 4 && std::string(argv[2]) == "--xr-loader")) &&
            std::string(argv[1]) == "--openxr-info") {
            const SdlOpenXrLoader loader(argc == 4 ? argv[3] : "");
            const OpenXrRuntime runtime(loader.entry());
            const auto& capabilities = runtime.capabilities();
            std::cout << capabilities.runtime_name << ' ' << XR_VERSION_MAJOR(capabilities.runtime_version)
                      << '.' << XR_VERSION_MINOR(capabilities.runtime_version) << '.'
                      << XR_VERSION_PATCH(capabilities.runtime_version) << "\nOpenGL "
                      << XR_VERSION_MAJOR(capabilities.minimum_opengl) << '.'
                      << XR_VERSION_MINOR(capabilities.minimum_opengl) << " through "
                      << XR_VERSION_MAJOR(capabilities.maximum_opengl) << '.'
                      << XR_VERSION_MINOR(capabilities.maximum_opengl) << '\n';
            return 0;
        }
        if (argc < 4) {
            throw std::invalid_argument(usage);
        }
        const HostPreferences preferences(std::filesystem::absolute(argv[3]).lexically_normal());
        auto arguments = launch_arguments(Application::openxr, {argv + 4, argv + argc});
        const auto xr = take_openxr_options(arguments);
        const auto options = preferences.resolve(arguments);
        run_session_app(argv[1], argv[2], argv[3], [&] {
            auto host = std::make_unique<OpenXrHost>(options, xr);
            preferences.write(options);
            return host;
        });
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
