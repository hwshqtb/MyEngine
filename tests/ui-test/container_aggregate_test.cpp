// Containers and layouts aggregating their children through the effective triple.
//
// The eight sections pin:
//  (1) layout_t::minimum_size()'s default — it answers size_hint(), so a layout that publishes no
//      floor of its own cannot be arranged below the size it asks for (the base class answers {0,0});
//  (2) box_layout_t::minimum_size(): the main-axis sum plus the gaps plus the margins, the cross-axis
//      maximum plus the margins — the same composition as size_hint() with every child's
//      effective_minimum_size() in place of its effective hint;
//  (3) grid_layout_t::minimum_size(): the per-column/per-row maxima summed per axis, a spanning item
//      contributing ceil(minimum / span) to each covered line, margins and spacing included;
//  (4) widget_t::minimum_size_hint(): the installed layout's minimum_size(), {0,0} without a layout,
//      and a child's hard minimum flowing up through that chain;
//  (5) group_box_t: the layout's floor plus the title strip and the border, a child's minimum_size()
//      raising the box's floor and a child's maximum_size() capping the box's hint;
//  (6) tab_widget_t: the per-axis maximum over the pages' effective triple plus the tab bar height;
//  (7) scroll_view_t: size_hint() is the viewport's minimal usable size (the content's floor, not its
//      natural size), minimum_size_hint() holds both bars plus a one-pixel viewport, and the scroll
//      RANGE keeps using the content's effective hint;
//  (8) the regression: with no hard constraint installed the three containers compose exactly the
//      numbers they composed before this aggregation existed.
//
// No window and no video subsystem: the containers and the layouts are pure offscreen code.

#include <cstdio>

#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/ui/box_layout_t.h"
#include "hwshqtb/ui/grid_layout_t.h"
#include "hwshqtb/ui/group_box_t.h"
#include "hwshqtb/ui/item_t.h"
#include "hwshqtb/ui/layout_t.h"
#include "hwshqtb/ui/scroll_view_t.h"
#include "hwshqtb/ui/scroll_bar_t.h"
#include "hwshqtb/ui/style_t.h"
#include "hwshqtb/ui/tab_widget_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using hwshqtb::ui::box_layout_t;
using hwshqtb::ui::default_style;
using hwshqtb::ui::grid_layout_t;
using hwshqtb::ui::group_box_t;
using hwshqtb::ui::hbox_layout_t;
using hwshqtb::ui::layout_t;
using hwshqtb::ui::pixel_metric_enum;
using hwshqtb::ui::scroll_view_t;
using hwshqtb::ui::scroll_bar_policy_enum;
using hwshqtb::ui::size_policy_enum;
using hwshqtb::ui::style_t;
using hwshqtb::ui::tab_widget_t;
using hwshqtb::ui::vbox_layout_t;
using hwshqtb::ui::widget_t;

namespace container_aggregate_test {

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

/// @brief A widget whose content opinion and content floor are configurable at run time.
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

    void set_hint(extent_t hint) {
        _hint = hint;
    }

    void set_minimum_hint(extent_t minimum_hint) {
        _minimum_hint = minimum_hint;
    }

private:
    extent_t _hint;
    extent_t _minimum_hint;
};

/// @brief A layout that publishes only a hint (no floor of its own): the base default applies.
class hint_only_layout_t final: public layout_t {
public:
    explicit hint_only_layout_t(extent_t hint):
        _hint(hint) { }

    void apply(widget_t&, rect_t) override { }

    extent_t size_hint() const override {
        return _hint;
    }

private:
    extent_t _hint;
};

/// @brief The bare base class: no children, no hint, no floor.
class bare_layout_t final: public layout_t {
public:
    void apply(widget_t&, rect_t) override { }
};

}  // namespace container_aggregate_test

using namespace container_aggregate_test;

int main() {
    pin_builtin_font_family(); // deterministic text: the built-in face, not the machine's UI font
    std::printf("== containers: aggregating children through the effective triple ==\n");

    // -----------------------------------------------------------------------
    // (1) layout_t::minimum_size() defaults to the hint.
    // -----------------------------------------------------------------------
    {
        widget_t owner;
        hint_only_layout_t* const layout = new hint_only_layout_t{ extent_t{ 70, 30 } };
        layout->set_margins(margins_t{ 1, 2, 3, 4 });
        layout->set_spacing(9);
        owner.set_layout(layout);
        // No floor of its own: the base implementation answers the size the layout asks for.
        CHECK(layout->minimum_size() == extent_t{ 70, 30 });
        CHECK(layout->minimum_size() == layout->size_hint());
        CHECK(owner.minimum_size_hint() == layout->minimum_size());

        widget_t bare_owner;
        bare_layout_t* const bare = new bare_layout_t;
        bare_owner.set_layout(bare);
        CHECK(bare->size_hint() == extent_t{ 0, 0 });
        CHECK(bare->minimum_size() == extent_t{ 0, 0 });  // the default of the default: still empty
        CHECK(bare_owner.minimum_size_hint() == extent_t{ 0, 0 });
    }

    // -----------------------------------------------------------------------
    // (2) box_layout_t::minimum_size(): main sum, cross max, margins and gaps.
    // -----------------------------------------------------------------------
    {
        widget_t parent;
        sized_widget_t a{ extent_t{ 40, 10 }, extent_t{ 15, 6 }, &parent };
        sized_widget_t b{ extent_t{ 60, 20 }, extent_t{ 25, 9 }, &parent };
        auto* const layout = new hbox_layout_t;
        layout->set_margins(margins_t{ 1, 2, 3, 4 });  // horizontal 4, vertical 6
        layout->set_spacing(5);
        parent.set_layout(layout);
        // Main: 15 + 5 + 25 + 4 = 49. Cross: max(6, 9) + 6 = 15.
        CHECK(layout->minimum_size() == extent_t{ 49, 15 });
        // The same shape as the hint: main 40 + 5 + 60 + 4 = 109, cross max(10, 20) + 6 = 26.
        CHECK(layout->size_hint() == extent_t{ 109, 26 });
        CHECK(parent.minimum_size_hint() == extent_t{ 49, 15 });

        // A vertical box swaps the axes: main 7 + 5 + 20 + 6 = 38, cross max(12, 3) + 4 = 16.
        widget_t column;
        sized_widget_t c{ extent_t{ 30, 20 }, extent_t{ 12, 7 }, &column };
        sized_widget_t d{ extent_t{ 50, 40 }, extent_t{ 3, 20 }, &column };
        auto* const vertical = new vbox_layout_t;
        vertical->set_margins(margins_t{ 1, 2, 3, 4 });
        vertical->set_spacing(5);
        column.set_layout(vertical);
        CHECK(vertical->minimum_size() == extent_t{ 16, 38 });
        CHECK(vertical->size_hint() == extent_t{ 54, 71 });
        CHECK(column.minimum_size_hint() == vertical->minimum_size());

        // An empty box answers the margins' sum on both axes, exactly like the hint does.
        widget_t empty_owner;
        auto* const empty = new vbox_layout_t;
        empty->set_margins(margins_t{ 4, 6, 8, 10 });
        empty->set_spacing(100);
        empty_owner.set_layout(empty);
        CHECK(empty->minimum_size() == extent_t{ 12, 16 });
        CHECK(empty->minimum_size() == empty->size_hint());
    }

    // -----------------------------------------------------------------------
    // (3) grid_layout_t::minimum_size(): per-line maxima, spans, gaps, margins.
    // -----------------------------------------------------------------------
    {
        widget_t owner;
        sized_widget_t g1{ extent_t{ 30, 10 }, extent_t{ 10, 5 }, &owner };   // row 0, column 0
        sized_widget_t g2{ extent_t{ 50, 30 }, extent_t{ 20, 15 }, &owner };  // row 0, column 1
        sized_widget_t g3{ extent_t{ 70, 20 }, extent_t{ 40, 25 }, &owner };  // row 1, both columns
        auto* const grid = new grid_layout_t;
        grid->set_margins(margins_t{ 10, 20, 30, 40 });  // horizontal 40, vertical 60
        grid->set_spacing(5);
        grid->add(&g1, 0, 0);
        grid->add(&g2, 0, 1);
        grid->add(&g3, 1, 0, 1, 2);
        owner.set_layout(grid);
        // Columns: max(10, ceil(40 / 2) = 20) = 20 and max(20, 20) = 20; rows: max(5, 15) = 15 and 25.
        // Width: 40 + 20 + 5 + 20 = 85. Height: 60 + 15 + 5 + 25 = 105.
        CHECK(grid->minimum_size() == extent_t{ 85, 105 });
        // The same aggregation over the effective hints (each raised to its own minimum hint):
        // columns max(30, 35) = 35 and max(50, 35) = 50, rows max(10, 30) = 30 and max(25, 25) = 25.
        CHECK(grid->size_hint() == extent_t{ 130, 120 });
        CHECK(owner.minimum_size_hint() == grid->minimum_size());
    }

    // -----------------------------------------------------------------------
    // (4) widget_t::minimum_size_hint(): the layout's floor, and a child's minimum flowing up.
    // -----------------------------------------------------------------------
    {
        widget_t bare;
        CHECK(bare.layout() == nullptr);
        CHECK(bare.minimum_size_hint() == extent_t{ 0, 0 });  // unchanged without a layout
        bare.set_geometry(rect_t{ 3, 4, 50, 60 });
        CHECK(bare.minimum_size_hint() == extent_t{ 0, 0 });  // not size() either

        widget_t owner;
        auto* const child = new widget_t(&owner);
        auto* const layout = new hbox_layout_t;
        owner.set_layout(layout);
        CHECK(child->minimum_size_hint() == extent_t{ 0, 0 });
        CHECK(owner.minimum_size_hint() == extent_t{ 0, 0 });

        // The hard minimum is the item's, the floor the container publishes is the aggregate.
        child->set_minimum_size(extent_t{ 90, 40 });
        CHECK(child->minimum_size_hint() == extent_t{ 0, 0 });         // the opinion is untouched
        CHECK(child->effective_minimum_size() == extent_t{ 90, 40 });  // the constraint
        CHECK(owner.minimum_size_hint() == extent_t{ 90, 40 });        // the aggregate floor

        // A maximum below the minimum lowers the minimum (the invariant) and caps the hint as well.
        child->set_maximum_size(extent_t{ 50, 15 });
        CHECK(child->effective_minimum_size() == extent_t{ 50, 15 });
        CHECK(child->effective_size_hint() == extent_t{ 50, 15 });
        CHECK(owner.minimum_size_hint() == extent_t{ 50, 15 });
        CHECK(owner.effective_minimum_size() == extent_t{ 50, 15 });
    }

    // -----------------------------------------------------------------------
    // (5) group_box_t: the content floor plus the title strip and the border.
    // -----------------------------------------------------------------------
    {
        const style_t& style = default_style();
        const int border = style.pixel_metric(pixel_metric_enum::BORDER_WIDTH);
        const int title_height = style.pixel_metric(pixel_metric_enum::TITLE_HEIGHT);
        const int padding = style.pixel_metric(pixel_metric_enum::TEXT_PADDING);
        // A PROPERTY, not the tuned number: the title band has to hold a whole glyph box.
        const ::hwshqtb::gui::font_metrics_t text_metrics{};
        CHECK(border == 1);
        CHECK(padding == 8);
        CHECK(title_height >= text_metrics.ascent() + text_metrics.descent());

        group_box_t box;
        auto* const child = new sized_widget_t{ extent_t{ 100, 60 }, extent_t{ 90, 40 }, &box };
        auto* const layout = new vbox_layout_t;  // no margins, no spacing: the layout is transparent
        box.set_layout(layout);
        // No title: the hint is the child's effective hint plus the insets, the floor is the child's
        // effective MINIMUM plus the same insets. The two differ, so the box must not share one body.
        CHECK(box.size_hint() == extent_t{ 100 + 2 * border, 60 + title_height + border });
        CHECK(box.minimum_size_hint() == extent_t{ 90 + 2 * border, 40 + title_height + border });
        CHECK(box.effective_minimum_size() == box.minimum_size_hint());
        CHECK(box.minimum_size_hint().width() < box.size_hint().width());
        CHECK(box.minimum_size_hint().height() < box.size_hint().height());

        // A hard minimum raises both extents (the constraint wins over the opinion).
        child->set_minimum_size(extent_t{ 120, 70 });
        CHECK(box.effective_minimum_size()
              == extent_t{ 120 + 2 * border, 70 + title_height + border });
        CHECK(box.effective_size_hint()
              == extent_t{ 120 + 2 * border, 70 + title_height + border });

        // A maximum on the child caps the hint the same way (the invariant lowers the minimum too).
        child->set_maximum_size(extent_t{ 40, 15 });
        CHECK(box.effective_size_hint()
              == extent_t{ 40 + 2 * border, 15 + title_height + border });
        CHECK(box.minimum_size_hint() == box.effective_minimum_size());

        // The title still sets the WIDTH floor: 30 glyphs = 180 px + 16 padding = 196.
        box.set_title("abcdefghijklmnopqrstuvwxyzABCD");
        CHECK(box.minimum_size_hint().width() == 30 * 6 + 2 * padding);
        CHECK(box.effective_minimum_size().width() == 30 * 6 + 2 * padding);
        CHECK(box.effective_minimum_size().height() == 15 + title_height + border);
    }

    // -----------------------------------------------------------------------
    // (6) tab_widget_t: the per-axis maximum over the pages plus the tab bar.
    // -----------------------------------------------------------------------
    {
        const style_t& style = default_style();
        const int tab_height = style.pixel_metric(pixel_metric_enum::TAB_HEIGHT);
        // A PROPERTY, not the tuned number: a tab strip has to hold a whole line box.
        CHECK(tab_height >= ::hwshqtb::gui::font_metrics_t{}.line_height());

        tab_widget_t tabs;
        CHECK(tabs.minimum_size_hint() == extent_t{ 0, 0 });  // no page: no floor either
        CHECK(tabs.size_hint() == extent_t{ 0, 0 });

        auto* const page_a = new widget_t;  // no opinion and no floor at all
        tabs.add_tab(page_a, "a");
        CHECK(tabs.minimum_size_hint() == extent_t{ 0, tab_height });  // the bar is all there is
        CHECK(tabs.size_hint() == extent_t{ 0, tab_height });

        page_a->set_minimum_size(extent_t{ 100, 30 });
        CHECK(tabs.effective_minimum_size() == extent_t{ 100, 30 + tab_height });
        CHECK(tabs.effective_size_hint() == extent_t{ 100, 30 + tab_height });

        // Two pages: each axis takes its own maximum, the bar height is added once.
        auto* const page_b = new widget_t;
        page_b->set_minimum_size(extent_t{ 60, 80 });
        tabs.add_tab(page_b, "b");
        CHECK(tabs.effective_minimum_size() == extent_t{ 100, 80 + tab_height });
        CHECK(tabs.effective_size_hint() == extent_t{ 100, 80 + tab_height });

        // A maximum on one page lowers that page's floor, not the other one's.
        page_a->set_maximum_size(extent_t{ 40, 20 });
        CHECK(page_a->effective_minimum_size() == extent_t{ 40, 20 });
        CHECK(tabs.effective_minimum_size() == extent_t{ 60, 80 + tab_height });
        CHECK(tabs.minimum_size_hint() == tabs.effective_minimum_size());
    }

    // -----------------------------------------------------------------------
    // (7) scroll_view_t: viewport floor for the hint, bars plus a one-pixel viewport for the floor,
    //     and the scroll RANGE still driven by the content's effective hint.
    // -----------------------------------------------------------------------
    {
        const style_t& style = default_style();
        const int thickness = style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);
        CHECK(thickness == 14);

        auto* const area = new scroll_view_t;
        CHECK(area->size_hint() == extent_t{ 0, 0 });  // no content: no opinion
        CHECK(area->minimum_size_hint() == extent_t{ 1 + thickness, 1 + thickness });
        CHECK(area->minimum_size_hint().width() > thickness);
        CHECK(area->minimum_size_hint().height() > thickness);
        CHECK(area->effective_minimum_size() == area->minimum_size_hint());

        auto* const content = new sized_widget_t{ extent_t{ 200, 150 }, extent_t{ 80, 50 } };
        area->set_content(content);  // the area owns it
        // The viewport's minimal usable size, not the content's natural size.
        CHECK(area->size_hint() == extent_t{ 80 + thickness, 50 + thickness });
        CHECK(area->size_hint().width() < content->size_hint().width());
        CHECK(area->size_hint().height() < content->size_hint().height());

        // The scroll range keeps using the content's effective hint.
        area->set_geometry(rect_t{ 0, 0, 100, 80 });
        CHECK(area->horizontal_scroll_bar()->maximum() == 200 - (100 - thickness));
        CHECK(area->vertical_scroll_bar()->maximum() == 150 - (80 - thickness));

        // A larger content grows the range, never the area's natural size. The area's rectangle is
        // unchanged, so writing the offset (with the value it already holds) is what re-derives it.
        content->set_hint(extent_t{ 400, 300 });
        area->set_scroll_offset(area->scroll_offset());
        CHECK(area->horizontal_scroll_bar()->maximum() == 400 - (100 - thickness));
        CHECK(area->vertical_scroll_bar()->maximum() == 300 - (80 - thickness));
        CHECK(area->size_hint() == extent_t{ 80 + thickness, 50 + thickness });

        // The content's own floor raises the viewport floor; its maximum caps it.
        content->set_minimum_hint(extent_t{ 120, 90 });
        CHECK(area->size_hint() == extent_t{ 120 + thickness, 90 + thickness });
        content->set_maximum_size(extent_t{ 60, 40 });  // lowers the minimum to 60x40: the hint follows
        CHECK(content->effective_minimum_size() == extent_t{ 60, 40 });
        CHECK(area->size_hint() == extent_t{ 60 + thickness, 40 + thickness });

        // ALWAYS_OFF reserves no bar thickness anywhere.
        area->set_scroll_bar_policy(scroll_bar_policy_enum::ALWAYS_OFF);
        CHECK(area->size_hint() == extent_t{ 60, 40 });
        CHECK(area->minimum_size_hint() == extent_t{ 1, 1 });

        // A container around the area sees the viewport floor, never the content's size.
        widget_t panel;
        auto* const nested = new scroll_view_t{ &panel };
        auto* const nested_content = new sized_widget_t{ extent_t{ 500, 400 }, extent_t{ 60, 30 } };
        nested->set_content(nested_content);
        auto* const panel_layout = new hbox_layout_t;
        panel.set_layout(panel_layout);
        CHECK(panel.size_hint() == extent_t{ 60 + thickness, 30 + thickness });
        CHECK(panel.minimum_size_hint() == nested->minimum_size_hint());
    }

    // -----------------------------------------------------------------------
    // (8) Regression: no hard constraint anywhere => the pre-aggregation numbers.
    // -----------------------------------------------------------------------
    {
        const style_t& style = default_style();
        const int border = style.pixel_metric(pixel_metric_enum::BORDER_WIDTH);
        const int title_height = style.pixel_metric(pixel_metric_enum::TITLE_HEIGHT);
        const int padding = style.pixel_metric(pixel_metric_enum::TEXT_PADDING);
        const int tab_height = style.pixel_metric(pixel_metric_enum::TAB_HEIGHT);
        const int thickness = style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);

        // group box: layout hint + insets, the same number as before; the floor is the title's own
        // value because the child contributes nothing.
        group_box_t box;
        box.set_title("box");
        auto* const box_child = new sized_widget_t{ extent_t{ 50, 25 }, extent_t{ 0, 0 }, &box };
        auto* const box_layout = new vbox_layout_t;
        box_layout->set_margins(margins_t{ 2, 3, 4, 5 });  // horizontal 6, vertical 8
        box_layout->set_spacing(7);
        box.set_layout(box_layout);
        CHECK(box.size_hint() == extent_t{ 50 + 6 + 2 * border,
                                          25 + 8 + title_height + border });
        // The child has no floor, so the layout's floor is its margins alone and the title wins.
        CHECK(box_layout->minimum_size() == extent_t{ 6, 8 });
        CHECK(box.minimum_size_hint() == extent_t{ 3 * 6 + 2 * padding, 8 + title_height + border });
        CHECK(box_child->effective_minimum_size() == extent_t{ 0, 0 });

        // tab widget: the bar plus the largest page hint (and the same floor shape).
        tab_widget_t tabs;
        auto* const page_one = new sized_widget_t{ extent_t{ 120, 40 }, extent_t{ 0, 0 } };
        auto* const page_two = new sized_widget_t{ extent_t{ 90, 30 }, extent_t{ 0, 0 } };
        tabs.add_tab(page_one, "1");
        tabs.add_tab(page_two, "2");
        CHECK(tabs.size_hint() == extent_t{ 120, 40 + tab_height });
        CHECK(tabs.minimum_size_hint() == extent_t{ 0, tab_height });

        // scroll area: the hint is the content's floor plus the bars — both fall back to the one-pixel
        // viewport when the content publishes no floor at all.
        auto* const area = new scroll_view_t;
        auto* const content = new sized_widget_t{ extent_t{ 300, 200 }, extent_t{ 0, 0 } };
        area->set_content(content);
        CHECK(area->size_hint() == extent_t{ 1 + thickness, 1 + thickness });
        CHECK(area->minimum_size_hint() == extent_t{ 1 + thickness, 1 + thickness });
        CHECK(area->effective_size_hint() == area->size_hint());
        CHECK(area->effective_size_hint() == area->minimum_size_hint());

        // box/grid hints are the plain aggregates they always were.
        widget_t row;
        sized_widget_t r1{ extent_t{ 40, 10 }, extent_t{ 0, 0 }, &row };
        sized_widget_t r2{ extent_t{ 60, 20 }, extent_t{ 0, 0 }, &row };
        auto* const row_layout = new hbox_layout_t;
        row_layout->set_spacing(5);
        row.set_layout(row_layout);
        CHECK(row.size_hint() == extent_t{ 40 + 5 + 60, 20 });
        CHECK(row.minimum_size_hint() == extent_t{ 5, 0 });  // the gap only: no child floor
    }

    std::printf("container aggregation: %d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
