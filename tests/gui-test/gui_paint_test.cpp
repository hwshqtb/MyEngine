// H2: the paint-engine seam (engine_state_t / paint_engine_t / paint_device_t), the SDL-backed
// image_t device and painter_t's clear / fill_rect / draw_rect.
//
// Every assertion is an offscreen pixel check: the same code path the window will use, with no
// window, no display and no SDL video subsystem. No SDL header appears here — image_t's backend is
// hidden behind pImpl, so the test only knows the GUI vocabulary.
//
// Note on CHECK: variadic, because several expressions build braced value types inline
// (rect_t{ 5, 4, 6, 5 }); the preprocessor separates on those commas, __VA_ARGS__ joins them back.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <utility>

#include "hwshqtb/gui/image_decoder_t.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/paint_device_t.h"
#include "hwshqtb/gui/paint_engine_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/gui/transform_t.h"

using namespace hwshqtb::gui;

namespace gui_paint_test {

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

/// Compile-time proof that transform_t and engine_state_t really are constant-expression types (the
/// reason they are header-only and unexported). These do not count towards the runtime total.
static_assert(transform_t{}.is_identity());
static_assert(transform_t::identity_transform() == transform_t{});
static_assert(transform_t{}.map(point_t{ 2, 3 }) == point_t{ 2, 3 });
static_assert(transform_t::from_translate(2.0, -3.0).map(point_t{ 1, 1 }) == point_t{ 3, -2 });
static_assert(transform_t::from_scale(2.0, 3.0).map(point_t{ 2, 4 }) == point_t{ 4, 12 });
static_assert((transform_t::from_translate(1.0, 0.0) * transform_t::from_scale(2.0, 2.0)).map(point_t{ 3, 0 })
              == point_t{ 7, 0 });
static_assert((transform_t::from_scale(2.0, 2.0) * transform_t::from_translate(1.0, 0.0)).map(point_t{ 3, 0 })
              == point_t{ 8, 0 });
static_assert(engine_state_t{}.is_dirty());
static_assert(engine_state_t{}.dirty_flags()
              == (engine_state_t::dirty_color | engine_state_t::dirty_clip | engine_state_t::dirty_transform));
static_assert(engine_state_t{}.color() == color_t{});
static_assert(engine_state_t{}.transform().is_identity());

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

/// Whether every pixel of @p rect has exactly @p expected colour (the caller keeps @p rect inside the
/// image; an outside coordinate would read as transparent and fail the check).
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

}  // namespace gui_paint_test

using namespace gui_paint_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    std::printf("== H2: paint engine seam, image_t and painter_t ==\n");

    // ---- (1) clear(): every pixel of the device is covered, by corners, centre and a full count ----
    {
        image_t image{ 32, 24 };
        CHECK(image.size() == extent_t{ 32, 24 });
        CHECK(image.size().width() == 32);
        CHECK(image.size().height() == 24);
        // SDL initialises a new surface to zero, i.e. transparent black (documented by image_t).
        CHECK(image.pixel(0, 0) == color_t::transparent_color());

        {
            painter_t painter{ image };
            CHECK(!painter.is_active());
            CHECK(painter.begin());
            CHECK(painter.is_active());
            painter.clear(color_t::white_color());
            CHECK(painter.is_active());               // clear() does not end the session
            CHECK(painter.end());
            CHECK(!painter.is_active());
        }

        CHECK(image.pixel(0, 0) == color_t::white_color());
        CHECK(image.pixel(31, 0) == color_t::white_color());
        CHECK(image.pixel(0, 23) == color_t::white_color());
        CHECK(image.pixel(31, 23) == color_t::white_color());
        CHECK(image.pixel(16, 12) == color_t::white_color());
        CHECK(count_color(image, color_t::white_color()) == 32 * 24);
        CHECK(count_color(image, color_t::transparent_color()) == 0);

        // A distinct-colour pass over a fresh image: the count proves every pixel was rewritten.
        image_t repainted{ 8, 6 };
        {
            painter_t painter{ repainted };
            painter.begin();
            painter.clear(color_t::white_color());
            painter.clear(color_t::magenta_color());
            painter.end();
        }
        CHECK(count_color(repainted, color_t::magenta_color()) == 8 * 6);
        CHECK(count_color(repainted, color_t::white_color()) == 0);
        CHECK(repainted.pixel(0, 0) == color_t::magenta_color());
        CHECK(repainted.pixel(7, 5) == color_t::magenta_color());
    }

    // ---- (2) fill_rect(): exactly the requested rectangle, every edge and every neighbour checked ----
    {
        image_t image{ 20, 16 };
        {
            painter_t painter{ image };
            painter.begin();
            painter.clear(color_t::white_color());
            painter.set_color(color_t::red_color());
            painter.fill_rect(rect_t{ 5, 4, 6, 5 });
            painter.end();
        }

        constexpr rect_t filled{ 5, 4, 6, 5 };
        constexpr color_t red = color_t::red_color();
        constexpr color_t white = color_t::white_color();

        // Exactly the requested pixels changed: the fill colour's count equals the rectangle's area.
        CHECK(count_color(image, red) == 6 * 5);
        CHECK(region_is(image, filled, red));

        // All four edges: the pixels just outside each side are untouched.
        CHECK(region_is(image, rect_t{ 4, 4, 1, 5 }, white));    // left (x = 4)
        CHECK(region_is(image, rect_t{ 11, 4, 1, 5 }, white));   // right (x = right() == 11)
        CHECK(region_is(image, rect_t{ 5, 3, 6, 1 }, white));    // above (y = 3)
        CHECK(region_is(image, rect_t{ 5, 9, 6, 1 }, white));    // below (y = bottom() == 9)

        // The four corners outside the rectangle, and the whole rest of the image.
        CHECK(image.pixel(4, 3) == white);
        CHECK(image.pixel(11, 3) == white);
        CHECK(image.pixel(4, 9) == white);
        CHECK(image.pixel(11, 9) == white);
        CHECK(outside_is(image, filled, white));

        // Half-open boundaries: right() - 1 / bottom() - 1 are filled, right() / bottom() are not.
        CHECK(image.pixel(10, 8) == red);
        CHECK(image.pixel(11, 8) == white);
        CHECK(image.pixel(10, 9) == white);
    }

    // ---- (3) draw_rect(): a one-pixel border inside the rectangle, the interior untouched ----
    {
        image_t image{ 20, 16 };
        {
            painter_t painter{ image };
            painter.begin();
            painter.clear(color_t::white_color());
            painter.set_color(color_t::black_color());
            painter.draw_rect(rect_t{ 5, 4, 10, 8 });
            painter.end();
        }

        constexpr rect_t outlined{ 5, 4, 10, 8 };
        constexpr color_t black = color_t::black_color();
        constexpr color_t white = color_t::white_color();

        // A one-pixel border is 2 * width + 2 * height - 4 pixels; a thicker border would be more.
        CHECK(count_color(image, black) == 2 * 10 + 2 * 8 - 4);

        // The four corners of the border (the right/bottom corner is right() - 1 / bottom() - 1).
        CHECK(image.pixel(5, 4) == black);
        CHECK(image.pixel(14, 4) == black);
        CHECK(image.pixel(5, 11) == black);
        CHECK(image.pixel(14, 11) == black);

        // The four border spans, each one pixel thick and no longer than the rectangle.
        CHECK(region_is(image, rect_t{ 5, 4, 10, 1 }, black));    // top row
        CHECK(region_is(image, rect_t{ 5, 11, 10, 1 }, black));   // bottom row
        CHECK(region_is(image, rect_t{ 5, 4, 1, 8 }, black));     // left column
        CHECK(region_is(image, rect_t{ 14, 4, 1, 8 }, black));    // right column

        // The interior (width - 2 by height - 2) and everything outside stay untouched.
        CHECK(region_is(image, rect_t{ 6, 5, 8, 6 }, white));
        CHECK(outside_is(image, outlined, white));
    }

    // ---- (4) clipping, zero-size and negative rectangles ----
    {
        // A fill partly outside the image is clipped without touching its neighbours.
        image_t image{ 10, 10 };
        {
            painter_t painter{ image };
            painter.begin();
            painter.clear(color_t::white_color());
            painter.set_color(color_t::red_color());
            painter.fill_rect(rect_t{ -3, -2, 5, 6 });   // covers x -3..1, y -2..3 -> visible 2 x 4
            painter.end();
        }
        constexpr color_t white = color_t::white_color();
        constexpr color_t red = color_t::red_color();
        CHECK(count_color(image, red) == 2 * 4);
        CHECK(region_is(image, rect_t{ 0, 0, 2, 4 }, red));
        CHECK(region_is(image, rect_t{ 2, 0, 8, 10 }, white));
        CHECK(image.pixel(0, 4) == white);      // one row below the clipped fill
        CHECK(image.pixel(2, 3) == white);      // one column right of it

        // Zero-size rectangles (and negative extents, which are empty too) draw nothing.
        const int white_before = count_color(image, white);
        {
            painter_t painter{ image };
            painter.begin();
            painter.set_color(color_t::black_color());
            painter.fill_rect(rect_t{ 3, 3, 0, 5 });
            painter.draw_rect(rect_t{ 3, 3, 5, 0 });
            painter.fill_rect(rect_t{ 3, 3, 0, 0 });
            painter.draw_rect(rect_t{ 3, 3, -4, 5 });
            painter.end();
        }
        CHECK(count_color(image, color_t::black_color()) == 0);
        CHECK(count_color(image, white) == white_before);
        CHECK(count_color(image, red) == 2 * 4);

        // Rectangles entirely outside the device draw nothing at all.
        {
            painter_t painter{ image };
            painter.begin();
            painter.set_color(color_t::blue_color());
            painter.fill_rect(rect_t{ 20, 20, 5, 5 });
            painter.draw_rect(rect_t{ -20, -20, 5, 5 });
            painter.end();
        }
        CHECK(count_color(image, color_t::blue_color()) == 0);
        CHECK(count_color(image, white) == white_before);

        // An outline partly outside is clipped as well: only the right and bottom edges survive.
        image_t outline{ 10, 10 };
        {
            painter_t painter{ outline };
            painter.begin();
            painter.clear(white);
            painter.set_color(color_t::blue_color());
            painter.draw_rect(rect_t{ -4, -4, 8, 8 });   // only x == 3 and y == 3 are inside
            painter.end();
        }
        CHECK(count_color(outline, color_t::blue_color()) == 4 + 4 - 1);
        CHECK(region_is(outline, rect_t{ 0, 3, 4, 1 }, color_t::blue_color()));
        CHECK(region_is(outline, rect_t{ 3, 0, 1, 3 }, color_t::blue_color()));
        CHECK(region_is(outline, rect_t{ 0, 0, 3, 3 }, white));
        CHECK(outside_is(outline, rect_t{ 0, 0, 4, 4 }, white));
    }

    // ---- (5) features(): the backend reports exactly the capabilities it implements ----
    {
        image_t image{ 4, 4 };
        paint_engine_t* const engine = image.paint_engine();
        CHECK(engine != nullptr);                     // a live image always has its engine
        CHECK(image.paint_engine() == engine);         // ... and the query hands out the same one
        CHECK(image.paint_engine() == image.paint_engine());

        const engine_feature_t features = engine->features();
        CHECK(bool(features & engine_feature_flag::PRIMITIVE_TRANSFORM));
        CHECK(bool(features & engine_feature_flag::PIXMAP_TRANSFORM));
        CHECK(bool(features & engine_feature_flag::CLIP));
        CHECK(bool(features & engine_feature_flag::BLEND));
        CHECK(!bool(features & engine_feature_flag::ANTIALIASING));   // SDL3 has no antialiasing switch

        const engine_feature_t expected = engine_feature_t{ engine_feature_flag::PRIMITIVE_TRANSFORM }
            | engine_feature_flag::PIXMAP_TRANSFORM | engine_feature_flag::CLIP | engine_feature_flag::BLEND;
        CHECK(features == expected);
        CHECK(engine_feature_t{} == engine_feature_flag::NONE);
        CHECK((engine_feature_t{ engine_feature_flag::NONE } & engine_feature_flag::CLIP) == engine_feature_flag::NONE);
    }

    // ---- (6) pixel() out of range: a defined value, never an out-of-bounds read ----
    {
        image_t image{ 4, 3 };
        {
            painter_t painter{ image };
            painter.begin();
            painter.clear(color_t::green_color());
            painter.end();
        }
        constexpr color_t green = color_t::green_color();
        constexpr color_t transparent = color_t::transparent_color();
        CHECK(image.pixel(0, 0) == green);
        CHECK(image.pixel(3, 2) == green);

        // Negative coordinates, the half-open edge (x == width, y == height), and far beyond.
        CHECK(image.pixel(-1, 0) == transparent);
        CHECK(image.pixel(0, -1) == transparent);
        CHECK(image.pixel(-1, -1) == transparent);
        CHECK(image.pixel(4, 0) == transparent);
        CHECK(image.pixel(0, 3) == transparent);
        CHECK(image.pixel(4, 3) == transparent);
        CHECK(image.pixel(1000000, 1000000) == transparent);
        CHECK(image.pixel(-1000000, 0) == transparent);

        // The reads did not disturb any pixel of the image.
        CHECK(image.pixel(0, 0) == green);
        CHECK(count_color(image, green) == 4 * 3);
    }

    // ---- (7) engine_state_t: defaults, dirty-flag accumulation, clear_dirty ----
    {
        engine_state_t state;
        CHECK(state.is_dirty());
        CHECK(state.dirty_flags()
              == (engine_state_t::dirty_color | engine_state_t::dirty_clip | engine_state_t::dirty_transform));
        CHECK(state.color() == color_t{});
        CHECK(!state.clip_enabled());
        CHECK(state.clip().isEmpty());
        CHECK(state.transform().is_identity());

        state.clear_dirty();
        CHECK(!state.is_dirty());
        CHECK(state.dirty_flags() == 0);

        state.set_color(color_t::red_color());
        CHECK(state.color() == color_t::red_color());
        CHECK(state.dirty_flags() == engine_state_t::dirty_color);

        state.set_clip(rect_t{ 1, 2, 3, 4 });
        CHECK(state.clip_enabled());
        CHECK(state.clip() == rect_t{ 1, 2, 3, 4 });
        CHECK((state.dirty_flags() & engine_state_t::dirty_clip) != 0);

        state.clear_clip();
        CHECK(!state.clip_enabled());
        CHECK((state.dirty_flags() & engine_state_t::dirty_clip) != 0);
        CHECK(state.clip() == rect_t{ 1, 2, 3, 4 });   // disabling the clip keeps its last rectangle

        state.clear_dirty();
        state.set_transform(transform_t::from_translate(2.0, 3.0));
        CHECK(state.transform() == transform_t::from_translate(2.0, 3.0));
        CHECK(state.dirty_flags() == engine_state_t::dirty_transform);
        CHECK(state.color() == color_t::red_color());  // the transform setter leaves colour alone
    }

    // ---- (8) transform_t: identity, factories, composition order, rounding, equality ----
    {
        const transform_t identity;
        CHECK(identity.is_identity());
        CHECK(transform_t::identity_transform() == identity);
        CHECK(transform_t{ 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 } == identity);
        CHECK(transform_t::from_scale(1.0, 1.0).is_identity());
        CHECK(transform_t::from_translate(0.0, 0.0).is_identity());

        const transform_t translate = transform_t::from_translate(2.0, 3.0);
        CHECK(!translate.is_identity());
        CHECK(translate.m11() == 1.0);
        CHECK(translate.m12() == 0.0);
        CHECK(translate.m21() == 0.0);
        CHECK(translate.m22() == 1.0);
        CHECK(translate.dx() == 2.0);
        CHECK(translate.dy() == 3.0);
        CHECK(translate.map(point_t{ 0, 0 }) == point_t{ 2, 3 });
        CHECK(translate.map(point_t{ -1, 1 }) == point_t{ 1, 4 });
        CHECK(translate.map(point_t{ 0, 0 }) == point_t{ 2, 3 });   // mapping is pure

        const transform_t scale = transform_t::from_scale(2.0, 3.0);
        CHECK(!scale.is_identity());
        CHECK(scale.m11() == 2.0);
        CHECK(scale.m22() == 3.0);
        CHECK(scale.map(point_t{ 2, 4 }) == point_t{ 4, 12 });
        CHECK(scale.map(point_t{ -1, -1 }) == point_t{ -2, -3 });

        // a * b applies b first, then a (column-vector convention, documented in transform_t.h).
        const transform_t translate_then_scale = translate * scale;
        CHECK(translate_then_scale.map(point_t{ 3, 0 }) == point_t{ 8, 3 });     // 3 * 2 + 2, 0 * 3 + 3
        const transform_t scale_then_translate = scale * translate;
        CHECK(scale_then_translate.map(point_t{ 3, 0 }) == point_t{ 10, 9 });    // (3 + 2) * 2, (0 + 3) * 3
        CHECK(translate * identity == translate);
        CHECK(identity * translate == translate);

        // Mapping rounds to the nearest pixel; an exact half rounds away from zero.
        CHECK(transform_t::from_translate(0.5, -0.5).map(point_t{ 0, 0 }) == point_t{ 1, -1 });
        CHECK(transform_t::from_scale(0.5, 0.5).map(point_t{ 3, 5 }) == point_t{ 2, 3 });   // 1.5 -> 2
        CHECK(transform_t{ 1.0, 0.5, 0.0, 1.0, 0.0, 0.0 }.map(point_t{ 2, 0 }) == point_t{ 2, 1 });

        // Equality compares all six coefficients.
        CHECK(transform_t::from_translate(1.0, 2.0) == transform_t::from_translate(1.0, 2.0));
        CHECK(!(transform_t::from_translate(1.0, 2.0) == transform_t::from_translate(1.0, 2.5)));
        CHECK(!(transform_t::from_scale(2.0, 2.0) == transform_t::from_scale(2.0, 3.0)));
    }

    // ---- (9) the engine seam directly: state snapshot, batched draw_rects, clip bit ----
    {
        image_t image{ 8, 8 };
        {
            painter_t painter{ image };
            painter.begin();
            painter.clear(color_t::white_color());
            painter.end();
        }

        paint_engine_t* const engine = image.paint_engine();
        CHECK(engine != nullptr);
        CHECK(engine->begin(image));
        engine_state_t state;
        state.set_color(color_t::red_color());
        engine->update_state(state);
        const rect_t batch[2] = { rect_t{ 0, 0, 2, 2 }, rect_t{ 5, 5, 2, 2 } };
        engine->draw_rects(batch, 2, true);
        CHECK(engine->end());

        CHECK(count_color(image, color_t::red_color()) == 2 * 2 * 2);
        CHECK(region_is(image, rect_t{ 0, 0, 2, 2 }, color_t::red_color()));
        CHECK(region_is(image, rect_t{ 5, 5, 2, 2 }, color_t::red_color()));
        CHECK(image.pixel(2, 2) == color_t::white_color());
        CHECK(image.pixel(4, 5) == color_t::white_color());

        // The clip bit is honoured: an empty clip rectangle draws nothing, and nothing else changes.
        CHECK(engine->begin(image));
        engine_state_t clipped;
        clipped.set_color(color_t::blue_color());
        clipped.set_clip(rect_t{ 0, 0, 0, 0 });
        engine->update_state(clipped);
        engine->draw_rects(batch, 2, true);
        CHECK(engine->end());
        CHECK(count_color(image, color_t::blue_color()) == 0);
        CHECK(count_color(image, color_t::red_color()) == 2 * 2 * 2);

        // An empty batch is a no-op, and ending a pass that was never begun is a no-op success.
        CHECK(engine->begin(image));
        engine->draw_rects(batch, 0, true);
        engine->draw_rects(nullptr, 2, true);
        CHECK(engine->end());
        CHECK(engine->end());
        CHECK(count_color(image, color_t::red_color()) == 2 * 2 * 2);
    }

    // ---- (10) painter session semantics: ignored drawing, flush guard, clear() and the colour ----
    {
        image_t image{ 6, 6 };
        {
            painter_t painter{ image };
            painter.clear(color_t::white_color());      // not begun -> ignored
            painter.fill_rect(rect_t{ 0, 0, 3, 3 });    // not begun -> ignored
            CHECK(!painter.is_active());
            CHECK(painter.end());                       // ending an inactive painter is a no-op
        }
        CHECK(count_color(image, color_t::transparent_color()) == 6 * 6);

        {
            painter_t painter{ image };
            CHECK(painter.begin());
            painter.set_color(color_t::red_color());
            painter.clear(color_t::white_color());
            painter.fill_rect(rect_t{ 1, 1, 2, 2 });    // clear() must not have changed the colour
            // No end(): the destructor has to flush the still-open session.
        }
        CHECK(image.pixel(0, 0) == color_t::white_color());
        CHECK(image.pixel(1, 1) == color_t::red_color());
        CHECK(image.pixel(2, 2) == color_t::red_color());
        CHECK(count_color(image, color_t::red_color()) == 4);
        CHECK(count_color(image, color_t::white_color()) == 32);

        {
            painter_t painter{ image };
            painter.begin();
            painter.end();
            painter.set_color(color_t::black_color());
            painter.fill_rect(rect_t{ 0, 0, 1, 1 });    // after end() -> ignored
            CHECK(painter.end());                       // a second end() is a no-op
        }
        CHECK(count_color(image, color_t::black_color()) == 0);
        CHECK(count_color(image, color_t::red_color()) == 4);
    }

    // ---- (11) image_t lifetime: move semantics and construction validation ----
    {
        image_t source{ 3, 3 };
        {
            painter_t painter{ source };
            painter.begin();
            painter.clear(color_t::green_color());
            painter.end();
        }
        image_t moved{ std::move(source) };
        CHECK(moved.size() == extent_t{ 3, 3 });
        CHECK(moved.pixel(1, 1) == color_t::green_color());
        CHECK(source.size() == extent_t{});                        // moved-from: empty, not dangling
        CHECK(source.size().isEmpty());
        CHECK(source.pixel(0, 0) == color_t::transparent_color());

        image_t target{ 2, 2 };
        target = std::move(moved);
        CHECK(target.size() == extent_t{ 3, 3 });
        CHECK(target.pixel(0, 0) == color_t::green_color());
        CHECK(moved.size() == extent_t{});

        // A moved-from image has no engine: asking is not an error, it answers nullptr — and a
        // painter on such a device cannot start, which is the same fact stated the way a caller acts
        // on it.
        CHECK(moved.paint_engine() == nullptr);
        CHECK(moved.paint_engine() == nullptr);        // the query never creates one either
        {
            painter_t cannot_paint{ moved };
            CHECK(!cannot_paint.begin());               // "this frame cannot be painted", not an exception
            CHECK(!cannot_paint.is_active());
            CHECK(cannot_paint.end());                  // ending a pass that never started is a no-op
        }

        bool size_threw = false;
        try {
            image_t invalid{ 0, 4 };
        } catch (const std::invalid_argument&) {
            size_threw = true;
        }
        CHECK(size_threw);
        size_threw = false;
        try {
            image_t invalid{ extent_t{ 4, -1 } };
        } catch (const std::invalid_argument&) {
            size_threw = true;
        }
        CHECK(size_threw);
    }

    // ---- (N) draw_image: the pixels travel, 1:1 AND scaled -------------------------------------------
    {
        // The source carries a known pattern: left half red, top-right green, bottom-right transparent.
        image_t source{ 4, 3 };
        {
            painter_t painter{ source };
            painter.begin();
            painter.clear(color_t{ 0, 0, 0, 0 });
            painter.set_color(color_t::red_color());
            painter.fill_rect(rect_t{ 0, 0, 2, 3 });
            painter.set_color(color_t::green_color());
            painter.fill_rect(rect_t{ 2, 0, 2, 2 });
            painter.end();
        }

        // The borrowed view is what the engine receives: pitch in PIXELS, never smaller than the width.
        const image_view_t view = source.view();
        CHECK(view.pixels != nullptr);
        CHECK(view.width == 4 && view.height == 3);
        CHECK(view.pitch >= view.width);

        image_t target{ 8, 6 };
        {
            painter_t painter{ target };
            painter.begin();
            painter.clear(color_t{ 0, 0, 0, 0 });
            painter.draw_image(rect_t{ 0, 0, 4, 3 }, source);   // 1:1 into the top half
            painter.draw_image(rect_t{ 0, 3, 8, 3 }, source);   // scaled into the bottom half
            painter.end();
        }

        // 1:1: every source pixel lands where it started.
        CHECK(target.pixel(0, 0) == color_t::red_color());
        CHECK(target.pixel(1, 2) == color_t::red_color());
        CHECK(target.pixel(3, 0) == color_t::green_color());
        CHECK(target.pixel(3, 2) == color_t::transparent_color());

        // Scaled: the destination SIZE drives the blit, so the same pattern spans the larger rectangle.
        CHECK(target.pixel(1, 5) == color_t::red_color());
        CHECK(target.pixel(5, 3) == color_t::green_color());
        CHECK(target.pixel(7, 5) == color_t::transparent_color());

        // A moved-from image answers a null view, and drawing one changes nothing.
        image_t moved{ std::move(source) };
        const image_view_t empty = source.view();
        CHECK(empty.pixels == nullptr);
        {
            painter_t painter{ target };
            painter.begin();
            // An opaque base, so "nothing was drawn" is observable: clearing with TRANSPARENT would
            // blend to a no-op and leave the previous content in place.
            painter.clear(color_t::white_color());
            painter.draw_image(rect_t{ 0, 0, 4, 3 }, source); // a null view: nothing to blit
            painter.end();
        }
        CHECK(target.pixel(0, 0) == color_t::white_color());
        CHECK(moved.view().pixels != nullptr); // the moved-TO image still owns the pixels
    }

    // ---- (N+1) load_image: a 24-bit BMP decodes to ARGB, bottom-up --------------------------------
    {
        // A hand-built 3x2 24-bit BMP exercises the two things a decoder gets wrong: the rows are stored
        // BOTTOM-first, and the bytes are BGR.
        const std::uint8_t top[3][3] = { { 0, 0, 255 }, { 0, 255, 0 }, { 255, 0, 0 } };   // red, green, blue
        const std::uint8_t bottom[3][3] = { { 255, 255, 255 }, { 0, 0, 0 }, { 128, 128, 128 } };
        const auto put16 = [](std::vector<std::uint8_t>& out, std::uint16_t value) {
            out.push_back(static_cast<std::uint8_t>(value & 0xFF));
            out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        };
        const auto put32 = [](std::vector<std::uint8_t>& out, std::uint32_t value) {
            for (int shift = 0; shift < 32; shift += 8) {
                out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
            }
        };
        // Rows are padded to a 4-byte boundary: 3 pixels * 3 bytes = 9, so 3 bytes of padding each.
        const std::uint32_t image_bytes = (9u + 3u) * 2u;
        std::vector<std::uint8_t> file;
        file.push_back('B');
        file.push_back('M');
        put32(file, 14 + 40 + image_bytes);
        put16(file, 0);
        put16(file, 0);
        put32(file, 14 + 40);
        put32(file, 40);
        put32(file, 3);
        put32(file, 2);
        put16(file, 1);
        put16(file, 24);
        put32(file, 0);
        put32(file, image_bytes);
        put32(file, 2835);
        put32(file, 2835);
        put32(file, 0);
        put32(file, 0);
        for (int column = 0; column < 3; ++column) { // the bottom row comes FIRST
            file.insert(file.end(), bottom[column], bottom[column] + 3);
        }
        file.insert(file.end(), 3u, std::uint8_t{ 0 });
        for (int column = 0; column < 3; ++column) {
            file.insert(file.end(), top[column], top[column] + 3);
        }
        file.insert(file.end(), 3u, std::uint8_t{ 0 });

        const std::string path = "gui_paint_test_probe.bmp";
        {
            std::ofstream out{ path, std::ios::binary };
            out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
        }
        const image_t loaded = load_image(path);
        std::remove(path.c_str());

        CHECK(loaded.size() == (extent_t{ 3, 2 }));
        CHECK(loaded.pixel(0, 0) == color_t::red_color());
        CHECK(loaded.pixel(1, 0) == color_t::green_color());
        CHECK(loaded.pixel(2, 0) == (color_t{ 0, 0, 255, 255 }));
        CHECK(loaded.pixel(0, 1) == color_t::white_color());
        CHECK(loaded.pixel(1, 1) == color_t::black_color());
        CHECK(loaded.pixel(2, 1) == (color_t{ 128, 128, 128, 255 }));

        // A file that is not there is an error, not a silent empty image.
        bool threw = false;
        try {
            (void)load_image("gui_paint_test_missing.bmp");
        } catch (const std::runtime_error& error) {
            threw = std::string(error.what()).find("cannot open") != std::string::npos;
        }
        CHECK(threw);
    }

    // ---- (N+2) the decoding seam: sniff by MAGIC BYTES, then dispatch -----------------------------
    {
        // Format detection is pure byte comparison and must not consult the file name.
        CHECK(image_format_of("\x89PNG\r\n\x1A\n....") == image_format_enum::PNG);
        CHECK(image_format_of("BM......") == image_format_enum::BMP);
        CHECK(image_format_of("\xFF\xD8\xFF\xE0....") == image_format_enum::JPEG);
        CHECK(image_format_of("GIF89a..") == image_format_enum::UNKNOWN);
        CHECK(image_format_of("") == image_format_enum::UNKNOWN);

        // A file whose magic is unknown is rejected BY THE SNIFFER, so the message names the problem
        // instead of leaking a decoder's vague "not a PNG".
        {
            const char* const path = "gui_paint_test_unknown.bin";
            {
                std::ofstream out{ path, std::ios::binary };
                out << "GIF89a not a raster format this layer knows";
            }
            bool rejected = false;
            try {
                (void)load_image(path);
            } catch (const std::runtime_error& error) {
                rejected = std::string(error.what()).find("no known magic bytes") != std::string::npos;
            }
            std::remove(path);
            CHECK(rejected);
        }

        // A PNG signature with a broken body IS dispatched as PNG, and the failure names the format —
        // that is the difference between "unknown format" and "the png decoder failed".
        {
            const char* const path = "gui_paint_test_broken.png";
            {
                std::ofstream out{ path, std::ios::binary };
                out << "\x89PNG\r\n\x1A\n" << "not really a png";
            }
            bool named = false;
            try {
                (void)load_image(path);
            } catch (const std::runtime_error& error) {
                named = std::string(error.what()).find("png") != std::string::npos;
            }
            std::remove(path);
            CHECK(named);
        }

        // The default decoder is the SDL backend, and the seam is replaceable.
        CHECK(image_decoder() != nullptr);
        CHECK(image_decoder()->name() == "sdl");
        for (const image_format_enum format :
             { image_format_enum::BMP, image_format_enum::PNG, image_format_enum::JPEG }) {
            CHECK(image_decoder()->supports(format));
        }
        CHECK(!image_decoder()->supports(image_format_enum::UNKNOWN));
    }

    if (g_failures == 0) {
        std::printf("gui paint: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("gui paint: %d of %d checks failed\n", g_failures, g_checks);
    return 1;
}
