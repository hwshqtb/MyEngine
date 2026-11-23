// The layout side of the hard size constraints: layouts use effective_minimum_size() as the
// compression floor and effective_maximum_size() as the growth cap.
//
// The six sections pin the compression semantics:
//  (1) with no hard constraint the placement is the pixel-exact old one (box, vbox and grid);
//  (2) a child with a hard minimum stops at that floor instead of shrinking with its siblings;
//  (3) when every floor fits nowhere, the biggest children are cut first and the axis still fits
//      the parent (the Qt qGeomCalc overfull rule) instead of overflowing it;
//  (4) a maximum caps the growth and the remaining space goes to the other expanding children;
//  (5) the floor is max(minimum_size_hint(), minimum_size()), not either one alone;
//  (6) the floor is per axis: a width constraint does not change the height compression.
//
// Section (2b) and (3b) repeat the floor rule on grid columns, so both layout algorithms are
// covered. No window and no video subsystem: widget_t and the layouts are pure offscreen code.

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

namespace layout_constraint_test {

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

/// @brief A widget whose content is exactly the hint: the unconstrained fixture of section (1).
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

}  // namespace layout_constraint_test

using namespace layout_constraint_test;

int main() {
    std::printf("== layouts: effective minimum floor, maximum cap, per-axis bounds ==\n");

    // ---- (1) regression: no hard constraint => the placement is the old one, pixel for pixel ----
    {
        // hbox hints 40/60/80 with spacing 5 in 300x100: the existing layout_test shape.
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

        // Overfull hbox with minimum hints 30: the existing shrink result 33/33/34.
        widget_t squeezed;
        squeezed.set_geometry(rect_t{ 0, 0, 100, 40 });
        sized_widget_t a{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &squeezed };
        sized_widget_t b{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &squeezed };
        sized_widget_t c{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &squeezed };
        auto* const squeeze_layout = new hbox_layout_t;
        squeeze_layout->set_spacing(0);
        squeezed.set_layout(squeeze_layout);
        CHECK(a.geometry() == rect_t{ 0, 0, 33, 40 });
        CHECK(b.geometry() == rect_t{ 33, 0, 33, 40 });
        CHECK(c.geometry() == rect_t{ 66, 0, 34, 40 });
        CHECK(a.geometry().width() + b.geometry().width() + c.geometry().width() == 100);

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

        // Grid 2x2, every cell 50x30 in 200x100.
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
    }

    // ---- (2) a hard minimum is the compression floor, not the minimum hint ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 40 });
        // A's content floor is 10, but the caller installed 80: together with B and C's floors the
        // axis is exactly full, so A must stay at 80 while B and C take the whole reduction.
        sized_widget_t a{ extent_t{ 80, 20 }, extent_t{ 10, 10 }, &parent };
        sized_widget_t b{ extent_t{ 80, 20 }, extent_t{ 10, 10 }, &parent };
        sized_widget_t c{ extent_t{ 80, 20 }, extent_t{ 10, 10 }, &parent };
        a.set_minimum_width(80);
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);

        CHECK(a.effective_minimum_size().width() == 80);
        CHECK(a.effective_size_hint().width() == 80);
        CHECK(a.geometry() == rect_t{ 0, 0, 80, 40 });
        CHECK(b.geometry() == rect_t{ 80, 0, 10, 40 });
        CHECK(c.geometry() == rect_t{ 90, 0, 10, 40 });
        CHECK(a.geometry().width() + b.geometry().width() + c.geometry().width() == 100);
    }

    // ---- (2b) the same floor rule on a grid column ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 100 });
        sized_widget_t a{ extent_t{ 80, 20 }, extent_t{ 10, 10 }, &parent };
        sized_widget_t b{ extent_t{ 80, 20 }, extent_t{ 10, 10 }, &parent };
        a.set_minimum_width(80);
        auto* const grid = new grid_layout_t;
        grid->add(&a, 0, 0);
        grid->add(&b, 0, 1);
        parent.set_layout(grid);

        // Column 0 is pinned at 80, so column 1 absorbs the 60-pixel deficit and stops at 20.
        CHECK(a.geometry() == rect_t{ 0, 0, 80, 20 });
        CHECK(b.geometry() == rect_t{ 80, 0, 20, 20 });
        CHECK(a.geometry().width() >= a.effective_minimum_size().width());
    }

    // ---- (3) every child at its floor still does not fit: the biggest are cut first, no overflow ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 40 });
        sized_widget_t a{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &parent };
        sized_widget_t b{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &parent };
        sized_widget_t c{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &parent };
        a.set_minimum_width(50);
        b.set_minimum_width(50);
        c.set_minimum_width(50);
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);

        // 150 pixels of floors in 100: the Qt overfull rule cuts 50 pixels off the three equal
        // minimums (34/33/33), so the row fits the parent instead of leaving it at 100..150.
        CHECK(a.geometry() == rect_t{ 0, 0, 34, 40 });
        CHECK(b.geometry() == rect_t{ 34, 0, 33, 40 });
        CHECK(c.geometry() == rect_t{ 67, 0, 33, 40 });
        CHECK(a.geometry().width() < a.effective_minimum_size().width());
        CHECK(b.geometry().width() < b.effective_minimum_size().width());
        CHECK(c.geometry().width() < c.effective_minimum_size().width());
        CHECK(a.geometry().width() + b.geometry().width() + c.geometry().width() == 100);
        CHECK(c.geometry().right() == 100);          // the last child ends on the parent's edge
        CHECK(c.geometry().right() == parent.size().width());
    }

    // ---- (3b) the same overfull rule on grid columns ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 100 });
        sized_widget_t a{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &parent };
        sized_widget_t b{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &parent };
        a.set_minimum_width(60);
        b.set_minimum_width(60);
        auto* const grid = new grid_layout_t;
        grid->add(&a, 0, 0);
        grid->add(&b, 0, 1);
        parent.set_layout(grid);

        // Two 60-pixel floors in 100: both columns are cut to 50 and the grid fits.
        CHECK(a.geometry() == rect_t{ 0, 0, 50, 20 });
        CHECK(b.geometry() == rect_t{ 50, 0, 50, 20 });
        CHECK(a.geometry().width() < a.effective_minimum_size().width());
        CHECK(b.geometry().width() < b.effective_minimum_size().width());
        CHECK(b.geometry().right() == 100);
        CHECK(b.geometry().right() == parent.size().width());
    }

    // ---- (4) a maximum caps growth; the remaining space goes to the other expanding child ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 300, 50 });
        sized_t capped{ extent_t{ 50, 10 }, &parent };
        sized_t free{ extent_t{ 50, 10 }, &parent };
        capped.set_maximum_width(60);
        capped.set_size_policy(size_policy_enum::EXPANDING);
        free.set_size_policy(size_policy_enum::EXPANDING);
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);

        CHECK(capped.effective_maximum_size().width() == 60);
        CHECK(capped.geometry() == rect_t{ 0, 0, 60, 50 });
        CHECK(free.geometry() == rect_t{ 60, 0, 240, 50 });
        CHECK(capped.geometry().width() <= capped.effective_maximum_size().width());
        CHECK(capped.geometry().width() + free.geometry().width() == 300);
        CHECK(free.geometry().right() == 300);
    }

    // ---- (4b) the maximum also caps the cross-axis fill of a box ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 200, 100 });
        sized_t child{ extent_t{ 40, 20 }, &parent };
        child.set_maximum_height(30);
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);

        CHECK(child.geometry() == rect_t{ 0, 0, 40, 30 });
        CHECK(child.geometry().height() <= child.effective_maximum_size().height());
    }

    // ---- (4c) grid columns grow up to the line maximum and no farther ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 200, 100 });
        sized_t capped{ extent_t{ 50, 20 }, &parent };
        sized_t free{ extent_t{ 50, 20 }, &parent };
        capped.set_maximum_width(30);
        capped.set_size_policy(size_policy_enum::EXPANDING);
        free.set_size_policy(size_policy_enum::EXPANDING);
        auto* const grid = new grid_layout_t;
        grid->add(&capped, 0, 0);
        grid->add(&free, 0, 1);
        parent.set_layout(grid);

        // The capped hint is 30, the other 50; the 120 spare pixels all go to column 1 because
        // column 0 is already at its maximum (30).
        CHECK(capped.geometry() == rect_t{ 0, 0, 30, 20 });
        CHECK(free.geometry() == rect_t{ 30, 0, 170, 20 });
        CHECK(free.geometry().x() == 30);
        CHECK(capped.geometry().width() <= capped.effective_maximum_size().width());
    }

    // ---- (4d) a capped expanding child with no other grower leaves the spare space unused ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 300, 50 });
        sized_t only{ extent_t{ 50, 10 }, &parent };
        only.set_maximum_width(60);
        only.set_size_policy(size_policy_enum::EXPANDING);
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);

        CHECK(only.geometry() == rect_t{ 0, 0, 60, 50 });
        CHECK(only.geometry().right() == 60);
    }

    // ---- (5) the floor is max(minimum_size_hint(), minimum_size()) ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 40 });
        // A: the hard minimum (70) is above the content floor (40). B: the content floor (90) is
        // above its hard minimum (0). Both floors must win over their respective smaller source.
        sized_widget_t a{ extent_t{ 100, 20 }, extent_t{ 40, 10 }, &parent };
        sized_widget_t b{ extent_t{ 100, 20 }, extent_t{ 90, 10 }, &parent };
        a.set_minimum_width(70);
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);

        CHECK(a.effective_minimum_size() == extent_t{ 70, 10 });
        CHECK(b.effective_minimum_size() == extent_t{ 90, 10 });
        // 160 pixels of floors in 100: the overfull rule cuts both to 50 and the row fits.
        CHECK(a.geometry() == rect_t{ 0, 0, 50, 40 });
        CHECK(b.geometry() == rect_t{ 50, 0, 50, 40 });
        CHECK(b.geometry().right() == 100);
        CHECK(a.geometry().width() < a.effective_minimum_size().width());
        CHECK(b.geometry().width() < b.effective_minimum_size().width());

        sized_widget_t standalone{ extent_t{ 100, 20 }, extent_t{ 90, 10 } };
        standalone.set_minimum_size(extent_t{ 70, 10 });
        CHECK(standalone.effective_minimum_size() == extent_t{ 90, 10 });  // the hint is larger
        standalone.set_minimum_size(extent_t{ 95, 10 });
        CHECK(standalone.effective_minimum_size() == extent_t{ 95, 10 });  // the hard bound is larger
    }

    // ---- (6) per axis: only the width floor is installed, the height still compresses normally ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 100 });
        sized_widget_t a{ extent_t{ 20, 80 }, extent_t{ 10, 30 }, &parent };
        sized_widget_t b{ extent_t{ 20, 80 }, extent_t{ 10, 30 }, &parent };
        a.set_minimum_width(90);
        auto* const layout = new vbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);

        CHECK(a.effective_minimum_size() == extent_t{ 90, 30 });  // width floor only
        // The 60-pixel height deficit is split over the 50-pixel slack of each child: 50 + 50.
        CHECK(a.geometry() == rect_t{ 0, 0, 100, 50 });
        CHECK(b.geometry() == rect_t{ 0, 50, 100, 50 });
        CHECK(a.geometry().height() == 50);
        CHECK(b.geometry().height() == 50);
    }

    // ---- (6b) the mirror case: a height floor does not change the width compression ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 100 });
        sized_widget_t a{ extent_t{ 80, 20 }, extent_t{ 10, 10 }, &parent };
        sized_widget_t b{ extent_t{ 80, 20 }, extent_t{ 10, 10 }, &parent };
        a.set_minimum_height(90);
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);

        CHECK(a.effective_minimum_size() == extent_t{ 10, 90 });
        CHECK(a.geometry().width() == 50);
        CHECK(b.geometry().width() == 50);
        CHECK(a.geometry().height() == 100);
        CHECK(b.geometry().height() == 100);
    }

    if (g_failures == 0) {
        std::printf("layout constraints: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("layout constraints: %d of %d checks failed\n", g_failures, g_checks);
    return 1;
}
