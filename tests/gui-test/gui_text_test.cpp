// H3: the bitmap font (font_t / font_metrics_t), painter_t::draw_text and the glyph-run seam.
//
// Two kinds of evidence live here:
//   - offscreen pixel assertions on Image_t, i.e. the same rendering path the window uses, with no
//     window and no display: ink exists, exact glyph shapes, the baseline model, advances, spaces,
//     newlines, untouched background;
//   - a recording paint engine: it records the glyph_run_t the painter hands it instead of
//     rasterising, which proves painter_t is engine-agnostic (layout only, rasterisation in the
//     engine). That test does not touch SDL at all.
//
// Note on CHECK: variadic, because several expressions build braced value types inline
// (rect_t{ 5, 4, 6, 5 }); the preprocessor separates on those commas and __VA_ARGS__ joins them back.

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

#include "hwshqtb/gui/font_database_t.h"
#include "hwshqtb/gui/font_engine_t.h"
#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/paint_device_t.h"
#include "hwshqtb/gui/paint_engine_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/gui/utf8.h"

using namespace hwshqtb::gui;

namespace gui_text_test {

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

/// The glyph index @p codepoint has in the resolved face: what a recorded run must carry.
/// @param codepoint The code point to map.
/// @return The glyph index; 0 (the missing-glyph box) when the face has no glyph for it.
std::uint32_t glyph_of(char32_t codepoint) {
    std::uint32_t glyph = 0;
    font_engine_t* const engine = font_database_t::instance().engine_for(font_t{});
    (void)engine->glyph_index(codepoint, glyph);
    return glyph;
}

/// Compile-time proof that the font value type and its coverage rules are constant expressions (the
/// reason font_t is header-only and unexported). These do not count towards the runtime total.
static_assert(font_t::default_font() == font_t{});
static_assert(font_t::default_font().pixel_size() == 0); // 0 = the family default
static_assert(font_t::default_font().family().empty());  // empty = the database default
static_assert(font_t{ "hwshqtb-mono-5x7", 7 }.pixel_size() == 7);
static_assert(font_t{ "hwshqtb-mono-5x7", 7 } == font_t{ "hwshqtb-mono-5x7", 7 });
static_assert(!(font_t{ "hwshqtb-mono-5x7", 7 } == font_t{ "hwshqtb-mono-5x7", 8 }));
static_assert(font_t::glyph_width() == 5);
static_assert(font_t::glyph_height() == 7);
static_assert(font_t::covers(32));
static_assert(font_t::covers(126));
static_assert(!font_t::covers(31));
static_assert(!font_t::covers(127));

/// Counts the pixels of @p image that have exactly @p color.
int count_color(const image_t& image, color_t color) {
    const extent_t size = image.size();
    int count = 0;
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            if (image.pixel(x, y) == color) {
                ++count;
            }
        }
    }
    return count;
}

/// Whether every pixel of @p rect has exactly @p expected colour.
bool region_is(const image_t& image, rect_t rect, color_t expected) {
    for (int y = rect.top(); y < rect.bottom(); ++y) {
        for (int x = rect.left(); x < rect.right(); ++x) {
            if (!(image.pixel(x, y) == expected)) {
                return false;
            }
        }
    }
    return true;
}

/// Whether every pixel outside @p rect has exactly @p expected colour.
bool outside_is(const image_t& image, rect_t rect, color_t expected) {
    const extent_t size = image.size();
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            if (rect.contains(point_t{ x, y })) {
                continue;
            }
            if (!(image.pixel(x, y) == expected)) {
                return false;
            }
        }
    }
    return true;
}

/// The bounding box of every pixel that is not @p background, in half-open rect_t form.
struct ink_bounds_t {
    bool any = false;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    rect_t rect() const noexcept {
        return rect_t{ left, top, right - left, bottom - top };
    }
};

ink_bounds_t ink_bounds(const image_t& image, color_t background) {
    const extent_t size = image.size();
    ink_bounds_t bounds{};
    bounds.left = size.width();
    bounds.top = size.height();
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            if (image.pixel(x, y) == background) {
                continue;
            }
            bounds.any = true;
            if (x < bounds.left) {
                bounds.left = x;
            }
            if (y < bounds.top) {
                bounds.top = y;
            }
            if (x + 1 > bounds.right) {
                bounds.right = x + 1;
            }
            if (y + 1 > bounds.bottom) {
                bounds.bottom = y + 1;
            }
        }
    }
    return bounds;
}

/// The number of ink pixels of one glyph in the font table.
int glyph_ink_count(const font_metrics_t& metrics, int codepoint) {
    int count = 0;
    for (const std::uint8_t row : metrics.glyph_rows(codepoint)) {
        count += std::popcount(row);
    }
    return count;
}

/// The leftmost ink column of one glyph (0..glyph_width()-1), or -1 for a blank glyph.
int glyph_left_column(const font_metrics_t& metrics, int codepoint) {
    const std::array<std::uint8_t, font_t::glyph_height()> rows = metrics.glyph_rows(codepoint);
    for (int column = 0; column < font_t::glyph_width(); ++column) {
        for (int row = 0; row < font_t::glyph_height(); ++row) {
            if (((rows[static_cast<std::size_t>(row)] >> (font_t::glyph_width() - 1 - column)) & 1u) != 0) {
                return column;
            }
        }
    }
    return -1;
}

/// Whether the pixel at (@p x, @p y) matches the glyph table's ink for @p codepoint, where (@p x,
/// @p y) is the cell's top-left corner.
bool cell_matches(const image_t& image, const font_metrics_t& metrics, int codepoint, int x, int y,
                  color_t ink, color_t background) {
    const std::array<std::uint8_t, font_t::glyph_height()> rows = metrics.glyph_rows(codepoint);
    for (int row = 0; row < font_t::glyph_height(); ++row) {
        for (int column = 0; column < font_t::glyph_width(); ++column) {
            const bool expected_ink =
                ((rows[static_cast<std::size_t>(row)] >> (font_t::glyph_width() - 1 - column)) & 1u) != 0;
            if (!(image.pixel(x + column, y + row) == (expected_ink ? ink : background))) {
                return false;
            }
        }
    }
    return true;
}

// ---- the recording engine: a paint_engine_t that records calls instead of rasterising ----

/// A copy of one glyph_run_t; the arrays in glyph_run_t only live for the call, so the engine copies
/// them the way a real backend would consume (or cache) them.
struct recorded_glyph_run_t {
    const font_engine_t* engine = nullptr;
    std::vector<std::uint32_t> glyphs;
    std::vector<point_t> positions;
    color_t color;
};

/// A paint engine that records the painter's requests. Its existence is the H3 engine-switchability
/// proof: a backend that never rasterises anything still receives a complete, correct glyph run.
class recording_paint_engine_t final: public paint_engine_t {
public:
    engine_feature_t features() const noexcept override {
        return engine_feature_t{};
    }

    bool begin(paint_device_t&) override {
        active = true;
        ++begins;
        return true;
    }

    bool end() override {
        active = false;
        ++ends;
        return true;
    }

    void update_state(const engine_state_t& state) override {
        last_state = state;
        ++state_updates;
    }

    void draw_rects(const rect_t*, int count, bool fill) override {
        ++rect_batches;
        rects_recorded += count;
        last_fill = fill;
    }

    void draw_glyphs(const glyph_run_t& run) override {
        recorded_glyph_run_t recorded;
        recorded.engine = run.engine;
        recorded.glyphs.assign(run.glyphs, run.glyphs + run.count);
        recorded.positions.assign(run.positions, run.positions + run.count);
        recorded.color = run.color;
        runs.push_back(recorded);
    }
    void draw_image(const rect_t&, const image_view_t&) override { }

    bool active = false;
    int begins = 0;
    int ends = 0;
    int state_updates = 0;
    int rect_batches = 0;
    int rects_recorded = 0;
    bool last_fill = false;
    engine_state_t last_state;
    std::vector<recorded_glyph_run_t> runs;
};

/// A paint device that hands out the recording engine (never rasterises, never touches SDL).
class recording_device_t final: public paint_device_t {
public:
    explicit recording_device_t(extent_t size):
        _size(size) { }

    extent_t size() const noexcept override {
        return _size;
    }

    paint_engine_t* paint_engine() noexcept override {
        return &_engine;
    }

    recording_paint_engine_t& engine() noexcept {
        return _engine;
    }

private:
    extent_t _size;
    recording_paint_engine_t _engine;
};

}  // namespace gui_text_test

using namespace gui_text_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    pin_builtin_font_family(); // deterministic text: the built-in face, not the machine's UI font
    std::printf("== H3: bitmap font, draw_text and the glyph-run seam ==\n");

    const font_t default_font = font_t::default_font();
    const font_metrics_t metrics{ default_font };
    constexpr color_t white = color_t::white_color();
    constexpr color_t black = color_t::black_color();

    // ---- (1) font_t / font_metrics_t: the vertical model and the advance model ----
    {
        CHECK(default_font == font_t{});
        // A default font is a description with no family and no size: it means "the database default".
        CHECK(default_font.family().empty());
        CHECK(default_font.pixel_size() == 0);
        CHECK(!(font_t{ "hwshqtb-mono-5x7", font_t::glyph_height() } == default_font));
        CHECK(font_t::first_codepoint() == 32);
        CHECK(font_t::last_codepoint() == 126);
        CHECK(font_t::covers(' '));
        CHECK(font_t::covers('~'));
        CHECK(font_t::covers('A'));
        CHECK(!font_t::covers(31));
        CHECK(!font_t::covers(127));
        CHECK(!font_t::covers(-1));
        CHECK(font_t::covers(32) && font_t::covers(126) && !font_t::covers(32 - 1) && !font_t::covers(126 + 1));

        CHECK(metrics.font() == default_font);
        CHECK(metrics.ascent() == 6);
        CHECK(metrics.descent() == 1);
        CHECK(metrics.line_height() == 8);
        CHECK(metrics.line_height() == metrics.ascent() + metrics.descent() + 1);

        CHECK(metrics.advance(' ') == 6);
        CHECK(metrics.advance('A') == 6);
        CHECK(metrics.advance('~') == 6);
        CHECK(metrics.advance(0) == 6);      // the replacement box advances like any glyph
        CHECK(metrics.advance('\n') == 6);   // draw_text consumes it; metrics does not special-case
        CHECK(metrics.advance('A') == font_t::glyph_width() + 1);

        CHECK(metrics.text_extent("").width() == 0);
        CHECK(metrics.text_extent("").height() == 0);
        CHECK(metrics.text_extent(" ").width() == metrics.advance(' '));
        CHECK(metrics.text_extent(" ").height() == metrics.line_height());
        CHECK(metrics.text_extent("hello world").width() == 11 * metrics.advance('h'));
        CHECK(metrics.text_extent("hello world").height() == metrics.line_height());
        CHECK(metrics.text_extent("a\nb").width() == metrics.advance('a'));
        CHECK(metrics.text_extent("a\nb").height() == 2 * metrics.line_height());
        CHECK(metrics.text_extent("ab\nc").width() == 2 * metrics.advance('a'));
        CHECK(metrics.text_extent("\n").width() == 0);
        CHECK(metrics.text_extent("\n").height() == 2 * metrics.line_height());
        CHECK(metrics.text_extent("\n").height() == 16);
    }

    // ---- (2) the glyph table: exact rows, the space, the replacement box, descenders ----
    {
        const std::array<std::uint8_t, 7> space_rows = metrics.glyph_rows(' ');
        CHECK(space_rows[0] == 0 && space_rows[1] == 0 && space_rows[2] == 0 && space_rows[3] == 0);
        CHECK(space_rows[4] == 0 && space_rows[5] == 0 && space_rows[6] == 0);
        CHECK(glyph_ink_count(metrics, ' ') == 0);

        // H is the glyph the raster assertions below compare against: its exact 5x7 pattern.
        const std::array<std::uint8_t, 7> h_rows = metrics.glyph_rows('H');
        const std::array<std::uint8_t, 7> expected_h{ 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x00 };
        CHECK(h_rows == expected_h);
        CHECK(h_rows[0] == 0x11);
        CHECK(h_rows[3] == 0x1F);
        CHECK(h_rows[6] == 0x00);
        CHECK(glyph_ink_count(metrics, 'H') == 15);

        // I is the second shape asserted in the raster test.
        const std::array<std::uint8_t, 7> expected_i{ 0x0E, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00 };
        CHECK(metrics.glyph_rows('I') == expected_i);

        // Out-of-coverage codepoints all map to one hollow replacement box.
        const std::array<std::uint8_t, 7> replacement = metrics.glyph_rows(0x7F);
        const std::array<std::uint8_t, 7> expected_replacement{ 0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F };
        CHECK(replacement == expected_replacement);
        CHECK(metrics.glyph_rows(0) == replacement);
        CHECK(metrics.glyph_rows(31) == replacement);
        CHECK(metrics.glyph_rows(127) == replacement);
        CHECK(metrics.glyph_rows('\n') == replacement);
        CHECK(metrics.glyph_rows(200) == replacement);

        // Row 6 is the descent row: blank for H, inked by p (which is why descent() is 1).
        CHECK(metrics.glyph_rows('H')[6] == 0);
        CHECK(metrics.glyph_rows('p')[6] != 0);
        CHECK(metrics.glyph_rows('g')[6] != 0);
        CHECK(metrics.glyph_rows('j')[6] != 0);
    }

    // ---- (3) "hello world": ink exists, the bbox matches the logical extent, background clean ----
    {
        image_t image{ 200, 48 };
        {
            painter_t painter{ image };
            CHECK(!painter.is_active());
            CHECK(painter.begin());
            painter.clear(white);
            painter.set_color(black);
            painter.draw_text(point_t{ 8, 30 }, "hello world");
            CHECK(painter.is_active());
            CHECK(painter.end());
        }

        const ink_bounds_t bounds = ink_bounds(image, white);
        CHECK(bounds.any);
        const rect_t bbox = bounds.rect();
        const extent_t extent = metrics.text_extent("hello world");

        // The first glyph is 'h', whose ink starts in column 0 and row 0, so the bbox's top-left is
        // exactly (origin.x, origin.y - ascent).
        CHECK(bbox.left() == 8);
        CHECK(bbox.top() == 30 - metrics.ascent());
        CHECK(bbox.top() == 24);

        // Width: 11 cells of 6 pixels, minus the trailing inter-glyph column (the last glyph 'd'
        // inks its cell's column 4, the spacing column after it carries no pixels).
        CHECK(bbox.width() == 65);
        CHECK(bbox.width() == extent.width() - (metrics.advance('h') - font_t::glyph_width()));

        // Height: ascenders reach row 0 and no glyph of "hello world" descends, so the ink is
        // exactly ascent() rows high and fits inside line_height().
        CHECK(bbox.height() == metrics.ascent());
        CHECK(bbox.height() == 6);
        CHECK(bbox.height() <= metrics.line_height());
        CHECK(bbox.bottom() == 30);
        CHECK(bbox.bottom() <= 30 + metrics.descent());

        // Every pixel outside the bbox is untouched background (the last H2-style background check).
        CHECK(outside_is(image, bbox, white));

        // Exactly the glyph table's ink pixels were drawn, no more and no fewer.
        int expected_ink = 0;
        for (const char ch : std::string_view{ "hello world" }) {
            expected_ink += glyph_ink_count(metrics, static_cast<unsigned char>(ch));
        }
        CHECK(expected_ink == 111);
        CHECK(count_color(image, black) == expected_ink);
        CHECK(count_color(image, white) == 200 * 48 - expected_ink);

        // No descenders in "hello world": the baseline row itself and everything above the ascent
        // stay background.
        CHECK(region_is(image, rect_t{ 0, 30, 200, 1 }, white));
        CHECK(region_is(image, rect_t{ 0, 0, 200, 24 }, white));
    }

    // ---- (4) exact glyph shape: every pixel of 'H' and 'I' matches the table ----
    {
        image_t image{ 32, 32 };
        {
            painter_t painter{ image };
            painter.begin();
            painter.clear(white);
            painter.set_color(black);
            painter.draw_text(point_t{ 4, 16 }, "H");
            painter.draw_text(point_t{ 20, 16 }, "I");
            painter.end();
        }
        // 'H' cell top-left is (4, 16 - 6) = (4, 10); 'I' cell top-left is (20, 10).
        CHECK(cell_matches(image, metrics, 'H', 4, 10, black, white));
        CHECK(cell_matches(image, metrics, 'I', 20, 10, black, white));
        CHECK(outside_is(image, rect_t{ 4, 10, 21, 7 }, white));

        // The cells were placed 16 pixels apart on purpose: cell_matches() above compares all 35
        // pixels of each cell against the table, so two independent placements prove the raster path
        // reproduces the glyph bitmap exactly rather than a plausible-looking approximation.
        CHECK(glyph_ink_count(metrics, 'H') == 15);
        CHECK(glyph_ink_count(metrics, 'I') == 10);
    }

    // ---- (5) baseline: first ink row is origin.y - ascent, descent stays above origin.y + descent ----
    {
        constexpr int baseline = 24;
        image_t image{ 40, 40 };
        {
            painter_t painter{ image };
            painter.begin();
            painter.clear(white);
            painter.set_color(black);
            painter.draw_text(point_t{ 10, baseline }, "H");
            painter.end();
        }
        const ink_bounds_t bounds = ink_bounds(image, white);
        CHECK(bounds.any);
        CHECK(bounds.top == baseline - metrics.ascent());
        CHECK(bounds.top == 18);
        CHECK(bounds.bottom == baseline);
        CHECK(bounds.bottom <= baseline + metrics.descent());
        CHECK(region_is(image, rect_t{ 0, 0, 40, 18 }, white));            // nothing above the ascent
        CHECK(region_is(image, rect_t{ 0, 24, 40, 16 }, white));           // nothing at/below the baseline
        CHECK(region_is(image, rect_t{ 0, 18, 10, 6 }, white));            // nothing left of the cell
        CHECK(region_is(image, rect_t{ 15, 18, 25, 6 }, white));           // nothing right of the cell

        // 'p' uses the single descent row: its lowest ink is at origin.y (row 6), one row below the
        // baseline, and never reaches origin.y + descent == baseline + 1.
        image_t descender{ 40, 40 };
        {
            painter_t painter{ descender };
            painter.begin();
            painter.clear(white);
            painter.set_color(black);
            painter.draw_text(point_t{ 10, baseline }, "p");
            painter.end();
        }
        const ink_bounds_t p_bounds = ink_bounds(descender, white);
        CHECK(p_bounds.any);
        CHECK(p_bounds.bottom == baseline + metrics.descent());
        CHECK(p_bounds.bottom == baseline + 1);
        CHECK(descender.pixel(10, baseline) == black);                     // the descender row itself
        CHECK(descender.pixel(11, baseline) == white);
        CHECK(region_is(descender, rect_t{ 0, baseline + 1, 40, 40 - baseline - 1 }, white));

        // The h (no descender) and p (descender) of one word share the baseline model.
        image_t word{ 40, 40 };
        {
            painter_t painter{ word };
            painter.begin();
            painter.clear(white);
            painter.set_color(black);
            painter.draw_text(point_t{ 10, baseline }, "hp");
            painter.end();
        }
        CHECK(word.pixel(10, baseline) == white);                          // h: no ink on the baseline row
        CHECK(word.pixel(10 + metrics.advance('h'), baseline) == black);   // p: ink on the baseline row
    }

    // ---- (6) advance: the second glyph starts advance() columns later; spaces only move the pen ----
    {
        image_t pair{ 64, 32 };
        {
            painter_t painter{ pair };
            painter.begin();
            painter.clear(white);
            painter.set_color(black);
            painter.draw_text(point_t{ 10, 20 }, "AA");
            painter.end();
        }
        const ink_bounds_t pair_bounds = ink_bounds(pair, white);
        CHECK(pair_bounds.any);
        CHECK(pair_bounds.left == 10 + glyph_left_column(metrics, 'A'));
        CHECK(glyph_left_column(metrics, 'A') == 0);
        CHECK(pair_bounds.right == 10 + metrics.advance('A') + font_t::glyph_width());
        CHECK(cell_matches(pair, metrics, 'A', 10, 14, black, white));
        CHECK(cell_matches(pair, metrics, 'A', 10 + metrics.advance('A'), 14, black, white));
        // The inter-glyph column (column 15) is blank: each glyph keeps its 5 ink columns and the
        // 6th is the spacing column.
        CHECK(region_is(pair, rect_t{ 15, 14, 1, 7 }, white));

        image_t spaced{ 64, 32 };
        {
            painter_t painter{ spaced };
            painter.begin();
            painter.clear(white);
            painter.set_color(black);
            painter.draw_text(point_t{ 10, 20 }, "A A");
            painter.end();
        }
        // The space advances the pen by one cell: the second A's ink starts at 10 + 2 * advance.
        const ink_bounds_t spaced_bounds = ink_bounds(spaced, white);
        CHECK(spaced_bounds.any);
        CHECK(spaced_bounds.left == 10 + glyph_left_column(metrics, 'A'));
        CHECK(spaced_bounds.right == 10 + 2 * metrics.advance('A') + font_t::glyph_width());
        CHECK(cell_matches(spaced, metrics, 'A', 10, 14, black, white));
        CHECK(cell_matches(spaced, metrics, 'A', 10 + 2 * metrics.advance('A'), 14, black, white));
        CHECK(region_is(spaced, rect_t{ 10 + metrics.advance('A'), 14, metrics.advance('A'), 7 }, white));

        // A space on its own advances the logical pen but draws nothing at all.
        image_t blank{ 32, 24 };
        {
            painter_t painter{ blank };
            painter.begin();
            painter.clear(white);
            painter.set_color(black);
            painter.draw_text(point_t{ 4, 16 }, "   ");
            painter.end();
        }
        CHECK(!ink_bounds(blank, white).any);
        CHECK(count_color(blank, black) == 0);
        CHECK(metrics.text_extent("   ").width() == 3 * metrics.advance(' '));
    }

    // ---- (7) newline: the next line is line_height() lower and starts at the same x ----
    {
        image_t image{ 48, 48 };
        {
            painter_t painter{ image };
            painter.begin();
            painter.clear(white);
            painter.set_color(black);
            painter.draw_text(point_t{ 10, 20 }, "A\nB");
            painter.end();
        }
        const ink_bounds_t bounds = ink_bounds(image, white);
        CHECK(bounds.any);
        CHECK(bounds.top == 20 - metrics.ascent());
        CHECK(bounds.bottom == 20 + metrics.line_height());   // B's last ink row is its baseline - 1
        CHECK(bounds.bottom == 28);
        CHECK(cell_matches(image, metrics, 'A', 10, 14, black, white));
        CHECK(cell_matches(image, metrics, 'B', 10, 14 + metrics.line_height(), black, white));
        // The blank band between the two lines (A's descent row and B's leading row) is untouched.
        CHECK(region_is(image, rect_t{ 0, 20, 48, 2 }, white));
        CHECK(region_is(image, rect_t{ 0, 0, 48, 14 }, white));

        // A run that starts with '\n' draws only the second line, at one line_height() lower.
        image_t leader{ 48, 48 };
        {
            painter_t painter{ leader };
            painter.begin();
            painter.clear(white);
            painter.set_color(black);
            painter.draw_text(point_t{ 10, 20 }, "\nB");
            painter.end();
        }
        const ink_bounds_t leader_bounds = ink_bounds(leader, white);
        CHECK(leader_bounds.any);
        CHECK(leader_bounds.top == 20 + metrics.line_height() - metrics.ascent());
        CHECK(cell_matches(leader, metrics, 'B', 10, 14 + metrics.line_height(), black, white));
    }

    // ---- (8) replacement box and the current colour ----
    {
        image_t image{ 32, 24 };
        {
            painter_t painter{ image };
            painter.begin();
            painter.clear(white);
            painter.set_color(color_t::red_color());
            painter.draw_text(point_t{ 4, 16 }, "\x7F");
            painter.end();
        }
        CHECK(cell_matches(image, metrics, 0x7F, 4, 10, color_t::red_color(), white));
        CHECK(count_color(image, black) == 0);

        // The painter's current colour is applied at call time, per run.
        image_t two{ 32, 24 };
        {
            painter_t painter{ two };
            painter.begin();
            painter.clear(white);
            painter.set_color(color_t::red_color());
            painter.draw_text(point_t{ 4, 16 }, "A");
            painter.set_color(color_t::blue_color());
            painter.draw_text(point_t{ 10, 16 }, "B");
            painter.end();
        }
        CHECK(cell_matches(two, metrics, 'A', 4, 10, color_t::red_color(), white));
        CHECK(cell_matches(two, metrics, 'B', 10, 10, color_t::blue_color(), white));
    }

    // ---- (9) the recording engine: painter_t is engine-agnostic ----
    {
        recording_device_t device{ extent_t{ 320, 200 } };
        {
            painter_t painter{ device };
            CHECK(!painter.is_active());
            CHECK(painter.begin());
            CHECK(painter.is_active());
            painter.set_color(color_t::red_color());
            painter.draw_text(point_t{ 8, 20 }, "hi");
            CHECK(painter.end());
        }

        recording_paint_engine_t& engine = device.engine();
        CHECK(engine.begins == 1);
        CHECK(engine.ends == 1);
        // The ONE run the contract promises for one draw_text() call.
        CHECK(engine.runs.size() == 1);
        const recorded_glyph_run_t& run = engine.runs.front();
        // The run names the resolved engine (the built-in face): a glyph index means nothing without it.
        CHECK(run.engine == metrics.engine());
        CHECK(run.positions.front() == point_t{ 8, 20 });
        CHECK(run.glyphs.size() == 2);
        CHECK(run.glyphs[0] == glyph_of(U'h'));
        CHECK(run.glyphs[1] == glyph_of(U'i'));
        CHECK(run.positions.size() == 2);
        CHECK(run.positions[0] == point_t{ 8, 20 });
        CHECK(run.positions[1] == point_t{ 8 + metrics.advance('h'), 20 });
        CHECK(run.positions[1] == point_t{ 14, 20 });
        CHECK(run.color == color_t::red_color());
        // Text is not a rectangle batch: the engine saw a glyph run and no rects.
        CHECK(engine.rect_batches == 0);
        CHECK(engine.rects_recorded == 0);
        // The state snapshot the painter pushed carries the colour too (update_state before the run).
        CHECK(engine.state_updates >= 1);
        CHECK(engine.last_state.color() == color_t::red_color());

        // Empty text emits no run; newline-only text emits nothing either.
        {
            painter_t painter{ device };
            painter.begin();
            painter.draw_text(point_t{ 0, 0 }, "");
            painter.draw_text(point_t{ 0, 0 }, "\n\n");
            painter.end();
        }
        CHECK(engine.runs.size() == 1);

        // Drawing while inactive is ignored.
        {
            painter_t painter{ device };
            painter.draw_text(point_t{ 0, 0 }, "ignored");
        }
        CHECK(engine.runs.size() == 1);

        // '\n' is layout-only: it never reaches the engine as a codepoint, it only shifts positions.
        {
            painter_t painter{ device };
            painter.begin();
            painter.draw_text(point_t{ 8, 30 }, "a\nb");
            painter.end();
        }
        CHECK(engine.runs.size() == 2);
        const recorded_glyph_run_t& multiline = engine.runs.back();
        CHECK(multiline.glyphs.size() == 2);
        CHECK(multiline.glyphs[0] == glyph_of(U'a'));
        CHECK(multiline.glyphs[1] == glyph_of(U'b'));
        CHECK(multiline.positions[0] == point_t{ 8, 30 });
        CHECK(multiline.positions[1] == point_t{ 8, 30 + metrics.line_height() });

        // A run that starts with '\n': the first GLYPH's baseline is the first line's successor, not the
        // draw_text() argument.
        {
            painter_t painter{ device };
            painter.begin();
            painter.draw_text(point_t{ 8, 30 }, "\nq");
            painter.end();
        }
        CHECK(engine.runs.size() == 3);
        CHECK(engine.runs.back().positions.front() == point_t{ 8, 30 + metrics.line_height() });

        // One run per call even for a long line; the run carries GLYPH INDICES of its engine.
        {
            painter_t painter{ device };
            painter.begin();
            painter.draw_text(point_t{ 8, 40 }, "hello world");
            painter.end();
        }
        CHECK(engine.runs.size() == 4);
        CHECK(engine.runs.back().glyphs.size() == 11);
        CHECK(engine.runs.back().glyphs[0] == glyph_of(U'h'));
        CHECK(engine.runs.back().glyphs[10] == glyph_of(U'd'));
        CHECK(engine.runs.back().positions[10] == point_t{ 8 + 10 * metrics.advance('h'), 40 });
    }

    if (g_failures == 0) {
        std::printf("gui text: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("gui text: %d of %d checks failed\n", g_failures, g_checks);
    return 1;
}
