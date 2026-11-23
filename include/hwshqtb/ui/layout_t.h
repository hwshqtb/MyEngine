#ifndef HWSHQTB__UI__LAYOUT_T_H
#define HWSHQTB__UI__LAYOUT_T_H

/// @file layout_t.h
/// @brief layout_t: the base class of the layout algorithms (stage W2).

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/ui/export.h"

namespace hwshqtb::ui {
    class widget_t;

    /// @brief Arranges a widget's item children inside an area (the QLayout subset W2 needs).
    ///
    /// @par Ownership and wiring
    /// widget_t::set_layout() takes ownership and deletes a previously installed layout, so a layout
    /// is never shared between two parents. widget_t::relayout() calls apply() with the parent's
    /// client rectangle (its own rect(), i.e. an origin at the parent's top-left); the layout writes
    /// every managed child's geometry through item_t::set_geometry(), which requests the repaint.
    ///
    /// @par Children
    /// A layout sees only the parent's ITEM children (widget_t::item_count() /
    /// widget_t::item_at_index()); plain object_t children of the same parent — controllers, models,
    /// timers — are ignored, exactly like the rest of the widget traversal. item_at_index() already
    /// filters out non-items, so apply() needs no type check of its own. The child ORDER is the
    /// object tree's order.
    ///
    /// @par Margins and spacing
    /// apply() first shrinks @p area by margins() on all four sides, then inserts spacing() between
    /// neighbours. A negative spacing is treated as zero; negative margins are allowed and simply
    /// enlarge the content area.
    ///
    /// @par Coordinates
    /// The geometry written to a child is relative to @p parent: apply() receives the parent's
    /// client rect (origin at the parent's top-left), not a global rectangle.
    class HWSHQTB_UI_EXPORT layout_t {
    public:
        /// @brief Destroys the layout (owned by the widget that installed it).
        virtual ~layout_t() = default;

        /// @brief Lay out the widget's item children (non-item children are ignored) inside
        ///        @p area, which is the parent's client rect (origin == parent's top-left).
        /// @param parent The widget whose children are arranged.
        /// @param area The parent's client rectangle, in the parent's own coordinates.
        /// @note The default arranges nothing, so a layout that only reserves space (or that is
        ///       installed before its rule is known) is a legal layout.
        /// @note This is an EXTENSION POINT: every layout algorithm is exactly this one override.
        virtual void apply(widget_t& parent, ::hwshqtb::gui::rect_t area);

        /// @brief The natural size this layout asks for its owner (the container's sizeHint).
        ///
        /// The layout derives it from its item children's effective_size_hint() — the spacing between
        /// them and its own margins included. That is the container's opinion, and it exists only
        /// because the children can be enumerated: a retained-mode capability the immediate side does
        /// not have.
        ///
        /// @return The natural extent. A layout with no item children (including one that was never
        ///         installed) answers the margins' sum; a grid answers from its explicit entries even
        ///         before installation.
        /// @note The hint is computed on demand; nothing is cached, so a child whose content changed
        ///       is reflected on the next question without an explicit invalidation step.
        virtual ::hwshqtb::gui::extent_t size_hint() const;

        /// @brief The smallest size this layout can arrange its items at (QLayout::minimumSize).
        /// @return The margins plus the aggregate of the items' effective_minimum_size(), composed
        ///         exactly like size_hint() composes their effective_size_hint(): a box sums its
        ///         children on the main axis and takes the largest one on the cross axis, a grid sums
        ///         the per-line minima (a spanning item contributes ceil(minimum / span) to each line
        ///         it covers).
        /// @note The default implementation answers size_hint(): a layout that publishes no separate
        ///       floor cannot be arranged smaller than the size it asks for. Override it when the
        ///       arrangement has a floor of its own — box_layout_t and grid_layout_t do.
        /// @note Computed on demand and never cached, exactly like size_hint().
        virtual ::hwshqtb::gui::extent_t minimum_size() const;

        /// @brief The current margins.
        ::hwshqtb::gui::margins_t margins() const;

        /// @brief Set the four edge gaps subtracted from the layout area.
        /// @param m The new margins.
        void set_margins(::hwshqtb::gui::margins_t m);

        /// @brief The current spacing.
        int spacing() const;

        /// @brief Set the gap inserted between two neighbouring children.
        /// @param s The gap in pixels; a negative value behaves like 0 in apply().
        void set_spacing(int s);

    protected:
        /// @brief The edge gaps of the layout area.
        ::hwshqtb::gui::margins_t _margins{};
        /// @brief The gap between neighbours (0 when the layout is tight).
        int _spacing = 0;
        /// @brief The widget that installed this layout; nullptr until widget_t::set_layout() stores it.
        widget_t* _parent = nullptr;

    private:
        /// @brief widget_t::set_layout() stores the owner so size_hint() can reach the item children.
        friend class widget_t;
    };
}

#endif
