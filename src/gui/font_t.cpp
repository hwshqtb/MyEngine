#include "hwshqtb/gui/font_t.h"

#include <array>
#include <cstdint>

#include "detail/builtin_glyph_table.h"
#include "hwshqtb/gui/font_database_t.h"
#include "hwshqtb/gui/font_engine_t.h"
#include "hwshqtb/gui/text_layout_t.h"

namespace hwshqtb::gui {
    font_metrics_t::font_metrics_t(const font_t& font) noexcept:
        _font(font),
        _engine(font_database_t::instance().engine_for(font)) { }

    const font_t& font_metrics_t::font() const noexcept {
        return _font;
    }

    font_engine_t* font_metrics_t::engine() const noexcept {
        return _engine;
    }

    int font_metrics_t::ascent() const noexcept {
        return _engine->ascent();
    }

    int font_metrics_t::descent() const noexcept {
        return _engine->descent();
    }

    int font_metrics_t::line_height() const noexcept {
        return _engine->ascent() + _engine->descent() + _engine->leading();
    }

    int font_metrics_t::advance(int codepoint) const noexcept {
        std::uint32_t glyph = 0;
        const char32_t value = static_cast<char32_t>(codepoint);
        if (!_engine->glyph_index(value, glyph)) {
            // An unmapped character is drawn as the missing-glyph box, so it advances like one: an
            // unsupported character keeps its cell instead of collapsing the line.
            glyph = 0;
        }
        return _engine->advance(glyph);
    }

    int font_metrics_t::horizontal_advance(std::string_view text) const {
        // The SAME layout the painter walks, so a caret and the painted glyphs cannot disagree about
        // where a code point ends. For a single line this is the sum of the advances.
        const text_layout_t layout{ text, _font };
        return layout.extent().width();
    }

    extent_t font_metrics_t::text_extent(std::string_view text) const {
        // The metrics answer with the REAL layout, so a metric and the painted frame can never disagree
        // about how wide a string is.
        const text_layout_t layout{ text, _font };
        return layout.extent();
    }

    std::array<std::uint8_t, font_t::glyph_height()> font_metrics_t::glyph_rows(int codepoint) const noexcept {
        // The legacy row accessor reads the BUILT-IN table: it is the deterministic glyph-shape query the
        // golden tests pin, and it deliberately ignores which family these metrics resolved to.
        return detail::builtin_glyph_rows(codepoint);
    }

    void pin_builtin_font_family() noexcept {
        try {
            font_database_t::instance().set_default_family(font_database_t::builtin_family());
        }
        catch (...) {
            // The promise of the header: a failed assignment leaves the current default in place.
        }
    }
}
