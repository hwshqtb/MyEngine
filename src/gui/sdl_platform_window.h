#ifndef HWSHQTB__GUI__SDL_PLATFORM_WINDOW_H
#define HWSHQTB__GUI__SDL_PLATFORM_WINDOW_H

/// @file sdl_platform_window.h
/// @brief Internal: the SDL3 implementation of the platform half of a window.

#include <cstdint>
#include <memory>
#include <string_view>

#include <SDL3/SDL.h>

#include "hwshqtb/gui/backing_store_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/input_state_t.h"
#include "hwshqtb/gui/platform_window_t.h"
#include "hwshqtb/metaobject/native_event_filter_t.h"
#include "sdl_paint_engine.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// <windows.h> defines CONST as a macro (windef.h), and that collides with the qualifier_flag::CONST
// enumerator of the metaobject headers, which every translation unit that includes this one also
// includes after it. The project never uses the Win32 spelling, so it is dropped here.
#ifdef CONST
#undef CONST
#endif
#endif

namespace hwshqtb::gui {
    class window_t;
}

namespace hwshqtb::gui::detail {
    /// @brief The SDL3 platform half of a window: the native window, its renderer, the backing store
    ///        and the SDL event source.
    ///
    /// It owns everything the SDL backend needs and nothing of the frame policy: the damage, the pass
    /// count, the presentation, the scroll accumulation, the input state and the capture decisions stay
    /// in window_t. Its job is to create/lose the native resources, to hand out the store and the
    /// engine, to translate one SDL event into one backend-independent step on its owner, and to
    /// answer the platform questions (sizes, id, focus, visibility).
    ///
    /// @note One instance per window, owned by that window (std::unique_ptr<platform_window_t>). The
    ///       native event filter is installed by attach() and removed by the destructor.
    /// @note The Win32 size-move modal loop lives here, because it is a platform detail: while Windows
    ///       drags a window its own modal loop blocks the SDL pump, so this object keeps a timer that
    ///       asks the owner for a repair frame (repair_frame()) without consuming input edges.
    class sdl_platform_window_t final: public ::hwshqtb::gui::platform_window_t,
                                       private ::hwshqtb::metaobject::native_event_filter_t {
    public:
        /// @brief Create the native window, its renderer, the paint engine and the backing store.
        /// @param title The window title.
        /// @param size The client area in window coordinates; both sides must be positive.
        /// @throws std::invalid_argument when @p size is empty.
        /// @throws std::runtime_error when video is not initialised, or SDL cannot create the
        ///         window/renderer (the message carries SDL_GetError()).
        sdl_platform_window_t(std::string_view title, extent_t size);

        /// @brief Remove the native event filter and the Win32 hook, then destroy the store, the
        ///        engine, the renderer and the window (in that order).
        ~sdl_platform_window_t() override;

        sdl_platform_window_t(const sdl_platform_window_t&) = delete;
        sdl_platform_window_t& operator=(const sdl_platform_window_t&) = delete;

        void show() override;
        void hide() override;
        bool is_visible() const noexcept override;
        void set_title(std::string_view title) override;
        void set_minimum_size(::hwshqtb::gui::extent_t size) noexcept override;
        std::uint32_t native_id() const noexcept override;

        extent_t device_size() const noexcept override;
        extent_t pixel_size() const noexcept override;
        rect_t window_geometry() const noexcept override;

        paint_engine_t* paint_engine() noexcept override;
        backing_store_t* backing_store() noexcept override;
        void present() override;
        void write_frame(std::string_view path) override;

        void set_text_input(bool enabled) noexcept override;
        void attach(::hwshqtb::gui::window_t& window) override;
        bool defer_repaint() noexcept override;

    private:
        /// @brief Whether this window currently owns the keyboard focus.
        /// @return true when SDL reports SDL_WINDOW_INPUT_FOCUS for it.
        /// @note This is NOT part of platform_window_t: nothing above the platform can answer the
        ///       question, and window_t never asks. Only sync_text_input_with_focus() uses it, to
        ///       decide whether to start or stop the platform text input.
        /// @note noexcept.
        bool has_keyboard_focus() const noexcept;

    private:
        /// @brief Interpret one SDL event for this window.
        /// @return Whether the event was consumed; always false (the filter observes, it does not
        ///         swallow, so a quit event keeps working).
        bool native_event_filter_t_proc(void* message, long* result) override;

        /// @brief The SDL -> semantic translation: correlate by window id, then report to the owner.
        /// @param event The SDL event; only the ones this window owns are acted on.
        /// @return Always false (observe only).
        bool handle_native_event(const SDL_Event& event);

        /// @brief The renderer output size (what the device actually draws into).
        extent_t render_size() const noexcept;

        /// @brief Remember the pointer of a motion/button report: the steps that carry no position
        ///        (wheel, key, text) report this one back, so the window's pointer level never jumps.
        /// @param position The pointer, in WINDOW coordinates.
        /// @param buttons The held-button level of that report.
        void remember_pointer(point_t position, mouse_button_t buttons) noexcept;

        /// @brief Enable the platform text input (committed text and composition events).
        void start_text_input();

        /// @brief Disable the platform text input and hide any screen keyboard it showed.
        void stop_text_input() noexcept;

        /// @brief Make the platform text input follow the window's current keyboard focus.
        void sync_text_input_with_focus();

        /// @brief The window object every platform report goes to; nullptr until attach().
        ::hwshqtb::gui::window_t* owner = nullptr;
        /// @brief The owned SDL window.
        SDL_Window* window = nullptr;
        /// @brief The owned SDL renderer over that window.
        SDL_Renderer* renderer = nullptr;
        /// @brief The SDL window id, used to correlate incoming native events with this window.
        SDL_WindowID window_id = 0;
        /// @brief The device's engine over @c renderer; created with the render target.
        std::unique_ptr<detail::sdl_paint_engine_t> engine;
        /// @brief The persistent surface painted between presents; created with the renderer.
        /// @note Handed out through backing_store(); window_t owns the decision to use it and the
        ///       store itself dies before the renderer it was created from (see the destructor).
        std::unique_ptr<backing_store_t> backing;
        /// @brief Whether this object is currently installed on the dispatcher as an event filter.
        bool filter_installed = false;
        /// @brief The pointer of the last pointer report, in WINDOW coordinates.
        point_t last_pointer{};
        /// @brief The held-button level of the last pointer report.
        mouse_button_t last_buttons{};

#if defined(_WIN32)
        /// @brief The native window handle (SDL's Win32 window property), or nullptr.
        HWND hwnd = nullptr;
        /// @brief True between WM_ENTERSIZEMOVE and WM_EXITSIZEMOVE.
        bool in_size_move = false;
        /// @brief Whether the 16 ms repair timer is currently installed.
        bool size_move_timer_active = false;
        /// @brief A repair frame is needed in the size-move modal loop.
        bool modal_dirty = false;

        /// @brief Install the process-wide SDL Windows message hook for @p hwnd.
        /// @param hwnd The window handle; nullptr is ignored.
        /// @param owner_window The implementation the hook dispatches to.
        static void register_windows_hook(HWND hwnd, sdl_platform_window_t* owner_window);
        /// @brief Remove @p hwnd from the hook registry (called before the window is destroyed).
        static void unregister_windows_hook(HWND hwnd);
        /// @brief SDL's Windows message hook: dispatch by HWND to the owning implementation.
        static bool SDLCALL windows_message_hook(void* userdata, MSG* msg);

        /// @brief WM_ENTERSIZEMOVE: mark the modal loop and start the 16 ms repair timer.
        void enter_size_move();
        /// @brief WM_EXITSIZEMOVE: stop the timer and clear the modal flags.
        void leave_size_move();
        /// @brief WM_TIMER: sync the real size, then paint one repair frame if the window is dirty.
        void modal_frame();
#endif
    };
}

#endif
