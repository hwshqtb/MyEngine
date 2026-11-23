// W2: the layout layer — layout_t / box_layout_t / grid_layout_t, size hints, policies and stretch.
//
// The W2 acceptance criteria as executable checks:
//  1. hbox: hints 40/60/80, spacing 5 in a 300-wide area -> exact x/width for all three;
//  2. margins_t shifts the content area on all four sides;
//  3. expanding children split the remainder in proportion to stretch (1:3);
//  4. shrinking: above the minimum hints the excess comes off in proportion to the room above the
//     floor; when even the floors do not fit, the biggest children are cut first so the row still
//     fits (never negative);
//  5. vbox mirrors the box algorithm along y (growth and shrink);
//  6. grid 2x2 with equal children -> equal cells; a spanning widget covers two columns;
//  7. resize / add / remove re-lays out (through set_geometry and the child hooks, and the placed
//     children are repainted through their own set_geometry());
//  8. nested layouts: an outer hbox holding two widgets that each own a vbox, all four leaves exact;
//  9. set_layout() replaces and deletes the old layout (destructor counter), same pointer is a no-op;
// 10. size_hint()/minimum_size_hint() defaults (a bare widget has no opinion), the fixed-size
//     constraint, content overrides, layout-derived container hints, policies and stretch.
//
// No window and no video subsystem: the layout classes and widget_t are pure offscreen code.

#include <cstddef>
#include <cstdio>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/metaobject/meta_object_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/ui/box_layout_t.h"
#include "hwshqtb/ui/grid_layout_t.h"
#include "hwshqtb/ui/layout_t.h"
#include "hwshqtb/ui/push_button_t.h"
#include "hwshqtb/ui/style_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using hwshqtb::metaobject::object_t;
using hwshqtb::ui::box_orientation_enum;
using hwshqtb::ui::grid_layout_t;
using hwshqtb::ui::hbox_layout_t;
using hwshqtb::ui::item_t;
using hwshqtb::ui::layout_t;
using hwshqtb::ui::push_button_t;
using hwshqtb::ui::size_policy_enum;
using hwshqtb::ui::vbox_layout_t;
using hwshqtb::ui::widget_t;

namespace layout_test {

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

/// @brief A widget whose content "is" the given hint, with a custom minimum (tests 4/5).
/// @note The hint is the CONTROL's opinion: there is no setter on widget_t any more.
class sized_widget_t final: public widget_t {
public:
    sized_widget_t(extent_t hint, extent_t minimum, widget_t* parent = nullptr):
        widget_t(parent),
        _hint(hint),
        _minimum(minimum) { }

    extent_t size_hint() const override {
        return _hint;
    }

    extent_t minimum_size_hint() const override {
        return _minimum;
    }

private:
    extent_t _hint;
    extent_t _minimum;
};

/// @brief A widget whose content is the given size: the leaf-with-an-opinion fixture used by the
///        layout tests (minimum stays {0, 0}, so it may still shrink).
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

/// @brief A layout that records how often it ran and how often it was destroyed (test 9).
class counting_layout_t final: public layout_t {
public:
    explicit counting_layout_t(int* destroyed):
        _destroyed(destroyed) { }

    ~counting_layout_t() override {
        ++*_destroyed;
    }

    void apply(widget_t&, rect_t) override {
        ++applies;
    }

    int applies = 0;

private:
    int* _destroyed;
};

/// @brief A widget that derives its hints from content (test 10).
class content_widget_t final: public widget_t {
public:
    extent_t size_hint() const override {
        return extent_t{ 11, 12 };
    }

    extent_t minimum_size_hint() const override {
        return extent_t{ 3, 4 };
    }
};

/// @brief A minimal NON-widget item: the item contract only (no layout, no control properties).
///        The grid must size it exactly like a widget because it asks items, not widgets.
class item_probe_t final: public item_t {
public:
    explicit item_probe_t(extent_t hint) noexcept:
        _hint(hint) { }

    extent_t size_hint() const override {
        return _hint;
    }

    extent_t minimum_size_hint() const override {
        return extent_t{};
    }

    item_t* hit(point_t local) const override {
        return rect().contains(local) ? const_cast<item_probe_t*>(this) : nullptr;
    }

private:
    extent_t _hint;
};

}  // namespace layout_test

using namespace layout_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    std::printf("== W2: layouts (box / grid / size hints) ==\n");

    static_assert(sizeof(object_t) == 32);

    // ---- (1) hbox: hints, spacing, exact positions ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 300, 100 });
        sized_t first{ extent_t{ 40, 10 }, &parent };
        sized_t second{ extent_t{ 60, 10 }, &parent };
        sized_t third{ extent_t{ 80, 10 }, &parent };

        auto* const layout = new hbox_layout_t;
        layout->set_spacing(5);
        parent.set_layout(layout);
        CHECK(parent.layout() == layout);
        CHECK(layout->orientation() == box_orientation_enum::HORIZONTAL);
        CHECK(layout->size_hint() == extent_t{ 40 + 5 + 60 + 5 + 80, 10 });

        // 40 + 5 + 60 + 5 + 80 = 190; the remaining 110 stays unused (no expanding child).
        CHECK(first.geometry() == rect_t{ 0, 0, 40, 100 });
        CHECK(second.geometry() == rect_t{ 45, 0, 60, 100 });
        CHECK(third.geometry() == rect_t{ 110, 0, 80, 100 });

        // A widget with a layout ignores manual child geometry on the next pass.
        first.set_geometry(rect_t{ 999, 999, 7, 7 });
        CHECK(first.geometry() == rect_t{ 999, 999, 7, 7 });
        parent.relayout();
        CHECK(first.geometry() == rect_t{ 0, 0, 40, 100 });

        // Non-widget children take no slot.
        object_t* const plain = new object_t;
        plain->set_parent(&parent);
        parent.relayout();
        CHECK(first.geometry() == rect_t{ 0, 0, 40, 100 });
        CHECK(second.geometry() == rect_t{ 45, 0, 60, 100 });
        CHECK(third.geometry() == rect_t{ 110, 0, 80, 100 });
        delete plain;
    }

    // ---- (2) margins shift the content area on all four sides ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 300, 100 });
        sized_t child{ extent_t{ 50, 30 }, &parent };

        auto* const layout = new hbox_layout_t;
        layout->set_margins(margins_t{ 10, 20, 30, 40 });
        layout->set_spacing(7);
        parent.set_layout(layout);

        CHECK(layout->margins() == margins_t{ 10, 20, 30, 40 });
        CHECK(layout->margins().left() == 10);
        CHECK(layout->margins().top() == 20);
        CHECK(layout->margins().right() == 30);
        CHECK(layout->margins().bottom() == 40);
        CHECK(layout->margins().horizontal() == 40);
        CHECK(layout->margins().vertical() == 60);
        CHECK(layout->spacing() == 7);
        CHECK(layout->size_hint() == extent_t{ 50 + 10 + 30, 30 + 20 + 40 });
        // Content = x 10..270 (260 wide), y 20..60 (40 high).
        CHECK(child.geometry() == rect_t{ 10, 20, 50, 40 });

        // A margin set larger than the area clamps the content extent to zero, not negative.
        parent.set_geometry(rect_t{ 0, 0, 20, 20 });
        CHECK(child.geometry() == rect_t{ 10, 20, 0, 0 });
    }

    // ---- (3) stretch splits the remainder 1:3 ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 300, 50 });
        sized_t one{ extent_t{ 50, 10 }, &parent };
        sized_t three{ extent_t{ 50, 10 }, &parent };
        one.set_size_policy(size_policy_enum::EXPANDING);
        three.set_size_policy(size_policy_enum::EXPANDING);
        one.set_stretch(1);
        three.set_stretch(3);
        CHECK(one.stretch() == 1);
        CHECK(three.stretch() == 3);

        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);
        CHECK(layout->size_hint() == extent_t{ 100, 10 });

        // Hints 50 + 50 = 100; remainder 200 -> 50 / 150.
        CHECK(one.geometry() == rect_t{ 0, 0, 100, 50 });
        CHECK(three.geometry() == rect_t{ 100, 0, 200, 50 });
        CHECK(one.geometry().width() - 50 == 50);
        CHECK(three.geometry().width() - 50 == 150);
    }

    // ---- (4) shrink: the minimum hint is the floor, never a negative extent ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 40 });
        sized_widget_t a{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &parent };
        sized_widget_t b{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &parent };
        sized_widget_t c{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &parent };

        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);

        // 240 needed, 140 excess, 150 total room above the minima -> shrink 47/47/46.
        CHECK(a.geometry() == rect_t{ 0, 0, 33, 40 });
        CHECK(b.geometry() == rect_t{ 33, 0, 33, 40 });
        CHECK(c.geometry() == rect_t{ 66, 0, 34, 40 });
        CHECK(a.geometry().width() + b.geometry().width() + c.geometry().width() == 100);
        CHECK(a.geometry().width() >= 30);
        CHECK(b.geometry().width() >= 30);
        CHECK(c.geometry().width() >= 30);

        // Even the minimums do not fit: the biggest children are cut first (Qt's qGeomCalc overfull
        // rule), so the row fits the 50 pixels and every child ends BELOW its own minimum hint.
        widget_t narrow;
        narrow.set_geometry(rect_t{ 0, 0, 50, 40 });
        sized_widget_t n1{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &narrow };
        sized_widget_t n2{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &narrow };
        sized_widget_t n3{ extent_t{ 80, 20 }, extent_t{ 30, 10 }, &narrow };
        auto* const narrow_layout = new hbox_layout_t;
        narrow_layout->set_spacing(0);
        narrow.set_layout(narrow_layout);
        CHECK(n1.geometry() == rect_t{ 0, 0, 17, 40 });
        CHECK(n2.geometry() == rect_t{ 17, 0, 17, 40 });
        CHECK(n3.geometry() == rect_t{ 34, 0, 16, 40 });
        CHECK(n1.geometry().width() < 30);                      // below its minimum hint
        CHECK(n2.geometry().width() < 30);
        CHECK(n3.geometry().width() < 30);
        CHECK(n1.geometry().width() > 0 && n2.geometry().width() > 0 && n3.geometry().width() > 0);
        CHECK(n1.geometry().width() + n2.geometry().width() + n3.geometry().width() == 50);
        CHECK(n3.geometry().right() == 50);                     // the row fits the parent
        CHECK(n3.geometry().right() == narrow.size().width());
    }

    // ---- (5) vbox mirrors the box algorithm along y ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 300 });
        sized_t first{ extent_t{ 10, 40 }, &parent };
        sized_t second{ extent_t{ 10, 60 }, &parent };
        sized_t third{ extent_t{ 10, 80 }, &parent };

        auto* const layout = new vbox_layout_t;
        layout->set_spacing(5);
        parent.set_layout(layout);
        CHECK(layout->orientation() == box_orientation_enum::VERTICAL);
        CHECK(layout->size_hint() == extent_t{ 10, 40 + 5 + 60 + 5 + 80 });

        CHECK(first.geometry() == rect_t{ 0, 0, 100, 40 });
        CHECK(second.geometry() == rect_t{ 0, 45, 100, 60 });
        CHECK(third.geometry() == rect_t{ 0, 110, 100, 80 });

        // Shrink along y: same floor rule.
        widget_t squeezed;
        squeezed.set_geometry(rect_t{ 0, 0, 100, 100 });
        sized_widget_t a{ extent_t{ 20, 80 }, extent_t{ 10, 30 }, &squeezed };
        sized_widget_t b{ extent_t{ 20, 80 }, extent_t{ 10, 30 }, &squeezed };
        sized_widget_t c{ extent_t{ 20, 80 }, extent_t{ 10, 30 }, &squeezed };
        auto* const squeeze_layout = new vbox_layout_t;
        squeeze_layout->set_spacing(0);
        squeezed.set_layout(squeeze_layout);
        CHECK(a.geometry().height() + b.geometry().height() + c.geometry().height() == 100);
        CHECK(a.geometry().height() >= 30 && b.geometry().height() >= 30 && c.geometry().height() >= 30);
        CHECK(a.geometry().height() > 0 && b.geometry().height() > 0 && c.geometry().height() > 0);
    }

    // ---- (6) grid: equal cells, spanning widget, margins/spacing ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 200, 100 });
        sized_t a{ extent_t{ 50, 30 }, &parent };
        sized_t b{ extent_t{ 50, 30 }, &parent };
        sized_t c{ extent_t{ 50, 30 }, &parent };
        sized_t d{ extent_t{ 50, 30 }, &parent };

        auto* const grid = new grid_layout_t;
        grid->add(&a, 0, 0);
        grid->add(&b, 0, 1);
        grid->add(&c, 1, 0);
        grid->add(&d, 1, 1);
        CHECK(grid->count() == 4);
        CHECK(grid->size_hint() == extent_t{ 100, 60 });  // two 50x30 columns/rows
        parent.set_layout(grid);

        CHECK(a.geometry() == rect_t{ 0, 0, 50, 30 });
        CHECK(b.geometry() == rect_t{ 50, 0, 50, 30 });
        CHECK(c.geometry() == rect_t{ 0, 30, 50, 30 });
        CHECK(d.geometry() == rect_t{ 50, 30, 50, 30 });
        CHECK(a.size() == b.size());
        CHECK(a.size() == c.size());
        CHECK(a.size() == d.size());

        // A spanning widget covers two columns; its hint is shared over the spanned columns.
        widget_t spanned_parent;
        spanned_parent.set_geometry(rect_t{ 0, 0, 200, 100 });
        sized_t span{ extent_t{ 100, 20 }, &spanned_parent };
        sized_t left{ extent_t{ 40, 20 }, &spanned_parent };
        sized_t right{ extent_t{ 40, 20 }, &spanned_parent };
        auto* const spanning = new grid_layout_t;
        spanning->add(&span, 0, 0, 1, 2);
        spanning->add(&left, 1, 0);
        spanning->add(&right, 1, 1);
        CHECK(spanning->size_hint() == extent_t{ 100, 40 }); // span 100 split 50/50 over its columns
        spanned_parent.set_layout(spanning);
        CHECK(span.geometry() == rect_t{ 0, 0, 100, 20 });
        CHECK(left.geometry() == rect_t{ 0, 20, 50, 20 });
        CHECK(right.geometry() == rect_t{ 50, 20, 50, 20 });
        CHECK(span.geometry().width() == left.geometry().width() + right.geometry().width());
        CHECK(left.geometry().right() == right.geometry().x());

        // Margins + spacing are honoured on both axes.
        widget_t framed;
        framed.set_geometry(rect_t{ 0, 0, 200, 100 });
        sized_t f1{ extent_t{ 40, 20 }, &framed };
        sized_t f2{ extent_t{ 40, 20 }, &framed };
        f1.set_size_policy(size_policy_enum::PREFERRED, size_policy_enum::EXPANDING);
        f2.set_size_policy(size_policy_enum::PREFERRED, size_policy_enum::EXPANDING);
        auto* const framed_grid = new grid_layout_t;
        framed_grid->set_margins(margins_t{ 10, 20, 30, 40 });
        framed_grid->set_spacing(5);
        framed_grid->add(&f1, 0, 0);
        framed_grid->add(&f2, 0, 1);
        CHECK(framed_grid->size_hint() == extent_t{ 40 + 5 + 40 + 40, 20 + 60 });
        framed.set_layout(framed_grid);
        CHECK(f1.geometry() == rect_t{ 10, 20, 40, 40 });
        CHECK(f2.geometry() == rect_t{ 55, 20, 40, 40 });

        // A destroyed entry disappears on the next apply instead of dangling.
        widget_t dead_parent;
        dead_parent.set_geometry(rect_t{ 0, 0, 100, 100 });
        widget_t* const dead = new sized_t{ extent_t{ 20, 20 }, &dead_parent };
        auto* const dead_grid = new grid_layout_t;
        dead_grid->add(dead, 0, 0);
        dead_parent.set_layout(dead_grid);
        CHECK(dead_grid->count() == 1);
        delete dead;  // ~object_t notifies the parent; object_pointer_t reads as null afterwards
        dead_parent.relayout();
        CHECK(dead_grid->count() == 0);
    }

    // ---- (7) resize / add / remove re-lay out; placement goes through set_geometry() ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 50 });
        widget_t a{ &parent };
        widget_t b{ &parent };
        a.set_size_policy(size_policy_enum::EXPANDING);
        b.set_size_policy(size_policy_enum::EXPANDING);
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);
        CHECK(a.geometry() == rect_t{ 0, 0, 50, 50 });
        CHECK(b.geometry() == rect_t{ 50, 0, 50, 50 });

        // Resize the parent: set_geometry() re-lays out the children and asks for a repaint.
        // The tree keeps ONE request receiver (an object_t) on its root; this recorder stands in for the
        // host that would otherwise move the pixels.
        struct recorder_t final: ::hwshqtb::metaobject::object_t {
            int repaints = 0;
            bool event(::hwshqtb::metaobject::event_t* e) override {
                if (e != nullptr && e->type() == ::hwshqtb::metaobject::event_type_enum::PAINT) {
                    ++repaints;
                    return true;
                }
                return object_t::event(e);
            }
        };
        recorder_t recorder;
        parent.set_request_receiver(&recorder);
        const int before = recorder.repaints;
        parent.set_geometry(rect_t{ 0, 0, 200, 50 });
        CHECK(a.geometry() == rect_t{ 0, 0, 100, 50 });
        CHECK(b.geometry() == rect_t{ 100, 0, 100, 50 });
        CHECK(recorder.repaints > before);  // the children were placed through set_geometry()

        // Adding a widget child re-lays out the parent: the hook runs from set_parent().
        widget_t* const added = new widget_t;
        added->set_size_policy(size_policy_enum::EXPANDING);
        added->set_parent(&parent);
        CHECK(a.geometry() == rect_t{ 0, 0, 67, 50 });
        CHECK(b.geometry() == rect_t{ 67, 0, 67, 50 });
        CHECK(added->geometry() == rect_t{ 134, 0, 66, 50 });
        CHECK(a.geometry().width() + b.geometry().width() + added->geometry().width() == 200);

        // Removing it re-lays out again, back to two equal children.
        delete added;
        CHECK(a.geometry() == rect_t{ 0, 0, 100, 50 });
        CHECK(b.geometry() == rect_t{ 100, 0, 100, 50 });

        // A plain object_t child is not a layout slot, but its removal is only a harmless extra pass.
        object_t* const plain = new object_t;
        plain->set_parent(&parent);
        CHECK(a.geometry() == rect_t{ 0, 0, 100, 50 });
        delete plain;
        CHECK(a.geometry() == rect_t{ 0, 0, 100, 50 });
    }

    // ---- (8) nested layouts: outer hbox, two inner vboxes, all four leaves ----
    {
        widget_t outer;
        outer.set_geometry(rect_t{ 0, 0, 300, 100 });
        widget_t left{ &outer };
        widget_t right{ &outer };
        left.set_size_policy(size_policy_enum::EXPANDING);
        left.set_stretch(1);
        right.set_size_policy(size_policy_enum::EXPANDING);
        right.set_stretch(1);

        // The inner layouts are installed BEFORE the outer one: only then can the outer box ask the
        // containers for their natural size (layout_t::size_hint()) and expand them to fill.
        sized_t left_top{ extent_t{ 100, 30 }, &left };
        sized_t left_bottom{ extent_t{ 100, 30 }, &left };
        auto* const left_layout = new vbox_layout_t;
        left_layout->set_spacing(0);
        left.set_layout(left_layout);

        sized_t right_top{ extent_t{ 100, 35 }, &right };
        sized_t right_bottom{ extent_t{ 100, 65 }, &right };
        auto* const right_layout = new vbox_layout_t;
        right_layout->set_spacing(0);
        right.set_layout(right_layout);

        // The containers have an opinion now: it is their children's sum.
        CHECK(left.size_hint() == extent_t{ 100, 30 + 30 });
        CHECK(right.size_hint() == extent_t{ 100, 35 + 65 });
        CHECK(left.size_hint() == left_layout->size_hint());
        CHECK(right.size_hint() == right_layout->size_hint());

        auto* const outer_layout = new hbox_layout_t;
        outer_layout->set_spacing(0);
        outer.set_layout(outer_layout);
        // Hints 100 + 100, remainder 100 split evenly by the two stretch-1 weights -> 150 each.
        CHECK(outer_layout->size_hint() == extent_t{ 200, 100 });
        CHECK(left.geometry() == rect_t{ 0, 0, 150, 100 });
        CHECK(right.geometry() == rect_t{ 150, 0, 150, 100 });

        CHECK(left_top.geometry() == rect_t{ 0, 0, 150, 30 });
        CHECK(left_bottom.geometry() == rect_t{ 0, 30, 150, 30 });
        // Leaf geometry is relative to its own parent, so the right vbox starts at x = 0 as well.
        CHECK(right_top.geometry() == rect_t{ 0, 0, 150, 35 });
        CHECK(right_bottom.geometry() == rect_t{ 0, 35, 150, 65 });

        // The outer layout was not disturbed by the inner passes.
        CHECK(left.geometry() == rect_t{ 0, 0, 150, 100 });
        CHECK(right.geometry() == rect_t{ 150, 0, 150, 100 });
    }

    // ---- (9) set_layout replaces and deletes the old layout; same pointer is a no-op ----
    {
        int destroyed = 0;
        {
            widget_t owner;
            auto* const first = new counting_layout_t{ &destroyed };
            owner.set_layout(first);
            CHECK(owner.layout() == first);
            CHECK(first->applies == 1);  // installed layouts are applied immediately
            owner.set_layout(first);     // same pointer: must not delete it
            CHECK(destroyed == 0);

            auto* const second = new counting_layout_t{ &destroyed };
            owner.set_layout(second);
            CHECK(destroyed == 1);
            CHECK(owner.layout() == second);
            CHECK(second->applies == 1);

            owner.set_layout(nullptr);
            CHECK(destroyed == 2);
            CHECK(owner.layout() == nullptr);

            owner.set_layout(new counting_layout_t{ &destroyed });
            CHECK(destroyed == 2);
        }
        CHECK(destroyed == 3);  // the widget deletes the layout it still owns

        // A layout installed on a widget with a non-zero size is applied to that size.
        int applied_area_width = -1;
        class area_layout_t final: public layout_t {
        public:
            explicit area_layout_t(int& width): _width(width) { }
            void apply(widget_t&, rect_t area) override {
                _width = area.width();
            }
        private:
            int& _width;
        };
        widget_t sized;
        sized.set_geometry(rect_t{ 0, 0, 123, 45 });
        sized.set_layout(new area_layout_t{ applied_area_width });
        CHECK(applied_area_width == 123);
    }

    // ---- (10) size opinions: bare widget, fixed constraint (item state), content override,
    //      layout-derived container hints, and the layouts enforcing the constraint ----
    {
        // A bare widget has NO opinion. The old default (size()) was circular — the layout uses the
        // hint to set the geometry that would then be the hint — so the answer is {0, 0}.
        widget_t widget;
        CHECK(widget.size_hint() == extent_t{ 0, 0 });
        CHECK(widget.minimum_size_hint() == extent_t{ 0, 0 });
        CHECK(widget.horizontal_policy() == size_policy_enum::PREFERRED);
        CHECK(widget.vertical_policy() == size_policy_enum::PREFERRED);
        CHECK(widget.stretch() == 0);
        CHECK(widget.layout() == nullptr);
        widget.relayout();  // no layout: documented no-op
        CHECK(widget.size_hint() == extent_t{ 0, 0 });

        widget.set_geometry(rect_t{ 5, 6, 30, 20 });
        CHECK(widget.size_hint() == extent_t{ 0, 0 });  // NOT size(): no circular hint
        CHECK(widget.size() == extent_t{ 30, 20 });
        CHECK(widget.geometry() == rect_t{ 5, 6, 30, 20 });

        // set_fixed_size() is item state: the OPINION is untouched, the constraint is what layouts
        // ask (effective_size_hint()).
        widget.set_fixed_size(extent_t{ 70, 40 });
        CHECK(widget.has_fixed_size());
        CHECK(widget.fixed_size() == extent_t{ 70, 40 });
        CHECK(widget.effective_size_hint() == extent_t{ 70, 40 });
        CHECK(widget.size_hint() == extent_t{ 0, 0 });          // the opinion is unchanged
        CHECK(widget.minimum_size_hint() == extent_t{ 0, 0 });  // ... and so is the minimum
        CHECK(widget.size() == extent_t{ 30, 20 });
        CHECK(widget.geometry() == rect_t{ 5, 6, 30, 20 });

        // A container's opinion comes from its layout: layout_t::size_hint() walks the item children.
        widget_t container;
        container.set_geometry(rect_t{ 0, 0, 300, 100 });
        sized_t c1{ extent_t{ 40, 10 }, &container };
        sized_t c2{ extent_t{ 60, 20 }, &container };
        auto* const container_layout = new hbox_layout_t;
        container_layout->set_spacing(5);
        container.set_layout(container_layout);
        CHECK(container_layout->size_hint() == extent_t{ 40 + 5 + 60, 20 });
        CHECK(container.size_hint() == container_layout->size_hint());
        CHECK(container.effective_size_hint() == container_layout->size_hint());
        // The floor is the layout's as well: sized_t publishes no minimum hint and installs no hard
        // constraint, so only the 5-px gap between the two children is left (the margins are 0).
        CHECK(container_layout->minimum_size() == extent_t{ 5, 0 });
        CHECK(container.minimum_size_hint() == container_layout->minimum_size());

        // The constraint wins for the layouts, while the container keeps reporting its opinion.
        container.set_fixed_size(extent_t{ 1, 2 });
        CHECK(container.effective_size_hint() == extent_t{ 1, 2 });
        CHECK(container.size_hint() == container_layout->size_hint());  // opinion unchanged
        // The floor is an opinion too: the container's own hard size does not rewrite it.
        CHECK(container.minimum_size_hint() == container_layout->minimum_size());

        // An empty layout answers its margins' sum: there is no child to ask.
        widget_t empty_owner;
        auto* const empty_box = new hbox_layout_t;
        empty_box->set_margins(margins_t{ 1, 2, 3, 4 });
        empty_box->set_spacing(9);
        empty_owner.set_layout(empty_box);
        CHECK(empty_box->size_hint() == extent_t{ 4, 6 });

        // A grid's hint follows the same column/row rule apply() uses.
        widget_t grid_owner;
        grid_owner.set_geometry(rect_t{ 0, 0, 200, 100 });
        sized_t g1{ extent_t{ 30, 10 }, &grid_owner };
        sized_t g2{ extent_t{ 50, 30 }, &grid_owner };
        auto* const grid = new grid_layout_t;
        grid->set_margins(margins_t{ 10, 20, 30, 40 });
        grid->set_spacing(5);
        grid->add(&g1, 0, 0);
        grid->add(&g2, 0, 1);
        grid_owner.set_layout(grid);
        CHECK(grid->size_hint() == extent_t{ 30 + 5 + 50 + 40, 30 + 60 });
        CHECK(grid_owner.size_hint() == grid->size_hint());

        widget.set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::FIXED);
        CHECK(widget.horizontal_policy() == size_policy_enum::EXPANDING);
        CHECK(widget.vertical_policy() == size_policy_enum::FIXED);
        widget.set_size_policy(size_policy_enum::FIXED);
        CHECK(widget.horizontal_policy() == size_policy_enum::FIXED);
        CHECK(widget.vertical_policy() == size_policy_enum::PREFERRED);

        widget.set_stretch(3);
        CHECK(widget.stretch() == 3);
        widget.set_stretch(-5);
        CHECK(widget.stretch() == 0);  // negative stretches are clamped

        // Virtual overrides: a content-derived hint and minimum are the item's own opinion.
        content_widget_t content;
        CHECK(content.size_hint() == extent_t{ 11, 12 });
        CHECK(content.minimum_size_hint() == extent_t{ 3, 4 });
        CHECK(content.effective_size_hint() == extent_t{ 11, 12 });

        // The constraint is applied by effective_size_hint(), not by the opinion virtuals.
        content.set_fixed_size(extent_t{ 7, 8 });
        CHECK(content.size_hint() == extent_t{ 11, 12 });          // opinion
        CHECK(content.minimum_size_hint() == extent_t{ 3, 4 });    // own floor
        CHECK(content.effective_size_hint() == extent_t{ 7, 8 });  // constraint wins

        // The real control: push_button_t's content hint stays its opinion; fixed wins for layouts.
        push_button_t button;
        button.set_text("OK");
        const extent_t button_opinion = button.size_hint();
        CHECK(button_opinion.height()
              == hwshqtb::ui::default_style().pixel_metric(
                     hwshqtb::ui::pixel_metric_enum::BUTTON_HEIGHT));
        CHECK(button_opinion.width() > 0);
        CHECK(button.effective_size_hint() == button_opinion);
        button.set_fixed_size(extent_t{ 100, 40 });
        CHECK(button.size_hint() == button_opinion);  // content still reports its opinion
        CHECK(button.effective_size_hint() == extent_t{ 100, 40 });

        // effective_size_hint() also clamps the opinion to the minimum, per axis.
        sized_widget_t clamped{ extent_t{ 5, 5 }, extent_t{ 20, 10 } };
        CHECK(clamped.size_hint() == extent_t{ 5, 5 });
        CHECK(clamped.minimum_size_hint() == extent_t{ 20, 10 });
        CHECK(clamped.effective_size_hint() == extent_t{ 20, 10 });

        // A box pins a fixed child on BOTH axes, even when the child is EXPANDING.
        widget_t box_parent;
        box_parent.set_geometry(rect_t{ 0, 0, 300, 100 });
        sized_t normal{ extent_t{ 40, 10 }, &box_parent };
        sized_t pinned{ extent_t{ 50, 20 }, &box_parent };
        pinned.set_fixed_size(extent_t{ 60, 25 });
        pinned.set_size_policy(size_policy_enum::EXPANDING);
        auto* const box = new hbox_layout_t;
        box->set_spacing(0);
        box_parent.set_layout(box);
        CHECK(normal.geometry() == rect_t{ 0, 0, 40, 100 });   // unfixed fills the cross axis
        CHECK(pinned.geometry() == rect_t{ 40, 0, 60, 25 });   // fixed: both axes pinned
        CHECK(box->size_hint() == extent_t{ 40 + 60, 25 });

        // A grid pins a fixed item on both axes inside its (possibly larger) shared cell.
        widget_t grid_parent;
        grid_parent.set_geometry(rect_t{ 0, 0, 200, 100 });
        sized_t grid_fixed{ extent_t{ 30, 10 }, &grid_parent };
        sized_t grid_free{ extent_t{ 50, 30 }, &grid_parent };
        grid_fixed.set_fixed_size(extent_t{ 20, 15 });
        auto* const fixed_grid = new grid_layout_t;
        fixed_grid->add(&grid_fixed, 0, 0);
        fixed_grid->add(&grid_free, 0, 1);
        grid_parent.set_layout(fixed_grid);
        CHECK(fixed_grid->size_hint() == extent_t{ 20 + 50, 30 });
        CHECK(grid_fixed.geometry() == rect_t{ 0, 0, 20, 15 });
        CHECK(grid_free.geometry() == rect_t{ 20, 0, 50, 30 });
    }

    // ---- (11) the grid arranges a NON-widget item: only the item contract takes part ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 200, 100 });
        sized_t widget_cell{ extent_t{ 50, 20 }, &parent };
        item_probe_t pinned{ extent_t{ 10, 10 } };
        pinned.set_fixed_size(extent_t{ 60, 30 });   // its column/row contribution is fixed
        sized_t other{ extent_t{ 20, 10 }, &parent };

        auto* const grid = new grid_layout_t;
        grid->add(&widget_cell, 0, 0);
        grid->add(&pinned, 1, 0);
        grid->add(&other, 1, 1);
        parent.set_layout(grid);

        // The item's fixed hint widens column 0 (50 -> 60) and heightens row 1 (10 -> 30), so the
        // WIDGET in row 0 is stretched by a NON-widget item: no widget-only API took part.
        CHECK(grid->size_hint() == extent_t{ 60 + 20, 20 + 30 });
        CHECK(widget_cell.geometry() == rect_t{ 0, 0, 60, 20 });
        CHECK(pinned.geometry() == rect_t{ 0, 20, 60, 30 });
        CHECK(other.geometry() == rect_t{ 60, 20, 20, 30 });

        // It has none of the widget-only layers: not a widget, no widget properties. The item layer
        // DOES own visible/enabled (every visual node has that state), so those resolve here; a
        // control's own property (text) does not.
        CHECK(!pinned.is_widget());
        CHECK(pinned.is_item());
        CHECK(pinned.meta_object() != nullptr);
        CHECK(pinned.meta_object()->property("object_name").is_valid());
        CHECK(pinned.meta_object()->property("visible").is_valid());
        CHECK(!pinned.meta_object()->property("text").is_valid());
    }

    if (g_failures == 0) {
        std::printf("ui layouts: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("ui layouts: %d checks, %d failure(s)\n", g_checks, g_failures);
    return 1;
}
