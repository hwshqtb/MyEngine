// The hard size constraints of item_t and the effective_*() triple a layout asks:
//   preferred = clamp(max(sizeHint, minimumSizeHint), minimumSize, maximumSize)
//   minimum   = max(minimumSizeHint, minimumSize), bounded by maximumSize
//   maximum   = maximumSize
//
// Section (13) is the regression that keeps the change behaviour-neutral: with NO maximum installed
// the two clamps of the preferred size are the identity, so the effective triple answers exactly
// what the formula that existed before the hard constraints did.
//
// Sections (1)-(11) are the API itself; (12) is the re-layout/repaint request every real change
// makes and every no-op does not. No window and no video subsystem: widget_t and item_t are pure
// offscreen code, like the other ui tests.

#include <cstddef>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <utility>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/ui/item_t.h"
#include "hwshqtb/ui/layout_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using hwshqtb::ui::item_t;
using hwshqtb::ui::layout_t;
using hwshqtb::ui::unbounded_extent;
using hwshqtb::ui::widget_t;

namespace size_constraint_test {

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

/// The interface contract: the const accessors are noexcept value getters.
static_assert(std::is_same_v<decltype(std::declval<const item_t&>().minimum_size()), extent_t>);
static_assert(std::is_same_v<decltype(std::declval<const item_t&>().maximum_size()), extent_t>);
static_assert(noexcept(std::declval<const item_t&>().minimum_size()));
static_assert(noexcept(std::declval<const item_t&>().maximum_size()));
static_assert(noexcept(std::declval<const item_t&>().has_fixed_size()));
static_assert(noexcept(std::declval<const item_t&>().fixed_size()));
static_assert(std::is_same_v<decltype(std::declval<const item_t&>().fixed_size()), extent_t>);
static_assert(std::is_same_v<decltype(std::declval<const item_t&>().effective_size_hint()),
                             extent_t>);
static_assert(std::is_same_v<decltype(std::declval<const item_t&>().effective_minimum_size()),
                             extent_t>);
static_assert(std::is_same_v<decltype(std::declval<const item_t&>().effective_maximum_size()),
                             extent_t>);

/// @brief A widget whose CONTENT opinion is the given hint, with a custom minimum hint.
/// @note The opinion is what a control reports; the hard constraints live on item_t and are never
///       read by these overrides.
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

/// @brief A minimal NON-widget item: the constraint API is the ITEM contract, not a widget one.
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

/// @brief A minimal WIDGET: the same probe, plus the widget layer's repaint record.
/// @note Damage belongs to the widget layer (an item that is not a widget has no pixels), so the
///       "a real change records damage" assertions of section (12a) use this probe.
class widget_probe_t final: public widget_t {
public:
    explicit widget_probe_t(extent_t hint) noexcept:
        _hint(hint) { }

    extent_t size_hint() const override {
        return _hint;
    }

    extent_t minimum_size_hint() const override {
        return extent_t{};
    }

private:
    extent_t _hint;
};

/// @brief A layout that only counts how often it ran: the re-layout request probe of section (12).
class counting_layout_t final: public layout_t {
public:
    void apply(widget_t&, rect_t) override {
        ++applies;
    }

    int applies = 0;
};

/// @brief The "no upper bound" extent, spelled out independently of the header's constant.
constexpr extent_t int_max_extent{ std::numeric_limits<int>::max(),
                                   std::numeric_limits<int>::max() };

}  // namespace size_constraint_test

using namespace size_constraint_test;

int main() {
    std::printf("== item size constraints: hard min/max, fixed size, effective triple ==\n");

    // ---- (1) the defaults: no constraint at all ----
    {
        sized_widget_t widget{ extent_t{ 50, 40 }, extent_t{ 10, 5 } };
        CHECK(widget.minimum_size() == extent_t{ 0, 0 });
        CHECK(widget.maximum_size() == unbounded_extent);
        CHECK(widget.maximum_size() == int_max_extent);
        CHECK(widget.maximum_size().width() == std::numeric_limits<int>::max());
        CHECK(widget.maximum_size().height() == std::numeric_limits<int>::max());
        CHECK(!widget.has_fixed_size());
        CHECK(widget.fixed_size() == extent_t{ 0, 0 });

        // The triple at rest: the opinion, its floor, and the unbounded ceiling.
        CHECK(widget.effective_size_hint() == extent_t{ 50, 40 });
        CHECK(widget.effective_minimum_size() == extent_t{ 10, 5 });
        CHECK(widget.effective_maximum_size() == unbounded_extent);

        // A bare widget has no opinion of its own: the triple is empty but the ceiling is unbounded.
        widget_t bare;
        CHECK(bare.minimum_size() == extent_t{ 0, 0 });
        CHECK(bare.maximum_size() == unbounded_extent);
        CHECK(!bare.has_fixed_size());
        CHECK(bare.effective_size_hint() == extent_t{ 0, 0 });
        CHECK(bare.effective_minimum_size() == extent_t{ 0, 0 });
        CHECK(bare.effective_maximum_size() == unbounded_extent);
    }

    // ---- (2) effective_size_hint() clamps the preferred size into [minimum, maximum] ----
    {
        // (2a) A hint BELOW the hard minimum is raised to it, per axis.
        sized_widget_t low{ extent_t{ 50, 40 }, extent_t{ 0, 0 } };
        low.set_minimum_size(extent_t{ 80, 20 });
        CHECK(low.effective_size_hint() == extent_t{ 80, 40 });  // width raised, height already above
        CHECK(low.size_hint() == extent_t{ 50, 40 });            // the opinion itself is untouched

        // (2b) A hint ABOVE the hard maximum is pressed down to it, per axis.
        sized_widget_t high{ extent_t{ 50, 40 }, extent_t{ 0, 0 } };
        high.set_maximum_size(extent_t{ 30, 90 });
        CHECK(high.effective_size_hint() == extent_t{ 30, 40 });  // width pressed, height below the cap
        CHECK(high.minimum_size() == extent_t{ 0, 0 });           // a maximum above the minimum leaves it

        // (2c) A hint INSIDE the interval is answered unchanged.
        sized_widget_t inside{ extent_t{ 50, 40 }, extent_t{ 0, 0 } };
        inside.set_minimum_size(extent_t{ 10, 10 });
        inside.set_maximum_size(extent_t{ 100, 100 });
        CHECK(inside.effective_size_hint() == extent_t{ 50, 40 });

        // (2d) Both bounds at once: the clamp is applied after the minimum-hint raise.
        sized_widget_t both{ extent_t{ 5, 5 }, extent_t{ 60, 0 } };
        both.set_minimum_size(extent_t{ 20, 20 });
        both.set_maximum_size(extent_t{ 40, 30 });
        // max(5, 60) is 60 on width, 5 on height; then [20, 40] x [20, 30] -> {40, 20}.
        CHECK(both.effective_size_hint() == extent_t{ 40, 20 });

        // (2e) The same clamp on a NON-widget item: this is item state, not widget state.
        item_probe_t probe{ extent_t{ 10, 10 } };
        probe.set_geometry(rect_t{ 0, 0, 10, 10 });
        CHECK(probe.effective_size_hint() == extent_t{ 10, 10 });
        probe.set_minimum_size(extent_t{ 25, 25 });
        CHECK(probe.effective_size_hint() == extent_t{ 25, 25 });
        probe.set_maximum_size(extent_t{ 15, 15 });  // below the minimum: the minimum follows it
        CHECK(probe.effective_size_hint() == extent_t{ 15, 15 });
    }

    // ---- (3) the preferred size is still max(size_hint(), minimum_size_hint()) ----
    //
    // The regression that the hard constraints did not REPLACE the minimum hint: a hint of {5, 5}
    // with a content minimum of {20, 10} answers {20, 10} even when the constraints are wide open.
    {
        sized_widget_t content{ extent_t{ 5, 5 }, extent_t{ 20, 10 } };
        CHECK(content.effective_size_hint() == extent_t{ 20, 10 });
        CHECK(content.effective_size_hint() != content.size_hint());  // not just the hint

        content.set_minimum_size(extent_t{ 1, 1 });  // a hard minimum BELOW the content minimum
        CHECK(content.effective_size_hint() == extent_t{ 20, 10 });

        content.set_maximum_size(extent_t{ 100, 100 });  // a ceiling far above both
        CHECK(content.effective_size_hint() == extent_t{ 20, 10 });

        // The other direction per axis: the hint wins where it is the larger one.
        sized_widget_t mixed{ extent_t{ 40, 5 }, extent_t{ 10, 20 } };
        CHECK(mixed.effective_size_hint() == extent_t{ 40, 20 });
    }

    // ---- (4) effective_minimum_size() = max(minimum_size_hint, minimum_size), capped by maximum ----
    {
        sized_widget_t widget{ extent_t{ 100, 100 }, extent_t{ 30, 20 } };
        CHECK(widget.effective_minimum_size() == extent_t{ 30, 20 });  // the content floor

        widget.set_minimum_size(extent_t{ 50, 10 });  // above the content minimum on width only
        CHECK(widget.effective_minimum_size() == extent_t{ 50, 20 });

        widget.set_maximum_size(extent_t{ 40, 200 });  // the cap lowers the width again
        CHECK(widget.effective_minimum_size() == extent_t{ 40, 20 });
        CHECK(widget.minimum_size() == extent_t{ 40, 10 });  // the hard minimum follows the cap

        // A maximum below the content minimum caps the floor: the window is [max, max] then.
        sized_widget_t squeezed{ extent_t{ 100, 100 }, extent_t{ 60, 60 } };
        squeezed.set_maximum_size(extent_t{ 25, 25 });
        CHECK(squeezed.effective_minimum_size() == extent_t{ 25, 25 });
        CHECK(squeezed.effective_size_hint() == extent_t{ 25, 25 });
    }

    // ---- (5) effective_maximum_size() is maximum_size(), nothing else ----
    {
        sized_widget_t widget{ extent_t{ 10, 10 }, extent_t{ 1, 1 } };
        CHECK(widget.effective_maximum_size() == widget.maximum_size());
        CHECK(widget.effective_maximum_size() == unbounded_extent);

        widget.set_maximum_size(extent_t{ 70, 80 });
        CHECK(widget.maximum_size() == extent_t{ 70, 80 });
        CHECK(widget.effective_maximum_size() == extent_t{ 70, 80 });

        // The maximum ignores both hints: a hint above it does not lift it.
        widget.set_maximum_size(extent_t{ 5, 5 });
        CHECK(widget.size_hint() == extent_t{ 10, 10 });
        CHECK(widget.effective_maximum_size() == extent_t{ 5, 5 });
    }

    // ---- (6) the invariant minimum_size() <= maximum_size(), in both directions ----
    {
        // (6a) A minimum above the current maximum RAISES the maximum to it.
        sized_widget_t up{ extent_t{ 10, 10 }, extent_t{ 0, 0 } };
        up.set_maximum_size(extent_t{ 100, 100 });
        up.set_minimum_size(extent_t{ 150, 50 });
        CHECK(up.minimum_size() == extent_t{ 150, 50 });
        CHECK(up.maximum_size() == extent_t{ 150, 100 });  // width raised, height untouched
        CHECK(!(up.minimum_size().width() > up.maximum_size().width()));
        CHECK(!(up.minimum_size().height() > up.maximum_size().height()));

        // (6b) A maximum below the current minimum LOWERS the minimum to it.
        sized_widget_t down{ extent_t{ 10, 10 }, extent_t{ 0, 0 } };
        down.set_minimum_size(extent_t{ 80, 80 });
        down.set_maximum_size(extent_t{ 40, 120 });
        CHECK(down.maximum_size() == extent_t{ 40, 120 });
        CHECK(down.minimum_size() == extent_t{ 40, 80 });  // width lowered, height untouched

        // (6c) The same rule per axis.
        sized_widget_t axis{ extent_t{ 10, 10 }, extent_t{ 0, 0 } };
        axis.set_minimum_width(90);
        axis.set_maximum_width(30);
        CHECK(axis.minimum_size().width() == 30);
        CHECK(axis.maximum_size().width() == 30);
        CHECK(axis.minimum_size().height() == 0);
        CHECK(axis.maximum_size().height() == std::numeric_limits<int>::max());

        // (6d) Repeating a value is not a change: the invariant state stays exactly as it was.
        axis.set_maximum_size(extent_t{ 30, std::numeric_limits<int>::max() });
        CHECK(axis.minimum_size() == extent_t{ 30, 0 });
        CHECK(axis.maximum_size() == extent_t{ 30, std::numeric_limits<int>::max() });
    }

    // ---- (7) a per-axis setter IS the whole setter with the other axis unchanged ----
    {
        sized_widget_t per_axis{ extent_t{ 10, 10 }, extent_t{ 0, 0 } };
        sized_widget_t whole{ extent_t{ 10, 10 }, extent_t{ 0, 0 } };
        per_axis.set_minimum_width(50);
        whole.set_minimum_size(extent_t{ 50, whole.minimum_size().height() });
        CHECK(per_axis.minimum_size() == whole.minimum_size());
        CHECK(per_axis.maximum_size() == whole.maximum_size());

        per_axis.set_minimum_height(60);
        whole.set_minimum_size(extent_t{ whole.minimum_size().width(), 60 });
        CHECK(per_axis.minimum_size() == whole.minimum_size());
        CHECK(per_axis.maximum_size() == whole.maximum_size());

        per_axis.set_maximum_width(120);
        whole.set_maximum_size(extent_t{ 120, whole.maximum_size().height() });
        CHECK(per_axis.minimum_size() == whole.minimum_size());
        CHECK(per_axis.maximum_size() == whole.maximum_size());

        per_axis.set_maximum_height(140);
        whole.set_maximum_size(extent_t{ whole.maximum_size().width(), 140 });
        CHECK(per_axis.minimum_size() == whole.minimum_size());
        CHECK(per_axis.maximum_size() == whole.maximum_size());
        CHECK(per_axis.minimum_size() == extent_t{ 50, 60 });
        CHECK(per_axis.maximum_size() == extent_t{ 120, 140 });

        // The unset axis really is untouched: the height ceiling is still the default one.
        sized_widget_t only_width{ extent_t{ 10, 10 }, extent_t{ 0, 0 } };
        only_width.set_minimum_width(50);
        CHECK(only_width.maximum_size().height() == std::numeric_limits<int>::max());
        only_width.set_maximum_width(70);
        CHECK(only_width.maximum_size().height() == std::numeric_limits<int>::max());
        CHECK(only_width.minimum_size() == extent_t{ 50, 0 });
        CHECK(only_width.maximum_size() == extent_t{ 70, std::numeric_limits<int>::max() });
    }

    // ---- (8) set_fixed_width()/set_fixed_height() pin ONE axis and leave the other alone ----
    {
        sized_widget_t widget{ extent_t{ 10, 10 }, extent_t{ 0, 0 } };
        widget.set_minimum_height(30);
        widget.set_maximum_height(90);

        widget.set_fixed_width(120);
        CHECK(widget.minimum_size().width() == 120);
        CHECK(widget.maximum_size().width() == 120);
        CHECK(widget.minimum_size().height() == 30);  // the height bounds survive untouched
        CHECK(widget.maximum_size().height() == 90);
        CHECK(!widget.has_fixed_size());  // one pinned axis is not a fixed size

        widget.set_fixed_height(70);
        CHECK(widget.minimum_size().height() == 70);
        CHECK(widget.maximum_size().height() == 70);
        CHECK(widget.minimum_size() == extent_t{ 120, 70 });
        CHECK(widget.maximum_size() == extent_t{ 120, 70 });
        CHECK(widget.has_fixed_size());  // both axes pinned at the same size

        // A fixed axis that crosses the current bounds wins over them (like Qt's setFixedWidth).
        widget.set_fixed_width(200);
        CHECK(widget.minimum_size().width() == 200);
        CHECK(widget.maximum_size().width() == 200);
        CHECK(widget.fixed_size() == extent_t{ 200, 70 });
    }

    // ---- (9) has_fixed_size(): both axes pinned at the SAME NON-EMPTY size ----
    {
        sized_widget_t widget{ extent_t{ 10, 10 }, extent_t{ 0, 0 } };
        CHECK(!widget.has_fixed_size());

        // One axis pinned: false.
        widget.set_fixed_width(40);
        CHECK(!widget.has_fixed_size());
        widget.set_minimum_width(40);  // still one axis
        CHECK(!widget.has_fixed_size());

        // Both axes pinned, but at different sizes: false.
        widget.set_fixed_height(25);
        widget.set_maximum_height(60);  // the height is no longer pinned
        CHECK(widget.minimum_size() == extent_t{ 40, 25 });
        CHECK(widget.maximum_size() == extent_t{ 40, 60 });
        CHECK(!widget.has_fixed_size());

        // Both axes pinned at one non-empty size through the PAIR of setters: true. The flag is
        // derived from min/max, so a constraint built axis by axis counts exactly like set_fixed_size.
        sized_widget_t derived{ extent_t{ 10, 10 }, extent_t{ 0, 0 } };
        derived.set_minimum_width(35);
        derived.set_maximum_width(35);
        derived.set_minimum_height(45);
        derived.set_maximum_height(45);
        CHECK(derived.has_fixed_size());
        CHECK(derived.fixed_size() == extent_t{ 35, 45 });

        // set_fixed_size() to an empty size is "no constraint": false, and fixed_size() answers {0,0}.
        sized_widget_t empty{ extent_t{ 10, 10 }, extent_t{ 0, 0 } };
        empty.set_fixed_size(extent_t{ 0, 0 });
        CHECK(empty.minimum_size() == extent_t{ 0, 0 });
        CHECK(empty.maximum_size() == extent_t{ 0, 0 });
        CHECK(!empty.has_fixed_size());
        CHECK(empty.fixed_size() == extent_t{ 0, 0 });

        // A zero on ONE axis of an otherwise fixed size is still empty: false.
        sized_widget_t flat{ extent_t{ 10, 10 }, extent_t{ 0, 0 } };
        flat.set_fixed_size(extent_t{ 0, 50 });
        CHECK(!flat.has_fixed_size());
        CHECK(flat.fixed_size() == extent_t{ 0, 0 });

        // And the ordinary fixed size: true, reported back unchanged.
        sized_widget_t fixed{ extent_t{ 10, 10 }, extent_t{ 0, 0 } };
        fixed.set_fixed_size(extent_t{ 70, 40 });
        CHECK(fixed.has_fixed_size());
        CHECK(fixed.fixed_size() == extent_t{ 70, 40 });
        CHECK(fixed.effective_size_hint() == extent_t{ 70, 40 });
        CHECK(fixed.size_hint() == extent_t{ 10, 10 });  // the opinion is still the content's
    }

    // ---- (10) set_fixed_size() overwrites BOTH bounds: no stale constraint survives ----
    {
        sized_widget_t widget{ extent_t{ 10, 10 }, extent_t{ 5, 5 } };
        widget.set_minimum_size(extent_t{ 20, 30 });
        widget.set_maximum_size(extent_t{ 500, 600 });
        CHECK(widget.minimum_size() == extent_t{ 20, 30 });
        CHECK(widget.maximum_size() == extent_t{ 500, 600 });

        widget.set_fixed_size(extent_t{ 70, 40 });
        CHECK(widget.minimum_size() == extent_t{ 70, 40 });  // the old minimum is gone
        CHECK(widget.maximum_size() == extent_t{ 70, 40 });  // ... and so is the old maximum
        CHECK(widget.fixed_size() == extent_t{ 70, 40 });

        // A fixed size BELOW the previous minimum is not clamped by the stale bound either.
        widget.set_fixed_size(extent_t{ 2, 3 });
        CHECK(widget.minimum_size() == extent_t{ 2, 3 });
        CHECK(widget.maximum_size() == extent_t{ 2, 3 });

        // A later minimum above the fixed size raises the maximum again: the pair stays consistent.
        widget.set_minimum_size(extent_t{ 90, 95 });
        CHECK(widget.minimum_size() == extent_t{ 90, 95 });
        CHECK(widget.maximum_size() == extent_t{ 90, 95 });
        CHECK(widget.has_fixed_size());
        CHECK(widget.fixed_size() == extent_t{ 90, 95 });
    }

    // ---- (11) a negative component is treated as 0 ----
    {
        sized_widget_t widget{ extent_t{ 10, 10 }, extent_t{ 0, 0 } };
        widget.set_minimum_size(extent_t{ -5, -7 });
        CHECK(widget.minimum_size() == extent_t{ 0, 0 });

        widget.set_maximum_size(extent_t{ -1, -2 });
        CHECK(widget.maximum_size() == extent_t{ 0, 0 });
        CHECK(widget.minimum_size() == extent_t{ 0, 0 });

        widget.set_minimum_size(extent_t{ 30, 40 });
        widget.set_maximum_size(extent_t{ 100, 200 });
        widget.set_minimum_width(-9);
        CHECK(widget.minimum_size().width() == 0);
        widget.set_minimum_height(-9);
        CHECK(widget.minimum_size().height() == 0);
        widget.set_maximum_width(-9);
        CHECK(widget.maximum_size().width() == 0);
        CHECK(widget.minimum_size().width() == 0);  // the minimum follows the lowered maximum
        widget.set_maximum_height(-9);
        CHECK(widget.maximum_size() == extent_t{ 0, 0 });
        CHECK(widget.minimum_size() == extent_t{ 0, 0 });

        widget.set_fixed_size(extent_t{ -3, -4 });
        CHECK(widget.minimum_size() == extent_t{ 0, 0 });
        CHECK(widget.maximum_size() == extent_t{ 0, 0 });
        CHECK(!widget.has_fixed_size());

        widget.set_fixed_width(-6);
        CHECK(widget.minimum_size().width() == 0);
        CHECK(widget.maximum_size().width() == 0);
        widget.set_fixed_height(-6);
        CHECK(widget.minimum_size().height() == 0);
        CHECK(widget.maximum_size().height() == 0);
    }

    // ---- (12) a REAL change requests a re-layout/repaint; a no-op requests nothing ----
    {
        // (12a) The WIDGET layer records damage: an item that is not a widget has no pixels.
        widget_probe_t probe{ extent_t{ 10, 10 } };
        probe.set_geometry(rect_t{ 0, 0, 40, 30 });
        probe.clear_damage();
        probe.set_minimum_size(extent_t{ 10, 10 });
        CHECK(!probe.damage().is_empty());

        probe.clear_damage();
        probe.set_minimum_size(extent_t{ 10, 10 });  // the same value again
        CHECK(probe.damage().is_empty());

        probe.clear_damage();
        probe.set_maximum_size(extent_t{ 20, 20 });
        CHECK(!probe.damage().is_empty());
        probe.clear_damage();
        probe.set_maximum_size(extent_t{ 20, 20 });
        CHECK(probe.damage().is_empty());

        // A negative value that clamps to the current one is NOT a change.
        probe.clear_damage();
        probe.set_minimum_size(extent_t{ 0, 0 });  // {10, 10} -> {0, 0}: a real change
        CHECK(!probe.damage().is_empty());
        probe.clear_damage();
        probe.set_minimum_size(extent_t{ -1, -1 });  // clamps to {0, 0}: no change
        CHECK(probe.damage().is_empty());

        // Per-axis setters request on a change as well.
        probe.clear_damage();
        probe.set_fixed_width(15);
        CHECK(!probe.damage().is_empty());

        // (12b) The widget layer re-applies the PARENT's layout: a child's new bound reaches the
        //       layout that owns the child's place, and the frame is requested through update().
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 200, 100 });
        sized_widget_t child{ extent_t{ 10, 10 }, extent_t{ 0, 0 }, &parent };
        child.set_geometry(rect_t{ 5, 5, 20, 20 });  // a placed child (an empty item damages nothing)
        auto* const layout = new counting_layout_t;
        parent.set_layout(layout);
        const int applies_before = layout->applies;
        child.clear_damage();
        child.set_minimum_size(extent_t{ 50, 50 });
        CHECK(layout->applies > applies_before);  // the parent's layout ran again
        CHECK(!child.damage().is_empty());        // ... and the repaint was requested

        const int applies_after = layout->applies;
        child.clear_damage();
        child.set_minimum_size(extent_t{ 50, 50 });  // no change
        CHECK(layout->applies == applies_after);
        CHECK(child.damage().is_empty());

        // set_fixed_size() is a real change on both axes: it requests too.
        child.clear_damage();
        child.set_fixed_size(extent_t{ 60, 30 });
        CHECK(!child.damage().is_empty());
        CHECK(layout->applies > applies_after);
    }

    // ---- (13) no maximum installed => the triple is the PREVIOUS behaviour, bit for bit ----
    //
    // The clamps added to the preferred size are the identity while maximum_size() keeps its
    // default, so the answers the layouts get cannot move.
    {
        const extent_t hints[] = { extent_t{ 0, 0 }, extent_t{ 5, 5 }, extent_t{ 10, 4 },
                                   extent_t{ 200, 3 } };
        const extent_t minimum_hints[] = { extent_t{ 0, 0 }, extent_t{ 20, 10 }, extent_t{ 3, 7 },
                                           extent_t{ 1, 400 } };
        for (const extent_t hint : hints) {
            for (const extent_t minimum_hint : minimum_hints) {
                sized_widget_t widget{ hint, minimum_hint };
                // The old formula: size_hint() with each axis raised to minimum_size_hint().
                extent_t expected = hint;
                if (expected.width() < minimum_hint.width()) {
                    expected.set_width(minimum_hint.width());
                }
                if (expected.height() < minimum_hint.height()) {
                    expected.set_height(minimum_hint.height());
                }
                CHECK(widget.effective_size_hint() == expected);
                CHECK(widget.effective_minimum_size() == minimum_hint);  // hard minimum is {0,0}
                CHECK(widget.effective_maximum_size() == unbounded_extent);
            }
        }

        // A minimum_size the opinion already satisfies changes nothing either (the clamp is the
        // identity inside the interval), while a minimum above the opinion still raises it.
        sized_widget_t satisfied{ extent_t{ 100, 100 }, extent_t{ 0, 0 } };
        satisfied.set_minimum_size(extent_t{ 50, 50 });
        CHECK(satisfied.effective_size_hint() == extent_t{ 100, 100 });
        satisfied.set_minimum_size(extent_t{ 150, 150 });
        CHECK(satisfied.effective_size_hint() == extent_t{ 150, 150 });
    }

    if (g_failures == 0) {
        std::printf("size constraints: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("size constraints: %d of %d checks failed\n", g_failures, g_checks);
    return 1;
}
