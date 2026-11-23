#ifndef HWSHQTB__GUI__GUI_APPLICATION_H
#define HWSHQTB__GUI__GUI_APPLICATION_H

/// @file gui_application_t.h
/// @brief gui_application_t: the lifetime of the GUI layer's platform side.

#include <string_view>

#include "hwshqtb/gui/export.h"

namespace hwshqtb::gui {
    /// @brief Owns the platform side — video and the native event pump — for the lifetime of the
    ///        object (RAII).
    ///
    /// @par What it owns
    /// core_application carries the object model and the event semantics and is platform-neutral.
    /// This object supplies the platform: it installs the SDL implementation of the event dispatcher
    /// on the calling thread and starts it, which initialises video and the event queue together
    /// (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)). The destructor stops the platform side
    /// (SDL_Quit) and uninstalls the dispatcher again.
    ///
    /// @par Required ordering
    /// @code
    /// int main(int argc, char** argv) {
    ///     core_application app{ argc, argv };          // the object model; outlives everything below
    ///     {
    ///         gui_application_t gui;                   // the platform; destroyed BEFORE app
    ///         window_t window{ "hello", { 320, 120 } };
    ///     }
    ///     return app.exec();                            // safe: the platform side is shut down
    /// }
    /// @endcode
    /// Destroy every window_t before the gui_application_t (the platform shutdown tears video down
    /// under live windows) and the gui_application_t before the core_application (the platform side
    /// must be gone while the object model still exists). The demo follows exactly this nesting.
    ///
    /// @par Main thread
    /// SDL initialises video on the calling thread and expects to use it from there, and the
    /// dispatcher is installed on the calling thread only; construct this object on the thread that
    /// creates the windows and runs the event loop.
    ///
    /// @par What it does not do
    /// It does not open windows, create renderers or run an event loop — window_t and
    /// core_application::exec() do that. There is no application-level window list.
    class HWSHQTB_GUI_EXPORT gui_application_t {
    public:
        /// @brief Install and start the SDL event dispatcher (video plus the event queue).
        /// @throws std::runtime_error when SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) fails (the
        ///         message carries SDL_GetError()); the object is not constructed and nothing stays
        ///         installed.
        gui_application_t();

        /// @brief Stop the platform side (SDL_Quit) and uninstall the SDL dispatcher.
        ~gui_application_t();

        gui_application_t(const gui_application_t&) = delete;
        gui_application_t& operator=(const gui_application_t&) = delete;
        gui_application_t(gui_application_t&&) = delete;
        gui_application_t& operator=(gui_application_t&&) = delete;

        /// @brief The video driver in use, on demand.
        /// @return The driver name (for example "windows"); empty when video is not initialised.
        /// @note The returned view points at SDL's own storage and stays valid while video is up.
        /// @note noexcept.
        static std::string_view video_driver_name() noexcept;

        /// @brief Whether the video subsystem is currently initialised.
        /// @note noexcept. Reads SDL_WasInit(SDL_INIT_VIDEO), so it also answers for video brought up
        ///       (or shut down) by another owner.
        static bool is_video_initialized() noexcept;
    };
}

#endif
