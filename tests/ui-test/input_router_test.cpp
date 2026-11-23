// W3: ui::input_router_t — the retained host's event routing (the interaction_t contract, applied to
// items rather than widgets only).
//
// The ten acceptance cases as executable checks:
//  1. hit + bubble: root > panel > button, point inside the button -> button, then (it returns false)
//     panel with the point translated into PANEL coordinates, then root; the first true stops;
//  2. a point inside the panel but outside the button -> only the panel (and up);
//  3. reverse order: two overlapping children -> the later child is hit (and the deepest widget wins);
//  4. invisible/disabled widgets and their subtrees are skipped by the hit test;
//  5. implicit grab: a press TAKES the pointer (independent of consumption); move far outside and
//     release -> BOTH move and release reach the item, which stays mouse_target() until the release;
//  6. focus: Tab cycles depth-first through the focusable widgets only (invisible/disabled/no_focus
//     skipped), wraps; Shift+Tab goes backwards; focus_in/focus_out fire exactly once per change;
//     has_focus() agrees;
//  7. keys: with no focus a key interaction is delivered to nobody; with focus exactly one widget
//     gets it (no bubbling);
//  8. item targets: a NON-widget item is hit, consumes, grabs, and is a focus station itself (the
//     im_host_t case), while the widget chain underneath sees nothing;
//  9. item_t::set_focus() (inherited by widget_t) with a router attached focuses that item;
//     without a host it is a no-op and does not crash;
// 10. click focus: a PRESS (edge, not a move/release) focuses the hit item's nearest
//     CLICK_FOCUS/STRONG_FOCUS ancestor that is focusable; the focus is set BEFORE the item's own
//     interact(), consumes nothing, is idempotent, and a press on a TAB_FOCUS/NO_FOCUS/disabled
//     item or on blank space leaves the current focus untouched.
//
// Plus the router's own choices: the synthetic hover-out when the hover target changes, the item
// state a press drives (is_pressed/captures_pointer), that the capture wins over the hit test, and
// guarded pointers that turn null when a focused/grabbed widget is deleted.
//
// No window and no video subsystem: the router is a plain class, so this is headless like the other ui
// tests. The host adapter (widget_window_t's native filter) is exercised by examples/ui_demo.

#include <cstddef>
#include <cstdio>
#include <type_traits>
#include <vector>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/metaobject/meta_method_t.h"
#include "hwshqtb/metaobject/meta_object_t.h"
#include "hwshqtb/ui/input_router_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using hwshqtb::ui::focus_policy_enum;
using hwshqtb::ui::input_router_t;
using hwshqtb::ui::item_t;
using hwshqtb::ui::widget_t;

namespace input_router_test {

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

/// One delivery: which widget saw an interaction, and exactly what it saw.
struct delivery_t {
    const void* widget = nullptr;
    interaction_t in{};
};

/// The global delivery log (in call order), so the tests can assert order AND payload.
std::vector<delivery_t>& log() {
    static std::vector<delivery_t> entries;
    return entries;
}

void clear_log() {
    log().clear();
}

/// A widget that records every interact() call and returns a configurable result. It also counts the
/// focus hooks, which is how "exactly once per change" is checked. A test fixture, not a reflected
/// class: it derives from widget_t without an OBJECT table (the documented way, as in ui_test.cpp).
class probe_widget_t final: public widget_t {
public:
    using widget_t::widget_t;

    bool interact(const interaction_t& in) override {
        log().push_back(delivery_t{ this, in });
        if (in.key != 0) {
            ++key_steps;
            last_key = in.key;
        }
        else {
            ++mouse_steps;
            if (in.mouse_pressed) {
                ++presses;
                press_focused = has_focus(); // the router must focus before the delivery
            }
            if (in.mouse_released) {
                ++releases;
            }
            if (in.hovered) {
                ++hovered_steps;
            }
            else {
                ++unhovered_steps;
            }
        }
        return consume;
    }

    void focus_change_event(::hwshqtb::ui::focus_change_event_t* event) override {
        if (event->has_focus()) {
            ++focus_ins;
        }
        else {
            ++focus_outs;
        }
    }

    void reset_counters() {
        mouse_steps = 0;
        presses = 0;
        releases = 0;
        hovered_steps = 0;
        unhovered_steps = 0;
        key_steps = 0;
        last_key = 0;
    }

    /// @brief Whether the widget consumed everything it was given.
    bool consume = false;
    /// @brief How many mouse steps reached this widget.
    int mouse_steps = 0;
    /// @brief How many press edges reached this widget.
    int presses = 0;
    /// @brief How many release edges reached this widget.
    int releases = 0;
    /// @brief How many deliveries had hovered == true.
    int hovered_steps = 0;
    /// @brief How many deliveries had hovered == false.
    int unhovered_steps = 0;
    /// @brief How many key steps reached this widget.
    int key_steps = 0;
    /// @brief The last key code delivered (0 when none).
    int last_key = 0;
    /// @brief How many focus_in_event() calls fired.
    int focus_ins = 0;
    /// @brief How many focus_out_event() calls fired.
    int focus_outs = 0;
    /// @brief Whether the last press it received arrived while it already held the focus.
    bool press_focused = false;
};

/// The three-level tree of acceptance 1/2. Global geometry: root (0,0)-(400,300), panel (10,20)-(210,170),
/// button (40,60)-(100,90).
struct world_t {
    probe_widget_t root;
    probe_widget_t panel{ &root };
    probe_widget_t button{ &panel };

    world_t() {
        root.set_geometry(rect_t{ 0, 0, 400, 300 });
        panel.set_geometry(rect_t{ 10, 20, 200, 150 });
        button.set_geometry(rect_t{ 30, 40, 60, 30 });
    }
};

/// A plain mouse snapshot at @p p (global coordinates; nothing held, no edges).
interaction_t move_to(point_t p) {
    interaction_t in;
    in.mouse = p;
    return in;
}

/// The press edge at @p p (global coordinates), with the button still held.
interaction_t press_at(point_t p) {
    interaction_t in = move_to(p);
    in.mouse_down = true;
    in.mouse_pressed = true;
    return in;
}

/// The release edge at @p p (global coordinates).
interaction_t release_at(point_t p) {
    interaction_t in = move_to(p);
    in.mouse_released = true;
    return in;
}

/// A NON-widget item: the ui::im_host_t role in its minimal form. It consumes everything it is
/// delivered, so the widget chain underneath must never see it. A test fixture, not a reflected
/// class (like probe_widget_t, it inherits the meta-object of its base).
class item_probe_t final: public item_t {
public:
    item_probe_t(widget_t* parent, rect_t geometry) {
        set_parent(parent);
        set_geometry(geometry);
    }

    extent_t size_hint() const override {
        return extent_t{};
    }

    extent_t minimum_size_hint() const override {
        return extent_t{};
    }

    item_t* hit(point_t local) const override {
        return rect().contains(local) ? const_cast<item_probe_t*>(this) : nullptr;
    }

    bool interact(const interaction_t&) override {
        ++steps;
        return true;
    }

    /// @brief How many interactions reached this item.
    int steps = 0;
};

/// A Tab snapshot; @p shift selects Shift+Tab.
interaction_t tab(bool shift) {
    interaction_t in;
    in.key = interaction_t::tab_key;
    in.key_pressed = true;
    in.modifiers = shift ? modifier_bits(modifier_flag::SHIFT) : 0u;
    return in;
}

}  // namespace input_router_test

using namespace input_router_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    std::printf("== W3: ui::input_router_t (retained event routing) ==\n");

    // ---- (0) the interaction_t shape: keys and the mouse defaults ----
    {
        static_assert(std::is_aggregate_v<interaction_t>);
        CHECK(interaction_t::tab_key == 9);

        interaction_t fresh;
        CHECK(fresh.key == 0);
        CHECK(!fresh.key_pressed);
        CHECK(!fresh.key_released);
        CHECK(!fresh.hovered);
        CHECK(fresh.mouse == point_t{ 0, 0 });

        // The key section is additive: the mouse contract is untouched.
        interaction_t mouse_only;
        mouse_only.mouse = point_t{ 3, 4 };
        mouse_only.mouse_pressed = true;
        CHECK(mouse_only.key == 0);
        CHECK(mouse_only.mouse_pressed);

        // The focus/paint handlers are PROTECTED virtuals, internal tree hooks, not scriptable API.
        // They must stay out of widget_t's reflected table — a reflected virtual would add an entry to
        // every derived class's table (the W1/W2 lesson). The scan is by NAME, so it also catches a
        // handler that was annotated differently.
        probe_widget_t probe;
        const hwshqtb::metaobject::meta_object_t* const meta = probe.meta_object();
        CHECK(meta != nullptr);
        CHECK(meta->class_name() == "widget_t");
        const auto has_method_named = [meta](const char* name) {
            for (std::size_t index = 0; index < meta->method_count(); ++index) {
                if (meta->method(static_cast<hwshqtb::metaobject::index_t>(index)).name() == name) {
                    return true;
                }
            }
            return false;
        };
        CHECK(!has_method_named("focus_change_event"));
        CHECK(!has_method_named("paint_event"));
    }

    // ---- (1) hit + bubble: button -> panel (translated) -> root, first true stops ----
    {
        world_t world;
        input_router_t router{ world.root };
        CHECK(router.focus_item() == nullptr);
        CHECK(router.mouse_target() == nullptr);

        clear_log();
        CHECK(!router.dispatch(move_to(point_t{ 50, 70 })));
        CHECK(log().size() == 3);
        CHECK(log()[0].widget == &world.button);
        CHECK(log()[0].in.mouse == point_t{ 10, 10 });
        CHECK(log()[0].in.hovered);
        CHECK(log()[1].widget == &world.panel);
        CHECK(log()[1].in.mouse == point_t{ 40, 50 });
        CHECK(log()[1].in.hovered);
        CHECK(log()[2].widget == &world.root);
        CHECK(log()[2].in.mouse == point_t{ 50, 70 });
        CHECK(log()[2].in.hovered);
        CHECK(world.button.mouse_steps == 1);
        CHECK(world.panel.mouse_steps == 1);
        CHECK(world.root.mouse_steps == 1);

        // The panel consumes: the chain stops there and the root never sees the step.
        world.panel.consume = true;
        clear_log();
        CHECK(router.dispatch(move_to(point_t{ 50, 70 })));
        CHECK(log().size() == 2);
        CHECK(log()[1].widget == &world.panel);
        CHECK(world.root.mouse_steps == 1);

        // The button consumes: it is the only widget that sees the step.
        world.button.consume = true;
        clear_log();
        CHECK(router.dispatch(move_to(point_t{ 50, 70 })));
        CHECK(log().size() == 1);
        CHECK(log()[0].widget == &world.button);
    }

    // ---- (2) inside the panel, outside the button: the panel (and up) only ----
    {
        world_t world;
        input_router_t router{ world.root };
        clear_log();
        CHECK(!router.dispatch(move_to(point_t{ 15, 25 })));
        CHECK(log().size() == 2);
        CHECK(log()[0].widget == &world.panel);
        CHECK(log()[0].in.mouse == point_t{ 5, 5 });
        CHECK(log()[1].widget == &world.root);
        CHECK(world.button.mouse_steps == 0);
    }
    {
        // Outside the root: nobody sees anything (a fresh router, so no hover-out is due either).
        world_t world;
        input_router_t router{ world.root };
        clear_log();
        CHECK(!router.dispatch(move_to(point_t{ 500, 400 })));
        CHECK(log().empty());
    }

    // ---- (3) reverse order: the later overlapping child wins, the deepest widget is the target ----
    {
        probe_widget_t root;
        root.set_geometry(rect_t{ 0, 0, 200, 200 });
        probe_widget_t first{ &root };
        first.set_geometry(rect_t{ 10, 10, 100, 100 });
        probe_widget_t second{ &root };
        second.set_geometry(rect_t{ 20, 20, 100, 100 });   // added later: on top
        probe_widget_t deep{ &second };
        deep.set_geometry(rect_t{ 5, 5, 20, 20 });         // (25,25)-(45,45) in root coordinates

        // A router per case: hover/grab state belongs to the router, and a hover change legitimately
        // adds one synthetic notification to the log.
        {
            input_router_t router{ root };
            clear_log();
            router.dispatch(move_to(point_t{ 30, 30 }));
            CHECK(log().size() == 3);
            CHECK(log()[0].widget == &deep);    // the point is inside it
            CHECK(log()[1].widget == &second);  // bubbling: parent coordinates
            CHECK(log()[2].widget == &root);
        }
        {
            // (22,22) is inside both first and second but outside deep -> the later child wins.
            input_router_t router{ root };
            clear_log();
            router.dispatch(move_to(point_t{ 22, 22 }));
            CHECK(log().size() == 2);
            CHECK(log()[0].widget == &second);
            CHECK(log()[1].widget == &root);
        }
        {
            // Hiding the later child exposes the earlier one (the hidden subtree goes with it).
            second.set_visible(false);
            input_router_t router{ root };
            clear_log();
            router.dispatch(move_to(point_t{ 22, 22 }));
            CHECK(log().size() == 2);
            CHECK(log()[0].widget == &first);
            CHECK(log()[1].widget == &root);
        }
    }

    // ---- (4) invisible / disabled widgets and their whole subtrees are skipped ----
    {
        probe_widget_t root;
        root.set_geometry(rect_t{ 0, 0, 300, 300 });
        probe_widget_t box{ &root };
        box.set_geometry(rect_t{ 10, 10, 100, 100 });
        probe_widget_t child{ &box };
        child.set_geometry(rect_t{ 5, 5, 40, 40 });        // (15,15)-(55,55) in root coordinates
        probe_widget_t other{ &root };
        other.set_geometry(rect_t{ 150, 150, 100, 100 });

        {
            // Both the hidden box and its child are skipped: the root itself is the hit.
            box.set_visible(false);
            input_router_t router{ root };
            clear_log();
            router.dispatch(move_to(point_t{ 20, 20 }));
            CHECK(log().size() == 1);
            CHECK(log()[0].widget == &root);
            CHECK(box.mouse_steps == 0);
            CHECK(child.mouse_steps == 0);
        }
        {
            // A disabled box hides its subtree too.
            box.set_visible(true);
            box.set_enabled(false);
            input_router_t router{ root };
            clear_log();
            router.dispatch(move_to(point_t{ 20, 20 }));
            CHECK(log().size() == 1);
            CHECK(log()[0].widget == &root);
            CHECK(child.mouse_steps == 0);
        }
        {
            // Enabled again: the deepest widget is back.
            box.set_enabled(true);
            input_router_t router{ root };
            clear_log();
            router.dispatch(move_to(point_t{ 20, 20 }));
            CHECK(log().size() == 3);
            CHECK(log()[0].widget == &child);
            CHECK(log()[1].widget == &box);
            CHECK(log()[2].widget == &root);
        }
        {
            // A disabled sibling is skipped; the other one keeps its own point.
            other.set_enabled(false);
            input_router_t router{ root };
            clear_log();
            router.dispatch(move_to(point_t{ 160, 160 }));
            CHECK(log().size() == 1);
            CHECK(log()[0].widget == &root);
            CHECK(other.mouse_steps == 0);
        }
    }

    // ---- (5) implicit grab: press (consumed), move far outside, release -> all three reach it ----
    {
        world_t world;
        world.button.consume = true;
        input_router_t router{ world.root };

        interaction_t press;
        press.mouse = point_t{ 50, 70 };
        press.mouse_down = true;
        press.mouse_pressed = true;
        clear_log();
        CHECK(router.dispatch(press));
        CHECK(router.mouse_target() == &world.button);
        CHECK(world.button.presses == 1);

        // Far outside every widget: the grabbed button still owns the pointer.
        clear_log();
        interaction_t drag = move_to(point_t{ 350, 250 });
        drag.mouse_down = true;
        CHECK(router.dispatch(drag));
        CHECK(log().size() == 1);                              // the button only: no panel, no root
        CHECK(log()[0].widget == &world.button);
        CHECK(log()[0].in.mouse == point_t{ 310, 190 });       // (350,250) - (40,60)
        CHECK(!log()[0].in.hovered);                           // the pointer left its rectangle
        CHECK(log()[0].in.mouse_down);
        CHECK(router.mouse_target() == &world.button);         // still grabbed until the release

        // The release reaches the button too, and the grab ends after it.
        clear_log();
        interaction_t release = move_to(point_t{ 350, 250 });
        release.mouse_released = true;
        CHECK(router.dispatch(release));
        CHECK(log().size() == 1);
        CHECK(log()[0].widget == &world.button);
        CHECK(log()[0].in.mouse_released);
        CHECK(router.mouse_target() == nullptr);

        // A release that the grabbed widget does NOT consume still ends the grab (and bubbles up).
        CHECK(router.dispatch(press));
        CHECK(router.mouse_target() == &world.button);
        world.button.consume = false;
        world.panel.consume = true;
        clear_log();
        CHECK(router.dispatch(release));
        CHECK(log()[0].widget == &world.button);   // the grab routes there first ...
        CHECK(log()[1].widget == &world.panel);    // ... and the unhandled step bubbles
        CHECK(router.mouse_target() == nullptr);
    }

    // ---- (6) focus: depth-first chain, wrap, Shift+Tab, exactly-once hooks ----
    {
        probe_widget_t root;
        root.set_geometry(rect_t{ 0, 0, 400, 300 });
        probe_widget_t panel{ &root };
        probe_widget_t b1{ &panel };
        probe_widget_t b2{ &panel };
        probe_widget_t b3{ &panel };
        probe_widget_t hidden_box{ &root };
        probe_widget_t hidden_child{ &hidden_box };
        probe_widget_t disabled_box{ &root };
        probe_widget_t nested{ &root };
        probe_widget_t n1{ &nested };

        b1.set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        b2.set_focus_policy(focus_policy_enum::CLICK_FOCUS);
        b3.set_focus_policy(focus_policy_enum::TAB_FOCUS);
        hidden_box.set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        hidden_child.set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        disabled_box.set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        n1.set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        hidden_box.set_visible(false);
        disabled_box.set_enabled(false);

        // The policy / focusability matrix first.
        CHECK(root.focus_policy() == focus_policy_enum::NO_FOCUS);   // nothing focusable by accident
        CHECK(!root.is_focusable());
        CHECK(b1.is_focusable());
        CHECK(!hidden_box.is_focusable());
        CHECK(!hidden_child.is_focusable());                      // under a hidden parent
        CHECK(!disabled_box.is_focusable());

        input_router_t router{ root };
        CHECK(router.focus_item() == nullptr);

        // Tab enters the chain at its first widget.
        CHECK(router.dispatch(tab(false)));
        CHECK(router.focus_item() == &b1);
        CHECK(b1.has_focus());
        CHECK(b1.focus_ins == 1);
        CHECK(b1.focus_outs == 0);

        CHECK(router.dispatch(tab(false)));
        CHECK(router.focus_item() == &b2);                       // click_focus is tab-reachable
        CHECK(b1.focus_outs == 1);
        CHECK(!b1.has_focus());
        CHECK(b2.focus_ins == 1);

        CHECK(router.dispatch(tab(false)));
        CHECK(router.focus_item() == &b3);
        CHECK(router.dispatch(tab(false)));
        CHECK(router.focus_item() == &n1);                       // hidden/disabled subtrees skipped
        CHECK(router.dispatch(tab(false)));
        CHECK(router.focus_item() == &b1);                       // wraps around
        CHECK(b1.focus_ins == 2);
        CHECK(b1.focus_outs == 1);
        CHECK(b1.has_focus());

        // Shift+Tab goes backwards, wrapping too.
        CHECK(router.dispatch(tab(true)));
        CHECK(router.focus_item() == &n1);
        CHECK(b1.focus_outs == 2);
        CHECK(n1.focus_ins == 2);
        CHECK(router.dispatch(tab(true)));
        CHECK(router.focus_item() == &b3);
        CHECK(router.dispatch(tab(true)));
        CHECK(router.focus_item() == &b2);

        // Exactly one focus_in per focus_out (plus the one widget that still holds it).
        const probe_widget_t* const all[] = { &root, &panel, &b1, &b2, &b3, &hidden_box,
                                              &hidden_child, &disabled_box, &nested, &n1 };
        int ins = 0;
        int outs = 0;
        for (const probe_widget_t* widget : all) {
            ins += widget->focus_ins;
            outs += widget->focus_outs;
            CHECK(widget->has_focus() == (router.focus_item() == widget));
        }
        CHECK(ins == outs + 1);
        CHECK(root.focus_ins == 0 && panel.focus_ins == 0);
        CHECK(hidden_box.focus_ins == 0 && hidden_child.focus_ins == 0);
        CHECK(disabled_box.focus_ins == 0);

        // A key step goes to the focused widget FIRST, Tab included: a control that consumes keys
        // keeps Tab (the router's Tab handling is only the fallback). The probes have already seen
        // the earlier Tabs, so compare the DELTA.
        b1.consume = true;
        router.set_focus(&b1);
        const int b1_keys_before = b1.key_steps;
        clear_log();
        CHECK(router.dispatch(tab(false)));
        CHECK(router.focus_item() == &b1);            // the control consumed the step
        CHECK(log().size() == 1);
        CHECK(log()[0].widget == &b1);
        CHECK(log()[0].in.key == interaction_t::tab_key);
        CHECK(b1.key_steps == b1_keys_before + 1);
        b1.consume = false;

        // With a non-consuming focus the router still moves: Tab is its fallback.
        router.set_focus(&b1);
        const int b1_keys_fallback = b1.key_steps;
        clear_log();
        CHECK(router.dispatch(tab(false)));
        CHECK(router.focus_item() == &b2);
        CHECK(log().size() == 1);                       // only the focused widget was asked
        CHECK(b1.key_steps == b1_keys_fallback + 1);

        // A focus that stops being focusable is dropped on the next dispatch (one focus_out).
        router.set_focus(&b2);
        const int outs_before = b2.focus_outs;
        b2.set_visible(false);
        clear_log();
        router.dispatch(move_to(point_t{ 1, 1 }));
        CHECK(router.focus_item() == nullptr);
        CHECK(b2.focus_outs == outs_before + 1);
        CHECK(!b2.has_focus());
    }

    // ---- (7) keys: nobody without focus, exactly one widget with focus ----
    {
        world_t world;
        input_router_t router{ world.root };

        interaction_t key;
        key.key = 65;   // 'A'
        key.key_pressed = true;
        clear_log();
        CHECK(!router.dispatch(key));            // no focus: the key is delivered to nobody
        CHECK(log().empty());

        world.button.set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        router.set_focus(&world.button);
        clear_log();
        CHECK(!router.dispatch(key));            // the button does not consume keys ...
        CHECK(log().size() == 1);                // ... but exactly one widget SAW it
        CHECK(log()[0].widget == &world.button);
        CHECK(log()[0].in.key == 65);
        CHECK(log()[0].in.key_pressed);
        // The key is not positional, but the payload's mouse position is still mapped into the target
        // (button global origin (40,60); the snapshot's default (0,0) becomes (-40,-60)).
        CHECK(log()[0].in.mouse == point_t{ -40, -60 });
        CHECK(world.panel.key_steps == 0);             // keys do not bubble
        CHECK(world.root.key_steps == 0);

        interaction_t up;
        up.key = 65;
        up.key_released = true;
        clear_log();
        router.dispatch(up);
        CHECK(log().size() == 1);
        CHECK(log()[0].widget == &world.button);
        CHECK(log()[0].in.key_released);
        CHECK(!log()[0].in.key_pressed);
        CHECK(world.button.last_key == 65);
    }

    // ---- (8) item targets: a non-widget item is hit, consumes, grabs, and is a focus station ----
    {
        world_t world;
        input_router_t router{ world.root };
        // Root-local geometry {250, 200, 100, 80}: outside panel/button, and added last, so the
        // reverse hit test asks it first.
        item_probe_t item{ &world.root, rect_t{ 250, 200, 100, 80 } };
        CHECK(item.geometry() == rect_t{ 250, 200, 100, 80 });

        interaction_t press = move_to(point_t{ 260, 210 });
        press.mouse_down = true;
        press.mouse_pressed = true;
        clear_log();
        CHECK(router.dispatch(press));
        CHECK(item.steps == 1);
        CHECK(log().empty());                              // the widget chain saw nothing
        CHECK(world.button.mouse_steps == 0);              // ... confirmed by the widget counters
        CHECK(router.mouse_target() == &item);             // the grab target is an item now

        // The release is delivered to the grabbed item even outside its rectangle.
        interaction_t release = move_to(point_t{ 390, 290 });
        release.mouse_released = true;
        clear_log();
        CHECK(router.dispatch(release));
        CHECK(item.steps == 2);
        CHECK(log().empty());
        CHECK(world.root.mouse_steps == 0);
        CHECK(router.mouse_target() == nullptr);           // the release ended the grab

        // The press state and the capture are ITEM state: a second output reads them from the item.
        CHECK(router.dispatch(press));
        CHECK(item.is_pressed(mouse_button_enum::LEFT));
        CHECK(!item.is_pressed(mouse_button_enum::RIGHT));
        CHECK(item.captures_pointer());
        CHECK(router.mouse_target() == &item);

        // While the item owns the pointer, a press over ANOTHER item is delivered to the owner: the
        // capture wins over the hit test, and consumption is irrelevant here.
        const int steps_after_press = item.steps;
        clear_log();
        CHECK(router.dispatch(press_at(point_t{ 50, 70 })));   // inside world.button
        CHECK(item.steps == steps_after_press + 1);
        CHECK(log().empty());                                  // the widget under the pointer saw nothing
        CHECK(world.button.presses == 0);
        CHECK(!world.button.is_pressed(mouse_button_enum::LEFT));
        CHECK(!world.button.captures_pointer());
        CHECK(router.mouse_target() == &item);

        // The release ends the grab and clears the press state on the item.
        (void)router.dispatch(release);
        CHECK(!item.is_pressed(mouse_button_enum::LEFT));
        CHECK(!item.captures_pointer());
        CHECK(router.mouse_target() == nullptr);

        // Focus is an ITEM property: this non-widget item can itself be a focus station.
        item.set_focus_policy(focus_policy_enum::TAB_FOCUS);
        item.set_focus();                                  // item_t::set_focus walks the ITEM chain
        CHECK(router.focus_item() == &item);
        CHECK(item.has_focus());
        clear_log();
        const int steps_before_tab = item.steps;
        CHECK(router.dispatch(tab(false)));                // the item consumes every step, Tab too
        CHECK(item.steps == steps_before_tab + 1);
        CHECK(router.focus_item() == &item);               // the focus stayed on the item
        item.set_focus_policy(focus_policy_enum::NO_FOCUS);
        CHECK(!item.has_focus());                          // lowering the policy clears the focus
        CHECK(router.focus_item() == nullptr);
    }

    // ---- (9) item_t::set_focus() (inherited by widget_t): the host is the authority; no host = no-op ----
    {
        world_t world;
        input_router_t router{ world.root };

        world.button.set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        world.button.set_focus();                     // walks up to the root's focus sink
        CHECK(router.focus_item() == &world.button);
        CHECK(world.button.has_focus());
        CHECK(world.button.focus_ins == 1);
        CHECK(world.button.focus_outs == 0);

        world.button.clear_focus();
        CHECK(router.focus_item() == nullptr);
        CHECK(!world.button.has_focus());
        CHECK(world.button.focus_outs == 1);

        // A widget that cannot hold focus cannot be focused: the call clears instead.
        world.panel.set_focus();                      // no_focus by default
        CHECK(router.focus_item() == nullptr);

        // Lowering the policy of the focused widget clears the focus (one focus_out).
        world.button.set_focus();
        CHECK(world.button.has_focus());
        world.button.set_focus_policy(focus_policy_enum::NO_FOCUS);
        CHECK(router.focus_item() == nullptr);
        CHECK(!world.button.has_focus());
        CHECK(world.button.focus_outs == 2);
    }
    {
        // No host attached: set_focus()/clear_focus() are documented no-ops, and nothing crashes.
        probe_widget_t orphan;
        orphan.set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        CHECK(orphan.is_focusable());
        orphan.set_focus();
        CHECK(!orphan.has_focus());
        CHECK(orphan.focus_ins == 0);
        orphan.clear_focus();
        CHECK(!orphan.has_focus());
        CHECK(orphan.focus_outs == 0);
    }

    // ---- (10) click focus: a press edge focuses the nearest click-focusable item ----
    {
        probe_widget_t root;
        root.set_geometry(rect_t{ 0, 0, 400, 300 });
        probe_widget_t panel{ &root };
        panel.set_geometry(rect_t{ 0, 0, 400, 100 });
        probe_widget_t strong{ &panel };
        strong.set_geometry(rect_t{ 10, 10, 80, 30 });
        strong.set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        probe_widget_t click_only{ &panel };
        click_only.set_geometry(rect_t{ 100, 10, 80, 30 });
        click_only.set_focus_policy(focus_policy_enum::CLICK_FOCUS);
        probe_widget_t tab_only{ &panel };
        tab_only.set_geometry(rect_t{ 190, 10, 80, 30 });
        tab_only.set_focus_policy(focus_policy_enum::TAB_FOCUS);
        probe_widget_t passive{ &panel };
        passive.set_geometry(rect_t{ 10, 50, 80, 30 });   // no_focus by default
        probe_widget_t disabled{ &panel };
        disabled.set_geometry(rect_t{ 100, 50, 80, 30 });
        disabled.set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        disabled.set_enabled(false);

        // A click-focusable container whose child must NOT become the focus item: the walk goes UP.
        probe_widget_t container{ &root };
        container.set_geometry(rect_t{ 0, 120, 200, 100 });
        container.set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        probe_widget_t child{ &container };
        child.set_geometry(rect_t{ 10, 10, 80, 30 });     // no_focus

        input_router_t router{ root };
        CHECK(root.focus_policy() == focus_policy_enum::NO_FOCUS);
        CHECK(router.focus_item() == nullptr);

        // The press focuses the STRONG_FOCUS probe before its own interact() (press_focused) and is
        // still delivered as an unconsumed step: focus is not consumption.
        CHECK(!router.dispatch(press_at(point_t{ 50, 25 })));
        CHECK(router.focus_item() == &strong);
        CHECK(strong.has_focus());
        CHECK(strong.focus_ins == 1);
        CHECK(strong.focus_outs == 0);
        CHECK(strong.press_focused);
        // The press TOOK the pointer (the implicit grab does not depend on consumption); the release
        // gives it back, so each later press is hit-tested again.
        CHECK(router.mouse_target() == &strong);
        CHECK(strong.captures_pointer());
        CHECK(strong.is_pressed(mouse_button_enum::LEFT));
        CHECK(!router.dispatch(release_at(point_t{ 50, 25 })));
        CHECK(router.mouse_target() == nullptr);
        CHECK(!strong.captures_pointer());
        CHECK(!strong.is_pressed(mouse_button_enum::LEFT));

        // A second press on the same item is not a real focus change: no second focus_in.
        CHECK(!router.dispatch(press_at(point_t{ 50, 25 })));
        CHECK(router.focus_item() == &strong);
        CHECK(strong.focus_ins == 1);
        CHECK(strong.focus_outs == 0);
        (void)router.dispatch(release_at(point_t{ 50, 25 }));

        // CLICK_FOCUS moves the focus too; the old item receives exactly one focus_out.
        CHECK(!router.dispatch(press_at(point_t{ 140, 25 })));
        CHECK(router.focus_item() == &click_only);
        CHECK(!strong.has_focus());
        CHECK(strong.focus_outs == 1);
        CHECK(click_only.has_focus());
        CHECK(click_only.focus_ins == 1);
        CHECK(click_only.press_focused);
        (void)router.dispatch(release_at(point_t{ 140, 25 }));

        // TAB_FOCUS is Tab-only: a press does not move the focus to it.
        CHECK(!router.dispatch(press_at(point_t{ 230, 25 })));
        CHECK(router.focus_item() == &click_only);
        CHECK(tab_only.focus_ins == 0);
        (void)router.dispatch(release_at(point_t{ 230, 25 }));

        // NO_FOCUS (a label-like passive item) and a DISABLED focusable item change nothing.
        CHECK(!router.dispatch(press_at(point_t{ 50, 65 })));
        CHECK(router.focus_item() == &click_only);
        CHECK(passive.focus_ins == 0);
        (void)router.dispatch(release_at(point_t{ 50, 65 }));
        CHECK(!router.dispatch(press_at(point_t{ 140, 65 })));
        CHECK(router.focus_item() == &click_only);
        CHECK(disabled.focus_ins == 0);
        (void)router.dispatch(release_at(point_t{ 140, 65 }));

        // Blank space inside the panel, and a press outside the whole tree, leave it untouched.
        CHECK(!router.dispatch(press_at(point_t{ 350, 80 })));
        CHECK(router.focus_item() == &click_only);
        (void)router.dispatch(release_at(point_t{ 350, 80 }));
        CHECK(!router.dispatch(press_at(point_t{ 900, 900 })));
        CHECK(router.focus_item() == &click_only);
        (void)router.dispatch(release_at(point_t{ 900, 900 }));

        // The walk follows the ITEM parent chain: the hit is the no_focus child, the focus goes to
        // its click-focusable container, and the container sees it in its own interact().
        CHECK(!router.dispatch(press_at(point_t{ 20, 140 })));
        CHECK(router.focus_item() == &container);
        CHECK(container.has_focus());
        CHECK(container.focus_ins == 1);
        CHECK(container.press_focused);
        CHECK(child.focus_ins == 0);
        CHECK(!child.has_focus());
        (void)router.dispatch(release_at(point_t{ 20, 140 }));

        // Only the PRESS edge focuses: a move or a release over another click-focusable item does
        // not move the focus.
        router.set_focus(nullptr);
        CHECK(!router.dispatch(move_to(point_t{ 50, 25 })));
        CHECK(router.focus_item() == nullptr);
        CHECK(!router.dispatch(release_at(point_t{ 140, 25 })));
        CHECK(router.focus_item() == nullptr);

        // The two focus mechanisms compose: Tab continues from the item a click focused.
        CHECK(!router.dispatch(press_at(point_t{ 50, 25 })));
        CHECK(router.focus_item() == &strong);
        CHECK(router.dispatch(tab(false)));
        CHECK(router.focus_item() == &click_only);
    }

    // ---- (10b) a topmost click-focusable item: its own sink is the router, so a press on the root
    //      itself still focuses it (the host case only works because the host skips its own sink) ----
    {
        probe_widget_t click_root;
        click_root.set_geometry(rect_t{ 0, 0, 100, 100 });
        click_root.set_focus_policy(focus_policy_enum::CLICK_FOCUS);
        input_router_t router{ click_root };
        CHECK(!router.dispatch(press_at(point_t{ 5, 5 })));
        CHECK(router.focus_item() == &click_root);
        CHECK(click_root.has_focus());
        CHECK(click_root.focus_ins == 1);
        CHECK(click_root.press_focused);
    }

    // ---- the router's own choices: the synthetic hover-out and guarded pointers ----
    {
        world_t world;
        input_router_t router{ world.root };

        // Enter the button: it is hovered.
        router.dispatch(move_to(point_t{ 50, 70 }));
        CHECK(world.button.hovered_steps == 1);

        // Move to the panel: the button gets ONE synthetic interaction with hovered == false and no
        // edges, BEFORE the panel sees the step; the return value is still false (nothing consumed).
        clear_log();
        CHECK(!router.dispatch(move_to(point_t{ 15, 25 })));
        CHECK(log().size() == 3);
        CHECK(log()[0].widget == &world.button);
        CHECK(!log()[0].in.hovered);
        CHECK(!log()[0].in.mouse_down);
        CHECK(!log()[0].in.mouse_pressed);
        CHECK(!log()[0].in.mouse_released);
        CHECK(log()[0].in.mouse == point_t{ -25, -35 });   // (15,25) - (40,60)
        CHECK(log()[1].widget == &world.panel);
        CHECK(log()[1].in.hovered);
        CHECK(log()[2].widget == &world.root);
        CHECK(world.button.unhovered_steps == 1);

        // Leaving the root clears the hover of the last hovered widget as well.
        clear_log();
        CHECK(!router.dispatch(move_to(point_t{ 900, 900 })));
        CHECK(log().size() == 1);
        CHECK(log()[0].widget == &world.panel);
        CHECK(!log()[0].in.hovered);

        // Guarded pointers: deleting the focused/grabbed widget turns them null, not dangling.
        auto* const doomed = new probe_widget_t{ &world.root };
        doomed->set_geometry(rect_t{ 200, 200, 50, 50 });
        doomed->set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        router.set_focus(doomed);
        CHECK(router.focus_item() == doomed);
        delete doomed;
        CHECK(router.focus_item() == nullptr);
    }

    if (g_failures == 0) {
        std::printf("router: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("router: %d checks, %d failure(s)\n", g_checks, g_failures);
    return 1;
}
