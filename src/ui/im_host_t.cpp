/// @file im_host_t.cpp
/// @brief im_host_t: the per-pass replay, the control pool and the group stack (see im_host_t.h).

#include "hwshqtb/ui/im_host_t.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/core_application.h"
#include "hwshqtb/metaobject/log.h"
#include "hwshqtb/metaobject/meta_object_t.h"
#include "hwshqtb/metaobject/meta_property_t.h"
#include "hwshqtb/metaobject/object_pointer_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/ui/group_stack_t.h"
#include "hwshqtb/ui/im_object_pool_t.h"
#include "hwshqtb/ui/im_pass_t.h"
#include "hwshqtb/ui/im_state_store_t.h"
#include "hwshqtb/ui/widget_t.h"

namespace hwshqtb::ui {
    namespace {
        /// @brief The log category of the ui layer's immediate host.
        /// @note Non-const on purpose: the category registers itself with the filter registry, and an
        ///       installed category filter receives a MUTABLE reference (it adjusts the levels).
        ::hwshqtb::metaobject::log_category_t log_ui("hwshqtb.ui");
    }

    im_host_t::im_host_t() {
        _pass.groups().reset(rect().width());
        // A focus request from an internal control stops at the host boundary: only a DIRECT object
        // child of this host that is a REGISTERED control and is focusable may become the inner
        // focus; anything else (including nullptr) clears it. The router's item chain is never
        // involved.
        set_focus_sink([this](item_t* item) {
            widget_t* const candidate =
                (item != nullptr && item->is_widget() && item->parent() == this)
                    ? static_cast<widget_t*>(item)
                    : nullptr;
            const bool registered = candidate != nullptr && is_control(*candidate);
            sync_inner_focus(
                (registered && candidate->is_focusable()) ? candidate : nullptr);
        });
        // The host is the inner pointer session: a control's capture_pointer() stops here, and the
        // single-owner rule is enforced on the item state the control already wrote. A request from
        // anything that is not one of this host's controls is refused (the item gives it back).
        set_pointer_sink([this](item_t* item) {
            if (item == nullptr) {
                return; // a release: the control already cleared its own flag
            }
            if (!item->is_widget() || item->parent() != this) {
                return;
            }
            auto* const requested = static_cast<widget_t*>(item);
            if (!is_control(*requested)) {
                requested->release_pointer(); // not a host control: give the pointer back
                return;
            }
            // The requested control's flag is already set, so it must not be searched for: collect
            // every OTHER owner first and release it. (Querying captured_control() here could return
            // the new owner and leave the old one armed, because the pool order is unspecified.)
            std::vector<widget_t*> superseded;
            for_each_control([requested, &superseded](std::string_view,
                                                      ::hwshqtb::metaobject::object_t& object) {
                if (!object.is_widget()) {
                    return;
                }
                auto* const control = static_cast<widget_t*>(&object);
                if (control != requested && control->captures_pointer()) {
                    superseded.push_back(control);
                }
            });
            for (widget_t* const control : superseded) {
                control->release_pointer(); // one owner: the new capture supersedes the old
            }
        });
    }

    im_host_t::~im_host_t() = default;

    bool im_host_t::is_control(const widget_t& control) const noexcept {
        if (_pool.contains(control)) {
            return true;
        }
        for (const auto& entry : _volatile) {
            if (entry.second.data() == &control) {
                return true;
            }
        }
        return false;
    }

    widget_t* im_host_t::inner_focus() const noexcept {
        widget_t* focused = nullptr;
        for_each_control([&focused](std::string_view, ::hwshqtb::metaobject::object_t& object) {
            if (focused != nullptr || !object.is_widget()) {
                return;
            }
            auto* const control = static_cast<widget_t*>(&object);
            if (control->has_focus()) {
                focused = control;
            }
        });
        return focused;
    }

    widget_t* im_host_t::captured_control() const noexcept {
        widget_t* captured = nullptr;
        for_each_control([&captured](std::string_view, ::hwshqtb::metaobject::object_t& object) {
            if (captured != nullptr || !object.is_widget()) {
                return;
            }
            auto* const control = static_cast<widget_t*>(&object);
            if (control->captures_pointer()) {
                captured = control;
            }
        });
        return captured;
    }

    void im_host_t::sync_inner_focus(widget_t* next) {
        // Clear every control except the target first: a real change fires focus_out before
        // focus_in, and a stray second holder cannot survive. The controls' has_focus() is the one
        // store; there is no member to keep in step.
        std::vector<widget_t*> losing;
        for_each_control([next, &losing](std::string_view, ::hwshqtb::metaobject::object_t& object) {
            if (!object.is_widget()) {
                return;
            }
            auto* const control = static_cast<widget_t*>(&object);
            if (control != next && control->has_focus()) {
                losing.push_back(control);
            }
        });
        for (widget_t* const control : losing) {
            control->set_has_focus(false); // fires focus_out_event()
        }
        if (next != nullptr && !next->has_focus()) {
            next->set_has_focus(true); // fires focus_in_event()
        }
    }

    void im_host_t::set_captured(widget_t* next) {
        widget_t* const previous = captured_control();
        if (previous == next) {
            return; // not a real change: exactly one owner at a time
        }
        if (previous != nullptr) {
            previous->release_pointer(); // clears the item state and tells the host sink
        }
        if (next != nullptr) {
            (void)next->capture_pointer(); // sets the item state and tells the host sink
        }
    }

    bool im_host_t::event(::hwshqtb::metaobject::event_t* event) {
        // The dispatch is item_t's, verbatim: every ui type reaches the same handler an item would run,
        // with no forwarded or deferred step in between — an immediate host's pass IS its frame, so a
        // state change must not wait for a later one. Only the handlers below add host behaviour.
        return item_t::event(event);
    }

    void im_host_t::focus_change_event(focus_change_event_t* event) {
        if (!event->has_focus()) {
            sync_inner_focus(nullptr); // the whole inner level loses the focus with the host
        }
    }

    void im_host_t::geometry_change_event(geometry_change_event_t*) {
        _pass.groups().set_root_width(rect().width()); // a group keeps its own fixed width
    }

    void im_host_t::visible_change_event(visible_change_event_t* event) {
        if (!event->visible()) {
            _pass.clear_input(); // a hidden host paints no pass, so the batch is dropped
        }
    }

    void im_host_t::enabled_change_event(enabled_change_event_t* event) {
        if (!event->enabled()) {
            _pass.clear_input(); // a disabled host must not replay a batch collected before it
        }
    }

    std::string im_host_t::key_of(std::string_view object_name) const {
        return _pass.groups().key_of(object_name);
    }

    std::vector<widget_t*> im_host_t::focus_chain() const {
        const auto& previous = _pass.previous();
        std::vector<widget_t*> chain;
        chain.reserve(previous.size());
        for (const auto& guard : previous) {
            widget_t* const control = guard.data();
            if (control != nullptr && is_control(*control) && control->is_focusable()) {
                chain.push_back(control);
            }
        }
        return chain;
    }

    widget_t* im_host_t::tab_target(bool backwards, const std::vector<widget_t*>& chain) const {
        if (chain.empty()) {
            return nullptr;
        }
        widget_t* const current = inner_focus();
        if (current == nullptr) {
            return backwards ? chain.back() : chain.front();
        }
        const auto found = std::find(chain.begin(), chain.end(), current);
        if (found == chain.end()) {
            return backwards ? chain.back() : chain.front(); // stale focus: enter at the near end
        }
        const std::size_t index = static_cast<std::size_t>(found - chain.begin());
        if (backwards) {
            return index == 0 ? nullptr : chain[index - 1];
        }
        return index + 1 < chain.size() ? chain[index + 1] : nullptr;
    }

    bool im_host_t::accepts_key(const ::hwshqtb::gui::interaction_t& in) const {
        if (in.key == ::hwshqtb::gui::interaction_t::tab_key) {
            if (!in.key_pressed) {
                return false; // a Tab release performs no move
            }
            const bool backwards =
                ::hwshqtb::gui::has_modifier(in.modifiers, ::hwshqtb::gui::modifier_flag::SHIFT);
            return tab_target(backwards, focus_chain()) != nullptr;
        }
        widget_t* const focused = inner_focus();
        return focused != nullptr && focused->is_focusable();
    }

    void im_host_t::handle_tab() {
        bool tab = false;
        bool backwards = false;
        for (std::size_t index = 0; index < _pass.input_count();) {
            const ::hwshqtb::gui::interaction_t& in = _pass.input(index).input;
            if (in.key == ::hwshqtb::gui::interaction_t::tab_key && (in.key_pressed || in.key_released)) {
                if (in.key_pressed) {
                    tab = true;
                    backwards = ::hwshqtb::gui::has_modifier(in.modifiers, ::hwshqtb::gui::modifier_flag::SHIFT);
                }
                _pass.erase_input(index);
            }
            else {
                ++index;
            }
        }
        if (!tab) {
            return;
        }
        if (widget_t* const next = tab_target(backwards, focus_chain())) {
            sync_inner_focus(next);
        }
    }

    ::hwshqtb::gui::interaction_t im_host_t::settle_input(
        const widget_t& control, ::hwshqtb::gui::point_t group_origin) const {
        ::hwshqtb::gui::interaction_t in;
        in.mouse = _pass.last_mouse() - group_origin - control.geometry().top_left();
        in.hovered = control.rect().contains(in.mouse);
        in.mouse_down = _pass.last_mouse_down();
        in.modifiers = _pass.last_modifiers();
        return in;
    }

    ::hwshqtb::gui::interaction_t im_host_t::make_input(
        const widget_t& control, ::hwshqtb::gui::point_t group_origin, im_pass_t::step_t& step) {
        const ::hwshqtb::gui::interaction_t& event = step.input;
        ::hwshqtb::gui::interaction_t in;
        in.mouse = event.mouse - group_origin - control.geometry().top_left();
        const bool inside = control.rect().contains(in.mouse);
        in.hovered = inside && !step.claimed;
        if (inside) {
            step.claimed = true; // first visited control under the pointer wins the event
        }
        widget_t* const captured = captured_control();
        widget_t* const focused = inner_focus();
        in.button = event.button;
        in.mouse_down = (captured == nullptr || captured == &control) && event.mouse_down;
        in.mouse_pressed = in.hovered && captured == nullptr && event.mouse_pressed;
        in.mouse_released = (captured == &control || (captured == nullptr && in.hovered))
            && event.mouse_released;
        in.wheel = event.wheel;
        in.text_input = event.text_input;
        in.modifiers = event.modifiers;
        if (focused == &control) {
            in.key = event.key;
            in.key_pressed = event.key_pressed;
            in.key_released = event.key_released;
        }
        return in;
    }

    void im_host_t::mirror_pointer_state(widget_t& control, const ::hwshqtb::gui::interaction_t& in,
                                         bool consumed) {
        control.set_hovered(in.hovered);
        if (in.mouse_pressed) {
            control.set_pressed(in.button, consumed);
        }
        if (in.mouse_released) {
            control.set_pressed(in.button, false);
        }
    }

    ::hwshqtb::gui::rect_t im_host_t::degenerate_geometry(const widget_t& control) const {
        const group_stack_t::layer_t& current = _pass.groups().layer();
        return ::hwshqtb::gui::rect_t{ current.cursor.x(), current.cursor.y(), current.width,
                                        std::max(1, control.size_hint().height()) };
    }

    widget_t* im_host_t::acquire(std::string_view type_name, std::string_view object_name) {
        if (object_name.empty()) {
            HWSHQTB_LOG_RAW(log_ui, ::hwshqtb::metaobject::log_level_enum::WARNING,
                "ui::im_host_t: object_name must not be empty; no control was created");
            return nullptr;
        }
        const std::string key = key_of(object_name);
        const bool is_new = !live_control(key);
        if (is_new) {
            _layout.erase(key); // no live control: its layout facts are stale (a dead id starts over)
        }

        auto create = [this, type_name, &key]() -> ::hwshqtb::metaobject::object_t* {
            ::hwshqtb::metaobject::object_t* const created =
                ::hwshqtb::metaobject::object_t::create(type_name, this);
            if (created == nullptr) {
                return nullptr; // create() logged the reason (unknown name / not constructible)
            }
            if (!created->is_widget()) {
                HWSHQTB_LOG_RAW(log_ui, ::hwshqtb::metaobject::log_level_enum::WARNING,
                    "ui::im_host_t: '" + std::string(type_name)
                        + "' is not a widget; the created object was deleted");
                delete created;
                return nullptr;
            }
            auto* const control = static_cast<widget_t*>(created);
            control->set_object_name(key);
            control->set_geometry(degenerate_geometry(*control));
            return control;
        };

        ::hwshqtb::metaobject::object_t* object = nullptr;
        if (_reuse) {
            object = _pool.take(key, _pass_count, create);
        }
        else {
            const auto found = _volatile.find(key);
            if (found != _volatile.end()) {
                object = found->second.data();
            }
            else {
                object = create();
                if (object != nullptr) {
                    _volatile.emplace(key, static_cast<widget_t*>(object));
                }
            }
        }
        if (object == nullptr) {
            _layout.erase(key); // a failed creation leaves no layout facts either
            return nullptr;
        }

        auto [layout, inserted] = _layout.try_emplace(key);
        if (inserted) {
            layout->second.type_name = std::string(type_name);
        }
        else if (layout->second.type_name != type_name) {
            HWSHQTB_LOG_RAW(log_ui, ::hwshqtb::metaobject::log_level_enum::WARNING,
                "ui::im_host_t: object_name '" + key + "' already names a '"
                    + layout->second.type_name + "'; keeping the first type (requested '"
                    + std::string(type_name) + "')");
        }
        // Seed a NEW object from the store. An object that already exists keeps its state, including
        // a write its caller made between passes: that is the state the store would hand back anyway,
        // and overwriting it would silently undo the caller's change.
        if (is_new) {
            _store.restore(*object, key);
        }
        (void)_pass.note_access(key, *static_cast<widget_t*>(object));
        return static_cast<widget_t*>(object);
    }

    void im_host_t::render(::hwshqtb::gui::painter_t& painter, const ::hwshqtb::gui::region_t& damage) {
        if (!is_visible()) {
            _pass.clear_input(); // nothing was painted, so nothing may stay queued
            return;
        }
        // A control that stopped being focusable must not keep the inner focus (one focus_out).
        if (widget_t* const focused = inner_focus();
            focused != nullptr && !focused->is_focusable()) {
            sync_inner_focus(nullptr);
        }

        handle_tab();

        // The host's damage in its own coordinates: the incoming region narrowed by the clip and by its
        // rectangle, exactly the area the painter is about to clip to — that is also what the event
        // carries once it reaches a widget.
        ::hwshqtb::gui::region_t local = damage;
        local.intersect(painter.clip_rect());
        local.intersect(geometry());
        {
            const ::hwshqtb::gui::point_t origin = geometry().top_left();
            local = local.translated(-origin.x(), -origin.y());
            local.intersect(rect());
        }

        painter.save();
        painter.translate(geometry().top_left());
        painter.set_clip(rect());

        // The host owns no pixels: it runs the user's process, and the controls that process acquires
        // paint themselves through their own control() calls below. There is nothing of its own to hand
        // to another paint event.
        ++_pass_count;
        _pass.begin(rect().width());
        _painter = &painter;
        if (_handler) {
            _handler(*this);
        }
        _painter = nullptr;

        painter.restore();

        write_back(); // one snapshot per acquired control, while the objects are still alive
        if (!_reuse) {
            drop_volatile_controls(); // reuse is off: the pass's objects go away, their state stays
        }
        _pass.end();
        if (_collect_enabled) {
            collect(_collect_max);
        }
        // The host owns no damage record: it is an item_t, not a widget — it only drives the user's
        // process and paints the controls that process acquires, so there is nothing of its own to clear.
    }

    item_t* im_host_t::hit(::hwshqtb::gui::point_t local) const {
        if (!is_visible() || !is_enabled()) {
            return nullptr;
        }
        // A host is a leaf: its controls are not items of the parent's tree.
        return rect().contains(local) ? const_cast<im_host_t*>(this) : nullptr;
    }

    bool im_host_t::interact(const ::hwshqtb::gui::interaction_t& in) {
        if (!is_visible() || !is_enabled()) {
            return false;
        }
        if (in.key != 0) {
            // I1: the decision is a PURE QUERY of the previous pass's focusable chain. A key the host
            // cannot act on is not collected at all, so the router keeps it (that is how Tab falls
            // back when the host's own chain ends).
            if (!accepts_key(in)) {
                return false;
            }
            _pass.push(in);
            // The batch is replayed by the host's own paint pass, so a consumed step has to make sure
            // that pass happens: a partial repaint would otherwise skip the frame (nothing else was
            // damaged) and the collected click would never reach its control.
            request_frame();
            return true;
        }
        _pass.record_pointer(in);
        _pass.push(in); // I1: collect only, no control state is touched here
        request_frame();
        return true;
    }

    void im_host_t::set_frame_handler(std::function<void(im_host_t&)> handler) {
        _handler = std::move(handler);
    }

    void im_host_t::request_frame() {
        for (::hwshqtb::metaobject::object_t* current = parent(); current != nullptr;
             current = current->parent()) {
            if (current->is_widget()) {
                auto* const widget = static_cast<widget_t*>(current);
                // Damage the HOST's rectangle only, in the widget's coordinates: the request is about
                // this host, while widget_t::update() would claim the widget's whole client area.
                const ::hwshqtb::gui::point_t offset =
                    map_to_global(::hwshqtb::gui::point_t{})
                    - widget->map_to_global(::hwshqtb::gui::point_t{});
                widget->update(rect().translated(offset));
                return;
            }
        }
    }

    std::uint64_t im_host_t::pass() const noexcept {
        return _pass_count;
    }

    ::hwshqtb::metaobject::any_t im_host_t::control(std::string_view type_name,
                                                    std::string_view object_name) {
        widget_t* const control = acquire(type_name, object_name);
        if (control == nullptr) {
            return ::hwshqtb::metaobject::any_t{};
        }
        const std::string key = key_of(object_name);
        const auto found = _layout.find(key);
        if (found == _layout.end()) {
            return ::hwshqtb::metaobject::any_t{}; // acquire() always records a live control
        }
        layout_entry_t& layout = found->second;
        layout.group_origin = _pass.groups().layer().origin;

        // Place the control (I3). set_geometry() requests a repaint on every call, so it runs only on a
        // real move: a control whose row did not change must not damage itself and its ancestors on an
        // idle pass, or the host would report damage forever and schedule a frame every pass.
        ::hwshqtb::gui::rect_t target = alloc_row(std::max(1, control->effective_size_hint().height()));
        if (control->has_fixed_size()) {
            // The host is a layout too: a fixed control keeps BOTH axes at the row origin.
            const ::hwshqtb::gui::extent_t fixed = control->fixed_size();
            target = ::hwshqtb::gui::rect_t{ target.x(), target.y(), fixed.width(), fixed.height() };
        }
        if (!(control->geometry() == target)) {
            control->set_geometry(target);
        }

        // A pass with no collected event still has to settle the control's per-frame state (the
        // retained path calls interact() once per frame). A pass with events replays ONLY the batch,
        // so the delivered call sequence is exactly the router's.
        if (_pass.input_count() == 0) {
            const ::hwshqtb::gui::interaction_t settled = settle_input(*control, layout.group_origin);
            (void)control->interact(settled);
            mirror_pointer_state(*control, settled, false);
        }
        for (std::size_t index = 0; index < _pass.input_count(); ++index) {
            const ::hwshqtb::gui::interaction_t in =
                make_input(*control, layout.group_origin, _pass.input(index));
            // The press edge moves the inner focus BEFORE the control's own interact(), so the control
            // already sees has_focus() == true in this same pass. sync_inner_focus() stays the ONE
            // writer of the inner focus; a press on a control that cannot take click focus changes
            // nothing.
            if (in.mouse_pressed && accepts_click_focus(*control)) {
                sync_inner_focus(control);
            }
            const bool consumed = control->interact(in);
            if (in.mouse_pressed && consumed) {
                set_captured(control);
            }
            if (in.mouse_released && captured_control() == control) {
                set_captured(nullptr);
            }
            // The host's inner hover/press is mirrored into the control's ITEM state before it is
            // painted, so every output paints the state the host actually acted on.
            mirror_pointer_state(*control, in, consumed);
        }

        if (_painter != nullptr) {
            // I2: consumption and paint are back to back. The control is painted through the framework
            // entry (render), which localises the painter, runs the control's own paint_event and clears
            // the control's damage record — exactly what the traversal does in a retained tree.
            control->render(*_painter);
        }
        _pass.visit(*control);

        // The frame's result: the class's User property. A control WITHOUT one still ran this pass,
        // so it answers the valid, declared-only void any_t ("driven, nothing to report") — that is a
        // success. Only acquire()'s failures above are the invalid, default-constructed state.
        const ::hwshqtb::metaobject::meta_property_t user = control->meta_object()->user_property();
        if (!user.is_valid()) {
            return ::hwshqtb::metaobject::any_t(
                ::hwshqtb::metaobject::meta_type_t::from_type<void>());
        }
        return user.read(*control);
    }

    widget_t* im_host_t::widget(std::string_view type_name, std::string_view object_name) {
        return acquire(type_name, object_name);
    }

    void im_host_t::collect(std::uint32_t max_idle_passes) {
        widget_t* const focused = inner_focus();
        // Persist what the live controls hold before the sweep. A write made outside a pass (between
        // two frames) would otherwise be lost together with the object it was made on.
        for_each_control([this](std::string_view id, ::hwshqtb::metaobject::object_t& object) {
            _store.snapshot(object, id);
        });
        const std::vector<::hwshqtb::metaobject::object_t*> dropped =
            _pool.collect(_pass_count, max_idle_passes);
        for (::hwshqtb::metaobject::object_t* const object : dropped) {
            if (object == focused) {
                // The focused control is going away: one focus_out before the delete, exactly like a
                // real focus change (the pool has already dropped the row, so this is the one writer
                // that must finish the item state first).
                static_cast<widget_t*>(object)->set_has_focus(false);
            }
            // Capture needs no equivalent step: it has no event, and the control's own flag dies with
            // the object, so captured_control() cannot find it once the row is gone.
            delete object; // the object detaches itself from this host; its state stays in the store
        }
        for (auto current = _layout.begin(); current != _layout.end();) {
            if (!live_control(current->first)) {
                current = _layout.erase(current); // the control is gone: its layout facts go with it
            }
            else {
                ++current;
            }
        }
    }

    bool im_host_t::live_control(std::string_view id) const {
        if (_pool.find(id) != nullptr) {
            return true;
        }
        const auto found = _volatile.find(std::string(id));
        return found != _volatile.end() && found->second.data() != nullptr;
    }

    void im_host_t::write_back() {
        for (const im_pass_t::access_t& access : _pass.accesses()) {
            widget_t* const control = access.control.data();
            if (control != nullptr) {
                _store.snapshot(*control, access.id);
            }
        }
    }

    void im_host_t::drop_volatile_controls() {
        for (auto& [id, control] : _volatile) {
            widget_t* const object = control.data();
            if (object != nullptr) {
                if (object->has_focus()) {
                    object->set_has_focus(false); // one focus_out, like collect()
                }
                delete object;
            }
            _layout.erase(id); // a per-pass object leaves no layout facts behind either
        }
        _volatile.clear();
    }

    void im_host_t::drop_all_controls() {
        std::vector<::hwshqtb::metaobject::object_t*> controls;
        for_each_control([&controls](std::string_view, ::hwshqtb::metaobject::object_t& object) {
            controls.push_back(&object);
        });
        for (::hwshqtb::metaobject::object_t* const object : controls) {
            if (object->is_widget() && static_cast<widget_t*>(object)->has_focus()) {
                static_cast<widget_t*>(object)->set_has_focus(false);
            }
            delete object;
        }
        _pool.clear();
        _volatile.clear();
        _layout.clear();
    }

    void im_host_t::set_reuse_enabled(bool enabled) {
        if (_reuse == enabled) {
            return;
        }
        if (_pass.active()) {
            HWSHQTB_LOG_RAW(log_ui, ::hwshqtb::metaobject::log_level_enum::WARNING,
                "ui::im_host_t: set_reuse_enabled() must not be called from the frame handler; "
                "the request was ignored");
            return;
        }
        // Persist first, then discard the objects: the state is what the next pass restores, so the
        // mode change itself is invisible.
        for_each_control([this](std::string_view id, ::hwshqtb::metaobject::object_t& object) {
            _store.snapshot(object, id);
        });
        drop_all_controls();
        _reuse = enabled;
    }

    bool im_host_t::reuse_enabled() const noexcept {
        return _reuse;
    }

    void im_host_t::set_cursor(::hwshqtb::gui::point_t cursor) {
        _pass.groups().set_cursor(cursor);
    }

    ::hwshqtb::gui::point_t im_host_t::cursor() const noexcept {
        return _pass.groups().cursor();
    }

    ::hwshqtb::gui::rect_t im_host_t::alloc_row(int height) {
        return _pass.groups().alloc_row(height);
    }

    void im_host_t::begin_group(::hwshqtb::gui::rect_t area, std::string_view id_prefix) {
        if (_painter == nullptr) {
            return; // groups exist only inside the frame handler, where the painter is valid
        }
        _painter->save();
        // Clip first, in the parent layer's coordinates, then move to the group origin: set_clip()
        // interprets the rectangle in the CURRENT transform, so clipping after the translation would
        // offset the rectangle twice.
        _painter->set_clip(area);
        _painter->translate(area.top_left());
        _pass.groups().begin_group(area, id_prefix);
    }

    void im_host_t::end_group() {
        if (!_pass.groups().end_group()) {
            return; // the host's own layer cannot be popped
        }
        if (_painter != nullptr) {
            _painter->restore(); // balances begin_group()'s save()
        }
    }

    META_OBJECT(im_host_t);
}
