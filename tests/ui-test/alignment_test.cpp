// Alignment: item_t::alignment() and the placement of a box/grid child inside a cell LARGER than it.
//
// The cell model these sections pin (the same one Qt's layouts use):
//  - a box layout gives a child its cross axis the whole content extent as the cell, and its MAIN
//    axis exactly the resolved slot;
//  - a grid gives a child the union of the columns/rows it covers (the spacing between them included)
//    as the cell;
//  - the item's own extent inside that cell is what the size model resolved: the slot on a box main
//    axis, the content/slot extent capped by effective_maximum_size() on a cross axis, the fixed size
//    for a pinned item, the union capped by the maximum in a grid.
// Alignment only decides WHERE that extent sits: zero bits fill the cell at its origin (the behaviour
// before alignment existed), and a cell no larger than the item has no room for the bits.
//
// Two Qt sources are the reference for the rules, and both are cited where the rule is pinned:
//  - qlayoutitem.cpp / QWidgetItem::setGeometry: AlignRight is tested before AlignLeft and the
//    centring is the fallback (AlignBottom before AlignTop likewise), so a contradictory pair is
//    resolved to RIGHT/BOTTOM rather than refused; setAlignment() stores the bits unchanged;
//  - qboxlayout.cpp / QBoxLayout::setGeometry: a box item's cell on the main axis is its chain slot
//    (`a.at(i).size`) and the cross axis is `s.height()`/`s.width()`, i.e. the whole content extent.
// Qt's QWidgetItem centres an axis WITHOUT bits when the item is smaller than the cell; this library
// deliberately keeps the origin on such an axis ("zero = fill the cell"), because that is the
// pre-alignment behaviour every existing layout test pins to the pixel.
//
// The sections:
//  (1) the regression: with no alignment bit set, hbox / vbox / grid keep the exact geometry they had
//      before alignment existed (including a fixed child and a maximum-capped child);
//  (2) hbox: the main axis has no slack (the slot IS the cell), so HCENTER/RIGHT/LEFT cannot move the
//      child horizontally; the cross (vertical) axis is where the bits place it, and only the bits of
//      that axis are read;
//  (3) vbox mirrors (2) with the axes swapped: VCENTER/BOTTOM/TOP are inert on the main axis and the
//      horizontal bits place the child inside the cross cell;
//  (4) grid: both axes are real cells, so centring/right/bottom are exact, each axis independently,
//      and the contradictory combinations follow Qt's precedence;
//  (5) grid: a spanning item's cell is the union of its lines (the spacing included), not one line;
//  (6) an overfull axis ignores the bits (the item is cut or the fixed box overruns its cell) and an
//      overfull cross cell keeps the origin;
//  (7) the item API: the default, the round trip, the stored (unsanitised) bits and the fact that
//      set_alignment() changes no geometry until the layout is applied again;
//  (8) box MAIN axis: an alignment bit caps that axis at the child's hint and takes it out of the
//      expansion distribution (Qt's qSmartMaxSize), so an EXPANDING child with a bit keeps its hint
//      extent and the spare goes to its siblings or stays unused — while the cell is still the slot,
//      so HCENTER/RIGHT/LEFT never shift it. The per-axis rule, the shortage paths that ignore the
//      bits, and an unaligned EXPANDING child (which still fills the axis) are covered too;
//  (9) label_t: text_alignment()/set_text_alignment() place the text INSIDE the label (pinned through
//      the recorded glyph run), while the inherited alignment()/set_alignment() place the label in its
//      layout cell. The compile-time assertions prove which member each name resolves to.
//
// No window and no video subsystem: widget_t and the layouts are pure offscreen code.

#include <cstdio>
#include <type_traits>
#include <vector>

#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/ui/box_layout_t.h"
#include "hwshqtb/ui/grid_layout_t.h"
#include "hwshqtb/ui/item_t.h"
#include "hwshqtb/ui/label_t.h"
#include "hwshqtb/ui/style_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using hwshqtb::ui::alignment_flag;
using hwshqtb::ui::alignment_t;
using hwshqtb::ui::default_style;
using hwshqtb::ui::grid_layout_t;
using hwshqtb::ui::hbox_layout_t;
using hwshqtb::ui::horizontal_alignment_mask;
using hwshqtb::ui::label_t;
using hwshqtb::ui::pixel_metric_enum;
using hwshqtb::ui::size_policy_enum;
using hwshqtb::ui::text_alignment_enum;
using hwshqtb::ui::vbox_layout_t;
using hwshqtb::ui::vertical_alignment_mask;
using hwshqtb::ui::widget_t;

namespace alignment_test {

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

/// @brief A widget whose content is exactly the given hint (no separate minimum hint).
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

/// @brief A widget with a content floor as well, so an overfull axis can cut it below the hint.
class floored_t final: public widget_t {
public:
    floored_t(extent_t hint, extent_t minimum_hint, widget_t* parent = nullptr):
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

/// @brief @p flags as an alignment_t (the fixture vocabulary: one flag at a time is verbose).
constexpr alignment_t as_alignment(alignment_flag flags) {
    return alignment_t{ flags };
}

/// @brief A paint engine that rasterises nothing and records the glyph run origin.
///
/// The label's text alignment moves the run WITHIN the label's rectangle, which no geometry accessor
/// reports: the only observable is where paint_event() draws. This engine records exactly that, and
/// never touches the video subsystem.
class recording_paint_engine_t final: public paint_engine_t {
public:
    engine_feature_t features() const noexcept override {
        return engine_feature_t{};
    }

    bool begin(paint_device_t&) override {
        active = true;
        return true;
    }

    bool end() override {
        active = false;
        return true;
    }

    void update_state(const engine_state_t& state) override {
        last_state = state;
    }

    void draw_rects(const rect_t* rects, int count, bool fill) override {
        for (int i = 0; i < count; ++i) {
            this->rects.push_back(rects[i]);
        }
        last_fill = fill;
    }

    void draw_glyphs(const glyph_run_t& run) override {
        // A run no longer carries a separate origin: the first glyph's baseline IS where it starts.
        origin = run.count > 0 ? run.positions[0] : point_t{};
        positions.assign(run.positions, run.positions + run.count);
        ++glyph_runs;
    }
    void draw_image(const rect_t&, const image_view_t&) override { }

    bool active = false;
    int glyph_runs = 0;
    bool last_fill = false;
    engine_state_t last_state;
    std::vector<rect_t> rects;
    point_t origin;
    std::vector<point_t> positions;
};

/// @brief A paint device that hands out the recording engine (never rasterises, never touches SDL).
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

/// @brief The x of the single glyph run @p target paints into a @p width x @p height device.
/// @return The run origin's x, or -1 when the pass produced no run at all.
/// @note Paints the item alone: the device is the label's own rectangle, so the run origin is in the
///       label's coordinates and no parent is involved.
int painted_text_x(label_t& target, int width, int height) {
    recording_device_t device{ extent_t{ width, height } };
    painter_t painter{ device };
    if (!painter.begin()) {
        return -1;
    }
    target.render(painter);
    painter.end();
    return device.engine().glyph_runs == 1 ? device.engine().origin.x() : -1;
}

}  // namespace alignment_test

using namespace alignment_test;

int main() {
    std::printf("== ui: item alignment inside a larger cell ==\n");

    // The interface is pinned at compile time: alignment_t is the flag wrapper, and the getter is the
    // noexcept one the layouts call.
    static_assert(std::is_same_v<decltype(&hwshqtb::ui::item_t::alignment),
                                 alignment_t (hwshqtb::ui::item_t::*)() const noexcept>);
    static_assert(std::is_same_v<decltype(&hwshqtb::ui::item_t::set_alignment),
                                 void (hwshqtb::ui::item_t::*)(alignment_t)>);
    static_assert(std::is_same_v<alignment_t,
                                 hwshqtb::metaobject::flag_t<alignment_flag>>);
    static_assert((horizontal_alignment_mask & vertical_alignment_mask) == alignment_flag::NONE);
    static_assert(as_alignment(alignment_flag::NONE) == alignment_flag::NONE);

    // ---- (1) regression: no alignment bit => the geometry of the pre-alignment layouts ----
    {
        // (1a) hbox, spacing 5, a maximum-capped child and a fixed child in the row.
        widget_t hbox_parent;
        hbox_parent.set_geometry(rect_t{ 0, 0, 300, 100 });
        sized_t plain{ extent_t{ 40, 20 }, &hbox_parent };
        sized_t capped{ extent_t{ 60, 30 }, &hbox_parent };
        sized_t pinned{ extent_t{ 80, 40 }, &hbox_parent };
        capped.set_maximum_height(30);                      // cross axis: 100 -> 30
        pinned.set_fixed_size(extent_t{ 30, 25 });          // main 80 -> 30, cross 25

        auto* const hbox = new hbox_layout_t;
        hbox->set_spacing(5);
        hbox_parent.set_layout(hbox);

        CHECK(plain.alignment() == alignment_flag::NONE);
        CHECK(capped.alignment() == alignment_flag::NONE);
        CHECK(pinned.alignment() == alignment_flag::NONE);
        // 40 + 60 + 30 = 130, two gaps of 5, so 160 of the 300 are used and the rest stays unused.
        CHECK(plain.geometry() == rect_t{ 0, 0, 40, 100 });
        CHECK(capped.geometry() == rect_t{ 45, 0, 60, 30 });
        CHECK(pinned.geometry() == rect_t{ 110, 0, 30, 25 });
        // The layout's own hints are a size question, not a placement one: alignment never enters.
        // 40 + 60 + 30 + two gaps of 5 = 140 wide, and the cross hint is the largest child hint (30).
        CHECK(hbox->size_hint() == extent_t{ 140, 30 });

        // (1b) vbox mirrors it along y.
        widget_t vbox_parent;
        vbox_parent.set_geometry(rect_t{ 0, 0, 100, 300 });
        sized_t v_plain{ extent_t{ 20, 40 }, &vbox_parent };
        sized_t v_capped{ extent_t{ 30, 60 }, &vbox_parent };
        sized_t v_pinned{ extent_t{ 40, 80 }, &vbox_parent };
        v_capped.set_maximum_width(30);                     // cross axis: 100 -> 30
        v_pinned.set_fixed_size(extent_t{ 25, 30 });

        auto* const vbox = new vbox_layout_t;
        vbox->set_spacing(5);
        vbox_parent.set_layout(vbox);

        CHECK(v_plain.geometry() == rect_t{ 0, 0, 100, 40 });
        CHECK(v_capped.geometry() == rect_t{ 0, 45, 30, 60 });
        CHECK(v_pinned.geometry() == rect_t{ 0, 110, 25, 30 });
        // 40 + 60 + 30 + two gaps of 5 = 140 tall, and the cross hint is the largest child hint (30).
        CHECK(vbox->size_hint() == extent_t{ 30, 140 });

        // (1c) grid, spacing 10, four plain children: each cell is its column x its row.
        widget_t grid_parent;
        grid_parent.set_geometry(rect_t{ 0, 0, 200, 150 });
        sized_t g1{ extent_t{ 50, 20 }, &grid_parent };
        sized_t g2{ extent_t{ 70, 30 }, &grid_parent };
        sized_t g3{ extent_t{ 60, 40 }, &grid_parent };
        sized_t g4{ extent_t{ 40, 25 }, &grid_parent };

        auto* const grid = new grid_layout_t;
        grid->set_spacing(10);
        grid->add(&g1, 0, 0);
        grid->add(&g2, 0, 1);
        grid->add(&g3, 1, 0);
        grid->add(&g4, 1, 1);
        grid_parent.set_layout(grid);

        // Columns 60 / 70, rows 30 / 40 (no maximum, so every item fills its cell exactly).
        CHECK(g1.geometry() == rect_t{ 0, 0, 60, 30 });
        CHECK(g2.geometry() == rect_t{ 70, 0, 70, 30 });
        CHECK(g3.geometry() == rect_t{ 0, 40, 60, 40 });
        CHECK(g4.geometry() == rect_t{ 70, 40, 70, 40 });
        CHECK(grid->size_hint() == extent_t{ 140, 80 });

        // An explicit zero is the same state as "never set": the geometry does not move.
        plain.set_alignment(as_alignment(alignment_flag::NONE));
        g2.set_alignment(as_alignment(alignment_flag::NONE));
        hbox_parent.relayout();
        grid_parent.relayout();
        CHECK(plain.geometry() == rect_t{ 0, 0, 40, 100 });
        CHECK(g2.geometry() == rect_t{ 70, 0, 70, 30 });
        CHECK(plain.alignment() == alignment_flag::NONE);
    }

    // ---- (2) hbox: the main axis is the slot, the cross axis is the cell ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 300, 100 });
        sized_t child{ extent_t{ 40, 20 }, &parent };
        child.set_maximum_size(extent_t{ 60, 30 });     // the child is smaller than its cell on both axes
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);

        CHECK(child.geometry() == rect_t{ 0, 0, 40, 30 });

        // Horizontal bits on the MAIN axis: the resolved slot is 40 wide and the child is 40 wide, so
        // there is no room to move it (Qt hands QBoxLayout's item a cell of `a.at(i).size`). Its size
        // never changes either: alignment moves, it does not resize.
        child.set_alignment(as_alignment(alignment_flag::HCENTER));
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 0, 0, 40, 30 });
        CHECK(child.geometry().width() == 40);
        CHECK(child.geometry().height() == 30);
        child.set_alignment(as_alignment(alignment_flag::RIGHT));
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 0, 0, 40, 30 });
        child.set_alignment(as_alignment(alignment_flag::LEFT));
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 0, 0, 40, 30 });   // LEFT is the default horizontal behaviour

        // The CROSS axis is where the hbox cell (100 tall) has room: 100 - 30 = 70 to place.
        child.set_alignment(as_alignment(alignment_flag::TOP));
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 0, 0, 40, 30 });
        child.set_alignment(as_alignment(alignment_flag::VCENTER));
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 0, 35, 40, 30 });
        child.set_alignment(as_alignment(alignment_flag::BOTTOM));
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 0, 70, 40, 30 });

        // Each axis independently: an odd spare pixel stays on the right/bottom, and the axis without
        // a bit keeps the origin (fill), so a horizontal bit leaves the vertical placement alone.
        child.set_alignment(as_alignment(alignment_flag::HCENTER));
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 0, 0, 40, 30 });
        child.set_alignment(as_alignment(alignment_flag::RIGHT) | alignment_flag::BOTTOM);
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 0, 70, 40, 30 });

        // A second child proves the row itself does not move: the aligned child stays at its slot.
        widget_t pair_parent;
        pair_parent.set_geometry(rect_t{ 0, 0, 300, 100 });
        sized_t first{ extent_t{ 40, 20 }, &pair_parent };
        sized_t second{ extent_t{ 50, 20 }, &pair_parent };
        first.set_maximum_size(extent_t{ 60, 30 });
        auto* const pair = new hbox_layout_t;
        pair->set_spacing(0);
        pair_parent.set_layout(pair);

        first.set_alignment(as_alignment(alignment_flag::HCENTER) | alignment_flag::VCENTER);
        pair_parent.relayout();
        CHECK(first.geometry() == rect_t{ 0, 35, 40, 30 });
        CHECK(second.geometry() == rect_t{ 40, 0, 50, 100 });
    }

    // ---- (3) vbox mirrors (2): the cross axis is horizontal ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 300 });
        sized_t child{ extent_t{ 40, 20 }, &parent };
        child.set_maximum_size(extent_t{ 30, 60 });
        auto* const layout = new vbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);

        CHECK(child.geometry() == rect_t{ 0, 0, 30, 20 });

        // Vertical bits on the MAIN axis: the slot is 20 tall and the child is 20 tall, no slack.
        child.set_alignment(as_alignment(alignment_flag::VCENTER));
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 0, 0, 30, 20 });
        child.set_alignment(as_alignment(alignment_flag::BOTTOM));
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 0, 0, 30, 20 });
        child.set_alignment(as_alignment(alignment_flag::TOP));
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 0, 0, 30, 20 });

        // The horizontal cell is 100 wide against a 30-wide child: 70 pixels to place.
        child.set_alignment(as_alignment(alignment_flag::LEFT));
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 0, 0, 30, 20 });
        child.set_alignment(as_alignment(alignment_flag::HCENTER));
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 35, 0, 30, 20 });
        child.set_alignment(as_alignment(alignment_flag::RIGHT));
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 70, 0, 30, 20 });

        // Only vertical bits: the horizontal axis still fills from the cell origin.
        child.set_alignment(as_alignment(alignment_flag::VCENTER) | alignment_flag::BOTTOM);
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 0, 0, 30, 20 });
    }

    // ---- (4) grid: both axes are cells, each independent, Qt's precedence for contradictions ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 200, 200 });
        // The shared line, not the item, sizes the cell: b widens column 0 to 100 and c heightens row 0
        // to 50, so the cell of `target` is 100 x 50 while the item is capped to 30 x 20.
        sized_t target{ extent_t{ 80, 60 }, &parent };
        sized_t taller{ extent_t{ 100, 10 }, &parent };
        sized_t wider{ extent_t{ 10, 50 }, &parent };
        target.set_maximum_size(extent_t{ 30, 20 });
        auto* const grid = new grid_layout_t;
        grid->set_spacing(0);
        grid->add(&target, 0, 0);
        grid->add(&taller, 1, 0);
        grid->add(&wider, 0, 1);
        parent.set_layout(grid);

        CHECK(target.geometry() == rect_t{ 0, 0, 30, 20 });
        CHECK(taller.geometry() == rect_t{ 0, 50, 100, 10 });
        CHECK(wider.geometry() == rect_t{ 100, 0, 10, 50 });

        target.set_alignment(as_alignment(alignment_flag::HCENTER) | alignment_flag::VCENTER);
        parent.relayout();
        CHECK(target.geometry() == rect_t{ 35, 15, 30, 20 });   // (100-30)/2, (50-20)/2
        CHECK(target.geometry().width() == 30);                 // placed, never resized
        CHECK(target.geometry().height() == 20);
        CHECK(taller.geometry() == rect_t{ 0, 50, 100, 10 });  // the neighbours keep their cells
        CHECK(wider.geometry() == rect_t{ 100, 0, 10, 50 });

        target.set_alignment(as_alignment(alignment_flag::RIGHT) | alignment_flag::BOTTOM);
        parent.relayout();
        CHECK(target.geometry() == rect_t{ 70, 30, 30, 20 });
        target.set_alignment(as_alignment(alignment_flag::LEFT) | alignment_flag::TOP);
        parent.relayout();
        CHECK(target.geometry() == rect_t{ 0, 0, 30, 20 });

        // One axis only: the other one keeps the cell origin (and the item its size).
        target.set_alignment(as_alignment(alignment_flag::HCENTER));
        parent.relayout();
        CHECK(target.geometry() == rect_t{ 35, 0, 30, 20 });
        target.set_alignment(as_alignment(alignment_flag::VCENTER));
        parent.relayout();
        CHECK(target.geometry() == rect_t{ 0, 15, 30, 20 });

        // The contradictory pairs: Qt resolves them at placement time (qlayoutitem.cpp), it never
        // refuses them, so RIGHT wins over LEFT and HCENTER, and BOTTOM over TOP and VCENTER.
        target.set_alignment(as_alignment(alignment_flag::LEFT) | alignment_flag::HCENTER);
        parent.relayout();
        CHECK(target.geometry() == rect_t{ 0, 0, 30, 20 });      // LEFT suppresses the centring
        target.set_alignment(as_alignment(alignment_flag::RIGHT) | alignment_flag::HCENTER);
        parent.relayout();
        CHECK(target.geometry() == rect_t{ 70, 0, 30, 20 });      // RIGHT is tested first
        target.set_alignment(as_alignment(alignment_flag::LEFT) | alignment_flag::RIGHT);
        parent.relayout();
        CHECK(target.geometry() == rect_t{ 70, 0, 30, 20 });      // RIGHT wins, no error
        target.set_alignment(as_alignment(alignment_flag::LEFT) | alignment_flag::RIGHT
                             | alignment_flag::HCENTER);
        parent.relayout();
        CHECK(target.geometry() == rect_t{ 70, 0, 30, 20 });
        target.set_alignment(as_alignment(alignment_flag::TOP) | alignment_flag::VCENTER);
        parent.relayout();
        CHECK(target.geometry() == rect_t{ 0, 0, 30, 20 });      // TOP suppresses the centring
        target.set_alignment(as_alignment(alignment_flag::BOTTOM) | alignment_flag::VCENTER);
        parent.relayout();
        CHECK(target.geometry() == rect_t{ 0, 30, 30, 20 });
        target.set_alignment(as_alignment(alignment_flag::TOP) | alignment_flag::BOTTOM);
        parent.relayout();
        CHECK(target.geometry() == rect_t{ 0, 30, 30, 20 });     // BOTTOM wins, no error

        // The bits are stored verbatim (QLayoutItem::setAlignment is a plain assignment).
        CHECK(target.alignment() == (alignment_t{ alignment_flag::TOP } | alignment_flag::BOTTOM));
    }

    // ---- (5) grid: a spanning item's cell is the union of its lines, spacing included ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 300, 300 });
        sized_t span{ extent_t{ 80, 80 }, &parent };
        sized_t top_left{ extent_t{ 60, 20 }, &parent };
        sized_t top_right{ extent_t{ 70, 10 }, &parent };
        sized_t bottom_left{ extent_t{ 10, 30 }, &parent };
        sized_t bottom_right{ extent_t{ 10, 40 }, &parent };
        span.set_maximum_size(extent_t{ 40, 40 });   // ceil(40 / 2) = 20 per covered line
        auto* const grid = new grid_layout_t;
        grid->set_spacing(10);
        grid->add(&span, 0, 0, 2, 2);
        grid->add(&top_left, 0, 0);
        grid->add(&top_right, 0, 1);
        grid->add(&bottom_left, 1, 0);
        grid->add(&bottom_right, 1, 1);
        parent.set_layout(grid);

        // Columns 60 / 70, rows 20 / 40: the union cell of the span is (60+10+70) x (20+10+40).
        CHECK(span.geometry() == rect_t{ 0, 0, 40, 40 });
        CHECK(top_left.geometry() == rect_t{ 0, 0, 60, 20 });
        CHECK(top_right.geometry() == rect_t{ 70, 0, 70, 20 });
        CHECK(bottom_left.geometry() == rect_t{ 0, 30, 60, 40 });
        CHECK(bottom_right.geometry() == rect_t{ 70, 30, 70, 40 });

        span.set_alignment(as_alignment(alignment_flag::HCENTER) | alignment_flag::VCENTER);
        parent.relayout();
        CHECK(span.geometry() == rect_t{ 50, 15, 40, 40 });       // (140-40)/2, (70-40)/2
        span.set_alignment(as_alignment(alignment_flag::RIGHT) | alignment_flag::BOTTOM);
        parent.relayout();
        CHECK(span.geometry() == rect_t{ 100, 30, 40, 40 });      // 140-40, 70-40
        span.set_alignment(as_alignment(alignment_flag::LEFT) | alignment_flag::TOP);
        parent.relayout();
        CHECK(span.geometry() == rect_t{ 0, 0, 40, 40 });
    }

    // ---- (6) an overfull axis ignores the bits ----
    {
        // (6a) hbox too narrow for the floors: the overfull rule cuts the slots, and the slots stay the
        // children's cells, so alignment has no room. The rects are the unaligned overfull result.
        widget_t crowded;
        crowded.set_geometry(rect_t{ 0, 0, 100, 40 });
        floored_t a{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &crowded };
        floored_t b{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &crowded };
        floored_t c{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &crowded };
        auto* const cut = new hbox_layout_t;
        cut->set_spacing(0);
        crowded.set_layout(cut);

        CHECK(a.geometry() == rect_t{ 0, 0, 34, 40 });
        CHECK(b.geometry() == rect_t{ 34, 0, 33, 40 });
        CHECK(c.geometry() == rect_t{ 67, 0, 33, 40 });
        a.set_alignment(as_alignment(alignment_flag::HCENTER) | alignment_flag::VCENTER);
        b.set_alignment(as_alignment(alignment_flag::HCENTER) | alignment_flag::VCENTER);
        c.set_alignment(as_alignment(alignment_flag::HCENTER) | alignment_flag::VCENTER);
        crowded.relayout();
        CHECK(a.geometry() == rect_t{ 0, 0, 34, 40 });
        CHECK(b.geometry() == rect_t{ 34, 0, 33, 40 });
        CHECK(c.geometry() == rect_t{ 67, 0, 33, 40 });
        CHECK(c.geometry().right() == 100);   // the row still fits: the bits did not push it out

        // (6b) a fixed child taller than the cross cell: it overruns the cell from the origin, so the
        // vertical bits cannot move it (cell - item is negative).
        widget_t overrun;
        overrun.set_geometry(rect_t{ 0, 0, 100, 40 });
        sized_t tall{ extent_t{ 20, 60 }, &overrun };
        tall.set_fixed_size(extent_t{ 20, 80 });
        auto* const overrun_layout = new hbox_layout_t;
        overrun_layout->set_spacing(0);
        overrun.set_layout(overrun_layout);
        CHECK(tall.geometry() == rect_t{ 0, 0, 20, 80 });

        tall.set_alignment(as_alignment(alignment_flag::RIGHT) | alignment_flag::BOTTOM
                           | alignment_flag::VCENTER);
        overrun.relayout();
        CHECK(tall.geometry() == rect_t{ 0, 0, 20, 80 });

        // (6c) grid: two fixed items wider than the squeezed columns; the boxes overrun their cells
        // from the cell origin, unaffected by the bits.
        widget_t squeezed;
        squeezed.set_geometry(rect_t{ 0, 0, 80, 100 });
        sized_t wide_a{ extent_t{ 60, 20 }, &squeezed };
        sized_t wide_b{ extent_t{ 70, 20 }, &squeezed };
        wide_a.set_fixed_size(extent_t{ 60, 20 });
        wide_b.set_fixed_size(extent_t{ 70, 20 });
        auto* const squeezed_grid = new grid_layout_t;
        squeezed_grid->set_spacing(0);
        squeezed_grid->add(&wide_a, 0, 0);
        squeezed_grid->add(&wide_b, 0, 1);
        squeezed.set_layout(squeezed_grid);

        CHECK(wide_a.geometry() == rect_t{ 0, 0, 60, 20 });
        CHECK(wide_b.geometry() == rect_t{ 40, 0, 70, 20 });

        wide_a.set_alignment(as_alignment(alignment_flag::RIGHT) | alignment_flag::HCENTER);
        wide_b.set_alignment(as_alignment(alignment_flag::BOTTOM) | alignment_flag::VCENTER);
        squeezed.relayout();
        CHECK(wide_a.geometry() == rect_t{ 0, 0, 60, 20 });
        CHECK(wide_b.geometry() == rect_t{ 40, 0, 70, 20 });
    }

    // ---- (7) the item API: default, round trip, and no geometry change until the next pass ----
    {
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 100 });
        sized_t child{ extent_t{ 20, 20 }, &parent };
        child.set_maximum_height(40);          // the cross cell (100) is larger than the child (40)
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);

        CHECK(child.geometry() == rect_t{ 0, 0, 20, 40 });
        CHECK(child.alignment() == alignment_flag::NONE);
        CHECK(!static_cast<bool>(child.alignment()));
        child.set_alignment(as_alignment(alignment_flag::VCENTER));
        CHECK(child.alignment() == alignment_flag::VCENTER);
        // A placement change is not a size constraint: nothing moves until the layout runs again.
        CHECK(child.geometry() == rect_t{ 0, 0, 20, 40 });
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 0, 30, 20, 40 });   // (100 - 40) / 2
        child.set_alignment(as_alignment(alignment_flag::NONE));
        CHECK(child.alignment() == alignment_flag::NONE);
        parent.relayout();
        CHECK(child.geometry() == rect_t{ 0, 0, 20, 40 });    // zero fills again
        child.set_alignment(as_alignment(alignment_flag::RIGHT) | alignment_flag::HCENTER);
        CHECK(child.alignment() == (alignment_t{ alignment_flag::RIGHT } | alignment_flag::HCENTER));
    }

    // ---- (8) box MAIN axis: the bits size the child instead of moving it ----
    //
    // A box gives a child a cell of exactly its resolved slot on the main axis: QBoxLayout::setGeometry()
    // passes QRect(a.at(i).pos, ..., a.at(i).size, ...) (qtbase/src/widgets/kernel/qboxlayout.cpp), so
    // the cell is never wider than the child and no offset can move it along the row/column. The bits
    // act on this axis through the SIZE model instead: an aligned axis caps the child's allocation at
    // its effective_size_hint() (Qt's qSmartMaxSize(), reached through QWidgetItem::maximumSize()) and
    // keeps it out of the expansion distribution, so an EXPANDING child with a bit on that axis keeps
    // its hint extent and the spare space goes to its expanding siblings or stays unused.
    {
        // (8a) two EXPANDING children: without a bit they share the spare, with HCENTER on the first
        // one it keeps its hint width and the sibling takes everything else.
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 300, 100 });
        sized_t centered{ extent_t{ 40, 20 }, &parent };
        sized_t other{ extent_t{ 50, 20 }, &parent };
        centered.set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::PREFERRED);
        other.set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::PREFERRED);
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);

        // Unaligned: 40 + 50 of hint, the spare 210 split equally, so 145 / 155.
        CHECK(centered.geometry() == rect_t{ 0, 0, 145, 100 });
        CHECK(other.geometry() == rect_t{ 145, 0, 155, 100 });

        centered.set_alignment(as_alignment(alignment_flag::HCENTER));
        parent.relayout();
        CHECK(centered.geometry() == rect_t{ 0, 0, 40, 100 });
        CHECK(centered.geometry().width() == centered.size_hint().width());
        CHECK(other.geometry() == rect_t{ 40, 0, 260, 100 });
        CHECK(centered.geometry().right() == other.geometry().left());   // the slots stay adjacent
        CHECK(other.geometry().right() == 300);                          // the row is exactly filled

        // (8b) HCENTER / RIGHT / LEFT are the same position on the main axis, because the cell IS the
        // slot and has no slack. The bits of the OTHER axis do place the child, so the bits are read,
        // not ignored: with a cross maximum the vertical bits move it inside the row's height.
        centered.set_maximum_height(30);
        parent.relayout();
        CHECK(centered.geometry() == rect_t{ 0, 0, 40, 30 });
        centered.set_alignment(as_alignment(alignment_flag::RIGHT));
        parent.relayout();
        CHECK(centered.geometry() == rect_t{ 0, 0, 40, 30 });
        centered.set_alignment(as_alignment(alignment_flag::LEFT));
        parent.relayout();
        CHECK(centered.geometry() == rect_t{ 0, 0, 40, 30 });
        // A horizontal bit next to a vertical one keeps the main-axis cap AND places the child.
        centered.set_alignment(as_alignment(alignment_flag::RIGHT) | alignment_flag::VCENTER);
        parent.relayout();
        CHECK(centered.geometry() == rect_t{ 0, 35, 40, 30 });           // (100 - 30) / 2
        centered.set_alignment(as_alignment(alignment_flag::RIGHT) | alignment_flag::BOTTOM);
        parent.relayout();
        CHECK(centered.geometry() == rect_t{ 0, 70, 40, 30 });           // BOTTOM places, RIGHT cannot
        CHECK(centered.geometry().width() == 40);
        // Dropping the horizontal bit releases the cap on the main axis: the child expands again,
        // exactly as if it had no bit at all, and the remaining vertical bit still places it.
        centered.set_alignment(as_alignment(alignment_flag::VCENTER));
        parent.relayout();
        CHECK(centered.geometry() == rect_t{ 0, 35, 145, 30 });
        CHECK(other.geometry() == rect_t{ 145, 0, 155, 100 });
    }

    // ---- (8c) vbox mirror: the bits of the vertical axis SIZE, the horizontal ones PLACE ----
    {
        widget_t column;
        column.set_geometry(rect_t{ 0, 0, 100, 300 });
        sized_t up{ extent_t{ 20, 40 }, &column };
        sized_t down{ extent_t{ 30, 50 }, &column };
        up.set_size_policy(size_policy_enum::PREFERRED, size_policy_enum::EXPANDING);
        down.set_size_policy(size_policy_enum::PREFERRED, size_policy_enum::EXPANDING);
        auto* const layout = new vbox_layout_t;
        layout->set_spacing(0);
        column.set_layout(layout);

        // Unaligned: 40 + 50 of hint height, the spare 210 split equally, so 145 / 155.
        CHECK(up.geometry() == rect_t{ 0, 0, 100, 145 });
        CHECK(down.geometry() == rect_t{ 0, 145, 100, 155 });

        up.set_alignment(as_alignment(alignment_flag::VCENTER));
        column.relayout();
        CHECK(up.geometry() == rect_t{ 0, 0, 100, 40 });
        CHECK(up.geometry().height() == up.size_hint().height());
        CHECK(down.geometry() == rect_t{ 0, 40, 100, 260 });
        CHECK(down.geometry().bottom() == 300);

        // BOTTOM / TOP cannot move it down the column (the cell is the slot), exactly like (8b).
        up.set_alignment(as_alignment(alignment_flag::BOTTOM));
        column.relayout();
        CHECK(up.geometry() == rect_t{ 0, 0, 100, 40 });
        up.set_alignment(as_alignment(alignment_flag::TOP));
        column.relayout();
        CHECK(up.geometry() == rect_t{ 0, 0, 100, 40 });

        // The horizontal bits are the cross axis of a column: they place the child, and a vertical bit
        // next to them keeps the main-axis cap in force.
        up.set_maximum_width(30);
        up.set_alignment(as_alignment(alignment_flag::BOTTOM) | alignment_flag::HCENTER);
        column.relayout();
        CHECK(up.geometry() == rect_t{ 35, 0, 30, 40 });                 // (100 - 30) / 2
        up.set_alignment(as_alignment(alignment_flag::BOTTOM) | alignment_flag::RIGHT);
        column.relayout();
        CHECK(up.geometry() == rect_t{ 70, 0, 30, 40 });
        CHECK(down.geometry() == rect_t{ 0, 40, 100, 260 });
        // A horizontal bit alone leaves the vertical axis unaligned: the cap is released and the child
        // expands again, while the bit still places it across the column.
        up.set_alignment(as_alignment(alignment_flag::HCENTER));
        column.relayout();
        CHECK(up.geometry() == rect_t{ 35, 0, 30, 145 });
        CHECK(down.geometry() == rect_t{ 0, 145, 100, 155 });
    }

    // ---- (8d) the spare space is not absorbed: alone, an aligned EXPANDING child keeps its hint and
    //      the rest of the axis stays blank ----
    {
        widget_t row;
        row.set_geometry(rect_t{ 0, 0, 300, 100 });
        sized_t solo{ extent_t{ 40, 20 }, &row };
        solo.set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::PREFERRED);
        auto* const row_layout = new hbox_layout_t;
        row_layout->set_spacing(0);
        row.set_layout(row_layout);
        CHECK(solo.geometry() == rect_t{ 0, 0, 300, 100 });   // unaligned: it fills the row
        solo.set_alignment(as_alignment(alignment_flag::HCENTER));
        row.relayout();
        CHECK(solo.geometry() == rect_t{ 0, 0, 40, 100 });    // aligned: hint width, 260 px unused
        CHECK(solo.geometry().right() == 40);

        widget_t column;
        column.set_geometry(rect_t{ 0, 0, 100, 300 });
        sized_t tall{ extent_t{ 20, 40 }, &column };
        tall.set_size_policy(size_policy_enum::PREFERRED, size_policy_enum::EXPANDING);
        auto* const column_layout = new vbox_layout_t;
        column_layout->set_spacing(0);
        column.set_layout(column_layout);
        CHECK(tall.geometry() == rect_t{ 0, 0, 100, 300 });
        tall.set_alignment(as_alignment(alignment_flag::VCENTER));
        column.relayout();
        CHECK(tall.geometry() == rect_t{ 0, 0, 100, 40 });
        CHECK(tall.geometry().bottom() == 40);
    }

    // ---- (8e) a shortage is unchanged: the compression and overfull rules never read the bits ----
    {
        // (8e-1) overfull: three EXPANDING children whose floors alone overrun the row. The cap
        // narrows a growth ceiling only, and the cut is driven by the minimums.
        widget_t crowded;
        crowded.set_geometry(rect_t{ 0, 0, 100, 40 });
        floored_t ca{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &crowded };
        floored_t cb{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &crowded };
        floored_t cc{ extent_t{ 80, 20 }, extent_t{ 50, 10 }, &crowded };
        for (floored_t* const child : { &ca, &cb, &cc }) {
            child->set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::PREFERRED);
        }
        auto* const cut = new hbox_layout_t;
        cut->set_spacing(0);
        crowded.set_layout(cut);

        CHECK(ca.geometry() == rect_t{ 0, 0, 34, 40 });
        CHECK(cb.geometry() == rect_t{ 34, 0, 33, 40 });
        CHECK(cc.geometry() == rect_t{ 67, 0, 33, 40 });
        ca.set_alignment(as_alignment(alignment_flag::HCENTER));
        cb.set_alignment(as_alignment(alignment_flag::VCENTER));
        crowded.relayout();
        CHECK(ca.geometry() == rect_t{ 0, 0, 34, 40 });
        CHECK(cb.geometry() == rect_t{ 34, 0, 33, 40 });
        CHECK(cc.geometry() == rect_t{ 67, 0, 33, 40 });
        CHECK(cc.geometry().right() == 100);   // the row still fits; the cap changed nothing

        // (8e-2) the shrink band above the floors: 200 + 200 of hint in a 300-wide row with a 50
        // floor each shrinks both to 150, before and after an alignment bit.
        widget_t band;
        band.set_geometry(rect_t{ 0, 0, 300, 40 });
        floored_t ba{ extent_t{ 200, 20 }, extent_t{ 50, 10 }, &band };
        floored_t bb{ extent_t{ 200, 20 }, extent_t{ 50, 10 }, &band };
        ba.set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::PREFERRED);
        bb.set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::PREFERRED);
        auto* const shrink = new hbox_layout_t;
        shrink->set_spacing(0);
        band.set_layout(shrink);
        CHECK(ba.geometry() == rect_t{ 0, 0, 150, 40 });
        CHECK(bb.geometry() == rect_t{ 150, 0, 150, 40 });
        ba.set_alignment(as_alignment(alignment_flag::HCENTER));
        band.relayout();
        CHECK(ba.geometry() == rect_t{ 0, 0, 150, 40 });
        CHECK(bb.geometry() == rect_t{ 150, 0, 150, 40 });
    }

    // ---- (8f) the cap is PER AXIS: a bit that only touches the cross axis leaves the main-axis
    //      expansion alone ----
    {
        widget_t row;
        row.set_geometry(rect_t{ 0, 0, 300, 100 });
        sized_t left{ extent_t{ 40, 20 }, &row };
        sized_t right{ extent_t{ 50, 20 }, &row };
        left.set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::PREFERRED);
        right.set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::PREFERRED);
        left.set_alignment(as_alignment(alignment_flag::VCENTER));   // vertical = cross axis of a row
        auto* const row_layout = new hbox_layout_t;
        row_layout->set_spacing(0);
        row.set_layout(row_layout);
        CHECK(left.geometry() == rect_t{ 0, 0, 145, 100 });          // still expands horizontally
        CHECK(right.geometry() == rect_t{ 145, 0, 155, 100 });
        // ... and the bit still places it once the cross cell has slack.
        left.set_maximum_height(30);
        row.relayout();
        CHECK(left.geometry() == rect_t{ 0, 35, 145, 30 });
        CHECK(right.geometry() == rect_t{ 145, 0, 155, 100 });

        widget_t column;
        column.set_geometry(rect_t{ 0, 0, 100, 300 });
        sized_t top{ extent_t{ 20, 40 }, &column };
        sized_t bottom{ extent_t{ 30, 50 }, &column };
        top.set_size_policy(size_policy_enum::PREFERRED, size_policy_enum::EXPANDING);
        bottom.set_size_policy(size_policy_enum::PREFERRED, size_policy_enum::EXPANDING);
        top.set_alignment(as_alignment(alignment_flag::RIGHT));      // horizontal = cross axis
        auto* const column_layout = new vbox_layout_t;
        column_layout->set_spacing(0);
        column.set_layout(column_layout);
        CHECK(top.geometry() == rect_t{ 0, 0, 100, 145 });           // still expands vertically
        CHECK(bottom.geometry() == rect_t{ 0, 145, 100, 155 });
    }

    // ---- (9) label_t: text alignment and layout alignment are separate accessors ----
    {
        // The text pair belongs to label_t; `alignment` resolves to item_t's accessors, which is what
        // "the rename unhid them" means at compile time.
        static_assert(std::is_same_v<decltype(&label_t::text_alignment),
                                     text_alignment_enum (label_t::*)() const>);
        static_assert(std::is_same_v<decltype(&label_t::set_text_alignment),
                                     void (label_t::*)(text_alignment_enum)>);
        static_assert(std::is_same_v<decltype(&label_t::alignment),
                                     alignment_t (hwshqtb::ui::item_t::*)() const noexcept>);
        static_assert(std::is_same_v<decltype(&label_t::set_alignment),
                                     void (hwshqtb::ui::item_t::*)(alignment_t)>);

        label_t label;
        CHECK(label.text_alignment() == text_alignment_enum::LEFT);   // text: at the leading padding
        CHECK(label.alignment() == alignment_flag::NONE);             // layout: fill the cell
        label.set_text_alignment(text_alignment_enum::CENTER);
        CHECK(label.text_alignment() == text_alignment_enum::CENTER);
        CHECK(label.alignment() == alignment_flag::NONE);             // one does not change the other
        label.set_alignment(as_alignment(alignment_flag::VCENTER));
        CHECK(label.alignment() == alignment_flag::VCENTER);
        CHECK(label.text_alignment() == text_alignment_enum::CENTER);

        // Layout placement: the label is a normal item, so a fixed-size label in a taller row is put
        // where item_t::alignment() says — no static_cast to item_t is needed any more.
        widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 200, 100 });
        label_t placed{ &parent };
        placed.set_fixed_size(extent_t{ 60, 20 });
        auto* const layout = new hbox_layout_t;
        layout->set_spacing(0);
        parent.set_layout(layout);
        CHECK(placed.geometry() == rect_t{ 0, 0, 60, 20 });
        placed.set_alignment(as_alignment(alignment_flag::VCENTER));
        parent.relayout();
        CHECK(placed.geometry() == rect_t{ 0, 40, 60, 20 });          // (100 - 20) / 2
        CHECK(placed.text_alignment() == text_alignment_enum::LEFT);  // the layout bit left the text alone

        // Text placement: the glyph run of paint_event() starts where the TEXT alignment says. The
        // style centres the run inside a text-width rectangle, so the run moves by the padding (LEFT),
        // to (width - text) / 2 (CENTER), and to width - padding - text (RIGHT).
        placed.set_geometry(rect_t{ 0, 0, 120, 26 });
        placed.set_text("abc");
        const int padding = default_style().pixel_metric(pixel_metric_enum::TEXT_PADDING);
        font_metrics_t metrics{};
        const int text_width = metrics.text_extent("abc").width();
        CHECK(text_width > 0);
        CHECK(text_width + 2 * padding < 120);   // every case is distinguishable inside the label

        CHECK(painted_text_x(placed, 120, 26) == padding);
        placed.set_text_alignment(text_alignment_enum::CENTER);
        CHECK(painted_text_x(placed, 120, 26) == (120 - text_width) / 2);
        placed.set_text_alignment(text_alignment_enum::RIGHT);
        CHECK(painted_text_x(placed, 120, 26) == 120 - padding - text_width);
        placed.set_text_alignment(text_alignment_enum::LEFT);
        CHECK(painted_text_x(placed, 120, 26) == padding);
        CHECK(placed.geometry() == rect_t{ 0, 0, 120, 26 });  // painting moved nothing in the layout
    }

    if (g_failures == 0) {
        std::printf("ui alignment: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("ui alignment: %d checks, %d failure(s)\n", g_checks, g_failures);
    return 1;
}
