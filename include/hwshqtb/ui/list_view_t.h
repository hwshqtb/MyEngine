#ifndef HWSHQTB__UI__LIST_VIEW_T_H
#define HWSHQTB__UI__LIST_VIEW_T_H

/// @file list_view_t.h
/// @brief list_view_t: a vertically scrolling, single-column item view with a fixed row height.

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/ui/abstract_item_view_t.h"
#include "hwshqtb/ui/export.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    /// @brief A single-column list of model rows with one vertical scroll bar (the QListView subset
    ///        M5 needs).
    ///
    /// @par Everything except the row content is inherited
    /// The row grid, both scroll bars, the visible range, the selection, the hovered row, the
    /// keyboard, the model wiring and the size floor belong to abstract_item_view_t. This class only
    /// answers what one row looks like (its DISPLAY text) and how large a list asks to be. A list
    /// never scrolls horizontally: the inherited content_width() reports the viewport width, so the
    /// horizontal bar stays hidden.
    ///
    /// @par Deliberately absent
    /// No delegate or custom painting, no editing, no variable row height, no table, no tree, no
    /// rubber-band selection and no tooltip.
    class HWSHQTB_UI_EXPORT list_view_t: public abstract_item_view_t {
        OBJECT

    public:
        /// @brief Construct an empty list with no model, no current row and no selection.
        /// @param parent The visual parent; nullptr makes the list a root widget.
        explicit list_view_t(widget_t* parent = nullptr);

        list_view_t(const list_view_t&) = delete;
        list_view_t& operator=(const list_view_t&) = delete;
        list_view_t(list_view_t&&) = delete;
        list_view_t& operator=(list_view_t&&) = delete;

        /// @brief The natural size: a fixed default viewport, independent of the row count.
        /// @return The style's LIST_VIEW_WIDTH x LIST_VIEW_HEIGHT.
        /// @note A list of 1 row and a list of 1,000,000 rows ask for the same size; the content
        ///       changes the scroll RANGE, never the hint.
        ::hwshqtb::gui::extent_t size_hint() const override;

    protected:
        /// @brief Paint one row: the inherited background states, then its DISPLAY text —
        ///        left-aligned, padded and clipped to its text cell.
        /// @param painter The painter, in the row content's coordinates.
        /// @param row The row whose text is painted.
        /// @param rect The row's full content rectangle; the list uses it as one cell.
        void paint_row(::hwshqtb::gui::painter_t& painter, int row,
                       ::hwshqtb::gui::rect_t rect) const override;
    };
}

#endif
