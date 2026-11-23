#ifndef HWSHQTB__UI__ITEM_T_H
#define HWSHQTB__UI__ITEM_T_H

/// @file item_t.h
/// @brief item_t: a node of the item tree — geometry, size hints, painting, hit testing and
///        interaction.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/region_t.h"
#include "hwshqtb/metaobject/flag_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/ui_event_t.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    /// @brief The extent a maximum_size() starts at: no upper bound on either axis.
    /// @note INT_MAX on both axes. A maximum is never a negative sentinel, because every setter
    ///       clamps a negative component to 0, so "unbounded" needs its own named value.
    inline constexpr ::hwshqtb::gui::extent_t unbounded_extent{
        std::numeric_limits<int>::max(),
        std::numeric_limits<int>::max(),
    };

    /// @brief One bit of a layout alignment: where a layout puts an item inside a cell LARGER than it
    ///        (Qt's Qt::AlignmentFlag, horizontal and vertical halves).
    ///
    /// The horizontal bits (LEFT / RIGHT / HCENTER) and the vertical bits (TOP / BOTTOM / VCENTER) are
    /// two independent sets: a bit set on one axis places the item on that axis, and an axis without
    /// any bit keeps the item at the cell origin, filling the cell as far as its own hard constraints
    /// allow. Zero is therefore "fill the cell" (Qt's default alignment), which is what an item that
    /// never calls set_alignment() gets.
    ///
    /// @note The bits are not exclusive: a caller may set two horizontal bits at once. Qt does not
    ///       refuse that either — QLayoutItem::setAlignment() stores the word unchanged
    ///       (qtbase/src/widgets/kernel/qlayoutitem.cpp) and QWidgetItem::setGeometry() resolves it
    ///       at placement time, RIGHT before LEFT before HCENTER. The offset helpers below reproduce
    ///       exactly that precedence, so a contradictory pair is a documented, deterministic choice
    ///       rather than an error.
    enum struct alignment_flag : std::uint8_t {
        /// @brief No bit: fill the cell at its origin on both axes (the default).
        NONE = 0,
        /// @brief Place the item at the cell's left edge (Qt::AlignLeft).
        LEFT = 1u << 0,
        /// @brief Place the item at the cell's right edge (Qt::AlignRight).
        RIGHT = 1u << 1,
        /// @brief Centre the item horizontally in the cell (Qt::AlignHCenter).
        HCENTER = 1u << 2,
        /// @brief Place the item at the cell's top edge (Qt::AlignTop).
        TOP = 1u << 3,
        /// @brief Place the item at the cell's bottom edge (Qt::AlignBottom).
        BOTTOM = 1u << 4,
        /// @brief Centre the item vertically in the cell (Qt::AlignVCenter).
        VCENTER = 1u << 5,
    };

    /// @brief A bit set of alignment_flag values (the project's typed flag wrapper, QFlags shape).
    using alignment_t = ::hwshqtb::metaobject::flag_t<alignment_flag>;

    /// @brief The three horizontal bits (Qt's Qt::AlignHorizontal_Mask).
    inline constexpr alignment_t horizontal_alignment_mask =
        alignment_t{ alignment_flag::LEFT } | alignment_flag::RIGHT | alignment_flag::HCENTER;

    /// @brief The three vertical bits (Qt's Qt::AlignVertical_Mask).
    inline constexpr alignment_t vertical_alignment_mask =
        alignment_t{ alignment_flag::TOP } | alignment_flag::BOTTOM | alignment_flag::VCENTER;

    /// @brief The horizontal position of an item inside a cell WIDER than it (Qt's alignment rule).
    /// @param cell_extent The cell's width.
    /// @param item_extent The item's width.
    /// @param alignment The item's alignment; only the horizontal bits are read.
    /// @return 0 when the cell leaves no room (item_extent >= cell_extent) or when no horizontal bit
    ///         is set, cell_extent - item_extent for RIGHT, and half of that for HCENTER (integer
    ///         division, so an odd spare pixel stays on the right).
    /// @note The precedence is Qt's, from QWidgetItem::setGeometry()
    ///       (qtbase/src/widgets/kernel/qlayoutitem.cpp): AlignRight is tested first, AlignLeft
    ///       second, and the centring is the fallback. LEFT|RIGHT and RIGHT|HCENTER therefore go
    ///       right, LEFT|HCENTER stays left, and all three go right. Qt does not refuse the
    ///       combination, and neither does this library.
    /// @note An axis with NO bit is not centred here: it keeps the cell origin (the "fill the cell"
    ///       default). Qt's own placement falls back to centring on such an axis; this library cannot
    ///       follow that without moving every unaligned item, so NONE is fill on both axes.
    /// @note Header-only so the layouts and a caller measuring the same cell share one rule.
    inline int horizontal_alignment_offset(int cell_extent, int item_extent,
                                           alignment_t alignment) noexcept {
        if (item_extent >= cell_extent) {
            return 0; // no room inside the cell (fill, or an overfull item): the origin wins
        }
        const alignment_t bits = alignment & horizontal_alignment_mask;
        if (!static_cast<bool>(bits)) {
            return 0; // no horizontal bit: fill the cell
        }
        if (static_cast<bool>(bits & alignment_flag::RIGHT)) {
            return cell_extent - item_extent;
        }
        if (static_cast<bool>(bits & alignment_flag::LEFT)) {
            return 0;
        }
        return (cell_extent - item_extent) / 2;
    }

    /// @brief The vertical position of an item inside a cell TALLER than it (Qt's alignment rule).
    /// @param cell_extent The cell's height.
    /// @param item_extent The item's height.
    /// @param alignment The item's alignment; only the vertical bits are read.
    /// @return 0 when the cell leaves no room (item_extent >= cell_extent) or when no vertical bit is
    ///         set, cell_extent - item_extent for BOTTOM, and half of that for VCENTER (integer
    ///         division, so an odd spare pixel stays at the bottom).
    /// @note The same Qt source and the same per-axis rule as horizontal_alignment_offset():
    ///       AlignBottom is tested first, AlignTop second, VCENTER is the fallback, and an axis
    ///       without a bit keeps the cell origin.
    /// @note Header-only for the same reason as its horizontal counterpart.
    inline int vertical_alignment_offset(int cell_extent, int item_extent,
                                         alignment_t alignment) noexcept {
        if (item_extent >= cell_extent) {
            return 0; // no room inside the cell (fill, or an overfull item): the origin wins
        }
        const alignment_t bits = alignment & vertical_alignment_mask;
        if (!static_cast<bool>(bits)) {
            return 0; // no vertical bit: fill the cell
        }
        if (static_cast<bool>(bits & alignment_flag::BOTTOM)) {
            return cell_extent - item_extent;
        }
        if (static_cast<bool>(bits & alignment_flag::TOP)) {
            return 0;
        }
        return (cell_extent - item_extent) / 2;
    }

    /// @brief How a layout may size an item along one axis (the QSizePolicy::Policy subset W2 needs).
    enum struct size_policy_enum {
        /// @brief Keep size_hint() and do not absorb spare space (Qt: Fixed).
        /// @note A layout does not shrink it below size_hint(): the floor is max(size_hint(),
        ///       minimum_size_hint()), not minimum_size_hint(). Only the overfull rule, which cuts the
        ///       biggest item first, can take it below that floor.
        FIXED,
        /// @brief Keep size_hint(), may shrink to minimum_size_hint(), never grows (Qt: Preferred).
        PREFERRED,
        /// @brief Absorb the layout's spare space, in proportion to stretch() (Qt: Expanding).
        EXPANDING,
    };

    /// @brief How an item can receive keyboard focus (the QWidget::FocusPolicy subset W3 needs).
    ///
    /// @par The minimal model
    ///  - no_focus: never focusable; neither a click nor Tab reaches it;
    ///  - click_focus: a press inside focuses it (and it is focusable, so Tab reaches it too);
    ///  - tab_focus: Tab reaches it, but a press does not move focus to it;
    ///  - strong_focus: both a press and Tab focus it.
    ///
    /// is_focusable() is "visible (in the window), enabled and not no_focus" — that is the definition
    /// the Tab chain uses, so in this minimal model click_focus is also tab-reachable (Qt's ClickFocus
    /// is not; W3 deliberately keeps the four policies but only two behaviours on that axis). Only
    /// click_focus / strong_focus state an intent to move focus on a press. The default is no_focus,
    /// so nothing becomes focusable by accident.
    enum struct focus_policy_enum {
        /// @brief Never focusable (the default).
        NO_FOCUS,
        /// @brief Focused by a press inside; also reachable by Tab under is_focusable().
        CLICK_FOCUS,
        /// @brief Reachable by Tab only; a press does not focus it.
        TAB_FOCUS,
        /// @brief Focused by a press inside and reachable by Tab.
        STRONG_FOCUS,
    };

    /// @brief A node that takes part in layout, painting and hit testing: the base of every visual
    ///        element of the ui layer.
    ///
    /// @par What an item is
    /// An item is an object_t with a rectangle inside its parent, a size hint, and the operations a
    /// visual tree needs: paint itself, answer a hit and interact. Layout, painting and hit testing
    /// traverse the item tree only, so a plain object_t child (a controller, a model, a timer) is
    /// invisible to all three. The size policy, the stretch, the alignment inside the layout cell,
    /// the minimum/maximum size constraints, the keyboard focus and the pointer state (hover / press /
    /// capture) are item state as well: a layout arranges items, the router's Tab chain is a chain of
    /// focusable ITEMS — a widget and an immediate host are peers there — and several outputs may
    /// display one tree, so every view must read the same hover/press/capture state from the items.
    ///
    /// @par Size model: opinions versus constraints
    /// An item states its size twice over, and the two must never be mixed:
    ///  - size_hint() / minimum_size_hint() are OPINIONS about the content. An implementation reads
    ///    its own content only and must NOT read minimum_size() / maximum_size();
    ///  - minimum_size() / maximum_size() are the HARD constraints a caller installs.
    /// A layout asks only the effective triple — effective_size_hint(), effective_minimum_size(),
    /// effective_maximum_size() — and a container aggregating its children asks that same triple of
    /// every child. set_fixed_size() adds no third source of truth: it is minimum_size() ==
    /// maximum_size().
    ///
    /// @par Alignment
    /// alignment() says where a layout puts this item inside its cell when the cell came out larger
    /// than the item; alignment_flag::NONE (the default) fills the cell at its origin. Each axis is
    /// independent and an axis without a bit keeps the origin, so an item that never sets an
    /// alignment, and every item on an unaligned axis, keeps exactly the geometry it had before the
    /// alignment existed. The horizontal_alignment_offset() / vertical_alignment_offset() helpers are
    /// the shared rule the layouts apply.
    ///
    /// @par Not everything is a widget
    /// The hierarchy is item_t -> { widget_t, im_host_t }: widget_t adds the layout, the repaint sink
    /// and the frame orchestration; a host is a leaf. The core layer knows neither type — is_item()
    /// is the marker every traversal asks for, in exactly the shape of is_widget(), which is narrowed
    /// to "is a ui::widget_t" and is used for type checks only (parent_widget()).
    ///
    /// @par Coordinates and painting
    /// geometry() is relative to the parent item. rect() is the same rectangle in this item's own
    /// coordinates ({0, 0} plus the geometry size), and hit()/paint() work in those coordinates.
    /// paint() is NON-const on purpose: painting may update a cache. It receives the painter already
    /// translated to the parent's origin and clipped to the parent's rect().
    ///
    /// @par State versus extension points
    /// The four things every item HAS — the rectangle, the visibility flag, the enabled flag and the
    /// size hints' hard constraints — are state of this class. Their accessors are not virtual, so a
    /// layout that wrote a rectangle and the pixels a frame paints from it cannot disagree. What an
    /// item DOES is where a subclass steps in: paint(), hit(), interact(), size_hint(),
    /// minimum_size_hint() and the event handlers (paint_event(), hover_event(), press_event(),
    /// geometry_change_event(), focus_change_event(), size_constraint_change_event(),
    /// content_change_event(), visible_change_event(), enabled_change_event()) are virtual with a
    /// defined default, so a subclass overrides exactly the behaviour it has and inherits the rest.
    /// There is deliberately no pure virtual function: the base is not instantiable (its constructor
    /// is protected), which is how an abstract item is expressed here.
    ///
    /// @par State changes are events
    /// Every real state change goes through event(): a setter constructs the matching ui_event_t on
    /// the stack, dispatches it with core_application::send_event() and the receiver's event() calls
    /// the matching handler. There is no deferred step and no queue: the handler runs inside the
    /// setter's call, exactly like the router and every host rely on.
    class HWSHQTB_UI_EXPORT item_t : public ::hwshqtb::metaobject::object_t {
        OBJECT

    public:
        ~item_t() override = default;

        /// @brief The natural size this item asks for (Qt's sizeHint): the item's OPINION.
        /// @return {0, 0} by default: an item that claims no space of its own.
        /// @note An opinion about the content: an implementation must not read the hard constraints
        ///       (minimum_size() / maximum_size()). The layouts clamp this hint themselves.
        /// @note This is an EXTENSION POINT: an item with content to show overrides it with the size
        ///       that content needs; "no opinion" is the honest default for a contentless item.
        virtual ::hwshqtb::gui::extent_t size_hint() const;

        /// @brief The smallest size a layout may shrink this item to (Qt's minimumSizeHint).
        /// @return {0, 0} by default, for the same reason as size_hint().
        /// @note An opinion as well: it says what the content needs at minimum, not what a caller
        ///       demanded. The hard floor is minimum_size(), reached through
        ///       effective_minimum_size().
        /// @note This is an EXTENSION POINT: a container that aggregates children answers the floor
        ///       they impose.
        virtual ::hwshqtb::gui::extent_t minimum_size_hint() const;

        /// @brief The lower bound of the hard size constraints (Qt's minimumSize).
        /// @return {0, 0} until a setter raises it.
        /// @note A CONSTRAINT, not an opinion: the size_hint() / minimum_size_hint() virtuals never
        ///       read it. Only the effective_*() triple below applies it.
        /// @note noexcept.
        ::hwshqtb::gui::extent_t minimum_size() const noexcept;

        /// @brief The upper bound of the hard size constraints (Qt's maximumSize).
        /// @return unbounded_extent (INT_MAX on both axes) until a setter lowers it.
        /// @note noexcept.
        ::hwshqtb::gui::extent_t maximum_size() const noexcept;

        /// @brief Install the lower bound of BOTH axes (Qt's setMinimumSize).
        /// @param size The new minimum; a negative component is treated as 0.
        /// @note minimum_size() <= maximum_size() is an invariant: a minimum above the current
        ///       maximum RAISES the maximum to it (the value being installed wins, the bound that
        ///       would contradict it follows).
        /// @note It changes constraint state only: it does not resize the item by itself. A real
        ///       change dispatches a size_constraint_change_event_t and requests a repaint.
        void set_minimum_size(::hwshqtb::gui::extent_t size);

        /// @brief Install the upper bound of BOTH axes (Qt's setMaximumSize).
        /// @param size The new maximum; a negative component is treated as 0.
        /// @note The invariant holds here too: a maximum below the current minimum LOWERS the
        ///       minimum to it.
        /// @note It changes constraint state only: it does not resize the item by itself. A real
        ///       change dispatches a size_constraint_change_event_t and requests a repaint.
        void set_maximum_size(::hwshqtb::gui::extent_t size);

        /// @brief Install a FIXED size constraint on BOTH axes.
        /// @param size The constrained size; a negative component is treated as 0.
        /// @note Exactly set_minimum_size(size) followed by set_maximum_size(size): a fixed size is
        ///       NOT separate state, it is minimum_size() == maximum_size() == size.
        /// @note It does not resize the item by itself: re-apply the parent layout (relayout() on the
        ///       parent, or the next resize) for the constraint to take effect.
        void set_fixed_size(::hwshqtb::gui::extent_t size);

        /// @brief Whether BOTH axes are pinned by the constraints.
        /// @return true only when minimum_size() == maximum_size() and that size is not empty
        ///         (positive on both axes); pinning one axis alone is not a fixed size, and an empty
        ///         size means "no constraint" — so has_fixed_size() answers false for
        ///         set_fixed_size({0, 0}).
        /// @note noexcept.
        bool has_fixed_size() const noexcept;

        /// @brief The pinned size: minimum_size() when has_fixed_size(), {0, 0} otherwise.
        /// @note noexcept.
        ::hwshqtb::gui::extent_t fixed_size() const noexcept;

        /// @brief Set only the width of the lower bound (Qt's setMinimumWidth).
        /// @param width The new minimum width; a negative value is treated as 0, and the height of
        ///        minimum_size() stays as it is.
        /// @note Equivalent to set_minimum_size({width, minimum_size().height()}).
        void set_minimum_width(int width);

        /// @brief Set only the height of the lower bound (Qt's setMinimumHeight).
        /// @param height The new minimum height; a negative value is treated as 0.
        /// @note Equivalent to set_minimum_size({minimum_size().width(), height}).
        void set_minimum_height(int height);

        /// @brief Set only the width of the upper bound (Qt's setMaximumWidth).
        /// @param width The new maximum width; a negative value is treated as 0.
        /// @note Equivalent to set_maximum_size({width, maximum_size().height()}).
        void set_maximum_width(int width);

        /// @brief Set only the height of the upper bound (Qt's setMaximumHeight).
        /// @param height The new maximum height; a negative value is treated as 0.
        /// @note Equivalent to set_maximum_size({maximum_size().width(), height}).
        void set_maximum_height(int height);

        /// @brief Pin only the width: minimum width == maximum width == @p width (Qt's
        ///        setFixedWidth).
        /// @param width The pinned width; a negative value is treated as 0.
        /// @note Equivalent to set_minimum_width(width) followed by set_maximum_width(width); the
        ///       height keeps whatever bounds it had, so has_fixed_size() stays false unless the
        ///       height was pinned too.
        void set_fixed_width(int width);

        /// @brief Pin only the height: minimum height == maximum height == @p height (Qt's
        ///        setFixedHeight).
        /// @param height The pinned height; a negative value is treated as 0.
        /// @note Equivalent to set_minimum_height(height) followed by set_maximum_height(height); the
        ///       width keeps whatever bounds it had.
        void set_fixed_height(int height);

        /// @brief The size a layout should give this item (QWidgetItem::sizeHint):
        ///        clamp(max(size_hint(), minimum_size_hint()), minimum_size(), maximum_size()).
        /// @note NON-virtual on purpose: this is the one size question a layout asks of any item, and
        ///       no subclass may change how the constraints are applied.
        ::hwshqtb::gui::extent_t effective_size_hint() const;

        /// @brief The smallest size a layout may shrink this item to (Qt's qSmartMinSize), per axis.
        /// @note The floor depends on the size policy of that axis: PREFERRED and EXPANDING shrink to
        ///       minimum_size_hint(); FIXED does not shrink at all and keeps max(size_hint(),
        ///       minimum_size_hint()). The result is clamped into [minimum_size(), maximum_size()].
        /// @note This is the compression floor, not a guarantee: when even the floors do not fit the
        ///       available space, the overfull rule cuts the biggest items first — a FIXED item
        ///       included — so a layout may still place an item below this size.
        ::hwshqtb::gui::extent_t effective_minimum_size() const;

        /// @brief The largest size a layout may give this item: maximum_size().
        ::hwshqtb::gui::extent_t effective_maximum_size() const;

        /// @brief The rectangle relative to the parent item.
        /// @note NON-virtual: the rectangle is item STATE. set_geometry() is the one writer, and a
        ///       subclass that could answer something else would make a layout's placement and the
        ///       painted pixels disagree.
        /// @note noexcept.
        ::hwshqtb::gui::rect_t geometry() const noexcept;

        /// @brief Move/resize the item relative to its parent.
        /// @param rect The new rectangle, in the parent's coordinates.
        /// @note NON-virtual: the value is stored, then a geometry_change_event_t is dispatched (which
        ///       runs geometry_change_event()), then the item asks for its own repaint. Installing the
        ///       rectangle the item already holds is a no-op: no event, no damage.
        /// @note A subclass reacts to the change by overriding geometry_change_event(), never by
        ///       re-implementing this setter.
        void set_geometry(::hwshqtb::gui::rect_t rect);

        /// @brief Whether the item is visible (its own flag, not its ancestors').
        /// @note The `visible` property: written by set_visible(), announced by visible_changed().
        /// @note NON-virtual: the flag is item state. Ancestors do not take part —
        ///       is_visible_in_window() is the question that walks the chain.
        /// @note noexcept.
        [[= ::hwshqtb::metaobject::make_property("visible", "set_visible", "visible_changed")]]
        bool is_visible() const noexcept;

        /// @brief Show or hide the item: record the state, dispatch the event, emit the signal and
        ///        request a repaint.
        /// @param visible The new visibility; an item that already holds the value is left alone.
        /// @note The event (visible_change_event_t -> visible_change_event()) is this item's OWN
        ///       reaction — widget_t asks its host for a frame there — while the signal tells the
        ///       observers. Both fire only on a real change.
        /// @note Order: state, event, signal, then update(). An observer therefore sees the new value
        ///       through is_visible() and receives every reaction of the item that owns the state.
        /// @note NON-virtual: the state and its two notifications are one operation; a subclass
        ///       reacts by overriding visible_change_event(), never by hiding this setter.
        void set_visible(bool visible);

        /// @brief Whether the item is enabled (its own flag).
        /// @note The `enabled` property: written by set_enabled(), announced by enabled_changed().
        /// @note NON-virtual, like is_visible().
        /// @note noexcept.
        [[= ::hwshqtb::metaobject::make_property("enabled", "set_enabled", "enabled_changed")]]
        bool is_enabled() const noexcept;

        /// @brief Enable or disable the item: record the state, dispatch the event, emit the signal and
        ///        request a repaint.
        /// @param enabled The new enabled state; an item that already holds the value is left alone.
        /// @note The same order and the same split as set_visible(): event for the item itself, signal
        ///       for the observers, both only on a real change.
        void set_enabled(bool enabled);

        /// @brief Whether this item and every item ancestor are visible.
        /// @note The walk follows the ITEM parent chain, so a host's visibility also hides the
        ///       controls inside it; the first non-item object stops the walk (it has no visual flag).
        bool is_visible_in_window() const;

        /// @name Keyboard focus (W3)
        ///
        /// Focus is an ITEM property, not a widget one: the router's Tab chain is a chain of focusable
        /// items, and an immediate host is a focus station exactly like a widget. The ROUTER is the
        /// authority, not the item: an item only asks. set_focus() walks up the parent chain to the
        /// nearest item that installed a focus sink (ui::input_router_t installs one on its root, and
        /// widget_window_t owns such a router). Without a host the call is a
        /// documented no-op — there is nobody to hold the focus — and has_focus() keeps answering
        /// false. has_focus() is maintained by the router through set_has_focus(), which is also what
        /// fires focus_in_event()/focus_out_event().
        /// @{
        /// @brief The focus policy (no_focus by default).
        focus_policy_enum focus_policy() const;

        /// @brief Install the policy that decides how this item can receive focus.
        /// @param policy The new policy; the default is no_focus.
        /// @note Lowering the policy to no_focus on the focused item clears the focus (one
        ///       focus_change_event() with has_focus() false), because an item that cannot hold focus
        ///       must not keep it.
        void set_focus_policy(focus_policy_enum policy);

        /// @brief Whether this item can hold focus: visible in the window, enabled, and not
        ///        no_focus.
        /// @note This is the filter the Tab chain applies; the whole subtree of an invisible or
        ///       disabled item is skipped before this is even asked.
        bool is_focusable() const;

        /// @brief Ask the host to focus this item.
        /// @note Walks up the PARENT chain to the NEAREST item that installed a focus sink (the
        ///       router's root in a retained tree, the immediate host inside one) and calls it. The
        ///       item's own sink belongs to its descendants and is skipped, so an immediate host
        ///       asking for focus reaches the router above it; the topmost item is the one exception
        ///       (it has no parent, so its own sink is the outermost authority). A no-op when no
        ///       sink is installed (documented: the router is the authority). Focusing an item that
        ///       is not focusable clears the focus instead, so an invisible or disabled item can
        ///       never hold it.
        void set_focus();

        /// @brief Ask the host to clear the focus when this item holds it.
        /// @note A no-op when this item does not hold the focus, or when no sink is installed. The
        ///       request goes to the same parent-chain sink as set_focus().
        void clear_focus();

        /// @brief Whether this item currently holds the keyboard focus.
        bool has_focus() const;
        /// @}

        /// @name Pointer state
        ///
        /// Driven by the ONE input session and readable by every view. The state lives on the ITEM,
        /// not on the router or the view: several outputs may display the same tree, and they must
        /// all see the same hover/press/capture state. ui::input_router_t maintains hover and press
        /// through the setters; capture goes through the pointer sink because the router's single
        /// capture target is what the request changes.
        ///
        /// A disabled or not-in-window item never enters hover. Only a real change requests a
        /// repaint (the hover_event() / press_event() handlers), so an unchanged state is free.
        /// @{
        /// @brief Whether the pointer is currently over this item.
        /// @note noexcept.
        bool is_hovered() const noexcept;

        /// @brief Whether @p button is currently held over this item.
        /// @param button The button to ask about.
        /// @note One item at a time carries a press: the router records it on the item it is
        ///       delivering to, so a second output paints the same pressed feedback.
        /// @note noexcept.
        bool is_pressed(::hwshqtb::gui::mouse_button_enum button) const noexcept;

        /// @brief Whether this item owns the pointer until it releases it (Qt's implicit grab).
        /// @note noexcept.
        bool captures_pointer() const noexcept;

        /// @brief Record the hover state; dispatches a hover_event_t when it actually changes.
        /// @param hovered The new state. true is refused for an item that is disabled or not visible
        ///        in the window, so such an item never enters hover.
        /// @note The repaint request comes from hover_event() (the default marks the item and its
        ///       ancestors damaged; a row view overrides it to damage the rows it left and entered).
        void set_hovered(bool hovered);

        /// @brief Record a button edge on this item; dispatches a press_event_t when it actually
        ///        changes.
        /// @param button The button whose state changes.
        /// @param pressed true for a press, false for a release.
        /// @note The repaint request comes from press_event(), like set_hovered().
        void set_pressed(::hwshqtb::gui::mouse_button_enum button, bool pressed);

        /// @brief Take the pointer capture (Qt's grabMouse / releaseMouse).
        /// @return true when the request reached a pointer sink; false when there is no sink (an item
        ///         outside any input session cannot capture).
        /// @note The capture target is the input session's, so the item ASKS through the pointer
        ///       sink and the item-side flag is what the session reads back.
        bool capture_pointer();

        /// @brief Release the pointer capture (Qt's releaseMouse).
        /// @note A no-op when this item does not capture. The request goes to the same parent-chain
        ///       sink as capture_pointer().
        void release_pointer();

        /// @brief Install the sink that tells the input session "this item now owns the pointer".
        /// @param sink The callable invoked with the capturing item (nullptr clears the target); an
        ///        empty function removes the sink.
        /// @note Mirrors set_focus_sink(): a container installs it for its children, the root
        ///       forwards to the router, and an item with no sink cannot capture.
        void set_pointer_sink(std::function<void(item_t*)> sink);
        /// @}

        /// @brief The horizontal size policy (preferred by default).
        size_policy_enum horizontal_policy() const;

        /// @brief The vertical size policy (preferred by default).
        size_policy_enum vertical_policy() const;

        /// @brief Set both size policies.
        /// @param horizontal The horizontal policy.
        /// @param vertical The vertical policy (defaults to preferred).
        void set_size_policy(size_policy_enum horizontal,
                             size_policy_enum vertical = size_policy_enum::PREFERRED);

        /// @brief The weight used when a layout distributes spare space to expanding items.
        /// @return The stretch, 0 by default (an expanding item among zero-stretch siblings shares
        ///         equally).
        int stretch() const;

        /// @brief Set the growth weight (Qt's QBoxLayout stretch factor).
        /// @param stretch The weight; a negative value is treated as 0.
        void set_stretch(int stretch);

        /// @brief The alignment inside the layout cell (alignment_flag::NONE by default).
        /// @note noexcept.
        alignment_t alignment() const noexcept;

        /// @brief How a layout places this item inside a cell LARGER than the item (Qt's alignment
        ///        flags).
        /// @param alignment The new alignment; alignment_flag::NONE (zero) means "fill the cell", which
        ///        is the current behaviour and the default, so an item that never sets alignment keeps
        ///        its geometry.
        /// @note Each axis is independent: the horizontal bits move the item horizontally and the
        ///       vertical bits vertically, while an axis without any bit stays at the cell origin. A
        ///       contradictory pair is stored as it is and resolved at placement time (RIGHT beats LEFT
        ///       beats HCENTER, BOTTOM beats TOP beats VCENTER), exactly like Qt's
        ///       QWidgetItem::setGeometry(); nothing is refused.
        /// @note Alignment only ever MOVES the item: it never resizes it. When the cell is not larger
        ///       than the item (a fill, or an overfull axis) the bits have no room and change nothing.
        /// @note A box layout also reads the bits of its MAIN axis as a size cap: on an aligned axis
        ///       the item never grows past effective_size_hint() and takes no share of the spare
        ///       space, so an EXPANDING item that carries a bit on that axis keeps its hint size.
        /// @note It changes no geometry by itself, and it requests no re-layout: re-apply the parent
        ///       layout (relayout()) for the new placement to take effect, as after set_fixed_size().
        void set_alignment(alignment_t alignment);

        /// @brief The item rectangle in its own coordinates: {{0, 0}, geometry()'s size}.
        ::hwshqtb::gui::rect_t rect() const noexcept;

        /// @brief Map a point from this item's coordinates to the top-level (global) coordinates.
        /// @param point The point in this item's local coordinates.
        /// @return The point in the coordinate system of the topmost item of the chain (the sum of
        ///         every geometry origin up to, and including, the topmost item).
        /// @note The walk stops at the first ancestor that is not an item: a non-item object has no
        ///       coordinate system to contribute.
        ::hwshqtb::gui::point_t map_to_global(::hwshqtb::gui::point_t point) const;

        /// @brief The inverse of map_to_global().
        /// @param point The point in global coordinates.
        /// @return The point in this item's local coordinates.
        ::hwshqtb::gui::point_t map_from_global(::hwshqtb::gui::point_t point) const;

        /// @brief Drive one paint pass for this item's subtree (QWidget::render()).
        /// @param painter The painter of the pass; its clip is the area to cover.
        /// @param damage The area to paint, in the painter's current (parent) coordinates.
        /// @note The FRAMEWORK entry a container uses on each item child, and the entry a caller uses to
        ///       paint a subtree into any device. The default does nothing — an item that is not a widget
        ///       owns no pixels — and widget_t implements it by localising the painter, running
        ///       paint_event() (the one PIXEL hook) and then descending into the item children.
        /// @note The immediate host implements it to run the user's process: it renders no pixels of its
        ///       own, but the pass has to be driven at the right point in the tree order.
        virtual void render(::hwshqtb::gui::painter_t& painter,
                            const ::hwshqtb::gui::region_t& damage);

        /// @brief Drive one paint pass for everything the painter's current clip shows.
        /// @param painter The painter of the pass; its clip IS the damage.
        /// @note Non-virtual convenience form of the two-argument entry.
        void render(::hwshqtb::gui::painter_t& painter);

        /// @brief This item's state changed and it wants to be repainted.
        ///
        /// Every setter that changes something visible calls this. The item layer owns NO damage record
        /// and schedules no frame: an item that is not a widget has no pixels, so "needs a repaint" is
        /// only meaningful to a subclass that can actually render. widget_t overrides this to record the
        /// damage and to ask its host for a frame.
        /// @note The default does nothing, which is correct for a non-visual item (a spacer, a model
        ///       node): it has nothing to repaint.
        virtual void notify_repaint();

        /// @brief Whether this item fills its whole rectangle with opaque pixels (Qt's
        ///        WA_OpaquePaintEvent).
        /// @return true only when paint() covers every pixel of rect() with an alpha of 255.
        /// @note noexcept.
        bool is_opaque() const noexcept;

        /// @brief Declare whether this item fills its whole rectangle with opaque pixels.
        /// @param opaque The new declaration; false by default.
        /// @note The declaration is a PROMISE to the paint traversal: a fully opaque item hides the
        ///       siblings under it inside its rectangle, so they are not painted there (that is the
        ///       occlusion culling). Declaring an item opaque that leaves parts of its rectangle
        ///       undrawn makes those pixels show the cleared background instead of the sibling.
        /// @note An item that draws only part of its rectangle (a text run, an indicator, a groove)
        ///       must stay non-opaque even when every pixel it does draw is solid.
        /// @note Set it before the item is painted — a control does it in its constructor. It records
        ///       no damage by itself, so a LATER change needs an explicit update() to repaint what the
        ///       item covers.
        /// @note noexcept.
        void set_opaque(bool opaque) noexcept;

        /// @brief The object that receives this tree's host requests.
        /// @param receiver The host object; nullptr removes it. Not owned.
        /// @note Two event types reach it: a plain PAINT request (event_type_enum::PAINT) when the tree
        ///       needs a frame, and a scroll_event_t when pixels want to move. This is the ONLY pointer
        ///       the tree keeps to the outside world — the sink callables it replaces were a third
        ///       mechanism.
        /// @note Stored on the ROOT of the tree; a non-root item forwards to its root, so a leaf can ask
        ///       without walking the tree itself.
        void set_request_receiver(::hwshqtb::metaobject::object_t* receiver) noexcept;

        /// @brief The object that receives this tree's host requests, or nullptr.
        /// @return The receiver set on this item's root.
        /// @note noexcept.
        ::hwshqtb::metaobject::object_t* request_receiver() const noexcept;

        /// @brief The number of ITEM children (plain object_t children are not counted).
        std::size_t item_count() const;

        /// @brief The @p index-th item child in child order (the object tree's order).
        /// @param index The zero-based item-child index; out of range gives nullptr.
        /// @return The child item, or nullptr when there is no such item child.
        item_t* item_at_index(std::size_t index) const;

        /// @brief Whether this object is an item; always true for item_t (and for every widget).
        /// @note The core-layer contract: is_item() == true implies an item_t, so the ui layer may
        ///       static_cast after this check.
        /// @note Hidden from the meta-info, like the base declaration: not scriptable API.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        bool is_item() const noexcept override {
            return true;
        }

        /// @brief The deepest item at @p local, or nullptr when nothing was hit.
        /// @param local The point in THIS item's coordinates.
        /// @return The hit item, or nullptr when the point misses this item and every descendant.
        /// @note The caller treats nullptr as "the point is on this item itself" only after it has
        ///       checked rect().contains(local); the implementation answers for its subtree alone.
        /// @note The default answers nullptr: an item that owns no points (a plain container) is not
        ///       hit-testable, and the traversal then falls back to the parent.
        virtual item_t* hit(::hwshqtb::gui::point_t local) const;

        /// @brief Deliver one interaction to this item (Qt's event handler counterpart).
        /// @param in The input snapshot for this step; in.mouse is already in this item's
        ///        coordinates — translating is the caller's job.
        /// @return true when the item consumed the input (a press, a drag, ...).
        /// @note The default consumes nothing and answers false, so a non-interactive item is a leaf
        ///       the router can skip without a line of code.
        virtual bool interact(const ::hwshqtb::gui::interaction_t& in);

        /// @brief Event entry point: dispatch a ui state event to its handler
        ///        (QObject::event's counterpart).
        /// @param event The event; nullptr answers false.
        /// @return true when a ui event was dispatched, or when object_t::event() handled another
        ///         type; false when nobody handled it.
        /// @note A ui event is handled HERE, inside this call: the matching virtual handler runs
        ///       synchronously, so the state and the reaction cannot be observed apart.
        /// @note Every type outside the ui range falls through to object_t::event() unchanged, which
        ///       is what keeps the meta-call / timer / child behaviour of the core layer working.
        bool event(::hwshqtb::metaobject::event_t* event) override;

        /// @brief Emitted when set_visible() actually changes the visibility.
        /// @param visible The new value.
        SIGNAL(void visible_changed(bool visible) const)

        /// @brief Emitted when set_enabled() actually changes the enabled state.
        /// @param enabled The new value.
        SIGNAL(void enabled_changed(bool enabled) const)

    protected:
        /// @brief Construct an item with no parent.
        /// @note Protected on purpose: with every operation defaulted, a directly constructed item_t
        ///       would be a silent no-op in the tree — it paints nothing, owns no points and consumes
        ///       no input. An item that takes part in a visual tree is a subclass (widget_t, a host,
        ///       a custom visual element); the base itself is not instantiable.
        item_t() = default;

        /// @brief Draw this item's own content for the frame carried by @p event.
        /// @param event The paint event; event.painter() is already translated to this item and
        ///        clipped to its rect().
        /// @note This is an EXTENSION POINT: the default draws nothing, so an item whose pixels are
        ///       produced elsewhere (a container that only arranges children, a spacer) has nothing
        ///       to override. widget_t::paint() dispatches the event; item_t::paint() itself is the
        ///       virtual entry a container calls on a child.
        /// @note Hidden from the meta-info: an internal tree hook, not scriptable API. A reflected
        ///       virtual would add an entry to every derived class's table.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        virtual void paint_event(paint_event_t* event);

        /// @brief React to the item's hover state having changed.
        /// @param event The event; event->hovered() is the new state.
        /// @note This is an EXTENSION POINT: the default requests a repaint (this item and every item
        ///       ancestor), which is what a control's hover feedback needs. A row view overrides it to
        ///       damage only the rows the pointer left and entered.
        /// @note Hidden from the meta-info for the same reason as paint_event().
        [[= ::hwshqtb::metaobject::make_hidden()]]
        virtual void hover_event(hover_event_t* event);

        /// @brief React to a mouse button edge recorded on the item.
        /// @param event The event; event->button() and event->pressed() are the edge.
        /// @note This is an EXTENSION POINT, with the default of hover_event().
        /// @note Hidden from the meta-info for the same reason as paint_event().
        [[= ::hwshqtb::metaobject::make_hidden()]]
        virtual void press_event(press_event_t* event);

        /// @brief React to the item having gained or lost the keyboard focus.
        /// @param event The event; event->has_focus() is the new state.
        /// @note This is an EXTENSION POINT: the default does nothing, and a subclass that paints a
        ///       focus ring overrides it to ask for the repaint. The router fires it exactly once per
        ///       change, the lost-focus call before the gained-focus call of the new item.
        /// @note Hidden from the meta-info for the same reason as paint_event().
        [[= ::hwshqtb::metaobject::make_hidden()]]
        virtual void focus_change_event(focus_change_event_t* event);

        /// @brief React to the item rectangle having changed.
        /// @param event The event; the new rectangle is already stored on the item.
        /// @note This is an EXTENSION POINT: a container re-lays its children out here (widget_t
        ///       does), so the handler runs BEFORE the repaint request set_geometry() makes — the
        ///       children have already asked for their own damage when the frame is scheduled.
        /// @note Hidden from the meta-info for the same reason as paint_event().
        [[= ::hwshqtb::metaobject::make_hidden()]]
        virtual void geometry_change_event(geometry_change_event_t* event);

        /// @brief React to a minimum/maximum size constraint having really changed.
        /// @note This is an EXTENSION POINT: the default does nothing, and the setter already requests
        ///       the repaint. widget_t overrides it to re-apply the parent's layout and to reach the
        ///       host's frame sink.
        /// @note Hidden from the meta-info for the same reason as paint_event().
        [[= ::hwshqtb::metaobject::make_hidden()]]
        virtual void size_constraint_change_event(size_constraint_change_event_t* event);

        /// @brief React to the content extent or decoration having changed for a non-input reason
        ///        (a new model, a rows change, a new row height).
        /// @note This is an EXTENSION POINT: a subclass that paints decoration outside the content (a
        ///       table's header) repaints it here; the default does nothing.
        /// @note Hidden from the meta-info for the same reason as paint_event().
        [[= ::hwshqtb::metaobject::make_hidden()]]
        virtual void content_change_event(content_change_event_t* event);

        /// @brief React to the item's visibility flag having changed.
        /// @param event The event; event->visible() is the new state.
        /// @note This is the item's OWN reaction, fired before visible_changed() reaches the
        ///       observers. widget_t overrides it to ask its host for a frame.
        /// @note Hidden from the meta-info for the same reason as paint_event().
        [[= ::hwshqtb::metaobject::make_hidden()]]
        virtual void visible_change_event(visible_change_event_t* event);

        /// @brief React to the item's enabled flag having changed.
        /// @param event The event; event->enabled() is the new state.
        /// @note This is the item's OWN reaction, fired before enabled_changed() reaches the
        ///       observers. widget_t overrides it to ask its host for a frame.
        /// @note Hidden from the meta-info for the same reason as paint_event().
        [[= ::hwshqtb::metaobject::make_hidden()]]
        virtual void enabled_change_event(enabled_change_event_t* event);

        /// @brief Install (or clear) the focus sink of this item.
        /// @param sink The callable invoked with the item that asks for the focus (nullptr clears);
        ///        an empty function removes the sink.
        /// @note A topmost item (the router's root) gets it from ui::input_router_t; a host installs
        ///       its own so requests from its internal controls stop at the host boundary.
        /// @note Protected: an implementation that owns a focus level installs it; not reflected.
        void set_focus_sink(std::function<void(item_t*)> sink);

        /// @brief The rectangle relative to the parent item.
        ::hwshqtb::gui::rect_t _geometry{};
        /// @brief Whether the item is visible (written by set_visible()).
        bool _visible = true;
        /// @brief Whether the item is enabled (written by set_enabled()).
        bool _enabled = true;
        /// @brief The horizontal growth/shrink policy.
        size_policy_enum _horizontal_policy = size_policy_enum::PREFERRED;
        /// @brief The vertical growth/shrink policy.
        size_policy_enum _vertical_policy = size_policy_enum::PREFERRED;
        /// @brief The growth weight used when spare space is distributed.
        int _stretch = 0;
        /// @brief Where a layout puts this item inside a cell larger than it (NONE = fill the cell).
        alignment_t _alignment{};
        /// @brief The lower bound of the hard size constraints.
        ::hwshqtb::gui::extent_t _minimum_size{};
        /// @brief The upper bound of the hard size constraints.
        ::hwshqtb::gui::extent_t _maximum_size = unbounded_extent;

    private:
        /// @brief Record that this item gained or lost the focus and dispatch the matching event.
        /// @param has_focus The new state.
        /// @note Called only by ui::input_router_t; dispatches exactly one focus_change_event_t on a
        ///       real change.
        void set_has_focus(bool has_focus);

        /// @brief The nearest item on the parent chain (this one when topmost) that installed a
        ///        pointer sink, or nullptr when nobody can hold the pointer.
        /// @note The walk skips this item's OWN sink, which belongs to its descendants — the same
        ///       rule as set_focus(): only a topmost item falls back to its own sink.
        item_t* pointer_sink_owner();

        /// @brief How this item can receive keyboard focus (W3; no_focus by default).
        focus_policy_enum _focus_policy = focus_policy_enum::NO_FOCUS;
        /// @brief Whether this item currently holds the focus (maintained by the router).
        bool _has_focus = false;
        /// @brief The focus sink the host installed (only present on a topmost item with a router).
        std::function<void(item_t*)> _focus_sink;

        /// @brief Whether the pointer is currently over this item (maintained by the input session).
        bool _hovered = false;
        /// @brief The held buttons, as bits of gui::mouse_button_flag.
        std::uint8_t _pressed_buttons = 0;
        /// @brief Whether this item owns the pointer (maintained through the pointer sink).
        bool _captures_pointer = false;
        /// @brief The pointer sink the host installed (only present on an item with an input session).
        std::function<void(item_t*)> _pointer_sink;

        /// @brief Whether this item fills its whole rectangle opaquely (see set_opaque()).
        bool _opaque = false;

        /// @brief The object that receives this tree's host requests, or nullptr.
        /// @note Set on the root; request_receiver() walks up to it.
        ::hwshqtb::metaobject::object_t* _request_receiver = nullptr;

        /// @brief The router is the focus authority: it installs the sink and drives has_focus().
        friend class input_router_t;
        /// @brief The immediate host drives the item-level focus of its internal controls.
        friend class im_host_t;
        /// @brief The paint traversal: a container hands its item children the pass it is painting.
        /// @note paint_event() is protected (a subclass overrides it), so the container that RECURSES
        ///       over its children must be allowed to call it on them. A friend declaration covers the
        ///       class's own member functions only, NOT its subclasses — a container that paints a
        ///       non-child item through the same entry (a viewport forwarding to its content) therefore
        ///       has to be named here as well.
        friend class widget_t;
        friend class viewport_t;
    };

    /// @brief Whether a mouse press inside @p item moves the focus to it.
    /// @param item The candidate (a widget or an immediate host).
    /// @return true when @p item can hold focus (visible in the window, enabled, not no_focus) and its
    ///         policy is click_focus or strong_focus.
    /// @note Header-only: the router's press rule and the immediate host's inner rule share this one
    ///       predicate, so both hosts agree on what "clickable focus" means.
    inline bool accepts_click_focus(const item_t& item) {
        const focus_policy_enum policy = item.focus_policy();
        return item.is_focusable()
            && (policy == focus_policy_enum::CLICK_FOCUS || policy == focus_policy_enum::STRONG_FOCUS);
    }
}

#endif
