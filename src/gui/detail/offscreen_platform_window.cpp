/// @file detail/offscreen_platform_window.cpp
/// @brief The platform_window_t implementation with no native window behind it.

#include "offscreen_platform_window.h"

#include <stdexcept>
#include <string>
#include <utility>

#include <SDL3/SDL.h>

#include "bmp.h"

namespace hwshqtb::gui::detail {
    offscreen_platform_window::offscreen_platform_window(extent_t size):
        _surface(size) {
        // Created here, once, so backing_store() is a pure query: the store is the persistent surface
        // every frame paints into, and the pixels it flushes land in _surface (the renderer's default
        // target is exactly that surface, because it was created over it).
        _store = std::make_unique<backing_store_t>(_surface.target());
    }

    offscreen_platform_window::~offscreen_platform_window() {
        // The store holds a texture of the renderer that _surface owns: it has to go first.
        _store.reset();
    }

    void offscreen_platform_window::show() {
        // There is no window to show: the flag is the whole state, and window_t's frame path (which
        // paints and presents) is what actually produces the pixels.
        _visible = true;
    }

    void offscreen_platform_window::hide() {
        _visible = false;
    }

    bool offscreen_platform_window::is_visible() const noexcept {
        return _visible;
    }

    void offscreen_platform_window::set_title(std::string_view title) {
        // Nothing displays a title; keeping it makes a debugger's view of this object readable.
        _title = std::string(title);
    }

    void offscreen_platform_window::set_minimum_size(::hwshqtb::gui::extent_t) noexcept {
        // An off-screen device has no window system to enforce a floor: the frame policy simply paints
        // whatever extent it is given.
    }
    std::uint32_t offscreen_platform_window::native_id() const noexcept {
        // There is no native window, so this window is part of no native event stream: 0 is the
        // documented "belongs to nothing".
        return 0;
    }

    extent_t offscreen_platform_window::device_size() const noexcept {
        return _surface.size();
    }

    extent_t offscreen_platform_window::pixel_size() const noexcept {
        // One surface is both the device output and the client area: the two extents are the same.
        return _surface.size();
    }

    rect_t offscreen_platform_window::window_geometry() const noexcept {
        // An offscreen buffer is not placed on any screen, and rect_t already carries the way to say
        // that: a NON-POSITIVE size, i.e. an empty rectangle. The position is therefore meaningless too
        // — reporting (0, 0) would claim a screen position the window does not have, and a sentinel
        // position would collide with a legal one (a multi-monitor desktop can place a window at -1, -1
        // while no real client area is ever negative).
        return rect_t{ point_t{}, extent_t{ -1, -1 } };
    }

    paint_engine_t* offscreen_platform_window::paint_engine() noexcept {
        return _surface.paint_engine();
    }

    backing_store_t* offscreen_platform_window::backing_store() noexcept {
        return _store.get();
    }

    void offscreen_platform_window::present() {
        // A software renderer over a surface has no window, and SDL's SW_RenderPresent returns false
        // exactly for that case (external/SDL3/src/render/software/SDL_render_sw.c). The pixels are
        // already in the surface; nothing has to be (or can be) presented.
    }

    void offscreen_platform_window::write_frame(std::string_view path) {
        // The caller owns the target: window_t reads the painted frame while the store is the render
        // target, and reads this surface otherwise. Either way SDL_RenderReadPixels returns the frame.
        SDL_Surface* const raw = SDL_RenderReadPixels(static_cast<SDL_Renderer*>(_surface.target()), nullptr);
        if (raw == nullptr) {
            throw std::runtime_error(std::string("window_t: SDL_RenderReadPixels failed: ")
                                     + SDL_GetError());
        }
        write_bmp(raw, path, "window_t");
    }

    void offscreen_platform_window::set_text_input(bool enabled) noexcept {
        // There is no event source and no input method: nothing can produce committed text here. The
        // window still calls this whenever the focus changes, so the answer has to be "accepted and
        // ignored" rather than an error.
        (void)enabled;
    }

    void offscreen_platform_window::attach(::hwshqtb::gui::window_t& window) {
        // No native messages exist, so there is no filter to install and no text input to follow: this
        // window is driven entirely through window_t's public report entries (handle_input() etc.).
        // That is exactly what makes it a useful probe of the interface.
        _owner = &window;
    }

    bool offscreen_platform_window::defer_repaint() noexcept {
        // Nothing ever blocks the frame schedule: every update() goes through the normal event queue.
        return false;
    }

    color_t offscreen_platform_window::pixel(int x, int y) const {
        return _surface.pixel(x, y);
    }
}
