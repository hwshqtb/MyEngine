// window_to_pixel_point(): the WINDOW-coordinate -> render/pixel-space conversion a pointer makes
// when it leaves SDL's event space. The function is pure constexpr integer arithmetic, so every
// ratio (1, 1.5, 2, and a shrinking 2/3) and every edge (degenerate extents, negative points, int
// saturation) is pinned here without a window and without a display.
//
// The rule under test: floor(window * pixel / window) per axis, on a 64-bit intermediate. Floor,
// not truncation and not nearest: the result names the pixel CELL that contains the logical point,
// so a hit test cannot land one cell to the right or below.

#include <climits>
#include <cstdio>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/window_coordinate.h"

using namespace hwshqtb::gui;

namespace window_coordinate_test {

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

/// Compile-time proof that the conversion is usable in constant expressions — the reason it is
/// header-only — and that the rounding rule is the one the contract states. The 1.5 cases are the
/// ones a HiDPI backend needs; the identity case is the only one Windows reports.
static_assert(noexcept(window_to_pixel_point(point_t{}, extent_t{}, extent_t{})));
static_assert(window_to_pixel_point(point_t{ 100, 60 }, extent_t{ 200, 120 }, extent_t{ 300, 180 })
              == point_t{ 150, 90 });
static_assert(window_to_pixel_point(point_t{ 0, 0 }, extent_t{ 200, 120 }, extent_t{ 300, 180 })
              == point_t{ 0, 0 });
static_assert(window_to_pixel_point(point_t{ 199, 119 }, extent_t{ 200, 120 }, extent_t{ 300, 180 })
              == point_t{ 298, 178 });
static_assert(window_to_pixel_point(point_t{ -1, -1 }, extent_t{ 200, 120 }, extent_t{ 300, 180 })
              == point_t{ -2, -2 });
static_assert(window_to_pixel_point(point_t{ 7, -9 }, extent_t{ 200, 120 }, extent_t{ 200, 120 })
              == point_t{ 7, -9 });

}  // namespace window_coordinate_test

using namespace window_coordinate_test;

int main() {
    std::printf("== window_to_pixel_point: WINDOW coordinates -> render/pixel space ==\n");

    // ---- (1) density 1 (W == P): the identity, for interior points, edges and negatives ----
    // This is the case every Windows backend and every driver the tests run under reports, and the
    // reason the fix cannot change the current behaviour there.
    {
        const extent_t extent{ 200, 120 };
        CHECK((window_to_pixel_point(point_t{ 0, 0 }, extent, extent) == point_t{ 0, 0 }));
        CHECK((window_to_pixel_point(point_t{ 100, 60 }, extent, extent) == point_t{ 100, 60 }));
        CHECK((window_to_pixel_point(point_t{ 199, 119 }, extent, extent) == point_t{ 199, 119 }));
        CHECK((window_to_pixel_point(point_t{ -1, -1 }, extent, extent) == point_t{ -1, -1 }));
        CHECK((window_to_pixel_point(point_t{ -200, -120 }, extent, extent) == point_t{ -200, -120 }));
        // The identity holds per axis even when the sides are large: no intermediate overflows.
        const extent_t huge{ INT_MAX, INT_MAX };
        CHECK((window_to_pixel_point(point_t{ INT_MAX, INT_MIN }, huge, huge)
               == point_t{ INT_MAX, INT_MIN }));
    }

    // ---- (2) density 1.5 (W=200x120 -> P=300x180): the mandated values and both edges ----
    {
        const extent_t window_extent{ 200, 120 };
        const extent_t pixel_extent{ 300, 180 };
        CHECK((window_to_pixel_point(point_t{ 0, 0 }, window_extent, pixel_extent) == point_t{ 0, 0 }));
        CHECK((window_to_pixel_point(point_t{ 1, 1 }, window_extent, pixel_extent) == point_t{ 1, 1 }));
        CHECK((window_to_pixel_point(point_t{ 2, 2 }, window_extent, pixel_extent) == point_t{ 3, 3 }));
        CHECK((window_to_pixel_point(point_t{ 10, 6 }, window_extent, pixel_extent) == point_t{ 15, 9 }));
        CHECK((window_to_pixel_point(point_t{ 100, 60 }, window_extent, pixel_extent)
               == point_t{ 150, 90 }));
        // The last logical cell of a 200x120 client area starts at (199,119) and must map to the
        // pixel cell that starts at (298,178): 298.5 and 178.5 floor to 298 and 178.
        CHECK((window_to_pixel_point(point_t{ 199, 119 }, window_extent, pixel_extent)
               == point_t{ 298, 178 }));
        // One logical step past the half-open client area maps to exactly one pixel step past the
        // pixel cell: the axes stay aligned across the edge.
        CHECK((window_to_pixel_point(point_t{ 200, 120 }, window_extent, pixel_extent)
               == point_t{ 300, 180 }));
    }

    // ---- (3) density 2 (W=200x120 -> P=400x240): every axis doubles ----
    {
        const extent_t window_extent{ 200, 120 };
        const extent_t pixel_extent{ 400, 240 };
        CHECK((window_to_pixel_point(point_t{ 0, 0 }, window_extent, pixel_extent) == point_t{ 0, 0 }));
        CHECK((window_to_pixel_point(point_t{ 37, 41 }, window_extent, pixel_extent) == point_t{ 74, 82 }));
        CHECK((window_to_pixel_point(point_t{ 100, 60 }, window_extent, pixel_extent)
               == point_t{ 200, 120 }));
        CHECK((window_to_pixel_point(point_t{ 199, 119 }, window_extent, pixel_extent)
               == point_t{ 398, 238 }));
        CHECK((window_to_pixel_point(point_t{ -3, -4 }, window_extent, pixel_extent)
               == point_t{ -6, -8 }));
    }

    // ---- (4) density below 1 (W=300x180 -> P=200x120): a shrinking display ----
    {
        const extent_t window_extent{ 300, 180 };
        const extent_t pixel_extent{ 200, 120 };
        CHECK((window_to_pixel_point(point_t{ 0, 0 }, window_extent, pixel_extent) == point_t{ 0, 0 }));
        CHECK((window_to_pixel_point(point_t{ 1, 1 }, window_extent, pixel_extent) == point_t{ 0, 0 }));
        CHECK((window_to_pixel_point(point_t{ 100, 60 }, window_extent, pixel_extent)
               == point_t{ 66, 40 }));
        CHECK((window_to_pixel_point(point_t{ 150, 90 }, window_extent, pixel_extent)
               == point_t{ 100, 60 }));
        CHECK((window_to_pixel_point(point_t{ 299, 179 }, window_extent, pixel_extent)
               == point_t{ 199, 119 }));
    }

    // ---- (5) FLOOR, not truncation: negative coordinates round towards negative infinity ----
    // C++ integer division truncates towards zero, which would place -1.5 in the cell starting at -1;
    // the cell that CONTAINS -1.5 starts at -2. These assertions are the ones a truncating
    // implementation fails.
    {
        const extent_t window_extent{ 200, 120 };
        const extent_t pixel_extent{ 300, 180 };
        CHECK((window_to_pixel_point(point_t{ -1, -1 }, window_extent, pixel_extent)
               == point_t{ -2, -2 }));
        CHECK((window_to_pixel_point(point_t{ -2, -2 }, window_extent, pixel_extent)
               == point_t{ -3, -3 }));  // -3 exactly
        CHECK((window_to_pixel_point(point_t{ -3, -3 }, window_extent, pixel_extent)
               == point_t{ -5, -5 }));  // -4.5 floors to -5, truncation would say -4
        CHECK((window_to_pixel_point(point_t{ -199, -119 }, window_extent, pixel_extent)
               == point_t{ -299, -179 }));  // -298.5 / -178.5
        CHECK((window_to_pixel_point(point_t{ -200, -120 }, window_extent, pixel_extent)
               == point_t{ -300, -180 }));  // -300 exactly
        // The shrinking ratio has the same rule: -2 * 2/3 is -1.333..., i.e. the cell at -2.
        const extent_t shrink_window{ 300, 180 };
        const extent_t shrink_pixel{ 200, 120 };
        CHECK((window_to_pixel_point(point_t{ -1, -1 }, shrink_window, shrink_pixel)
               == point_t{ -1, -1 }));
        CHECK((window_to_pixel_point(point_t{ -2, -2 }, shrink_window, shrink_pixel)
               == point_t{ -2, -2 }));
        CHECK((window_to_pixel_point(point_t{ -3, -3 }, shrink_window, shrink_pixel)
               == point_t{ -2, -2 }));  // -2 exactly
        CHECK((window_to_pixel_point(point_t{ -4, -4 }, shrink_window, shrink_pixel)
               == point_t{ -3, -3 }));  // -2.666 floors to -3
    }

    // ---- (6) degenerate extents: never divide by zero, and never move the point ----
    // The chosen fallback is the identity. It is the answer a density of 1 gives, so a window that
    // reports no size (before the first frame, or during destruction) cannot put the pointer in a
    // corner it was not pointing at.
    {
        const point_t point{ 123, 45 };
        CHECK((window_to_pixel_point(point, extent_t{ 0, 0 }, extent_t{ 300, 180 }) == point));
        CHECK((window_to_pixel_point(point, extent_t{ 200, 120 }, extent_t{ 0, 0 }) == point));
        CHECK((window_to_pixel_point(point, extent_t{ 0, 120 }, extent_t{ 300, 180 }) == point));
        CHECK((window_to_pixel_point(point, extent_t{ 200, 0 }, extent_t{ 300, 180 }) == point));
        CHECK((window_to_pixel_point(point, extent_t{ -200, 120 }, extent_t{ 300, 180 }) == point));
        CHECK((window_to_pixel_point(point, extent_t{ 200, 120 }, extent_t{ 300, -180 }) == point));
        CHECK((window_to_pixel_point(point, extent_t{ 0, 0 }, extent_t{ 0, 0 }) == point));
        CHECK((window_to_pixel_point(point_t{ 0, 0 }, extent_t{ 0, 0 }, extent_t{ 300, 180 })
               == point_t{ 0, 0 }));
    }

    // ---- (7) the int range: a 64-bit product, a saturated result, no overflow ----
    {
        const extent_t unit{ 1, 1 };
        const extent_t huge{ INT_MAX, INT_MAX };
        CHECK((window_to_pixel_point(point_t{ 0, 0 }, unit, huge) == point_t{ 0, 0 }));
        // INT_MAX * INT_MAX is ~4.6e18: it fits in the 64-bit intermediate, and the result saturates.
        CHECK((window_to_pixel_point(point_t{ INT_MAX, INT_MAX }, unit, huge)
               == point_t{ INT_MAX, INT_MAX }));
        CHECK((window_to_pixel_point(point_t{ INT_MIN, INT_MIN }, unit, huge)
               == point_t{ INT_MIN, INT_MIN }));
        // A ratio inside the int range does not saturate even with the same huge operands.
        CHECK((window_to_pixel_point(point_t{ INT_MAX, INT_MIN }, huge, huge)
               == point_t{ INT_MAX, INT_MIN }));
    }

    if (g_failures == 0) {
        std::printf("window_coordinate: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("window_coordinate: %d of %d checks failed\n", g_failures, g_checks);
    return 1;
}
