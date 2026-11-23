// H1: the GUI layer's constexpr value types — point_t / extent_t / rect_t arithmetic (including the
// half-open rectangle semantics every later hit test depends on) and color_t's RGBA channels plus its
// SDL_PIXELFORMAT_ARGB8888 packing.
//
// The headers are included directly and not imported: there is no gui target yet (that lands in H5),
// so metaobject is linked only to reach the project's include/ root.
//
// Note on CHECK: it is variadic because most expressions here build braced value types inline
// (point_t{ 1, 2 }); a comma inside braces still separates arguments of a single-parameter macro,
// while __VA_ARGS__ stringifies the whole expression as usual.

#include <cstdint>
#include <cstdio>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/geometry.h"

using namespace hwshqtb::gui;

namespace gui_geometry_test {

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

/// Compile-time proof that the value types really are usable in constant expressions (the reason
/// both headers are header-only). These do not count towards the runtime check total.
static_assert(point_t{ 1, 2 }.x() == 1);
static_assert(point_t{ 3, -4 }.manhattan_length() == 7);
static_assert((point_t{ 1, 2 } + point_t{ 3, 4 }) == point_t{ 4, 6 });
static_assert((point_t{ 1, 2 } - point_t{ 3, 4 }) == point_t{ -2, -2 });
static_assert(!extent_t{ 1, 1 }.isEmpty());
static_assert(extent_t{ 0, 1 }.isEmpty());
static_assert((extent_t{ 2, 3 } + extent_t{ 4, 5 }) == extent_t{ 6, 8 });
static_assert(rect_t{ 0, 0, 4, 3 }.right() == 4);
static_assert(rect_t{ 0, 0, 4, 3 }.bottom() == 3);
static_assert(rect_t{ 0, 0, 4, 3 }.contains(point_t{ 3, 2 }));
static_assert(!rect_t{ 0, 0, 4, 3 }.contains(point_t{ 4, 2 }));
static_assert(rect_t{ 0, 0, 4, 4 }.intersected(rect_t{ 2, 2, 4, 4 }) == rect_t{ 2, 2, 2, 2 });
static_assert(rect_t{ 0, 0, 4, 4 }.translated(point_t{ 1, 1 }) == rect_t{ 1, 1, 4, 4 });
static_assert(color_t::black_color().to_argb() == 0xFF000000u);
static_assert(color_t::white_color().to_argb() == 0xFFFFFFFFu);
static_assert(color_t::red_color().to_argb() == 0xFFFF0000u);
static_assert(color_t::transparent_color().to_argb() == 0x00000000u);
static_assert(color_t::from_argb(0xFFFF0000u) == color_t::red_color());
static_assert(color_t::from_rgb(0x123456u) == color_t{ 0x12, 0x34, 0x56 });

}  // namespace gui_geometry_test

using namespace gui_geometry_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    std::printf("== H1: GUI geometry and colour value types ==\n");

    // ---- (1) point_t: construction, accessors, compound/plain arithmetic, equality, Manhattan ----
    {
        constexpr point_t origin;
        CHECK(origin.x() == 0);
        CHECK(origin.y() == 0);

        point_t p{ 3, -4 };
        CHECK(p.x() == 3);
        CHECK(p.y() == -4);

        p.set_x(10);
        p.set_y(-20);
        CHECK(p.x() == 10);
        CHECK(p.y() == -20);
        CHECK((p == point_t{ 10, -20 }));
        CHECK(!(p == point_t{ 10, -19 }));

        point_t q{ 1, 2 };
        q += point_t{ 3, 4 };
        CHECK((q == point_t{ 4, 6 }));
        q -= point_t{ 5, -1 };
        CHECK((q == point_t{ -1, 7 }));

        // operator+ / operator- are pure: the operands keep their values.
        const point_t a{ 2, 5 };
        const point_t b{ -7, 3 };
        CHECK((a + b) == point_t{ -5, 8 });
        CHECK((a - b) == point_t{ 9, 2 });
        CHECK((a == point_t{ 2, 5 }));
        CHECK((b == point_t{ -7, 3 }));

        CHECK((point_t{ 3, -4 }.manhattan_length() == 7));
        CHECK((point_t{ -3, -4 }.manhattan_length() == 7));
        CHECK((point_t{ -3, 4 }.manhattan_length() == 7));
        CHECK(point_t{}.manhattan_length() == 0);
    }

    // ---- (2) extent_t: width/height, isEmpty (including negative extents), +, equality ----
    {
        constexpr extent_t empty;
        CHECK(empty.width() == 0);
        CHECK(empty.height() == 0);
        CHECK(empty.isEmpty());

        CHECK((extent_t{ 1, 0 }.isEmpty()));
        CHECK((extent_t{ 0, 1 }.isEmpty()));
        CHECK((extent_t{ -3, 5 }.isEmpty()));
        CHECK((extent_t{ 5, -3 }.isEmpty()));
        CHECK(!(extent_t{ 1, 1 }.isEmpty()));

        CHECK((extent_t{ 2, 3 } == extent_t{ 2, 3 }));
        CHECK(!(extent_t{ 2, 3 } == extent_t{ 3, 2 }));
        CHECK(!(extent_t{ -1, 2 } == extent_t{ 1, 2 }));

        extent_t e;
        e.set_width(7);
        e.set_height(9);
        CHECK(e.width() == 7);
        CHECK(e.height() == 9);
        CHECK((e == extent_t{ 7, 9 }));

        const extent_t sum = extent_t{ 2, 3 } + extent_t{ 4, 5 };
        CHECK((sum == extent_t{ 6, 8 }));
        CHECK((e == extent_t{ 7, 9 }));                                 // addition is pure
        CHECK(((extent_t{ -1, -2 } + extent_t{ 1, 2 }) == extent_t{}));
    }

    // ---- (3) rect_t: half-open right()/bottom(), contains() including every edge ----
    {
        constexpr rect_t r{ 10, 20, 30, 40 };
        CHECK(r.x() == 10);
        CHECK(r.y() == 20);
        CHECK(r.width() == 30);
        CHECK(r.height() == 40);
        CHECK(r.left() == r.x());
        CHECK(r.top() == r.y());
        CHECK(r.right() == 40);                                         // x() + width(), not -1
        CHECK(r.bottom() == 60);                                        // y() + height(), not -1
        CHECK((r.top_left() == point_t{ 10, 20 }));
        CHECK((rect_t{ point_t{ 10, 20 }, extent_t{ 30, 40 } } == r));

        // Interior, and the last covered pixel on each axis.
        CHECK(r.contains(point_t{ 25, 30 }));
        CHECK(r.contains(point_t{ 39, 59 }));
        // Each edge: the left/top edges are inside, right()/bottom() are NOT (half-open).
        CHECK(r.contains(point_t{ 10, 20 }));                           // top-left corner
        CHECK(r.contains(point_t{ 39, 20 }));                           // right()-1 on the top row
        CHECK(r.contains(point_t{ 10, 59 }));                           // bottom()-1 on the left column
        CHECK(!r.contains(point_t{ 40, 20 }));                          // right() excluded
        CHECK(!r.contains(point_t{ 10, 60 }));                          // bottom() excluded
        CHECK(!r.contains(point_t{ 9, 20 }));                           // left()-1
        CHECK(!r.contains(point_t{ 10, 19 }));                          // top()-1
        CHECK(!r.contains(point_t{ 40, 60 }));                          // bottom-right corner

        // Empty rectangles contain nothing, not even their own origin.
        CHECK(rect_t{}.isEmpty());
        CHECK((rect_t{ 5, 5, 0, 10 }.isEmpty()));
        CHECK((rect_t{ 5, 5, 10, 0 }.isEmpty()));
        CHECK((rect_t{ 5, 5, -1, 10 }.isEmpty()));
        CHECK(!r.isEmpty());
        CHECK(!(rect_t{ 0, 0, 1, 1 }.isEmpty()));
        CHECK(!(rect_t{ 5, 5, 0, 10 }.contains(point_t{ 5, 5 })));
        CHECK(!rect_t{}.contains(point_t{ 0, 0 }));

        // A rectangle in negative coordinates is symmetric: right()/bottom() still add the extent.
        constexpr rect_t n{ -5, -4, 10, 8 };
        CHECK(n.left() == -5);
        CHECK(n.top() == -4);
        CHECK(n.right() == 5);
        CHECK(n.bottom() == 4);
        CHECK(n.contains(point_t{ -5, -4 }));
        CHECK(n.contains(point_t{ -1, -1 }));
        CHECK(n.contains(point_t{ 4, 3 }));                             // right()-1 / bottom()-1
        CHECK(!n.contains(point_t{ 5, 3 }));                            // right()
        CHECK(!n.contains(point_t{ 4, 4 }));                            // bottom()
        CHECK(!n.contains(point_t{ -6, 0 }));
        CHECK(!n.contains(point_t{ -5, -5 }));
        CHECK((n.top_left() == point_t{ -5, -4 }));
    }

    // ---- (4) intersected(): overlap, containment, touching edges, disjoint, empty operands ----
    {
        constexpr rect_t a{ 0, 0, 10, 10 };
        constexpr rect_t b{ 5, 5, 10, 10 };

        constexpr rect_t overlap = a.intersected(b);
        CHECK((overlap == rect_t{ 5, 5, 5, 5 }));
        CHECK(overlap.right() == 10);
        CHECK(overlap.bottom() == 10);

        // A rectangle contained in another intersects to itself.
        CHECK((a.intersected(rect_t{ 2, 2, 3, 3 }) == rect_t{ 2, 2, 3, 3 }));
        CHECK((rect_t{ 2, 2, 3, 3 }.intersected(a) == rect_t{ 2, 2, 3, 3 }));

        // One-dimensional overlap.
        CHECK((a.intersected(rect_t{ 5, -5, 2, 30 }) == rect_t{ 5, 0, 2, 10 }));

        // Touching edges do not overlap under the half-open convention.
        CHECK(a.intersected(rect_t{ 10, 0, 5, 5 }).isEmpty());          // shares the right edge
        CHECK(a.intersected(rect_t{ 0, 10, 5, 5 }).isEmpty());          // shares the bottom edge
        CHECK(a.intersected(rect_t{ 10, 10, 5, 5 }).isEmpty());         // corner only
        CHECK(a.intersected(rect_t{ -5, 0, 5, 10 }).isEmpty());         // shares the left edge

        // Disjoint rectangles in both directions are empty.
        CHECK(a.intersected(rect_t{ 100, 100, 5, 5 }).isEmpty());
        CHECK(a.intersected(rect_t{ -20, 0, 5, 5 }).isEmpty());

        // Empty operands make the intersection empty.
        CHECK(a.intersected(rect_t{}).isEmpty());
        CHECK(rect_t{}.intersected(a).isEmpty());

        // Commutative, and overlap in negative coordinates works the same way.
        CHECK((a.intersected(b) == b.intersected(a)));
        CHECK((rect_t{ -5, -5, 10, 10 }.intersected(rect_t{ -2, -2, 4, 4 }) == rect_t{ -2, -2, 4, 4 }));
    }

    // ---- (5) united(): bounding box, negative coordinates, and the empty-operand identity ----
    {
        constexpr rect_t a{ 0, 0, 10, 10 };
        constexpr rect_t b{ 5, 5, 10, 10 };

        CHECK((a.united(b) == rect_t{ 0, 0, 15, 15 }));
        CHECK((b.united(a) == rect_t{ 0, 0, 15, 15 }));
        CHECK((a.united(rect_t{ -5, -5, 2, 2 }) == rect_t{ -5, -5, 15, 15 }));

        // Both operands are covered: the negative-coordinate union has the right extent.
        CHECK((rect_t{ -3, -2, 4, 1 }.united(rect_t{ 2, 1, 5, 5 }) == rect_t{ -3, -2, 10, 8 }));

        // A contained rectangle does not grow the union.
        CHECK((a.united(rect_t{ 2, 2, 3, 3 }) == a));

        // An empty operand is ignored; two empties stay empty.
        CHECK((a.united(rect_t{}) == a));
        CHECK((rect_t{}.united(a) == a));
        CHECK(rect_t{}.united(rect_t{}).isEmpty());
        CHECK((rect_t{}.united(rect_t{}) == rect_t{}));
    }

    // ---- (6) translated() and center() ----
    {
        constexpr rect_t r{ 10, 20, 30, 40 };
        CHECK((r.translated(point_t{ 5, -5 }) == rect_t{ 15, 15, 30, 40 }));
        CHECK((r.translated(point_t{ 0, 0 }) == r));
        CHECK(r.translated(point_t{ 5, -5 }).width() == r.width());     // extent unchanged
        CHECK(r.translated(point_t{ 5, -5 }).height() == r.height());
        CHECK((r == rect_t{ 10, 20, 30, 40 }));                         // the source is untouched

        // Even an empty rectangle translates as a position (its extent stays empty).
        CHECK((rect_t{}.translated(point_t{ 7, 8 }) == rect_t{ 7, 8, 0, 0 }));

        // center(): x() + width() / 2, y() + height() / 2 with truncating integer division.
        CHECK((rect_t{ 0, 0, 10, 6 }.center() == point_t{ 5, 3 }));
        CHECK((rect_t{ 1, 1, 10, 10 }.center() == point_t{ 6, 6 }));
        CHECK((rect_t{ 0, 0, 1, 1 }.center() == point_t{ 0, 0 }));
        CHECK((rect_t{ 0, 0, 3, 3 }.center() == point_t{ 1, 1 }));
        CHECK((rect_t{ -5, -4, 10, 8 }.center() == point_t{ 0, 0 }));
        CHECK((rect_t{ 4, 4, 2, 4 }.center() == point_t{ 5, 6 }));
    }

    // ---- (7) color_t: named colours, from_rgb/from_argb, the ARGB word, round trip, is_opaque ----
    {
        // The default is opaque black.
        CHECK(color_t{}.red() == 0);
        CHECK(color_t{}.green() == 0);
        CHECK(color_t{}.blue() == 0);
        CHECK(color_t{}.alpha() == 255);
        CHECK(color_t{}.is_opaque());
        CHECK((color_t{} == color_t::black_color()));

        // Named colours and their packed ARGB words (0xAARRGGBB, H0 measurement).
        CHECK(color_t::black_color().to_argb() == 0xFF000000u);
        CHECK(color_t::white_color().to_argb() == 0xFFFFFFFFu);
        CHECK(color_t::red_color().to_argb() == 0xFFFF0000u);
        CHECK(color_t::green_color().to_argb() == 0xFF00FF00u);
        CHECK(color_t::blue_color().to_argb() == 0xFF0000FFu);
        CHECK(color_t::yellow_color().to_argb() == 0xFFFFFF00u);
        CHECK(color_t::cyan_color().to_argb() == 0xFF00FFFFu);
        CHECK(color_t::magenta_color().to_argb() == 0xFFFF00FFu);
        CHECK(color_t::gray_color().to_argb() == 0xFF808080u);
        CHECK(color_t::transparent_color().to_argb() == 0x00000000u);

        // The named colours expose the expected channels.
        CHECK(color_t::red_color().red() == 255);
        CHECK(color_t::red_color().green() == 0);
        CHECK(color_t::red_color().blue() == 0);
        CHECK(color_t::red_color().alpha() == 255);
        CHECK(color_t::transparent_color().alpha() == 0);
        CHECK(!color_t::transparent_color().is_opaque());

        // from_rgb(): 0xRRGGBB with alpha forced to 255; the high byte is ignored.
        constexpr color_t rgb = color_t::from_rgb(0x123456u);
        CHECK(rgb.red() == 0x12);
        CHECK(rgb.green() == 0x34);
        CHECK(rgb.blue() == 0x56);
        CHECK(rgb.alpha() == 255);
        CHECK((rgb == color_t{ 0x12, 0x34, 0x56 }));
        CHECK(rgb.to_argb() == 0xFF123456u);
        CHECK((color_t::from_rgb(0xAB123456u) == color_t::from_rgb(0x123456u)));

        // from_argb(): the exact inverse of to_argb().
        constexpr color_t argb = color_t::from_argb(0x80123456u);
        CHECK(argb.red() == 0x12);
        CHECK(argb.green() == 0x34);
        CHECK(argb.blue() == 0x56);
        CHECK(argb.alpha() == 0x80);
        CHECK(argb.to_argb() == 0x80123456u);
        CHECK((color_t{ 0x12, 0x34, 0x56, 0x78 }.to_argb() == 0x78123456u));

        // The round trip holds for opaque, translucent and fully transparent words.
        CHECK(color_t::from_argb(0x00000000u).to_argb() == 0x00000000u);
        CHECK(color_t::from_argb(0xFFFF0000u).to_argb() == 0xFFFF0000u);
        CHECK(color_t::from_argb(0xFFFFFFFFu).to_argb() == 0xFFFFFFFFu);
        CHECK(color_t::from_argb(0x7F336699u).to_argb() == 0x7F336699u);
        CHECK(color_t::from_argb(0x80123456u).to_argb() == 0x80123456u);
        CHECK(color_t::from_argb(0xFF00FF00u).to_argb() == 0xFF00FF00u);
        CHECK((color_t::from_argb(color_t::blue_color().to_argb()) == color_t::blue_color()));

        // is_opaque() is alpha == 255, not "any alpha".
        CHECK(color_t::white_color().is_opaque());
        CHECK((color_t{ 0, 0, 0, 254 }.is_opaque() == false));
        CHECK((color_t{ 0, 0, 0, 255 }.is_opaque()));
        CHECK((color_t{ 1, 2, 3 }.is_opaque()));                        // default alpha argument

        // Equality compares all four channels.
        CHECK((color_t{ 1, 2, 3, 4 } == color_t{ 1, 2, 3, 4 }));
        CHECK(!(color_t{ 1, 2, 3, 4 } == color_t{ 1, 2, 3, 255 }));
        CHECK(!(color_t{ 1, 2, 3, 4 } == color_t{ 1, 2, 4, 4 }));
        CHECK(!(color_t::white_color() == color_t::transparent_color()));
    }

    if (g_failures == 0) {
        std::printf("gui geometry: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("gui geometry: %d of %d checks failed\n", g_failures, g_checks);
    return 1;
}
