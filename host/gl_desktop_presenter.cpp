#include "gl_desktop_presenter.hpp"
#include "sdl_common.hpp"
#include "gl_attributes.hpp"
#include <algorithm>

namespace comanchecd::host {
class GlDesktopPresenter::Impl {
  public:
    Impl(ImageScaling selected_scaling, ViewAspect selected_aspect, DisplayPacing pacing)
        : scaling(selected_scaling), aspect(selected_aspect) {
        GlAttributes restore;
        if (scaling == ImageScaling::profile_default || aspect == ViewAspect::profile_default) {
            throw std::invalid_argument(
                "Resolve the desktop presentation profile before creating its backend");
        }
        if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) != 0 ||
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2) != 0 ||
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) != 0) {
            throw sdl_error("Desktop OpenGL attributes failed");
        }
        window.reset(SDL_CreateWindow("OpenComanche enhanced GPU", SDL_WINDOWPOS_CENTERED,
                                      SDL_WINDOWPOS_CENTERED, aspect == ViewAspect::widescreen ? 1280 : 960,
                                      720,
                                      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI));
        if (!window) {
            throw sdl_error("Desktop SDL2 OpenGL window failed");
        }
        context.reset(SDL_GL_CreateContext(window.get()));
        if (!context) {
            throw sdl_error("Desktop SDL2 OpenGL context failed");
        }
        current();
        // Some drivers do not expose swap control. Match SDL's existing best-effort pacing.
        SDL_GL_SetSwapInterval(pacing == DisplayPacing::vertical_sync ? 1 : 0);
        scene = std::make_unique<GlScene>();
    }
    ~Impl() {
        if (scene) {
            SDL_GL_MakeCurrent(window.get(), context.get());
            scene.reset();
        }
    }
    void current() const {
        if (SDL_GL_MakeCurrent(window.get(), context.get()) != 0) {
            throw sdl_error("Desktop OpenGL context activation failed");
        }
    }
    DesktopLayout layout(ViewAspect selected_aspect, ImageScaling selected_scaling) const {
        int width = 0, height = 0;
        SDL_GL_GetDrawableSize(window.get(), &width, &height);
        return desktop_layout(std::max(1, width), std::max(1, height), selected_aspect, selected_scaling);
    }
    static GraphicsTarget target(const DesktopLayout& layout) {
        return {0, layout.width, layout.height, GraphicsColorSpace::srgb, layout.viewport};
    }
    ImageScaling scaling;
    ViewAspect aspect;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{nullptr, SDL_DestroyWindow};
    std::unique_ptr<void, decltype(&SDL_GL_DeleteContext)> context{nullptr, SDL_GL_DeleteContext};
    std::unique_ptr<GlScene> scene;
};

GlDesktopPresenter::GlDesktopPresenter(ImageScaling scaling, ViewAspect aspect, DisplayPacing pacing)
    : impl_(std::make_unique<Impl>(scaling, aspect, pacing)) {}
GlDesktopPresenter::~GlDesktopPresenter() = default;
SDL_Window* GlDesktopPresenter::window() const {
    return impl_->window.get();
}
std::pair<unsigned, unsigned> GlDesktopPresenter::spatial_size() const {
    const auto layout = impl_->layout(impl_->aspect, impl_->scaling);
    return {layout.width, layout.height};
}
void GlDesktopPresenter::set_pacing(DisplayPacing pacing) {
    impl_->current();
    SDL_GL_SetSwapInterval(pacing == DisplayPacing::vertical_sync ? 1 : 0);
}
void GlDesktopPresenter::present(const CockpitImage& image) {
    impl_->current();
    impl_->scene->image(Impl::target(impl_->layout(ViewAspect::original, impl_->scaling)), image,
                        impl_->scaling);
    SDL_GL_SwapWindow(impl_->window.get());
}
void GlDesktopPresenter::present_scene(const SpatialFrame& frame, const CameraView& view,
                                       const EntityPoses& poses, const CockpitImage& instruments,
                                       const std::optional<CameraView>& cockpit, SceneOverlay overlay,
                                       const ButtonPromptImage* prompts) {
    impl_->current();
    const auto layout = impl_->layout(impl_->aspect, impl_->scaling);
    if (overlay == SceneOverlay::sharp &&
        (layout.width < 2 * cockpit_width || layout.height < 2 * cockpit_height)) {
        overlay = SceneOverlay::nearest;
    }
    impl_->scene->draw(Impl::target(layout), frame, view, poses, instruments, cockpit, overlay);
    if (prompts) { impl_->scene->button_prompts(Impl::target(layout), *prompts); }
    SDL_GL_SwapWindow(impl_->window.get());
}
} // namespace comanchecd::host
