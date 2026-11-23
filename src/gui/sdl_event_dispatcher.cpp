/// @file sdl_event_dispatcher.cpp
/// @brief The SDL3 implementation of the event dispatcher (the platform boundary of the gui layer).

#include "sdl_event_dispatcher.h"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

#include "hwshqtb/metaobject/log.h"

namespace hwshqtb::gui::detail {
    sdl_event_dispatcher_t& sdl_event_dispatcher() noexcept {
        // Never destroyed: a dispatcher pointer may outlive every window (see event_dispatcher).
        static sdl_event_dispatcher_t* dispatcher = new sdl_event_dispatcher_t();
        return *dispatcher;
    }

    bool sdl_event_dispatcher_t::started() const noexcept {
        return _started;
    }

    void sdl_event_dispatcher_t::start_platform() {
        if (_started) {
            return;
        }
        // Video belongs to this layer, so the platform start brings it up together with the event
        // queue the dispatcher pumps.
        _started = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        if (!_started) {
            // Everything goes through the logging facade so that callers can silence or redirect it.
            HWSHQTB_LOG_RAW(metaobject::log_events(), metaobject::log_level_enum::CRITICAL,
                std::string("SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) failed: ") + SDL_GetError());
        }
    }

    void sdl_event_dispatcher_t::stop_platform() {
        if (_started) {
            // A message reserved for the next fetch belongs to the SDL session that is ending now.
            _has_pending = false;
            SDL_Quit();
            _started = false;
        }
    }

    bool sdl_event_dispatcher_t::fetch_native() {
        if (!_started) {
            return false;
        }
        // A message the blocking wait already reserved is delivered here: the wait only reserves,
        // every delivery goes through this function.
        if (_has_pending) {
            _has_pending = false;
            deliver_native(&_pending);
            return true;
        }
        SDL_Event message{};
        if (!SDL_PollEvent(&message)) {
            return false;
        }
        deliver_native(&message);
        return true;
    }

    void sdl_event_dispatcher_t::wait_native(std::chrono::milliseconds timeout) {
        if (!_started || _has_pending) {
            return;
        }
        // Reserve the message for the next fetch_native() instead of delivering it here: the
        // delivery path must stay unique.
        if (SDL_WaitEventTimeout(&_pending, static_cast<int>(timeout.count()))) {
            _has_pending = true;
        }
    }

    void sdl_event_dispatcher_t::deliver_native(void* message) {
        // The installed native filters get the raw message first. When one of them claims it, the
        // message is neither interpreted as an event_t nor dispatched — the quit check below is part
        // of that interpretation, so a claimed quit never registers a request.
        if (filter_native_message(message)) {
            return;
        }
        if (static_cast<SDL_Event*>(message)->type == SDL_EVENT_QUIT) {
            request_quit();
        }
        // Other platform messages have no consumer at this layer: the platform half of a window
        // (gui::detail::sdl_platform_window_t) translates the ones it knows in its own native filter,
        // and the rest are dropped.
    }

    void install_sdl_event_dispatcher() {
        sdl_event_dispatcher_t& dispatcher = sdl_event_dispatcher();
        dispatcher.start_platform();
        if (!dispatcher.started()) {
            throw std::runtime_error(std::string("gui_application_t: SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) failed: ")
                                     + SDL_GetError());
        }
        metaobject::event_dispatcher::set_instance(&dispatcher);
    }

    void uninstall_sdl_event_dispatcher() noexcept {
        sdl_event_dispatcher_t& dispatcher = sdl_event_dispatcher();
        if (metaobject::event_dispatcher::replacement() == &dispatcher) {
            metaobject::event_dispatcher::set_instance(nullptr);
        }
        dispatcher.stop_platform();
    }
}
