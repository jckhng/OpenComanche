#pragma once

#include "sdl_options.hpp"

#include <filesystem>

namespace comanchecd::host {
class HostPreferences {
public:
    explicit HostPreferences(std::filesystem::path directory);
    SdlHostOptions read() const;
    SdlHostOptions resolve(const std::vector<std::string>& arguments) const;
    void write(const SdlHostOptions& options) const;
private:
    std::filesystem::path directory_;
};
}
