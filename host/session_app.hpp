#pragma once
#include "flight_host.hpp"
#include <filesystem>
#include <functional>

namespace comanchecd::host {
void run_session_app(const std::filesystem::path& installation, const std::filesystem::path& payload,
                     const std::filesystem::path& profile,
                     const std::function<std::unique_ptr<FlightHost>()>& create_host);
}
