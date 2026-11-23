/// @file platform_window_factory.cpp
/// @brief The one place that chooses this build's platform_window_t implementation.

#include "platform_window_factory.h"

#include <memory>
#include <string_view>
#include <utility>

#include "hwshqtb/gui/window_t.h"
#include "sdl_platform_window.h"

namespace hwshqtb::gui::detail {
    std::unique_ptr<platform_window_t> create_platform_window(
        window_t& owner, std::string_view title, extent_t size) {
        auto window = std::make_unique<sdl_platform_window_t>(title, size);
        // The platform is wired to its window here, before it is handed over: from this point on every
        // native event reaches the owner, and the owner's destruction removes the filter again.
        window->attach(owner);
        return window;
    }
}
