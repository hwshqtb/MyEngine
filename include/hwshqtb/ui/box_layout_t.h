#ifndef HWSHQTB__UI__BOX_LAYOUT_T_H
#define HWSHQTB__UI__BOX_LAYOUT_T_H

/// @file box_layout_t.h
/// @brief The one-dimensional box layouts: box_layout_t / hbox_layout_t / vbox_layout_t (stage W2).

#include "hwshqtb/ui/layout_t.h"

namespace hwshqtb::ui {
    class widget_t;

    /// @brief The axis a box layout arranges its children along.
    enum struct box_orientation_enum {
        HORIZONTAL,  ///< Children left to right (QHBoxLayout).
        VERTICAL,    ///< Children top to bottom (QVBoxLayout).
    };

    /// @brief A one-dimensional layout: children are packed along one axis, all get the full cross
    ///        extent (the QBoxLayout algorithm, minimal form).
    ///
    /// @par Main-axis algorithm
    /// 1. the content area is the layout area minus margins();
    /// 2. every item child starts at effective_size_hint() (the hard constraint wins over the
    ///    child's opinion, which the effective triple clamps into [minimum_size, maximum_size]), with
    ///    spacing() between neighbours;
    /// 3. if the content exceeds the available space the children shrink proportionally to the room
    ///    above their effective_minimum_size(), so a child whose floor already reaches its hint (a
    ///    FIXED policy child) does not shrink at all; when even the floors do not fit, the biggest
    ///    children are cut first, below their floor if necessary, and the row still fits the content
    ///    area (the Qt qGeomCalc overfull rule, see detail::resolve_extents()). A FIXED child is cut
    ///    there like any other: "fixed" survives a shortage only while the floors fit;
    /// 4. if space is left over it is distributed over the children with size_policy_enum::EXPANDING,
    ///    in proportion to their stretch(), never above their effective_maximum_size(); pixels a
    ///    child cannot take because it reached its maximum go to the remaining expanding children.
    ///    With no expanding child the remainder stays unused, and expanding children whose stretches
    ///    are all zero share it equally. A FIXED child takes no share (it never grows), and neither
    ///    does a child aligned on this axis (see the Alignment section below).
    ///
    /// @par Cross axis
    /// Every child receives the full content extent on the cross axis capped by its
    /// effective_maximum_size(), except a child with a fixed size constraint, which keeps the fixed
    /// cross extent (both axes of a fixed size are guaranteed). The cross axis is not a compression
    /// stage: it enforces neither the minimum floor nor the overfull rule.
    ///
    /// @par Alignment
    /// item_t::alignment() acts on two different things, one per axis.
    ///
    ///  - The CROSS axis uses the bits to PLACE the child: its cell is the whole content extent, so a
    ///    child that came out smaller than the cell (capped by its maximum, pinned by a fixed size)
    ///    sits at the top/bottom/centre of the row or column.
    ///  - The MAIN axis uses the bits to SIZE the child, not to move it: the cell is the resolved slot
    ///    — Qt's QBoxLayout hands every item a cell of its slot size
    ///    (qtbase/src/widgets/kernel/qboxlayout.cpp) — so the cell is never wider than the child and
    ///    every offset the bits could ask for is 0. What the bits do here is cap the child's
    ///    allocation at its own effective_size_hint() on that axis (Qt's qSmartMaxSize(), reached
    ///    through QWidgetItem::maximumSize()) and take it out of the expansion distribution: an
    ///    EXPANDING child carrying, say, HCENTER in a row keeps its hint width, and the spare space
    ///    goes to the remaining expanding children or stays unused.
    ///
    /// The compression and overfull rules never read the bits: when the content does not fit, an
    /// aligned child is cut exactly like an unaligned one. An axis without a bit keeps the fill/size
    /// behaviour it had before alignment existed, so alignment_flag::NONE — the default — lays out
    /// byte for byte as before.
    class HWSHQTB_UI_EXPORT box_layout_t : public layout_t {
    public:
        /// @brief Construct a box layout along @p orientation.
        /// @param orientation horizontal (default) or vertical.
        explicit box_layout_t(box_orientation_enum orientation = box_orientation_enum::HORIZONTAL);

        /// @brief Lay the parent's item children out along the box axis.
        /// @param parent The widget whose children are arranged.
        /// @param area The parent's client rect.
        void apply(widget_t& parent, ::hwshqtb::gui::rect_t area) override;

        /// @brief The natural size of the container once laid out by this box.
        /// @return On the main axis: the sum of the children's effective_size_hint() plus the spacing
        ///         between them plus the corresponding margins. On the cross axis: the largest child
        ///         effective hint plus the corresponding margins. An empty box answers the margins'
        ///         sum.
        /// @note The container has an opinion only because the children can be enumerated (the
        ///       retained-mode advantage; an immediate host has nothing to walk).
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief The smallest size this box can arrange its children at (QLayout::minimumSize).
        /// @return The same composition as size_hint() with each child's effective_minimum_size() in
        ///         place of its effective hint: the sum plus the spacing on the main axis, the largest
        ///         one plus the margins on the cross axis. An empty box answers the margins' sum.
        /// @note A child whose hard minimum_size() rose raises this floor, which is how a container
        ///       answers "the children need at least this much".
        ::hwshqtb::gui::extent_t minimum_size() const override;

        /// @brief The arrangement axis.
        box_orientation_enum orientation() const;

    protected:
        /// @brief The arrangement axis (fixed for hbox_layout_t / vbox_layout_t).
        box_orientation_enum _orientation = box_orientation_enum::HORIZONTAL;

    private:
        /// @brief The shared body of size_hint() and minimum_size().
        /// @param minimum true aggregates effective_minimum_size(), false effective_size_hint().
        ::hwshqtb::gui::extent_t aggregate(bool minimum) const;
    };

    /// @brief A horizontal box layout (QHBoxLayout): children left to right.
    class HWSHQTB_UI_EXPORT hbox_layout_t final : public box_layout_t {
    public:
        /// @brief Construct a horizontal box layout.
        hbox_layout_t();
    };

    /// @brief A vertical box layout (QVBoxLayout): children top to bottom.
    class HWSHQTB_UI_EXPORT vbox_layout_t final : public box_layout_t {
    public:
        /// @brief Construct a vertical box layout.
        vbox_layout_t();
    };
}

#endif
