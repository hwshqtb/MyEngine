// M3a: the wheel in interaction_t, scroll_bar_t and scroll_view_t.
//
// The acceptance criteria as executable checks:
//  1. the wheel field: input_state_t accumulates whole ticks and reset_wheel() clears only them; a
//     wheel snapshot is read by a control under BOTH hosts (the retained router delivers it, the
//     immediate host replays it into each control);
//  2. scroll_bar_t: the defaults and the style-derived size, the proportional handle, the groove page
//     (before/after the handle), the handle drag (with the grab offset preserved), the keyboard
//     (arrows/pages/Home/End), the clamp invariant, the silent setters, the disabled state, the
//     "announce only a real change" rule, and the User property;
//  3. scroll_view_t: set_content ownership (the old content is deleted), the viewport/range
//     computation with two bars, the AS_NEEDED/ALWAYS_ON/ALWAYS_OFF policies, set_scroll_offset
//     clamping and content translation, ensure_visible, the wheel, the keyboard (and the content's
//     focus winning over the area's), and the CLIPPING: nothing the content draws escapes the
//     viewport and the content is translated by -scroll_offset;
//  4. the container's clip is ONE rectangle for painting AND hit testing: a point on a scroll bar
//     hits the bar, a point in the viewport hits the content, and a point inside the area but
//     outside the viewport hits neither — the content owns only what the area shows of it.
//
// Offscreen only (image_t + painter_t); no window and no video subsystem are created.

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/input_state_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/log.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/metaobject/meta_object_t.h"
#include "hwshqtb/metaobject/meta_property_t.h"
#include "hwshqtb/metaobject/meta_type_t.h"
#include "hwshqtb/metaobject/object_pointer_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/ui/im_host_t.h"
#include "hwshqtb/ui/input_router_t.h"
#include "hwshqtb/ui/push_button_t.h"
#include "hwshqtb/ui/scroll_view_t.h"
#include "hwshqtb/ui/scroll_bar_t.h"
#include "hwshqtb/ui/style_t.h"
#include "hwshqtb/ui/viewport_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;
using namespace hwshqtb::ui;

namespace scroll_test {

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

interaction_t wheel_step(point_t ticks) {
    interaction_t in;
    in.wheel = ticks;
    return in;
}

/// @brief A content that fills its rectangle and draws two landmarks: a green line at y = 10 and a
///        blue 4x4 block at its top-left. Both are at CONTENT coordinates, which is what makes the
///        scroll translation and the viewport clipping observable in pixels.
class canvas_t final: public widget_t {
public:
    using widget_t::widget_t;

    extent_t size_hint() const override {
        return _hint;
    }

    void set_hint(extent_t hint) {
        _hint = hint;
    }

    static constexpr color_t fill_color{ 180, 40, 40 };
    static constexpr color_t line_color{ 40, 200, 40 };
    static constexpr color_t corner_color{ 40, 40, 220 };

protected:
    void paint_event(paint_event_t* event) override {
        painter_t& painter = event->painter();
        painter.set_color(fill_color);
        painter.fill_rect(rect());
        painter.set_color(line_color);
        painter.fill_rect(rect_t{ 0, 10, rect().width(), 2 });
        painter.set_color(corner_color);
        painter.fill_rect(rect_t{ 0, 0, 4, 4 });
    }

private:
    extent_t _hint{ 200, 150 };
};

/// @brief A content for the standalone viewport: a fill with two 4x4 landmarks at known content
///        coordinates, so a source-to-destination translation is observable in pixels.
class viewport_canvas_t final: public widget_t {
public:
    using widget_t::widget_t;

    extent_t size_hint() const override {
        return extent_t{ 64, 48 };
    }

    extent_t minimum_size_hint() const override {
        return extent_t{};
    }

    static constexpr color_t fill_color{ 20, 120, 200 };
    static constexpr color_t first_marker{ 240, 40, 40 };
    static constexpr color_t second_marker{ 40, 240, 40 };

protected:
    void paint_event(paint_event_t* event) override {
        painter_t& painter = event->painter();
        painter.set_color(fill_color);
        painter.fill_rect(rect());
        painter.set_color(first_marker);
        painter.fill_rect(rect_t{ 10, 12, 4, 4 });
        painter.set_color(second_marker);
        painter.fill_rect(rect_t{ 40, 30, 4, 4 });
    }
};

/// @brief A probe that records the wheel it is handed and never consumes it (so a retained step
///        keeps bubbling); registered so the immediate host can create it by name.
class wheel_probe_t final: public widget_t {
    OBJECT

public:
    wheel_probe_t():
        widget_t(nullptr) { }

    explicit wheel_probe_t(widget_t* parent):
        widget_t(parent) { }

    static inline point_t last_wheel{};
    static inline point_t last_mouse{};
    static inline int wheel_steps = 0;

    static void reset() {
        last_wheel = point_t{};
        last_mouse = point_t{};
        wheel_steps = 0;
    }

    extent_t size_hint() const override {
        return extent_t{ 40, 20 };
    }

    bool interact(const interaction_t& in) override {
        if (in.key == 0 && (in.wheel.x() != 0 || in.wheel.y() != 0)) {
            last_wheel = in.wheel;
            last_mouse = in.mouse;
            ++wheel_steps;
        }
        return false; // not consumed: the retained step keeps bubbling
    }

protected:
    void paint_event(paint_event_t*) override { }
};
META_OBJECT(wheel_probe_t);
META_REGISTER_TYPE(wheel_probe_t);

/// @brief Paint @p area into a fresh white image and return it (the pixel checks' helper).
void paint_area(scroll_view_t& area, image_t& image, painter_t& painter) {
    painter.clear(color_t::white_color());
    area.render(painter);
}

}  // namespace scroll_test

using namespace scroll_test;

int main() {
    install_log_handler([](log_level_enum, const log_context_t&, std::string_view) {
        return true; // consumed: the default stderr sink stays silent
    });
    (void)meta_type_t::register_type<bool>();

    std::printf("== M3a: wheel + scroll_bar_t + scroll_view_t ==\n");

    // -----------------------------------------------------------------------
    // (1) The wheel field: the input state's accumulator, its reset, and a control reading the
    //     snapshot under both hosts.
    // -----------------------------------------------------------------------
    {
        input_state_t state;
        CHECK(state.wheel_delta() == point_t{});
        state.add_wheel(point_t{ 2, -3 });
        state.add_wheel(point_t{ 1, 1 });
        CHECK(state.wheel_delta() == point_t{ 3, -2 });
        state.reset_wheel();
        CHECK(state.wheel_delta() == point_t{}); // the wheel is cleared ...
        state.set_mouse_position(point_t{ 7, 8 });
        CHECK(state.mouse_position() == point_t{ 7, 8 }); // ... and the levels survive
        state.add_wheel(point_t{ 1, 0 });
        state.reset_frame();
        CHECK(state.wheel_delta() == point_t{});

        interaction_t step = wheel_step(point_t{ 1, -2 });
        CHECK(step.wheel == point_t{ 1, -2 });
        CHECK(interaction_t{}.wheel == point_t{}); // the default step carries no wheel

        // (1a) The RETAINED host: the router delivers the snapshot; the probe reads the wheel and the
        //      mapped mouse position.
        {
            widget_t root;
            wheel_probe_t probe{ &root };
            root.set_geometry(rect_t{ 0, 0, 100, 100 });
            probe.set_geometry(rect_t{ 0, 0, 40, 20 });
            input_router_t router{ root };
            wheel_probe_t::reset();
            interaction_t in = wheel_step(point_t{ 1, -2 });
            in.mouse = point_t{ 5, 6 };
            CHECK(!router.dispatch(in)); // the probe does not consume: the step bubbles and ends false
            CHECK(wheel_probe_t::wheel_steps == 1);
            CHECK(wheel_probe_t::last_wheel == point_t{ 1, -2 });
            CHECK(wheel_probe_t::last_mouse == point_t{ 5, 6 });
        }

        // (1b) The IMMEDIATE host: its batch replay copies the wheel into each control's step.
        {
            image_t image{ 100, 60 };
            painter_t painter{ image };
            painter.begin();
            im_host_t host;
            host.set_geometry(rect_t{ 0, 0, 100, 60 });
            host.set_frame_handler([](im_host_t& driven) { (void)driven.control("wheel_probe_t", "p"); });
            host.render(painter);
            wheel_probe_t::reset();
            interaction_t in = wheel_step(point_t{ -1, 3 });
            in.mouse = point_t{ 5, 6 };
            CHECK(host.interact(in));
            host.render(painter);
            CHECK(wheel_probe_t::wheel_steps == 1);
            CHECK(wheel_probe_t::last_wheel == point_t{ -1, 3 });
            CHECK(wheel_probe_t::last_mouse == point_t{ 5, 6 });
            painter.end();
        }
    }

    // -----------------------------------------------------------------------
    // (2) scroll_bar_t: defaults, size hint and the style metrics it comes from.
    // -----------------------------------------------------------------------
    {
        const style_t& style = default_style();
        scroll_bar_t bar;
        CHECK(bar.minimum() == 0 && bar.maximum() == 100 && bar.value() == 0);
        CHECK(bar.page_step() == 10 && bar.single_step() == 1);
        CHECK(bar.orientation() == orientation_enum::VERTICAL);
        CHECK(bar.focus_policy() == focus_policy_enum::STRONG_FOCUS); // an interactive control
        CHECK(bar.size_hint()
              == extent_t{ style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS),
                           style.pixel_metric(pixel_metric_enum::SCROLL_BAR_LENGTH) });
        bar.set_orientation(orientation_enum::HORIZONTAL);
        CHECK(bar.size_hint()
              == extent_t{ style.pixel_metric(pixel_metric_enum::SCROLL_BAR_LENGTH),
                           style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS) });
        CHECK(scroll_bar_t::static_meta_object.user_property().is_valid());
        CHECK(scroll_bar_t::static_meta_object.user_property().name() == "value_changed");
        CHECK(!scroll_view_t::static_meta_object.user_property().is_valid());
        CHECK(scroll_view_t().focus_policy() == focus_policy_enum::STRONG_FOCUS);
    }

    // -----------------------------------------------------------------------
    // (3) scroll_bar_t: the groove page, the handle drag, the clamps and the signals.
    // -----------------------------------------------------------------------
    {
        const style_t& style = default_style();
        const int thickness = style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);
        const int min_handle = style.pixel_metric(pixel_metric_enum::SCROLL_BAR_MIN_HANDLE);
        CHECK(thickness == 14 && min_handle == 16);

        scroll_bar_t bar;
        bar.set_orientation(orientation_enum::VERTICAL);
        bar.set_geometry(rect_t{ 0, 0, thickness, 120 }); // track 120, handle 16, travel 104
        int changes = 0;
        int last_value = -1;
        bar.connect("value_changed(int) const", [&changes, &last_value](int value) {
            ++changes;
            last_value = value;
        });

        // The handle sits at the top for value 0; a press BELOW it pages towards the maximum.
        CHECK(bar.interact(press(point_t{ 7, 100 })));
        CHECK(bar.value() == 10);
        CHECK(changes == 1);
        CHECK(last_value == 10);
        CHECK(bar.value_changed());
        CHECK(user_bool(bar));
        // A groove page is a press-only action: the bar does not grab the pointer, so the release is
        // not its step (the host's implicit grab still delivers it).
        CHECK(!bar.interact(release(point_t{ 7, 100 }, true)));
        CHECK(!bar.interact(move(point_t{ 7, 100 }, false)));
        CHECK(!bar.value_changed()); // the next driven frame clears the event

        // One more page towards the maximum.
        CHECK(bar.interact(press(point_t{ 7, 100 }))); // handle 10..25 -> still below
        CHECK(bar.value() == 20);
        CHECK(!bar.interact(release(point_t{ 7, 100 }, true)));
        // A press ABOVE the handle pages back towards the minimum.
        CHECK(bar.interact(press(point_t{ 7, 5 }))); // handle 20..35 -> above it
        CHECK(bar.value() == 10);
        CHECK(!bar.interact(release(point_t{ 7, 5 }, true)));

        // Drag: grab the handle at its top (value 20 -> handle start 20), then move.
        bar.set_value(20); // silent
        const int changes_before = changes;
        CHECK(bar.interact(press(point_t{ 7, 22 }))); // inside the handle (start 20, length 16)
        CHECK(bar.value() == 20);                    // grabbing the handle does not jump
        CHECK(changes == changes_before);
        CHECK(bar.interact(move(point_t{ 7, 74 }, true))); // start 72 -> value 69
        CHECK(bar.value() == 69);
        CHECK(changes == changes_before + 1);
        CHECK(bar.interact(move(point_t{ 7, 500 }, true))); // far outside: clamped to the maximum
        CHECK(bar.value() == 100);
        CHECK(bar.interact(release(point_t{ 7, 500 }, true)));
        CHECK(bar.value() == 100);
        CHECK(!bar.interact(release(point_t{ 7, 500 }, true))); // no drag: the release is not ours

        // The clamp invariant; the setters are silent.
        bar.set_value(1000);
        CHECK(bar.value() == 100);
        bar.set_value(-20);
        CHECK(bar.value() == 0);
        bar.set_range(10, 20);
        CHECK(bar.minimum() == 10 && bar.maximum() == 20);
        CHECK(bar.value() == 10);
        bar.set_range(30, 10); // inverted: swapped
        CHECK(bar.minimum() == 10 && bar.maximum() == 30);
        CHECK(bar.value() == 10);
        bar.set_page_step(-5);
        CHECK(bar.page_step() == 0);
        bar.set_page_step(10);

        // Disabled: consumes nothing and never changes.
        bar.set_value(15);
        const int disabled_changes = changes;
        bar.set_enabled(false);
        CHECK(!bar.interact(press(point_t{ 7, 100 })));
        CHECK(!bar.interact(move(point_t{ 7, 30 }, true)));
        CHECK(!bar.interact(release(point_t{ 7, 100 }, true)));
        CHECK(!bar.interact(key_press(interaction_t::down_key)));
        CHECK(bar.value() == 15);
        CHECK(changes == disabled_changes);
    }

    // -----------------------------------------------------------------------
    // (4) scroll_bar_t keyboard: arrows/pages/Home/End, "consumed but silent" at the bounds.
    // -----------------------------------------------------------------------
    {
        scroll_bar_t bar;
        bar.set_geometry(rect_t{ 0, 0, 14, 120 });
        bar.set_range(0, 100);
        bar.set_value(50);       // silent
        int changes = 0;
        bar.connect("value_changed(int) const", [&changes]() { ++changes; });
        input_router_t router{ bar }; // the bar is its own root item

        CHECK(!bar.interact(key_press(interaction_t::down_key))); // no focus: not ours
        CHECK(bar.value() == 50);

        router.set_focus(&bar);
        CHECK(bar.has_focus());
        CHECK(router.dispatch(key_press(interaction_t::down_key)));
        CHECK(bar.value() == 51);
        CHECK(changes == 1);
        CHECK(bar.value_changed());
        (void)router.dispatch(key_release(interaction_t::down_key));

        CHECK(router.dispatch(key_press(interaction_t::up_key)));
        CHECK(bar.value() == 50);
        (void)router.dispatch(key_release(interaction_t::up_key));
        CHECK(router.dispatch(key_press(interaction_t::page_down_key)));
        CHECK(bar.value() == 60);
        (void)router.dispatch(key_release(interaction_t::page_down_key));
        CHECK(router.dispatch(key_press(interaction_t::page_up_key)));
        CHECK(bar.value() == 50);
        (void)router.dispatch(key_release(interaction_t::page_up_key));
        CHECK(router.dispatch(key_press(interaction_t::end_key)));
        CHECK(bar.value() == 100);
        (void)router.dispatch(key_release(interaction_t::end_key));
        CHECK(router.dispatch(key_press(interaction_t::home_key)));
        CHECK(bar.value() == 0);
        (void)router.dispatch(key_release(interaction_t::home_key));

        // At the minimum: consumed, no change, no signal, no User event.
        const int before = changes;
        CHECK(router.dispatch(key_press(interaction_t::up_key)));
        CHECK(bar.value() == 0);
        CHECK(changes == before);
        CHECK(!bar.value_changed());
        (void)router.dispatch(key_release(interaction_t::up_key));
    }

    // -----------------------------------------------------------------------
    // (5) scroll_bar_t painting: the handle and the groove are different colours, and the handle
    //     moves with the value.
    // -----------------------------------------------------------------------
    {
        image_t image{ 14, 120 };
        painter_t painter{ image };
        painter.begin();
        scroll_bar_t bar;
        bar.set_geometry(rect_t{ 0, 0, 14, 120 });
        bar.set_range(0, 100);
        painter.clear(color_t::white_color());
        bar.set_value(0);
        bar.render(painter);
        const color_t top_at_start = image.pixel(7, 2);
        const color_t bottom_at_start = image.pixel(7, 112);
        CHECK(top_at_start == image.pixel(7, 8));   // the whole handle is one colour
        CHECK(top_at_start != bottom_at_start);     // ... and the groove is another
        painter.clear(color_t::white_color());
        bar.set_value(100);
        bar.render(painter);
        CHECK(image.pixel(7, 112) == top_at_start); // the handle moved to the far end
        CHECK(image.pixel(7, 2) == bottom_at_start);
        painter.end();
    }

    // -----------------------------------------------------------------------
    // (6) scroll_view_t: content ownership, the viewport/range computation and the policies.
    // -----------------------------------------------------------------------
    {
        const style_t& style = default_style();
        const int thickness = style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);
        CHECK(thickness == 14);

        scroll_view_t area;
        CHECK(area.focus_policy() == focus_policy_enum::STRONG_FOCUS);
        CHECK(area.content() == nullptr);
        CHECK(area.scroll_bar_policy() == scroll_bar_policy_enum::AS_NEEDED);
        CHECK(area.vertical_scroll_bar() != nullptr && area.horizontal_scroll_bar() != nullptr);
        CHECK(area.vertical_scroll_bar()->parent() == &area); // real child items (hit-testable)
        CHECK(area.horizontal_scroll_bar()->parent() == &area);
        CHECK(area.vertical_scroll_bar()->orientation() == orientation_enum::VERTICAL);
        CHECK(area.horizontal_scroll_bar()->orientation() == orientation_enum::HORIZONTAL);

        auto* const first = new canvas_t;
        first->set_hint(extent_t{ 200, 150 });
        first->set_fixed_size(extent_t{ 200, 150 });
        object_pointer_t<object_t> first_guard{ first };
        area.set_content(first);
        CHECK(area.content() == first);
        CHECK(first->parent() == &area);
        CHECK(area.size_hint() == extent_t{ 200 + thickness, 150 + thickness });

        area.set_geometry(rect_t{ 0, 0, 100, 80 });
        // Content 200x150, viewport 86x66 (both bars needed) -> ranges 114 / 84.
        CHECK(area.vertical_scroll_bar()->is_visible());
        CHECK(area.horizontal_scroll_bar()->is_visible());
        CHECK(area.horizontal_scroll_bar()->maximum() == 200 - (100 - thickness));
        CHECK(area.vertical_scroll_bar()->maximum() == 150 - (80 - thickness));
        CHECK(area.horizontal_scroll_bar()->page_step() == 100 - thickness);
        CHECK(area.vertical_scroll_bar()->page_step() == 80 - thickness);
        CHECK(area.scroll_offset() == point_t{});
        CHECK(first->geometry() == rect_t{ 0, 0, 200, 150 });

        // The offset is clamped per axis and moves the SOURCE REGION by exactly the offset; the
        // content's own geometry never changes.
        area.set_scroll_offset(point_t{ 10, 20 });
        CHECK(area.scroll_offset() == point_t{ 10, 20 });
        CHECK(first->geometry() == rect_t{ 0, 0, 200, 150 });
        CHECK(area.source_rect() == (rect_t{ 10, 20, 86, 66 }));
        CHECK(area.destination_rect() == (rect_t{ 0, 0, 86, 66 }));
        area.set_scroll_offset(point_t{ 1000, 1000 });
        CHECK(area.scroll_offset() == point_t{ 114, 84 });
        CHECK(area.source_rect() == (rect_t{ 114, 84, 86, 66 }));
        area.set_scroll_offset(point_t{ -5, -5 });
        CHECK(area.scroll_offset() == point_t{ 0, 0 });
        CHECK(first->geometry() == rect_t{ 0, 0, 200, 150 });
        CHECK(area.source_rect() == (rect_t{ 0, 0, 86, 66 }));

        // ALWAYS_ON: both bars stay visible even when the content is smaller.
        auto* const small = new canvas_t;
        small->set_hint(extent_t{ 50, 40 });
        small->set_fixed_size(extent_t{ 50, 40 });
        area.set_content(small);
        CHECK(first_guard.data() == nullptr); // the replaced content was deleted (Qt's setWidget)
        CHECK(area.scroll_bar_policy() == scroll_bar_policy_enum::AS_NEEDED);
        CHECK(area.vertical_scroll_bar()->is_visible() == false);
        CHECK(area.horizontal_scroll_bar()->is_visible() == false);
        CHECK(area.scroll_offset() == point_t{});
        area.set_scroll_bar_policy(scroll_bar_policy_enum::ALWAYS_ON);
        CHECK(area.vertical_scroll_bar()->is_visible());
        CHECK(area.horizontal_scroll_bar()->is_visible());
        CHECK(area.vertical_scroll_bar()->maximum() == 0); // nothing to scroll, still shown
        area.set_scroll_bar_policy(scroll_bar_policy_enum::ALWAYS_OFF);
        CHECK(!area.vertical_scroll_bar()->is_visible());
        CHECK(!area.horizontal_scroll_bar()->is_visible());
        CHECK(area.size_hint() == extent_t{ 50, 40 }); // ALWAYS_OFF reserves no bar thickness
        area.set_scroll_bar_policy(scroll_bar_policy_enum::AS_NEEDED);
        CHECK(area.size_hint() == extent_t{ 50 + thickness, 40 + thickness });

        // Changing the content's size updates the ranges on the next layout pass. The geometry is
        // unchanged, so set_geometry() would be a no-op; writing the offset (even with the value it
        // already holds) is the public step that re-derives the scroll state.
        small->set_fixed_size(extent_t{ 300, 90 });
        area.set_scroll_offset(area.scroll_offset());
        CHECK(area.vertical_scroll_bar()->is_visible());
        CHECK(area.horizontal_scroll_bar()->is_visible());
        CHECK(area.horizontal_scroll_bar()->maximum() == 300 - (100 - thickness));
        CHECK(area.vertical_scroll_bar()->maximum() == 90 - (80 - thickness));

        // The area refuses a cyclic content; the current content stays.
        area.set_content(&area);
        CHECK(area.content() == small);
        area.set_content(area.vertical_scroll_bar());
        CHECK(area.content() == small);

        // set_content(nullptr) removes and deletes the content; the bars go away.
        object_pointer_t<object_t> guard{ small };
        area.set_content(nullptr);
        CHECK(area.content() == nullptr);
        CHECK(guard.data() == nullptr);
        CHECK(!area.vertical_scroll_bar()->is_visible());
        CHECK(!area.horizontal_scroll_bar()->is_visible());
    }

    // -----------------------------------------------------------------------
    // (7) scroll_view_t: the wheel (both axes, clamping and the "only consume a step that moved"
    //     rule) and the keyboard.
    // -----------------------------------------------------------------------
    {
        const style_t& style = default_style();
        const int wheel_pixels = style.pixel_metric(pixel_metric_enum::SCROLL_WHEEL_STEP);
        const int thickness = style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);
        CHECK(wheel_pixels == 16);

        scroll_view_t area;
        auto* const content = new canvas_t;
        content->set_hint(extent_t{ 200, 150 });
        content->set_fixed_size(extent_t{ 200, 150 });
        area.set_content(content);
        area.set_geometry(rect_t{ 0, 0, 100, 80 });
        const int max_x = area.horizontal_scroll_bar()->maximum();
        const int max_y = area.vertical_scroll_bar()->maximum();
        CHECK(max_x == 114 && max_y == 84);

        // One notch away from the user (y = -1) scrolls DOWN (the offset grows); x grows it too.
        CHECK(area.interact(wheel_step(point_t{ 0, -1 })));
        CHECK(area.scroll_offset() == point_t{ 0, wheel_pixels });
        CHECK(area.interact(wheel_step(point_t{ 1, 0 })));
        CHECK(area.scroll_offset() == point_t{ wheel_pixels, wheel_pixels });
        CHECK(area.interact(wheel_step(point_t{ 0, 1 })));
        CHECK(area.scroll_offset() == point_t{ wheel_pixels, 0 }); // away from the user: back up
        CHECK(content->geometry() == rect_t{ 0, 0, 200, 150 }); // the content never moves
        CHECK(area.source_rect() == (rect_t{ wheel_pixels, 0, 86, 66 }));
        CHECK(area.horizontal_scroll_bar()->value() == wheel_pixels);
        CHECK(area.vertical_scroll_bar()->value() == 0);

        // Clamped at the boundaries; a step that cannot move is not consumed (it keeps bubbling).
        area.set_scroll_offset(point_t{ max_x, max_y });
        CHECK(!area.interact(wheel_step(point_t{ 0, -1 })));
        CHECK(area.scroll_offset() == point_t{ max_x, max_y });
        area.set_scroll_offset(point_t{ 0, 0 });
        CHECK(!area.interact(wheel_step(point_t{ 0, 1 })));
        CHECK(area.scroll_offset() == point_t{ 0, 0 });
        CHECK(!area.interact(move(point_t{ 5, 5 }, false))); // a plain mouse step is not the area's

        // Keyboard: the area must hold the focus; the page step is the viewport extent.
        input_router_t router{ area };
        CHECK(!area.interact(key_press(interaction_t::down_key))); // no focus: not ours
        CHECK(area.scroll_offset() == point_t{ 0, 0 });
        router.set_focus(&area);
        CHECK(area.has_focus());
        CHECK(router.dispatch(key_press(interaction_t::down_key)));
        CHECK(area.scroll_offset() == point_t{ 0, 1 }); // single_step
        (void)router.dispatch(key_release(interaction_t::down_key));
        CHECK(router.dispatch(key_press(interaction_t::right_key)));
        CHECK(area.scroll_offset() == point_t{ 1, 1 });
        (void)router.dispatch(key_release(interaction_t::right_key));
        CHECK(router.dispatch(key_press(interaction_t::page_down_key)));
        CHECK(area.scroll_offset() == point_t{ 1, 1 + (80 - thickness) });
        (void)router.dispatch(key_release(interaction_t::page_down_key));
        CHECK(router.dispatch(key_press(interaction_t::end_key)));
        CHECK(area.scroll_offset() == point_t{ max_x, max_y });
        (void)router.dispatch(key_release(interaction_t::end_key));
        CHECK(router.dispatch(key_press(interaction_t::home_key)));
        CHECK(area.scroll_offset() == point_t{ 0, 0 });
        (void)router.dispatch(key_release(interaction_t::home_key));

        // A focused control INSIDE the content wins over the area: the key never reaches the area.
        auto* const button = new push_button_t(content);
        button->set_geometry(rect_t{ 0, 0, 40, 26 });
        router.set_focus(button);
        CHECK(button->has_focus());
        CHECK(!area.has_focus());
        const point_t before = area.scroll_offset();
        CHECK(!router.dispatch(key_press(interaction_t::down_key))); // the button ignores it, no bubble
        CHECK(area.scroll_offset() == before);
        (void)router.dispatch(key_release(interaction_t::down_key));
        CHECK(!area.interact(key_press(interaction_t::down_key))); // defensive: area refuses too
    }

    // -----------------------------------------------------------------------
    // (8) scroll_view_t: ensure_visible reveals an item inside the content.
    // -----------------------------------------------------------------------
    {
        const int thickness = default_style().pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);
        scroll_view_t area;
        auto* const content = new canvas_t;
        content->set_hint(extent_t{ 200, 150 });
        content->set_fixed_size(extent_t{ 200, 150 });
        area.set_content(content);
        area.set_geometry(rect_t{ 0, 0, 100, 80 });
        const int viewport_width = 100 - thickness;
        const int viewport_height = 80 - thickness;

        auto* const marker = new widget_t(content);
        marker->set_geometry(rect_t{ 150, 100, 20, 20 });
        auto* const top_left = new widget_t(content);
        top_left->set_geometry(rect_t{ 0, 0, 10, 10 });

        area.set_scroll_offset(point_t{ 0, 0 });
        area.ensure_visible(marker);
        CHECK(area.scroll_offset() == point_t{ 150 + 20 - viewport_width, 100 + 20 - viewport_height });
        area.ensure_visible(top_left);
        CHECK(area.scroll_offset() == point_t{ 0, 0 });
        area.ensure_visible(nullptr); // ignored
        CHECK(area.scroll_offset() == point_t{ 0, 0 });
        area.ensure_visible(&area); // not inside the content: ignored
        CHECK(area.scroll_offset() == point_t{ 0, 0 });
    }

    // -----------------------------------------------------------------------
    // (9) scroll_view_t clipping and translation: nothing the content draws escapes the viewport,
    //     and the content is drawn at -scroll_offset.
    // -----------------------------------------------------------------------
    {
        const int thickness = default_style().pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);
        image_t image{ 160, 140 };
        painter_t painter{ image };
        painter.begin();
        scroll_view_t area;
        auto* const content = new canvas_t;
        content->set_hint(extent_t{ 200, 150 });
        content->set_fixed_size(extent_t{ 200, 150 });
        area.set_content(content);
        area.set_geometry(rect_t{ 10, 10, 60, 50 });
        const int viewport_width = 60 - thickness;
        const int viewport_height = 50 - thickness;
        const int viewport_right = 10 + viewport_width;
        const int viewport_bottom = 10 + viewport_height;

        // Offset {0, 0}: the content's top-left is the viewport's top-left.
        area.set_scroll_offset(point_t{ 0, 0 });
        paint_area(area, image, painter);
        CHECK(image.pixel(11, 11) == canvas_t::corner_color);          // content (0,0) at the origin
        CHECK(image.pixel(11, 20) == canvas_t::line_color);            // the line at content y=10
        CHECK(image.pixel(40, 40) == canvas_t::fill_color);            // inside the viewport
        CHECK(image.pixel(9, 9) == color_t::white_color());            // outside the area: untouched
        CHECK(image.pixel(150, 130) == color_t::white_color());
        // The content is clipped to the VIEWPORT: the bar column and the bottom-right corner (which
        // no bar covers) show the viewport/bar, never the content colour.
        CHECK(image.pixel(viewport_right + 1, 12) != canvas_t::fill_color);
        CHECK(image.pixel(viewport_right + 5, 12) != canvas_t::fill_color);
        CHECK(image.pixel(10 + 60 - 1, 10 + 50 - 1) != canvas_t::fill_color);

        // Offset {0, 5}: the content is translated up by 5, so the line moves to viewport y=5.
        area.set_scroll_offset(point_t{ 0, 5 });
        paint_area(area, image, painter);
        CHECK(image.pixel(11, 15) == canvas_t::line_color);            // content y=10 moved to 10+5
        CHECK(image.pixel(11, 20) == canvas_t::fill_color);            // ... and the old row is content
        CHECK(image.pixel(11, 11) == canvas_t::fill_color);            // the corner block scrolled away

        // The content's ink never escapes the area's rectangle: the pixel just outside stays white,
        // even with a content much larger than the viewport.
        CHECK(image.pixel(9, 30) == color_t::white_color());
        CHECK(image.pixel(10 + 60, 30) == color_t::white_color());
        CHECK(image.pixel(30, viewport_bottom + 1) != color_t::white_color()); // the bar, not white
        CHECK(image.pixel(30, 10 + 50) == color_t::white_color());             // below the area
        painter.end();
    }

    // -----------------------------------------------------------------------
    // (10) The viewport is the HIT-TESTING clip too: an area shows an oversized content through a
    //      window, so the content owns only the points inside that window. The three answers the
    //      task pins: a point on a bar is the bar's, a point in the viewport is the content's, and a
    //      point inside the area but outside the viewport is nobody's (never the content's).
    // -----------------------------------------------------------------------
    {
        const int thickness = default_style().pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);
        const extent_t viewport_extent{ 200, 120 };
        const extent_t content_extent{ 400, 400 }; // the content is far larger than the viewport
        const rect_t area_rect{ 0, 0, viewport_extent.width() + thickness,
                                viewport_extent.height() + thickness };

        scroll_view_t area;
        auto* const content = new canvas_t;
        content->set_hint(content_extent);
        content->set_fixed_size(content_extent);
        area.set_content(content);
        area.set_geometry(area_rect);
        // A child INSIDE the content, to prove the clip does not stop the recursion into it.
        auto* const inside = new push_button_t(content);
        inside->set_text("in");
        inside->set_geometry(rect_t{ 10, 10, 40, 26 });

        CHECK(area.vertical_scroll_bar()->is_visible());
        CHECK(area.horizontal_scroll_bar()->is_visible());
        // The content really does overflow the area: that is what makes the clip observable at all.
        CHECK(content->geometry() == rect_t{ 0, 0, 400, 400 });
        CHECK(content->geometry().width() > area_rect.width());

        // The clip contract itself: the content is clipped to the VIEWPORT, the bars keep the whole
        // area (they lie beside the viewport, so clipping them to it would clip them away entirely).
        CHECK((area.items_rect(*content) == rect_t{ 0, 0, 200, 120 }));
        CHECK(area.items_rect(*area.vertical_scroll_bar()) == area_rect);
        CHECK(area.items_rect(*area.horizontal_scroll_bar()) == area_rect);

        // The bands of the two bars, and the corner square they leave empty (inside the area, on no
        // bar and outside the viewport — the only such point in this layout).
        const point_t bar_point{ 200 + thickness / 2, 60 };   // vertical bar, y inside the viewport
        const point_t h_bar_point{ 100, 120 + thickness / 2 }; // horizontal bar
        const point_t corner_point{ 200 + thickness / 2, 120 + thickness / 2 };

        // 1. A point on the VERTICAL bar hits the bar, not the content: the content's rectangle
        //    covers that column, so only the clip keeps it out.
        CHECK((area.hit(bar_point) == area.vertical_scroll_bar()));
        CHECK((area.hit(h_bar_point) == area.horizontal_scroll_bar()));
        // The content is not the answer for either bar band, which is the defect itself.
        CHECK((area.hit(bar_point) != content));
        CHECK((area.hit(h_bar_point) != content));

        // 2. A point inside the VIEWPORT still hits the content, and inside the content's own child
        //    it hits that child: the clip must not cut the visible part away as well.
        CHECK((area.hit(point_t{ 100, 60 }) == content));
        CHECK((area.hit(point_t{ 20, 20 }) == inside));

        // 3. A point inside the AREA but outside the viewport (the corner no bar covers) must not hit
        //    the content: hit() answers nullptr, i.e. the area itself owns it.
        CHECK((area.hit(corner_point) == nullptr));

        // The clip does not follow the scroll offset: the source region moves over the content, the
        // window it is shown through does not, and the content's own geometry stays at the origin.
        area.set_scroll_offset(point_t{ 100, 100 });
        CHECK(content->geometry() == rect_t{ 0, 0, 400, 400 });
        CHECK(area.source_rect() == (rect_t{ 100, 100, 200, 120 }));
        CHECK((area.items_rect(*content) == rect_t{ 0, 0, 200, 120 }));
        CHECK((area.hit(bar_point) == area.vertical_scroll_bar()));
        CHECK((area.hit(point_t{ 100, 60 }) == content));
        CHECK((area.hit(corner_point) == nullptr));
        area.set_scroll_offset(point_t{ 0, 0 });

        // The same answers through the ROUTER, which is what a real host uses: the press lands on the
        // bar and pages it. With the content winning the hit test the bar never saw the press, so its
        // value and the area's offset stayed where they were.
        input_router_t router{ area };
        const int bar_value_before = area.vertical_scroll_bar()->value();
        CHECK(bar_value_before == 0);
        CHECK(router.dispatch(press(bar_point))); // the bar takes the press (its implicit grab too)
        CHECK(area.vertical_scroll_bar()->value() > bar_value_before);
        CHECK(area.scroll_offset().y() == area.vertical_scroll_bar()->value());
        (void)router.dispatch(release(bar_point, true));
    }

    // -----------------------------------------------------------------------
    // (11) viewport_t used WITHOUT a subclass: it maps the content's source region onto its own
    //      destination rectangle, creates no scroll bar and does not reparent the content.
    // -----------------------------------------------------------------------
    {
        viewport_canvas_t content;
        content.set_geometry(rect_t{ 0, 0, 64, 48 });

        viewport_t viewport;
        viewport.set_geometry(rect_t{ 0, 0, 100, 100 });
        viewport.set_content(&content);
        viewport.set_source_rect(rect_t{ 8, 10, 20, 20 });
        viewport.set_destination_rect(rect_t{ 30, 40, 20, 20 });
        CHECK(viewport.content() == &content);
        CHECK(viewport.item_count() == 0); // no scroll bars, and the content is not reparented
        CHECK(content.parent() == nullptr);
        const int frame = default_style().pixel_metric(pixel_metric_enum::BORDER_WIDTH);
        CHECK(viewport.body_rect()
              == (rect_t{ frame, frame, 100 - 2 * frame, 100 - 2 * frame }));

        image_t image{ 100, 100 };
        painter_t painter{ image };
        painter.begin();
        painter.clear(color_t::white_color());
        viewport.render(painter);
        // The content's (8, 10) lands on the destination's (30, 40), so its (10, 12) marker lands
        // on (32, 42): this is the source -> destination translation itself.
        CHECK(image.pixel(30, 40) == viewport_canvas_t::fill_color);
        CHECK(image.pixel(32, 42) == viewport_canvas_t::first_marker);
        CHECK(image.pixel(49, 59) == viewport_canvas_t::fill_color);
        // Outside the destination the frame is painted, never the content.
        CHECK(image.pixel(29, 40) != viewport_canvas_t::fill_color); // clipped at the left
        CHECK(image.pixel(50, 40) != viewport_canvas_t::fill_color);  // ... and right
        CHECK(image.pixel(30, 60) != viewport_canvas_t::fill_color);  // ... and bottom
        CHECK(image.pixel(11, 13) != viewport_canvas_t::fill_color);  // the content's own place
        painter.end();

        // (12) The destination really is what the pixels follow: the same source region at another
        //      destination, then another source region at that destination.
        viewport.set_destination_rect(rect_t{ 60, 20, 20, 20 });
        image_t moved{ 100, 100 };
        painter_t moved_painter{ moved };
        moved_painter.begin();
        moved_painter.clear(color_t::white_color());
        viewport.render(moved_painter);
        CHECK(moved.pixel(62, 22) == viewport_canvas_t::first_marker);
        CHECK(moved.pixel(32, 42) != viewport_canvas_t::first_marker); // the old place is not painted
        moved_painter.end();

        viewport.set_source_rect(rect_t{ 36, 26, 20, 20 });
        image_t other_region{ 100, 100 };
        painter_t other_painter{ other_region };
        other_painter.begin();
        other_painter.clear(color_t::white_color());
        viewport.render(other_painter);
        CHECK(other_region.pixel(64, 24) == viewport_canvas_t::second_marker);
        CHECK(other_region.pixel(61, 21) == viewport_canvas_t::fill_color);
        other_painter.end();
    }

    // -----------------------------------------------------------------------
    // (13) scroll_view_t: scrolling changes the source rectangle and the two bar values, never the
    //      content item's geometry.
    // -----------------------------------------------------------------------
    {
        scroll_view_t area;
        auto* const content = new canvas_t;
        content->set_hint(extent_t{ 200, 150 });
        content->set_fixed_size(extent_t{ 200, 150 });
        area.set_content(content);
        area.set_geometry(rect_t{ 0, 0, 100, 80 });
        const rect_t geometry = content->geometry();
        CHECK(area.source_rect() == (rect_t{ 0, 0, 86, 66 }));
        CHECK(area.destination_rect() == (rect_t{ 0, 0, 86, 66 }));

        area.set_scroll_offset(point_t{ 10, 20 });
        CHECK(content->geometry() == geometry); // the content never moves
        CHECK(content->geometry() == (rect_t{ 0, 0, 200, 150 }));
        CHECK(area.scroll_offset() == point_t{ 10, 20 });
        CHECK(area.source_rect() == (rect_t{ 10, 20, 86, 66 }));
        CHECK(area.vertical_scroll_bar()->value() == 20);
        CHECK(area.horizontal_scroll_bar()->value() == 10);
        CHECK(area.destination_rect() == (rect_t{ 0, 0, 86, 66 }));

        // Scrolling back to the origin restores the same source rectangle; the geometry is still
        // the one the content has owned from the start.
        area.set_scroll_offset(point_t{ 0, 0 });
        CHECK(content->geometry() == geometry);
        CHECK(area.source_rect() == (rect_t{ 0, 0, 86, 66 }));
        CHECK(area.vertical_scroll_bar()->value() == 0);
        CHECK(area.horizontal_scroll_bar()->value() == 0);
    }

    install_log_handler(log_handler{});

    std::printf("scroll: %d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
