/// @file fallback_font_engine.cpp
/// @brief The per-character fallback chain over several font engines.

#include "fallback_font_engine.h"

#include <stdexcept>
#include <utility>

namespace hwshqtb::gui::detail {
    namespace {
        /// @brief The sub-engine number packed into a glyph index (one-based; 0 is the box).
        constexpr std::uint32_t engine_shift = 24;

        /// @brief The child a packed glyph index belongs to.
        /// @param glyph The packed index.
        /// @return The child index, or -1 for the reserved "missing box" index 0.
        int engine_of(std::uint32_t glyph) noexcept {
            return static_cast<int>(glyph >> engine_shift) - 1;
        }
    }

    fallback_font_engine_t::fallback_font_engine_t(std::vector<font_engine_t*> engines):
        _engines(std::move(engines)) {
        if (_engines.empty()) {
            throw std::invalid_argument("fallback_font_engine_t: the chain must not be empty");
        }
        for (const font_engine_t* const engine : _engines) {
            if (engine == nullptr) {
                throw std::invalid_argument("fallback_font_engine_t: a child engine must not be null");
            }
        }
    }

    fallback_font_engine_t::~fallback_font_engine_t() = default;

    int fallback_font_engine_t::ascent() const noexcept {
        return _engines.front()->ascent();
    }

    int fallback_font_engine_t::descent() const noexcept {
        return _engines.front()->descent();
    }

    int fallback_font_engine_t::leading() const noexcept {
        return _engines.front()->leading();
    }

    std::size_t fallback_font_engine_t::engine_count() const noexcept {
        return _engines.size();
    }

    bool fallback_font_engine_t::glyph_index(char32_t codepoint, std::uint32_t& glyph) const noexcept {
        for (std::size_t index = 0; index < _engines.size(); ++index) {
            std::uint32_t inner = 0;
            if (!_engines[index]->glyph_index(codepoint, inner)) {
                continue;
            }
            if (inner > inner_glyph_mask) {
                continue; // a child glyph that does not fit the packing cannot be addressed
            }
            glyph = (static_cast<std::uint32_t>(index) + 1u) << engine_shift | inner;
            return true;
        }
        return false;
    }

    int fallback_font_engine_t::advance(std::uint32_t glyph) const noexcept {
        const int child = engine_of(glyph);
        if (child < 0) {
            // Index 0 is the requested missing-glyph box: the last child (the built-in face) owns it.
            return _engines.back()->advance(0);
        }
        if (child >= static_cast<int>(_engines.size())) {
            return 0;
        }
        return _engines[static_cast<std::size_t>(child)]->advance(glyph & inner_glyph_mask);
    }

    bool fallback_font_engine_t::glyph_bitmap(std::uint32_t glyph, glyph_bitmap_t& out) const {
        const int child = engine_of(glyph);
        if (child < 0) {
            return _engines.back()->glyph_bitmap(0, out);
        }
        if (child >= static_cast<int>(_engines.size())) {
            return false;
        }
        return _engines[static_cast<std::size_t>(child)]->glyph_bitmap(glyph & inner_glyph_mask, out);
    }
}
