#ifndef HWSHQTB__UI__ITEM_MODEL_T_H
#define HWSHQTB__UI__ITEM_MODEL_T_H

/// @file item_model_t.h
/// @brief item_model_t: the row source an item view reads, and the roles it asks for.

#include <cstdint>

#include "hwshqtb/metaobject/any_t.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/ui/export.h"

namespace hwshqtb::ui {
    /// @brief The kind of value a view asks a model for (the QAbstractItemModel role subset).
    ///
    /// @par Roles are caller-defined above USER
    /// DISPLAY is the one role every view understands. USER is the first role the model's owner may
    /// assign a meaning to: `static_cast<item_role_enum>(item_role_enum::USER + N)` is the N-th free
    /// role. The enums only share one numbering space; the model and the caller agree on all roles
    /// above DISPLAY.
    enum struct item_role_enum : std::uint8_t {
        /// @brief The text a view shows; expects a std::string_view.
        DISPLAY,
        /// @brief The first role free for the caller's own use.
        USER,
    };

    /// @brief One node of a hierarchical model (Qt's QModelIndex): a row and a column under a parent,
    ///        plus the opaque identity the model uses to tell nodes apart.
    ///
    /// @par Identity
    /// Two indices name the same node when all three fields are equal. A flat model leaves node null,
    /// so an index is addressed by (row, column) alone; a hierarchical model must put a stable value
    /// there (a pointer to its own record, say), because the (row, column) pair is unique only among
    /// ONE parent's children.
    ///
    /// @par Validity
    /// The default value is invalid (row and column are -1). is_valid() reports the pair, not the
    /// node: a valid index of a flat model has node == nullptr.
    struct item_index_t {
        /// @brief The row under the parent; -1 when invalid.
        int row = -1;
        /// @brief The column under the parent; -1 when invalid.
        int column = -1;
        /// @brief The model's own identity for the node; nullptr in a flat model.
        const void* node = nullptr;

        /// @brief Whether the index names a row/column pair.
        /// @return true when row and column are both non-negative.
        bool is_valid() const noexcept {
            return row >= 0 && column >= 0;
        }

        /// @brief Whether both indices name the same node (all three fields equal).
        friend constexpr bool operator==(const item_index_t& left, const item_index_t& right) noexcept {
            return left.row == right.row && left.column == right.column && left.node == right.node;
        }
    };

    /// @brief A row source for item views: a row count, a per-row value and change notifications.
    ///
    /// @par One input, many outputs
    /// A model is a plain object_t, not an item: it owns no geometry and is never painted. Any number
    /// of views may read the same model and every view reacts to the same notifications. The model
    /// never knows a view and never draws.
    ///
    /// @par Row validity
    /// The CALLER owns row validity: a view clamps a requested row into [0, row_count()) before it
    /// asks data(). A model that receives an out-of-range row may do anything, including throwing;
    /// this layer does not guard it.
    ///
    /// @par Hierarchy is optional
    /// The row form (row_count(), data(row, role)) describes the TOP LEVEL and is what the flat views
    /// use. A model that also has children overrides row_count(parent), index() and parent(), and a
    /// tree view then walks it through item_index_t. Every hierarchical member has a default that
    /// answers "no children, no parent, nothing to give", so a flat model serves the flat views AND
    /// shows up as a one-level tree without a single line of change.
    ///
    /// @par Cells and columns
    /// A row is a sequence of cells; column_count() says how many. The default of 1 is what lets ONE
    /// model interface serve a list and a table: a list asks only column 0 (through
    /// data(row, column, role), whose default forwards to data(row, role)), while a table asks every
    /// column. Column validity is the caller's too, exactly like row validity.
    ///
    /// @par Lifetime
    /// Views hold a NON-OWNING reference. Destroying the model while a view still points at it turns
    /// that view's model() into nullptr (the view guards the pointer); destroying the view does not
    /// touch the model.
    class HWSHQTB_UI_EXPORT item_model_t : public ::hwshqtb::metaobject::object_t {
        OBJECT

    public:
        ~item_model_t() override = default;

        /// @brief How many rows the model has.
        /// @return The row count; 0 makes every view empty.
        /// @note The default answers 0: an empty model is a legal model, so a subclass that is not
        ///       ready yet (or that only serves one role) needs no row count of its own.
        /// @note This is an EXTENSION POINT: it is the one question every view asks, and the answer
        ///       changes only with one of the row notifications below.
        /// @note This is the TOP-LEVEL row count: the number of nodes that have no parent.
        virtual int row_count() const;

        /// @brief How many children @p parent has.
        /// @param parent The parent node; an invalid index asks for the root, whose children are the
        ///        top-level rows row_count() counts.
        /// @return The child count.
        /// @note The default implementation answers 0, so a flat model needs no change and shows all
        ///       of its rows at depth 0.
        /// @note A model that returns a non-zero count here must also override index(), because the
        ///       children get their identity from it.
        /// @note This is an EXTENSION POINT: only a hierarchical model overrides it.
        virtual int row_count(const item_index_t& parent) const;

        /// @brief The index of the node at @p row / @p column under @p parent.
        /// @param row The row among the parent's children.
        /// @param column The column.
        /// @param parent The parent node; an invalid index asks for a top-level node.
        /// @return The node's index, or an invalid index when the pair is out of range.
        /// @note The default implementation checks the pair against row_count()/column_count() and
        ///       leaves node null, which is exactly what a flat model's top level needs.
        /// @note This is an EXTENSION POINT: only a hierarchical model overrides it.
        virtual item_index_t index(int row, int column, const item_index_t& parent) const;

        /// @brief The parent of @p index.
        /// @param index The child node.
        /// @return The parent's index; an invalid index (the root) for a top-level node.
        /// @note The default implementation answers an invalid index for every node: a flat model has
        ///       no parents.
        /// @note This is an EXTENSION POINT: only a hierarchical model overrides it.
        virtual item_index_t parent(const item_index_t& index) const;

        /// @brief How many columns a row has (QAbstractItemModel::columnCount).
        /// @return The column count; the default implementation answers 1, so a list model needs no
        ///         change and ONE model interface serves both views.
        /// @note A view asks only for columns in [0, column_count()).
        /// @note This is an EXTENSION POINT, like row_count().
        virtual int column_count() const;

        /// @brief The value of @p row for @p role; an empty any_t when the model has nothing to say.
        /// @param row The row, already validated by the caller.
        /// @param role The kind of value asked for.
        /// @return A value for that role, or a default-constructed (invalid) any_t.
        /// @note The default implementation answers the empty any_t for every role, so a model only
        ///       overrides the roles it serves.
        /// @note This form is the row's column 0. A model with more than one column overrides
        ///       data(row, column, role) instead; the cell form of column 0 falls back to this one, so
        ///       a single-column model never has to write both.
        /// @note This is an EXTENSION POINT: answering a role is the whole point of a model.
        virtual ::hwshqtb::metaobject::any_t data(int row, item_role_enum role) const;

        /// @brief The value of one CELL.
        /// @param row The row, already validated by the caller.
        /// @param column The column, already validated against column_count() by the caller.
        /// @param role The kind of value asked for.
        /// @return A value for that cell and role, or a default-constructed (invalid) any_t.
        /// @note The default implementation forwards to data(row, role) for column 0 and answers an
        ///       empty any_t for every other column, so a model written for a list needs no change.
        /// @note This is an EXTENSION POINT, like data(row, role).
        virtual ::hwshqtb::metaobject::any_t data(int row, int column, item_role_enum role) const;

        /// @brief The value of the cell @p index names.
        /// @param index The node and column, already validated by the caller.
        /// @param role The kind of value asked for.
        /// @return A value for that cell and role, or a default-constructed (invalid) any_t.
        /// @note This is the form a tree view asks. The default implementation forwards to
        ///       data(index.row, index.column, role), so a flat model serves a tree unchanged; the
        ///       flat views keep calling the row forms and never reach this one.
        /// @note This is an EXTENSION POINT, like data(row, role).
        virtual ::hwshqtb::metaobject::any_t data(const item_index_t& index,
                                                  item_role_enum role) const;

        /// @brief The header of @p column (Qt's headerData); an empty any_t means "no header".
        /// @param column The column whose header is asked for.
        /// @param role The kind of value asked for.
        /// @return A value, or a default-constructed (invalid) any_t.
        /// @note The default implementation answers the empty any_t: a view without a header row is
        ///       the normal case, so a model only overrides it when it has headers to show.
        /// @note This is an EXTENSION POINT, like data(row, role).
        virtual ::hwshqtb::metaobject::any_t header_data(int column, item_role_enum role) const;

        /// @brief Emitted when the values of rows @p first .. @p last changed.
        /// @param first The first changed row (inclusive).
        /// @param last The last changed row (inclusive).
        /// @note The rows still exist: this is a content change, not a structural one.
        SIGNAL(void data_changed(int first, int last) const)

        /// @brief Emitted after @p count rows were inserted starting at row @p first.
        /// @param first The row the first inserted row now occupies.
        /// @param count The number of inserted rows.
        SIGNAL(void rows_inserted(int first, int count) const)

        /// @brief Emitted after @p count rows were removed starting at row @p first.
        /// @param first The row the first removed row occupied.
        /// @param count The number of removed rows.
        SIGNAL(void rows_removed(int first, int count) const)

        /// @brief Emitted when the model changed so much that no row identity survives.
        /// @note A view resets its offset, current row and selection: row numbers from before the
        ///       reset mean nothing after it.
        SIGNAL(void model_reset() const)
    };
}

#endif
