// item_model_t: ONE model interface for a list and a table.
//
// The acceptance criteria as executable checks:
//  1. the defaults a model written for a list relies on: column_count() is 1, data(row, 0, role)
//     forwards to data(row, role), data(row, column != 0, role) is empty and header_data() is empty;
//  2. a three-column model: column_count() is 3, data(row, column, role) reaches every cell, and
//     header_data(column, role) answers per column;
//  3. a list view keeps working without a line of change: it shows column 0 of the three-column
//     model, it shows the text of a model that only overrides data(row, role), and it shows the text
//     of a model that only overrides data(row, column, role).
//
// Offscreen only (image_t + painter_t); no window and no video subsystem are created.

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/any_t.h"
#include "hwshqtb/ui/item_model_t.h"
#include "hwshqtb/ui/list_view_t.h"

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;
using namespace hwshqtb::ui;

namespace item_model_test {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

/// @brief The colour the list draws a row's text with (default_style_t's LIST_TEXT).
constexpr color_t row_text_color{ 245, 248, 250 };

/// @brief A model written for a LIST: it overrides only data(row, role) and counts the calls.
class list_model_t final: public item_model_t {
public:
    explicit list_model_t(int rows = 0) {
        set_rows(rows);
    }

    int row_count() const override {
        return _rows;
    }

    any_t data(int row, item_role_enum role) const override {
        ++data_calls;
        if (row < 0 || row >= _rows || role != item_role_enum::DISPLAY) {
            return {};
        }
        return any_t(std::string_view(_texts[static_cast<std::size_t>(row)]));
    }

    void set_rows(int rows) {
        _rows = rows;
        _texts.clear();
        for (int row = 0; row < rows; ++row) {
            _texts.push_back("list " + std::to_string(row));
        }
    }

    mutable int data_calls = 0;

private:
    int _rows = 0;
    std::vector<std::string> _texts;
};

/// @brief A three-column model: every cell is "r<row>c<column>", every header "H<column>".
class table_model_t final: public item_model_t {
public:
    explicit table_model_t(int rows = 0) {
        set_rows(rows);
    }

    int row_count() const override {
        return _rows;
    }

    int column_count() const override {
        return 3;
    }

    any_t data(int row, item_role_enum role) const override {
        ++row_form_calls;
        return data(row, 0, role);
    }

    any_t data(int row, int column, item_role_enum role) const override {
        ++cell_calls;
        if (row < 0 || row >= _rows || column < 0 || column >= 3
            || role != item_role_enum::DISPLAY) {
            return {};
        }
        asked_cells.push_back(point_t{ column, row });
        std::string& cell = _cells[static_cast<std::size_t>(row) * 3
                                   + static_cast<std::size_t>(column)];
        cell = "r" + std::to_string(row) + "c" + std::to_string(column);
        return any_t(std::string_view(cell));
    }

    any_t header_data(int column, item_role_enum role) const override {
        if (column < 0 || column >= 3 || role != item_role_enum::DISPLAY) {
            return {};
        }
        return any_t(std::string_view(_headers[static_cast<std::size_t>(column)]));
    }

    void set_rows(int rows) {
        _rows = rows;
        _cells.assign(static_cast<std::size_t>(rows) * 3, std::string{});
        for (int column = 0; column < 3; ++column) {
            _headers[static_cast<std::size_t>(column)] = "H" + std::to_string(column);
        }
    }

    mutable int row_form_calls = 0;
    mutable int cell_calls = 0;
    /// @brief Every cell the view asked for, as (column, row).
    mutable std::vector<point_t> asked_cells;

private:
    int _rows = 0;
    /// @brief The storage every cell answer points into (sized once, so a view stays valid).
    mutable std::vector<std::string> _cells;
    std::string _headers[3];
};

/// @brief A model written for a TABLE: it overrides only the three-argument data() and column_count.
class cell_model_t final: public item_model_t {
public:
    explicit cell_model_t(int rows = 0) {
        set_rows(rows);
    }

    int row_count() const override {
        return _rows;
    }

    int column_count() const override {
        return 2;
    }

    any_t data(int row, int column, item_role_enum role) const override {
        ++cell_calls;
        if (row < 0 || row >= _rows || column != 0 || role != item_role_enum::DISPLAY) {
            return {};
        }
        return any_t(std::string_view(_texts[static_cast<std::size_t>(row)]));
    }

    void set_rows(int rows) {
        _rows = rows;
        _texts.clear();
        for (int row = 0; row < rows; ++row) {
            _texts.push_back("cell " + std::to_string(row));
        }
    }

    mutable int cell_calls = 0;

private:
    int _rows = 0;
    std::vector<std::string> _texts;
};

/// @brief Paint the list into a fresh image and count the text-coloured pixels of row @p row.
int text_pixels_of_row(list_view_t& view, int row) {
    image_t image{ extent_t{ 240, 160 } };
    painter_t painter{ image };
    painter.begin();
    painter.clear(color_t::white_color());
    view.render(painter);
    painter.end();
    const int top = row * view.row_height() - view.scroll_offset().y();
    int count = 0;
    for (int y = top; y < top + view.row_height(); ++y) {
        for (int x = 0; x < 240; ++x) {
            if (image.pixel(x, y) == row_text_color) {
                ++count;
            }
        }
    }
    return count;
}

void test_defaults() {
    std::printf("(1) the defaults every list model relies on\n");
    list_model_t model{ 4 };
    // The defaults belong to the INTERFACE: a view holds an item_model_t* and reaches them through
    // it, exactly as below. (Calling model.data(row, column, role) on the concrete type would only
    // see the override, because a member function of the derived class hides the base's overloads.)
    const item_model_t& interface = model;

    CHECK(interface.column_count() == 1);
    // The single-column default: column 0 is exactly data(row, role).
    CHECK(interface.data(2, 0, item_role_enum::DISPLAY).is_initialized());
    CHECK(interface.data(2, 0, item_role_enum::DISPLAY).get<std::string_view>() == "list 2");
    CHECK(interface.data(2, 0, item_role_enum::DISPLAY).get<std::string_view>()
          == interface.data(2, item_role_enum::DISPLAY).get<std::string_view>());
    // ... and every other column answers nothing without asking the model at all.
    const int calls_before = model.data_calls;
    CHECK(!interface.data(2, 1, item_role_enum::DISPLAY).is_initialized());
    CHECK(!interface.data(2, 2, item_role_enum::USER).is_initialized());
    CHECK(model.data_calls == calls_before); // a non-zero column never reaches data(row, role)
    CHECK(interface.data(3, 0, item_role_enum::DISPLAY).get<std::string_view>() == "list 3");
    CHECK(model.data_calls == calls_before + 1); // ... while column 0 forwards to it
    // No header by default.
    CHECK(!interface.header_data(0, item_role_enum::DISPLAY).is_initialized());
    CHECK(!interface.header_data(7, item_role_enum::USER).is_initialized());
    // An unknown role is still empty through the cell form.
    CHECK(!interface.data(0, 0, item_role_enum::USER).is_initialized());
}

void test_three_columns() {
    std::printf("(2) a three-column model: every cell and header is reachable\n");
    table_model_t model{ 5 };

    CHECK(model.column_count() == 3);
    // Every cell of row 2 is its own value.
    CHECK(model.data(2, 0, item_role_enum::DISPLAY).get<std::string_view>() == "r2c0");
    CHECK(model.data(2, 1, item_role_enum::DISPLAY).get<std::string_view>() == "r2c1");
    CHECK(model.data(2, 2, item_role_enum::DISPLAY).get<std::string_view>() == "r2c2");
    // Out-of-range rows and columns answer nothing.
    CHECK(!model.data(2, 3, item_role_enum::DISPLAY).is_initialized());
    CHECK(!model.data(-1, 1, item_role_enum::DISPLAY).is_initialized());
    CHECK(!model.data(5, 1, item_role_enum::DISPLAY).is_initialized());
    // A role other than DISPLAY is empty here, per column.
    CHECK(!model.data(2, 1, item_role_enum::USER).is_initialized());
    // Headers are per column and per role.
    CHECK(model.header_data(0, item_role_enum::DISPLAY).get<std::string_view>() == "H0");
    CHECK(model.header_data(2, item_role_enum::DISPLAY).get<std::string_view>() == "H2");
    CHECK(!model.header_data(1, item_role_enum::USER).is_initialized());
    CHECK(!model.header_data(3, item_role_enum::DISPLAY).is_initialized());
    // The row form of a table model still answers column 0 through the base forward.
    CHECK(model.data(4, item_role_enum::DISPLAY).get<std::string_view>() == "r4c0");
}

void test_list_keeps_working() {
    std::printf("(3) a list view still works with all three model shapes\n");
    // (3a) the list-only model: the text of data(row, role) reaches the pixels.
    list_model_t list_model;
    list_model.set_rows(4);
    list_view_t list;
    list.set_geometry(rect_t{ 0, 0, 240, 160 });
    list.set_model(&list_model);
    list_model.data_calls = 0;
    CHECK(text_pixels_of_row(list, 0) > 0);
    CHECK(text_pixels_of_row(list, 2) > 0);
    CHECK(list_model.data_calls > 0);

    // (3b) the three-column model: the list shows column 0 and asks for NOTHING else.
    table_model_t table;
    table.set_rows(4);
    list_view_t table_in_list;
    table_in_list.set_geometry(rect_t{ 0, 0, 240, 160 });
    table_in_list.set_model(&table);
    table.cell_calls = 0;
    table.asked_cells.clear();
    CHECK(text_pixels_of_row(table_in_list, 1) > 0);
    CHECK(!table.asked_cells.empty());
    bool only_first_column = true;
    for (const point_t& cell : table.asked_cells) {
        only_first_column = only_first_column && cell.x() == 0;
    }
    CHECK(only_first_column);
    CHECK(table.row_form_calls == 0); // the view asks the cell form, not the row form

    // (3c) the cell-only model: the list shows its column 0, so a table-shaped model still feeds a
    // list without an override of the row form.
    cell_model_t cells;
    cells.set_rows(3);
    list_view_t cells_in_list;
    cells_in_list.set_geometry(rect_t{ 0, 0, 240, 160 });
    cells_in_list.set_model(&cells);
    cells.cell_calls = 0;
    CHECK(text_pixels_of_row(cells_in_list, 0) > 0);
    CHECK(text_pixels_of_row(cells_in_list, 2) > 0);
    CHECK(cells.cell_calls > 0);
}

}  // namespace item_model_test

int main() {
    using namespace item_model_test;
    std::printf("== item_model_test ==\n");
    test_defaults();
    test_three_columns();
    test_list_keeps_working();
    std::printf("checks: %d, failures: %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
