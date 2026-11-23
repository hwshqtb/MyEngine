#ifndef HWSHQTB__GUI__FONT_H
#define HWSHQTB__GUI__FONT_H

/// @file font_t.h
/// @brief font_t: a font DESCRIPTION (family + pixel size); font_metrics_t: its resolved metrics
///        (the QFont / QFontMetrics split).

#include <array>
#include <cstdint>
#include <string_view>

#include "hwshqtb/gui/export.h"
#include "hwshqtb/gui/font_engine_t.h"
#include "hwshqtb/gui/geometry.h"

namespace hwshqtb::gui {
    /// @brief A font as a caller names it: a family plus a pixel size (the QFont value subset).
    ///
    /// @par Description, not a rasteriser
    /// font_t carries no glyphs and answers no metrics: it is the KEY a font_engine_t is resolved from
    /// (font_database_t::engine_for()). That split is Qt's: QFont is a description, QFontEngine is the
    /// thing that knows how to draw.
    ///
    /// @par Defaults
    /// An empty family means "the database's default family" and a pixel size of 0 means "that
    /// family's default size", which is what makes a default-constructed font_t the application
    /// default and keeps `font_metrics_t{}` a meaningful, machine-independent statement.
    ///
    /// @par Header-only by design
    /// A constexpr value type: usable in constant expressions, and deliberately NOT exported (see
    /// export.h: 接口模板不导出).
    class font_t {
    public:
        /// @brief Construct the default description (empty family, default size).
        constexpr font_t() noexcept = default;

        /// @brief Construct a description of @p family at @p pixel_size pixels.
        /// @param family The family name; empty means the database default.
        /// @param pixel_size The pixel size; 0 means the family default.
        constexpr font_t(std::string_view family, int pixel_size) noexcept:
            _family(family),
            _pixel_size(pixel_size) { }

        /// @brief The font this class names until a caller says otherwise (the same value as a
        ///        default-constructed font_t).
        ///
        /// Static instances are static methods named `<name>_<class without _t>` — hence
        /// `default_font()` rather than `default()`.
        static constexpr font_t default_font() noexcept {
            return font_t{};
        }

        /// @brief The family name; empty means "the database's default family".
        constexpr std::string_view family() const noexcept {
            return _family;
        }

        /// @brief The pixel size; 0 means "the family's default size".
        /// @note The built-in bitmap face IGNORES this: it has exactly one size (a 5x7 cell), so a
        ///       built-in font at any pixel size draws identically. A platform face (a real TrueType
        ///       font behind the GDI engine) is the one that honours it, by selecting that pixel height.
        constexpr int pixel_size() const noexcept {
            return _pixel_size;
        }

        /// @name Geometry of the built-in face
        /// The one face whose cell geometry is a compile-time constant. A resolved engine reports its
        /// own metrics instead (font_engine_t::ascent()/descent()/leading()); these constants describe
        /// the built-in 5x7 table and are what the deterministic tests pin.
        /// @{
        /// @brief The width of one glyph cell of the built-in face in pixels.
        static constexpr int glyph_width() noexcept {
            return 5;
        }

        /// @brief The height of one glyph cell of the built-in face in pixels.
        static constexpr int glyph_height() noexcept {
            return 7;
        }

        /// @brief The first code point with a glyph in the built-in table (space, 32).
        static constexpr int first_codepoint() noexcept {
            return 32;
        }

        /// @brief The last code point with a glyph in the built-in table (tilde, 126).
        static constexpr int last_codepoint() noexcept {
            return 126;
        }

        /// @brief Whether the BUILT-IN table has a glyph for @p codepoint.
        /// @param codepoint The code point to test.
        /// @return true for 32..126 inclusive.
        /// @note This asks the built-in face, not the face this description names: use
        ///       font_engine_t::glyph_index() to ask a resolved font.
        static constexpr bool covers(int codepoint) noexcept {
            return codepoint >= first_codepoint() && codepoint <= last_codepoint();
        }
        /// @}

        /// @brief Whether two descriptions name the same family at the same size.
        friend constexpr bool operator==(font_t lhs, font_t rhs) noexcept {
            return lhs._family == rhs._family && lhs._pixel_size == rhs._pixel_size;
        }

    private:
        /// @brief The family name; empty means the database default.
        std::string_view _family{};
        /// @brief The pixel size; 0 means the family default.
        int _pixel_size = 0;
    };

    /// @brief The metrics of one resolved font (QFontMetrics' minimal form).
    ///
    /// @par Forwarding, not owning
    /// The constructor resolves the description through font_database_t and keeps the engine pointer;
    /// every metric and every glyph answers from that engine. The engine belongs to the database (which
    /// outlives every metrics object), so copying a font_metrics_t is cheap and safe.
    ///
    /// @par Vertical model
    /// A glyph's baseline row is drawn at the pen y; ascent() pixels lie above it and descent() below.
    /// line_height() is ascent() + descent() + leading() from the engine.
    ///
    /// @par Horizontal model
    /// Text extent is advance based (QFontMetrics::horizontalAdvance), not ink based: the sum of the
    /// engine's advances along each line.
    ///
    /// @par Coverage and the replacement glyph
    /// A code point the engine cannot map is drawn as the missing-glyph box (glyph index 0) and still
    /// advances by that engine's advance(0), so unsupported characters keep their cells instead of
    /// vanishing.
    ///
    /// @par glyph_rows()
    /// The legacy row accessor is defined over the BUILT-IN 5x7 table only. It keeps answering for the
    /// built-in face (which is what the deterministic glyph-shape tests assert) and returns the
    /// replacement box for everything else; a caller that needs another face's pixels asks its
    /// font_engine_t::glyph_bitmap().
    class HWSHQTB_GUI_EXPORT font_metrics_t {
    public:
        /// @brief Resolve @p font through the font database and keep its engine.
        /// @param font The description; defaults to font_t::default_font().
        /// @note noexcept: resolving a family NEVER throws. A family that cannot be loaded — unknown
        ///       name, no platform backend, allocation failure — falls back to the built-in face.
        ///       Metrics are constructed on the paint path and inside size_hint(), so a throwing
        ///       constructor would put a load failure into layout; Qt's QFontMetrics never throws
        ///       either.
        explicit font_metrics_t(const font_t& font = font_t::default_font()) noexcept;

        /// @brief The description these metrics were resolved from.
        const font_t& font() const noexcept;

        /// @brief The resolved engine (never null).
        /// @note noexcept. The pointer is owned by the font database.
        font_engine_t* engine() const noexcept;

        /// @brief The number of pixels above the baseline.
        int ascent() const noexcept;

        /// @brief The number of pixels below the baseline.
        int descent() const noexcept;

        /// @brief The baseline-to-baseline distance in pixels.
        int line_height() const noexcept;

        /// @brief The pen advance of one code point in pixels.
        /// @param codepoint The code point; unmapped ones advance like the missing-glyph box.
        int advance(int codepoint) const noexcept;

        /// @brief The pen advance of @p text: the sum of its code points' advances (QFontMetrics).
        /// @param text The text; '\n' starts a new line and the answer is then the widest line, exactly
        ///        like text_extent().
        /// @return The advance in pixels.
        /// @note The building block for a caret or a selection edge: with a proportional face every code
        ///       point has its own advance, so "index * cell_width" is wrong. A wide character advances
        ///       like the cell it occupies, not like one byte.
        /// @note This runs the SAME layout text_extent() does, so a caret placed with this and a glyph
        ///       painted by painter_t can never disagree.
        /// @note O(n) per call; a line edit calls it with the prefix before the caret.
        int horizontal_advance(std::string_view text) const;

        /// @brief The extent of @p text, advance based.
        ///
        /// width() is the widest line's sum of advances; height() is (number of '\n'-separated lines) *
        /// line_height(). The empty string has an empty extent. A code point without a glyph occupies
        /// the missing-glyph box's advance, so an unsupported character never collapses the line.
        ///
        /// @param text The text; '\n' separates lines.
        /// @return The extent in pixels.
        /// @note This runs the real text layout: the answer is exactly what painter_t::draw_text() lays
        ///       out, so metrics and painting cannot disagree.
        extent_t text_extent(std::string_view text) const;

        /// @brief The seven row bytes of one code point of the BUILT-IN face.
        /// @param codepoint The code point; out-of-coverage values give the replacement box.
        /// @return Seven row bytes, top row first, bit 4 = leftmost column.
        /// @note Only meaningful for the built-in face (family "hwshqtb-mono-5x7"): it reads that
        ///       table, not the resolved engine, and every other face answers the replacement box.
        /// @note noexcept.
        std::array<std::uint8_t, font_t::glyph_height()> glyph_rows(int codepoint) const noexcept;

    private:
        /// @brief The description these metrics were resolved from.
        font_t _font;
        /// @brief The resolved engine; owned by the font database, never null.
        font_engine_t* _engine = nullptr;
    };

    /// @brief Pin the font database's default family to the built-in bitmap face.
    ///
    /// The default family is the platform's UI face, which is machine dependent: its glyph shapes, its
    /// advances and therefore the pixels of any rendered text differ between systems. A test or a golden
    /// capture that asserts text (or a whole frame) calls this BEFORE creating any control, and gets the
    /// one deterministic face the project ships.
    ///
    /// @note Declared here, next to the metrics that read it, and defined out of line: a caller that
    ///       already includes this header needs no other include, so pinning is exactly one statement.
    /// @note Call it on the thread that creates the widgets, before the first font_metrics_t of a
    ///       machine-independent scenario is constructed.
    /// @note noexcept: it only assigns a string (an allocation failure leaves the default unchanged).
    /// @note Exported like every free function a consumer calls (see export.h: a class-level marker
    ///       does not cover a free function).
    HWSHQTB_GUI_EXPORT void pin_builtin_font_family() noexcept;
}

#endif
