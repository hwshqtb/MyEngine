/// @file input_router_t.cpp
/// @brief input_router_t: hit testing, bubbling, the implicit grab and focus (see
///        input_router_t.h for the contract).

#include "hwshqtb/ui/input_router_t.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace hwshqtb::ui {
    namespace {
        /// @brief Append the focusable items of @p item's subtree in depth-first pre-order order.
        /// @param item The subtree root.
        /// @param chain The chain to append to (parents before children, children in child order).
        /// @note Focus is an item property, so there is NO is_widget() filter on the PUSH: a widget
        ///       and an immediate host are peers in the chain.
        /// @note Only a widget is a traversal CONTAINER: an immediate host is a leaf item whose
        ///       children are its internal controls, so the outer chain does not descend into it.
        void collect_focus_chain(item_t& item, std::vector<item_t*>& chain) {
            if (!item.is_visible() || !item.is_enabled()) {
                return; // an invisible/disabled item hides its whole subtree from the Tab chain
            }
            if (item.is_focusable()) {
                chain.push_back(&item);
            }
            if (!item.is_widget()) {
                return; // a leaf item (ui::im_host_t): its children belong to its own focus level
            }
            for (std::size_t index = 0; index < item.item_count(); ++index) {
                if (item_t* const child = item.item_at_index(index)) {
                    collect_focus_chain(*child, chain);
                }
            }
        }

        /// @brief The parent item of @p item, or nullptr when the object parent is not an item.
        item_t* parent_item_of(const item_t& item) {
            ::hwshqtb::metaobject::object_t* const parent = item.parent();
            if (parent == nullptr || !parent->is_item()) {
                return nullptr;
            }
            return static_cast<item_t*>(parent);
        }
    }

    input_router_t::input_router_t(item_t& root):
        _root(&root) {
        // item_t asks, the router is the authority: a focus request walks up to this sink, and a
        // capture request goes through the pointer sink. The item-side state (has_focus() /
        // captures_pointer()) is what every view reads back, so the sinks only record the answer.
        _root->set_focus_sink([this](item_t* item) {
            set_focus(item);
        });
        _root->set_pointer_sink([this](item_t* item) {
            set_pointer_target(item);
        });
    }

    input_router_t::~input_router_t() {
        if (_focus.data() != nullptr) {
            set_focus(nullptr); // one focus lost event while the tree is still alive
        }
        release_pointer_target(); // give the pointer back while the items are still alive
        if (_root != nullptr) {
            _root->set_focus_sink({});
            _root->set_pointer_sink({});
        }
        _root = nullptr;
    }

    bool input_router_t::dispatch(const ::hwshqtb::gui::interaction_t& in) {
        if (_root == nullptr) {
            return false;
        }

        // A focus that stopped being focusable (hidden, disabled, policy lowered) must not survive the
        // next input: drop it now, which dispatches its focus lost event exactly once.
        if (item_t* const focused = _focus.data(); focused != nullptr && !focused->is_focusable()) {
            set_focus(nullptr);
        }

        // ---- key and text-input steps ------------------------------------------------------
        // Neither is positional: a key is delivered to the focus ITEM and a text-input step (key == 0,
        // no mouse edges) is the typed text for that same focus item. Delivering the text by hit
        // testing would send typing wherever the pointer happens to be, which is wrong.
        if (in.key != 0 || !in.text_input.empty()) {
            // 1. The focus ITEM gets the step first — including Tab, which it may consume.
            item_t* const target = _focus.data();
            if (target != nullptr) {
                ::hwshqtb::gui::interaction_t copy = in;
                copy.mouse = target->map_from_global(in.mouse);
                copy.hovered = target->rect().contains(copy.mouse);
                if (target->interact(copy)) {
                    return true;
                }
            }
            // 2. Tab is the router's fallback: move the focus only when the focus item did not consume.
            if (in.key_pressed && in.key == ::hwshqtb::gui::interaction_t::tab_key) {
                if (::hwshqtb::gui::has_modifier(in.modifiers, ::hwshqtb::gui::modifier_flag::SHIFT)) {
                    focus_previous();
                }
                else {
                    focus_next();
                }
                return true;
            }
            return false;
        }

        // ---- mouse steps -------------------------------------------------------------------
        // The capture target wins over the hit test: while an item owns the pointer, every step is
        // delivered to it even when the pointer left its rectangle. The truth is the ITEM's own flag
        // (captures_pointer()), so a target that gave the pointer back — or died, which nulls the
        // guarded pointer — drops out here instead of routing to a ghost.
        if (item_t* const captured = _pointer.data();
            captured != nullptr && !captured->captures_pointer()) {
            _pointer.clear();
        }
        // The hit test still runs while a capture is held: hover follows the POINTER (where it is),
        // while delivery follows the CAPTURE (who owns it).
        item_t* const hover_target = hit_test(in.mouse);
        item_t* const start = _pointer.data() != nullptr ? _pointer.data() : hover_target;

        // A press edge moves the focus BEFORE the delivery below, so the pressed control already sees
        // has_focus() == true in its own interact(). Focusing consumes nothing: the same press is still
        // routed (and may bubble) as usual, and a press without a click-focusable target leaves the
        // current focus untouched.
        if (in.mouse_pressed) {
            focus_on_press(hover_target);
        }

        // Hover is ITEM state: the old target is cleared before the new one is set, and only a real
        // change costs a notification (the synthetic interaction below, or this step's own delivery).
        if (item_t* const previous = _hovered.data(); previous != hover_target) {
            if (previous != nullptr) {
                previous->set_hovered(false);
                if (previous != start) {
                    send_hover_out(*previous, in);
                }
            }
            _hovered = hover_target;
            if (hover_target != nullptr) {
                hover_target->set_hovered(true);
            }
        }

        if (start == nullptr) {
            return false; // outside the root, and nothing captured
        }

        // Press and release are recorded on the DELIVERY target before the step reaches it, so the
        // item sees the current state in its own interact() and every view paints the same feedback.
        if (in.mouse_pressed) {
            start->set_pressed(in.button, true);
            // Qt's implicit grab: the press takes the pointer. The request goes through the item, so
            // the item-side captures_pointer() stays the one truth the sink records.
            (void)start->capture_pointer();
        }
        if (in.mouse_released) {
            start->set_pressed(in.button, false);
        }

        ::hwshqtb::gui::point_t local = start->map_from_global(in.mouse);
        bool consumed = false;
        for (item_t* item = start; item != nullptr; item = parent_item_of(*item)) {
            ::hwshqtb::gui::interaction_t copy = in;
            copy.mouse = local;
            // hovered is per delivered item: true exactly where the pointer is inside it, so a
            // captured item that the pointer left sees hovered == false.
            copy.hovered = item->rect().contains(local);
            if (item->interact(copy)) {
                consumed = true;
                break; // first consumer stops the chain
            }
            if (item == _root) {
                break;
            }
            local += item->geometry().top_left(); // the parent's coordinate system
        }

        if (in.mouse_released && !in.mouse_down) {
            // The last held button is gone: the implicit grab ends. An item that wants to keep the
            // pointer must ask again (capture_pointer()) from its own release handling.
            release_pointer_target();
        }
        return consumed;
    }

    void input_router_t::set_focus(item_t* item) {
        if (item != nullptr && (!item->is_focusable() || !is_in_tree(*item))) {
            item = nullptr; // cannot hold focus (or belongs to another tree): clearing is the answer
        }
        item_t* const previous = _focus.data();
        if (previous == item) {
            return; // no change, so no focus_in/focus_out: exactly one pair per real change
        }
        if (previous != nullptr) {
            _focus.clear();
            previous->set_has_focus(false); // dispatches the focus lost event
        }
        if (item != nullptr) {
            _focus = item;
            item->set_has_focus(true); // dispatches the focus gained event
        }
    }

    item_t* input_router_t::focus_item() const {
        return _focus.data();
    }

    void input_router_t::focus_next() {
        focus_step(+1);
    }

    void input_router_t::focus_previous() {
        focus_step(-1);
    }

    item_t* input_router_t::mouse_target() const {
        return _pointer.data();
    }

    void input_router_t::clear_pointer_state() {
        if (item_t* const captured = _pointer.data()) {
            clear_pressed(*captured);
        }
        release_pointer_target();
        if (item_t* const hovered = _hovered.data()) {
            _hovered.clear();
            hovered->set_hovered(false);
        }
    }

    item_t* input_router_t::hit_test(::hwshqtb::gui::point_t root_point) const {
        if (_root == nullptr || !_root->is_visible() || !_root->is_enabled()) {
            return nullptr;
        }
        const ::hwshqtb::gui::point_t local = _root->map_from_global(root_point);
        if (!_root->rect().contains(local)) {
            return nullptr; // outside the root: nothing in this tree owns the point
        }
        // hit() already descends in reverse child order, skips invisible/disabled subtrees and
        // returns the deepest hit item; nullptr here means "the point is on the root itself".
        item_t* const deeper = _root->hit(local);
        return deeper != nullptr ? deeper : _root;
    }

    bool input_router_t::is_in_tree(const item_t& item) const {
        for (const item_t* current = &item; current != nullptr; current = parent_item_of(*current)) {
            if (current == _root) {
                return true;
            }
        }
        return false;
    }

    void input_router_t::focus_step(int delta) {
        if (_root == nullptr) {
            return;
        }
        std::vector<item_t*> chain;
        collect_focus_chain(*_root, chain);
        if (chain.empty()) {
            set_focus(nullptr); // nothing can hold focus
            return;
        }
        const auto found = std::find(chain.begin(), chain.end(), _focus.data());
        if (found == chain.end()) {
            // No focus yet (or it just stopped being focusable): enter the chain at the matching end.
            set_focus(delta > 0 ? chain.front() : chain.back());
            return;
        }
        const std::size_t count = chain.size();
        const std::size_t index = static_cast<std::size_t>(found - chain.begin());
        const std::size_t next = (delta > 0) ? (index + 1) % count : (index + count - 1) % count;
        set_focus(chain[next]); // wraps; a one-element chain is a no-op (same widget, no events)
    }

    void input_router_t::focus_on_press(item_t* target) {
        for (item_t* item = target; item != nullptr; item = parent_item_of(*item)) {
            if (accepts_click_focus(*item)) {
                item->set_focus(); // asks the nearest focus sink, which is this router's root
                return;
            }
        }
    }

    void input_router_t::send_hover_out(item_t& item, const ::hwshqtb::gui::interaction_t& in) {
        ::hwshqtb::gui::interaction_t out;
        out.mouse = item.map_from_global(in.mouse);
        out.hovered = false;
        out.modifiers = in.modifiers;
        // No buttons and no edges: this is a hover notification, not an event. The return value is
        // deliberately ignored — a hover change is not a consumption.
        (void)item.interact(out);
    }

    void input_router_t::set_pointer_target(item_t* item) {
        if (item == nullptr) {
            _pointer.clear(); // the capture target gave the pointer back
            return;
        }
        // One owner at a time: a new capture supersedes the previous target, and the previous item's
        // own flag drops with it (the item is the truth, this pointer only the delivery record).
        if (item_t* const previous = _pointer.data(); previous != nullptr && previous != item) {
            _pointer.clear();
            if (previous->captures_pointer()) {
                previous->release_pointer();
            }
        }
        _pointer = item;
    }

    void input_router_t::release_pointer_target() {
        item_t* const target = _pointer.data();
        _pointer.clear();
        if (target != nullptr && target->captures_pointer()) {
            target->release_pointer(); // clears the item-side truth; the sink call re-clears this
        }
    }

    void input_router_t::clear_pressed(item_t& item) {
        for (const ::hwshqtb::gui::mouse_button_enum button : {
                 ::hwshqtb::gui::mouse_button_enum::LEFT,
                 ::hwshqtb::gui::mouse_button_enum::MIDDLE,
                 ::hwshqtb::gui::mouse_button_enum::RIGHT }) {
            item.set_pressed(button, false);
        }
    }
}
