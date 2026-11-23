#include "hwshqtb/gui/gui_application_t.h"

#include "sdl_event_dispatcher.h"

#include <SDL3/SDL.h>

#include <string_view>

namespace hwshqtb::gui {
    gui_application_t::gui_application_t() {
        // The platform side of the layer: install SDL's event dispatcher and let it bring video and
        // the event queue up. The paired destructor call is what tears it down again.
        detail::install_sdl_event_dispatcher();
    }

    gui_application_t::~gui_application_t() {
        detail::uninstall_sdl_event_dispatcher();
    }

    std::string_view gui_application_t::video_driver_name() noexcept {
        const char* driver = SDL_GetCurrentVideoDriver();
        return driver == nullptr ? std::string_view{} : std::string_view{ driver };
    }

    bool gui_application_t::is_video_initialized() noexcept {
        return SDL_WasInit(SDL_INIT_VIDEO) != 0;
    }
}
