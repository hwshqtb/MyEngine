// M3b: the unified setter/User-event rule, group_box_t and tab_widget_t.
//
// The acceptance criteria as executable checks:
//  1. the unified rule: a setter emits the state-change signal only when the value really changes and
//     NEVER raises the User property; the control's own user action (click/drag/key) emits the signal
//     AND raises the User event for that frame — checked for check_box_t, radio_button_t, slider_t,
//     scroll_bar_t and (unchanged) progress_bar_t;
//  2. the shared orientation_enum: progress_bar_t, slider_t and scroll_bar_t all speak it;
//  3. group_box_t: title/title_changed, the content area (inset by the border and the title strip),
//     the layout applied to the content area, size_hint/minimum_size_hint following the title and the
//     child hint, not focusable, no User property;
//  4. tab_widget_t: add/remove pages, the current page is the only visible one and gets the content
//     rectangle, click/Left/Right/Home/End switching, current_changed only on a real change, the
//     keyboard refused while a control inside the current page has the focus, size_hint from the
//     largest page, and the tab bar/page pixels.
//
// Offscreen only (image_t + painter_t); no window and no video subsystem are created.

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/any_t.h"
#include "hwshqtb/metaobject/log.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/metaobject/meta_object_t.h"
#include "hwshqtb/metaobject/meta_property_t.h"
#include "hwshqtb/metaobject/meta_type_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/ui/box_layout_t.h"
#include "hwshqtb/ui/check_box_t.h"
#include "hwshqtb/ui/group_box_t.h"
#include "hwshqtb/ui/input_router_t.h"
#include "hwshqtb/ui/progress_bar_t.h"
#include "hwshqtb/ui/push_button_t.h"
#include "hwshqtb/ui/radio_button_t.h"
#include "hwshqtb/ui/scroll_bar_t.h"
#include "hwshqtb/ui/slider_t.h"
#include "hwshqtb/ui/style_t.h"
#include "hwshqtb/ui/tab_widget_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;
using namespace hwshqtb::ui;

namespace containers_test {

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

/// @brief The User property's current value of @p widget (false when the class declares none).
bool user_bool(widget_t& widget) {
    const meta_property_t user = widget.meta_object()->user_property();
    if (!user.is_valid()) {
        return false;
    }
    return user.read(widget).get<bool>();
}

interaction_t press(point_t local) {
    interaction_t in;
    in.mouse = local;
    in.mouse_down = true;
    in.mouse_pressed = true;
    in.hovered = true;
    return in;
}

interaction_t release(point_t local, bool inside) {
    interaction_t in;
    in.mouse = local;
    in.mouse_released = true;
    in.hovered = inside;
    return in;
}

interaction_t move(point_t local, bool down) {
    interaction_t in;
    in.mouse = local;
    in.mouse_down = down;
    in.hovered = true;
    return in;
}

interaction_t key_press(int key) {
    interaction_t in;
    in.key = key;
    in.key_pressed = true;
    return in;
}

interaction_t key_release(int key) {
    interaction_t in;
    in.key = key;
    in.key_released = true;
    return in;
}

/// @brief A page that fills its whole rectangle with one colour (what makes page visibility and the
///        content rectangle observable in pixels).
class fill_page_t final: public widget_t {
public:
    explicit fill_page_t(color_t color, widget_t* parent = nullptr):
        widget_t(parent),
        _color(color) { }

protected:
    void paint_event(paint_event_t* event) override {
        painter_t& painter = event->painter();
        painter.set_color(_color);
        painter.fill_rect(rect());
    }

private:
    color_t _color;
};

}  // namespace containers_test

using namespace containers_test;

int main() {
    pin_builtin_font_family(); // deterministic text: the built-in face, not the machine's UI font
    install_log_handler([](log_level_enum, const log_context_t&, std::string_view) {
        return true; // consumed: the default stderr sink stays silent
    });
    (void)meta_type_t::register_type<bool>();
    (void)meta_type_t::register_type<std::string_view>();

    std::printf("== M3b: setter/User-event rule + orientation_enum + group_box + tab_widget ==\n");

    // -----------------------------------------------------------------------
    // (1) The unified rule: setter = state signal only; user action = signal AND User event.
    // -----------------------------------------------------------------------
    {
        // (1a) check_box_t.
        widget_t root;
        check_box_t box{ &root };
        box.set_geometry(rect_t{ 0, 0, 120, 26 });
        int toggles = 0;
        bool last_checked = false;
        box.connect("toggled(bool) const", [&toggles, &last_checked](bool checked) {
            ++toggles;
            last_checked = checked;
        });
        box.set_checked(true);
        CHECK(box.is_checked());
        CHECK(toggles == 1 && last_checked);
        CHECK(!box.toggled());              // a setter never raises the User event
        CHECK(!user_bool(box));
        box.set_checked(true);              // no real change: no signal
        CHECK(toggles == 1);
        box.set_checked(false);
        CHECK(toggles == 2 && !last_checked);
        CHECK(!box.toggled());
        // The user's click does BOTH.
        CHECK(box.interact(press(point_t{ 6, 13 })));
        CHECK(box.interact(release(point_t{ 6, 13 }, true)));
        CHECK(toggles == 3 && last_checked);
        CHECK(box.toggled());
        CHECK(user_bool(box));
        CHECK(!box.interact(move(point_t{ 6, 13 }, false)));
        CHECK(!box.toggled());              // the next driven frame clears the User event
        CHECK(toggles == 3);                // ... but it does not emit again
    }

    // (1b) radio_button_t: the setter signals itself and clears the peers; no User event anywhere.
    {
        widget_t root;
        radio_button_t a{ &root };
        radio_button_t b{ &root };
        root.set_geometry(rect_t{ 0, 0, 120, 60 });
        a.set_geometry(rect_t{ 0, 0, 120, 26 });
        b.set_geometry(rect_t{ 0, 30, 120, 26 });
        int a_true = 0;
        int a_false = 0;
        int b_true = 0;
        int b_false = 0;
        a.connect("toggled(bool) const", [&a_true, &a_false](bool checked) {
            (checked ? a_true : a_false) += 1;
        });
        b.connect("toggled(bool) const", [&b_true, &b_false](bool checked) {
            (checked ? b_true : b_false) += 1;
        });
        a.set_checked(true);
        CHECK(a_true == 1 && b_true == 0);  // the setter signals its own change
        CHECK(!a.toggled());                // ... without a User event
        CHECK(!user_bool(a));
        b.set_checked(true);
        CHECK(b_true == 1 && a_false == 1); // ... and its own change clears the announced peer
        CHECK(!b.toggled() && !a.toggled());
        // The user's click on A: A's signal + A's User event, B's clear signal + no B User event.
        CHECK(a.interact(press(point_t{ 6, 13 })));
        CHECK(a.interact(release(point_t{ 6, 13 }, true)));
        CHECK(a_true == 2 && b_false == 1);
        CHECK(a.toggled() && !b.toggled());
        CHECK(user_bool(a) && !user_bool(b));
    }

    // (1c) slider_t: set_value signals on a real change only; a user press does both.
    {
        slider_t slider;
        slider.set_geometry(rect_t{ 0, 0, 120, 18 });
        int changes = 0;
        int last_value = -1;
        slider.connect("value_changed(int) const", [&changes, &last_value](int value) {
            ++changes;
            last_value = value;
        });
        slider.set_value(50);
        CHECK(changes == 1 && last_value == 50);
        CHECK(!slider.value_changed());     // no User event from the setter
        slider.set_value(50);               // no real change: no signal
        CHECK(changes == 1);
        slider.set_range(10, 20);           // the clamp moves 50 -> 20: one state signal
        CHECK(slider.value() == 20);
        CHECK(changes == 2 && last_value == 20);
        CHECK(!slider.value_changed());
        slider.set_range(0, 100);           // the value stays inside: no signal
        CHECK(changes == 2);
        // A user press at the far right changes the value and raises the User event.
        CHECK(slider.interact(press(point_t{ 114, 9 })));
        CHECK(slider.value() == 100);
        CHECK(changes == 3);
        CHECK(slider.value_changed());
        CHECK(user_bool(slider));
        CHECK(slider.interact(release(point_t{ 114, 9 }, true)));
        CHECK(!slider.interact(move(point_t{ 60, 9 }, false)));
        CHECK(!slider.value_changed());
    }

    // (1d) scroll_bar_t: set_value signals on a real change; a groove page does both.
    {
        scroll_bar_t bar;
        bar.set_geometry(rect_t{ 0, 0, 14, 120 });
        int changes = 0;
        bar.connect("value_changed(int) const", [&changes]() { ++changes; });
        bar.set_value(30);
        CHECK(changes == 1);
        CHECK(!bar.value_changed());
        bar.set_value(30);
        CHECK(changes == 1);
        // The handle for value 30 sits at 31..46; a press below it pages +10.
        CHECK(bar.interact(press(point_t{ 7, 100 })));
        CHECK(bar.value() == 40);
        CHECK(changes == 2);
        CHECK(bar.value_changed());
        CHECK(user_bool(bar));
        CHECK(!bar.interact(release(point_t{ 7, 100 }, true)));
        CHECK(!bar.interact(move(point_t{ 7, 100 }, false)));
        CHECK(!bar.value_changed());
    }

    // (1e) progress_bar_t already followed the rule: a setter signals, and it has no User property.
    {
        progress_bar_t bar;
        int changes = 0;
        bar.connect("value_changed(int) const", [&changes]() { ++changes; });
        bar.set_value(40);
        CHECK(changes == 1);
        bar.set_value(40);
        CHECK(changes == 1);
        bar.set_range(50, 60);              // the clamp moves 40 -> 50
        CHECK(bar.value() == 50);
        CHECK(changes == 2);
        CHECK(!progress_bar_t::static_meta_object.user_property().is_valid());
    }

    // (1f) The shared orientation_enum replaces the three old enums.
    {
        CHECK(progress_bar_t{}.direction() == orientation_enum::HORIZONTAL);
        CHECK(slider_t{}.orientation() == orientation_enum::HORIZONTAL);
        CHECK(scroll_bar_t{}.orientation() == orientation_enum::VERTICAL);
        slider_t slider;
        slider.set_orientation(orientation_enum::VERTICAL);
        CHECK(slider.orientation() == orientation_enum::VERTICAL);
        scroll_bar_t bar;
        bar.set_orientation(orientation_enum::HORIZONTAL);
        CHECK(bar.orientation() == orientation_enum::HORIZONTAL);
        progress_bar_t progress;
        progress.set_direction(orientation_enum::VERTICAL);
        CHECK(progress.direction() == orientation_enum::VERTICAL);
    }

    // -----------------------------------------------------------------------
    // (2) group_box_t: the title, the content area and the layout/size hints.
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
        CHECK(box.title().empty());
        CHECK(box.focus_policy() == focus_policy_enum::NO_FOCUS); // a passive container
        CHECK(!group_box_t::static_meta_object.user_property().is_valid());
        int title_signals = 0;
        std::string last_title;
        box.connect(box.meta_object()->normalized_signature("title_changed(std::string_view) const"),
                    [&title_signals, &last_title](std::string_view title) {
                        ++title_signals;
                        last_title = std::string(title);
                    });
        box.set_title("Options");
        CHECK(box.title() == "Options");
        CHECK(title_signals == 1 && last_title == "Options");
        box.set_title("Options");           // no real change: no signal
        CHECK(title_signals == 1);

        // A child plus a layout: the layout must apply to the CONTENT area, never the title strip.
        auto* const child = new widget_t(&box);
        child->set_fixed_size(extent_t{ 60, 20 });
        auto* const layout = new vbox_layout_t;
        layout->set_margins(margins_t{ 0, 0, 0, 0 });
        box.set_layout(layout);
        box.set_geometry(rect_t{ 0, 0, 100, 80 });
        const rect_t content = box.content_rect();
        CHECK(content == rect_t{ border, title_height, 100 - 2 * border, 80 - title_height - border });
        CHECK(child->geometry() == rect_t{ content.x(), content.y(), 60, 20 });
        CHECK(child->geometry().y() >= title_height); // never laid out over the title

        // "Options" is 7 glyphs: 42 px + 16 padding = 58; the child needs 60 + 2 * border = 62.
        CHECK(box.size_hint() == extent_t{ 62, 20 + title_height + border });
        box.set_title("A much longer title"); // 19 glyphs: 114 + 16 = 130 > 62
        const extent_t wide = box.size_hint();
        CHECK(wide.width() == 130);
        CHECK(wide.width() > 62);
        CHECK(wide.height() == 20 + title_height + border);
        // The minimum keeps the same recipe from the layout's floor: the child is FIXED at 60x20, so
        // its effective_minimum_size() is 60x20 and the layout (no margins, no spacing, one child)
        // publishes the same. The title (130 px) still sets the width.
        CHECK(box.minimum_size_hint() == extent_t{ 130, 20 + title_height + border });

        // A resize re-applies the layout to the new content area.
        box.set_geometry(rect_t{ 0, 0, 200, 120 });
        CHECK(box.content_rect() == rect_t{ border, title_height, 200 - 2 * border, 120 - title_height - border });
        CHECK(child->geometry().x() == box.content_rect().x());
        CHECK(child->geometry().y() == box.content_rect().y());
    }

    // -----------------------------------------------------------------------
    // (3) tab_widget_t: pages, the tab bar hit test, the keyboard and the current_changed signal.
    // -----------------------------------------------------------------------
    {
        const style_t& style = default_style();
        const int tab_height = style.pixel_metric(pixel_metric_enum::TAB_HEIGHT);
        // A PROPERTY, not the tuned number: a tab strip has to hold a whole line box.
        CHECK(tab_height >= ::hwshqtb::gui::font_metrics_t{}.line_height());

        tab_widget_t tabs;
        CHECK(tabs.focus_policy() == focus_policy_enum::STRONG_FOCUS);
        CHECK(!tab_widget_t::static_meta_object.user_property().is_valid());
        CHECK(tabs.count() == 0 && tabs.current_index() == -1);
        CHECK(tabs.page_at(0) == nullptr && tabs.tab_title(0).empty());
        int current_signals = 0;
        int last_index = -99;
        tabs.connect("current_changed(int) const", [&current_signals, &last_index](int index) {
            ++current_signals;
            last_index = index;
        });

        auto* const page_a = new fill_page_t(color_t::red_color());
        page_a->set_fixed_size(extent_t{ 100, 40 });
        auto* const page_b = new fill_page_t(color_t::blue_color());
        page_b->set_fixed_size(extent_t{ 120, 30 });
        tabs.add_tab(page_a, "A");
        CHECK(tabs.count() == 1 && tabs.current_index() == 0);
        CHECK(current_signals == 1 && last_index == 0); // the first page becomes current
        CHECK(page_a->parent() == &tabs && page_a->is_visible());
        CHECK(tabs.tab_title(0) == "A" && tabs.page_at(0) == page_a);
        tabs.add_tab(page_b, "B");
        CHECK(tabs.count() == 2 && tabs.current_index() == 0);
        CHECK(current_signals == 1);                     // a later page changes nothing
        CHECK(!page_b->is_visible());
        tabs.add_tab(page_b, "B");                       // a duplicate is ignored
        CHECK(tabs.count() == 2);
        tabs.add_tab(nullptr, "X");                      // nullptr is ignored
        CHECK(tabs.count() == 2);

        tabs.set_geometry(rect_t{ 0, 0, 200, 100 });
        CHECK(tabs.content_rect() == rect_t{ 0, tab_height, 200, 100 - tab_height });
        CHECK(page_a->geometry() == tabs.content_rect());
        CHECK(tabs.size_hint() == extent_t{ 120, 40 + tab_height }); // the largest page hint

        // The tab widths follow the titles: "A" and "B" are 6 + 2 * 8 = 22 px each.
        CHECK(tabs.interact(press(point_t{ 30, 10 })));  // inside tab 1
        CHECK(tabs.current_index() == 1);
        CHECK(current_signals == 2 && last_index == 1);
        CHECK(page_b->is_visible() && !page_a->is_visible());
        CHECK(page_b->geometry() == tabs.content_rect());
        CHECK(tabs.interact(release(point_t{ 30, 10 }, true))); // the bar press owns the release
        CHECK(!tabs.interact(press(point_t{ 5, 50 })));         // below the bar: not the widget's
        CHECK(tabs.interact(press(point_t{ 5, 10 })));          // inside tab 0
        CHECK(tabs.current_index() == 0);
        CHECK(tabs.interact(release(point_t{ 5, 10 }, true)));
        // A bar click that hits no tab is consumed but keeps the current page.
        const int signals_before_bar = current_signals;
        CHECK(tabs.interact(press(point_t{ 150, 10 })));
        CHECK(tabs.current_index() == 0);
        CHECK(current_signals == signals_before_bar);
        CHECK(tabs.interact(release(point_t{ 150, 10 }, true)));

        // Keyboard: Left/Right/Home/End; a clamped move is consumed but changes nothing.
        input_router_t router{ tabs };
        router.set_focus(&tabs);
        CHECK(tabs.has_focus());
        CHECK(router.dispatch(key_press(interaction_t::right_key)));
        CHECK(tabs.current_index() == 1);
        (void)router.dispatch(key_release(interaction_t::right_key));
        CHECK(router.dispatch(key_press(interaction_t::left_key)));
        CHECK(tabs.current_index() == 0);
        (void)router.dispatch(key_release(interaction_t::left_key));
        CHECK(router.dispatch(key_press(interaction_t::left_key))); // clamped at the first page
        CHECK(tabs.current_index() == 0);
        (void)router.dispatch(key_release(interaction_t::left_key));
        CHECK(router.dispatch(key_press(interaction_t::end_key)));
        CHECK(tabs.current_index() == 1);
        (void)router.dispatch(key_release(interaction_t::end_key));
        CHECK(router.dispatch(key_press(interaction_t::home_key)));
        CHECK(tabs.current_index() == 0);
        (void)router.dispatch(key_release(interaction_t::home_key));

        // A focused control INSIDE the current page wins: the key never reaches the tab widget.
        auto* const page_button = new push_button_t(page_a);
        page_button->set_geometry(rect_t{ 0, 0, 40, 26 });
        router.set_focus(page_button);
        CHECK(page_button->has_focus());
        const int signals_before_key = current_signals;
        CHECK(!router.dispatch(key_press(interaction_t::right_key)));
        CHECK(tabs.current_index() == 0);
        CHECK(current_signals == signals_before_key);
        (void)router.dispatch(key_release(interaction_t::right_key));

        // set_current_index clamps, and signals only on a real change.
        const int before_set = current_signals;
        tabs.set_current_index(1);
        CHECK(tabs.current_index() == 1);
        CHECK(current_signals == before_set + 1);
        tabs.set_current_index(1);
        CHECK(current_signals == before_set + 1);
        tabs.set_current_index(5);                       // clamps to 1: no change
        CHECK(tabs.current_index() == 1);
        tabs.set_current_index(-3);                      // clamps to 0: a real change
        CHECK(tabs.current_index() == 0);
        CHECK(current_signals == before_set + 2);

        // remove_tab: detach (the caller owns the page), follow the index, signal on a real change.
        auto* const page_c = new fill_page_t(color_t::green_color());
        page_c->set_fixed_size(extent_t{ 80, 20 });
        tabs.add_tab(page_c, "C");
        CHECK(tabs.count() == 3);
        tabs.set_current_index(2);
        int before_remove = current_signals;
        tabs.remove_tab(0);                              // before the current page: index shifts
        CHECK(tabs.count() == 2);
        CHECK(tabs.current_index() == 1);
        CHECK(tabs.page_at(1) == page_c);
        CHECK(current_signals == before_remove + 1);     // the index really changed
        CHECK(page_a->parent() == nullptr);              // detached, not deleted
        delete page_a;
        before_remove = current_signals;
        tabs.remove_tab(1);                              // the current page: the nearest becomes current
        CHECK(tabs.count() == 1);
        CHECK(tabs.current_index() == 0);
        CHECK(tabs.page_at(0) == page_b);
        CHECK(current_signals == before_remove + 1);
        CHECK(page_c->parent() == nullptr);
        delete page_c;
        before_remove = current_signals;
        tabs.remove_tab(0);                              // the last page: no current page
        CHECK(tabs.count() == 0 && tabs.current_index() == -1);
        CHECK(current_signals == before_remove + 1);
        CHECK(page_b->parent() == nullptr);
        delete page_b;
        tabs.remove_tab(0);                              // out of range: no-op
        CHECK(tabs.count() == 0);
        tabs.set_current_index(0);                       // no pages: no-op
        CHECK(tabs.current_index() == -1);
    }

    // -----------------------------------------------------------------------
    // (4) tab_widget_t pixels: only the current page is painted, the tab bar shows both tabs, and the
    //     selected tab differs from the other one.
    // -----------------------------------------------------------------------
    {
        image_t image{ 200, 100 };
        painter_t painter{ image };
        painter.begin();
        tab_widget_t tabs;
        auto* const first = new fill_page_t(color_t::red_color());
        first->set_fixed_size(extent_t{ 200, 100 });
        auto* const second = new fill_page_t(color_t::blue_color());
        second->set_fixed_size(extent_t{ 200, 100 });
        tabs.add_tab(first, "A");
        tabs.add_tab(second, "B");
        tabs.set_geometry(rect_t{ 0, 0, 200, 100 });

        painter.clear(color_t::white_color());
        tabs.render(painter);
        CHECK(image.pixel(50, 50) == color_t::red_color());          // the current page
        // The probe row is INSIDE the tab band, above the centred caption: derived from the band height
        // (a quarter of it), not a magic 5 that only fits a 24 px band.
        const int tab_y = default_style().pixel_metric(pixel_metric_enum::TAB_HEIGHT) / 4;
        CHECK(image.pixel(50, tab_y) != color_t::red_color());           // the tab bar is not the page
        CHECK(image.pixel(3, tab_y) != color_t::white_color());          // ... and it is painted
        const color_t unselected_tab = image.pixel(5, tab_y);
        const color_t selected_tab = image.pixel(30, tab_y);
        CHECK(unselected_tab != selected_tab);                       // selected vs unselected

        tabs.set_current_index(1);
        painter.clear(color_t::white_color());
        tabs.render(painter);
        CHECK(image.pixel(50, 50) == color_t::blue_color());          // the other page now
        CHECK(image.pixel(5, tab_y) == selected_tab);                // tab 0 is the unselected one now
        CHECK(image.pixel(30, tab_y) == unselected_tab);
        painter.end();
    }

    install_log_handler(log_handler{});

    std::printf("containers: %d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
