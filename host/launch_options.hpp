#pragma once
#include <string>
#include <vector>

namespace comanchecd::host {
enum class Application { faithful, enhanced, openxr };
std::vector<std::string> launch_arguments(Application application, const std::vector<std::string>& arguments);
} // namespace comanchecd::host
