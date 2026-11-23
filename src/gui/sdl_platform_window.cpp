/// @file sdl_platform_window.cpp
/// @brief The SDL3 platform half of a window: native resources, the store/engine hand-out, the capture
///        and the SDL -> semantic step translation.

#include "sdl_platform_window.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "hwshqtb/gui/gui_application_t.h"
#include "hwshqtb/gui/window_t.h"
#include "hwshqtb/metaobject/event_dispatcher.h"
#include "detail/bmp.h"

namespace hwshqtb::gui::detail {
    namespace {
        /// @brief Translate one SDL button index to the backend-independent bit.
        mouse_button_flag mouse_button_flag_of(std::uint8_t button) noexcept {
            switch (button) {
            case SDL_BUTTON_LEFT:
                return mouse_button_flag::LEFT;
            case SDL_BUTTON_MIDDLE:
                return mouse_button_flag::MIDDLE;
            case SDL_BUTTON_RIGHT:
                return mouse_button_flag::RIGHT;
            default:
                return mouse_button_flag::NONE;
            }
        }

        /// @brief Translate one SDL button index to the identity an edge carries.
        /// @note The values of mouse_button_enum and mouse_button_flag are the same bits (see the
        ///       static_asserts in interaction_t.h), so the identity is the flag's value and an unknown
        ///       index becomes 0 (no button), which the window ignores.
        mouse_button_enum mouse_button_enum_of(std::uint8_t button) noexcept {
            return static_cast<mouse_button_enum>(static_cast<std::uint8_t>(mouse_button_flag_of(button)));
        }

        /// @brief Translate an SDL motion state word to the held-button LEVEL.
        /// @note The LEVEL only: a motion step must not manufacture press/release edges, those come
        ///       from the button steps (input_state_t::set_button).
        mouse_button_t mouse_button_t_of(SDL_MouseButtonFlags state) noexcept {
            mouse_button_t buttons{};
            if ((state & SDL_BUTTON_LMASK) != 0) {
                buttons |= mouse_button_flag::LEFT;
            }
            if ((state & SDL_BUTTON_MMASK) != 0) {
                buttons |= mouse_button_flag::MIDDLE;
            }
            if ((state & SDL_BUTTON_RMASK) != 0) {
                buttons |= mouse_button_flag::RIGHT;
            }
            return buttons;
        }

        /// @brief Translate the SDL modifier bits to the gui layer's modifier flags.
        modifier_t modifiers_of(SDL_Keymod modifiers) noexcept {
            modifier_t flags{};
            if ((modifiers & SDL_KMOD_SHIFT) != 0) {
                flags |= modifier_flag::SHIFT;
            }
            if ((modifiers & SDL_KMOD_CTRL) != 0) {
                flags |= modifier_flag::CONTROL;
            }
            if ((modifiers & SDL_KMOD_ALT) != 0) {
                flags |= modifier_flag::ALT;
            }
            if ((modifiers & SDL_KMOD_GUI) != 0) {
                flags |= modifier_flag::GUI;
            }
            return flags;
        }

        /// @brief Map one SDL pointer coordinate (a float) to an int without undefined behaviour.
        /// @param value The coordinate SDL reported for the pointer.
        /// @return @p value truncated towards zero, saturated at the int range; NaN yields 0.
        /// @note The guard MUST sit here, at the entry of the pointer path: converting a float that is
        ///       NaN, infinite or outside the int range to an integer is UNDEFINED BEHAVIOUR in C++,
        ///       not merely an unspecified value. The compiler is therefore allowed to assume the value
        ///       fits and to miscompile every comparison the coordinate later feeds, so clamping at
        ///       each use would be too late and could not repair the damage. SDL reports the pointer as
        ///       a float and a broken driver, a remote session or an injected event can hand over any
        ///       of those values.
        /// @note NaN maps to 0, i.e. a position whose both axes are NaN lands exactly on the origin
        ///       (0, 0), a legal client-area point. A NaN carries no position at all, so no pixel is
        ///       "right" and the choice has to be arbitrary: the origin needs no sign bit (a NaN has
        ///       none to saturate towards), it keeps the result inside the geometry instead of at an
        ///       extreme a hit test would then have to subtract from, and it is stable — every NaN
        ///       event produces the same point. An infinite coordinate keeps its direction and
        ///       saturates to the matching int extreme.
        int pointer_coordinate(float value) noexcept {
            if (std::isnan(value)) {
                return 0;
            }
            // The clamp runs in double: float cannot represent INT_MAX exactly (it rounds up to
            // 2^31), while every float converts to double exactly, so the bound compares truthfully.
            const double bounded = std::clamp(static_cast<double>(value),
                static_cast<double>(std::numeric_limits<int>::min()),
                static_cast<double>(std::numeric_limits<int>::max()));
            return static_cast<int>(bounded);
        }

        /// @brief The pointer position of an SDL mouse event, in WINDOW coordinates.
        /// @param x The event's x, in WINDOW coordinates (SDL_GetWindowSize's space).
        /// @param y The event's y, in WINDOW coordinates.
        /// @return The point in the platform's window space. Converting it to the pixel space is the
        ///         WINDOW's job (window_t owns the two extents that conversion divides), so the platform
        ///         only reports where the pointer is in its own units.
        /// @note Both axes go through pointer_coordinate(), not a plain cast: a float outside the int
        ///       range (or NaN) makes the cast undefined behaviour, and it has to be guarded before the
        ///       value exists as an int at all.
        point_t window_point_of(float x, float y) noexcept {
            return point_t{ pointer_coordinate(x), pointer_coordinate(y) };
        }
    }

    sdl_platform_window_t::sdl_platform_window_t(std::string_view title, extent_t size) {
        if (!::hwshqtb::gui::gui_application_t::is_video_initialized()) {
            throw std::runtime_error(
                "window_t: the video subsystem is not initialised; construct a gui_application_t first");
        }
        if (size.isEmpty()) {
            throw std::invalid_argument("window_t: the extent must be positive");
        }
        const std::string title_copy{ title };
        // Hidden by default (a widget appears on show()) and resizable, like a normal top-level window.
        window = SDL_CreateWindow(title_copy.c_str(), size.width(), size.height(),
                                  SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
        if (window == nullptr) {
            throw std::runtime_error(std::string("window_t: SDL_CreateWindow failed: ") + SDL_GetError());
        }
        window_id = SDL_GetWindowID(window);
        // The device's backend: the same SDL paint engine image_t uses, over this window's renderer.
        renderer = SDL_CreateRenderer(window, nullptr);
        if (renderer == nullptr) {
            // The destructor does not run for an object whose constructor threw: the native window has to
            // be released here, or it would outlive every handle to it.
            const std::string error = SDL_GetError();
            SDL_DestroyWindow(window);
            window = nullptr;
            throw std::runtime_error(std::string("window_t: SDL_CreateRenderer failed: ") + error);
        }
        // Created once the render target exists, so paint_engine() is a pure query from now on.
        engine = std::make_unique<detail::sdl_paint_engine_t>(renderer);
        // The persistent surface is created here (no pixels yet: the first resize() allocates them) so
        // backing_store() is a pure query too. It wraps the renderer, and the member order makes it die
        // before the renderer does.
        backing = std::make_unique<backing_store_t>(renderer);
    }

    sdl_platform_window_t::~sdl_platform_window_t() {
#if defined(_WIN32)
        // Stop the modal repair timer and stop dispatching to this implementation BEFORE the window
        // starts dying: the hook registry holds a raw pointer.
        leave_size_move();
        sdl_platform_window_t::unregister_windows_hook(hwnd);
        hwnd = nullptr;
#endif
        // Stop listening before anything starts dying: the dispatcher holds a raw pointer.
        if (filter_installed) {
            ::hwshqtb::metaobject::event_dispatcher::instance().remove_native_event_filter_t(this);
            filter_installed = false;
        }
        owner = nullptr;
        // The engine wraps the renderer, and the backing store was created FROM the renderer: both must
        // go before SDL_DestroyRenderer, then the window itself.
        backing.reset();
        engine.reset();
        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
            renderer = nullptr;
        }
        if (window != nullptr) {
            // Do not leave a platform input method or screen keyboard attached to a window that is about
            // to disappear.
            stop_text_input();
            (void)SDL_HideWindow(window);
            SDL_DestroyWindow(window);
            window = nullptr;
        }
    }

    void sdl_platform_window_t::attach(::hwshqtb::gui::window_t& window_owner) {
        owner = &window_owner;
#if defined(_WIN32)
        // One process-wide SDL message hook dispatches by HWND; this window only registers itself.
        // The hook drives the Win32 size-move modal loop (the pump is blocked there).
        hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        sdl_platform_window_t::register_windows_hook(hwnd, this);
#endif
        // A platform may hand a created window the keyboard focus immediately (a hidden window normally
        // does not): follow whatever the real focus is instead of assuming it arrives as an event later.
        sync_text_input_with_focus();
        ::hwshqtb::metaobject::event_dispatcher::instance().install_native_event_filter_t(this);
        filter_installed = true;
    }

    bool sdl_platform_window_t::native_event_filter_t_proc(void* message, long* result) {
        if (result != nullptr) {
            *result = 0;
        }
        if (owner == nullptr || message == nullptr) {
            return false;
        }
        return handle_native_event(*static_cast<SDL_Event*>(message));
    }

    void sdl_platform_window_t::remember_pointer(point_t position, mouse_button_t buttons) noexcept {
        last_pointer = position;
        last_buttons = buttons;
    }

    bool sdl_platform_window_t::handle_native_event(const SDL_Event& event) {
        if (owner == nullptr) {
            return false;
        }
        // Only events stamped with this window's id are acted on: SDL correlates mouse and key focus
        // that way, and a window must never feed another window's events into its own state.
        // Nothing is interpreted here beyond that: every case fills in ONE interaction_t step (or one
        // lifecycle report) and the window owns the state the step changes.
        switch (event.type) {
        case SDL_EVENT_WINDOW_EXPOSED:
            if (event.window.windowID == window_id) {
                owner->handle_expose();
            }
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (event.window.windowID == window_id) {
                owner->handle_resize();
            }
            break;
        case SDL_EVENT_WINDOW_MOVED:
            if (event.window.windowID == window_id) {
                owner->handle_move(static_cast<int>(event.window.data1),
                    static_cast<int>(event.window.data2));
            }
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (event.window.windowID == window_id) {
                owner->handle_close();
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (event.motion.windowID == window_id) {
                const point_t position = window_point_of(event.motion.x, event.motion.y);
                const mouse_button_t buttons = mouse_button_t_of(event.motion.state);
                remember_pointer(position, buttons);
                interaction_t step;
                step.mouse = position;
                step.buttons = buttons;
                step.modifiers = modifier_bits(modifiers_of(SDL_GetModState()));
                owner->handle_input(step);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.windowID == window_id) {
                const point_t position = window_point_of(event.button.x, event.button.y);
                const mouse_button_flag flag = mouse_button_flag_of(event.button.button);
                // The step carries the new LEVEL: the button report is the authority for the button it
                // names, so the cached level is updated here and every later position-less step reports
                // it back.
                mouse_button_t buttons = last_buttons;
                if (flag != mouse_button_flag::NONE) {
                    if (event.button.down) {
                        buttons |= flag;
                    }
                    else {
                        buttons &= ~mouse_button_t{ flag };
                    }
                }
                remember_pointer(position, buttons);
                interaction_t step;
                step.mouse = position;
                step.buttons = buttons;
                step.button = mouse_button_enum_of(event.button.button);
                step.mouse_down = event.button.down;
                step.mouse_pressed = event.button.down;
                step.mouse_released = !event.button.down;
                step.modifiers = modifier_bits(modifiers_of(SDL_GetModState()));
                owner->handle_input(step);
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (event.wheel.windowID == window_id) {
                // A wheel event has no position of its own: the step reports the pointer the window
                // already has, so the level never jumps to the origin.
                interaction_t step;
                step.mouse = last_pointer;
                step.buttons = last_buttons;
                step.wheel = point_t{ event.wheel.integer_x, event.wheel.integer_y };
                step.modifiers = modifier_bits(modifiers_of(SDL_GetModState()));
                owner->handle_input(step);
            }
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (event.text.windowID == window_id && event.text.text != nullptr) {
                interaction_t step;
                step.mouse = last_pointer;
                step.buttons = last_buttons;
                step.text_input = event.text.text;
                step.modifiers = modifier_bits(modifiers_of(SDL_GetModState()));
                owner->handle_input(step);
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.windowID == window_id) {
                interaction_t step;
                step.mouse = last_pointer;
                step.buttons = last_buttons;
                step.key = static_cast<int>(event.key.key);
                step.key_pressed = true;
                step.modifiers = modifier_bits(modifiers_of(SDL_GetModState()));
                owner->handle_input(step);
            }
            break;
        case SDL_EVENT_KEY_UP:
            if (event.key.windowID == window_id) {
                interaction_t step;
                step.mouse = last_pointer;
                step.buttons = last_buttons;
                step.key = static_cast<int>(event.key.key);
                step.key_released = true;
                step.modifiers = modifier_bits(modifiers_of(SDL_GetModState()));
                owner->handle_input(step);
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            if (event.window.windowID == window_id) {
                owner->handle_focus(true);
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (event.window.windowID == window_id) {
                owner->handle_focus(false);
            }
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            if (event.window.windowID == window_id) {
                // The pointer session of this window has ended (the mouse left the client area): the
                // host owns that state and drops it on this report.
                owner->handle_pointer_leave();
            }
            break;
        default:
            break;
        }
        return false;
    }

    void sdl_platform_window_t::show() {
        if (!SDL_ShowWindow(window)) {
            throw std::runtime_error(std::string("window_t::show: SDL_ShowWindow failed: ") + SDL_GetError());
        }
        (void)SDL_RaiseWindow(window);
        // Shown and possibly focused already: SDL sets SDL_WINDOW_INPUT_FOCUS when it grants the keyboard
        // focus, so text input can start now instead of waiting for the queued focus report.
        sync_text_input_with_focus();
    }

    void sdl_platform_window_t::hide() {
        if (!SDL_HideWindow(window)) {
            throw std::runtime_error(std::string("window_t::hide: SDL_HideWindow failed: ") + SDL_GetError());
        }
        // A hidden window cannot own the keyboard, so it must not keep text input (an input method or a
        // screen keyboard) alive. Stopping after a successful hide leaves the state untouched when the
        // hide failed and the window is still visible.
        stop_text_input();
    }

    bool sdl_platform_window_t::is_visible() const noexcept {
        return (SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN) == 0;
    }

    void sdl_platform_window_t::set_title(std::string_view title) {
        const std::string title_copy{ title };
        if (!SDL_SetWindowTitle(window, title_copy.c_str())) {
            throw std::runtime_error(std::string("window_t::set_title: SDL_SetWindowTitle failed: ")
                                     + SDL_GetError());
        }
    }

    void sdl_platform_window_t::set_minimum_size(::hwshqtb::gui::extent_t size) noexcept {
        if (window == nullptr) {
            return;
        }
        // The window system enforces this, which is what stops the user dragging a tree below the size its
        // own minimum hints compute. A refused hint is not an error: the window simply keeps no floor.
        (void)SDL_SetWindowMinimumSize(window, size.width() > 0 ? size.width() : 0,
                                       size.height() > 0 ? size.height() : 0);
    }
    std::uint32_t sdl_platform_window_t::native_id() const noexcept {
        return static_cast<std::uint32_t>(window_id);
    }

    extent_t sdl_platform_window_t::render_size() const noexcept {
        int width = 0;
        int height = 0;
        if (renderer != nullptr && SDL_GetRenderOutputSize(renderer, &width, &height)) {
            return extent_t{ width, height };
        }
        if (window != nullptr) {
            (void)SDL_GetWindowSize(window, &width, &height);
        }
        return extent_t{ width, height };
    }

    extent_t sdl_platform_window_t::device_size() const noexcept {
        return render_size();
    }

    extent_t sdl_platform_window_t::pixel_size() const noexcept {
        int width = 0;
        int height = 0;
        if (window != nullptr && SDL_GetWindowSizeInPixels(window, &width, &height)) {
            return extent_t{ width, height };
        }
        return render_size();
    }

    rect_t sdl_platform_window_t::window_geometry() const noexcept {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        if (window != nullptr) {
            (void)SDL_GetWindowPosition(window, &x, &y);
            (void)SDL_GetWindowSize(window, &width, &height);
        }
        return rect_t{ point_t{ x, y }, extent_t{ width, height } };
    }

    paint_engine_t* sdl_platform_window_t::paint_engine() noexcept {
        return engine.get();
    }

    backing_store_t* sdl_platform_window_t::backing_store() noexcept {
        return backing.get();
    }

    void sdl_platform_window_t::present() {
        // A minimized or hidden window may refuse to present; the next frame tries again.
        (void)SDL_RenderPresent(renderer);
    }

    void sdl_platform_window_t::write_frame(std::string_view path) {
        // The caller owns the target: window_t reads the painted frame while the store is the render
        // target, and reads the window target before presenting a direct-to-window frame.
        SDL_Surface* const raw = SDL_RenderReadPixels(renderer, nullptr);
        if (raw == nullptr) {
            throw std::runtime_error(std::string("window_t: SDL_RenderReadPixels failed: ")
                                     + SDL_GetError());
        }
        // Normalise to 24-bit BGR so the file is byte-identical whichever renderer produced the frame: a
        // window back buffer reads back as 32-bit ARGB, the dummy/offscreen drivers as 24-bit RGB, and
        // SDL_SaveBMP writes the surface's own format.
        write_bmp(raw, path, "window_t");
    }

    bool sdl_platform_window_t::has_keyboard_focus() const noexcept {
        return (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    }

    void sdl_platform_window_t::start_text_input() {
        // Idempotent: SDL_StartTextInput does nothing while the input is already active. The return value
        // is ignored on purpose — a rejected start leaves the window without text input, which the next
        // focus change retries.
        if (window != nullptr) {
            (void)SDL_StartTextInput(window);
        }
    }

    void sdl_platform_window_t::stop_text_input() noexcept {
        // Idempotent: SDL_StopTextInput does nothing while the input is already inactive.
        if (window != nullptr) {
            (void)SDL_StopTextInput(window);
        }
    }

    void sdl_platform_window_t::sync_text_input_with_focus() {
        if (window == nullptr) {
            return;
        }
        if (has_keyboard_focus()) {
            start_text_input();
        }
        else {
            stop_text_input();
        }
    }

    void sdl_platform_window_t::set_text_input(bool enabled) noexcept {
        if (enabled) {
            start_text_input();
        }
        else {
            stop_text_input();
        }
    }

    bool sdl_platform_window_t::defer_repaint() noexcept {
#if defined(_WIN32)
        if (in_size_move) {
            // The Win32 modal loop does not run our pump: a posted PAINT would wait until the drag ends.
            // Mark the frame dirty; modal_frame() paints it from the 16 ms timer.
            modal_dirty = true;
            return true;
        }
#endif
        return false;
    }

#if defined(_WIN32)
    namespace {
        /// @brief Process-wide HWND -> sdl_platform_window_t registry for the one SDL message hook.
        struct windows_hook_registry_t {
            /// @brief Guards @c owners and @c installed.
            std::mutex mutex;
            /// @brief Registered windows; the SDL hook is process-wide, the target is per HWND.
            std::vector<std::pair<HWND, void*>> owners;
            /// @brief Whether the SDL hook has been installed (installed once, never removed).
            bool installed = false;
        };

        /// @brief The process-wide registry (function-local so it is created on first use).
        windows_hook_registry_t& windows_hook_registry() noexcept {
            static windows_hook_registry_t registry;
            return registry;
        }
    }

    void sdl_platform_window_t::register_windows_hook(HWND hwnd, sdl_platform_window_t* owner_window) {
        if (hwnd == nullptr || owner_window == nullptr) {
            return;
        }
        auto& registry = windows_hook_registry();
        std::scoped_lock lock(registry.mutex);
        registry.owners.emplace_back(hwnd, owner_window);
        if (!registry.installed) {
            SDL_SetWindowsMessageHook(&windows_message_hook, nullptr);
            registry.installed = true;
        }
    }

    void sdl_platform_window_t::unregister_windows_hook(HWND hwnd) {
        if (hwnd == nullptr) {
            return;
        }
        auto& registry = windows_hook_registry();
        std::scoped_lock lock(registry.mutex);
        std::erase_if(registry.owners, [hwnd](const auto& entry) {
            return entry.first == hwnd;
        });
    }

    bool SDLCALL sdl_platform_window_t::windows_message_hook(void* userdata, MSG* msg) {
        (void)userdata;
        if (msg == nullptr) {
            return true;
        }
        sdl_platform_window_t* owner_window = nullptr;
        {
            auto& registry = windows_hook_registry();
            std::scoped_lock lock(registry.mutex);
            for (const auto& entry : registry.owners) {
                if (entry.first == msg->hwnd) {
                    owner_window = static_cast<sdl_platform_window_t*>(entry.second);
                    break;
                }
            }
        }
        if (owner_window == nullptr) {
            return true; // not one of our windows: let SDL process it normally
        }
        switch (msg->message) {
        case WM_ENTERSIZEMOVE:
            owner_window->enter_size_move();
            break;
        case WM_TIMER:
            if (msg->wParam == static_cast<WPARAM>(::hwshqtb::gui::window_t::size_move_timer_id)) {
                owner_window->modal_frame();
            }
            break;
        case WM_EXITSIZEMOVE:
            owner_window->leave_size_move();
            break;
        default:
            break;
        }
        return true; // never swallow a message: SDL/Windows still have to process it
    }

    void sdl_platform_window_t::enter_size_move() {
        if (in_size_move) {
            return;
        }
        in_size_move = true;
        modal_dirty = true;
        // The client area is moving: the repair frames are full frames, so the surface is never left
        // showing a stale band while Windows drags the window. handle_expose() IS that request: it
        // damages the whole client area, asks for the frame and queues the expose hook — the frame it
        // schedules is this loop's repair frame, because update() goes through defer_repaint() here.
        if (owner != nullptr) {
            owner->handle_expose();
        }
        if (hwnd != nullptr) {
            size_move_timer_active = SetTimer(hwnd,
                static_cast<UINT_PTR>(::hwshqtb::gui::window_t::size_move_timer_id), 16, nullptr) != 0;
        }
    }

    void sdl_platform_window_t::leave_size_move() {
        if (hwnd != nullptr && size_move_timer_active) {
            (void)KillTimer(hwnd, static_cast<UINT_PTR>(::hwshqtb::gui::window_t::size_move_timer_id));
        }
        size_move_timer_active = false;
        in_size_move = false;
        modal_dirty = false;
    }

    void sdl_platform_window_t::modal_frame() {
        if (!in_size_move || owner == nullptr) {
            return;
        }
        // (1) Sync the device size: handle_resize(true) compares it with the window's bookkeeping, calls
        //     resize_event() DIRECTLY when it changed (there is no pump inside the modal loop) and asks
        //     for a repaint — which, with this loop marked as the frame owner, lands back in
        //     defer_repaint() and sets modal_dirty. A backend whose output lags the real pixel size is
        //     therefore re-synced and re-painted on the next tick.
        owner->handle_resize(true);
        if (!modal_dirty) {
            return;
        }
        modal_dirty = false;
        // (2) Repair frame. repair_frame() must NOT clear the input edges: the SDL input events are still
        //     queued and have not been dispatched, so clearing them here would drop clicks.
        owner->repair_frame();
    }
#endif
}
