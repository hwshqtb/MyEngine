// State survives the OBJECT, not the other way round:
//
//  1. a control's linked state property is collected (the object deleted) and a later pass
//     re-creates it: the value comes back from im_state_store_t; configuration such as a check
//     box label does not, because it is not linked to a USER event;
//  2. the reuse switch (set_reuse_enabled) changes nothing observable: the pooled and the
//     per-pass-object paths produce the same pixels, the same control state and the same signal
//     count for the same interaction sequence;
//  3. the snapshot boundary: only the property whose notifier resolves to the USER property is
//     state; an unlinked writable property, visible/enabled and geometry are NOT snapshot;
//  4. a type change under one id discards the old snapshot and the new control keeps its defaults;
//  5. radio_button_t, slider_t, scroll_bar_t and line_edit_t link checked / value / text to their
//     USER event, so the generic store restores them; the accepted clamp contract of a value setter
//     is pinned separately.
//
// Everything is offscreen (image_t + painter_t); no window and no video subsystem are created.

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/metaobject/meta_object_t.h"
#include "hwshqtb/metaobject/meta_property_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/check_box_t.h"
#include "hwshqtb/ui/im_host_t.h"
#include "hwshqtb/ui/im_state_store_t.h"
#include "hwshqtb/ui/line_edit_t.h"
#include "hwshqtb/ui/push_button_t.h"
#include "hwshqtb/ui/radio_button_t.h"
#include "hwshqtb/ui/scroll_bar_t.h"
#include "hwshqtb/ui/slider_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;
using namespace hwshqtb::ui;

/// A reflected control whose text is linked to its USER event: the state store must find `text`
/// through `text_changed`, and must NOT treat the unlinked `checked` property as state.
class im_state_probe_t: public widget_t {
    OBJECT

public:
    im_state_probe_t():
        widget_t(nullptr) { }

    explicit im_state_probe_t(widget_t* parent):
        widget_t(parent) { }

    /// @brief The label (the linked state property).
    [[= make_property("text", "set_text", "text_changed")]]
    std::string_view text() const {
        return _text;
    }

    /// @brief Replace the label and emit text_changed() on a real change.
    void set_text(std::string_view text) {
        if (_text == text) {
            return;
        }
        _text.assign(text);
        update();
        text_changed(_text);
    }

    /// @brief A writable property with NO USER link: configuration, not state.
    [[= make_property("checked", "set_checked", "checked_changed")]]
    bool checked() const {
        return _checked;
    }

    /// @brief Replace the checked configuration and emit checked_changed() on a real change.
    void set_checked(bool checked) {
        if (_checked == checked) {
            return;
        }
        _checked = checked;
        update();
        checked_changed(checked);
    }

    /// @brief The class's USER event property: named exactly like text's notifier.
    [[= make_property("text_changed", "", "", "", property_trait_flag::USER)]]
    bool text_changed() const {
        return _text_event;
    }

    /// @brief A fixed natural size: the host's row logic uses it.
    extent_t size_hint() const override {
        return extent_t{ 120, 26 };
    }

    /// @brief Emitted when set_text() changes the label (text's notifier).
    SIGNAL(void text_changed(std::string_view text) const)

    /// @brief Emitted when set_checked() changes the configuration.
    SIGNAL(void checked_changed(bool checked) const)

protected:
    /// @brief Paint nothing: these tests assert state, not pixels.
    void paint_event(paint_event_t*) override { }

private:
    std::string _text;
    bool _checked = false;
    /// @brief The per-frame USER result of this probe (never set: it has no interaction of its own).
    bool _text_event = false;
};
META_OBJECT(im_state_probe_t);
META_REGISTER_TYPE(im_state_probe_t);

/// A second reflected control with a default empty text: the type-change test needs another class.
class im_state_other_t: public widget_t {
    OBJECT

public:
    im_state_other_t():
        widget_t(nullptr) { }

    /// @brief The label (defaults to empty).
    [[= make_property("text", "set_text", "text_changed")]]
    std::string_view text() const {
        return _text;
    }

    /// @brief Replace the label.
    void set_text(std::string_view text) {
        if (_text == text) {
            return;
        }
        _text.assign(text);
        text_changed(_text);
    }

    /// @brief The class's USER event property: named exactly like text's notifier.
    [[= make_property("text_changed", "", "", "", property_trait_flag::USER)]]
    bool text_changed() const {
        return _text_event;
    }

    /// @brief Emitted when set_text() changes the label.
    SIGNAL(void text_changed(std::string_view text) const)

protected:
    /// @brief Paint nothing: the test asserts state, not pixels.
    void paint_event(paint_event_t*) override { }

private:
    std::string _text;
    /// @brief The per-frame USER result of this probe (never set).
    bool _text_event = false;
};
META_OBJECT(im_state_other_t);
META_REGISTER_TYPE(im_state_other_t);

namespace im_state_test {
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

    /// @brief Whether two images hold the same pixel in every position.
    bool same_pixels(const image_t& left, const image_t& right) {
        if (!(left.size() == right.size())) {
            return false;
        }
        for (int y = 0; y < left.size().height(); ++y) {
            for (int x = 0; x < left.size().width(); ++x) {
                if (!(left.pixel(x, y) == right.pixel(x, y))) {
                    return false;
                }
            }
        }
        return true;
    }

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

    /// @brief The USER event of one class and the writable properties that notify it.
    struct link_info_t {
        /// @brief Whether the class declares a USER property.
        bool user_present = false;
        /// @brief The USER property's name.
        std::string user_name;
        /// @brief The writable, non-USER properties whose notifier resolves to that name.
        std::vector<std::string> states;
    };

    /// @brief Read the USER/state link exactly as the store does (notifier -> USER name).
    /// @param meta The class's meta-object.
    link_info_t link_of(const meta_object_t& meta) {
        link_info_t info;
        const meta_property_t user = meta.user_property();
        if (user.is_valid()) {
            info.user_present = true;
            info.user_name = std::string(user.name());
        }
        for (std::size_t index = 0; index < meta.property_count(); ++index) {
            const meta_property_t property =
                meta.property(static_cast<index_t>(index));
            if (!property.is_valid() || property.is_user() || !property.is_writable()) {
                continue;
            }
            const meta_method_t notifier = property.notifier();
            if (notifier.is_valid() && notifier.is_signal() && notifier.name() == user.name()) {
                info.states.emplace_back(property.name());
            }
        }
        return info;
    }

    /// @brief A root -> immediate host world with one check box under the id "box".
    /// @note The frame handler sets the label every pass (configuration) and drives; `checked` is
    ///       the state the store carries across passes when the object is re-created.
    struct equivalent_world_t {
        image_t image{ 240, 160 };
        painter_t painter{ image };
        widget_t root;
        im_host_t host;
        int toggles = 0;
        std::string text;
        bool checked = false;
        connection_handle_t handle;

        explicit equivalent_world_t(bool reuse):
            host() {
            painter.begin();
            root.set_geometry(rect_t{ 0, 0, 240, 160 });
            host.set_parent(&root);
            host.set_geometry(rect_t{ 0, 0, 240, 160 });
            host.set_reuse_enabled(reuse);
            host.set_frame_handler([this](im_host_t& driven) { drive(driven); });
        }

        void drive(im_host_t& driven) {
            auto* box = static_cast<check_box_t*>(driven.widget("check_box_t", "box"));
            if (box == nullptr) {
                return;
            }
            if (!handle.is_valid()) {
                handle = box->connect("toggled(bool) const", [this]() { ++toggles; });
            }
            box->set_focus_policy(focus_policy_enum::NO_FOCUS); // no focus ring: pixels isolate state
            box->set_text("eq"); // configuration: the producing pass supplies it every time
            (void)driven.control("check_box_t", "box");
            box = static_cast<check_box_t*>(driven.widget("check_box_t", "box"));
            text = box != nullptr ? std::string(box->text()) : std::string{};
            checked = box != nullptr && box->is_checked();
        }

        void paint_once() {
            host.render(painter);
        }

        void click() {
            host.interact(press_in(point_t{ 10, 13 }));
            host.interact(release_in(point_t{ 10, 13 }));
        }
    };

    /// @brief A root -> immediate host world whose handler changes with @c stage.
    struct scripted_world_t {
        image_t image{ 240, 160 };
        painter_t painter{ image };
        widget_t root;
        im_host_t host;
        int stage = 0;
        bool use_check = true;
        std::string text;
        bool checked = false;

        scripted_world_t() {
            painter.begin();
            root.set_geometry(rect_t{ 0, 0, 240, 160 });
            host.set_parent(&root);
            host.set_geometry(rect_t{ 0, 0, 240, 160 });
            host.set_frame_handler([this](im_host_t& driven) { drive(driven); });
        }

        void drive(im_host_t& driven) {
            if (stage == 1) {
                return; // an empty pass: the control is not visited, so the pool may collect it
            }
            if (use_check) {
                if (stage == 0) {
                    if (auto* box = static_cast<check_box_t*>(driven.widget("check_box_t", "same"))) {
                        box->set_text("old");
                        box->set_checked(true);
                    }
                }
                (void)driven.control("check_box_t", "same");
                auto* box = static_cast<check_box_t*>(driven.widget("check_box_t", "same"));
                text = box != nullptr ? std::string(box->text()) : std::string{};
                checked = box != nullptr && box->is_checked();
            }
            else {
                (void)driven.control("push_button_t", "same");
                auto* button = static_cast<push_button_t*>(driven.widget("push_button_t", "same"));
                text = button != nullptr ? std::string(button->text()) : std::string{};
                checked = false;
            }
        }

        void paint_once() {
            host.render(painter);
        }
    };

    /// @brief A root -> immediate host world holding one radio, one slider, one scroll bar and one
    ///        line edit.
    /// @note The configured values are inside each control's DEFAULT range, so the store's restore
    ///       reaches them without a range clamp; the clamp contract is pinned separately.
    struct value_world_t {
        image_t image{ 240, 240 };
        painter_t painter{ image };
        widget_t root;
        im_host_t host;
        int stage = 0;
        bool radio_checked = false;
        int slider_value = -1;
        int bar_value = -1;
        std::string line_text;

        value_world_t() {
            painter.begin();
            root.set_geometry(rect_t{ 0, 0, 240, 240 });
            host.set_parent(&root);
            host.set_geometry(rect_t{ 0, 0, 240, 240 });
            host.set_frame_handler([this](im_host_t& driven) { drive(driven); });
        }

        void drive(im_host_t& driven) {
            if (stage == 1) {
                return; // empty passes: the pool may collect the controls
            }
            if (stage == 0) {
                if (auto* radio =
                        static_cast<radio_button_t*>(driven.widget("radio_button_t", "radio"))) {
                    radio->set_checked(true);
                }
                if (auto* slider = static_cast<slider_t*>(driven.widget("slider_t", "slider"))) {
                    slider->set_value(40);
                }
                if (auto* bar = static_cast<scroll_bar_t*>(driven.widget("scroll_bar_t", "bar"))) {
                    bar->set_value(60);
                }
                if (auto* edit = static_cast<line_edit_t*>(driven.widget("line_edit_t", "edit"))) {
                    edit->set_text("hello");
                }
            }
            (void)driven.control("radio_button_t", "radio");
            (void)driven.control("slider_t", "slider");
            (void)driven.control("scroll_bar_t", "bar");
            (void)driven.control("line_edit_t", "edit");

            auto* radio = static_cast<radio_button_t*>(driven.widget("radio_button_t", "radio"));
            radio_checked = radio != nullptr && radio->is_checked();
            auto* slider = static_cast<slider_t*>(driven.widget("slider_t", "slider"));
            slider_value = slider != nullptr ? slider->value() : -1;
            auto* bar = static_cast<scroll_bar_t*>(driven.widget("scroll_bar_t", "bar"));
            bar_value = bar != nullptr ? bar->value() : -1;
            auto* edit = static_cast<line_edit_t*>(driven.widget("line_edit_t", "edit"));
            line_text = edit != nullptr ? std::string(edit->text()) : std::string{};
        }

        void paint_once() {
            host.render(painter);
        }
    };
}  // namespace im_state_test

using namespace im_state_test;

int main() {
    std::printf("== im_state_store_t / im_object_pool_t: state outlives the object ==\n");

    // -----------------------------------------------------------------------
    // (1) The state does not travel with the object: the check box's `checked` (the property linked
    //     to its USER event) is restored by the control created on a later pass. The label is
    //     configuration, not state, so it starts from the default again.
    // -----------------------------------------------------------------------
    {
        // The check box state the test persists is a reflected writable property, not a private
        // field the store special-cases.
        const meta_property_t checked_property = check_box_t::static_meta_object.property("checked");
        CHECK(checked_property.is_valid());
        CHECK(checked_property.is_writable());
        CHECK(checked_property.is_notifiable());
        CHECK(checked_property.notifier().is_valid());
        CHECK(checked_property.notifier().name() == "toggled");
        CHECK(checked_property.notifier().param_count() == 1u);

        scripted_world_t world;
        world.stage = 0;
        world.paint_once();
        CHECK(world.text == "old");
        CHECK(world.checked);
        CHECK(world.host.children().size() == 1u);
        CHECK(world.host.pass() == 1u);

        // Two empty passes: nothing visits the control, then collect(0) deletes the object.
        world.stage = 1;
        world.paint_once();
        world.paint_once();
        CHECK(world.host.pass() == 3u);
        CHECK(world.host.children().size() == 1u); // the automatic 600-pass policy has not run
        world.host.collect(0);
        CHECK(world.host.children().empty()); // the object (and only the object) is gone
        CHECK(world.host.inner_focus() == nullptr);

        // The next pass re-creates the control; the linked state comes from the store.
        world.stage = 2;
        world.paint_once();
        CHECK(world.checked);
        CHECK(world.text.empty()); // the label is configuration: it is not in the store
        CHECK(world.host.children().size() == 1u);
    }

    // -----------------------------------------------------------------------
    // (1b) A STATE write made OUTSIDE a pass is persisted by collect() before the object is deleted:
    //      the sweep itself must never be the reason state is lost.
    // -----------------------------------------------------------------------
    {
        scripted_world_t world;
        world.stage = 0;
        world.paint_once();
        auto* box = static_cast<check_box_t*>(world.host.widget("check_box_t", "same"));
        CHECK(box != nullptr);
        box->set_checked(false); // state written after the pass ended, so no write-back has seen it
        world.stage = 1;
        world.paint_once(); // the control is not visited: no write-back for it
        world.paint_once();
        world.host.collect(0);
        CHECK(world.host.children().empty());

        world.stage = 2;
        world.paint_once();
        CHECK(!world.checked); // the collect() snapshot captured the out-of-pass write
    }

    // -----------------------------------------------------------------------
    // (2) Reuse on/off equivalence: the same interaction sequence through the pool and through
    //     per-pass objects produces identical pixels, state and signal counts.
    // -----------------------------------------------------------------------
    {
        equivalent_world_t pooled(true);
        equivalent_world_t fresh(false);
        CHECK(pooled.host.reuse_enabled());
        CHECK(!fresh.host.reuse_enabled());

        for (int pass = 0; pass < 4; ++pass) {
            if (pass == 1 || pass == 3) {
                pooled.click();
                fresh.click();
            }
            pooled.paint_once();
            fresh.paint_once();
            CHECK(same_pixels(pooled.image, fresh.image));
            CHECK(pooled.text == fresh.text);
            CHECK(pooled.checked == fresh.checked);
            CHECK(pooled.toggles == fresh.toggles);
            CHECK(pooled.host.inner_focus() == nullptr);
            CHECK(fresh.host.inner_focus() == nullptr);
            CHECK(pooled.host.captured_control() == nullptr);
            CHECK(fresh.host.captured_control() == nullptr);
        }
        CHECK(pooled.text == "eq");
        CHECK(pooled.checked == false); // toggled twice: true on pass 1, false on pass 3
        CHECK(pooled.toggles == 2);
        CHECK(fresh.toggles == 2);
        CHECK(pooled.handle.is_valid()); // the connection was real, so the counter is not vacuous

        // The switch can be flipped between passes: the live controls are persisted and discarded,
        // and the next pass restores the same state through the other path.
        pooled.host.set_reuse_enabled(false);
        CHECK(!pooled.host.reuse_enabled());
        CHECK(pooled.host.children().empty()); // objects dropped, state kept
        fresh.host.set_reuse_enabled(true);
        CHECK(fresh.host.reuse_enabled());
        CHECK(fresh.host.children().empty());
        for (int pass = 0; pass < 2; ++pass) {
            pooled.paint_once();
            fresh.paint_once();
            CHECK(same_pixels(pooled.image, fresh.image));
            CHECK(pooled.text == fresh.text);
            CHECK(pooled.checked == fresh.checked);
            CHECK(pooled.toggles == fresh.toggles);
        }
        CHECK(pooled.toggles == 2);
        CHECK(fresh.toggles == 2);
    }

    // -----------------------------------------------------------------------
    // (3) The snapshot boundary: only the property linked to the USER event is state. `checked` is
    //     writable but not linked, visible/enabled are item_t properties, geometry is not a property
    //     at all: none of them comes back.
    // -----------------------------------------------------------------------
    {
        im_state_store_t store;
        im_state_probe_t probe;
        probe.set_geometry(rect_t{ 1, 2, 30, 40 });
        probe.set_visible(false);
        probe.set_enabled(false);
        probe.set_text("kept");
        probe.set_checked(true);
        store.snapshot(probe, "probe");
        CHECK(store.contains("probe"));
        CHECK(store.size() == 1u);

        probe.set_geometry(rect_t{ 5, 6, 70, 80 });
        probe.set_visible(true);
        probe.set_enabled(true);
        probe.set_text("changed");
        probe.set_checked(false);

        CHECK(store.restore(probe, "probe"));
        CHECK(probe.text() == "kept"); // the linked property: text's notifier is the USER event
        CHECK(!probe.checked());       // writable but unlinked: configuration, not state
        CHECK(probe.geometry() == (rect_t{ 5, 6, 70, 80 })); // not a property: never stored
        CHECK(probe.is_visible());     // item_t property: the pass sets it, the store does not
        CHECK(probe.is_enabled());     // item_t property: same rule

        // The owner can forget state explicitly.
        CHECK(store.erase("probe"));
        CHECK(!store.contains("probe"));
        CHECK(!store.erase("probe"));
        CHECK(store.size() == 0u);
        CHECK(!store.restore(probe, "missing")); // an unknown id restores nothing

        // Two ids, then clear(): every entry goes, and neither id restores.
        im_state_other_t other;
        store.snapshot(probe, "p1");
        store.snapshot(other, "p2");
        CHECK(store.size() == 2u);
        store.clear();
        CHECK(store.size() == 0u);
        CHECK(!store.contains("p1"));
        CHECK(!store.contains("p2"));
        CHECK(!store.restore(probe, "p1"));
    }

    // -----------------------------------------------------------------------
    // (4) A type change under one id discards the old snapshot; the new control keeps its defaults.
    // -----------------------------------------------------------------------
    {
        im_state_store_t store;
        im_state_probe_t first;
        first.set_text("old");
        first.set_checked(true);
        store.snapshot(first, "shared");
        CHECK(store.contains("shared"));

        im_state_other_t second;
        CHECK(!store.restore(second, "shared")); // the class differs: the entry is discarded
        CHECK(!store.contains("shared"));
        CHECK(second.text().empty()); // the new control was not written

        // A later write-back records the new class, so the id is usable again.
        second.set_text("new");
        store.snapshot(second, "shared");
        CHECK(store.contains("shared"));
        CHECK(store.size() == 1u);

        // The host path: a collected check box followed by a push button under the same id.
        scripted_world_t world;
        world.stage = 0;
        world.paint_once();
        CHECK(world.text == "old");
        CHECK(world.checked);
        world.stage = 1;
        world.paint_once();
        world.host.collect(0);
        CHECK(world.host.children().empty());

        world.use_check = false;
        world.stage = 2;
        world.paint_once();
        CHECK(world.text == "Button"); // push_button_t's default, not the check box's "old"
        CHECK(!world.checked);
        CHECK(world.host.widget("push_button_t", "same") != nullptr);
        CHECK(world.host.children().size() == 1u);
    }

    // -----------------------------------------------------------------------
    // (5) The three controls that had a User event but no value property: radio_button_t::checked,
    //     slider_t::value and scroll_bar_t::value are state now, so the generic store restores them
    //     after the object is collected.
    // -----------------------------------------------------------------------
    {
        const meta_property_t radio_checked =
            radio_button_t::static_meta_object.property("checked");
        CHECK(radio_checked.is_valid());
        CHECK(radio_checked.is_writable());
        CHECK(radio_checked.notifier().name() == "toggled");
        CHECK(radio_checked.notifier().param_count() == 1u);

        const meta_property_t slider_value = slider_t::static_meta_object.property("value");
        CHECK(slider_value.is_valid());
        CHECK(slider_value.is_writable());
        CHECK(slider_value.notifier().name() == "value_changed");
        CHECK(slider_value.notifier().param_count() == 1u); // the signal, not the bool getter

        const meta_property_t bar_value = scroll_bar_t::static_meta_object.property("value");
        CHECK(bar_value.is_valid());
        CHECK(bar_value.is_writable());
        CHECK(bar_value.notifier().name() == "value_changed");
        CHECK(bar_value.notifier().param_count() == 1u);

        value_world_t values;
        values.stage = 0;
        values.paint_once();
        CHECK(values.radio_checked);
        CHECK(values.slider_value == 40);
        CHECK(values.bar_value == 60);
        CHECK(values.line_text == "hello");
        CHECK(values.host.children().size() == 4u);

        // Empty passes, then the sweep deletes all four objects.
        values.stage = 1;
        values.paint_once();
        values.paint_once();
        values.host.collect(0);
        CHECK(values.host.children().empty());

        // The next pass re-creates them: the values come back from the store, not from an object.
        values.stage = 2;
        values.paint_once();
        CHECK(values.radio_checked);
        CHECK(values.slider_value == 40);
        CHECK(values.bar_value == 60);
        CHECK(values.line_text == "hello");
        CHECK(values.host.children().size() == 4u);
    }

    // -----------------------------------------------------------------------
    // (6) The accepted clamp contract. restore() writes through set_value(), so to a freshly created
    //     control (default range 0..100) an out-of-range stored value arrives clamped. set_range()
    //     afterwards only clamps what is already there; it does not bring the original value back.
    //     A caller with a non-default range must therefore set the range and re-feed the value every
    //     pass (the external-value model). This assertion pins that decision.
    // -----------------------------------------------------------------------
    {
        im_state_store_t store;
        slider_t original;
        original.set_range(0, 500);
        original.set_value(300);
        store.snapshot(original, "range");

        slider_t restored;
        CHECK(restored.maximum() == 100); // the default range
        CHECK(store.restore(restored, "range"));
        CHECK(restored.value() == 100); // clamped by the default maximum
        restored.set_range(0, 500);
        CHECK(restored.value() == 100); // set_range only clamps the value it already sees
    }

    // -----------------------------------------------------------------------
    // (7) The metadata link itself. The store's selection is not a heuristic: the USER property is
    //     the event, and the state is the writable property whose notifier resolves to that name.
    // -----------------------------------------------------------------------
    {
        const auto check_link = [](const meta_object_t& meta, std::string_view user_name,
                                   std::string_view state_name) {
            const link_info_t info = link_of(meta);
            CHECK(info.user_present);
            CHECK(info.user_name == user_name);
            CHECK(info.states.size() == 1u); // exactly one writable property notifies the event
            CHECK(!info.states.empty() && info.states.front() == state_name);
        };
        check_link(check_box_t::static_meta_object, "toggled", "checked");
        check_link(radio_button_t::static_meta_object, "toggled", "checked");
        check_link(slider_t::static_meta_object, "value_changed", "value");
        check_link(scroll_bar_t::static_meta_object, "value_changed", "value");
        check_link(line_edit_t::static_meta_object, "text_changed", "text");

        // The command event (clicked) stays the control() result channel but no value property
        // links it: the store must record nothing for a push button.
        const link_info_t button = link_of(push_button_t::static_meta_object);
        CHECK(button.user_present); // a command USER event remains, see the report
        CHECK(button.user_name == "clicked");
        CHECK(button.states.empty());
        const meta_property_t button_property = push_button_t::static_meta_object.user_property();
        CHECK(button_property.notifier().is_valid());
        CHECK(button_property.notifier().name() == "clicked_changed");
        CHECK(button_property.notifier().param_count() == 1u);

        // The unified signal has a default argument: a no-argument call means clicked (true).
        push_button_t probe;
        bool default_clicked = false;
        probe.connect("clicked_changed(bool) const", [&default_clicked](bool clicked) {
            default_clicked = clicked;
        });
        probe.clicked_changed();
        CHECK(default_clicked);

        im_state_store_t store;
        push_button_t pushed;
        store.snapshot(pushed, "button");
        CHECK(!store.contains("button"));
        CHECK(store.size() == 0u);
        CHECK(!store.restore(pushed, "button"));
    }

    if (g_failures == 0) {
        std::printf("im_state: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("im_state: %d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
}
