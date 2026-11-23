// J1: ui::im_host_t, the immediate host.
//
// The acceptance criteria as executable checks:
//  1. passes drive ONE object per name (same address) and its state survives between passes;
//  2. a press pass does not activate; a release-inside pass activates exactly once; the next pass is
//     false again (the per-frame result is cleared by the settle snapshot);
//  3. releasing outside cancels (no activation, no clicked());
//  4. THE FOLDING COUNTEREXAMPLE: one batch with DOWN outside + UP inside must NOT activate (the old
//     folded snapshot WOULD activate; the case prints both results, which is the decidable boundary);
//  5. press inside -> drag out -> drag back -> release inside activates (the grab survives leaving);
//  6. empty name / unregistered type / non-widget type: invalid any_t plus exactly one Warning;
//  7. collect() deletes an unvisited control and the next control() creates a fresh object; a push
//     button has a command event and no linked state, so its label is configuration, not restored;
//  8. THE SWITCHABILITY PROOF: the same widget class driven by the router and by the host records the
//     same two virtual calls with the same arguments;
//  9. geometry: controls stack by size_hint().height(), and the cursor matches alloc_row();
// 10. interact() only COLLECTS: two interactions change no control state until paint();
// 11. a NO_FOCUS host is not a focus station: the router's chain skips it and moves to the next
//     focusable item, while a mouse step is still always collected;
// 12. the host AS a focus station: the router focuses the host item, its own Tab chain (previous
//     pass filtered by is_focusable(), NO_FOCUS excluded) moves without wrapping, and at the chain
//     end the host refuses Tab so the router advances to the next item;
// 13. a non-Tab key reaches only the host's inner focus (the other controls see key == 0);
// 14. an internal control's set_focus() stops at the host boundary (its item-level focus and the
//     router's focus item do not change), and lowering it to NO_FOCUS makes set_focus() clear the
//     inner focus;
// 15. the two focus levels are mirrored: moving the inner focus drives the controls' item-level
//     has_focus() and exactly one focus_in/focus_out pair per real change;
// 16. the focused control's clear_focus() reaches the host sink, clears the inner focus, and a normal
//     key is then refused;
// 17. begin_group: the id prefix isolates the caches ("ok" vs "im/ok") and input is mapped through
//     the group origin (the controls' geometry is group-local);
// 18. the cursor stack: a group starts at the group origin, end_group() restores the outer cursor;
// 19. nested groups concatenate prefixes ("a/b/ok") and accumulate origins;
// 20. group clipping: a control larger than its group is clipped to the group area;
// 21. the host's own boundary: a 300-pixel content row packed into an 80-pixel host keeps every
//     pixel outside the host rectangle untouched, including text a control draws directly (the
//     host's clip, not the control's own, is what stops it);
// 22. click focus inside the host: a press on a CLICK_FOCUS/STRONG_FOCUS control moves the inner
//     focus through sync_inner_focus() BEFORE the control's own interact(); the same press focuses
//     the click-focusable host item, so host.has_focus() is true as well. A press on a NO_FOCUS
//     control or on blank host space leaves the inner focus unchanged.
// 15. the inner focus is MIRRORED into the controls' item-level has_focus: Tab from a to b gives
//     a->has_focus() == false / b->has_focus() == true (what a focus ring paints from), one
//     focus_out/focus_in pair per change and nothing on a redundant request;
// 16. the focused control's clear_focus() reaches the host sink: the inner focus is cleared,
//     has_focus() turns false, and a normal key afterwards is refused by the host.
//
// Everything is offscreen (image_t + painter_t); no window and no video subsystem are created. The
// host is fed by calling interact() directly (what the router does for an item target), and the key
// cases use a real input_router_t over an item tree so the focus hand-off is exercised end to end.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/any_t.h"
#include "hwshqtb/metaobject/log.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/metaobject/meta_type_t.h"
#include "hwshqtb/metaobject/object_pointer_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/im_host_t.h"
#include "hwshqtb/ui/input_router_t.h"
#include "hwshqtb/ui/push_button_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;
using namespace hwshqtb::ui;

namespace context_test {

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

/// Captured log records (one Warning is part of three acceptance cases).
struct record_t {
    std::string category;
    log_level_enum level = log_level_enum::DEBUG;
    std::string message;
};

std::vector<record_t>& records() {
    static std::vector<record_t> captured;
    return captured;
}

std::size_t count_records(log_level_enum level, std::string_view needle) {
    std::size_t found = 0;
    for (const record_t& record : records()) {
        if (record.level == level && record.message.find(needle) != std::string::npos) {
            ++found;
        }
    }
    return found;
}

/// The reflected type name of push_button_t. Naming the meta-object here also keeps push_button_t's
/// translation unit linked in a static build, which is what registers it.
const std::string_view button_type = push_button_t::static_meta_object.class_name();

/// One offscreen host: an image, its painter and the immediate host under test.
struct env_t {
    image_t image{ 240, 160 };
    painter_t painter{ image };
    im_host_t host;

    env_t() {
        painter.begin();
        host.set_geometry(rect_t{ 0, 0, 240, 160 });
    }
};

/// The three interaction shapes the host consumes (hovered is recomputed by the host).
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

interaction_t move_in(point_t local, bool down) {
    interaction_t in;
    in.mouse = local;
    in.mouse_down = down;
    in.hovered = true;
    return in;
}

// ---------------------------------------------------------------------------
// The switchability probes (8). Both paths must record the same two virtual calls.
// ---------------------------------------------------------------------------
struct probe_call_t {
    enum struct kind_t {
        interact,
        paint,
    };

    kind_t kind = kind_t::interact;
    interaction_t in{};
    rect_t geometry{};
    /// @brief The probe that produced the call (the log is shared by all instances).
    const void* widget = nullptr;
};

/// A counting widget: records every interact()/paint() with its arguments.
class probe_widget_t: public widget_t {
    OBJECT

public:
    probe_widget_t():
        widget_t(nullptr) { }

    explicit probe_widget_t(widget_t* parent):
        widget_t(parent) { }

    /// @brief The shared call log (all instances write here, so the test snapshots between paths).
    static inline std::vector<probe_call_t> log;

    /// @brief How many focus gained events all probes received (shared, like the call log).
    static inline int focus_in_calls = 0;

    /// @brief How many focus lost events all probes received (shared, like the call log).
    static inline int focus_out_calls = 0;

    /// @brief Install the fake content size the probe reports (it has no content of its own).
    void set_natural_size(extent_t size) noexcept {
        _natural_size = size;
    }

    /// @brief Whether the last press it received arrived after the host had already focused it.
    bool saw_press_with_focus = false;

    /// @brief The probe's fake content size (its opinion; the layout applies any constraint).
    extent_t size_hint() const override {
        return _natural_size;
    }

    bool interact(const interaction_t& in) override {
        if (in.mouse_pressed) {
            saw_press_with_focus = has_focus(); // the host must focus before the delivery
        }
        log.push_back(probe_call_t{ probe_call_t::kind_t::interact, in, geometry(), this });
        return false;
    }

    void paint_event(paint_event_t* event) override {
        log.push_back(probe_call_t{ probe_call_t::kind_t::paint, interaction_t{}, geometry(), this });
        widget_t::paint_event(event);
    }

protected:
    void focus_change_event(focus_change_event_t* event) override {
        if (event->has_focus()) {
            ++focus_in_calls;
        }
        else {
            ++focus_out_calls;
        }
        widget_t::focus_change_event(event); // keep the repaint request of the base class
    }

private:
    extent_t _natural_size{};
};
META_OBJECT(probe_widget_t);
META_REGISTER_TYPE(probe_widget_t);

/// A control that draws one long text run DIRECTLY in paint_event(), from its left edge to the right.
/// The host gives it a 300-pixel content width inside a much narrower host; only the host's own clip
/// stands between the text and the pixels beyond the host's rectangle.
class spill_text_widget_t: public widget_t {
    OBJECT

public:
    spill_text_widget_t():
        widget_t(nullptr) { }

    explicit spill_text_widget_t(widget_t* parent):
        widget_t(parent) { }

    /// @brief The text the control paints: 40 monospace cells (240 pixels) in one run.
    static constexpr std::string_view text = "0123456789012345678901234567890123456789";

protected:
    void paint_event(paint_event_t* event) override {
        painter_t& painter = event->painter();
        painter.set_color(color_t::black_color());
        painter.draw_text(point_t{ 0, 12 }, text);
    }
};
META_OBJECT(spill_text_widget_t);
META_REGISTER_TYPE(spill_text_widget_t);

/// (6) A registered object that is NOT a widget: control() must reject it.
struct plain_object_t: object_t {
    OBJECT
public:
    plain_object_t() = default;
};
META_OBJECT(plain_object_t);
META_REGISTER_TYPE(plain_object_t);

/// @brief Compare two interaction snapshots field by field.
bool same_interaction(const interaction_t& a, const interaction_t& b) {
    return a.mouse == b.mouse && a.mouse_down == b.mouse_down
        && a.mouse_pressed == b.mouse_pressed && a.mouse_released == b.mouse_released
        && a.hovered == b.hovered && a.modifiers == b.modifiers;
}

/// @brief A Tab key step (press edge only).
interaction_t tab_press(bool shift = false) {
    interaction_t in;
    in.key = interaction_t::tab_key;
    in.key_pressed = true;
    in.modifiers = shift ? modifier_bits(modifier_flag::SHIFT) : 0u;
    return in;
}

/// @brief A printable key step (press edge only).
interaction_t key_press(int key) {
    interaction_t in;
    in.key = key;
    in.key_pressed = true;
    return in;
}

/// @brief A routed IM world: root -> panel (NO_FOCUS container) -> im_host (a focus STATION) plus a
///        focusable sink widget after it, so the router's move-on can be observed.
struct routed_env_t {
    image_t image{ 240, 160 };
    painter_t painter{ image };
    widget_t root;
    widget_t panel;
    im_host_t host;
    widget_t sink;
    input_router_t router;
    probe_widget_t* a = nullptr;
    probe_widget_t* b = nullptr;
    probe_widget_t* c = nullptr;

    routed_env_t(bool host_focusable, bool sink_focusable, bool a_tab_focus, bool b_tab_focus,
                 bool c_tab_focus = false):
        router(root) {
        painter.begin();
        root.set_geometry(rect_t{ 0, 0, 240, 160 });
        panel.set_parent(&root);
        panel.set_geometry(rect_t{ 0, 0, 240, 80 });
        host.set_parent(&panel);
        host.set_geometry(rect_t{ 0, 0, 240, 80 });
        host.set_focus_policy(host_focusable ? focus_policy_enum::TAB_FOCUS : focus_policy_enum::NO_FOCUS);
        sink.set_parent(&root);
        sink.set_geometry(rect_t{ 0, 90, 240, 20 });
        sink.set_focus_policy(sink_focusable ? focus_policy_enum::TAB_FOCUS : focus_policy_enum::NO_FOCUS);
        a = make_probe("a", a_tab_focus);
        b = make_probe("b", b_tab_focus);
        c = make_probe("c", c_tab_focus);
        host.set_frame_handler([](im_host_t& driven) {
            (void)driven.control("probe_widget_t", "a");
            (void)driven.control("probe_widget_t", "b");
            (void)driven.control("probe_widget_t", "c");
        });
    }

    probe_widget_t* make_probe(const char* name, bool tab_focus) {
        auto* const probe = static_cast<probe_widget_t*>(host.widget("probe_widget_t", name));
        if (probe == nullptr) {
            return nullptr;
        }
        probe->set_natural_size(extent_t{ 80, 26 });
        probe->set_focus_policy(tab_focus ? focus_policy_enum::TAB_FOCUS : focus_policy_enum::NO_FOCUS);
        return probe;
    }

    /// @brief One pass (also moves the host's second-level focus for a queued Tab).
    void pass() {
        host.render(painter);
    }

    /// @brief How many interact() calls @p control made with exactly @p key.
    int keys_seen(const probe_widget_t* control, int key) const {
        int count = 0;
        for (const probe_call_t& call : probe_widget_t::log) {
            if (call.widget == control && call.kind == probe_call_t::kind_t::interact && call.in.key == key) {
                ++count;
            }
        }
        return count;
    }
};
}  // namespace context_test

using namespace context_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    install_log_handler([](log_level_enum level, const log_context_t& context, std::string_view message) {
        records().push_back(record_t{ std::string(context.category), level, std::string(message) });
        return true; // consumed: the default stderr sink stays silent
    });

    // No application object here: register the bool the User property read returns.
    (void)meta_type_t::register_type<bool>();

    std::printf("== J1: ui::im_host_t ==\n");
    CHECK(button_type == "push_button_t");

    // -----------------------------------------------------------------------
    // (1) Two passes drive the same object; its state survives.
    // -----------------------------------------------------------------------
    {
        env_t env;
        auto& host = env.host;
        host.set_frame_handler([](im_host_t& driven) {
            (void)driven.control(button_type, "ok");
        });

        host.render(env.painter);
        CHECK(host.pass() == 1u);
        push_button_t* const first = static_cast<push_button_t*>(host.widget(button_type, "ok"));
        CHECK(first != nullptr);
        first->set_text("OK");

        host.render(env.painter);
        CHECK(host.pass() == 2u);
        push_button_t* const second = static_cast<push_button_t*>(host.widget(button_type, "ok"));
        CHECK(second == first);                        // the same object, not a new one
        CHECK(second->text() == "OK");                 // its state survived the pass boundary
        CHECK(host.children().size() == 1u);           // and it is the host's child (object tree owns it)
    }

    // -----------------------------------------------------------------------
    // (2) Press pass -> no activation; release-inside pass -> true once; the next pass -> false.
    // -----------------------------------------------------------------------
    {
        env_t env;
        auto& host = env.host;
        any_t value;
        host.set_frame_handler([&value](im_host_t& driven) {
            value = driven.control(button_type, "ok");
        });
        host.render(env.painter);
        CHECK(value.is_initialized());
        CHECK(!value.get<bool>());

        push_button_t* const button = static_cast<push_button_t*>(host.widget(button_type, "ok"));
        CHECK(button != nullptr);
        int clicked_count = 0;
        button->connect("clicked_changed(bool) const", [&clicked_count](bool clicked) {
            if (clicked) {
                ++clicked_count;
            }
        });

        // (2a) press pass: the button is pressed but not activated.
        host.interact(press_in(point_t{ 10, 13 }));
        host.render(env.painter);
        CHECK(value.is_initialized());
        CHECK(!value.get<bool>());
        CHECK(clicked_count == 0);

        // (2b) release-inside pass: activated exactly once.
        host.interact(release_in(point_t{ 10, 13 }));
        host.render(env.painter);
        CHECK(value.is_initialized());
        CHECK(value.get<bool>());
        CHECK(clicked_count == 1);

        // (2c) the next pass (no event: the settle snapshot) is false again.
        host.render(env.painter);
        CHECK(value.is_initialized());
        CHECK(!value.get<bool>());
        CHECK(clicked_count == 1);
    }

    // -----------------------------------------------------------------------
    // (3) Release outside cancels: no activation, no clicked().
    // -----------------------------------------------------------------------
    {
        env_t env;
        auto& host = env.host;
        any_t value;
        host.set_frame_handler([&value](im_host_t& driven) {
            value = driven.control(button_type, "outside");
        });
        host.render(env.painter);
        CHECK(value.is_initialized());

        push_button_t* const button = static_cast<push_button_t*>(host.widget(button_type, "outside"));
        CHECK(button != nullptr);
        int clicked_count = 0;
        button->connect("clicked_changed(bool) const", [&clicked_count](bool clicked) {
            if (clicked) {
                ++clicked_count;
            }
        });

        host.interact(press_in(point_t{ 10, 13 }));
        host.render(env.painter);
        CHECK(!value.get<bool>());

        // Move far outside and release.
        host.interact(move_in(point_t{ 200, 150 }, true));
        host.interact(release_in(point_t{ 200, 150 }));
        host.render(env.painter);
        CHECK(value.is_initialized());
        CHECK(!value.get<bool>());
        CHECK(clicked_count == 0);
    }

    // -----------------------------------------------------------------------
    // (4) THE FOLDING COUNTEREXAMPLE. One batch with DOWN outside and UP inside must NOT activate;
    //     the folded snapshot the old implementation rebuilt WOULD activate. Printing both results is
    //     what makes the boundary decidable.
    // -----------------------------------------------------------------------
    {
        env_t env;
        auto& host = env.host;
        any_t value;
        host.set_frame_handler([&value](im_host_t& driven) {
            value = driven.control(button_type, "folded");
        });
        host.render(env.painter);
        push_button_t* const button = static_cast<push_button_t*>(host.widget(button_type, "folded"));
        CHECK(button != nullptr);
        int clicked_count = 0;
        button->connect("clicked_changed(bool) const", [&clicked_count](bool clicked) {
            if (clicked) {
                ++clicked_count;
            }
        });

        // The real sequence: the user pressed outside the control and released over it.
        host.interact(press_in(point_t{ -5, 13 }));
        host.interact(release_in(point_t{ 10, 13 }));
        host.render(env.painter);
        const bool batch_activated = value.is_initialized() && value.get<bool>();
        const int batch_clicks = clicked_count;
        CHECK(value.is_initialized());
        CHECK(!batch_activated);

        // The folded snapshot: the same batch collapsed to "last position + both edges". The button
        // state machine cannot tell it apart from a real click, so it activates — the exact fake the
        // replay prevents.
        interaction_t folded;
        folded.mouse = point_t{ 10, 13 };
        folded.mouse_pressed = true;
        folded.mouse_released = true;
        folded.hovered = true;
        host.interact(folded);
        host.render(env.painter);
        const bool folded_activated = value.is_initialized() && value.get<bool>();
        const int folded_clicks = clicked_count;
        CHECK(folded_activated);
        CHECK(folded_clicks == 1);

        std::printf("  [G4] one batch DOWN(-5,13)+UP(10,13) -> activated=%d clicked=%d; "
                    "folded snapshot -> activated=%d clicked=%d (the old fake)\n",
                    batch_activated ? 1 : 0, batch_clicks, folded_activated ? 1 : 0, folded_clicks);
    }

    // -----------------------------------------------------------------------
    // (5) Press inside -> drag out -> drag back -> release inside activates: the grab survives
    //     leaving the control's rectangle.
    // -----------------------------------------------------------------------
    {
        env_t env;
        auto& host = env.host;
        any_t value;
        host.set_frame_handler([&value](im_host_t& driven) {
            value = driven.control(button_type, "drag");
        });
        host.render(env.painter);
        CHECK(value.is_initialized() && !value.get<bool>());

        host.interact(press_in(point_t{ 10, 13 }));
        host.render(env.painter);
        CHECK(!value.get<bool>());

        host.interact(move_in(point_t{ 10, 120 }, true)); // dragged outside, still held
        host.render(env.painter);
        CHECK(!value.get<bool>());

        host.interact(move_in(point_t{ 10, 13 }, true));  // dragged back in, still held
        host.render(env.painter);
        CHECK(!value.get<bool>());

        host.interact(release_in(point_t{ 10, 13 }));
        host.render(env.painter);
        CHECK(value.is_initialized());
        CHECK(value.get<bool>());
    }

    // -----------------------------------------------------------------------
    // (6) Empty name / unregistered type / non-widget type: a DEFAULT-CONSTRUCTED (invalid) any_t,
    //     not the valid void any_t a control without a User property answers, plus exactly one
    //     Warning.
    // -----------------------------------------------------------------------
    {
        env_t env;
        auto& host = env.host;
        const std::size_t before = host.children().size();
        any_t value;

        // (6a) empty object_name.
        records().clear();
        host.set_frame_handler([&value](im_host_t& driven) {
            value = driven.control(button_type, "");
        });
        host.render(env.painter);
        CHECK(!value.is_valid());       // invalid, not merely uninitialised
        CHECK(!value.is_initialized());
        CHECK(count_records(log_level_enum::WARNING, "object_name must not be empty") == 1u);
        CHECK(host.children().size() == before);

        // (6b) unregistered type: object_t::create() logs the one Warning.
        records().clear();
        host.set_frame_handler([&value](im_host_t& driven) {
            value = driven.control("no_such_type_xyz", "missing");
        });
        host.render(env.painter);
        CHECK(!value.is_valid());
        CHECK(!value.is_initialized());
        CHECK(count_records(log_level_enum::WARNING, "no class registered") == 1u);
        CHECK(host.children().size() == before);

        // (6c) a registered object that is NOT a widget: created, rejected, deleted, one Warning.
        records().clear();
        host.set_frame_handler([&value](im_host_t& driven) {
            value = driven.control("plain_object_t", "plain");
        });
        host.render(env.painter);
        CHECK(!value.is_valid());
        CHECK(!value.is_initialized());
        CHECK(count_records(log_level_enum::WARNING, "is not a widget") == 1u);
        CHECK(host.children().size() == before);

        records().clear();
        CHECK(host.widget("no_such_type_xyz", "missing") == nullptr);
        CHECK(count_records(log_level_enum::WARNING, "no class registered") == 1u);
    }

    // -----------------------------------------------------------------------
    // (7) collect(): an unvisited control is deleted; a visited one survives; the next control()
    //     creates a fresh object. push_button_t has a command event and no linked state, so its
    //     label is configuration and starts from the default again.
    // -----------------------------------------------------------------------
    {
        env_t env;
        auto& host = env.host;

        host.set_frame_handler([](im_host_t& driven) {
            (void)driven.control(button_type, "idle");
        });
        host.render(env.painter);
        push_button_t* const first = static_cast<push_button_t*>(host.widget(button_type, "idle"));
        CHECK(first != nullptr);
        first->set_text("old label");
        object_pointer_t<object_t> old_guard{ first };

        // Three passes that never visit "idle" but keep "kept" alive.
        host.set_frame_handler([](im_host_t& driven) {
            (void)driven.control(button_type, "kept");
        });
        for (int i = 0; i < 3; ++i) {
            host.render(env.painter);
        }

        host.collect(0); // 0 = "not visited in this pass": deletes "idle" only
        CHECK(host.children().size() == 1u); // "kept" survived, "idle" was deleted
        CHECK(host.widget(button_type, "kept") != nullptr);
        CHECK(old_guard.data() == nullptr); // the collected control was really destroyed

        host.set_frame_handler([](im_host_t& driven) {
            (void)driven.control(button_type, "idle");
        });
        host.render(env.painter);
        push_button_t* const second = static_cast<push_button_t*>(host.widget(button_type, "idle"));
        CHECK(second != nullptr);
        // The raw address may be REUSED by the allocator for the next control of the same size, so a
        // fresh object is proven by the death guard of the old one and by the new child, not by
        // address inequality. No state is stored for a push button (its activated event is a
        // command, not a state change), so the label is default-constructed again.
        CHECK(host.children().size() == 2u);           // the kept control + the freshly created idle
        CHECK(second->object_name() == "idle");        // cached under the same name
        CHECK(second->text() == "Button");             // configuration: not state, not restored
        CHECK(old_guard.data() == nullptr);
    }

    // -----------------------------------------------------------------------
    // (8) THE SWITCHABILITY PROOF. The same widget class, the same two virtuals, the same arguments.
    // -----------------------------------------------------------------------
    {
        env_t env;
        any_t value;
        im_host_t& host = env.host;
        host.set_geometry(rect_t{ 0, 0, 120, 80 });

        // Prepare the host's control (hint 26 so its row matches the retained probe's rectangle).
        probe_widget_t* const host_probe =
            static_cast<probe_widget_t*>(host.widget("probe_widget_t", "probe"));
        CHECK(host_probe != nullptr);
        host_probe->set_natural_size(extent_t{ 120, 26 });
        probe_widget_t::log.clear();

        // (a) The RETAINED path: the router drives the same class through a real widget tree.
        widget_t retained_root;
        retained_root.set_geometry(rect_t{ 0, 0, 120, 80 });
        probe_widget_t retained{ &retained_root };
        retained.set_geometry(rect_t{ 0, 0, 120, 26 });
        probe_widget_t::log.clear();
        input_router_t router{ retained_root };
        interaction_t retained_in;
        retained_in.mouse = retained_root.map_to_global(point_t{ 6, 4 });
        retained_in.mouse_down = true;
        retained_in.mouse_pressed = true;
        retained_in.modifiers = modifier_bits(modifier_flag::SHIFT);
        (void)router.dispatch(retained_in);
        retained_root.render(env.painter);
        const std::vector<probe_call_t> retained_log = probe_widget_t::log;

        // (b) The HOST path: the same class, the same step, driven through control().
        probe_widget_t::log.clear();
        host.set_frame_handler([&value](im_host_t& driven) {
            value = driven.control("probe_widget_t", "probe");
        });
        interaction_t host_in;
        host_in.mouse = point_t{ 6, 4 }; // host-local; the control sits at {0, 0}
        host_in.mouse_down = true;
        host_in.mouse_pressed = true;
        host_in.modifiers = modifier_bits(modifier_flag::SHIFT);
        host.interact(host_in);
        host.render(env.painter);
        const std::vector<probe_call_t> host_log = probe_widget_t::log;

        CHECK(value.is_valid());          // the control exists: a valid, declared-only void any_t
        CHECK(!value.is_initialized());    // ... with nothing to report (no User property)
        CHECK(retained_log.size() == 2u);
        CHECK(host_log.size() == 2u);
        CHECK(retained_log[0].kind == probe_call_t::kind_t::interact);
        CHECK(retained_log[1].kind == probe_call_t::kind_t::paint);
        CHECK(host_log[0].kind == probe_call_t::kind_t::interact);
        CHECK(host_log[1].kind == probe_call_t::kind_t::paint);
        CHECK(same_interaction(retained_log[0].in, host_log[0].in));
        CHECK(retained_log[0].geometry == host_log[0].geometry);
        CHECK(retained_log[1].geometry == host_log[1].geometry);
        CHECK(retained_log[0].in.mouse == point_t{ 6, 4 });
        CHECK(retained_log[0].in.mouse_pressed);
        CHECK(retained_log[0].in.hovered);
    }

    // -----------------------------------------------------------------------
    // (9) Geometry: controls stack by size_hint().height(), the width is the host width, and the
    //     cursor matches what alloc_row() produced.
    // -----------------------------------------------------------------------
    {
        env_t env;
        auto& host = env.host;
        host.set_frame_handler([](im_host_t& driven) {
            (void)driven.control(button_type, "a");
            (void)driven.control(button_type, "b");
            (void)driven.control(button_type, "c");
        });
        host.render(env.painter);

        widget_t* const a = host.widget(button_type, "a");
        widget_t* const b = host.widget(button_type, "b");
        widget_t* const c = host.widget(button_type, "c");
        CHECK(a != nullptr && b != nullptr && c != nullptr);
        if (a != nullptr && b != nullptr && c != nullptr) {
            const int height = a->size_hint().height();
            // A PROPERTY, not a magic value: a button band holds one line box.
            CHECK(height >= ::hwshqtb::gui::font_metrics_t{}.line_height());
            CHECK(a->geometry() == rect_t{ 0, 0, 240, height });
            CHECK(b->geometry() == rect_t{ 0, height + 4, 240, height });
            CHECK(c->geometry() == rect_t{ 0, 2 * (height + 4), 240, height });
            CHECK(host.cursor() == point_t{ 0, 3 * (height + 4) });

            // The cursor is exactly the alloc_row() cursor: one more row starts where it points.
            const point_t cursor = host.cursor();
            const rect_t row = host.alloc_row(height);
            CHECK(row == rect_t{ 0, cursor.y(), 240, height });
            CHECK(host.cursor() == point_t{ 0, cursor.y() + height + 4 });
        }
    }

    // -----------------------------------------------------------------------
    // (10) interact() only COLLECTS: two interactions change no control state until paint().
    // -----------------------------------------------------------------------
    {
        env_t env;
        auto& host = env.host;
        any_t value;
        host.set_frame_handler([&value](im_host_t& driven) {
            value = driven.control(button_type, "queued");
        });
        host.render(env.painter);
        push_button_t* const button = static_cast<push_button_t*>(host.widget(button_type, "queued"));
        CHECK(button != nullptr);
        int clicked_count = 0;
        button->connect("clicked_changed(bool) const", [&clicked_count](bool clicked) {
            if (clicked) {
                ++clicked_count;
            }
        });

        // Two interactions arrive; NO paint runs between them (I1: nothing is touched yet).
        host.interact(press_in(point_t{ 10, 13 }));
        host.interact(release_in(point_t{ 10, 13 }));
        CHECK(button->clicked() == false);
        CHECK(clicked_count == 0);
        CHECK(value.is_initialized() && !value.get<bool>());

        // The next pass replays the whole batch: consumption and paint are back to back (I2).
        host.render(env.painter);
        CHECK(value.is_initialized());
        CHECK(value.get<bool>());
        CHECK(clicked_count == 1);
    }

    // -----------------------------------------------------------------------
    // (11) A NO_FOCUS host is NOT a focus station: the router's Tab chain skips it and moves to the
    //      sink widget, while a mouse step is still always collected by the host.
    // -----------------------------------------------------------------------
    {
        routed_env_t env{ false, true, false, false, false };
        env.pass();
        CHECK(env.a != nullptr && env.b != nullptr && env.c != nullptr);
        CHECK(!env.host.interact(tab_press()));       // not focusable / empty inner chain
        CHECK(!env.host.interact(key_press(65)));     // no internal focus
        CHECK(env.host.interact(press_in(point_t{ 10, 13 }))); // mouse is always collected

        probe_widget_t::log.clear();
        CHECK(env.router.dispatch(tab_press()));      // chain = [sink]; the host is not in it
        CHECK(env.router.focus_item() == &env.sink);
        env.pass();
        CHECK(env.keys_seen(env.a, interaction_t::tab_key) == 0);
        CHECK(env.keys_seen(env.b, interaction_t::tab_key) == 0);
        CHECK(env.keys_seen(env.c, interaction_t::tab_key) == 0);
    }

    // -----------------------------------------------------------------------
    // (12) The host AS a focus station: the router focuses the host item; the host's own Tab chain
    //      (a/b focusable, c NO_FOCUS) moves without wrapping; at the end the host refuses Tab and
    //      the router advances to the next item.
    // -----------------------------------------------------------------------
    {
        routed_env_t env{ true, true, true, true, false };
        env.pass();
        CHECK(env.a != nullptr && env.b != nullptr && env.c != nullptr);
        env.router.set_focus(&env.host);
        CHECK(env.router.focus_item() == &env.host);
        CHECK(env.host.has_focus());                  // item-level focus (distinct from the inner one)

        CHECK(env.router.dispatch(tab_press()));      // the host takes Tab -> inner focus = a
        CHECK(env.router.focus_item() == &env.host);  // the host consumed it
        env.pass();
        CHECK(env.keys_seen(env.a, interaction_t::tab_key) == 0); // Tab is eaten, not replayed
        probe_widget_t::log.clear();
        CHECK(env.router.dispatch(key_press(65)));    // delivered to the inner focus (a)
        env.pass();
        CHECK(env.keys_seen(env.a, 65) == 1);
        CHECK(env.keys_seen(env.b, 65) == 0);
        CHECK(env.keys_seen(env.c, 65) == 0);
        CHECK(env.keys_seen(env.b, 0) == 1);          // b saw the step without the key
        CHECK(env.keys_seen(env.c, 0) == 1);

        CHECK(env.host.interact(tab_press()));        // a -> b
        env.pass();
        probe_widget_t::log.clear();
        CHECK(env.host.interact(key_press(66)));
        env.pass();
        CHECK(env.keys_seen(env.a, 66) == 0);
        CHECK(env.keys_seen(env.b, 66) == 1);         // b owns the inner focus
        CHECK(env.keys_seen(env.c, 66) == 0);         // c never joined the chain

        CHECK(!env.host.interact(tab_press()));       // b is the end: NO wrap forward
        CHECK(env.host.interact(tab_press(true)));    // b -> a exists backwards
        env.pass();                                   // inner focus = a
        CHECK(!env.host.interact(tab_press(true)));   // a is the first: NO wrap backwards

        CHECK(env.host.interact(tab_press()));        // back to b for the hand-off check
        env.pass();
        CHECK(!env.host.interact(tab_press()));       // b: the host refuses Tab
        CHECK(env.router.dispatch(tab_press()));      // ... so the ROUTER advances to the next item
        CHECK(env.router.focus_item() == &env.sink);
        CHECK(!env.host.has_focus());                 // the router cleared the old focus
    }

    // -----------------------------------------------------------------------
    // (13) Non-Tab keys go only to the inner focus of the focused host; the other controls see
    //      key == 0.
    // -----------------------------------------------------------------------
    {
        routed_env_t env{ true, true, true, true, false };
        env.pass();
        CHECK(env.a != nullptr && env.b != nullptr);
        env.router.set_focus(&env.host);
        CHECK(env.router.dispatch(tab_press()));      // inner focus = a
        env.pass();
        CHECK(env.host.interact(tab_press()));        // a -> b
        env.pass();
        probe_widget_t::log.clear();
        CHECK(env.router.dispatch(key_press(88)));    // host -> inner focus b
        env.pass();
        CHECK(env.keys_seen(env.a, 88) == 0);
        CHECK(env.keys_seen(env.b, 88) == 1);
        CHECK(env.keys_seen(env.c, 88) == 0);
        CHECK(env.keys_seen(env.a, 0) == 1);
        CHECK(env.keys_seen(env.c, 0) == 1);
    }

    // -----------------------------------------------------------------------
    // (14) An internal control's set_focus() stops at the HOST boundary: it moves the host's
    //      second-level focus only; the host's item-level focus and the router's focus item do not
    //      change. Lowering the control to NO_FOCUS makes set_focus() clear that inner focus.
    // -----------------------------------------------------------------------
    {
        routed_env_t env{ true, true, true, true, false };
        env.pass();
        env.router.set_focus(&env.host);
        CHECK(env.router.focus_item() == &env.host);
        CHECK(env.host.has_focus());

        // Put the inner focus on b first (two Tabs), then a's set_focus() moves it to a only.
        CHECK(env.host.interact(tab_press()));
        env.pass();
        CHECK(env.host.interact(tab_press()));
        env.pass();
        probe_widget_t::log.clear();
        CHECK(env.router.dispatch(key_press(70)));
        env.pass();
        CHECK(env.keys_seen(env.b, 70) == 1);

        env.a->set_focus();                            // stops at the host's own sink
        CHECK(env.router.focus_item() == &env.host);   // router focus unchanged
        CHECK(env.host.has_focus());                   // item-level focus unchanged
        probe_widget_t::log.clear();
        CHECK(env.router.dispatch(key_press(71)));
        env.pass();
        CHECK(env.keys_seen(env.a, 71) == 1);          // the inner focus moved to a
        CHECK(env.keys_seen(env.b, 71) == 0);

        // Lowering a to NO_FOCUS makes its set_focus() request fail the host's validation: the inner
        // focus is cleared, so the next non-Tab key is refused by the host.
        env.a->set_focus_policy(focus_policy_enum::NO_FOCUS);
        env.a->set_focus();
        CHECK(env.router.focus_item() == &env.host);
        CHECK(env.host.has_focus());
        probe_widget_t::log.clear();
        CHECK(!env.router.dispatch(key_press(72)));    // no inner focus -> the host refuses
        env.pass();
        CHECK(env.keys_seen(env.a, 72) == 0);
        CHECK(env.keys_seen(env.b, 72) == 0);
    }

    // -----------------------------------------------------------------------
    // (15) The inner focus is MIRRORED into the controls' ITEM-level focus: after Tab moves it from
    //      a to b, a->has_focus() is false and b->has_focus() is true — the pair a focus ring paints
    //      from. Each move fires exactly one focus_out/focus_in, and a repeated request fires none.
    // -----------------------------------------------------------------------
    {
        routed_env_t env{ true, true, true, true, false };
        env.pass();
        CHECK(env.a != nullptr && env.b != nullptr);
        env.router.set_focus(&env.host);
        CHECK(!env.a->has_focus());                    // no inner focus yet, so no control owns it
        CHECK(!env.b->has_focus());

        const int in_before = probe_widget_t::focus_in_calls;
        const int out_before = probe_widget_t::focus_out_calls;

        CHECK(env.router.dispatch(tab_press()));       // the host takes Tab -> inner focus = a
        env.pass();
        CHECK(env.a->has_focus());                     // the host drove the item-level focus
        CHECK(!env.b->has_focus());
        CHECK(probe_widget_t::focus_in_calls == in_before + 1);
        CHECK(probe_widget_t::focus_out_calls == out_before); // nobody lost the focus

        CHECK(env.router.dispatch(tab_press()));       // the host takes Tab -> inner focus = b
        env.pass();
        CHECK(!env.a->has_focus());                    // the OLD control lost it (focus ring off)
        CHECK(env.b->has_focus());                     // the NEW control gained it (focus ring on)
        CHECK(probe_widget_t::focus_in_calls == in_before + 2);
        CHECK(probe_widget_t::focus_out_calls == out_before + 1);

        env.b->set_focus();                            // same target again: not a real change
        CHECK(env.b->has_focus());
        CHECK(probe_widget_t::focus_in_calls == in_before + 2); // ... so no second focus_in
        CHECK(probe_widget_t::focus_out_calls == out_before + 1);
    }

    // -----------------------------------------------------------------------
    // (16) The focused control's clear_focus() reaches the host's focus sink instead of being
    //      short-circuited by item_t::clear_focus() (it runs only while the item HAS the focus): the
    //      inner focus is cleared, has_focus() turns false, and a normal key is then refused.
    // -----------------------------------------------------------------------
    {
        routed_env_t env{ true, true, true, true, false };
        env.pass();
        CHECK(env.a != nullptr && env.b != nullptr);
        env.router.set_focus(&env.host);
        CHECK(env.router.dispatch(tab_press()));       // inner focus = a
        env.pass();
        CHECK(env.a->has_focus());

        env.a->clear_focus();                          // goes through the host's own sink
        CHECK(!env.a->has_focus());                    // the host mirrored the clear back
        CHECK(env.router.focus_item() == &env.host);   // the router-level focus did not move
        CHECK(env.host.has_focus());

        probe_widget_t::log.clear();
        CHECK(!env.router.dispatch(key_press(90)));    // no inner focus left: the host refuses it
        env.pass();
        CHECK(env.keys_seen(env.a, 90) == 0);
        CHECK(env.keys_seen(env.b, 90) == 0);
        CHECK(env.keys_seen(env.a, 0) == 1);           // a still received the neutral snapshot
    }

    // -----------------------------------------------------------------------
    // (17) begin_group: the id prefix isolates the caches ("ok" vs "im/ok"), and input is mapped
    //      through the group origin (the controls' geometry is group-local).
    // -----------------------------------------------------------------------
    {
        env_t env;
        auto& host = env.host;
        const rect_t area{ 0, 80, 200, 60 };
        host.set_frame_handler([&area](im_host_t& h) {
            (void)h.control("push_button_t", "ok");
            h.begin_group(area, "im");
            (void)h.control("push_button_t", "ok");
            h.end_group();
        });
        host.render(env.painter);
        auto* const outer = static_cast<push_button_t*>(host.widget(button_type, "ok"));
        auto* const inner = static_cast<push_button_t*>(host.widget(button_type, "im/ok"));
        CHECK(outer != nullptr && inner != nullptr);
        if (outer != nullptr && inner != nullptr) {
            CHECK(outer != inner);                          // two independent objects
            CHECK(outer->object_name() == "ok");
            CHECK(inner->object_name() == "im/ok");         // the prefix is part of the identity
            const int outer_h = outer->size_hint().height();
            CHECK(outer->geometry() == rect_t{ 0, 0, 240, outer_h });
            CHECK(inner->geometry() == rect_t{ 0, 0, area.width(), outer_h }); // group-LOCAL geometry

            int outer_clicks = 0;
            int inner_clicks = 0;
            outer->connect("clicked_changed(bool) const", [&outer_clicks](bool clicked) {
                if (clicked) {
                    ++outer_clicks;
                }
            });
            inner->connect("clicked_changed(bool) const", [&inner_clicks](bool clicked) {
                if (clicked) {
                    ++inner_clicks;
                }
            });

            // Pointer at "group origin + control-local (10,13)": only the INNER control sees it.
            host.interact(press_in(point_t{ area.x() + 10, area.y() + 13 }));
            host.interact(release_in(point_t{ area.x() + 10, area.y() + 13 }));
            host.render(env.painter);
            CHECK(inner_clicks == 1);
            CHECK(outer_clicks == 0);

            // Pointer at host-local (10,13): only the OUTER control sees it.
            host.interact(press_in(point_t{ 10, 13 }));
            host.interact(release_in(point_t{ 10, 13 }));
            host.render(env.painter);
            CHECK(outer_clicks == 1);
            CHECK(inner_clicks == 1);
        }
    }

    // -----------------------------------------------------------------------
    // (18) The cursor stack: entering a group starts at the group origin; end_group() restores the
    //      OUTER cursor, so the next outer control is not placed inside the group area.
    // -----------------------------------------------------------------------
    {
        env_t env;
        auto& host = env.host;
        const rect_t area{ 0, 100, 240, 60 };
        host.set_frame_handler([&area](im_host_t& h) {
            (void)h.control("push_button_t", "before");
            h.begin_group(area, "g");
            (void)h.control("push_button_t", "in");
            h.end_group();
            (void)h.control("push_button_t", "after");
        });
        host.render(env.painter);
        widget_t* const before = host.widget(button_type, "before");
        widget_t* const in_group = host.widget(button_type, "g/in");
        widget_t* const after = host.widget(button_type, "after");
        CHECK(before != nullptr && in_group != nullptr && after != nullptr);
        if (before != nullptr && in_group != nullptr && after != nullptr) {
            const int row_h = before->size_hint().height();
            const int row_step = row_h + 4;
            CHECK(before->geometry() == rect_t{ 0, 0, 240, row_h });
            CHECK(in_group->geometry() == rect_t{ 0, 0, area.width(), row_h }); // group-local row 0
            CHECK(after->geometry() == rect_t{ 0, row_step, 240, row_h });      // outer cursor continues
            CHECK(after->geometry().y() + after->geometry().height() < area.y()); // outside the group
            CHECK(host.cursor() == point_t{ 0, 2 * row_step });                   // outer cursor restored
        }
    }

    // -----------------------------------------------------------------------
    // (19) Nested groups: the prefixes concatenate ("a/b/ok") and the origins accumulate.
    // -----------------------------------------------------------------------
    {
        env_t env;
        auto& host = env.host;
        const rect_t outer_area{ 10, 20, 200, 100 };
        const rect_t inner_area{ 5, 5, 100, 50 };
        host.set_frame_handler([&outer_area, &inner_area](im_host_t& h) {
            h.begin_group(outer_area, "a");
            h.begin_group(inner_area, "b");
            (void)h.control("push_button_t", "ok");
            h.end_group();
            h.end_group();
        });
        host.render(env.painter);
        auto* const nested = static_cast<push_button_t*>(host.widget(button_type, "a/b/ok"));
        CHECK(nested != nullptr);
        if (nested != nullptr) {
            CHECK(nested->object_name() == "a/b/ok");
            CHECK(nested->geometry() == rect_t{ 0, 0, inner_area.width(), nested->size_hint().height() });
            int clicks = 0;
            nested->connect("clicked_changed(bool) const", [&clicks](bool clicked) {
                if (clicked) {
                    ++clicks;
                }
            });
            // Group origin accumulates: outer.top_left + inner.top_left.
            const point_t point{ outer_area.x() + inner_area.x() + 10,
                                 outer_area.y() + inner_area.y() + 13 };
            host.interact(press_in(point));
            host.interact(release_in(point));
            host.render(env.painter);
            CHECK(clicks == 1);
        }
    }

    // -----------------------------------------------------------------------
    // (20) Group clipping: a control that is larger than its group is clipped to the group area, so
    //      nothing outside it is painted.
    // -----------------------------------------------------------------------
    {
        env_t env;
        auto& host = env.host;
        auto* const big = static_cast<push_button_t*>(host.widget(button_type, "g/big"));
        CHECK(big != nullptr);
        if (big != nullptr) {
            big->set_fixed_size(extent_t{ 300, 100 }); // much larger than the group below
            const rect_t area{ 20, 20, 100, 50 };
            host.set_frame_handler([&area](im_host_t& h) {
                h.begin_group(area, "g");
                (void)h.control("push_button_t", "big");
                h.end_group();
            });
            env.painter.clear(color_t::white_color());
            host.render(env.painter);
            CHECK(big->geometry() == rect_t{ 0, 0, 300, 100 }); // group-local, exceeds the group
            // Inside the group the control was painted...
            CHECK(env.image.pixel(25, 25) != color_t::white_color());
            CHECK(env.image.pixel(118, 68) != color_t::white_color());
            // ... outside it the clip kept the cleared background.
            CHECK(env.image.pixel(150, 80) == color_t::white_color());
            CHECK(env.image.pixel(19, 95) == color_t::white_color());
        }
    }

    // -----------------------------------------------------------------------
    // (21) The host's own boundary: everything the host paints is clipped to rect(), including text
    //      a control draws DIRECTLY past the host's right edge. A 300-pixel content row inside an
    //      80-pixel host must not light one pixel outside the host rectangle.
    // -----------------------------------------------------------------------
    {
        image_t image{ 240, 160 };
        painter_t painter{ image };
        painter.begin();
        painter.clear(color_t::white_color()); // the blank background every outside pixel must keep

        widget_t root;
        root.set_geometry(rect_t{ 0, 0, 240, 160 });
        im_host_t host;
        host.set_parent(&root);
        const rect_t host_rect{ 20, 10, 80, 120 }; // deliberately much narrower than the content
        host.set_geometry(host_rect);
        host.set_frame_handler([](im_host_t& driven) {
            if (auto* const control =
                    static_cast<spill_text_widget_t*>(driven.widget("spill_text_widget_t", "wide"))) {
                control->set_fixed_size(extent_t{ 300, 26 }); // the content is 220 pixels too wide
            }
            (void)driven.control("spill_text_widget_t", "wide");
        });
        host.render(painter);

        widget_t* const control = host.widget("spill_text_widget_t", "wide");
        CHECK(control != nullptr);
        if (control != nullptr) {
            // The premise: the content really is wider than the host. Without this, a failure to
            // install the fixed size would let the pixel comparison below pass for the wrong reason.
            CHECK(control->geometry().width() == 300);
            CHECK(control->geometry().width() > host_rect.width());
        }

        int inside_ink = 0;
        int outside_ink = 0;
        for (int y = 0; y < image.size().height(); ++y) {
            for (int x = 0; x < image.size().width(); ++x) {
                if (image.pixel(x, y) == color_t::white_color()) {
                    continue; // untouched background
                }
                if (host_rect.contains(point_t{ x, y })) {
                    ++inside_ink;
                }
                else {
                    ++outside_ink;
                }
            }
        }
        // The text was really painted (the outside comparison is not vacuous)...
        CHECK(inside_ink > 0);
        // ... and not one pixel of it reached the area beyond the host's own rectangle.
        CHECK(outside_ink == 0);
        painter.end();
    }

    // -----------------------------------------------------------------------
    // (22) Click focus inside the host: a press on an inner click-focusable control moves the INNER
    //      focus through sync_inner_focus() before the control's own interact(), and the host item
    //      (STRONG_FOCUS) is focused by the same press, so host.has_focus() is true. A NO_FOCUS
    //      control and blank host space change nothing.
    // -----------------------------------------------------------------------
    {
        image_t image{ 240, 160 };
        painter_t painter{ image };
        painter.begin();
        widget_t root;
        im_host_t host;
        root.set_geometry(rect_t{ 0, 0, 240, 160 });
        host.set_parent(&root);
        host.set_geometry(rect_t{ 0, 0, 240, 160 });
        host.set_focus_policy(focus_policy_enum::STRONG_FOCUS); // the host item is click-focusable
        host.set_frame_handler([](im_host_t& driven) {
            (void)driven.control("probe_widget_t", "a");
            (void)driven.control("probe_widget_t", "b");
            (void)driven.control("probe_widget_t", "c");
            (void)driven.control("probe_widget_t", "d");
        });
        auto* const a = static_cast<probe_widget_t*>(host.widget("probe_widget_t", "a"));
        auto* const b = static_cast<probe_widget_t*>(host.widget("probe_widget_t", "b"));
        auto* const c = static_cast<probe_widget_t*>(host.widget("probe_widget_t", "c"));
        auto* const d = static_cast<probe_widget_t*>(host.widget("probe_widget_t", "d"));
        CHECK(a != nullptr && b != nullptr && c != nullptr && d != nullptr);
        if (a != nullptr && b != nullptr && c != nullptr && d != nullptr) {
            a->set_natural_size(extent_t{ 80, 26 });
            b->set_natural_size(extent_t{ 80, 26 });
            c->set_natural_size(extent_t{ 80, 26 });
            d->set_natural_size(extent_t{ 80, 26 });
            a->set_focus_policy(focus_policy_enum::STRONG_FOCUS);
            b->set_focus_policy(focus_policy_enum::STRONG_FOCUS);
            c->set_focus_policy(focus_policy_enum::NO_FOCUS);
            d->set_focus_policy(focus_policy_enum::STRONG_FOCUS);
            d->set_enabled(false); // click-focusable on paper, but disabled
            host.render(painter); // rows: a {0,0,...}, b {0,30,...}, c {0,60,...}, d {0,90,...}
            input_router_t router{ root };

            // A real routed click (press + release) inside @p control, in ROOT coordinates.
            const auto click = [&host, &router](const widget_t& control) {
                const rect_t row = control.geometry();
                interaction_t press;
                press.mouse = host.map_to_global(
                    point_t{ row.x() + 6, row.y() + row.height() / 2 });
                press.mouse_down = true;
                press.mouse_pressed = true;
                const bool consumed = router.dispatch(press);
                interaction_t release = press;
                release.mouse_down = false;
                release.mouse_pressed = false;
                release.mouse_released = true;
                (void)router.dispatch(release);
                return consumed;
            };

            const int in_before = probe_widget_t::focus_in_calls;
            const int out_before = probe_widget_t::focus_out_calls;
            CHECK(router.focus_item() == nullptr);

            // Click a: the host takes the outer focus and the press, and the replayed press moves the
            // inner focus to a BEFORE a's own interact() sees it.
            CHECK(click(*a));
            CHECK(router.focus_item() == &host);
            CHECK(host.has_focus());
            host.render(painter);
            CHECK(a->has_focus());
            CHECK(a->saw_press_with_focus);
            CHECK(!b->has_focus());
            CHECK(probe_widget_t::focus_in_calls == in_before + 1);
            CHECK(probe_widget_t::focus_out_calls == out_before);

            // Click b: the inner focus transfers, a receives exactly one focus_out.
            CHECK(click(*b));
            host.render(painter);
            CHECK(b->has_focus());
            CHECK(!a->has_focus());
            CHECK(b->saw_press_with_focus);
            CHECK(probe_widget_t::focus_in_calls == in_before + 2);
            CHECK(probe_widget_t::focus_out_calls == out_before + 1);

            // A press on the NO_FOCUS control c is collected but does not move the inner focus.
            CHECK(click(*c));
            host.render(painter);
            CHECK(b->has_focus());
            CHECK(!c->has_focus());
            CHECK(probe_widget_t::focus_in_calls == in_before + 2);
            CHECK(probe_widget_t::focus_out_calls == out_before + 1);

            // A press on the DISABLED (but policy-wise click-focusable) control d changes nothing.
            CHECK(click(*d));
            host.render(painter);
            CHECK(b->has_focus());
            CHECK(!d->has_focus());
            CHECK(probe_widget_t::focus_in_calls == in_before + 2);
            CHECK(probe_widget_t::focus_out_calls == out_before + 1);

            // Blank host space (below the last row) leaves the inner focus unchanged as well.
            interaction_t blank;
            blank.mouse = host.map_to_global(point_t{ 120, 130 });
            blank.mouse_down = true;
            blank.mouse_pressed = true;
            CHECK(router.dispatch(blank));
            interaction_t blank_release = blank;
            blank_release.mouse_down = false;
            blank_release.mouse_pressed = false;
            blank_release.mouse_released = true;
            (void)router.dispatch(blank_release);
            host.render(painter);
            CHECK(b->has_focus());
            CHECK(!c->has_focus());
            CHECK(router.focus_item() == &host);
            CHECK(host.has_focus());
        }
        painter.end();
    }

    install_log_handler(log_handler{});

    if (g_failures == 0) {
        std::printf("context: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("context: %d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
}
