// table_view_t — the multi-column item view.
//
// The acceptance criteria as executable checks:
//  1. columns: the default width, program-set widths, content_width() == their sum, the accumulated
//     column_left(), and the out-of-range answers of both accessors;
//  2. cell virtualisation: a 1,000-row x 6-column model has data() asked only for the cells inside
//     the viewport, at the top and at the bottom of the range (counted);
//  3. column virtualisation: after scrolling to the right end the left columns are no longer asked
//     and the visible ones are (counted), for the cells AND for the header titles;
//  4. the header strip: the titles come from header_data(), they move with the horizontal offset and
//     they do NOT move with the vertical one (the band is compared pixel for pixel);
//  5. the hit test: column_at() on the column boundaries, the header band, the bars and the area right
//     of the last column, and a click that sets (row, column);
//  6. the keyboard: Left/Right move the current column, Enter emits cell_activated(row, column) and
//     the inherited row navigation keeps working (and keeps the column);
//  7. horizontal scrolling: the bar appears when the columns are wider than the view, its range and
//     page step are right, the wheel scrolls it and the content really moved;
//  8. size_hint() (capped by TABLE_MAX_WIDTH) and minimum_size_hint();
//  9. the list regression: list_view_t keeps no header, no horizontal bar and its old hints.
//
// Offscreen only (image_t + painter_t); no window and no video subsystem are created.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/gui/region_t.h"
#include "hwshqtb/metaobject/any_t.h"
#include "hwshqtb/ui/abstract_item_view_t.h"
#include "hwshqtb/ui/input_router_t.h"
#include "hwshqtb/ui/item_model_t.h"
#include "hwshqtb/ui/list_view_t.h"
#include "hwshqtb/ui/scroll_bar_t.h"
#include "hwshqtb/ui/style_t.h"
#include "hwshqtb/ui/table_view_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;
using namespace hwshqtb::ui;

namespace table_view_test {

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

/// @brief A probe model: rows x columns cells named "r<row>c<column>", optional per-column header
///        titles, and a count of every data()/header_data() call (out-of-range calls counted apart: a
///        virtualised view never makes one).
class probe_table_model_t final : public item_model_t {
public:
    probe_table_model_t(int rows, int columns):
        _rows(rows),
        _columns(columns),
        _headers(static_cast<std::size_t>(columns < 0 ? 0 : columns)) {
        _cells.reserve(static_cast<std::size_t>(rows < 0 ? 0 : rows) * _headers.size());
        for (int row = 0; row < _rows; ++row) {
            for (int column = 0; column < _columns; ++column) {
                _cells.push_back("r" + std::to_string(row) + "c" + std::to_string(column));
            }
        }
    }

    int row_count() const override {
        return _rows;
    }

    int column_count() const override {
        return _columns;
    }

    any_t data(int row, int column, item_role_enum role) const override {
        ++data_calls;
        if (row < 0 || row >= _rows || column < 0 || column >= _columns) {
            ++out_of_range_calls;
            return {};
        }
        asked_cells.emplace_back(row, column);
        if (role != item_role_enum::DISPLAY) {
            return {};
        }
        const auto index = static_cast<std::size_t>(row) * static_cast<std::size_t>(_columns)
            + static_cast<std::size_t>(column);
        return any_t(std::string_view(_cells[index]));
    }

    any_t header_data(int column, item_role_enum role) const override {
        ++header_calls;
        if (column < 0 || column >= _columns) {
            ++header_out_of_range_calls;
            return {};
        }
        asked_header_columns.push_back(column);
        if (role != item_role_enum::DISPLAY || _headers[static_cast<std::size_t>(column)].empty()) {
            return {};
        }
        return any_t(std::string_view(_headers[static_cast<std::size_t>(column)]));
    }

    /// @brief Give @p column the header title @p title (an empty title makes header_data() answer
    ///        nothing for it).
    void set_header(int column, std::string title) {
        if (column < 0 || column >= _columns) {
            return;
        }
        _headers[static_cast<std::size_t>(column)] = std::move(title);
    }

    void forget_calls() {
        data_calls = 0;
        out_of_range_calls = 0;
        asked_cells.clear();
        header_calls = 0;
        header_out_of_range_calls = 0;
        asked_header_columns.clear();
    }

    mutable int data_calls = 0;
    mutable int out_of_range_calls = 0;
    mutable std::vector<std::pair<int, int>> asked_cells;
    mutable int header_calls = 0;
    mutable int header_out_of_range_calls = 0;
    mutable std::vector<int> asked_header_columns;

private:
    int _rows = 0;
    int _columns = 0;
    std::vector<std::string> _cells;
    std::vector<std::string> _headers;
};

interaction_t press(point_t local, std::uint32_t modifiers = 0) {
    interaction_t in;
    in.mouse = local;
    in.mouse_down = true;
    in.mouse_pressed = true;
    in.hovered = true;
    in.modifiers = modifiers;
    return in;
}

interaction_t release(point_t local, bool inside) {
    interaction_t in;
    in.mouse = local;
    in.mouse_released = true;
    in.hovered = inside;
    return in;
}

interaction_t wheel_at(point_t local, point_t ticks) {
    interaction_t in;
    in.mouse = local;
    in.wheel = ticks;
    in.hovered = true;
    return in;
}

interaction_t key_step(int key, std::uint32_t modifiers = 0) {
    interaction_t in;
    in.key = key;
    in.key_pressed = true;
    in.modifiers = modifiers;
    return in;
}

/// @brief The view's scroll bar of @p orientation, or nullptr.
scroll_bar_t* bar_of(abstract_item_view_t& view, orientation_enum orientation) {
    for (std::size_t index = 0; index < view.item_count(); ++index) {
        scroll_bar_t* const bar = dynamic_cast<scroll_bar_t*>(view.item_at_index(index));
        if (bar != nullptr && bar->orientation() == orientation) {
            return bar;
        }
    }
    return nullptr;
}

/// @brief Paint one full frame of @p view into a fresh @p image (cleared white first).
void paint_into(abstract_item_view_t& view, image_t& image) {
    painter_t painter{ image };
    painter.begin();
    painter.clear(color_t::white_color());
    view.render(painter);
    painter.end();
}

/// @brief The pixels of @p area, row by row.
std::vector<color_t> band_pixels(const image_t& image, rect_t area) {
    std::vector<color_t> pixels;
    pixels.reserve(static_cast<std::size_t>(area.width()) * static_cast<std::size_t>(area.height()));
    for (int y = area.y(); y < area.bottom(); ++y) {
        for (int x = area.x(); x < area.right(); ++x) {
            pixels.push_back(image.pixel(x, y));
        }
    }
    return pixels;
}

/// @brief How many pixels of @p area equal @p color.
int count_color(const image_t& image, rect_t area, color_t color) {
    int count = 0;
    for (int y = area.y(); y < area.bottom(); ++y) {
        for (int x = area.x(); x < area.right(); ++x) {
            if (image.pixel(x, y) == color) {
                ++count;
            }
        }
    }
    return count;
}

/// @brief Whether @p region covers every pixel of @p rect.
bool covers(const region_t& region, rect_t rect) {
    return region_t{ rect }.subtracted(region).is_empty();
}

/// @brief The leftmost @p color pixel of @p area, or -1.
int first_color_x(const image_t& image, rect_t area, color_t color) {
    for (int x = area.x(); x < area.right(); ++x) {
        for (int y = area.y(); y < area.bottom(); ++y) {
            if (image.pixel(x, y) == color) {
                return x;
            }
        }
    }
    return -1;
}

// The colours of the painted parts, as the style paints them. They are hard-coded on purpose: the
// assertions are about PIXELS, so the expectation must not be derived from the code under test.
constexpr color_t row_fill_color{ 30, 42, 56 };          // LIST_ROW
constexpr color_t header_fill_color{ 40, 55, 70 };       // TABLE_HEADER background
constexpr color_t header_border_color{ 70, 84, 98 };     // TABLE_HEADER bottom border
constexpr color_t grid_color{ 70, 84, 98 };              // TABLE_GRID
constexpr color_t text_color{ 245, 248, 250 };           // TABLE_CELL / TABLE_HEADER text
constexpr color_t cell_outline_color{ 255, 214, 10 };    // TABLE_CELL current-cell outline

/// @brief Build the expected ask list of @p rows x @p columns cells.
std::vector<std::pair<int, int>> expected_cells(int first_row, int last_row, int first_column,
                                                int last_column) {
    std::vector<std::pair<int, int>> cells;
    for (int row = first_row; row <= last_row; ++row) {
        for (int column = first_column; column <= last_column; ++column) {
            cells.emplace_back(row, column);
        }
    }
    return cells;
}

void test_columns() {
    std::printf("(1) columns: widths, content width and the accumulated left edge\n");
    const style_t& style = default_style();
    const int default_column = style.pixel_metric(pixel_metric_enum::TABLE_COLUMN_WIDTH);
    CHECK(default_column == 80);

    probe_table_model_t model{ 50, 3 };
    table_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 180 });
    view.set_model(&model);

    // No width was installed: every column asks the style.
    CHECK(view.column_width(0) == default_column);
    CHECK(view.column_width(2) == default_column);
    CHECK(view.column_width(-1) == 0);
    // The answer does not check the model's column count: it is the width the column would have.
    CHECK(view.column_width(3) == default_column);
    CHECK(view.content_width() == 3 * default_column);
    CHECK(view.column_left(0) == 0);
    CHECK(view.column_left(1) == 80);
    CHECK(view.column_left(2) == 160);
    CHECK(view.column_left(3) == 240); // == content_width()
    CHECK(view.column_left(-1) == 0);
    // Past the model's columns the answer is the content's right edge: every column of the model lies
    // before it.
    CHECK(view.column_left(9999) == 240);

    // Program-set widths move every following left edge.
    view.set_column_width(0, 100);
    view.set_column_width(1, 60);
    view.set_column_width(2, 40);
    CHECK(view.column_width(0) == 100);
    CHECK(view.column_width(1) == 60);
    CHECK(view.column_width(2) == 40);
    CHECK(view.content_width() == 200);
    CHECK(view.column_left(0) == 0);
    CHECK(view.column_left(1) == 100);
    CHECK(view.column_left(2) == 160);
    CHECK(view.column_left(3) == 200);
    CHECK(view.size_hint().width() == 200);

    // A negative width is a hidden column (not a reset back to the default), and a negative column
    // index is ignored by the setter.
    view.set_column_width(1, -7);
    CHECK(view.column_width(1) == 0);
    CHECK(view.content_width() == 140);
    view.set_column_width(1, 60);
    view.set_column_width(-1, 500);
    CHECK(view.column_width(-1) == 0);
    CHECK(view.content_width() == 200);

    // A width is remembered across a model change: the new model's columns 3 and 4 use the default.
    probe_table_model_t wider{ 10, 5 };
    view.set_model(&wider);
    CHECK(view.content_width() == 100 + 60 + 40 + default_column + default_column);
    CHECK(view.column_left(3) == 200);
    CHECK(view.column_left(4) == 280);
    CHECK(view.column_left(5) == 360);

    // The content width is the horizontal extent: 360 > 240, so the inherited horizontal bar appears
    // and its range is exactly content width minus viewport width.
    scroll_bar_t* const horizontal = bar_of(view, orientation_enum::HORIZONTAL);
    CHECK(horizontal != nullptr);
    CHECK(horizontal->is_visible());
    const int thickness = style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);
    CHECK(horizontal->maximum() == 360 - (240 - thickness));
    CHECK(horizontal->page_step() == 240 - thickness);
}

void test_cell_virtualisation() {
    std::printf("(2) cell virtualisation: only the cells inside the viewport are asked\n");
    const style_t& style = default_style();
    const int header = style.pixel_metric(pixel_metric_enum::TABLE_HEADER_HEIGHT);
    const int thickness = style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);
    CHECK(header == 18);
    CHECK(thickness == 14);

    probe_table_model_t model{ 1000, 6 };
    table_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 180 });
    view.set_model(&model);

    CHECK(view.row_height() == 30);
    CHECK(view.content_width() == 6 * 80);

    // Both bars are visible: 30000 > 148 row-area pixels, and 480 > 240.
    scroll_bar_t* const vertical = bar_of(view, orientation_enum::VERTICAL);
    scroll_bar_t* const horizontal = bar_of(view, orientation_enum::HORIZONTAL);
    CHECK(vertical != nullptr);
    CHECK(horizontal != nullptr);
    CHECK(vertical->is_visible());
    CHECK(horizontal->is_visible());
    // The vertical bar spans the whole right edge (the corner belongs to it) but its PAGE is the row
    // area under the header only.
    CHECK(vertical->geometry() == (rect_t{ 226, 0, 14, 166 }));
    CHECK(vertical->page_step() == 180 - header - thickness);
    CHECK(vertical->maximum() == 1000 * 30 - (180 - header - thickness));
    CHECK(horizontal->geometry() == (rect_t{ 0, 180 - thickness, 226, 14 }));

    // A 240x180 view over 80-pixel columns shows columns 0..2 (the viewport is 226 wide) and
    // (180 - 18 - 14) / 30 rows round up to 5 rows: 15 cells, independent of the 1000 rows.
    image_t image{ extent_t{ 240, 180 } };
    model.forget_calls();
    paint_into(view, image);
    CHECK(model.data_calls == 5 * 3);
    CHECK(model.out_of_range_calls == 0);
    CHECK(model.asked_cells == expected_cells(0, 4, 0, 2));
    CHECK(model.header_calls == 3);
    CHECK(model.asked_header_columns == (std::vector<int>{ 0, 1, 2 }));

    // The same cost at the bottom of the range: the rows moved, the count did not.
    view.scroll_to_row(999);
    CHECK(view.scroll_offset().y() == 1000 * 30 - (180 - header - thickness));
    model.forget_calls();
    paint_into(view, image);
    CHECK(model.data_calls == 5 * 3);
    CHECK(model.out_of_range_calls == 0);
    CHECK(model.asked_cells == expected_cells(995, 999, 0, 2));

    // A hidden table asks for nothing.
    view.set_visible(false);
    model.forget_calls();
    paint_into(view, image);
    CHECK(model.data_calls == 0);
    view.set_visible(true);
}

void test_column_virtualisation() {
    std::printf("(3) column virtualisation: the columns scrolled out are no longer asked\n");
    probe_table_model_t model{ 1000, 6 };
    table_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 180 });
    view.set_model(&model);
    scroll_bar_t* const horizontal = bar_of(view, orientation_enum::HORIZONTAL);
    CHECK(horizontal != nullptr);
    CHECK(horizontal->is_visible());

    horizontal->set_value(horizontal->maximum());
    CHECK(view.scroll_offset().x() == 480 - 226);
    image_t image{ extent_t{ 240, 180 } };
    model.forget_calls();
    paint_into(view, image);

    // At the right end only columns 3, 4 and 5 intersect the viewport: five rows of three cells.
    CHECK(model.data_calls == 5 * 3);
    CHECK(model.out_of_range_calls == 0);
    CHECK(model.asked_cells == expected_cells(0, 4, 3, 5));
    CHECK(model.asked_header_columns == (std::vector<int>{ 3, 4, 5 }));
    // The proof that the left columns were NOT asked: no cell of column 0, 1 or 2 was requested.
    int left_columns = 0;
    for (const auto& [row, column] : model.asked_cells) {
        (void)row;
        if (column < 3) {
            ++left_columns;
        }
    }
    CHECK(left_columns == 0);

    // Half way the middle columns are asked and the outer ones are not: the viewport [80, 306) holds
    // columns 1, 2 and 3.
    horizontal->set_value(80);
    CHECK(view.scroll_offset().x() == 80);
    model.forget_calls();
    paint_into(view, image);
    CHECK(model.data_calls == 5 * 3);
    CHECK(model.asked_cells == expected_cells(0, 4, 1, 3));
    CHECK(model.asked_header_columns == (std::vector<int>{ 1, 2, 3 }));
}

void test_header() {
    std::printf("(4) the header strip: titles, horizontal follow, vertical independence\n");
    const style_t& style = default_style();
    const int header = style.pixel_metric(pixel_metric_enum::TABLE_HEADER_HEIGHT);
    const int thickness = style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);
    const int padding = style.pixel_metric(pixel_metric_enum::TEXT_PADDING);
    CHECK(padding == 8);

    probe_table_model_t model{ 100, 4 };
    model.set_header(0, "unit 0");
    model.set_header(1, "unit 1");
    model.set_header(2, "unit 2");
    model.set_header(3, "unit 3");
    table_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 180 });
    view.set_model(&model);
    CHECK(view.content_width() == 4 * 80);

    // The band is 18 pixels tall, spans the viewport width and sits above the rows: the row area
    // starts at y == header.
    const rect_t band{ 0, 0, 240 - thickness, header };
    image_t frame{ extent_t{ 240, 180 } };
    model.forget_calls();
    paint_into(view, frame);
    CHECK(model.header_calls == 3); // columns 0..2: the fourth is right of the viewport
    CHECK(model.asked_header_columns == (std::vector<int>{ 0, 1, 2 }));
    CHECK(model.header_out_of_range_calls == 0);

    // The strip is painted: its fill, its bottom border and at least one title pixel.
    CHECK(count_color(frame, band, header_fill_color) > 0);
    CHECK(count_color(frame, band, header_border_color) > 0);
    CHECK(count_color(frame, band, text_color) > 0);
    // The bottom border sits on the band's last pixel row.
    CHECK(count_color(frame, rect_t{ 0, header - 1, 240 - thickness, 1 }, header_border_color)
          == 240 - thickness);
    // The first row starts right below the header: no row pixel above it, a row pixel at/after it.
    CHECK(!(frame.pixel(200, header - 1) == row_fill_color));
    CHECK(frame.pixel(200, header) != header_fill_color);

    // The titles come from header_data(): a model without titles paints no text at all.
    probe_table_model_t untitled{ 100, 4 };
    table_view_t untitled_view;
    untitled_view.set_geometry(rect_t{ 0, 0, 240, 180 });
    untitled_view.set_model(&untitled);
    image_t bare{ extent_t{ 240, 180 } };
    paint_into(untitled_view, bare);
    CHECK(untitled.header_calls == 3);
    CHECK(count_color(bare, band, text_color) == 0);

    // The first title starts at the strip's padding and follows the horizontal offset exactly.
    const rect_t scan{ 45, 0, 160 - 45, header };
    const int first_title_x = first_color_x(frame, scan, text_color);
    CHECK(first_title_x > 0); // column 1's title, right of column 0's 6-glyph run
    CHECK(first_title_x >= 80 + padding);
    scroll_bar_t* const horizontal = bar_of(view, orientation_enum::HORIZONTAL);
    CHECK(horizontal != nullptr);
    // The titles follow the horizontal offset, so the scroll has to repaint the band: the inherited
    // fast path shifts the ROW AREA only, which never covers the strip above it.
    view.clear_damage();
    horizontal->set_value(40);
    CHECK(view.scroll_offset().x() == 40);
    CHECK(covers(view.damage(), band));
    image_t shifted{ extent_t{ 240, 180 } };
    paint_into(view, shifted);
    const int moved_title_x = first_color_x(shifted, scan, text_color);
    CHECK(moved_title_x == first_title_x - 40);

    // A vertical scroll does not move the band: the rows move, the strip does not. At this offset the
    // top row starts ABOVE the row area (row 5's top would be at y == 0), so the comparison also
    // proves that no row paints over the header.
    image_t before{ extent_t{ 240, 180 } };
    paint_into(view, before);
    const std::vector<color_t> band_before = band_pixels(before, band);
    view.scroll_to_row(10);
    CHECK(view.scroll_offset().y() == 10 * 30 + 30 - (180 - header - thickness));
    CHECK(view.scroll_offset().y() == 182);
    image_t after{ extent_t{ 240, 180 } };
    paint_into(view, after);
    CHECK(band_pixels(after, band) == band_before);
    // ... while the rows did move.
    const rect_t rows{ 0, header, 240 - thickness, 180 - header - thickness };
    CHECK(!(band_pixels(after, rows) == band_pixels(before, rows)));
    // The hover/selection state is untouched, so the top row's background is the plain row fill.
    CHECK(count_color(after, rows, header_fill_color) == 0);

    // A model change resets the contents AND the titles: the band is damaged as well.
    view.clear_damage();
    probe_table_model_t replacement{ 100, 4 };
    replacement.set_header(0, "other");
    view.set_model(&replacement);
    CHECK(covers(view.damage(), band));
}

void test_hit_test() {
    std::printf("(5) the hit test: column_at() boundaries and a click that sets the cell\n");
    probe_table_model_t model{ 50, 3 };
    table_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 180 });
    view.set_column_width(0, 100);
    view.set_column_width(1, 60);
    view.set_column_width(2, 40);
    view.set_model(&model);
    CHECK(view.content_width() == 200); // 200 < 240: no horizontal bar
    scroll_bar_t* const horizontal = bar_of(view, orientation_enum::HORIZONTAL);
    CHECK(horizontal != nullptr);
    CHECK(!horizontal->is_visible());

    // The viewport is 226 wide (the vertical bar) and starts at y = 18 (the header).
    CHECK(view.column_at(point_t{ 0, 20 }) == 0);
    CHECK(view.column_at(point_t{ 99, 20 }) == 0);
    CHECK(view.column_at(point_t{ 100, 20 }) == 1); // a boundary belongs to the right column
    CHECK(view.column_at(point_t{ 159, 20 }) == 1);
    CHECK(view.column_at(point_t{ 160, 20 }) == 2);
    CHECK(view.column_at(point_t{ 199, 20 }) == 2);
    CHECK(view.column_at(point_t{ 200, 20 }) == -1); // right of the last column
    CHECK(view.column_at(point_t{ 225, 20 }) == -1);
    CHECK(view.column_at(point_t{ 226, 20 }) == -1); // the vertical bar
    CHECK(view.column_at(point_t{ 10, 17 }) == -1);  // the header strip
    CHECK(view.column_at(point_t{ 10, 18 }) == 0);   // the first row-area pixel
    CHECK(view.column_at(point_t{ 10, 179 }) == 0);  // the last one (no horizontal bar)
    CHECK(view.column_at(point_t{ -1, 20 }) == -1);  // outside the view
    CHECK(view.column_at(point_t{ 10, 200 }) == -1);

    // A click selects the ROW (the inherited logic) and makes the clicked CELL current.
    int row_activations = 0;
    int activated_row = -1;
    int cell_activations = 0;
    view.connect("row_activated(int) const", [&row_activations, &activated_row](int row) {
        ++row_activations;
        activated_row = row;
    });
    view.connect("cell_activated(int,int) const", [&cell_activations]() { ++cell_activations; });

    CHECK(view.interact(press(point_t{ 130, 50 }))); // row 1, column 1
    CHECK(view.current_row() == 1);
    CHECK(view.current_column() == 1);
    CHECK(view.selected_rows() == (std::vector<int>{ 1 }));
    CHECK(view.interact(release(point_t{ 130, 50 }, true)));
    CHECK(row_activations == 1);
    CHECK(activated_row == 1);
    CHECK(cell_activations == 0); // a click does not activate a cell: Return does

    CHECK(view.interact(press(point_t{ 170, 80 }))); // row 2, column 2
    CHECK(view.current_row() == 2);
    CHECK(view.current_column() == 2);
    CHECK(view.interact(release(point_t{ 170, 80 }, true)));
    CHECK(row_activations == 2);

    // A press on the header is consumed and changes neither the cell nor the selection. It takes no
    // pointer capture, so the release that follows is not claimed by the table either.
    CHECK(view.interact(press(point_t{ 130, 5 })));
    CHECK(view.current_row() == 2);
    CHECK(view.current_column() == 2);
    CHECK(view.selected_rows() == (std::vector<int>{ 2 }));
    CHECK(!view.interact(release(point_t{ 130, 5 }, true)));
    CHECK(view.current_row() == 2);
    CHECK(view.current_column() == 2);

    // A press right of the last column still selects its row but has no column.
    CHECK(view.interact(press(point_t{ 210, 80 })));
    CHECK(view.current_row() == 2);
    CHECK(view.current_column() == -1);
    CHECK(view.interact(release(point_t{ 210, 80 }, true)));

    // A press below the last row clears the cell and the selection.
    probe_table_model_t short_model{ 3, 3 };
    table_view_t short_view;
    short_view.set_geometry(rect_t{ 0, 0, 240, 180 });
    short_view.set_model(&short_model);
    short_view.set_current_cell(1, 2);
    CHECK(short_view.current_row() == 1);
    CHECK(short_view.current_column() == 2);
    CHECK(short_view.interact(press(point_t{ 10, 140 }))); // past the third row (18 + 90)
    CHECK(short_view.current_row() == -1);
    CHECK(short_view.current_column() == -1);
    CHECK(short_view.selected_rows().empty());
}

void test_keyboard() {
    std::printf("(6) the keyboard: Left/Right columns, Enter cell activation, rows unchanged\n");
    probe_table_model_t model{ 100, 3 };
    table_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 180 });
    view.set_column_width(0, 100);
    view.set_column_width(1, 60);
    view.set_column_width(2, 40);
    view.set_model(&model);
    input_router_t router{ view };
    router.set_focus(&view);
    CHECK(view.has_focus());

    int cell_activations = 0;
    int activated_row = -1;
    int activated_column = -1;
    int row_activations = 0;
    view.connect("cell_activated(int,int) const",
                 [&cell_activations, &activated_row, &activated_column](int row, int column) {
                     ++cell_activations;
                     activated_row = row;
                     activated_column = column;
                 });
    view.connect("row_activated(int) const", [&row_activations]() { ++row_activations; });

    // The row navigation is the base's: Down makes row 0 current and leaves the column alone.
    CHECK(router.dispatch(key_step(interaction_t::down_key)));
    CHECK(view.current_row() == 0);
    CHECK(view.current_column() == -1);

    // Right establishes the first column, then walks right and clamps at the last one.
    CHECK(router.dispatch(key_step(interaction_t::right_key)));
    CHECK(view.current_row() == 0);
    CHECK(view.current_column() == 0);
    CHECK(router.dispatch(key_step(interaction_t::right_key)));
    CHECK(view.current_column() == 1);
    CHECK(router.dispatch(key_step(interaction_t::right_key)));
    CHECK(view.current_column() == 2);
    CHECK(router.dispatch(key_step(interaction_t::right_key)));
    CHECK(view.current_column() == 2);
    CHECK(router.dispatch(key_step(interaction_t::left_key)));
    CHECK(view.current_column() == 1);

    // The base's row keys keep working and keep the column.
    CHECK(router.dispatch(key_step(interaction_t::down_key)));
    CHECK(view.current_row() == 1);
    CHECK(view.current_column() == 1);
    CHECK(router.dispatch(key_step(interaction_t::end_key)));
    CHECK(view.current_row() == 99);
    CHECK(view.current_column() == 1);
    CHECK(router.dispatch(key_step(interaction_t::home_key)));
    CHECK(view.current_row() == 0);
    CHECK(view.current_column() == 1);
    CHECK(router.dispatch(key_step(interaction_t::page_down_key)));
    CHECK(view.current_row() < 99);
    CHECK(view.current_column() == 1);

    // Return activates the CELL; the inherited row activation stays silent.
    const int current_row = view.current_row();
    CHECK(router.dispatch(key_step(interaction_t::return_key)));
    CHECK(cell_activations == 1);
    CHECK(activated_row == current_row);
    CHECK(activated_column == 1);
    CHECK(row_activations == 0);

    // Left at the first column stays there.
    view.set_current_cell(0, 0);
    CHECK(router.dispatch(key_step(interaction_t::left_key)));
    CHECK(view.current_row() == 0);
    CHECK(view.current_column() == 0);

    // The current column lives with the current row: clearing the row clears the column.
    view.set_current_row(-1);
    CHECK(view.current_row() == -1);
    CHECK(view.current_column() == -1);
    // No current cell: Return activates nothing but is still the table's key.
    cell_activations = 0;
    CHECK(router.dispatch(key_step(interaction_t::return_key)));
    CHECK(cell_activations == 0);
    // ... and an arrow establishes (0, 0).
    CHECK(router.dispatch(key_step(interaction_t::right_key)));
    CHECK(view.current_row() == 0);
    CHECK(view.current_column() == 0);

    // A model with no rows offers no cell to move to: the arrows are not consumed.
    probe_table_model_t empty{ 0, 3 };
    table_view_t empty_view;
    empty_view.set_geometry(rect_t{ 0, 0, 240, 180 });
    empty_view.set_model(&empty);
    input_router_t empty_router{ empty_view };
    empty_router.set_focus(&empty_view);
    CHECK(!empty_router.dispatch(key_step(interaction_t::right_key)));
    CHECK(empty_view.current_row() == -1);
    CHECK(empty_view.current_column() == -1);

    // A key step without the focus is not the table's.
    view.clear_focus();
    CHECK(!view.has_focus());
    CHECK(!router.dispatch(key_step(interaction_t::right_key)));
}

void test_horizontal_scroll() {
    std::printf("(7) horizontal scrolling: the bar, its range, the wheel and the moved content\n");
    const style_t& style = default_style();
    const int thickness = style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);
    const int header = style.pixel_metric(pixel_metric_enum::TABLE_HEADER_HEIGHT);
    const int step = style.pixel_metric(pixel_metric_enum::SCROLL_WHEEL_STEP);

    probe_table_model_t model{ 50, 6 };
    table_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 180 });
    view.set_model(&model);
    CHECK(view.content_width() == 480);
    CHECK(view.scroll_offset() == (point_t{ 0, 0 }));

    scroll_bar_t* const horizontal = bar_of(view, orientation_enum::HORIZONTAL);
    const scroll_bar_t* const vertical = bar_of(view, orientation_enum::VERTICAL);
    CHECK(horizontal != nullptr);
    CHECK(vertical != nullptr);
    CHECK(horizontal->is_visible()); // 480 > 240
    CHECK(horizontal->geometry() == (rect_t{ 0, 180 - thickness, 226, 14 }));
    CHECK(horizontal->minimum() == 0);
    CHECK(horizontal->maximum() == 480 - 226);
    CHECK(horizontal->page_step() == 226);
    CHECK(horizontal->value() == 0);
    // The vertical bar keeps the full height minus the horizontal bar: the bars never leave a hole.
    CHECK(vertical->geometry() == (rect_t{ 226, 0, 14, 180 - thickness }));
    CHECK(vertical->maximum() == 50 * 30 - (180 - header - thickness));

    // The wheel scrolls the horizontal axis by the style step.
    CHECK(view.interact(wheel_at(point_t{ 10, 100 }, point_t{ 1, 0 })));
    CHECK(view.scroll_offset().x() == step);
    CHECK(horizontal->value() == step);
    // A huge step clamps to the right end; a step past the end changes nothing and is not consumed.
    CHECK(view.interact(wheel_at(point_t{ 10, 100 }, point_t{ 100, 0 })));
    CHECK(view.scroll_offset().x() == 480 - 226);
    CHECK(!view.interact(wheel_at(point_t{ 10, 100 }, point_t{ 1, 0 })));
    CHECK(view.scroll_offset().x() == 480 - 226);
    CHECK(view.interact(wheel_at(point_t{ 10, 100 }, point_t{ -1, 0 })));
    CHECK(view.scroll_offset().x() == 480 - 226 - step);

    // The content really moved: the grid line between columns 3 and 4 (content x == 320) lands at
    // 320 - 254 = 66, while at offset 0 that x is plain row fill of the first column.
    horizontal->set_value(480 - 226);
    CHECK(view.scroll_offset().x() == 480 - 226);
    image_t right{ extent_t{ 240, 180 } };
    paint_into(view, right);
    CHECK(right.pixel(66, header + 2) == grid_color);
    CHECK(right.pixel(80, header + 2) == row_fill_color);
    horizontal->set_value(0);
    CHECK(view.scroll_offset().x() == 0);
    image_t left{ extent_t{ 240, 180 } };
    paint_into(view, left);
    CHECK(left.pixel(66, header + 2) == row_fill_color);
    CHECK(left.pixel(80, header + 2) == grid_color);
    CHECK(left.pixel(160, header + 2) == grid_color);
    CHECK(right.pixel(146, header + 2) == grid_color); // content x == 400: column 5's leading edge
    CHECK(right.pixel(160, header + 2) == row_fill_color);
}

void test_size_hints() {
    std::printf("(8) size_hint() and minimum_size_hint()\n");
    const style_t& style = default_style();
    const int header = style.pixel_metric(pixel_metric_enum::TABLE_HEADER_HEIGHT);
    const int thickness = style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);
    const int cap = style.pixel_metric(pixel_metric_enum::TABLE_MAX_WIDTH);
    const int default_column = style.pixel_metric(pixel_metric_enum::TABLE_COLUMN_WIDTH);
    const int list_height = style.pixel_metric(pixel_metric_enum::LIST_VIEW_HEIGHT);
    CHECK(cap == 480);

    probe_table_model_t wide{ 10, 8 };
    table_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 180 });
    view.set_model(&wide);
    CHECK(view.content_width() == 8 * default_column); // 640, wider than the cap
    CHECK(view.size_hint() == (extent_t{ cap, list_height }));
    CHECK(view.minimum_size_hint()
          == (extent_t{ default_column + 2 * thickness, header + 30 + 2 * thickness }));

    // Narrower than the cap: the hint follows the column widths, not the view's width.
    for (int column = 0; column < 8; ++column) {
        view.set_column_width(column, 30);
    }
    CHECK(view.content_width() == 240);
    CHECK(view.size_hint() == (extent_t{ 240, list_height }));
    CHECK(view.minimum_size_hint().width() == 30 + 2 * thickness);

    // The floor follows the row height.
    view.set_row_height(20);
    CHECK(view.minimum_size_hint().height() == header + 20 + 2 * thickness);

    // No model: no content to ask for, but the floor still has one column and both bars.
    table_view_t bare;
    CHECK(bare.content_width() == 0);
    CHECK(bare.size_hint() == (extent_t{ 0, list_height }));
    CHECK(bare.minimum_size_hint() == (extent_t{ default_column + 2 * thickness,
                                                 header + 30 + 2 * thickness }));
}

void test_list_regression() {
    std::printf("(9) the list regression: no header, no horizontal bar, the old hints\n");
    const style_t& style = default_style();
    const int list_min_width = style.pixel_metric(pixel_metric_enum::LIST_VIEW_MIN_WIDTH);
    const int thickness = style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);

    // A multi-column model changes nothing for a list: it asks column 0 only and never scrolls
    // horizontally, so the inherited hook still reports the viewport width as its content width.
    probe_table_model_t model{ 100, 3 };
    list_view_t list;
    list.set_geometry(rect_t{ 0, 0, 240, 180 });
    list.set_model(&model);
    CHECK(list.row_height() == 30);
    CHECK(list.size_hint() == (extent_t{ 160, 160 }));
    CHECK(list.minimum_size_hint() == (extent_t{ list_min_width, 30 + thickness }));
    scroll_bar_t* const horizontal = bar_of(list, orientation_enum::HORIZONTAL);
    CHECK(horizontal != nullptr);
    CHECK(!horizontal->is_visible());
    CHECK(list.scroll_offset() == (point_t{ 0, 0 }));
    CHECK(!list.interact(wheel_at(point_t{ 10, 100 }, point_t{ 1, 0 })));
    CHECK(list.scroll_offset().x() == 0);

    image_t list_frame{ extent_t{ 240, 180 } };
    model.forget_calls();
    paint_into(list, list_frame);
    CHECK(model.data_calls == 6); // 180 / 30
    int non_zero_columns = 0;
    for (const auto& [row, column] : model.asked_cells) {
        (void)row;
        non_zero_columns += column != 0 ? 1 : 0;
    }
    CHECK(non_zero_columns == 0);

    // No header band at the top of a list: the very first pixel row belongs to row 0, while the same
    // pixel of a table is the header.
    CHECK(list_frame.pixel(200, 0) == row_fill_color);
    table_view_t table;
    table.set_geometry(rect_t{ 0, 0, 240, 180 });
    table.set_model(&model);
    image_t table_frame{ extent_t{ 240, 180 } };
    paint_into(table, table_frame);
    CHECK(table_frame.pixel(200, 0) == header_fill_color);

    // The current cell is marked: the outline of cell (0, 1) runs down its left edge (x == 80), where
    // the row outline (which only follows the row rectangle) never paints. A focused table draws it;
    // an unfocused one does not.
    CHECK(count_color(list_frame, rect_t{ 0, 0, 240, 180 }, cell_outline_color) == 0);
    CHECK(count_color(table_frame, rect_t{ 0, 0, 240, 180 }, cell_outline_color) == 0);
    table.set_current_cell(0, 1);
    image_t unfocused{ extent_t{ 240, 180 } };
    paint_into(table, unfocused);
    CHECK(unfocused.pixel(80, 30) == grid_color); // no focus, no outline: the grid line shows
    input_router_t table_router{ table };
    table_router.set_focus(&table);
    CHECK(table.has_focus());
    image_t marked{ extent_t{ 240, 180 } };
    paint_into(table, marked);
    CHECK(marked.pixel(80, 30) == cell_outline_color);
    CHECK(!(marked.pixel(80, 30) == grid_color)); // the outline covers the grid line beneath it
    CHECK(count_color(marked, rect_t{ 0, 0, 240, 180 }, cell_outline_color) > 0);
}

void test_header_child() {
    std::printf("(10) the header is a child item, not part of the content\n");
    const style_t& style = default_style();
    const int header = style.pixel_metric(pixel_metric_enum::TABLE_HEADER_HEIGHT);
    const int thickness = style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);

    probe_table_model_t model{ 100, 4 };
    model.set_header(0, "unit 0");
    model.set_header(1, "unit 1");
    model.set_header(2, "unit 2");
    model.set_header(3, "unit 3");
    table_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 180 });
    view.set_model(&model);

    widget_t* const header_item = view.header_item();
    CHECK(header_item != nullptr);
    CHECK(header_item->parent() == &view); // a real child item of the view ...
    CHECK(header_item != view.content());  // ... and not the scroll content
    CHECK(header_item->geometry() == (rect_t{ 0, 0, 240 - thickness, header }));
    const rect_t destination = view.destination_rect();
    // The destination is the body minus the strip: the header occupies the band ABOVE it.
    CHECK(destination == (rect_t{ 0, header, 240 - thickness, 180 - header - thickness }));
    const rect_t band{ 0, 0, destination.width(), header };

    image_t visible{ extent_t{ 240, 180 } };
    paint_into(view, visible);
    CHECK(header_item->is_visible());
    CHECK(count_color(visible, band, header_fill_color) > 0);
    CHECK(count_color(visible, band, text_color) > 0);

    // Hiding the header changes NO geometry (it is not a band carved out of the content); it only
    // stops the strip from being painted.
    header_item->set_visible(false);
    CHECK(!header_item->is_visible());
    CHECK(view.destination_rect() == destination);
    const int content_width_before = view.content_width();
    image_t hidden{ extent_t{ 240, 180 } };
    paint_into(view, hidden);
    CHECK(view.destination_rect() == destination);
    CHECK(view.content_width() == content_width_before);
    CHECK(count_color(hidden, band, header_fill_color) == 0);
    CHECK(count_color(hidden, band, text_color) == 0);
    // The rows below the (still reserved) band are painted exactly as before.
    const rect_t rows{ destination.x(), destination.y(), destination.width(),
                       destination.height() };
    CHECK(count_color(hidden, rows, row_fill_color) > 0);

    // Showing it again restores the titles: the strip was never reparented or removed.
    header_item->set_visible(true);
    image_t restored{ extent_t{ 240, 180 } };
    paint_into(view, restored);
    CHECK(count_color(restored, band, header_fill_color) > 0);
}

} // namespace table_view_test

int main() {
    pin_builtin_font_family(); // deterministic text: the built-in face, not the machine's UI font
    using namespace table_view_test;
    std::printf("== table_view_test ==\n");
    test_columns();
    test_cell_virtualisation();
    test_column_virtualisation();
    test_header();
    test_hit_test();
    test_keyboard();
    test_horizontal_scroll();
    test_size_hints();
    test_list_regression();
    test_header_child();
    std::printf("checks: %d, failures: %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
