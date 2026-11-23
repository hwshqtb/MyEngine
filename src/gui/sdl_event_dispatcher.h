#ifndef HWSHQTB__GUI__SDL_EVENT_DISPATCHER_H
#define HWSHQTB__GUI__SDL_EVENT_DISPATCHER_H

/// @file sdl_event_dispatcher.h
/// @brief Internal: the SDL3 implementation of the metaobject event dispatcher.

#include <chrono>

#include <SDL3/SDL.h>

#include "hwshqtb/metaobject/event_dispatcher.h"

namespace hwshqtb::gui::detail {
    /// @brief The SDL3 platform side of the event dispatcher.
    ///
    /// It owns the native message pump: start_platform() initialises video and events — video is
    /// this layer's subsystem — fetch_native() delivers one message, wait_native() reserves the next
    /// one, and stop_platform() shuts SDL down. Nothing is translated here: every message goes
    /// through the installed native event filters, and a quit-type message only sets the dispatcher's
    /// quit flag.
    ///
    /// @note One instance per process (sdl_event_dispatcher()), installed on the thread that runs the
    ///       loop and owns the windows: SDL initialises video for the calling thread.
    class sdl_event_dispatcher_t final: public metaobject::event_dispatcher {
    public:
        sdl_event_dispatcher_t() = default;
        ~sdl_event_dispatcher_t() override = default;

        sdl_event_dispatcher_t(const sdl_event_dispatcher_t&) = delete;
        sdl_event_dispatcher_t& operator=(const sdl_event_dispatcher_t&) = delete;

        /// @brief Whether the SDL side is currently up.
        /// @return true between a successful start_platform() and stop_platform().
        /// @note noexcept.
        bool started() const noexcept;

        /// @brief Start SDL: video plus the event queue (see the class note).
        void start_platform() override;

        /// @brief Shut SDL down (SDL_Quit); idempotent.
        void stop_platform() override;

        /// @brief Deliver one SDL message: the one the wait reserved, or a freshly polled one.
        bool fetch_native() override;

        /// @brief Reserve the next SDL message for fetch_native() (SDL_WaitEventTimeout).
        /// @param timeout The longest wait.
        void wait_native(std::chrono::milliseconds timeout) override;

    private:
        /// @brief Deliver one pumped message: filters first, then the quit-type check.
        /// @param message The SDL message; never nullptr.
        void deliver_native(void* message);

        /// @brief The message a wait reserved, until fetch_native() delivers it.
        SDL_Event _pending{};

        /// @brief Whether @c _pending holds a reserved message.
        bool _has_pending = false;

        /// @brief Whether SDL_Init succeeded and SDL_Quit has not run yet.
        bool _started = false;
    };

    /// @brief The process-wide SDL dispatcher, created on first use and never destroyed.
    /// @return The one instance.
    /// @note Never destroyed for the same reason as every dispatcher: ~object_t may reach it during
    ///       static destruction.
    sdl_event_dispatcher_t& sdl_event_dispatcher() noexcept;

    /// @brief Start the SDL platform side and install it as the calling thread's dispatcher.
    /// @throws std::runtime_error when SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) fails; nothing is
    ///         installed in that case.
    void install_sdl_event_dispatcher();

    /// @brief Stop the SDL platform side and uninstall it from the calling thread.
    /// @note noexcept. A no-op on the platform side when the calling thread runs a different
    ///       dispatcher.
    void uninstall_sdl_event_dispatcher() noexcept;
}

#endif
