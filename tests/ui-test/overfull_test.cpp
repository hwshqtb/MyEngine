// Space shortage: the Qt qGeomCalc overfull rule (cut the biggest first, still fit the parent) and
// the per-policy compression floor of effective_minimum_size().
//
// The six sections pin:
//  (1) the overfull rule: the row fits the parent, the biggest minimum loses the most, the smaller
//      ones are untouched; an EXPANDING child is cut exactly like a PREFERRED one;
//  (2) the extreme cases: a parent narrower than one minimum, an empty parent, and a spacing wider
//      than the parent (the gap is the caller's and cannot be shrunk here);
//  (3) the per-policy floor: FIXED = max(size_hint, minimum_size_hint), PREFERRED / EXPANDING =
//      minimum_size_hint, both clamped into [minimum_size, maximum_size];
//  (4) the interaction: a FIXED child holds its hint while the PREFERRED ones absorb the deficit,
//      and is cut like everybody else once even the floors do not fit;
//  (5) the maximum stays an upper bound on the overfull path;
//  (6) the regression: no constraint and enough space => the placement does not move, and the band
//      between the floors and the hints still shrinks proportionally.
//
// No window and no video subsystem: widget_t and the layouts are pure offscreen code.

#include <cstdio>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/ui/box_layout_t.h"
#include "hwshqtb/ui/grid_layout_t.h"
#include "hwshqtb/ui/item_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using hwshqtb::ui::grid_layout_t;
using hwshqtb::ui::hbox_layout_t;
using hwshqtb::ui::size_policy_enum;
using hwshqtb::ui::vbox_layout_t;
using hwshqtb::ui::widget_t;

namespace overfull_test {

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

/// @brief A widget whose content opinion is the given hint; its minimum hint is configurable.
class sized_widget_t final: public widget_t {
public:
    sized_widget_t(extent_t hint, extent_t minimum_hint, widget_t* parent = nullptr):
        widget_t(parent),
        _hint(hint),
        _minimum_hint(minimum_hint) { }

    extent_t size_hint() const override {
        return _hint;
    }

    extent_t minimum_size_hint() const override {
        return _minimum_hint;
    }

private:
    extent_t _hint;
    extent_t _minimum_hint;
};

/// @brief A widget whose content is exactly the hint (no separate minimum hint).
class sized_t final: public widget_t {
public:
    sized_t(extent_t hint, widget_t* parent = nullptr):
        widget_t(parent),
        _hint(hint) { }

    extent_t size_hint() const override {
        return _hint;
    }

private:
    extent_t _hint;
};

}  // namespace overfull_test

using namespace overfull_test;

int main() {
    std::printf("== layouts: the overfull rule and the per-policy compression floor ==\n");

    // ---- (1) overfull: the row fits the parent and the biggest minimum loses the most ----
    {
        // Three 50-pixel floors in 100: the deficit (50) is split evenly, the first children take
        // the rounding remainder, and no child stays at its floor.
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 40 });
        sized_widget_t a{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &parent };
        sized_widget_t b{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &parent };
        sized_widget_t c{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &parent };
        c.set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::PREFERRED);
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);

        CHECK(a.effective_minimum_size().width() == 50);
        CHECK(b.effective_minimum_size().width() == 50);
        CHECK(c.effective_minimum_size().width() == 50);   // EXPANDING has the same floor
        CHECK(a.geometry() == rect_t{ 0, 0, 34, 40 });
        CHECK(b.geometry() == rect_t{ 34, 0, 33, 40 });
        CHECK(c.geometry() == rect_t{ 67, 0, 33, 40 });
        CHECK(a.geometry().width() < 50);                  // every floor is broken
        CHECK(b.geometry().width() < 50);
        CHECK(c.geometry().width() < 50);
        CHECK(c.geometry().right() == 100);                // ... and the row fits
        CHECK(c.geometry().right() == parent.size().width());
        CHECK(a.geometry().width() + b.geometry().width() + c.geometry().width() == 100);

        // Floors 10 + 40 + 60 in 100: the two smallest keep their minimum, the 60 one pays the whole
        // 10-pixel deficit — "take from the biggest first", not "shrink everybody equally".
        widget_t uneven;
        uneven.set_geometry(rect_t{ 0, 0, 100, 40 });
        sized_widget_t small{ extent_t{ 80, 20 }, extent_t{ 10, 10 }, &uneven };
        sized_widget_t middle{ extent_t{ 80, 20 }, extent_t{ 40, 10 }, &uneven };
        sized_widget_t big{ extent_t{ 80, 20 }, extent_t{ 60, 10 }, &uneven };
        auto* const uneven_layout = new hbox_layout_t;
        uneven_layout->set_spacing(0);
        uneven.set_layout(uneven_layout);
        CHECK(small.geometry() == rect_t{ 0, 0, 10, 40 });   // floor 10, untouched
        CHECK(middle.geometry() == rect_t{ 10, 0, 40, 40 }); // floor 40, untouched
        CHECK(big.geometry() == rect_t{ 50, 0, 50, 40 });    // floor 60, cut by exactly 10
        CHECK(big.geometry().width() < big.effective_minimum_size().width());
        CHECK(big.geometry().width() < 60);
        CHECK(small.geometry().width() == 10);
        CHECK(middle.geometry().width() == 40);
        CHECK(big.geometry().right() == 100);

        // Two equal big floors plus a small one: the small one is below the common cap and stays,
        // both big ones are cut by the same 45 pixels.
        widget_t two_big;
        two_big.set_geometry(rect_t{ 0, 0, 100, 40 });
        sized_widget_t tiny{ extent_t{ 200, 20 }, extent_t{ 10, 10 }, &two_big };
        sized_widget_t big_one{ extent_t{ 200, 20 }, extent_t{ 90, 10 }, &two_big };
        sized_widget_t big_two{ extent_t{ 200, 20 }, extent_t{ 90, 10 }, &two_big };
        auto* const two_big_layout = new hbox_layout_t;
        two_big_layout->set_spacing(0);
        two_big.set_layout(two_big_layout);
        CHECK(tiny.geometry() == rect_t{ 0, 0, 10, 40 });
        CHECK(big_one.geometry() == rect_t{ 10, 0, 45, 40 });
        CHECK(big_two.geometry() == rect_t{ 55, 0, 45, 40 });
        CHECK(big_one.geometry().width() < 90);
        CHECK(big_two.geometry().width() < 90);
        CHECK(big_two.geometry().right() == 100);
    }

    // ---- (2) the extreme cases: narrower than one minimum, empty, and a spacing wider than the axis ----
    {
        // 20 pixels for three 50-pixel floors: every child ends below its floor, none is negative,
        // and the row still fits 20.
        widget_t tiny_parent;
        tiny_parent.set_geometry(rect_t{ 0, 0, 20, 40 });
        sized_widget_t a{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &tiny_parent };
        sized_widget_t b{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &tiny_parent };
        sized_widget_t c{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &tiny_parent };
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        tiny_parent.set_layout(layout);
        CHECK(a.geometry() == rect_t{ 0, 0, 7, 40 });
        CHECK(b.geometry() == rect_t{ 7, 0, 7, 40 });
        CHECK(c.geometry() == rect_t{ 14, 0, 6, 40 });
        CHECK(a.geometry().width() >= 0 && b.geometry().width() >= 0 && c.geometry().width() >= 0);
        CHECK(a.geometry().width() > 0 && b.geometry().width() > 0 && c.geometry().width() > 0);
        CHECK(c.geometry().right() == 20);

        // An empty parent: every extent is 0, nothing goes negative.
        widget_t empty;
        empty.set_geometry(rect_t{ 0, 0, 0, 40 });
        sized_widget_t e1{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &empty };
        sized_widget_t e2{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &empty };
        auto* const empty_layout = new hbox_layout_t;
        empty_layout->set_spacing(0);
        empty.set_layout(empty_layout);
        CHECK(e1.geometry() == rect_t{ 0, 0, 0, 40 });
        CHECK(e2.geometry() == rect_t{ 0, 0, 0, 40 });
        CHECK(e2.geometry().right() == 0);

        // The spacing alone is wider than the parent: the gaps are the caller's and are not shrunk
        // (Qt shrinks its spacers proportionally), so every child is emptied and the gaps overrun the
        // 10 pixels. This is the one case that does not fit, by the gaps alone.
        widget_t gapped;
        gapped.set_geometry(rect_t{ 0, 0, 10, 40 });
        sized_widget_t g1{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &gapped };
        sized_widget_t g2{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &gapped };
        sized_widget_t g3{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &gapped };
        auto* const gapped_layout = new hbox_layout_t;
        gapped_layout->set_spacing(15);
        gapped.set_layout(gapped_layout);
        CHECK(g1.geometry() == rect_t{ 0, 0, 0, 40 });
        CHECK(g2.geometry() == rect_t{ 15, 0, 0, 40 });
        CHECK(g3.geometry() == rect_t{ 30, 0, 0, 40 });
        CHECK(g1.geometry().width() == 0 && g2.geometry().width() == 0 && g3.geometry().width() == 0);
    }

    // ---- (3) the floor depends on the size policy (Qt's qSmartMinSize) ----
    {
        // The same opinion two ways: FIXED does not shrink (its floor is the hint), PREFERRED and
        // EXPANDING shrink to their minimum hint. The policy is installed on both axes here, so the
        // whole extent is under its rule.
        sized_widget_t fixed{ extent_t{ 60, 20 }, extent_t{ 10, 10 } };
        sized_widget_t preferred{ extent_t{ 60, 20 }, extent_t{ 10, 10 } };
        sized_widget_t expanding{ extent_t{ 60, 20 }, extent_t{ 10, 10 } };
        fixed.set_size_policy(size_policy_enum::FIXED, size_policy_enum::FIXED);
        expanding.set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::EXPANDING);
        CHECK(fixed.effective_minimum_size() == extent_t{ 60, 20 });
        CHECK(preferred.effective_minimum_size() == extent_t{ 10, 10 });
        CHECK(expanding.effective_minimum_size() == extent_t{ 10, 10 });
        CHECK(fixed.effective_minimum_size() == fixed.effective_size_hint());

        // The hard constraints still clamp a FIXED floor, on both sides.
        fixed.set_minimum_size(extent_t{ 70, 5 });
        CHECK(fixed.effective_minimum_size() == extent_t{ 70, 20 });   // hard minimum wins on width
        fixed.set_maximum_size(extent_t{ 50, 15 });                     // ... and lowers it on both
        CHECK(fixed.minimum_size() == extent_t{ 50, 5 });               // the invariant followed
        CHECK(fixed.effective_minimum_size() == extent_t{ 50, 15 });
        preferred.set_minimum_size(extent_t{ 70, 5 });
        CHECK(preferred.effective_minimum_size() == extent_t{ 70, 10 });

        // A FIXED policy is per axis: the horizontal one does not raise the vertical floor.
        sized_widget_t mixed{ extent_t{ 60, 20 }, extent_t{ 10, 10 } };
        mixed.set_size_policy(size_policy_enum::FIXED, size_policy_enum::PREFERRED);
        CHECK(mixed.horizontal_policy() == size_policy_enum::FIXED);
        CHECK(mixed.vertical_policy() == size_policy_enum::PREFERRED);
        CHECK(mixed.effective_minimum_size() == extent_t{ 60, 10 });
    }

    // ---- (4) FIXED holds while a PREFERRED sibling can still give, and is cut once it cannot ----
    {
        // Floors 60 (FIXED) + 40 (PREFERRED) fill the 100 exactly: the deficit comes out of the
        // PREFERRED child alone, which stops on its minimum hint.
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 40 });
        sized_widget_t fixed{ extent_t{ 60, 20 }, extent_t{ 10, 10 }, &parent };
        sized_widget_t preferred{ extent_t{ 80, 20 }, extent_t{ 40, 10 }, &parent };
        fixed.set_size_policy(size_policy_enum::FIXED);
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);
        CHECK(fixed.effective_minimum_size().width() == 60);
        CHECK(preferred.effective_minimum_size().width() == 40);
        CHECK(fixed.geometry() == rect_t{ 0, 0, 60, 40 });
        CHECK(preferred.geometry() == rect_t{ 60, 0, 40, 40 });
        CHECK(fixed.geometry().width() == fixed.size_hint().width());          // kept its hint
        CHECK(preferred.geometry().width() == preferred.minimum_size_hint().width());
        CHECK(preferred.geometry().width() < preferred.size_hint().width());
        CHECK(fixed.geometry().width() + preferred.geometry().width() == 100);

        // Two PREFERRED children share the same deficit: the FIXED one is not touched at all.
        widget_t three;
        three.set_geometry(rect_t{ 0, 0, 100, 40 });
        sized_widget_t keeper{ extent_t{ 70, 20 }, extent_t{ 10, 10 }, &three };
        sized_widget_t give_one{ extent_t{ 90, 20 }, extent_t{ 10, 10 }, &three };
        sized_widget_t give_two{ extent_t{ 90, 20 }, extent_t{ 10, 10 }, &three };
        keeper.set_size_policy(size_policy_enum::FIXED);
        auto* const three_layout = new hbox_layout_t;
        three_layout->set_spacing(0);
        three.set_layout(three_layout);
        CHECK(keeper.geometry() == rect_t{ 0, 0, 70, 40 });
        CHECK(give_one.geometry() == rect_t{ 70, 0, 15, 40 });
        CHECK(give_two.geometry() == rect_t{ 85, 0, 15, 40 });
        CHECK(keeper.geometry().width() == 70);                 // its hint, untouched
        CHECK(give_one.geometry().width() < give_one.size_hint().width());
        CHECK(give_two.geometry().width() < give_two.size_hint().width());
        CHECK(give_one.geometry().width() + give_two.geometry().width() == 30);
        CHECK(give_two.geometry().right() == 100);

        // Even the floors do not fit now (100 + 20 in 100): the overfull rule cuts the biggest first,
        // so the FIXED child goes below its hint and the PREFERRED one lands on its floor.
        widget_t squeezed;
        squeezed.set_geometry(rect_t{ 0, 0, 100, 40 });
        sized_widget_t cut_fixed{ extent_t{ 100, 20 }, extent_t{ 10, 10 }, &squeezed };
        sized_widget_t cut_preferred{ extent_t{ 80, 20 }, extent_t{ 20, 10 }, &squeezed };
        cut_fixed.set_size_policy(size_policy_enum::FIXED);
        auto* const squeezed_layout = new hbox_layout_t;
        squeezed_layout->set_spacing(0);
        squeezed.set_layout(squeezed_layout);
        CHECK(cut_fixed.effective_minimum_size().width() == 100);
        CHECK(cut_preferred.effective_minimum_size().width() == 20);
        CHECK(cut_fixed.geometry() == rect_t{ 0, 0, 80, 40 });
        CHECK(cut_preferred.geometry() == rect_t{ 80, 0, 20, 40 });
        CHECK(cut_fixed.geometry().width() < cut_fixed.effective_minimum_size().width());
        CHECK(cut_fixed.geometry().width() < cut_fixed.size_hint().width());
        CHECK(cut_preferred.geometry().width() == cut_preferred.minimum_size_hint().width());
        CHECK(cut_preferred.geometry().right() == 100);
    }

    // ---- (5) the maximum stays an upper bound on the overfull path ----
    {
        // 20 + 90 floors in 100: the 10-pixel deficit comes off the big child, the small one stays at
        // its maximum (20) and is not pushed above it.
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 40 });
        sized_widget_t pinned{ extent_t{ 20, 20 }, extent_t{ 20, 10 }, &parent };
        sized_widget_t big{ extent_t{ 90, 20 }, extent_t{ 90, 10 }, &parent };
        pinned.set_fixed_width(20);
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);
        CHECK(pinned.effective_maximum_size().width() == 20);
        CHECK(pinned.geometry() == rect_t{ 0, 0, 20, 40 });
        CHECK(big.geometry() == rect_t{ 20, 0, 80, 40 });
        CHECK(pinned.geometry().width() <= pinned.effective_maximum_size().width());
        CHECK(big.geometry().width() <= big.effective_maximum_size().width());
        CHECK(big.geometry().width() < big.effective_minimum_size().width());
        CHECK(big.geometry().right() == 100);

        // Both children are cut to 30 while their maxima are 50 and 55: the cut never lifts a child
        // above its maximum.
        widget_t capped;
        capped.set_geometry(rect_t{ 0, 0, 60, 40 });
        sized_widget_t first{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &capped };
        sized_widget_t second{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &capped };
        first.set_maximum_width(50);
        second.set_maximum_width(55);
        auto* const capped_layout = new hbox_layout_t;
        capped_layout->set_spacing(0);
        capped.set_layout(capped_layout);
        CHECK(first.geometry() == rect_t{ 0, 0, 30, 40 });
        CHECK(second.geometry() == rect_t{ 30, 0, 30, 40 });
        CHECK(first.geometry().width() <= first.effective_maximum_size().width());
        CHECK(second.geometry().width() <= second.effective_maximum_size().width());
        CHECK(second.geometry().right() == 60);
    }

    // ---- (6) regression: no constraint and enough space => the placement does not move ----
    {
        // hbox hints 40/60/80 with spacing 5 in 300x100 (the shape the other layout tests pin).
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 300, 100 });
        sized_t first{ extent_t{ 40, 10 }, &parent };
        sized_t second{ extent_t{ 60, 10 }, &parent };
        sized_t third{ extent_t{ 80, 10 }, &parent };
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(5);
        parent.set_layout(layout);
        CHECK(layout->size_hint() == extent_t{ 190, 10 });
        CHECK(first.geometry() == rect_t{ 0, 0, 40, 100 });
        CHECK(second.geometry() == rect_t{ 45, 0, 60, 100 });
        CHECK(third.geometry() == rect_t{ 110, 0, 80, 100 });

        // Spare space: the EXPANDING children share it, the FIXED-policy child takes nothing.
        widget_t grown;
        grown.set_geometry(rect_t{ 0, 0, 300, 40 });
        sized_widget_t grow_a{ extent_t{ 50, 10 }, extent_t{ 10, 10 }, &grown };
        sized_widget_t grow_b{ extent_t{ 50, 10 }, extent_t{ 10, 10 }, &grown };
        sized_widget_t stays{ extent_t{ 50, 10 }, extent_t{ 10, 10 }, &grown };
        grow_a.set_size_policy(size_policy_enum::EXPANDING);
        grow_b.set_size_policy(size_policy_enum::EXPANDING);
        stays.set_size_policy(size_policy_enum::FIXED);
        auto* const grown_layout = new hbox_layout_t;
        grown_layout->set_spacing(0);
        grown.set_layout(grown_layout);
        CHECK(grow_a.geometry() == rect_t{ 0, 0, 125, 40 });
        CHECK(grow_b.geometry() == rect_t{ 125, 0, 125, 40 });
        CHECK(stays.geometry() == rect_t{ 250, 0, 50, 40 });

        // vbox hints 10x40, 10x60, 10x80 with spacing 5 in 100x300.
        widget_t vertical;
        vertical.set_geometry(rect_t{ 0, 0, 100, 300 });
        sized_t v1{ extent_t{ 10, 40 }, &vertical };
        sized_t v2{ extent_t{ 10, 60 }, &vertical };
        sized_t v3{ extent_t{ 10, 80 }, &vertical };
        auto* const vertical_layout = new vbox_layout_t;
        vertical_layout->set_spacing(5);
        vertical.set_layout(vertical_layout);
        CHECK(v1.geometry() == rect_t{ 0, 0, 100, 40 });
        CHECK(v2.geometry() == rect_t{ 0, 45, 100, 60 });
        CHECK(v3.geometry() == rect_t{ 0, 110, 100, 80 });

        // Grid 2x2 of 50x30 cells in 200x100.
        widget_t grid_parent;
        grid_parent.set_geometry(rect_t{ 0, 0, 200, 100 });
        sized_t g1{ extent_t{ 50, 30 }, &grid_parent };
        sized_t g2{ extent_t{ 50, 30 }, &grid_parent };
        sized_t g3{ extent_t{ 50, 30 }, &grid_parent };
        sized_t g4{ extent_t{ 50, 30 }, &grid_parent };
        auto* const grid = new grid_layout_t;
        grid->add(&g1, 0, 0);
        grid->add(&g2, 0, 1);
        grid->add(&g3, 1, 0);
        grid->add(&g4, 1, 1);
        grid_parent.set_layout(grid);
        CHECK(grid->size_hint() == extent_t{ 100, 60 });
        CHECK(g1.geometry() == rect_t{ 0, 0, 50, 30 });
        CHECK(g2.geometry() == rect_t{ 50, 0, 50, 30 });
        CHECK(g3.geometry() == rect_t{ 0, 30, 50, 30 });
        CHECK(g4.geometry() == rect_t{ 50, 30, 50, 30 });

        // Between the floors and the hints the proportional rule is unchanged: 80/80/80 with 30
        // minimum hints in 100 still shrinks 47/47/46 to 33/33/34.
        widget_t middle;
        middle.set_geometry(rect_t{ 0, 0, 100, 40 });
        sized_widget_t m1{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &middle };
        sized_widget_t m2{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &middle };
        sized_widget_t m3{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &middle };
        auto* const middle_layout = new hbox_layout_t;
        middle_layout->set_spacing(0);
        middle.set_layout(middle_layout);
        CHECK(m1.geometry() == rect_t{ 0, 0, 33, 40 });
        CHECK(m2.geometry() == rect_t{ 33, 0, 33, 40 });
        CHECK(m3.geometry() == rect_t{ 66, 0, 34, 40 });
        CHECK(m1.geometry().width() >= 30 && m2.geometry().width() >= 30
              && m3.geometry().width() >= 30);
        CHECK(m1.geometry().width() + m2.geometry().width() + m3.geometry().width() == 100);
    }

    if (g_failures == 0) {
        std::printf("overfull: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("overfull: %d of %d checks failed\n", g_failures, g_checks);
    return 1;
}
