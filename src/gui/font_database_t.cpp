/// @file font_database_t.cpp
/// @brief The process-wide font registry and the (family, size) → engine cache.

#include "hwshqtb/gui/font_database_t.h"

#include <string>
#include <utility>
#include <vector>

#include "detail/builtin_font_engine.h"
#include "fallback_font_engine.h"
#include "win32_font_engine.h"

namespace hwshqtb::gui {
    font_database_t& font_database_t::instance() noexcept {
        // Never destroyed: engines outlive static destruction, exactly like the event dispatchers.
        static font_database_t* database = new font_database_t();
        return *database;
    }

    font_database_t::font_database_t():
        _builtin(std::make_unique<detail::builtin_font_engine>()) {
        // The default family is the face the system uses for its own chrome, so application text looks
        // native without any configuration. When the platform cannot answer (or has no font backend at
        // all) the built-in bitmap face is the default instead, and text still appears.
        const std::string system_family = detail::system_default_family();
        _default_family = system_family.empty() ? std::string(builtin_family()) : system_family;
        _families.push_back(_default_family);
        if (_default_family != builtin_family()) {
            _families.push_back(builtin_family());
        }
    }

    font_database_t::~font_database_t() = default;

    std::string_view font_database_t::builtin_family() noexcept {
        return detail::builtin_font_engine::family_name;
    }

    void font_database_t::set_default_family(std::string_view family) {
        // An empty request restores the system default; when the platform has no font backend that is
        // the built-in face, and text still appears.
        ++_generation; // the style layer's text metrics are now stale
        if (!family.empty()) {
            _default_family = std::string(family);
            return;
        }
        const std::string system_family = detail::system_default_family();
        _default_family = system_family.empty() ? std::string(builtin_family()) : system_family;
    }

    unsigned font_database_t::generation() const noexcept {
        return _generation;
    }

    std::string_view font_database_t::default_family() const noexcept {
        return _default_family;
    }

    std::span<const std::string_view> font_database_t::families() const noexcept {
        return _families;
    }

    font_engine_t* font_database_t::engine_for(const font_t& font) noexcept {
        // Every failure answers the built-in face: see the header's contract. Metrics are constructed on
        // the paint path, so resolution cannot be allowed to fail upward.
        try {
            const std::string family =
                font.family().empty() ? _default_family : std::string(font.family());
            const int pixel_size = font.pixel_size();
            // The built-in bitmap face has exactly one size, so its key ignores the pixel size; a
            // platform face selects that height.
            const auto key = std::make_pair(family, family == std::string(builtin_family()) ? 0 : pixel_size);
            const auto cached = _resolved.find(key);
            if (cached != _resolved.end()) {
                return cached->second;
            }
            if (family == builtin_family()) {
                _resolved.emplace(key, _builtin.get());
                return _builtin.get();
            }
            // A platform face: the requested family first, then the SYSTEM family (a Latin-only face has
            // no CJK glyphs, and the system face usually does), then the built-in bitmap face as the
            // last resort — which is also the owner of the missing-glyph box (glyph index 0).
            std::vector<font_engine_t*> chain;
            auto primary = std::make_unique<detail::win32_font_engine>(family, pixel_size);
            chain.push_back(primary.get());
            _owned.push_back(std::move(primary));
            const std::string system_family = detail::system_default_family();
            if (!system_family.empty() && system_family != family) {
                auto secondary = std::make_unique<detail::win32_font_engine>(system_family, pixel_size);
                chain.push_back(secondary.get());
                _owned.push_back(std::move(secondary));
            }
            chain.push_back(_builtin.get());
            if (chain.size() == 1) {
                _resolved.emplace(key, chain.front());
                return chain.front();
            }
            auto composite = std::make_unique<detail::fallback_font_engine_t>(std::move(chain));
            font_engine_t* const resolved = composite.get();
            _owned.push_back(std::move(composite));
            _resolved.emplace(key, resolved);
            return resolved;
        }
        catch (...) {
            return _builtin.get();
        }
    }
}
