// The font stack's semantics: description → database → engine → glyph indices.
//
// What this pins:
//  1. the built-in face answers metrics and glyph masks, and refuses a code point it has no glyph for;
//  2. UTF-8 is decoded once, as CODE POINTS: "中" is ONE glyph and ONE advance, not three - the bug this
//     round fixes (the layout used to treat every byte as a character);
//  3. the extent of a string follows its code points, malformed bytes included;
//  4. the database resolves any description to an engine and never answers null (a missing family falls
//     back to the built-in face, so text cannot vanish);
//  5. a text layout emits one run per engine, with glyph indices and parallel pen positions.
//
// Determinism: every assertion here is about the BUILT-IN family ("hwshqtb-mono-5x7"), which needs no
// platform font, no video subsystem and no display - so the numbers are exact on every machine.
//
// NOT covered here: the platform (GDI) font engine's CJK coverage. That backend does not exist in this
// build yet, so pinning it here would be an assertion about nothing.

#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

#include "hwshqtb/gui/font_database_t.h"
#include "hwshqtb/gui/font_engine_t.h"
#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/gui/text_layout_t.h"
#include "hwshqtb/gui/utf8.h"

using namespace hwshqtb::gui;

namespace font_engine_test {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

/// The UTF-8 decoder is pure arithmetic, so its contract is pinned at compile time.
static_assert(utf8_sequence_length('a') == 1);
static_assert(utf8_sequence_length(0xC3) == 2);
static_assert(utf8_sequence_length(0xE4) == 3);
static_assert(utf8_sequence_length(0xF0) == 4);
static_assert(utf8_sequence_length(0x80) == 0); // a stray continuation byte
static_assert(utf8_sequence_length(0xFF) == 0);

constexpr bool utf8_self_test() {
    // "a中b": 'a' (1 byte), U+4E2D (3 bytes), 'b' (1 byte).
    constexpr std::string_view text = "a\xE4\xB8\xAD" "b";
    std::size_t index = 0;
    const int first = utf8_next(text, index);
    const int second = utf8_next(text, index);
    const int third = utf8_next(text, index);
    return first == 'a' && second == 0x4E2D && third == 'b' && index == text.size()
        && utf8_codepoint_count(text) == 3;
}

static_assert(utf8_self_test());
static_assert(utf8_codepoint_count("\xE4\xB8\xAD") == 1);      // one code point, three bytes
static_assert(utf8_codepoint_count(std::string_view{ "\xFF" }) == 1); // one malformed byte, one cell
static_assert(utf8_codepoint_index("\xE4\xB8\xAD" "b", 3) == 1);       // past the CJK character

/// The code point U+4E2D ("中"), spelled in bytes so the source file stays ASCII.
constexpr std::string_view cjk_text = "\xE4\xB8\xAD";

/// The code point U+4E2D as a value.
constexpr char32_t cjk_codepoint = 0x4E2D;

/// The number of pixels of @p image that are not pure white (the ink a text frame left behind).
int count_ink(const image_t& image) {
    const extent_t size = image.size();
    int ink = 0;
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            if (!(image.pixel(x, y) == color_t::white_color())) {
                ++ink;
            }
        }
    }
    return ink;
}

}  // namespace font_engine_test

using namespace font_engine_test;

int main() {
    // The platform's default family, read BEFORE pinning: when a platform font backend exists this is
    // the system UI face, and the sections below use it explicitly instead of relying on the default.
    const std::string system_family{ font_database_t::instance().default_family() };
    pin_builtin_font_family(); // this test asserts the BUILT-IN face, so pin it before any resolution
    std::printf("== font engine: description → database → engine → glyphs ==\n");

    font_database_t& database = font_database_t::instance();
    const std::string_view builtin = font_database_t::builtin_family();
    CHECK(builtin == "hwshqtb-mono-5x7");
    CHECK(database.default_family() == builtin); // this build has exactly one family
    CHECK(database.families().size() >= 1);

    // ---- (1) the built-in engine: metrics, mapping, masks -------------------------------------------
    {
        font_engine_t* const engine = database.engine_for(font_t{});
        CHECK(engine != nullptr);
        CHECK(engine->ascent() == 6);
        CHECK(engine->descent() == 1);
        CHECK(engine->leading() == 1);

        // Printable ASCII has a glyph; the missing-glyph box is index 0 and keeps the same advance.
        std::uint32_t glyph = 0;
        CHECK(engine->glyph_index(U'A', glyph));
        CHECK(glyph != 0);
        CHECK(engine->advance(glyph) == 6);
        CHECK(engine->advance(0) == 6);
        CHECK(!engine->glyph_index(U' ', glyph) == false); // space IS covered
        CHECK(!engine->glyph_index(cjk_codepoint, glyph));
        CHECK(!engine->glyph_index(0xE000, glyph)); // private use area: no glyph
        CHECK(!engine->glyph_index(0x1F600, glyph)); // an astral code point

        // The mask of a printable glyph is 5x7 8-bit coverage with ink in it.
        glyph_bitmap_t bitmap;
        std::uint32_t box = 0;
        CHECK(engine->glyph_bitmap(box, bitmap));
        CHECK(bitmap.width == 5);
        CHECK(bitmap.height == 7);
        CHECK(bitmap.pitch >= 5);
        CHECK(bitmap.alpha != nullptr);
        int ink = 0;
        for (int row = 0; row < bitmap.height; ++row) {
            for (int column = 0; column < bitmap.width; ++column) {
                if (bitmap.alpha[row * bitmap.pitch + column] != 0) {
                    ++ink;
                }
            }
        }
        // The box is a hollow rectangle: an outline, so ink but not a full cell.
        CHECK(ink > 0);
        CHECK(ink < bitmap.width * bitmap.height);
        std::printf("[font] builtin: ascent %d, advance %d, box ink %d px\n", engine->ascent(),
                    engine->advance(box), ink);
    }

    // ---- (2) the core regression: ONE code point is ONE glyph and ONE advance -----------------------
    {
        const font_metrics_t metrics{};
        const int advance = metrics.advance(cjk_codepoint);
        CHECK(advance == 6);
        // "中" is three bytes but one code point: the extent used to be 18 because each byte drew a box.
        CHECK(metrics.text_extent(cjk_text).width() == advance);
        CHECK(metrics.text_extent(cjk_text).height() == metrics.line_height());

        // "a中b" is three code points, so three advances.
        const std::string_view mixed = "a" "\xE4\xB8\xAD" "b";
        CHECK(metrics.text_extent(mixed).width() == 3 * advance);
        CHECK(metrics.text_extent("ab").width() == 2 * advance);

        // A malformed byte is one replacement cell, not zero and not four.
        CHECK(metrics.text_extent("\xFF").width() == advance);
        CHECK(metrics.text_extent("\xFF\xFF").width() == 2 * advance);

        // Multi-line: the widest line times the line count height.
        CHECK(metrics.text_extent("ab\nabcd").width() == 4 * advance);
        CHECK(metrics.text_extent("ab\nabcd").height() == 2 * metrics.line_height());
        CHECK(metrics.text_extent("").isEmpty());
        std::printf("[font] \"中\" = %d px wide (one code point, %zu bytes)\n",
                    metrics.text_extent(cjk_text).width(), cjk_text.size());
    }

    // ---- (3) the layout: one run per engine, glyph indices, parallel positions ----------------------
    {
        const text_layout_t layout{ "a\xE4\xB8\xAD" "b", font_t{} };
        CHECK(layout.runs().size() == 1); // every glyph resolves through the same engine
        const glyph_run_t& run = layout.runs().front();
        CHECK(run.engine != nullptr);
        CHECK(run.count == 3); // three CODE POINTS, not five bytes
        CHECK(run.glyphs != nullptr);
        CHECK(run.positions != nullptr);
        std::uint32_t glyph = 0;
        CHECK(run.glyphs[0] == (run.engine->glyph_index(U'a', glyph) ? glyph : 0));
        // The CJK character has no glyph, so the run carries the missing box (index 0) for it.
        CHECK(run.glyphs[1] == 0);
        CHECK(run.positions[1].x() == 6);
        CHECK(run.positions[2].x() == 12);
        CHECK(layout.line_height() == 8);
        CHECK((layout.extent() == extent_t{ 18, 8 }));

        // The empty string lays out nothing at all.
        const text_layout_t empty_layout{ "", font_t{} };
        CHECK(empty_layout.runs().empty());
        CHECK(empty_layout.extent().isEmpty());
    }

    // ---- (4) the database never fails to resolve ---------------------------------------------------
    {
        // An unknown family and a size nobody uses still answer an engine: text must never disappear
        // because a font is missing, so the built-in face is the end of every chain.
        font_engine_t* const unknown = database.engine_for(font_t{ "no-such-family", 13 });
        CHECK(unknown != nullptr);
        // The same description resolves to the same engine (the cache), and so does its default-size twin.
        CHECK(unknown == database.engine_for(font_t{ "no-such-family", 13 }));
        CHECK(database.engine_for(font_t{}) == database.engine_for(font_t{ builtin, 0 }));
        CHECK(database.engine_for(font_t{ builtin, 7 }) != nullptr);
    }

    // ---- (5) an explicit family name is a description, not a lookup failure ------------------------
    {
        const font_t named{ builtin, 7 };
        CHECK(named.family() == builtin);
        CHECK(named.pixel_size() == 7);
        const font_metrics_t metrics{ named };
        CHECK(metrics.font() == named);
        CHECK(metrics.engine() != nullptr);
        CHECK(metrics.ascent() == 6); // this build has one rasteriser, so the size does not scale it yet
    }

    // ---- (6) the platform face, when this build has one ---------------------------------------------
    // Semantic assertions only: the system font is machine dependent, so nothing here pins a pixel or an
    // exact advance — only "GDI has a glyph for CJK", "it has ink", "a wide character is wider", and
    // "a private-use code point has nothing".
    if (system_family != builtin) {
        const font_metrics_t system_metrics{ font_t{ system_family, 0 } };
        font_engine_t* const engine = system_metrics.engine();
        CHECK(engine != nullptr);
        CHECK(system_metrics.ascent() > 0);
        CHECK(system_metrics.line_height() > 0);

        std::uint32_t cjk = 0;
        CHECK(engine->glyph_index(cjk_codepoint, cjk));
        std::uint32_t ascii = 0;
        CHECK(engine->glyph_index(U'A', ascii));
        const int cjk_advance = engine->advance(cjk);
        const int ascii_advance = engine->advance(ascii);
        CHECK(ascii_advance > 0);
        // A CJK cell is about one em: wider than a Latin letter, and never absurdly so.
        CHECK(cjk_advance >= ascii_advance * 3 / 2);
        CHECK(cjk_advance <= ascii_advance * 5 / 2);

        glyph_bitmap_t mask{};
        CHECK(engine->glyph_bitmap(cjk, mask));
        CHECK(mask.alpha != nullptr);
        CHECK(mask.pitch % 4 == 0);
        // The left side bearing travels WITH the mask: a real face's ink does not start at the pen, and
        // the renderer offsets the mask by this field. (The exact value is machine dependent, so only
        // "a Latin capital does not overhang its origin" is asserted here; the precise equality with
        // GDI's gmptGlyphOrigin.x is pinned by gui_win32_font_engine_test, which talks to GDI directly.)
        CHECK(mask.bearing_x >= 0);
        int ink = 0;
        for (int row = 0; row < mask.height; ++row) {
            for (int column = 0; column < mask.width; ++column) {
                if (mask.alpha[row * mask.pitch + column] != 0) {
                    ++ink;
                }
            }
        }
        CHECK(ink > 0);

        std::uint32_t unused = 0;
        CHECK(!engine->glyph_index(0xE000, unused)); // private use area: no glyph, so the box is drawn
        // A code point without a glyph still occupies a cell (the missing box's advance).
        CHECK(system_metrics.text_extent("\xEE\x80\x80").width() > 0);

        // The fallback chain: an unknown family is substituted by GDI with a Latin face that has no CJK
        // glyphs, so the SYSTEM engine later in the chain has to catch 中. If the chain were not there,
        // this would resolve to the missing-glyph box and answer no glyph.
        const font_metrics_t chained{ font_t{ "no-such-family-for-fallback", 0 } };
        std::uint32_t chained_cjk = 0;
        CHECK(chained.engine()->glyph_index(cjk_codepoint, chained_cjk));
        std::printf("[font] platform face '%s': CJK advance %d vs ASCII %d, %d ink px, chain caught CJK\n",
                    system_family.c_str(), cjk_advance, ascii_advance, ink);
    }
    else {
        std::printf("[font] no platform font backend: the system-font sections are skipped\n");
    }

    // ---- (7) end to end through the REAL painter: a system face paints 中文 onto pixels -------------
    // This is the round's hard requirement: not "the engine answers", but "draw_text() puts ink on the
    // device". The frame is a plain image_t, so the path is painter_t -> paint_engine_t::draw_glyphs ->
    // SDL renderer, exactly what a window uses.
    if (system_family != builtin) {
        const font_t system_font{ system_family, 12 };
        const font_metrics_t system_metrics{ system_font };

        image_t cjk_image{ 120, 40 };
        int cjk_ink = 0;
        {
            painter_t painter{ cjk_image };
            CHECK(painter.begin());
            painter.clear(color_t::white_color());
            painter.set_color(color_t::black_color());
            // "中文" — U+4E2D U+6587 as UTF-8 bytes, so this source file stays pure ASCII.
            painter.draw_text(point_t{ 4, system_metrics.ascent() }, "\xE4\xB8\xAD\xE6\x96\x87", system_font);
            CHECK(painter.end());
            cjk_ink = count_ink(cjk_image);
        }
        image_t ascii_image{ 120, 40 };
        int ascii_ink = 0;
        {
            painter_t painter{ ascii_image };
            CHECK(painter.begin());
            painter.clear(color_t::white_color());
            painter.set_color(color_t::black_color());
            painter.draw_text(point_t{ 4, system_metrics.ascent() }, "ab", system_font);
            CHECK(painter.end());
            ascii_ink = count_ink(ascii_image);
        }
        CHECK(cjk_ink > 0);
        CHECK(ascii_ink > 0);
        // Two CJK glyphs lay down clearly more ink than two Latin letters: this is what makes "the text
        // is really there" observable without pinning a machine-specific pixel.
        CHECK(cjk_ink > ascii_ink);
        std::printf("[font] end to end: two CJK glyphs = %d ink px, two Latin = %d ink px\n", cjk_ink,
                    ascii_ink);
    }
    else {
        std::printf("[font] no platform font backend: the end-to-end section is skipped\n");
    }

    if (g_failures == 0) {
        std::printf("font engine: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("font engine: %d of %d checks failed\n", g_failures, g_checks);
    return 1;
}
