#pragma once

#include "flight_host.hpp"
#include <functional>

namespace comanchecd::host {
// The caller owns devices. The simulation callback owns session access until return.
void run_presentation(FlightHost& host, const std::function<void(FlightHost&)>& simulate);
} // namespace comanchecd::host
