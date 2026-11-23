/// @file text_layout_t.cpp
/// @brief The layout: decode UTF-8 once, map every code point to a glyph, emit positioned runs.

#include "hwshqtb/gui/text_layout_t.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "hwshqtb/gui/font_database_t.h"
#include "hwshqtb/gui/font_engine_t.h"
#include "hwshqtb/gui/utf8.h"

namespace hwshqtb::gui {
    namespace {
        /// @brief One contiguous stretch of glyphs that resolves through the same engine.
        struct segment_t {
            /// @brief The engine the glyphs of this segment rasterise with.
            const font_engine_t* engine = nullptr;
            /// @brief Glyph indices, in text order.
            std::vector<std::uint32_t> glyphs;
            /// @brief The pen position of each glyph (its baseline origin), parallel to @c glyphs.
            std::vector<point_t> positions;
        };
    }

    struct text_layout_t::impl {
        /// @brief The segments, in text order.
        std::vector<segment_t> segments;
        /// @brief The runs handed out by runs(); each points into one segment's buffers.
        std::vector<glyph_run_t> runs;
        /// @brief The logical extent of the laid-out text.
        extent_t extent;
        /// @brief The baseline-to-baseline distance of the layout's primary engine.
        int line_height = 0;
    };

    text_layout_t::text_layout_t(std::string_view text, const font_t& font):
        _impl(std::make_unique<impl>()) {
        font_engine_t* const primary = font_database_t::instance().engine_for(font);
        _impl->line_height = primary->ascent() + primary->descent() + primary->leading();
        if (text.empty()) {
            return; // the empty string has an empty extent and no run
        }
        int pen_x = 0;
        int pen_y = 0;
        int widest = 0;
        int lines = 1;
        segment_t* current = nullptr;
        for (std::size_t index = 0; index < text.size();) {
            if (text[index] == '\n') {
                ++index; // the painter consumes the line break; an engine never sees it
                if (pen_x > widest) {
                    widest = pen_x;
                }
                pen_x = 0;
                pen_y += _impl->line_height;
                ++lines;
                continue;
            }
            const int decoded = utf8_next(text, index);
            // A malformed byte is ONE replacement character (U+FFFD), not one glyph per byte: the cell
            // count of a line therefore follows the code points, exactly like the line edit's caret.
            const char32_t codepoint =
                decoded < 0 ? static_cast<char32_t>(0xFFFD) : static_cast<char32_t>(decoded);
            font_engine_t* engine = primary;
            std::uint32_t glyph = 0;
            if (!engine->glyph_index(codepoint, glyph)) {
                // No engine in the chain has this character: draw the missing-glyph box of the primary
                // engine, and still advance by that box's advance so the line keeps its shape.
                glyph = 0;
            }
            if (current == nullptr || current->engine != engine) {
                _impl->segments.emplace_back();
                current = &_impl->segments.back();
                current->engine = engine;
            }
            current->glyphs.push_back(glyph);
            current->positions.push_back(point_t{ pen_x, pen_y });
            pen_x += engine->advance(glyph);
        }
        if (pen_x > widest) {
            widest = pen_x;
        }
        _impl->extent = extent_t{ widest, lines * _impl->line_height };
        // Runs are built last: their spans point into the segment buffers, which do not move afterwards
        // (a later push_back into the OUTER vector cannot invalidate an inner vector's storage).
        _impl->runs.reserve(_impl->segments.size());
        for (const segment_t& segment : _impl->segments) {
            glyph_run_t run;
            run.engine = segment.engine;
            run.glyphs = segment.glyphs.data();
            run.positions = segment.positions.data();
            run.count = segment.glyphs.size();
            run.color = color_t::black_color();
            _impl->runs.push_back(run);
        }
    }

    text_layout_t::~text_layout_t() = default;

    text_layout_t::text_layout_t(text_layout_t&& other) noexcept:
        _impl(std::move(other._impl)) { }

    text_layout_t& text_layout_t::operator=(text_layout_t&& other) noexcept {
        if (this != &other) {
            _impl = std::move(other._impl);
        }
        return *this;
    }

    std::span<const glyph_run_t> text_layout_t::runs() const noexcept {
        if (_impl == nullptr) {
            return {};
        }
        return _impl->runs;
    }

    extent_t text_layout_t::extent() const noexcept {
        return _impl == nullptr ? extent_t{} : _impl->extent;
    }

    int text_layout_t::line_height() const noexcept {
        return _impl == nullptr ? 0 : _impl->line_height;
    }

    void text_layout_t::translate(point_t offset) noexcept {
        if (_impl == nullptr || (offset.x() == 0 && offset.y() == 0)) {
            return;
        }
        for (segment_t& segment : _impl->segments) {
            for (point_t& position : segment.positions) {
                position += offset;
            }
        }
        // The runs point at the same storage, so they already see the moved positions.
    }
}
