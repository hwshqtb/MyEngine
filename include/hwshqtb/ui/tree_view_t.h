#ifndef HWSHQTB__UI__TREE_VIEW_T_H
#define HWSHQTB__UI__TREE_VIEW_T_H

/// @file tree_view_t.h
/// @brief tree_view_t: the hierarchical item view — indentation, expanders and expand/collapse
///        navigation over the virtualised row machinery of abstract_item_view_t.

#include <set>
#include <vector>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/ui/abstract_item_view_t.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/item_model_t.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    /// @brief A tree of model nodes: every visible node is one row, a row is indented by its depth and
    ///        a node with children carries an expander (the QTreeView subset this layer needs).
    ///
    /// @par Rows are the visible nodes, not the model's rows
    /// The view keeps a flattened list of the nodes that are currently shown — the top-level rows plus
    /// the children of every expanded node, in display order — and its row numbers are indices into
    /// that list. Everything the base does with a row (painting, the hit test, per-row damage, the
    /// keyboard, the selection, the scroll range) therefore works on the rows the user sees, one row
    /// per visible node, with the fixed row height unchanged.
    ///
    /// @par The table is rebuilt, never patched
    /// Expanding or collapsing a node, a row insert/remove and a new model all re-derive the whole
    /// list from the model. The rebuild re-maps the current row, the anchor and the selected set
    /// through the node identities, so a current node or a selection follows its node instead of
    /// staying on a row number that now holds something else.
    ///
    /// @par Identity is the model's
    /// Expansion state is remembered by item_index_t, so a hierarchical model must fill node with a
    /// stable, unique value per node (a flat model leaves it null and has no children to expand). An
    /// index that the view does not show — a child of a collapsed parent — cannot be expanded: reveal
    /// its ancestors with expand() first.
    ///
    /// @par What the model must answer
    /// row_count(parent), index(row, column, parent), parent(index) and data(index, DISPLAY). A model
    /// that only implements the flat row form shows up as a tree of top-level rows, no expanders.
    ///
    /// @par Deliberately absent
    /// No header (the model's column 0 is the only column shown), no custom delegate or editing, no
    /// variable row height, no multi-column rows, no drag/drop, no sorting or filtering, no lazy
    /// population (a node's children are read when the node is expanded) and no per-column selection.
    class HWSHQTB_UI_EXPORT tree_view_t: public abstract_item_view_t {
        OBJECT

    public:
        /// @brief Construct an empty tree with no model, no current row and no expansion.
        /// @param parent The visual parent; nullptr makes the tree a root widget.
        /// @note The indentation starts at the style's TREE_INDENTATION.
        explicit tree_view_t(widget_t* parent = nullptr);

        tree_view_t(const tree_view_t&) = delete;
        tree_view_t& operator=(const tree_view_t&) = delete;
        tree_view_t(tree_view_t&&) = delete;
        tree_view_t& operator=(tree_view_t&&) = delete;

        /// @brief The horizontal distance one level indents its rows (TREE_INDENTATION until set).
        int indentation() const noexcept;

        /// @brief Set the horizontal distance one level indents its rows.
        /// @param pixels The indentation in pixels; a negative value is treated as 0.
        /// @note Every row's expander and text move, so the cached content width, the horizontal range
        ///       and the view are re-derived; the model is not asked anything.
        void set_indentation(int pixels);

        /// @brief Show the children of @p index.
        /// @param index The node; it must be a visible row and must have children.
        /// @note Does nothing — no rebuild, no signal — when the node is not visible, has no children
        ///       or is already expanded, so repeated calls are free.
        /// @note Emits expanded_changed(index, true) on a real change.
        void expand(const item_index_t& index);

        /// @brief Hide the children of @p index.
        /// @param index The node; an index that is not expanded is ignored.
        /// @note Unlike expand(), the node need not be visible: collapsing is a pure state change that
        ///       takes effect the next time the node is shown.
        /// @note A current row or a selection inside the hidden subtree is re-mapped through the
        ///       rebuild: a node that disappeared leaves no current row and no selected row.
        /// @note Emits expanded_changed(index, false) on a real change.
        void collapse(const item_index_t& index);

        /// @brief Expand @p index when it is collapsed, collapse it when it is expanded.
        /// @param index The node; an index that cannot be expanded is ignored.
        void toggle_expanded(const item_index_t& index);

        /// @brief Whether @p index is expanded (its children are in the visible list).
        /// @return true only for a node the view remembers as expanded; a node that cannot have
        ///         children is never expanded.
        bool is_expanded(const item_index_t& index) const;

        /// @brief The node the current row shows, or an invalid index when no row is current.
        item_index_t current_index() const;

        /// @brief Make the node @p index current.
        /// @param index The node; it must be one of the visible rows.
        /// @note An index the view does not show is ignored (no current row is cleared): expand the
        ///       node's ancestors first. The row half goes through set_current_row(), so the selection
        ///       follows it exactly as it does for a list.
        /// @note Does not scroll by itself: use the inherited scroll_to_row() to reveal the row.
        void set_current_index(const item_index_t& index);

        /// @brief The node under @p local, or an invalid index.
        /// @param local The point in this view's coordinates.
        /// @return The node of the row whose band contains the point; the area below the last row,
        ///         both scroll bars and the area outside the view answer an invalid index.
        item_index_t index_at(::hwshqtb::gui::point_t local) const;

        /// @brief How many rows the tree currently shows: the flattened visible list's size.
        /// @return The size of the visible list, which is what the inherited row operations use.
        int visible_row_count() const override;

        /// @brief The widest visible row, indentation and expander included.
        /// @return @p max(the widest row from the cache, the body width), so the rows still fill the
        ///         viewport; the cached part is what a paint pass reads, so it asks the model nothing.
        /// @note This is the extent the inherited horizontal bar scrolls. The bar appears when the
        ///       widest row exceeds the VIEW's width (the base's rule, which keeps a bar from shrinking
        ///       the destination below the width that justified it), so a row wider than the
        ///       destination but not wider than the view is clipped at its end instead of scrolling.
        int content_width() const override;

        /// @brief The natural size: a fixed default viewport, independent of the node count.
        /// @return The style's LIST_VIEW_WIDTH x LIST_VIEW_HEIGHT.
        /// @note A tree of 1 node and a tree of 1,000,000 nodes ask for the same size; the content
        ///       changes the scroll range, never the hint.
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief Drive the inherited input, and toggle a node when the press lands on its expander.
        /// @param in The input snapshot, in this view's local coordinates.
        /// @return true when the view consumed the step.
        /// @note A press on an expander toggles that node and touches neither the current row nor the
        ///       selection, and it records no pressed row, so the release that follows cannot activate
        ///       a row either.
        bool interact(const ::hwshqtb::gui::interaction_t& in) override;

        /// @brief Emitted when a node's expanded state actually changed.
        /// @param index The node.
        /// @param expanded true when it was expanded, false when it was collapsed.
        /// @note A repeated expand()/collapse() of the same state emits nothing.
        SIGNAL(void expanded_changed(const item_index_t& index, bool expanded) const)

    protected:
        /// @brief Paint one row: the inherited background states, then the row's expander (a node with
        ///        children only) and its DISPLAY text, both placed from the row's depth.
        /// @param painter The painter, in the row content's coordinates.
        /// @param row The visible row to paint.
        /// @param rect The row's full content rectangle; the indentation starts at its left edge.
        void paint_row(::hwshqtb::gui::painter_t& painter, int row,
                       ::hwshqtb::gui::rect_t rect) const override;

        /// @brief Re-measure the changed rows, then damage exactly those rows (the base's rule).
        /// @param event The data event; event.first() and event.last() are the changed visible rows.
        /// @note A changed text width moves the cached content width; the whole destination is then
        ///       damaged instead, because the rows' geometry moved with the horizontal range.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void data_change_event(data_change_event_t* event) override;

        /// @brief Rebuild the visible list after an insert/remove, then let the base clamp the rows.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void rows_change_event(rows_change_event_t* event) override;

        /// @brief Handle Left (collapse, or move to the parent) and Right (expand, or move to the first
        ///        child); every other key goes to the base.
        /// @param in The key step.
        /// @return true for Left/Right, false otherwise.
        bool handle_extra_key(const ::hwshqtb::gui::interaction_t& in) override;

        /// @brief Forget every expansion and rebuild the visible list before the base resets the rows.
        void reset_contents() override;

    private:
        /// @brief One row of the flattened visible list.
        struct row_info_t {
            /// @brief The node the row shows.
            item_index_t index{};
            /// @brief How deep the node sits, 0 for the top level: the indentation multiplier.
            int depth = 0;
            /// @brief Whether the node has children, i.e. whether the row draws an expander.
            bool expandable = false;
            /// @brief The width of the node's DISPLAY text, without indentation and expander.
            int text_width = 0;
        };

        /// @brief Orders nodes by identity, and is the key order of the expansion set.
        /// @note The identity is the model's node pointer (plus the column) when the model supplies one,
        ///       and the (row, column) pair when it does not. The row of a node that HAS a node pointer
        ///       is not part of the key, so a node keeps its expanded state when a sibling inserted in
        ///       front of it changes its row.
        struct index_less_t {
            /// @brief Whether @p left sorts before @p right.
            bool operator()(const item_index_t& left, const item_index_t& right) const noexcept;
        };

        /// @brief The visible row that shows @p index, or -1 when the view does not show it.
        int visible_row_of(const item_index_t& index) const;

        /// @brief Whether @p index has children, i.e. whether it may be expanded.
        bool is_expandable(const item_index_t& index) const;

        /// @brief The content rectangle of the expander of @p row.
        /// @return The square at the row's indentation column, in content coordinates; an empty
        ///         rectangle for a row without children.
        ::hwshqtb::gui::rect_t expander_rect(int row) const;

        /// @brief The visible row whose expander contains @p local, or -1.
        int expander_row_at(::hwshqtb::gui::point_t local) const;

        /// @brief The full content width of @p row: indentation, expander, text and both paddings.
        int row_extent(const row_info_t& row) const;

        /// @brief The DISPLAY text width of @p index, in pixels (0 when the model has none).
        int measure_text(const item_index_t& index) const;

        /// @brief Append @p parent's subtree to the visible list, one row per shown node.
        /// @param parent The parent node; an invalid index appends the top level.
        /// @param depth The depth to record for the appended rows.
        /// @note Recurses into a node only when it is expanded, which is what makes the list "the
        ///       visible nodes". The model must describe an acyclic tree.
        void flatten(const item_index_t& parent, int depth);

        /// @brief Re-derive the cached width of the widest row from the visible list (no model calls).
        void recompute_widest_row();

        /// @brief Rebuild the visible list and re-map the row-derived state through the node identities.
        void rebuild_visible_rows();

        /// @brief Re-derive the scroll geometry and repaint the destination.
        void refresh_rows();

        /// @brief Make @p row current and scroll it into view (the tree's own arrow navigation).
        /// @param row The wanted visible row; the inherited setter clamps it.
        void move_to_row(int row);

        /// @brief The nodes that are expanded, keyed by identity.
        std::set<item_index_t, index_less_t> _expanded_nodes;
        /// @brief The flattened list of the nodes on screen, in display order.
        std::vector<row_info_t> _rows;
        /// @brief The horizontal distance one level indents its rows.
        int _indentation = 0;
        /// @brief The cached content width of the widest visible row; the body width is added on top
        ///        of it by content_width(), because a view can be resized without touching the rows.
        int _widest_row = 0;
    };
}

#endif
