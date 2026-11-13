#pragma once

#include "sdl_host.hpp"

namespace comanchecd::host {
SdlHostOptions parse_sdl_options(const std::vector<std::string>& arguments);
}
