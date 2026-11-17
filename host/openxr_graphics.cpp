#include "openxr_graphics.hpp"
#include "sdl_common.hpp"
#include "gl_scene.hpp"
#include "gl_attributes.hpp"
#include <SDL_syswm.h>
#include <SDL_opengl.h>
#if defined(_WIN32)
#include <unknwn.h>
#define XR_USE_PLATFORM_WIN32
#elif defined(__linux__)
#include <GL/glx.h>
#define XR_USE_PLATFORM_XLIB
#else
#error OpenXR graphics currently supports Windows WGL and Linux X11 GLX
#endif
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace comanchecd::host {
class OpenXrGraphics::Impl {
public:
    explicit Impl(const OpenXrCapabilities& requirements) {
        GlAttributes restore;
        const auto desired = requirements.minimum_opengl;
        const auto profile = desired >= XR_MAKE_VERSION(3, 2, 0) ? SDL_GL_CONTEXT_PROFILE_CORE : 0;
        if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, int(XR_VERSION_MAJOR(desired))) != 0 ||
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, int(XR_VERSION_MINOR(desired))) != 0 ||
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile) != 0) {
            throw sdl_error("XR OpenGL attributes failed");
        }
        window_.reset(SDL_CreateWindow("OpenComanche XR graphics", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN));
        if (!window_) { throw sdl_error("XR SDL2 OpenGL window failed"); }
        context_.reset(SDL_GL_CreateContext(window_.get()));
        if (!context_) { throw sdl_error("XR SDL2 OpenGL context failed"); }
        current();
        int major = 0, minor = 0;
        if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major) != 0 ||
                SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor) != 0) {
            throw sdl_error("XR OpenGL version query failed");
        }
        const auto actual = XR_MAKE_VERSION(major, minor, 0);
        if (actual < requirements.minimum_opengl) {
            throw std::runtime_error("SDL2 OpenGL context falls below the XR runtime minimum version");
        }
        for (unsigned index = 0; index < linear_.size(); ++index) {
            const double value = index / 255.0;
            const auto converted = value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
            linear_[index] = static_cast<std::uint8_t>(std::lround(converted * 255));
        }
    }

    ~Impl() {
        if (scene_) {
            SDL_GL_MakeCurrent(window_.get(), context_.get());
            scene_.reset();
        }
    }
    void render_eye(const OpenXrTarget& target, const SpatialFrame& frame, const CameraView& view,
                    const EntityPoses& poses, const CockpitImage& source, const std::optional<CameraView>& cockpit) {
        if (!target.texture) { throw std::invalid_argument("XR requires an acquired swapchain texture"); }
        current();
        if (!scene_) { scene_ = std::make_unique<GlScene>(); }
        scene_->draw(target, frame, view, poses, source, cockpit);
    }
    void current() {
        if (SDL_GL_MakeCurrent(window_.get(), context_.get()) != 0) { throw sdl_error("XR OpenGL context activation failed"); }
    }
    void release() {
        if (SDL_GL_MakeCurrent(window_.get(), nullptr) != 0) { throw sdl_error("XR OpenGL context release failed"); }
    }
    std::unique_ptr<OpenXrSession> create_session(const OpenXrRuntime& runtime, double scale) {
        current();
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window_.get(), &info)) { throw sdl_error("XR native SDL2 window binding unavailable"); }
#if defined(_WIN32)
        if (info.subsystem != SDL_SYSWM_WINDOWS) { throw std::runtime_error("XR OpenGL requires SDL2's Windows video driver"); }
        XrGraphicsBindingOpenGLWin32KHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
        binding.hDC = wglGetCurrentDC();
        binding.hGLRC = wglGetCurrentContext();
#elif defined(__linux__)
        if (info.subsystem != SDL_SYSWM_X11) { throw std::runtime_error("XR OpenGL requires SDL_VIDEODRIVER=x11 on Linux"); }
        XrGraphicsBindingOpenGLXlibKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
        binding.xDisplay = info.info.x11.display;
        binding.glxContext = glXGetCurrentContext();
        binding.glxDrawable = glXGetCurrentDrawable();
        int config_id = 0;
        if (glXQueryContext(binding.xDisplay, binding.glxContext, GLX_FBCONFIG_ID, &config_id) != 0) {
            throw std::runtime_error("XR GLX framebuffer configuration query failed");
        }
        int screen = 0;
        if (glXQueryContext(binding.xDisplay, binding.glxContext, GLX_SCREEN, &screen) != 0) {
            throw std::runtime_error("XR GLX screen query failed");
        }
        int count = 0;
        const int attributes[]{GLX_FBCONFIG_ID, config_id, None};
        auto* configs = glXChooseFBConfig(binding.xDisplay, screen, attributes, &count);
        if (!configs || count != 1) {
            if (configs) { XFree(configs); }
            throw std::runtime_error("XR GLX framebuffer configuration unavailable");
        }
        binding.glxFBConfig = configs[0];
        XFree(configs);
        int visual = 0;
        if (glXGetFBConfigAttrib(binding.xDisplay, binding.glxFBConfig, GLX_VISUAL_ID, &visual) != 0) {
            throw std::runtime_error("XR GLX visual query failed");
        }
        binding.visualid = std::uint32_t(visual);
#endif
        return runtime.create_session(reinterpret_cast<const XrBaseInStructure&>(binding), scale);
    }
    void upload(const OpenXrTarget& target, const SpatialImage& image) {
        if (!target.texture || target.width != image.width || target.height != image.height ||
                image.pixels.size() != std::size_t(image.width) * image.height) {
            throw std::invalid_argument("XR upload dimensions differ from the acquired swapchain");
        }
        current();
        pixels_.resize(image.pixels.size());
        for (unsigned y = 0; y < image.height; ++y) {
            const auto source = std::size_t(y) * image.width;
            const auto destination = std::size_t(image.height - 1 - y) * image.width;
            for (unsigned x = 0; x < image.width; ++x) {
                auto color = image.pixels[source + x];
                if (target.color_space == OpenXrColorSpace::linear) {
                    color = (color & 0xff000000u) | (std::uint32_t(linear_[(color >> 16) & 255]) << 16) |
                        (std::uint32_t(linear_[(color >> 8) & 255]) << 8) | linear_[color & 255];
                }
                pixels_[destination + x] = color;
            }
        }
        glBindTexture(GL_TEXTURE_2D, target.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.width, image.height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels_.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        glFlush();
        if (glGetError() != GL_NO_ERROR) { throw std::runtime_error("XR OpenGL swapchain upload failed"); }
    }
private:
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window_{nullptr, SDL_DestroyWindow};
    std::unique_ptr<void, decltype(&SDL_GL_DeleteContext)> context_{nullptr, SDL_GL_DeleteContext};
    std::array<std::uint8_t, 256> linear_{};
    std::vector<std::uint32_t> pixels_;
    std::unique_ptr<GlScene> scene_;
};

OpenXrGraphics::OpenXrGraphics(const OpenXrCapabilities& requirements) : impl_(std::make_unique<Impl>(requirements)) {}
OpenXrGraphics::~OpenXrGraphics() = default;
void OpenXrGraphics::activate() { impl_->current(); }
void OpenXrGraphics::release() { impl_->release(); }
std::unique_ptr<OpenXrSession> OpenXrGraphics::create_session(const OpenXrRuntime& runtime, double scale) {
    return impl_->create_session(runtime, scale);
}
void OpenXrGraphics::upload(const OpenXrTarget& target, const SpatialImage& image) { impl_->upload(target, image); }
void OpenXrGraphics::render_eye(const OpenXrTarget& target, const SpatialFrame& frame, const CameraView& view,
                               const EntityPoses& poses, const CockpitImage& source, const std::optional<CameraView>& cockpit) {
    impl_->render_eye(target, frame, view, poses, source, cockpit);
}
}
