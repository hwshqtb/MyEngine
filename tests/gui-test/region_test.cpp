// region_t: the disjoint-rectangle set the repaint machinery will build on. This test pins the
// class contract directly (the three invariants: no overlap with full-edge merging, the y()/x()
// order, and the degrading-to-bounds direction), plus the area/coverage algebra every later dirty
// region step relies on.
//
// Pure value algebra: no window, no display and no SDL, so the test links gui only for the
// out-of-line members of region_t.

#include <cstddef>
#include <cstdio>
#include <span>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/region_t.h"

using namespace hwshqtb::gui;

namespace region_test {

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

/// The first invariant: no pixel is covered twice, so the sum of the areas is the real area.
bool pairwise_disjoint(const region_t& region) {
    const std::span<const rect_t> rects = region.rects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        for (std::size_t j = i + 1; j < rects.size(); ++j) {
            if (!rects[i].intersected(rects[j]).isEmpty()) {
                return false;
            }
        }
    }
    return true;
}

/// The second invariant: by y() first, by x() second.
bool ordered_by_y_then_x(const region_t& region) {
    const std::span<const rect_t> rects = region.rects();
    for (std::size_t i = 1; i < rects.size(); ++i) {
        if (rects[i - 1].y() > rects[i].y()) {
            return false;
        }
        if (rects[i - 1].y() == rects[i].y() && rects[i - 1].x() > rects[i].x()) {
            return false;
        }
    }
    return true;
}

/// The first invariant, second half: no pair could be merged without adding a single pixel, i.e.
/// no pair shares a full edge and neither contains the other.
bool no_mergeable_pair(const region_t& region) {
    const std::span<const rect_t> rects = region.rects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        for (std::size_t j = i + 1; j < rects.size(); ++j) {
            const rect_t a = rects[i];
            const rect_t b = rects[j];
            const bool a_has_b = a.left() <= b.left() && b.right() <= a.right() && a.top() <= b.top() && b.bottom() <= a.bottom();
            const bool b_has_a = b.left() <= a.left() && a.right() <= b.right() && b.top() <= a.top() && a.bottom() <= b.bottom();
            const bool side_by_side = a.top() == b.top() && a.height() == b.height() && (a.right() == b.left() || b.right() == a.left());
            const bool stacked = a.left() == b.left() && a.width() == b.width() && (a.bottom() == b.top() || b.bottom() == a.top());
            if (a_has_b || b_has_a || side_by_side || stacked) {
                return false;
            }
        }
    }
    return true;
}

/// The three invariants as one predicate, for regions whose exact decomposition is not the point.
bool region_well_formed(const region_t& region) {
    return pairwise_disjoint(region) && ordered_by_y_then_x(region) && no_mergeable_pair(region) && region.rectangle_count() <= region_t::max_rectangles();
}

/// Whether two regions hold the same rectangles in the same order.
bool same_rects(const region_t& a, const region_t& b) {
    if (a.rectangle_count() != b.rectangle_count()) {
        return false;
    }
    const std::span<const rect_t> left = a.rects();
    const std::span<const rect_t> right = b.rects();
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (!(left[i] == right[i])) {
            return false;
        }
    }
    return true;
}

/// Whether any rectangle of @p a overlaps any rectangle of @p b.
bool overlaps_any(const region_t& a, const region_t& b) {
    const std::span<const rect_t> left = a.rects();
    const std::span<const rect_t> right = b.rects();
    for (const rect_t& l : left) {
        for (const rect_t& r : right) {
            if (!l.intersected(r).isEmpty()) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace region_test

using namespace region_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    std::printf("== region_t: disjoint rectangle sets ==\n");

    // ---- (1) the empty region: nothing covered, bounds is the default rectangle ----
    {
        const region_t empty;
        CHECK(empty.is_empty());
        CHECK(empty.rects().empty());
        CHECK(empty.rectangle_count() == 0);
        CHECK(empty.area() == 0);
        CHECK((empty.bounds() == rect_t{}));
        CHECK(region_well_formed(empty));
    }

    // ---- (2) one rectangle, and empty/inverted input rectangles are ignored ----
    {
        const region_t single{ rect_t{ 2, 3, 4, 5 } };
        CHECK(!single.is_empty());
        CHECK(single.rectangle_count() == 1);
        CHECK(single.rects().size() == 1);
        CHECK((single.rects().front() == rect_t{ 2, 3, 4, 5 }));
        CHECK(single.area() == 20);
        CHECK((single.bounds() == rect_t{ 2, 3, 4, 5 }));
        CHECK(region_well_formed(single));

        const region_t negative_width{ rect_t{ 1, 1, -2, 4 } };
        const region_t negative_height{ rect_t{ 1, 1, 4, -2 } };
        const region_t zero_width{ rect_t{ 1, 1, 0, 4 } };
        const region_t zero_height{ rect_t{ 1, 1, 4, 0 } };
        CHECK(negative_width.is_empty());
        CHECK(negative_height.is_empty());
        CHECK(zero_width.is_empty());
        CHECK(zero_height.is_empty());

        region_t reset;
        reset.set(rect_t{ 1, 2, 3, 4 });
        CHECK((reset.rects().front() == rect_t{ 1, 2, 3, 4 }));
        reset.set(rect_t{ 1, 2, -3, 4 });
        CHECK(reset.is_empty());

        // unite() ignores an inverted rectangle instead of emptying the region.
        region_t filtered{ rect_t{ 0, 0, 4, 4 } };
        filtered.unite(rect_t{ 10, 10, -1, 4 });
        CHECK(filtered.rectangle_count() == 1);
        CHECK(filtered.area() == 16);

        // subtracted() ignores it too: the result equals the source.
        const region_t kept = region_t{ rect_t{ 0, 0, 4, 4 } }.subtracted(rect_t{ 1, 1, -1, -1 });
        CHECK(kept.rectangle_count() == 1);
        CHECK(kept.area() == 16);

        // intersect() with an inverted rectangle keeps no pixel: nothing is covered by both.
        filtered.intersect(rect_t{ 0, 0, 0, 0 });
        CHECK(filtered.is_empty());
    }

    // ---- (3) two disjoint rectangles stay two, ordered by y() and then by x() ----
    {
        region_t ordered;
        ordered.unite(rect_t{ 10, 6, 2, 2 });  // inserted out of order on purpose
        CHECK(ordered.rectangle_count() == 1);
        ordered.unite(rect_t{ 2, 4, 2, 2 });
        ordered.unite(rect_t{ 6, 4, 2, 2 });
        CHECK(ordered.rectangle_count() == 3);
        CHECK((ordered.rects()[0] == rect_t{ 2, 4, 2, 2 }));
        CHECK((ordered.rects()[1] == rect_t{ 6, 4, 2, 2 }));
        CHECK((ordered.rects()[2] == rect_t{ 10, 6, 2, 2 }));
        CHECK(ordered.area() == 12);
        CHECK((ordered.bounds() == rect_t{ 2, 4, 10, 4 }));
        CHECK(region_well_formed(ordered));

        region_t pair;
        pair.unite(rect_t{ 5, 0, 2, 2 });
        pair.unite(rect_t{ 0, 0, 2, 2 });
        CHECK(pair.rectangle_count() == 2);
        CHECK((pair.rects()[0] == rect_t{ 0, 0, 2, 2 }));
        CHECK((pair.rects()[1] == rect_t{ 5, 0, 2, 2 }));
        CHECK(pair.area() == 8);
    }

    // ---- (4) two overlapping rectangles become one (the union is exactly a rectangle) ----
    {
        region_t identical;
        identical.unite(rect_t{ 1, 1, 4, 4 });
        identical.unite(rect_t{ 1, 1, 4, 4 });
        CHECK(identical.rectangle_count() == 1);
        CHECK(identical.area() == 16);

        region_t covering;
        covering.unite(rect_t{ 1, 1, 4, 4 });
        covering.unite(rect_t{ 2, 2, 1, 1 });
        CHECK(covering.rectangle_count() == 1);
        CHECK((covering.rects().front() == rect_t{ 1, 1, 4, 4 }));

        // Partially overlapping along x: the union is still one rectangle.
        region_t side_overlap;
        side_overlap.unite(rect_t{ 0, 0, 4, 4 });
        side_overlap.unite(rect_t{ 2, 0, 4, 4 });
        CHECK(side_overlap.rectangle_count() == 1);
        CHECK((side_overlap.rects().front() == rect_t{ 0, 0, 6, 4 }));
        CHECK(side_overlap.area() == 24);

        // The same in the opposite insertion order.
        region_t side_overlap_reverse;
        side_overlap_reverse.unite(rect_t{ 2, 0, 4, 4 });
        side_overlap_reverse.unite(rect_t{ 0, 0, 4, 4 });
        CHECK(side_overlap_reverse.rectangle_count() == 1);
        CHECK((side_overlap_reverse.rects().front() == rect_t{ 0, 0, 6, 4 }));
    }

    // ---- (5) two rectangles sharing a full edge become one (both directions, plus a chain) ----
    {
        region_t horizontal;
        horizontal.unite(rect_t{ 0, 0, 2, 2 });
        horizontal.unite(rect_t{ 2, 0, 2, 2 });
        CHECK(horizontal.rectangle_count() == 1);
        CHECK((horizontal.rects().front() == rect_t{ 0, 0, 4, 2 }));
        CHECK(horizontal.area() == 8);

        region_t vertical;
        vertical.unite(rect_t{ 0, 2, 2, 2 });  // lower one first
        vertical.unite(rect_t{ 0, 0, 2, 2 });
        CHECK(vertical.rectangle_count() == 1);
        CHECK((vertical.rects().front() == rect_t{ 0, 0, 2, 4 }));
        CHECK(vertical.area() == 8);

        // Three strips with a gap: filling the gap must cascade into one rectangle, which only a
        // re-sorting merge pass (not a single scan) can find.
        region_t chain;
        chain.unite(rect_t{ 0, 0, 1, 1 });
        chain.unite(rect_t{ 2, 0, 1, 1 });
        CHECK(chain.rectangle_count() == 2);
        chain.unite(rect_t{ 1, 0, 1, 1 });
        CHECK(chain.rectangle_count() == 1);
        CHECK((chain.rects().front() == rect_t{ 0, 0, 3, 1 }));
        CHECK(chain.area() == 3);
    }

    // ---- (6) an L shape stays two disjoint rectangles, and its area is the union's area ----
    {
        region_t ell;
        ell.unite(rect_t{ 0, 0, 4, 2 });
        ell.unite(rect_t{ 0, 2, 2, 2 });
        CHECK(ell.rectangle_count() == 2);
        CHECK((ell.rects()[0] == rect_t{ 0, 0, 4, 2 }));
        CHECK((ell.rects()[1] == rect_t{ 0, 2, 2, 2 }));
        CHECK(ell.area() == 12);  // 4x2 + 2x2, disjoint so the sum is exact
        CHECK((ell.bounds() == rect_t{ 0, 0, 4, 4 }));
        CHECK(pairwise_disjoint(ell));
        CHECK(region_well_formed(ell));

        region_t ell_reverse;
        ell_reverse.unite(rect_t{ 0, 2, 2, 2 });
        ell_reverse.unite(rect_t{ 0, 0, 4, 2 });
        CHECK(same_rects(ell_reverse, ell));

        // Corner overlap: the union is a staircase, three rectangles, still exact.
        region_t staircase;
        staircase.unite(rect_t{ 0, 0, 4, 4 });
        staircase.unite(rect_t{ 2, 2, 4, 4 });
        CHECK(staircase.rectangle_count() == 3);
        CHECK(staircase.area() == 28);  // 16 + 16 - 4
        CHECK((staircase.bounds() == rect_t{ 0, 0, 6, 6 }));
        CHECK(region_well_formed(staircase));
    }

    // ---- (7) intersect(): partial overlap, no overlap, containment, superset, and re-merging ----
    {
        const region_t base{ rect_t{ 0, 0, 10, 10 } };

        region_t partial = base;
        partial.intersect(rect_t{ 5, 5, 10, 10 });
        CHECK(partial.rectangle_count() == 1);
        CHECK((partial.rects().front() == rect_t{ 5, 5, 5, 5 }));
        CHECK(partial.area() == 25);

        region_t none = base;
        none.intersect(rect_t{ 20, 20, 5, 5 });
        CHECK(none.is_empty());
        CHECK(none.area() == 0);

        region_t contained = base;
        contained.intersect(rect_t{ 2, 2, 3, 3 });
        CHECK(contained.rectangle_count() == 1);
        CHECK((contained.rects().front() == rect_t{ 2, 2, 3, 3 }));

        region_t superset = base;
        superset.intersect(rect_t{ -5, -5, 30, 30 });
        CHECK(superset.rectangle_count() == 1);
        CHECK((superset.rects().front() == rect_t{ 0, 0, 10, 10 }));
        CHECK(superset.area() == 100);

        // An intersection may make two rectangles adjacent; normalize() has to merge them.
        region_t column = region_t{ rect_t{ 0, 0, 4, 2 } };
        column.unite(rect_t{ 0, 2, 2, 2 });
        column.intersect(rect_t{ 0, 0, 2, 4 });
        CHECK(column.rectangle_count() == 1);
        CHECK((column.rects().front() == rect_t{ 0, 0, 2, 4 }));
        CHECK(column.area() == 8);

        region_t two_columns;
        two_columns.unite(rect_t{ 0, 0, 2, 6 });
        two_columns.unite(rect_t{ 4, 0, 2, 6 });
        two_columns.intersect(rect_t{ 0, 1, 10, 2 });
        CHECK(two_columns.rectangle_count() == 2);
        CHECK((two_columns.rects()[0] == rect_t{ 0, 1, 2, 2 }));
        CHECK((two_columns.rects()[1] == rect_t{ 4, 1, 2, 2 }));
        CHECK(two_columns.area() == 8);
        CHECK(region_well_formed(two_columns));
    }

    // ---- (8) subtracted(rect_t): hole in the middle (4), an edge (1), full cover, no overlap ----
    {
        const region_t canvas{ rect_t{ 0, 0, 10, 10 } };

        const region_t holed = canvas.subtracted(rect_t{ 3, 3, 4, 4 });
        CHECK(holed.rectangle_count() == 4);
        CHECK((holed.rects()[0] == rect_t{ 0, 0, 10, 3 }));
        CHECK((holed.rects()[1] == rect_t{ 0, 3, 3, 4 }));
        CHECK((holed.rects()[2] == rect_t{ 7, 3, 3, 4 }));
        CHECK((holed.rects()[3] == rect_t{ 0, 7, 10, 3 }));
        CHECK(holed.area() == 84);  // 100 - 16
        CHECK((holed.bounds() == rect_t{ 0, 0, 10, 10 }));
        CHECK(region_well_formed(holed));
        CHECK(canvas.rectangle_count() == 1);  // the source is untouched

        const region_t edge = canvas.subtracted(rect_t{ 0, 0, 10, 3 });
        CHECK(edge.rectangle_count() == 1);
        CHECK((edge.rects().front() == rect_t{ 0, 3, 10, 7 }));
        CHECK(edge.area() == 70);

        const region_t covered = canvas.subtracted(rect_t{ -5, -5, 30, 30 });
        CHECK(covered.is_empty());
        CHECK(covered.area() == 0);

        const region_t untouched = canvas.subtracted(rect_t{ 20, 20, 5, 5 });
        CHECK(untouched.rectangle_count() == 1);
        CHECK((untouched.rects().front() == rect_t{ 0, 0, 10, 10 }));
        CHECK(untouched.area() == 100);

        // A cut spanning two rectangles carves both and keeps the survivors apart.
        region_t two_blocks;
        two_blocks.unite(rect_t{ 0, 0, 4, 4 });
        two_blocks.unite(rect_t{ 6, 0, 4, 4 });
        const region_t slitted = two_blocks.subtracted(rect_t{ 3, -1, 4, 6 });
        CHECK(slitted.rectangle_count() == 2);
        CHECK((slitted.rects()[0] == rect_t{ 0, 0, 3, 4 }));
        CHECK((slitted.rects()[1] == rect_t{ 7, 0, 3, 4 }));
        CHECK(slitted.area() == 24);
        CHECK(region_well_formed(slitted));
    }

    // ---- (9) subtracted(region_t) agrees with subtracting the rectangles one by one ----
    {
        region_t source;
        source.unite(rect_t{ 0, 0, 6, 6 });
        source.unite(rect_t{ 10, 0, 4, 4 });
        source.unite(rect_t{ 0, 8, 4, 2 });
        CHECK(source.rectangle_count() == 3);
        CHECK(source.area() == 60);  // 36 + 16 + 8

        region_t cuts;
        cuts.unite(rect_t{ 2, 2, 4, 4 });
        cuts.unite(rect_t{ 10, 0, 2, 4 });
        CHECK(cuts.rectangle_count() == 2);

        const region_t whole = source.subtracted(cuts);
        CHECK(whole.area() == 36);  // 60 - 16 - 8
        CHECK(region_well_formed(whole));
        CHECK(!overlaps_any(whole, cuts));

        region_t per_rect = source;
        for (const rect_t& cut : cuts.rects()) {
            per_rect = per_rect.subtracted(cut);
        }
        CHECK(per_rect.area() == whole.area());
        CHECK(same_rects(per_rect, whole));

        // Subtracting an empty region is a copy, not a normalization pass that changes anything.
        const region_t no_cuts = source.subtracted(region_t{});
        CHECK(same_rects(no_cuts, source));
    }

    // ---- (10) degradation: past max_rectangles() the region becomes its bounding rectangle ----
    {
        CHECK(region_t::max_rectangles() == 64);

        region_t capped;
        for (int i = 0; i < 64; ++i) {
            capped.unite(rect_t{ i * 2, 0, 1, 1 });
        }
        CHECK(capped.rectangle_count() == 64);  // exactly at the cap: still exact
        CHECK(capped.area() == 64);
        CHECK(region_well_formed(capped));

        capped.unite(rect_t{ 128, 0, 1, 1 });  // the 65th rectangle forces the fallback
        CHECK(capped.rectangle_count() == 1);
        CHECK((capped.bounds() == rect_t{ 0, 0, 129, 1 }));
        CHECK(capped.area() == 129);
        CHECK(capped.area() == static_cast<std::size_t>(capped.bounds().width()) * static_cast<std::size_t>(capped.bounds().height()));
        CHECK(capped.area() >= 65);  // degrading may add area, never lose it

        // Two capped rows united: 40 + 40 disjoint rectangles exceed the cap in one operation.
        region_t top_row;
        region_t bottom_row;
        for (int i = 0; i < 40; ++i) {
            top_row.unite(rect_t{ i * 2, 0, 1, 1 });
            bottom_row.unite(rect_t{ i * 2, 5, 1, 1 });
        }
        CHECK(top_row.rectangle_count() == 40);
        CHECK(bottom_row.rectangle_count() == 40);

        top_row.unite(bottom_row);
        CHECK(top_row.rectangle_count() == 1);
        CHECK((top_row.bounds() == rect_t{ 0, 0, 79, 6 }));
        CHECK(top_row.area() == 79u * 6u);
        CHECK(top_row.area() == static_cast<std::size_t>(top_row.bounds().width()) * static_cast<std::size_t>(top_row.bounds().height()));
        CHECK(region_well_formed(top_row));
    }

    // ---- (11) the rects() invariants hold on a region built by mixed operations ----
    {
        const region_t mixed{ rect_t{ 0, 0, 12, 12 } };
        region_t punched = mixed.subtracted(rect_t{ 4, 4, 4, 4 });
        punched = punched.subtracted(rect_t{ 0, 0, 2, 2 });
        punched.unite(rect_t{ 20, 20, 3, 3 });
        punched.intersect(rect_t{ -1, -1, 25, 25 });
        CHECK(punched.rectangle_count() == 6);
        CHECK(punched.area() == 144 - 16 - 4 + 9);
        CHECK(pairwise_disjoint(punched));
        CHECK(ordered_by_y_then_x(punched));
        CHECK(region_well_formed(punched));

        for (std::size_t i = 0; i < punched.rects().size(); ++i) {
            for (std::size_t j = i + 1; j < punched.rects().size(); ++j) {
                CHECK(punched.rects()[i].intersected(punched.rects()[j]).isEmpty());
            }
        }
    }

    // ---- (12) idempotence: repeating an operation on its own result changes nothing ----
    {
        region_t ell;
        ell.unite(rect_t{ 0, 0, 4, 2 });
        ell.unite(rect_t{ 0, 2, 2, 2 });
        const region_t before = ell;

        ell.unite(rect_t{ 0, 0, 4, 2 });
        CHECK(same_rects(ell, before));
        ell.unite(rect_t{ 0, 2, 2, 2 });
        CHECK(same_rects(ell, before));
        CHECK(ell.area() == before.area());

        // Uniting a region with itself, or with an empty one, is a no-op.
        region_t self_union = before;
        self_union.unite(self_union);
        CHECK(same_rects(self_union, before));
        region_t empty_union = before;
        empty_union.unite(region_t{});
        CHECK(same_rects(empty_union, before));

        // intersect() is idempotent too.
        region_t cut = before;
        cut.intersect(rect_t{ 0, 0, 2, 4 });
        const region_t cut_once = cut;
        cut.intersect(rect_t{ 0, 0, 2, 4 });
        CHECK(same_rects(cut, cut_once));

        // And so is subtracted().
        const region_t cut_away = before.subtracted(rect_t{ 0, 0, 2, 2 });
        const region_t cut_away_twice = cut_away.subtracted(rect_t{ 0, 0, 2, 2 });
        CHECK(same_rects(cut_away_twice, cut_away));
    }

    // ---- (13) translated_within(): the damage inside a moved rectangle moves with the pixels ----
    {
        const rect_t area{ 0, 0, 10, 10 };

        // A rectangle fully inside moves whole, in either axis, and keeps its area.
        const region_t inside{ rect_t{ 3, 3, 2, 2 } };
        CHECK(same_rects(inside.translated_within(area, 0, -2), region_t{ rect_t{ 3, 1, 2, 2 } }));
        CHECK(same_rects(inside.translated_within(area, -2, 0), region_t{ rect_t{ 1, 3, 2, 2 } }));

        // A rectangle outside the moved area describes pixels that did not move: it keeps its place.
        const region_t outside{ rect_t{ 20, 20, 2, 2 } };
        CHECK(same_rects(outside.translated_within(area, 0, -2), outside));

        // The moved part is clipped back into the area: what leaves it was shifted away for good.
        const region_t clipped = region_t{ rect_t{ 2, 2, 4, 4 } }.translated_within(area, 0, -3);
        CHECK(same_rects(clipped, region_t{ rect_t{ 2, 0, 4, 3 } }));
        CHECK(clipped.area() == 12); // one of the four rows was dropped
        CHECK(region_well_formed(clipped));

        // A shift of at least the extent keeps nothing inside: the area is fully exposed instead.
        CHECK(region_t{ area }.translated_within(area, 0, -10).is_empty());
        CHECK(region_t{ area }.translated_within(area, 0, 10).is_empty());
        CHECK(region_t{ area }.translated_within(area, -10, 0).is_empty());

        // A shift that carries the whole rectangle out of the area drops it as well.
        CHECK(region_t{ inside }.translated_within(area, 0, 10).is_empty());

        // A straddling rectangle: the part outside stays, the part inside moves (clipped).
        const region_t straddling = region_t{ rect_t{ 8, 2, 4, 2 } }.translated_within(area, 1, 0);
        CHECK(region_well_formed(straddling));
        CHECK(straddling.area() == 4 + 2); // the 2x2 outside piece + the 1x2 that stayed in the area
        CHECK(!straddling.subtracted(rect_t{ 10, 2, 2, 2 }).is_empty()); // outside piece, unmoved
        CHECK(!straddling.subtracted(rect_t{ 9, 2, 1, 2 }).is_empty()); // moved piece, clipped

        // Mixed: one inside and one outside rectangle are transformed independently.
        region_t mixed = inside;
        mixed.unite(outside);
        const region_t transformed = mixed.translated_within(area, 0, -1);
        CHECK(region_well_formed(transformed));
        CHECK(transformed.area() == 8);
        CHECK(!transformed.subtracted(rect_t{ 3, 2, 2, 2 }).is_empty());
        CHECK(!transformed.subtracted(rect_t{ 20, 20, 2, 2 }).is_empty());
        CHECK(transformed.subtracted(area).subtracted(outside.bounds()).is_empty());

        // A zero shift, an empty area and an empty region are copies.
        CHECK(same_rects(inside.translated_within(area, 0, 0), inside));
        CHECK(same_rects(inside.translated_within(rect_t{}, 0, -2), inside));
        CHECK(region_t{}.translated_within(area, 0, -2).is_empty());
        const region_t two{ rect_t{ 1, 8, 2, 2 } };
        CHECK(same_rects(two.translated_within(area, 0, 0), two));

        // Several rectangles: each keeps the invariants after the transform, and one that stays inside
        // keeps its area.
        region_t many;
        many.unite(rect_t{ 2, 2, 2, 2 });
        many.unite(rect_t{ 5, 5, 2, 2 });
        many.unite(rect_t{ 7, 1, 2, 2 });
        const region_t moved_many = many.translated_within(area, 0, -1);
        CHECK(region_well_formed(moved_many));
        CHECK(moved_many.area() == many.area());
    }

    if (g_failures == 0) {
        std::printf("region: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("region: %d of %d checks failed\n", g_failures, g_checks);
    return 1;
}