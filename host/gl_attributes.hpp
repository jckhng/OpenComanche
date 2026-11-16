#pragma once
#include <SDL.h>
#include <array>

namespace comanchecd::host {
class GlAttributes {
  public:
    GlAttributes() {
        for (unsigned index = 0; index < attributes_.size(); ++index) {
            if (SDL_GL_GetAttribute(attributes_[index], &values_[index]) != 0) {
                values_[index] = defaults_[index];
            }
        }
    }
    ~GlAttributes() {
        for (unsigned index = 0; index < attributes_.size(); ++index) {
            SDL_GL_SetAttribute(attributes_[index], values_[index]);
        }
    }

  private:
    const std::array<SDL_GLattr, 3> attributes_{SDL_GL_CONTEXT_MAJOR_VERSION, SDL_GL_CONTEXT_MINOR_VERSION,
                                                SDL_GL_CONTEXT_PROFILE_MASK};
    const std::array<int, 3> defaults_{2, 1, 0};
    std::array<int, 3> values_{};
};
} // namespace comanchecd::host
