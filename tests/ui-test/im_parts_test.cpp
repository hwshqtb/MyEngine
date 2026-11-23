// The immediate host's parts after the split:
//
//  1. ui::im_object_pool_t — take/reuse by id, the "keep the first type" job left to the caller,
//     the pass clock the CALLER supplies, the idle boundary, clear(), and the traversal the
//     focus/capture queries use;
//  2. ui::group_stack_t — the procedural cursor (layout_t's dual): row advance, nested groups, the
//     name prefix, and reset();
//  3. ui::im_pass_t — one content-production pass: the batch with each claim in its own entry, the
//     last pointer LEVEL, the previous-pass visit order the Tab ring reads, and the acquisition
//     (write-back) log;
//  4. ui::im_host_t — focus and capture are QUERIES over the controls' own item state, not a second
//     store; the host integration (click -> focus, Tab, host focus loss, capture) did not change;
//  5. collect() deletes a focused/captured control and the query answers null afterwards; a push
//     button has no linked state, so a later control() gets default configuration again.
//
// Everything is offscreen (image_t + painter_t); no window and no video subsystem are created.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/object_pointer_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/group_stack_t.h"
#include "hwshqtb/ui/im_host_t.h"
#include "hwshqtb/ui/im_object_pool_t.h"
#include "hwshqtb/ui/im_pass_t.h"
#include "hwshqtb/ui/input_router_t.h"
#include "hwshqtb/ui/push_button_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;
using namespace hwshqtb::ui;

namespace im_parts_test {

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

/// The reflected type name of push_button_t (also keeps its registration linked).
const std::string_view button_type = push_button_t::static_meta_object.class_name();

interaction_t press_in(point_t local) {
    interaction_t in;
    in.mouse = local;
    in.mouse_down = true;
    in.mouse_pressed = true;
    in.hovered = true;
    return in;
}

interaction_t release_in(point_t local) {
    interaction_t in;
    in.mouse = local;
    in.mouse_released = true;
    in.hovered = true;
    return in;
}

interaction_t tab_press(bool shift = false) {
    interaction_t in;
    in.key = interaction_t::tab_key;
    in.key_pressed = true;
    in.modifiers = shift ? modifier_bits(modifier_flag::SHIFT) : 0u;
    return in;
}

/// @brief A root -> immediate host world with four controls, two focusable and placed in rows.
struct host_env_t {
    image_t image{ 240, 160 };
    painter_t painter{ image };
    widget_t root;
    im_host_t host;
    input_router_t router;
    push_button_t* a = nullptr;
    push_button_t* b = nullptr;
    push_button_t* c = nullptr;
    push_button_t* d = nullptr;

    host_env_t():
        router(root) {
        painter.begin();
        root.set_geometry(rect_t{ 0, 0, 240, 160 });
        host.set_parent(&root);
        host.set_geometry(rect_t{ 0, 0, 240, 160 });
        host.set_focus_policy(focus_policy_enum::TAB_FOCUS);
        host.set_frame_handler([](im_host_t& driven) {
            (void)driven.control(button_type, "a");
            (void)driven.control(button_type, "b");
            (void)driven.control(button_type, "c");
            (void)driven.control(button_type, "d");
        });
        a = static_cast<push_button_t*>(host.widget(button_type, "a"));
        b = static_cast<push_button_t*>(host.widget(button_type, "b"));
        c = static_cast<push_button_t*>(host.widget(button_type, "c"));
        d = static_cast<push_button_t*>(host.widget(button_type, "d"));
        // The chain is [a, b]: c opts out and d is disabled (a focusable policy is not enough).
        a->set_focus_policy(focus_policy_enum::CLICK_FOCUS);
        b->set_focus_policy(focus_policy_enum::TAB_FOCUS);
        c->set_focus_policy(focus_policy_enum::NO_FOCUS);
        d->set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        d->set_enabled(false);
        // Rows: a {0,0,240,26}, b {0,30,...}, c {0,60,...}, d {0,90,...}.
        host.render(painter);
    }
};

}  // namespace im_parts_test

using namespace im_parts_test;

int main() {
    std::printf("== im_host_t parts (cache / group stack / pass / queries) ==\n");

    // -----------------------------------------------------------------------
    // (1) im_object_pool_t: take/reuse, id identity, an external clock and the idle boundary.
    // -----------------------------------------------------------------------
    {
        im_object_pool_t pool;
        CHECK(pool.size() == 0u);
        CHECK(pool.find("alpha") == nullptr);

        int factory_calls = 0;
        object_t* const alpha =
            pool.take("alpha", 0, [&factory_calls]() -> object_t* {
                ++factory_calls;
                return new object_t;
            });
        CHECK(alpha != nullptr);
        CHECK(factory_calls == 1);
        CHECK(pool.size() == 1u);
        CHECK(pool.find("alpha") == alpha);

        // The same id reuses the registered object: the factory is not called again, and take()
        // records the pass the caller supplied (here pass 1).
        object_t* const alpha_again =
            pool.take("alpha", 1, [&factory_calls]() -> object_t* {
                ++factory_calls;
                return new object_t;
            });
        CHECK(alpha_again == alpha);
        CHECK(factory_calls == 1);

        // A factory that returns nullptr registers nothing.
        CHECK(pool.take("failed", 1, []() -> object_t* { return nullptr; }) == nullptr);
        CHECK(pool.find("failed") == nullptr);
        CHECK(pool.size() == 1u);

        // The clock is the caller's: take() stores the given pass, not an internal counter.
        object_t* const beta = pool.take("beta", 100, []() { return new object_t; });
        // alpha was last taken in pass 1: at pass 104 it is idle with a limit of 5.
        const std::vector<object_t*> first_sweep = pool.collect(104, 5);
        CHECK(first_sweep.size() == 1u);
        CHECK(!first_sweep.empty() && first_sweep.front() == alpha);
        CHECK(pool.find("alpha") == nullptr);
        CHECK(pool.find("beta") == beta);
        CHECK(pool.size() == 1u);

        // A repeated collect at the SAME pass keeps beta (distance 4). A pool counting its own
        // sweeps would already have moved past the limit here.
        CHECK(pool.collect(104, 5).empty());
        CHECK(pool.find("beta") == beta);

        // Distance 5 is the boundary: the row is unregistered and handed back, NOT deleted.
        object_pointer_t<object_t> beta_guard{ beta };
        const std::vector<object_t*> second_sweep = pool.collect(105, 5);
        CHECK(second_sweep.size() == 1u);
        CHECK(!second_sweep.empty() && second_sweep.front() == beta);
        CHECK(beta_guard.data() == beta); // the pool only unregisters
        CHECK(pool.size() == 0u);
        delete beta; // the caller decides the aftermath
        CHECK(beta_guard.data() == nullptr);

        // max_idle_passes == 0: keep only what was taken in the pass being collected.
        object_t* const gamma = pool.take("gamma", 7, []() { return new object_t; });
        CHECK(pool.collect(7, 0).empty());
        CHECK(pool.size() == 1u);
        CHECK(pool.collect(8, 0).size() == 1u);
        CHECK(pool.size() == 0u);
        delete gamma;

        // A row whose object died is dropped without being returned (nothing to hand back).
        object_t* const dead = pool.take("dead", 1, []() { return new object_t; });
        object_pointer_t<object_t> dead_guard{ dead };
        delete dead;
        CHECK(dead_guard.data() == nullptr);
        CHECK(pool.find("dead") == nullptr); // a dead row answers null
        CHECK(pool.size() == 1u);            // ... but stays listed until swept
        CHECK(pool.collect(1000, 1).empty());
        CHECK(pool.size() == 0u);

        // take() on a dead row builds and registers a new object under the same id.
        object_t* const phoenix = pool.take("phoenix", 1, []() { return new object_t; });
        delete phoenix;
        object_t* const reborn = pool.take("phoenix", 2, []() { return new object_t; });
        CHECK(reborn != nullptr);
        CHECK(pool.find("phoenix") == reborn);
        CHECK(pool.size() == 1u);
        pool.clear();
        delete reborn;

        // clear() unregisters but leaves the objects alive.
        object_t* const keep = pool.take("keep", 1, []() { return new object_t; });
        object_pointer_t<object_t> keep_guard{ keep };
        pool.clear();
        CHECK(pool.size() == 0u);
        CHECK(pool.find("keep") == nullptr);
        CHECK(keep_guard.data() == keep);
        delete keep;

        // for_each visits live rows only, with the id and the object identity.
        object_t* const one = pool.take("one", 1, []() { return new object_t; });
        object_t* const two = pool.take("two", 1, []() { return new object_t; });
        int visited = 0;
        bool saw_one = false;
        bool saw_two = false;
        pool.for_each([&](std::string_view id, object_t& object) {
            ++visited;
            saw_one = saw_one || (id == "one" && &object == one);
            saw_two = saw_two || (id == "two" && &object == two);
        });
        CHECK(visited == 2);
        CHECK(saw_one && saw_two);
        CHECK(pool.contains(*one));
        CHECK(pool.contains(*two));
        delete one;
        CHECK(pool.contains(*two)); // identity, not an id or a type: two is still the live one
        visited = 0;
        pool.for_each([&](std::string_view, object_t&) { ++visited; });
        CHECK(visited == 1); // the dead row is skipped
        delete two;
        CHECK(pool.size() == 2u); // dead rows remain until collected
    }

    // -----------------------------------------------------------------------
    // (2) group_stack_t: the procedural cursor, nested groups, prefixes and reset.
    // -----------------------------------------------------------------------
    {
        group_stack_t stack;
        CHECK(stack.depth() == 1u);
        stack.reset(240);
        CHECK(stack.depth() == 1u);
        CHECK(stack.layer().width == 240);
        CHECK(stack.layer().origin == point_t{});
        CHECK(stack.cursor() == point_t{});
        CHECK(stack.key_of("a") == "a");

        const rect_t first = stack.alloc_row(26);
        CHECK(first == rect_t{ 0, 0, 240, 26 });
        CHECK(stack.cursor() == point_t{ 0, 30 });
        const rect_t clamped = stack.alloc_row(0); // <= 0 becomes height 1
        CHECK(clamped.y() == 30);
        CHECK(clamped.height() == 1);
        CHECK(stack.cursor() == point_t{ 0, 35 });

        stack.set_cursor(point_t{ 5, 6 });
        CHECK(stack.cursor() == point_t{ 5, 6 });

        stack.begin_group(rect_t{ 10, 20, 100, 50 }, "g");
        CHECK(stack.depth() == 2u);
        CHECK(stack.layer().origin == point_t{ 10, 20 });
        CHECK(stack.layer().cursor == point_t{});
        CHECK(stack.layer().width == 100);
        CHECK(stack.layer().id_prefix == "g/");
        CHECK(stack.key_of("x") == "g/x");
        CHECK(stack.alloc_row(10) == rect_t{ 0, 0, 100, 10 });
        CHECK(stack.cursor() == point_t{ 0, 14 });

        stack.begin_group(rect_t{ 5, 6, 40, 20 }, "n");
        CHECK(stack.depth() == 3u);
        CHECK(stack.layer().origin == point_t{ 15, 26 }); // origins accumulate
        CHECK(stack.layer().width == 40);
        CHECK(stack.layer().id_prefix == "g/n/");
        CHECK(stack.key_of("x") == "g/n/x");

        CHECK(stack.end_group());
        CHECK(stack.depth() == 2u);
        CHECK(stack.cursor() == point_t{ 0, 14 }); // the outer group's cursor survived the nesting
        CHECK(stack.layer().id_prefix == "g/");

        CHECK(stack.end_group());
        CHECK(stack.depth() == 1u);
        CHECK(stack.cursor() == point_t{ 5, 6 }); // the root cursor was never touched by group rows
        CHECK(!stack.end_group());                // the root cannot be popped
        CHECK(stack.depth() == 1u);

        stack.set_root_width(320);
        CHECK(stack.layer().width == 320);
        stack.begin_group(rect_t{ 0, 0, -10, 10 }, "neg");
        CHECK(stack.layer().width == 0); // a negative width is clamped, never stored
        stack.set_root_width(999);       // ignored while a group is open
        CHECK(stack.layer().width == 0);
        CHECK(stack.end_group());
        stack.set_root_width(100);
        CHECK(stack.layer().width == 100);

        stack.reset(80);
        CHECK(stack.depth() == 1u);
        CHECK(stack.layer().width == 80);
        CHECK(stack.key_of("y") == "y");
    }

    // -----------------------------------------------------------------------
    // (3) im_pass_t: the batch and its claims, the pointer snapshot, the visit order, begin/end.
    // -----------------------------------------------------------------------
    {
        im_pass_t pass;
        CHECK(!pass.active());
        pass.begin(240);
        CHECK(pass.active());
        CHECK(pass.input_count() == 0u);
        CHECK(pass.groups().layer().width == 240);
        CHECK(pass.previous().empty());

        interaction_t first;
        first.mouse = point_t{ 1, 2 };
        first.mouse_down = true;
        first.modifiers = modifier_bits(modifier_flag::SHIFT);
        interaction_t second;
        second.mouse = point_t{ 3, 4 };
        second.mouse_down = false;

        pass.push(first);
        pass.push(second);
        CHECK(pass.input_count() == 2u);
        CHECK(!pass.input(0).claimed);
        CHECK(!pass.input(1).claimed);
        pass.input(1).claimed = true;

        // The claim travels IN the entry: erasing the first step must leave the second step's claim
        // attached to the second step's interaction (two parallel arrays drift here).
        pass.erase_input(0);
        CHECK(pass.input_count() == 1u);
        CHECK(pass.input(0).input.mouse == point_t{ 3, 4 });
        CHECK(pass.input(0).claimed);

        // begin() clears the claims of the steps that are about to be replayed, never the steps.
        pass.begin(240);
        CHECK(pass.input_count() == 1u);
        CHECK(!pass.input(0).claimed);
        CHECK(pass.input(0).input.mouse == point_t{ 3, 4 });

        // The snapshot takes the pointer LEVELS; a key step does not advance it (the host's rule).
        pass.record_pointer(first);
        CHECK(pass.last_mouse() == point_t{ 1, 2 });
        CHECK(pass.last_mouse_down());
        CHECK(pass.last_modifiers() == modifier_bits(modifier_flag::SHIFT));
        interaction_t key;
        key.key = 65;
        key.key_pressed = true;
        key.mouse = point_t{ 9, 9 };
        key.mouse_down = false;
        key.modifiers = 0;
        pass.push(key);
        CHECK(pass.last_mouse() == point_t{ 1, 2 });
        CHECK(pass.last_mouse_down());
        CHECK(pass.input_count() == 2u);

        // The visit order of the open pass becomes previous() only at end().
        widget_t one;
        widget_t two;
        pass.visit(one);
        pass.visit(two);

        // The acquisition (write-back) log records each id once, with the acquired object.
        CHECK(pass.accesses().empty());
        CHECK(pass.note_access("one", one));
        CHECK(pass.note_access("two", two));
        CHECK(!pass.note_access("one", one)); // a second acquisition of the id is not a new entry
        CHECK(pass.accesses().size() == 2u);
        CHECK(pass.accesses()[0].id == "one");
        CHECK(pass.accesses()[0].control.data() == &one);
        CHECK(pass.accesses()[1].id == "two");
        CHECK(pass.accesses()[1].control.data() == &two);

        CHECK(pass.previous().empty());
        pass.end();
        CHECK(!pass.active());
        CHECK(pass.input_count() == 0u); // end() consumes the batch
        CHECK(pass.accesses().empty());  // ... and the host has written the state back by now
        CHECK(pass.previous().size() == 2u);
        CHECK(pass.previous()[0].data() == &one);
        CHECK(pass.previous()[1].data() == &two);

        pass.begin(300);
        CHECK(pass.previous().size() == 2u); // the Tab ring survives begin()
        CHECK(pass.accesses().empty());      // the acquisition log is per pass
        CHECK(pass.groups().layer().width == 300);
        pass.end();
        CHECK(pass.previous().empty()); // ... and a pass that visits nothing empties it
        CHECK(pass.groups().layer().width == 300);
    }

    // -----------------------------------------------------------------------
    // (4) Host integration: click -> inner focus, Tab moves it without wrapping, losing the host's
    //     focus clears it, and capture is the controls' own item state.
    // -----------------------------------------------------------------------
    {
        host_env_t env;
        im_host_t& host = env.host;
        CHECK(env.a != nullptr && env.b != nullptr && env.c != nullptr && env.d != nullptr);
        CHECK(host.pass() == 1u);
        CHECK(host.inner_focus() == nullptr);
        CHECK(host.captured_control() == nullptr);

        // Click a: the press moves the inner focus before a's own interact(), and a consumed press
        // takes the capture; the release gives it back.
        CHECK(host.interact(press_in(point_t{ 10, 13 })));
        CHECK(host.interact(release_in(point_t{ 10, 13 })));
        host.render(env.painter);
        CHECK(env.a->has_focus());
        CHECK(host.inner_focus() == env.a);
        CHECK(!env.b->has_focus());
        CHECK(host.captured_control() == nullptr);
        CHECK(!env.a->captures_pointer());

        // Host-driven capture: press (no release yet) makes a the owner the query reports.
        CHECK(host.interact(press_in(point_t{ 10, 13 })));
        host.render(env.painter);
        CHECK(env.a->captures_pointer());
        CHECK(host.captured_control() == env.a);
        CHECK(host.interact(release_in(point_t{ 10, 13 })));
        host.render(env.painter);
        CHECK(!env.a->captures_pointer());
        CHECK(host.captured_control() == nullptr);

        // Tab: a -> b (c is NO_FOCUS and never joins the chain); no wrapping at either end.
        CHECK(host.interact(tab_press()));
        host.render(env.painter);
        CHECK(env.b->has_focus());
        CHECK(!env.a->has_focus());
        CHECK(host.inner_focus() == env.b);
        CHECK(!host.interact(tab_press())); // b is the last: the host refuses the key
        CHECK(host.interact(tab_press(true)));
        host.render(env.painter);
        CHECK(env.a->has_focus());
        CHECK(host.inner_focus() == env.a);
        CHECK(!host.interact(tab_press(true))); // a is the first

        // Losing the HOST's item-level focus clears the whole inner level.
        env.router.set_focus(&host);
        CHECK(host.has_focus());
        env.router.set_focus(nullptr);
        CHECK(!host.has_focus());
        CHECK(!env.a->has_focus());
        CHECK(host.inner_focus() == nullptr);

        // The query reads the CONTROL's state, not a host copy: an item-level focus written by the
        // router itself (which never goes through the host's sink) is what inner_focus() reports.
        env.router.set_focus(env.a);
        CHECK(env.a->has_focus());
        CHECK(host.inner_focus() == env.a); // a stored member would still be null here
        env.router.set_focus(nullptr);
        CHECK(host.inner_focus() == nullptr);

        // The same rule for capture: the control's flag is the truth, the host stores no target.
        CHECK(env.a->capture_pointer());
        CHECK(env.a->captures_pointer());
        CHECK(host.captured_control() == env.a);
        env.a->release_pointer();
        CHECK(!env.a->captures_pointer());
        CHECK(host.captured_control() == nullptr);

        // One owner at a time: a new capture by another control supersedes the old one.
        CHECK(env.a->capture_pointer());
        CHECK(env.b->capture_pointer());
        CHECK(!env.a->captures_pointer());
        CHECK(env.b->captures_pointer());
        CHECK(host.captured_control() == env.b);
        env.b->release_pointer();
        CHECK(host.captured_control() == nullptr);

        // The idle pass keeps the controls and the queries stable.
        host.render(env.painter);
        CHECK(host.pass() == 7u);
        CHECK(host.widget(button_type, "a") == env.a);
        CHECK(host.inner_focus() == nullptr);
        CHECK(host.captured_control() == nullptr);
    }

    // -----------------------------------------------------------------------
    // (5) collect(): a focused/captured control that is not visited is deleted; both queries answer
    //     null afterwards, and the surviving control is untouched.
    // -----------------------------------------------------------------------
    {
        host_env_t env;
        im_host_t& host = env.host;
        CHECK(env.a != nullptr && env.b != nullptr);

        CHECK(host.interact(press_in(point_t{ 10, 13 })));
        host.render(env.painter); // a is visited, focused and captured
        CHECK(env.a->has_focus());
        CHECK(env.a->captures_pointer());
        CHECK(host.inner_focus() == env.a);
        CHECK(host.captured_control() == env.a);

        // Stop visiting a; b (and only b) keeps the host alive.
        env.a->set_text("remembered"); // configuration: a push button has no state to carry
        host.set_frame_handler([](im_host_t& driven) {
            (void)driven.control(button_type, "b");
        });
        host.render(env.painter); // a is not focusable-checked here: it is still visible/enabled
        CHECK(host.inner_focus() == env.a); // still the control's own state before the sweep
        CHECK(host.captured_control() == env.a);

        object_pointer_t<object_t> a_guard{ env.a };
        host.collect(0); // 0 = "not visited in this pass"
        CHECK(a_guard.data() == nullptr); // the host deleted it
        CHECK(host.inner_focus() == nullptr); // ... and the query no longer finds a focus
        CHECK(host.captured_control() == nullptr);
        CHECK(!env.b->has_focus());
        CHECK(host.children().size() == 1u); // b survived; the unvisited a, c and d are gone

        // A later use of the same name creates a fresh control; a push button has no linked state, so
        // its label comes back as the default, not as the configuration written above.
        host.set_frame_handler([](im_host_t& driven) {
            (void)driven.control(button_type, "a");
            (void)driven.control(button_type, "b");
        });
        host.render(env.painter);
        push_button_t* const fresh = static_cast<push_button_t*>(host.widget(button_type, "a"));
        CHECK(fresh != nullptr);
        CHECK(fresh->object_name() == "a");
        CHECK(fresh->text() == "Button");
        CHECK(host.inner_focus() == nullptr);
        CHECK(host.captured_control() == nullptr);
    }

    if (g_failures == 0) {
        std::printf("im_parts: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("im_parts: %d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
}
