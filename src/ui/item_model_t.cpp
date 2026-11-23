/// @file item_model_t.cpp
/// @brief item_model_t: the default (empty) role answer, the single-column defaults, the flat answers
///        of the hierarchical members and the class's meta-object table.

#include "hwshqtb/ui/item_model_t.h"

#include "hwshqtb/metaobject/register_object.h"

namespace hwshqtb::ui {
    int item_model_t::row_count() const {
        return 0; // an empty model is a legal model: no rows, no views, no crash
    }

    int item_model_t::column_count() const {
        return 1;
    }

    int item_model_t::row_count(const item_index_t&) const {
        return 0; // a flat model has no children: its rows are the whole tree
    }

    item_index_t item_model_t::index(int row, int column, const item_index_t& parent) const {
        if (row < 0 || column < 0 || column >= column_count()) {
            return item_index_t{};
        }
        // An invalid parent asks for the root, whose rows are counted by row_count().
        const int rows = parent.is_valid() ? row_count(parent) : row_count();
        if (row >= rows) {
            return item_index_t{};
        }
        return item_index_t{ row, column, nullptr };
    }

    item_index_t item_model_t::parent(const item_index_t&) const {
        return item_index_t{}; // top level: a flat model has no node that has a parent
    }

    ::hwshqtb::metaobject::any_t item_model_t::data(int, item_role_enum) const {
        return ::hwshqtb::metaobject::any_t{};
    }

    ::hwshqtb::metaobject::any_t item_model_t::data(int row, int column,
                                                    item_role_enum role) const {
        if (column != 0) {
            return ::hwshqtb::metaobject::any_t{};
        }
        return data(row, role);
    }

    ::hwshqtb::metaobject::any_t item_model_t::data(const item_index_t& index,
                                                    item_role_enum role) const {
        if (!index.is_valid()) {
            return ::hwshqtb::metaobject::any_t{};
        }
        return data(index.row, index.column, role); // a flat model's node is its (row, column)
    }

    ::hwshqtb::metaobject::any_t item_model_t::header_data(int, item_role_enum) const {
        return ::hwshqtb::metaobject::any_t{};
    }

    META_OBJECT(item_model_t);
}
