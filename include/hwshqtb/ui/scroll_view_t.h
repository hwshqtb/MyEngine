#ifndef HWSHQTB__UI__SCROLL_VIEW_T_H
#define HWSHQTB__UI__SCROLL_VIEW_T_H

/// @file scroll_view_t.h
/// @brief scroll_view_t: a viewport with two scroll bars, offsets and ranges around its content.

#include <cstdint>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/viewport_t.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    class scroll_bar_t;

    /// @brief When a scroll view shows a scroll bar.
    enum struct scroll_bar_policy_enum : std::uint8_t {
        /// @brief Always show the bar, even when there is nothing to scroll.
        ALWAYS_ON,
        /// @brief Never show the bar; the content is clipped and that axis cannot scroll.
        ALWAYS_OFF,
        /// @brief Show the bar only when the content is larger than the destination (the default).
        AS_NEEDED,
    };

    /// @brief A viewport that scrolls: it creates the two scroll bars, owns the offsets and the
    ///        ranges, and shifts its source rectangle instead of moving the content.
    ///
    /// @par The content
    /// There is exactly one content item. set_content() takes ownership: the content is reparented
    /// to this view and the PREVIOUS content is deleted (Qt's QScrollArea::setWidget behaviour), so a
    /// caller must not keep using a replaced content. The content's extent is its
    /// effective_size_hint(); a view whose rows are not item children makes its content item report
    /// the row grid's extent.
    ///
    /// @par Scrolling is changing the source rectangle
    /// "Scrolling" moves source_rect() over the content: the content item's geometry never changes
    /// and its pixels are never moved. set_scroll_offset() updates the offset, source_rect() and the
    /// two bars' values; painting then translates the content by the difference of the source and
    /// destination origins (see viewport_t::paint_content()).
    ///
    /// @par The destination and the ranges
    /// The visible content area is destination_rect(): the body minus the band a subclass reserves
    /// (a table's header). Both ranges are computed from it, NOT from body_rect(): reserving a band
    /// must not enlarge the scrollable range. The destination defaults to body_rect(), which is
    /// rect() minus the strips of the VISIBLE scroll bars.
    ///
    /// @par The bars
    /// The two bars are ordinary child items of this view, so the router hit-tests and drives them
    /// like any other control. The base scrolls the wheel and the keyboard (while this view holds the
    /// focus); subclasses keep their own input contract.
    ///
    /// @par The fast path
    /// A scroll step asks the host to copy the already-painted content pixels and repaints only the
    /// decoration the copy leaves behind (the frame ring and a subclass's overlay). The source-
    /// destination translation makes the shifted and the fully repainted frame identical.
    class HWSHQTB_UI_EXPORT scroll_view_t: public viewport_t {
        OBJECT

    public:
        /// @brief Construct an empty scroll view with both bars and the AS_NEEDED policy.
        scroll_view_t();

        /// @brief Construct an empty scroll view inside a retained widget tree.
        /// @param parent The visual parent; nullptr makes it a root widget.
        explicit scroll_view_t(widget_t* parent);

        /// @brief Install the single content item and take ownership of it.
        /// @param content The new content; nullptr removes the content (the previous one is deleted).
        /// @note The previous content is DELETED. This view and its scroll bars are rejected (the
        ///       call is ignored).
        void set_content(item_t* content) override;

        /// @brief The extent both scroll ranges are derived from.
        /// @return The content's effective_size_hint(), or {0, 0} without a content.
        virtual ::hwshqtb::gui::extent_t content_extent() const;

        /// @brief The current scroll offset.
        /// @return x is the horizontal offset and y the vertical one; an axis without a range
        ///         answers 0.
        ::hwshqtb::gui::point_t scroll_offset() const noexcept;

        /// @brief Scroll to @p offset; each axis is clamped into its range.
        /// @param offset The wanted scroll offset: x is the horizontal axis, y the vertical one
        ///        (0 is the start of the content).
        /// @note Only the offset, source_rect() and the two bars' values change: the content item's
        ///       geometry is NOT touched.
        void set_scroll_offset(::hwshqtb::gui::point_t offset);

        /// @brief The policy of both scroll bars.
        scroll_bar_policy_enum scroll_bar_policy() const;

        /// @brief Install the policy of BOTH scroll bars.
        /// @param policy The new policy (AS_NEEDED by default).
        void set_scroll_bar_policy(scroll_bar_policy_enum policy);

        /// @brief The vertical scroll bar (an owned child item; never nullptr).
        scroll_bar_t* vertical_scroll_bar() const;

        /// @brief The horizontal scroll bar (an owned child item; never nullptr).
        scroll_bar_t* horizontal_scroll_bar() const;

        /// @brief Scroll so @p item (the content or one of its descendants) is fully visible.
        /// @param item The item to reveal; nullptr or an item outside the content is ignored.
        /// @note Only the scroll offset changes; the item's geometry is not touched.
        void ensure_visible(item_t* item);

        /// @brief The body: rect() minus the strips of the visible scroll bars.
        /// @return The area the content may occupy, in this widget's coordinates.
        /// @note The frame is NOT subtracted: the content paints over it, exactly as the widget
        ///       paints the frame before the content. The reserved band of a subclass (a table's
        ///       header) is not subtracted either; destination_rect() is the area the content gets.
        ::hwshqtb::gui::rect_t body_rect() const override;

        /// @brief The natural size: the content's effective_minimum_size() plus one bar thickness per
        ///        axis unless the policy is ALWAYS_OFF.
        /// @note Never the content's natural size and never the scroll range: overflow enlarges the
        ///       ranges, not this hint. A view with no content answers {0, 0}.
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief The smallest size the view can lay out: both bars plus a one-pixel body.
        /// @note Independent of the content: a scroll view may shrink to the bars and scroll.
        ::hwshqtb::gui::extent_t minimum_size_hint() const override;

        /// @brief Drive the wheel and the keyboard; mouse steps belong to the bars and the content.
        /// @param in The input snapshot, in this view's local coordinates.
        /// @return true when the view consumed the step; false otherwise, so the step keeps bubbling.
        bool interact(const ::hwshqtb::gui::interaction_t& in) override;

    protected:
        /// @brief Draw the frame and the content, then the item children (the content skipped).
        /// @param event The paint event; event.painter() is already translated to this widget and
        ///        clipped to rect().
        void paint_event(paint_event_t* event) override;

        /// @brief Resize the view and re-place the bars and the content.
        /// @param event The geometry event; the new rectangle is already stored on the item.
        /// @note The widget layer re-lays the children out first; the scroll state (bars,
        ///       destination, ranges, source) is then re-derived from the new rectangle.
        /// @note Hidden from the meta-info like the base hook.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void geometry_change_event(geometry_change_event_t* event) override;

        /// @brief Recompute the bars, the destination, the ranges and the source rectangle.
        /// @note Idempotent and guarded against re-entrancy: a bar is only written when its value
        ///       differs, so a paint pass cannot keep requesting repaints.
        virtual void update_layout();

        /// @brief The area the content occupies inside @p body.
        /// @param body body_rect() for the current bars.
        /// @return @p body by default; a table reserves its header band here.
        /// @note The ranges and the destination are both derived from this, so a subclass decides
        ///       the visible content area in one place.
        virtual ::hwshqtb::gui::rect_t destination_for_body(::hwshqtb::gui::rect_t body) const;

        /// @brief Whether the horizontal bar is needed for a destination @p viewport_width wide.
        /// @param viewport_width The destination width the visible bars leave.
        /// @return true when the content is wider than the space it would get.
        /// @note The default compares the content with the destination, so a content whose width is
        ///       whatever space it is given (a row list) never justifies a bar that would then
        ///       shrink the destination below that width.
        virtual bool horizontal_bar_needed(int viewport_width) const;

        /// @brief Called after the offsets really changed.
        /// @note A subclass whose painted decoration is not part of the shifted content (a table's
        ///       header follows the horizontal offset) repaints it here.
        virtual void body_scrolled();

        /// @brief The current vertical offset in pixels (0 is the content's top edge).
        int vertical_offset() const noexcept;

        /// @brief The current horizontal offset in pixels (0 is the content's left edge).
        int horizontal_offset() const noexcept;

        /// @brief The largest legal vertical offset of the current layout.
        int vertical_range() const noexcept;

        /// @brief The largest legal horizontal offset of the current layout.
        int horizontal_range() const noexcept;

        /// @brief Scroll the vertical axis to @p offset, clamped, with the pixel-shift fast path.
        /// @param offset The wanted vertical offset.
        void set_vertical_offset(int offset);

        /// @brief Scroll the horizontal axis to @p offset, clamped, with the pixel-shift fast path.
        /// @param offset The wanted horizontal offset.
        void set_horizontal_offset(int offset);

        /// @brief Move both offsets to 0 without shifting pixels or re-deriving the hovered state.
        /// @note For a contents reset: the caller repaints the visible content itself.
        void reset_offsets();

        /// @brief Put @p widget at @p rect without recording damage.
        /// @param widget The child to place.
        /// @param rect The new geometry, in this view's coordinates.
        /// @note For a scroll view that has already accounted for the pixels: the paint path and the
        ///       damage records own the repaint, so a placement must not damage the whole content.
        void set_child_geometry_silently(widget_t& widget, ::hwshqtb::gui::rect_t rect);

    private:
        /// @brief The part of the destination whose pixels may be copied by a scroll: the destination
        ///        minus the frame lines that lie on this widget's own edges.
        ::hwshqtb::gui::rect_t copiable_rect() const;

        /// @brief Mark the frame ring as damaged.
        /// @note It is repainted where it belongs, never shifted, so a decoration drawn there is
        ///       restored instead of being dragged along with the content.
        void damage_decoration();

        /// @brief Ask the window to copy the content's pixels by (dx, dy) and repaint what it uncovers.
        /// @param dx The horizontal shift of the content, in pixels.
        /// @param dy The vertical shift of the content, in pixels.
        /// @note A shift of at least one destination extent cannot reveal already-painted pixels, so
        ///       the destination is repainted instead.
        void scroll_body(int dx, int dy);

        /// @brief Clamp both axes into their ranges, update the source rectangle and apply the shift.
        void apply_offsets(int horizontal, int vertical);

        /// @brief Write @p value into @p bar unless the bar already holds it.
        /// @note Guarded against the bar's own value_changed() signal re-entering the offsets.
        void sync_bar_value(scroll_bar_t& bar, int value);

        /// @brief Put the content at its own geometry for the current extent.
        void place_content();

        /// @brief Whether at least one bar is visible (the view has something to scroll).
        bool any_bar_visible() const;

        /// @brief Whether any control inside the content currently holds the keyboard focus.
        bool content_has_focus() const;

        /// @brief The owned vertical scroll bar.
        scroll_bar_t* _vertical_bar = nullptr;
        /// @brief The owned horizontal scroll bar.
        scroll_bar_t* _horizontal_bar = nullptr;
        /// @brief The vertical scroll offset (0 is the content's top edge).
        int _vertical_offset = 0;
        /// @brief The horizontal scroll offset (0 is the content's left edge).
        int _horizontal_offset = 0;
        /// @brief The largest legal vertical offset of the current layout.
        int _vertical_range = 0;
        /// @brief The largest legal horizontal offset of the current layout.
        int _horizontal_range = 0;
        /// @brief The policy of both bars.
        scroll_bar_policy_enum _policy = scroll_bar_policy_enum::AS_NEEDED;
        /// @brief Guards update_layout() and the bar writes against re-entrancy.
        bool _updating = false;
    };
}

#endif
