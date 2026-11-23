#ifndef HWSHQTB__UI__INPUT_ROUTER_T_H
#define HWSHQTB__UI__INPUT_ROUTER_T_H

/// @file input_router_t.h
/// @brief input_router_t: the retained host's event routing (stage W3) — hit testing, bubbling, the
///        single pointer capture, keyboard focus (a click or the Tab chain) and the item-level hover.
///
/// @par Why a plain class
/// Routing is logic, not a window: it must be testable headless, exactly like the layouts. The router
/// therefore knows nothing about the platform, events or windows — it takes one interaction_t and
/// delivers it. widget_window_t is only a thin ADAPTER: its interact() landing point receives the
/// snapshot the window built (window_t::interact_event()) and calls dispatch(). A test builds the same
/// snapshot by hand and gets the same routing without a window.
///
/// @par Coordinates
/// dispatch() takes interaction_t::mouse in the GLOBAL space item_t::map_to_global() produces (the
/// window's client coordinates when the root sits at the window origin, which is what
/// widget_window_t sets). The router translates it into each delivered item's local coordinates,
/// which is what item_t::interact() requires. That is the one place where the "coordinates are
/// local" contract of interaction_t is fulfilled by the router instead of the host.
///
/// @par The routing rules (Qt's semantics, minimal)
///  1. HIT: from the root, descend through item children in reverse order (the child added last is
///     painted last and wins), skipping invisible/disabled items and their subtrees, requiring the
///     point inside every item's rect(); the deepest containing item is the target. The root itself
///     is an item too, so a target may be a non-widget item (ui::im_host_t): that is how the
///     immediate host receives events at all.
///  2. BUBBLING: interact() is called on the target first; when it returns false the position is
///     translated into the parent's coordinates and the parent item is asked, up to the root. The
///     first true stops the chain.
///  3. POINTER CAPTURE: the item a press is delivered to owns the pointer until the release (Qt's
///     implicit grab); an item may also ask for it with item_t::capture_pointer(). While a capture
///     target exists, every mouse step is delivered to it even when the pointer left its rectangle,
///     and the hit test decides only where the hover is.
///  4. KEYS AND TEXT INPUT: a snapshot with a non-zero key is a KEY step, and one with a non-empty
///     interaction_t::text_input (and no key) is a TEXT-INPUT step. Neither is positional, so both go
///     to the focus ITEM first (a widget or an immediate host — both are items and both are focus
///     stations); a text-input step must reach the focus item, not whatever the pointer happens to be
///     over. If the focus item does not consume a KEY step AND the key is a Tab press, the router treats
///     Tab as its own (focus next/previous). An unconsumed non-Tab key or text-input step returns false
///     and does NOT bubble. Tab is therefore no longer the router's exclusive property: the focus item
///     sees it first. Keys are not positional, so the key itself is not translated and nothing bubbles;
///     the payload's mouse position is still mapped into the receiver, because interaction_t::mouse is
///     always local to the receiver.
///  5. HOVER: the hit item is the hover target, recorded on the ITEM through item_t::set_hovered().
///     When the target changes, the previously hovered item is cleared and receives one synthetic
///     interaction with hovered == false (no edges) so its own interact() sees the change. That
///     notification is sent BEFORE the current step, to that item only (a hover change is not a click
///     and does not bubble), and it is SKIPPED when the old target is the item about to receive this
///     step anyway (the capture case, where the delivery already carries hovered == false).
///
/// @par Pointer state lives on the items
/// The router owns the routing, not the hover/press/capture state: hover and press are item_t state
/// (set_hovered() / set_pressed()), so every output of one tree reads the same values, and press is
/// recorded per button. The single capture target is the ITEM's captures_pointer(), reached through
/// the pointer sink that this router installs on its root; the guarded pointer here only remembers
/// which item the delivery belongs to and drops out when the item gives the pointer back or dies.
///
/// @par Focus
/// The router is the focus AUTHORITY: it owns the focus pointer, drives item_t::has_focus() through
/// item_t::set_has_focus() (which dispatches a focus_change_event_t exactly once per change),
/// and installs a focus sink on its root so item_t::set_focus() from any descendant reaches it.
/// The Tab chain is a depth-first pre-order walk of the ITEM tree (parents before children, children
/// in child order) over the focusable items only — a widget and an immediate host are peers there.
/// focus_next()/focus_previous() wrap around, and a focus that is no longer focusable is dropped
/// (one focus_change_event() with has_focus() false) before the next delivery.
///
/// A PRESS edge also moves the focus: the hit item, or its nearest ancestor, with CLICK_FOCUS or
/// STRONG_FOCUS that is focusable receives it before the press is delivered, which is why a clicked
/// control already sees has_focus() in its own interact(). The press is still routed normally (focus
/// consumes nothing), and when no such item exists the focus is left untouched — blank space, a
/// NO_FOCUS or TAB_FOCUS item, and a disabled item all leave it where it was.
///
/// @par Lifetime
/// The widget tree must outlive the router: the router holds guarded pointers (object_pointer_t), so a
/// deleted focus/pointer/hover item becomes null instead of dangling, but the ROOT itself is a plain
/// reference the caller must keep alive (widget_window_t destroys the router before the root).

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/metaobject/object_pointer_t.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/item_t.h"

namespace hwshqtb::ui {
    /// @brief Routes host-neutral interactions through one retained item tree (see the file note).
    class HWSHQTB_UI_EXPORT input_router_t {
    public:
        /// @brief Build a router for @p root and install the focus and pointer sinks on it.
        /// @param root The tree's root item (normally a widget); it must outlive this router.
        explicit input_router_t(item_t& root);

        /// @brief Clear the focus (one focus_change_event() with has_focus() false), give the pointer
        ///        back, and remove the
        ///        sinks from the root.
        /// @note The root must still be alive; widget_window_t destroys the router first.
        ~input_router_t();

        input_router_t(const input_router_t&) = delete;
        input_router_t& operator=(const input_router_t&) = delete;
        input_router_t(input_router_t&&) = delete;
        input_router_t& operator=(input_router_t&&) = delete;

        /// @brief Deliver one interaction: hit-test first, then bubble (see the file note).
        /// @param in The interaction snapshot; mouse is in the ROOT's coordinates.
        /// @return Whether any item (or the router itself, for Tab) consumed it.
        bool dispatch(const ::hwshqtb::gui::interaction_t& in);

        /// @brief The item that currently holds the focus, or nullptr.
        item_t* focus_item() const;

        /// @brief Focus @p item, or clear the focus when it is null or cannot hold focus.
        /// @param item The new focus item (a widget or an immediate host); it must belong to this
        ///        router's tree.
        /// @note Fires focus_change_event() on the old item (has_focus() false) and on the new one
        ///       (true),
        ///       exactly once each, and only when the focus actually changes.
        void set_focus(item_t* item);

        /// @brief Move the focus to the next focusable item in depth-first order (wraps).
        /// @note With no current focus, focuses the first focusable item; with none at all, does
        ///       nothing.
        void focus_next();

        /// @brief Move the focus to the previous focusable item in depth-first order (wraps).
        void focus_previous();

        /// @brief The item that holds the pointer capture, or nullptr.
        /// @note Set by the implicit grab a press starts and by item_t::capture_pointer(), cleared
        ///       once the release was delivered (or the item gave the pointer back). The capture
        ///       target is an item, not a widget: a non-widget item (a host) can capture too.
        /// @note The truth is the item's captures_pointer(); this is the delivery-side record of it.
        item_t* mouse_target() const;

        /// @brief Drop the pointer state: the hover, the pressed buttons and the capture.
        /// @note The window adapter calls it when the pointer can no longer reach the tree (the mouse
        ///       left the window, or the window lost the focus), because dispatch() only sees steps
        ///       that arrive. The router is the ONE place that maintains and clears hover; a view
        ///       never does.
        void clear_pointer_state();

    private:
        /// @brief The deepest visible/enabled item of this tree at @p root_point (root coordinates).
        /// @return The target item, or nullptr when the point is outside the root.
        item_t* hit_test(::hwshqtb::gui::point_t root_point) const;

        /// @brief Whether @p item belongs to this router's tree (walks the ITEM parents to the root).
        bool is_in_tree(const item_t& item) const;

        /// @brief Move the focus one step along the depth-first chain of focusable items.
        /// @param delta +1 for the next, -1 for the previous.
        void focus_step(int delta);

        /// @brief Move the focus to @p target or its nearest click-focusable ancestor.
        /// @param target The item under the press (nullptr when the press is outside the root).
        /// @note Only CLICK_FOCUS / STRONG_FOCUS items that are focusable (visible in the window and
        ///       enabled) qualify; when none does, the current focus is left untouched.
        void focus_on_press(item_t* target);

        /// @brief Send the synthetic hover-out to @p item (no edges, hovered == false).
        void send_hover_out(item_t& item, const ::hwshqtb::gui::interaction_t& in);

        /// @brief Record @p item as the single capture target (nullptr clears it).
        /// @param item The capturing item; its captures_pointer() is already set by the item.
        /// @note Written by the pointer sink only, so the item's request is the one writer path.
        void set_pointer_target(item_t* item);

        /// @brief Drop the capture target and its item-side captures_pointer().
        void release_pointer_target();

        /// @brief Clear every button state of @p item (the pointer session ended).
        void clear_pressed(item_t& item);

        /// @brief The tree's root (plain: the caller guarantees it outlives the router).
        item_t* _root = nullptr;
        /// @brief The focus item, guarded against deletion elsewhere.
        ::hwshqtb::metaobject::object_pointer_t<item_t> _focus;
        /// @brief The item that owns the pointer, guarded against deletion elsewhere.
        ::hwshqtb::metaobject::object_pointer_t<item_t> _pointer;
        /// @brief The item that was told hovered == true last, guarded against deletion elsewhere.
        /// @note The STATE is on the item; this only remembers whom to clear on the next move.
        ::hwshqtb::metaobject::object_pointer_t<item_t> _hovered;
    };
}

#endif
