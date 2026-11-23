// tree_view_t — the hierarchical item view.
//
// The acceptance criteria as executable checks:
//  0. the model's hierarchical members: the flat defaults (row_count(parent) == 0, an invalid
//     parent(), an out-of-range index() invalid) and a hierarchical model's round trip;
//  1. flattening: the visible list before and after expanding, the row numbers, the display order and
//     each row's depth; a collapsed parent hides its whole subtree, while an expanded node's own state
//     is remembered across its parent's collapse;
//  2. expanded_changed: emitted only on a real change (never on a repeated expand/collapse, a leaf, or
//     an index the view does not show);
//  3. virtualisation: a 3-level, 1,110-node model asks data() only for the rows the viewport shows, at
//     the top and at the bottom of the range (counted), and a paint pass makes no index() call at all;
//  4. the painting: the indentation of every depth, the expander's position and its two forms, the
//     leaf's text lining up with an expandable sibling's, and set_indentation() moving everything;
//  5. the hit test: index_at() on the rows, the blank area, the bars and the outside of the view; a
//     press on an expander toggles the node, leaves the current row and the selection alone, takes no
//     capture, and the release after it activates nothing;
//  6. the keyboard: Right expands then moves to the first child, Left collapses then moves to the
//     parent, and the inherited Up/Down/PageUp/PageDown/Home/End navigation works on visible rows;
//  7. the selection: SINGLE/MULTI, Ctrl/Shift and the anchor on visible rows, and a rebuild (an expand
//     or a collapse) carrying the current NODE and the selection along;
//  8. the model notifications: data_changed damaging exactly its rows and re-measuring only them, a
//     wider text moving the content width and the horizontal bar, rows_inserted/rows_removed rebuilding
//     the visible list and keeping the current node, a removed node clearing the current row, and
//     model_reset dropping the expansion and the current row;
//  9. the regression: a flat model SEEN THROUGH A TREE (the hierarchical defaults at work), and
//     list_view_t / table_view_t asking and painting exactly what they did before.
//
// Offscreen only (image_t + painter_t); no window and no video subsystem are created.

#include <algorithm>
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
#include "hwshqtb/metaobject/meta_method_t.h"
#include "hwshqtb/metaobject/meta_object_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/ui/abstract_item_view_t.h"
#include "hwshqtb/ui/input_router_t.h"
#include "hwshqtb/ui/item_model_t.h"
#include "hwshqtb/ui/list_view_t.h"
#include "hwshqtb/ui/scroll_bar_t.h"
#include "hwshqtb/ui/style_t.h"
#include "hwshqtb/ui/table_view_t.h"
#include "hwshqtb/ui/tree_view_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;
using namespace hwshqtb::ui;

namespace tree_view_test {

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

// ---- the probe models ------------------------------------------------------------------------

/// @brief One node of the probe tree: the name it shows and its place in the hierarchy.
struct node_t {
    /// @brief The DISPLAY text of the node.
    std::string name;
    /// @brief The parent, or nullptr for a top-level node.
    node_t* parent = nullptr;
    /// @brief The children, in row order.
    std::vector<node_t*> children;
};

/// @brief A hierarchical probe model: named nodes, call counters, and the notifications the test drives
///        by hand.
///
/// @par Counters
/// Every query is counted, and every data() call for an index that names no node is counted apart: a
/// virtualised view answers 0 there.
class probe_tree_model_t final: public item_model_t {
public:
    probe_tree_model_t() = default;

    ~probe_tree_model_t() override {
        for (node_t* root : _roots) {
            delete_subtree(root);
        }
    }

    probe_tree_model_t(const probe_tree_model_t&) = delete;
    probe_tree_model_t& operator=(const probe_tree_model_t&) = delete;
    probe_tree_model_t(probe_tree_model_t&&) = delete;
    probe_tree_model_t& operator=(probe_tree_model_t&&) = delete;

    /// @brief Append a node under @p parent (nullptr makes a new top-level row).
    /// @return The new node; the model owns it.
    node_t* add_node(node_t* parent, std::string name) {
        node_t* const node = new node_t;
        node->name = std::move(name);
        node->parent = parent;
        if (parent != nullptr) {
            parent->children.push_back(node);
        }
        else {
            _roots.push_back(node);
        }
        return node;
    }

    /// @brief The top-level nodes, in row order.
    const std::vector<node_t*>& roots() const noexcept {
        return _roots;
    }

    /// @brief Give @p node a new DISPLAY text (does not notify: the test emits).
    void set_name(node_t* node, std::string name) {
        if (node != nullptr) {
            node->name = std::move(name);
        }
    }

    /// @brief The index that names @p node, or an invalid index when it is not in the tree.
    item_index_t index_of(const node_t* node) const {
        if (node == nullptr) {
            return item_index_t{};
        }
        const std::vector<node_t*>& siblings =
            node->parent != nullptr ? node->parent->children : _roots;
        for (std::size_t row = 0; row < siblings.size(); ++row) {
            if (siblings[row] == node) {
                return item_index_t{ static_cast<int>(row), 0, node };
            }
        }
        return item_index_t{};
    }

    /// @brief The index of top-level row @p row, whatever node sits there (an invalid index when the
    ///        row does not exist).
    item_index_t top_index(int row) const {
        if (row < 0 || row >= static_cast<int>(_roots.size())) {
            return item_index_t{};
        }
        return item_index_t{ row, 0, _roots[static_cast<std::size_t>(row)] };
    }

    /// @brief Insert @p count empty top-level rows at @p first and announce rows_inserted().
    void insert_roots(int first, int count) {
        const int at = std::clamp(first, 0, static_cast<int>(_roots.size()));
        for (int index = 0; index < count; ++index) {
            node_t* const node = new node_t;
            node->name = "I" + std::to_string(index);
            _roots.insert(_roots.begin() + at + index, node);
        }
        rows_inserted(at, count);
    }

    /// @brief Remove @p count top-level rows at @p first and announce rows_removed().
    void remove_roots(int first, int count) {
        const int size = static_cast<int>(_roots.size());
        const int at = std::clamp(first, 0, size);
        const int removed = std::clamp(count, 0, size - at);
        for (int index = 0; index < removed; ++index) {
            delete_subtree(_roots[static_cast<std::size_t>(at)]);
            _roots.erase(_roots.begin() + at);
        }
        rows_removed(at, removed);
    }

    /// @brief Announce data_changed() for the visible rows @p first .. @p last.
    void notify_data(int first, int last) {
        data_changed(first, last);
    }

    /// @brief Replace every top-level node with one root and announce model_reset().
    void reset_to_one_root() {
        for (node_t* root : _roots) {
            delete_subtree(root);
        }
        _roots.clear();
        add_node(nullptr, "R");
        model_reset();
    }

    int row_count() const override {
        ++row_count_calls;
        return static_cast<int>(_roots.size());
    }

    int row_count(const item_index_t& parent) const override {
        ++row_count_calls;
        const node_t* const node = node_of(parent);
        return node != nullptr ? static_cast<int>(node->children.size()) : 0;
    }

    item_index_t index(int row, int column, const item_index_t& parent) const override {
        ++index_calls;
        if (column != 0 || row < 0) {
            return item_index_t{};
        }
        const std::vector<node_t*>& siblings = siblings_of(parent);
        if (row >= static_cast<int>(siblings.size())) {
            ++index_out_of_range_calls;
            return item_index_t{};
        }
        return item_index_t{ row, column, siblings[static_cast<std::size_t>(row)] };
    }

    item_index_t parent(const item_index_t& index) const override {
        ++parent_calls;
        const node_t* const node = node_of(index);
        return node != nullptr ? index_of(node->parent) : item_index_t{};
    }

    any_t data(const item_index_t& index, item_role_enum role) const override {
        ++data_calls;
        const node_t* const node = node_of(index);
        if (node == nullptr) {
            ++data_out_of_range_calls;
            return {};
        }
        asked.push_back(index);
        if (role != item_role_enum::DISPLAY) {
            return {};
        }
        return any_t(std::string_view(node->name));
    }

    /// @brief Zero every counter and the ask list.
    void forget_calls() {
        row_count_calls = 0;
        index_calls = 0;
        index_out_of_range_calls = 0;
        parent_calls = 0;
        data_calls = 0;
        data_out_of_range_calls = 0;
        asked.clear();
    }

    mutable int row_count_calls = 0;
    mutable int index_calls = 0;
    mutable int index_out_of_range_calls = 0;
    mutable int parent_calls = 0;
    mutable int data_calls = 0;
    mutable int data_out_of_range_calls = 0;
    mutable std::vector<item_index_t> asked;

private:
    /// @brief Delete @p node and its whole subtree.
    static void delete_subtree(node_t* node) {
        for (node_t* child : node->children) {
            delete_subtree(child);
        }
        delete node;
    }

    /// @brief The node @p index names, or nullptr for an invalid index.
    static const node_t* node_of(const item_index_t& index) {
        return index.is_valid() ? static_cast<const node_t*>(index.node) : nullptr;
    }

    /// @brief The children a node under @p parent has; the top level for an invalid parent.
    const std::vector<node_t*>& siblings_of(const item_index_t& parent) const {
        static const std::vector<node_t*> none;
        if (!parent.is_valid()) {
            return _roots;
        }
        const node_t* const node = node_of(parent);
        return node != nullptr ? node->children : none;
    }

    std::vector<node_t*> _roots;
};

/// @brief A FLAT probe model: it overrides only the row form (row_count(), data(row, role)), which is
///        the compatibility claim the hierarchical members must not break.
class probe_flat_model_t final: public item_model_t {
public:
    explicit probe_flat_model_t(int rows):
        _rows(rows) {
        for (int row = 0; row < rows; ++row) {
            _texts.push_back("L" + std::to_string(row));
        }
    }

    int row_count() const override {
        return _rows;
    }

    any_t data(int row, item_role_enum role) const override {
        ++data_calls;
        if (row < 0 || row >= _rows) {
            ++out_of_range_calls;
            return {};
        }
        asked_rows.push_back(row);
        if (role != item_role_enum::DISPLAY) {
            return {};
        }
        return any_t(std::string_view(_texts[static_cast<std::size_t>(row)]));
    }

    /// @brief Replace the text of @p row.
    void set_text(int row, std::string text) {
        if (row >= 0 && row < _rows) {
            _texts[static_cast<std::size_t>(row)] = std::move(text);
        }
    }

    void forget_calls() {
        data_calls = 0;
        out_of_range_calls = 0;
        asked_rows.clear();
    }

    mutable int data_calls = 0;
    mutable int out_of_range_calls = 0;
    mutable std::vector<int> asked_rows;

private:
    int _rows = 0;
    std::vector<std::string> _texts;
};

/// @brief A flat THREE-COLUMN probe model for the table regression: "r<row>c<column>" cells.
class probe_cell_model_t final: public item_model_t {
public:
    probe_cell_model_t(int rows, int columns):
        _rows(rows),
        _columns(columns),
        _headers(static_cast<std::size_t>(columns < 0 ? 0 : columns)) {
        for (int row = 0; row < _rows; ++row) {
            for (int column = 0; column < _columns; ++column) {
                _cells.push_back("r" + std::to_string(row) + "c" + std::to_string(column));
            }
        }
        for (int column = 0; column < _columns; ++column) {
            _headers[static_cast<std::size_t>(column)] = "H" + std::to_string(column);
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
        return any_t(std::string_view(_cells[static_cast<std::size_t>(row)
                                              * static_cast<std::size_t>(_columns)
                                          + static_cast<std::size_t>(column)]));
    }

    any_t header_data(int column, item_role_enum role) const override {
        if (column < 0 || column >= _columns || role != item_role_enum::DISPLAY) {
            return {};
        }
        return any_t(std::string_view(_headers[static_cast<std::size_t>(column)]));
    }

    void forget_calls() {
        data_calls = 0;
        out_of_range_calls = 0;
        asked_cells.clear();
    }

    mutable int data_calls = 0;
    mutable int out_of_range_calls = 0;
    mutable std::vector<std::pair<int, int>> asked_cells;

private:
    int _rows = 0;
    int _columns = 0;
    std::vector<std::string> _cells;
    std::vector<std::string> _headers;
};

// ---- steps and pixels -----------------------------------------------------------------------

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

interaction_t key_step(int key, std::uint32_t modifiers = 0) {
    interaction_t in;
    in.key = key;
    in.key_pressed = true;
    in.modifiers = modifiers;
    return in;
}

/// @brief The raw modifier bits of @p flag, ready for interaction_t::modifiers.
std::uint32_t modifiers_of(modifier_flag flag) {
    return modifier_bits(flag);
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

/// @brief Drop the damage of the view AND its scroll bars.
void clear_damages(abstract_item_view_t& view) {
    view.clear_damage();
    for (std::size_t index = 0; index < view.item_count(); ++index) {
        if (item_t* const child = view.item_at_index(index)) {
            if (child->is_widget()) {
                static_cast<widget_t*>(child)->clear_damage();
            }
        }
    }
}

/// @brief Whether @p region covers every pixel of @p rect.
bool covers(const region_t& region, rect_t rect) {
    return region_t{ rect }.subtracted(region).is_empty();
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
constexpr color_t text_color{ 245, 248, 250 };           // LIST_TEXT
constexpr color_t expander_fill_color{ 40, 55, 70 };     // TREE_EXPANDER background
constexpr color_t expander_marker_color{ 150, 165, 180 }; // TREE_EXPANDER border and marker

/// @brief The content x a depth's expander starts at (the documented layout rule).
int expander_left(const tree_view_t& view, int depth) {
    const int padding = default_style().pixel_metric(pixel_metric_enum::TEXT_PADDING);
    return padding + depth * view.indentation();
}

/// @brief The content x a depth's text starts at: the expander column plus one padding.
int text_left(const tree_view_t& view, int depth) {
    const int padding = default_style().pixel_metric(pixel_metric_enum::TEXT_PADDING);
    return expander_left(view, depth)
        + default_style().pixel_metric(pixel_metric_enum::TREE_EXPANDER_SIZE) + padding;
}

/// @brief The expander box of @p row of a node @p depth levels deep, in the view's coordinates.
/// @note The view is expected to sit at the origin with no offsets, which every caller arranges.
rect_t expander_box(const tree_view_t& view, int row, int depth) {
    const int size = default_style().pixel_metric(pixel_metric_enum::TREE_EXPANDER_SIZE);
    return rect_t{ expander_left(view, depth),
                   row * view.row_height() + (view.row_height() - size) / 2, size, size };
}

/// @brief The full row band of @p row of a view @p width wide at the origin.
rect_t row_band(const tree_view_t& view, int row, int width) {
    return rect_t{ 0, row * view.row_height(), width, view.row_height() };
}

/// @brief The normalised signature of the signal @p name of @p object, or an empty string.
std::string signal_signature(const object_t& object, std::string_view name) {
    const meta_object_t* const meta = object.meta_object();
    for (std::size_t index = 0; index < meta->method_count(); ++index) {
        const meta_method_t method = meta->method(static_cast<index_t>(index));
        if (method.is_signal() && method.name() == name) {
            return std::string(method.signature());
        }
    }
    return {};
}

/// @brief A probe of expanded_changed(): every emission, in order.
struct expansion_probe_t {
    /// @brief Connect to @p view's expanded_changed().
    /// @return true when the signal was found and connected.
    bool attach(object_t& object) {
        const std::string key = signal_signature(object, "expanded_changed");
        if (key.empty()) {
            return false;
        }
        object.connect(key, [this](const item_index_t& index, bool expanded) {
            events.emplace_back(index, expanded);
        });
        return true;
    }

    /// @brief The node/state pairs, in emission order.
    std::vector<std::pair<item_index_t, bool>> events;
};

// ---- (0) the model's hierarchical defaults ----------------------------------------------------

void test_model_defaults() {
    std::printf("(0) item_model_t: the flat defaults of the hierarchical members\n");
    probe_flat_model_t flat{ 3 };
    CHECK(flat.row_count() == 3);
    // A view holds an item_model_t, so the hierarchical members are asked through the base interface
    // (a model that overrides the row form hides the overloads on ITS OWN type, not through the base).
    const item_model_t& model = flat;
    // A flat model has no children, no parent and no depth: the three defaults say exactly that.
    CHECK(model.row_count(item_index_t{}) == 0);
    const item_index_t top = model.index(0, 0, item_index_t{});
    CHECK(top.is_valid());
    CHECK(top.row == 0);
    CHECK(top.column == 0);
    CHECK(top.node == nullptr);
    CHECK(!model.parent(top).is_valid());
    // index() validates the pair against the row and column counts.
    CHECK(!model.index(3, 0, item_index_t{}).is_valid());
    CHECK(!model.index(-1, 0, item_index_t{}).is_valid());
    CHECK(!model.index(0, 1, item_index_t{}).is_valid()); // column_count() is 1
    CHECK(!model.index(0, -1, item_index_t{}).is_valid());
    // The index form of data() forwards to the row form, which is what lets a flat model serve a tree.
    const any_t value = model.data(top, item_role_enum::DISPLAY);
    CHECK(value.is_initialized());
    CHECK(value.get<std::string_view>() == "L0");
    CHECK(!model.data(item_index_t{}, item_role_enum::DISPLAY).is_initialized());

    // A hierarchical model round-trips: children of a parent, a parent of a child, no parent at the top.
    probe_tree_model_t tree;
    node_t* const root = tree.add_node(nullptr, "A");
    node_t* const child = tree.add_node(root, "A0");
    tree.add_node(child, "A00");
    const item_index_t root_index = tree.index_of(root);
    const item_index_t child_index = tree.index_of(child);
    CHECK(root_index.is_valid());
    CHECK(root_index.node == static_cast<const void*>(root));
    CHECK(tree.row_count() == 1);
    CHECK(tree.row_count(root_index) == 1);
    CHECK(tree.row_count(child_index) == 1);
    CHECK(tree.index(0, 0, root_index) == child_index);
    CHECK(tree.parent(child_index) == root_index);
    CHECK(!tree.parent(root_index).is_valid());
    CHECK(!tree.index(1, 0, root_index).is_valid());
    CHECK(tree.data(child_index, item_role_enum::DISPLAY).get<std::string_view>() == "A0");
}

// ---- (1) flattening --------------------------------------------------------------------------

void test_flattening() {
    std::printf("(1) flattening: the visible list, the row numbers and the display order\n");
    probe_tree_model_t model;
    node_t* const a = model.add_node(nullptr, "A");
    node_t* const a0 = model.add_node(a, "A0");
    node_t* const a00 = model.add_node(a0, "A00");
    node_t* const a01 = model.add_node(a0, "A01");
    node_t* const a1 = model.add_node(a, "A1");
    node_t* const b = model.add_node(nullptr, "B");
    node_t* const b0 = model.add_node(b, "B0");

    tree_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 400 });
    view.set_model(&model);

    // Nothing is expanded: only the top level is shown, and a row's node is what index_at() answers.
    CHECK(view.visible_row_count() == 2);
    CHECK(view.index_at(point_t{ 5, 5 }) == model.index_of(a));
    CHECK(view.index_at(point_t{ 5, 35 }) == model.index_of(b));
    CHECK(!view.index_at(point_t{ 5, 65 }).is_valid());
    // The rows below the last one are not rows: a point there is not in the model at all.
    CHECK(view.index_at(point_t{ 5, 300 }).row == -1);

    // Expanding a node splices its children in at the node's position, in row order.
    view.expand(model.index_of(a));
    CHECK(view.visible_row_count() == 4);
    CHECK(view.index_at(point_t{ 5, 5 }) == model.index_of(a));
    CHECK(view.index_at(point_t{ 5, 35 }) == model.index_of(a0));
    CHECK(view.index_at(point_t{ 5, 65 }) == model.index_of(a1));
    CHECK(view.index_at(point_t{ 5, 95 }) == model.index_of(b));

    // A grandchild level nests: the expanded child's rows go between the child and its next sibling.
    view.expand(model.index_of(a0));
    CHECK(view.visible_row_count() == 6);
    CHECK(view.index_at(point_t{ 5, 5 }) == model.index_of(a));
    CHECK(view.index_at(point_t{ 5, 35 }) == model.index_of(a0));
    CHECK(view.index_at(point_t{ 5, 65 }) == model.index_of(a00));
    CHECK(view.index_at(point_t{ 5, 95 }) == model.index_of(a01));
    CHECK(view.index_at(point_t{ 5, 125 }) == model.index_of(a1));
    CHECK(view.index_at(point_t{ 5, 155 }) == model.index_of(b));

    // Collapsing the parent hides the WHOLE subtree ...
    view.collapse(model.index_of(a));
    CHECK(view.visible_row_count() == 2);
    CHECK(view.index_at(point_t{ 5, 35 }) == model.index_of(b));
    // ... and keeps the grandchild's own expansion: re-expanding the parent restores all six rows.
    CHECK(!view.is_expanded(model.index_of(a)));
    CHECK(view.is_expanded(model.index_of(a0)));
    view.expand(model.index_of(a));
    CHECK(view.visible_row_count() == 6);
    CHECK(view.index_at(point_t{ 5, 65 }) == model.index_of(a00));

    // A second top-level branch keeps its own state.
    view.expand(model.index_of(b));
    CHECK(view.visible_row_count() == 7);
    CHECK(view.index_at(point_t{ 5, 185 }) == model.index_of(b0));
    view.collapse(model.index_of(a0));
    CHECK(view.visible_row_count() == 5);
    CHECK(view.index_at(point_t{ 5, 65 }) == model.index_of(a1));
    CHECK(view.index_at(point_t{ 5, 95 }) == model.index_of(b));

    // The data the tree asked for is the DISPLAY text of exactly those nodes, in row order.
    CHECK(!model.asked.empty());
    CHECK(model.data_out_of_range_calls == 0);
    CHECK(model.index_out_of_range_calls == 0);
}

// ---- (2) expanded_changed --------------------------------------------------------------------

void test_expanded_signals() {
    std::printf("(2) expanded_changed: only a real change announces itself\n");
    probe_tree_model_t model;
    node_t* const a = model.add_node(nullptr, "A");
    node_t* const a0 = model.add_node(a, "A0");
    node_t* const a00 = model.add_node(a0, "A00");
    model.add_node(a, "A1");
    model.add_node(nullptr, "B");

    tree_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 400 });
    view.set_model(&model);

    expansion_probe_t probe;
    CHECK(probe.attach(view));
    const item_index_t a_index = model.index_of(a);
    const item_index_t a0_index = model.index_of(a0);
    const item_index_t a00_index = model.index_of(a00);

    view.expand(a_index);
    CHECK(probe.events.size() == 1);
    CHECK(probe.events[0].first == a_index);
    CHECK(probe.events[0].second);

    // Repeating the same state changes nothing and announces nothing.
    view.expand(a_index);
    CHECK(probe.events.size() == 1);
    CHECK(view.visible_row_count() == 4);

    // A grandchild of the collapsed node is not on screen: expanding it would insert rows the caller
    // cannot see, so the view refuses and says nothing.
    view.collapse(a_index);
    CHECK(probe.events.size() == 2);
    CHECK(!probe.events[1].second);
    view.expand(a0_index);
    CHECK(probe.events.size() == 2);
    CHECK(!view.is_expanded(a0_index));
    view.expand(a_index);
    CHECK(probe.events.size() == 3);

    // A repeated collapse is silent too.
    view.collapse(a_index);
    CHECK(probe.events.size() == 4);
    view.collapse(a_index);
    CHECK(probe.events.size() == 4);

    // toggle_expanded() is the same change seen from the other side.
    view.toggle_expanded(a_index);
    CHECK(probe.events.size() == 5);
    CHECK(probe.events[4].second);
    view.toggle_expanded(a_index);
    CHECK(probe.events.size() == 6);
    CHECK(!probe.events[5].second);

    // A leaf never expands: it has no children, so there is nothing to show and nothing to announce.
    view.expand(a_index);
    const int before_leaf = static_cast<int>(probe.events.size());
    view.expand(a00_index);
    CHECK(probe.events.size() == static_cast<std::size_t>(before_leaf));
    CHECK(!view.is_expanded(a00_index));
    view.collapse(a00_index);
    CHECK(probe.events.size() == static_cast<std::size_t>(before_leaf));

    // An invalid index is not a node either.
    view.expand(item_index_t{});
    view.collapse(item_index_t{});
    CHECK(probe.events.size() == static_cast<std::size_t>(before_leaf));
}

// ---- (3) virtualisation ----------------------------------------------------------------------

void test_virtualisation() {
    std::printf("(3) virtualisation: a 1,110-node tree asks only for the visible rows\n");
    probe_tree_model_t model;
    for (int root = 0; root < 10; ++root) {
        node_t* const root_node = model.add_node(nullptr, "r" + std::to_string(root));
        for (int child = 0; child < 10; ++child) {
            node_t* const child_node =
                model.add_node(root_node, "c" + std::to_string(root) + "." + std::to_string(child));
            for (int grand = 0; grand < 10; ++grand) {
                model.add_node(child_node, "g" + std::to_string(grand));
            }
        }
    }

    tree_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 180 });
    view.set_model(&model);
    CHECK(view.row_height() == 30);
    CHECK(view.visible_row_count() == 10); // the top level only

    for (node_t* const root : model.roots()) {
        view.expand(model.index_of(root));
        for (node_t* const child : root->children) {
            view.expand(model.index_of(child));
        }
    }
    CHECK(view.visible_row_count() == 10 + 100 + 1000);
    // A visible bar appears (33,300 pixels of content in a 180-pixel destination), so the destination
    // is 226 pixels wide and shows six 30-pixel rows.
    const rect_t destination = view.destination_rect();
    CHECK(destination == (rect_t{ 0, 0, 240 - 14, 180 }));

    model.forget_calls();
    image_t image{ extent_t{ 240, 180 } };
    paint_into(view, image);
    CHECK(model.data_calls == 6);
    CHECK(model.data_out_of_range_calls == 0);
    // A paint pass never walks the model: no index() call, no row_count() call.
    CHECK(model.index_calls == 0);
    CHECK(model.row_count_calls == 0);
    // The frame asked for the first six rows of the flattened order: the first root, its first child,
    // and that child's first four children.
    node_t* const first_root = model.roots().front();
    node_t* const first_child = first_root->children.front();
    std::vector<item_index_t> expected;
    expected.push_back(model.index_of(first_root));
    expected.push_back(model.index_of(first_child));
    for (int grand = 0; grand < 4; ++grand) {
        expected.push_back(model.index_of(first_child->children[static_cast<std::size_t>(grand)]));
    }
    CHECK(model.asked == expected);

    // The same cost at the bottom of the range: the rows moved, the count did not.
    view.scroll_to_row(1110 - 1);
    scroll_bar_t* const vertical = bar_of(view, orientation_enum::VERTICAL);
    CHECK(vertical != nullptr);
    CHECK(vertical->maximum() == 1110 * 30 - 180);
    CHECK(view.scroll_offset().y() == 1110 * 30 - 180);
    model.forget_calls();
    paint_into(view, image);
    CHECK(model.data_calls == 6);
    CHECK(model.data_out_of_range_calls == 0);
    CHECK(model.index_calls == 0);
    // The last six rows are the grandchildren 4..9 of the last child of the last root.
    node_t* const last_child = model.roots().back()->children.back();
    std::vector<item_index_t> tail;
    for (int grand = 4; grand < 10; ++grand) {
        tail.push_back(model.index_of(last_child->children[static_cast<std::size_t>(grand)]));
    }
    CHECK(model.asked == tail);
}

// ---- (4) the painting ------------------------------------------------------------------------

void test_painting() {
    std::printf("(4) painting: indentation, the expander's two forms and the text column\n");
    const int padding = default_style().pixel_metric(pixel_metric_enum::TEXT_PADDING);
    const int indentation = default_style().pixel_metric(pixel_metric_enum::TREE_INDENTATION);
    const int expander = default_style().pixel_metric(pixel_metric_enum::TREE_EXPANDER_SIZE);
    CHECK(padding == 8);
    CHECK(indentation == 16);
    CHECK(expander == 12);

    probe_tree_model_t model;
    node_t* const a = model.add_node(nullptr, "A");
    node_t* const l1 = model.add_node(a, "L1");
    model.add_node(l1, "L2");

    tree_view_t view;
    view.set_geometry(rect_t{ 0, 0, 300, 160 });
    view.set_model(&model);
    CHECK(view.indentation() == indentation);
    CHECK(view.visible_row_count() == 1); // just "A" until it is expanded

    view.expand(model.index_of(a));
    view.expand(model.index_of(l1));
    CHECK(view.visible_row_count() == 3);

    image_t image{ extent_t{ 300, 160 } };
    paint_into(view, image);

    // The text column is one padding past the expander column, and the expander column is one
    // indentation per level: three depths, three columns.
    CHECK(first_color_x(image, row_band(view, 0, 300), text_color) == text_left(view, 0));
    CHECK(first_color_x(image, row_band(view, 1, 300), text_color) == text_left(view, 1));
    CHECK(first_color_x(image, row_band(view, 2, 300), text_color) == text_left(view, 2));
    CHECK(text_left(view, 0) == 28);
    CHECK(text_left(view, 1) == 44);
    CHECK(text_left(view, 2) == 60);

    // The expander sits in its column, one row-height high and centred: its top-left corner is the
    // border pixel of the box.
    const rect_t box_a = expander_box(view, 0, 0);
    const rect_t box_l1 = expander_box(view, 1, 1);
    CHECK(box_a == (rect_t{ 8, 9, 12, 12 }));
    CHECK(box_l1 == (rect_t{ 24, 39, 12, 12 }));
    CHECK(image.pixel(box_a.x(), box_a.y()) == expander_marker_color);
    CHECK(image.pixel(box_l1.x(), box_l1.y()) == expander_marker_color);

    // The two forms differ by the vertical stroke: expanded shows the horizontal one alone, so the
    // pixel just above the middle bar is the box fill; collapsed adds the stroke there.
    const point_t stroke_point{ box_a.x() + 5, box_a.y() + 3 };
    CHECK(image.pixel(stroke_point.x(), stroke_point.y()) == expander_fill_color);
    const int expanded_marker = count_color(image, box_a, expander_marker_color);
    CHECK(expanded_marker > 0);
    view.collapse(model.index_of(a));
    image_t collapsed_image{ extent_t{ 300, 160 } };
    paint_into(view, collapsed_image);
    CHECK(collapsed_image.pixel(stroke_point.x(), stroke_point.y()) == expander_marker_color);
    // Exactly the vertical stroke was added: its 2 x 6 pixels minus the 2 x 2 the two bars share.
    CHECK(count_color(collapsed_image, box_a, expander_marker_color) == expanded_marker + 12 - 4);

    // A leaf has no expander at all: the whole column stays background, and its text still starts after
    // the reserved column so it lines up with an expandable sibling's.
    view.expand(model.index_of(a));
    image_t leaves{ extent_t{ 300, 160 } };
    paint_into(view, leaves);
    const rect_t leaf_column{ expander_left(view, 2), row_band(view, 2, 300).y(), expander, expander };
    CHECK(count_color(leaves, leaf_column, expander_marker_color) == 0);
    CHECK(count_color(leaves, leaf_column, expander_fill_color) == 0);
    CHECK(first_color_x(leaves, row_band(view, 2, 300), text_color) == text_left(view, 2));
    CHECK(text_left(view, 2) == 8 + 2 * 16 + 12 + 8);

    // set_indentation() moves every level: the same rows, a new column for each depth.
    view.set_indentation(24);
    CHECK(view.indentation() == 24);
    image_t wider{ extent_t{ 300, 160 } };
    paint_into(view, wider);
    CHECK(first_color_x(wider, row_band(view, 2, 300), text_color) == 8 + 2 * 24 + 12 + 8);
    CHECK(first_color_x(wider, row_band(view, 1, 300), text_color) == 8 + 24 + 12 + 8);
    CHECK(expander_box(view, 0, 0) == (rect_t{ padding, 9, expander, expander }));
    // A negative indentation is zero, not a backwards layout.
    view.set_indentation(-5);
    CHECK(view.indentation() == 0);
    view.set_indentation(16);
}

// ---- (5) the hit test ------------------------------------------------------------------------

void test_hit_test() {
    std::printf("(5) the hit test: rows, the expander, the blank area and the outside\n");
    probe_tree_model_t model;
    node_t* const a = model.add_node(nullptr, "A");
    model.add_node(a, "A0");
    node_t* const b = model.add_node(nullptr, "B");
    model.add_node(b, "B0");

    tree_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 200 });
    view.set_model(&model);
    const item_index_t a_index = model.index_of(a);
    const item_index_t b_index = model.index_of(b);

    // A row hit answers the node of the row under the point, at any x inside the view.
    CHECK(view.index_at(point_t{ 5, 5 }) == a_index);
    CHECK(view.index_at(point_t{ 100, 35 }) == b_index);
    // Below the last row, outside the view and over a bar there is no node.
    CHECK(!view.index_at(point_t{ 5, 65 }).is_valid());
    CHECK(!view.index_at(point_t{ 5, 199 }).is_valid());
    CHECK(!view.index_at(point_t{ 300, 5 }).is_valid());
    CHECK(!view.index_at(point_t{ -1, 5 }).is_valid());

    // The current node and its selection are in place before the expander is pressed.
    view.set_current_index(a_index);
    CHECK(view.current_row() == 0);
    CHECK(view.selected_rows() == (std::vector<int>{ 0 }));

    // A press on B's expander toggles B, and only B: the row hit is NOT what happened, so neither the
    // current row nor the selection moved and the press recorded no pressed ROW. The router owns the
    // implicit grab of any press, so the capture flag itself says nothing about which part was hit;
    // the observable difference is the release below (an expander press never activates a row).
    input_router_t router{ view };
    int activations = 0;
    view.connect("row_activated(int) const", [&activations]() { ++activations; });
    const rect_t box_b = expander_box(view, 1, 0);
    const point_t box_b_centre{ box_b.x() + box_b.width() / 2, box_b.y() + box_b.height() / 2 };
    CHECK(router.dispatch(press(box_b_centre)));
    CHECK(view.is_expanded(b_index));
    CHECK(view.visible_row_count() == 3);
    CHECK(view.current_row() == 0);
    CHECK(view.current_index() == a_index);
    CHECK(view.selected_rows() == (std::vector<int>{ 0 }));
    // The release after it therefore activates nothing, even at the same point.
    CHECK(!router.dispatch(release(box_b_centre, true)));
    CHECK(activations == 0);

    // The same point pressed a second time collapses it again, still without touching the selection.
    CHECK(router.dispatch(press(box_b_centre)));
    CHECK(!view.is_expanded(b_index));
    CHECK(view.visible_row_count() == 2);
    CHECK(view.current_index() == a_index);
    CHECK(view.selected_rows() == (std::vector<int>{ 0 }));
    CHECK(!router.dispatch(release(box_b_centre, true)));

    // A press on the TEXT of a row is an ordinary row press: it selects that row, and the release on it
    // activates it — the contrast with the expander press above.
    CHECK(router.dispatch(press(point_t{ text_left(view, 0) + 2, 35 })));
    CHECK(view.current_row() == 1);
    CHECK(view.current_index() == b_index);
    CHECK(view.selected_rows() == (std::vector<int>{ 1 }));
    CHECK(router.dispatch(release(point_t{ text_left(view, 0) + 2, 35 }, true)));
    CHECK(activations == 1);

    // A press in the blank area below the last row clears the current row and the selection.
    CHECK(router.dispatch(press(point_t{ 100, 100 })));
    CHECK(view.current_row() == -1);
    CHECK(view.selected_rows().empty());
    CHECK(!view.index_at(point_t{ 100, 100 }).is_valid());
}

// ---- (6) the keyboard ------------------------------------------------------------------------

void test_keyboard() {
    std::printf("(6) the keyboard: Right expands then enters, Left collapses then climbs\n");
    probe_tree_model_t model;
    node_t* const a = model.add_node(nullptr, "A");
    node_t* const a0 = model.add_node(a, "A0");
    node_t* const a00 = model.add_node(a0, "A00");
    model.add_node(a, "A1");
    node_t* const b = model.add_node(nullptr, "B");

    tree_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 300 });
    view.set_model(&model);
    input_router_t router{ view };
    router.set_focus(&view);
    CHECK(view.has_focus());

    const item_index_t a_index = model.index_of(a);
    const item_index_t a0_index = model.index_of(a0);
    const item_index_t a00_index = model.index_of(a00);
    const item_index_t b_index = model.index_of(b);
    expansion_probe_t probe;
    CHECK(probe.attach(view));

    // Down establishes the first row; Right expands it and leaves the current row on the node.
    CHECK(router.dispatch(key_step(interaction_t::down_key)));
    CHECK(view.current_row() == 0);
    CHECK(router.dispatch(key_step(interaction_t::right_key)));
    CHECK(view.is_expanded(a_index));
    CHECK(view.current_row() == 0);
    CHECK(view.visible_row_count() == 4);
    // Right on an expanded node enters its first child, which is the very next visible row.
    CHECK(router.dispatch(key_step(interaction_t::right_key)));
    CHECK(view.current_row() == 1);
    CHECK(view.current_index() == a0_index);
    // The same for the grandchild level.
    CHECK(router.dispatch(key_step(interaction_t::right_key)));
    CHECK(view.is_expanded(a0_index));
    CHECK(view.current_row() == 1);
    CHECK(view.visible_row_count() == 5);
    CHECK(router.dispatch(key_step(interaction_t::right_key)));
    CHECK(view.current_row() == 2);
    CHECK(view.current_index() == a00_index);
    // Right on a leaf does nothing: there is nothing to open and no child to enter.
    CHECK(router.dispatch(key_step(interaction_t::right_key)));
    CHECK(view.current_row() == 2);

    // Left on a leaf climbs to its parent.
    CHECK(router.dispatch(key_step(interaction_t::left_key)));
    CHECK(view.current_row() == 1);
    CHECK(view.current_index() == a0_index);
    // Left on an expanded node collapses it and stays.
    CHECK(router.dispatch(key_step(interaction_t::left_key)));
    CHECK(!view.is_expanded(a0_index));
    CHECK(view.current_row() == 1);
    CHECK(view.visible_row_count() == 4);
    // Left on a collapsed node climbs one level.
    CHECK(router.dispatch(key_step(interaction_t::left_key)));
    CHECK(view.current_row() == 0);
    CHECK(view.current_index() == a_index);
    // A top-level node has no parent to climb to; a collapsed one has nothing to collapse either.
    CHECK(router.dispatch(key_step(interaction_t::left_key)));
    CHECK(!view.is_expanded(a_index));
    CHECK(view.current_row() == 0);
    CHECK(router.dispatch(key_step(interaction_t::left_key)));
    CHECK(view.current_row() == 0);
    CHECK(view.visible_row_count() == 2);

    // The inherited navigation runs on the VISIBLE rows: Down from the first row reaches the second
    // top-level node, not the first node of the model's second level.
    CHECK(router.dispatch(key_step(interaction_t::down_key)));
    CHECK(view.current_row() == 1);
    CHECK(view.current_index() == b_index);
    CHECK(router.dispatch(key_step(interaction_t::up_key)));
    CHECK(view.current_row() == 0);
    // A page is the number of whole visible rows (300 / 30 == 10), clamped to the last row.
    CHECK(router.dispatch(key_step(interaction_t::page_down_key)));
    CHECK(view.current_row() == 1);
    CHECK(router.dispatch(key_step(interaction_t::page_up_key)));
    CHECK(view.current_row() == 0);
    CHECK(router.dispatch(key_step(interaction_t::end_key)));
    CHECK(view.current_row() == 1);
    CHECK(router.dispatch(key_step(interaction_t::home_key)));
    CHECK(view.current_row() == 0);

    // Return is still the inherited row activation.
    int activations = 0;
    int activated_row = -1;
    view.connect("row_activated(int) const", [&activations, &activated_row](int row) {
        ++activations;
        activated_row = row;
    });
    CHECK(router.dispatch(key_step(interaction_t::return_key)));
    CHECK(activations == 1);
    CHECK(activated_row == 0);

    // Four expansion changes were announced: A on, A0 on, A0 off, A off.
    CHECK(probe.events.size() == 4);
    CHECK(probe.events[0].first == a_index);
    CHECK(probe.events[0].second);
    CHECK(probe.events[1].first == a0_index);
    CHECK(probe.events[1].second);
    CHECK(!probe.events[2].second);
    CHECK(!probe.events[3].second);
}

// ---- (7) the selection -----------------------------------------------------------------------

void test_selection() {
    std::printf("(7) the selection: visible rows, MULTI/Ctrl/Shift and the rebuild's re-mapping\n");
    probe_tree_model_t model;
    node_t* const a = model.add_node(nullptr, "A");
    node_t* const a0 = model.add_node(a, "A0");
    model.add_node(a, "A1");
    node_t* const b = model.add_node(nullptr, "B");
    model.add_node(b, "B0");
    node_t* const c = model.add_node(nullptr, "C");

    tree_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 300 });
    view.set_model(&model);
    view.expand(model.index_of(a));
    CHECK(view.visible_row_count() == 5); // A, A0, A1, B, C

    int selection_signals = 0;
    int current_signals = 0;
    view.connect("selection_changed() const", [&selection_signals]() { ++selection_signals; });
    view.connect("current_changed(int) const", [&current_signals]() {
        ++current_signals;
    });

    // SINGLE is the default: the clicked row is the whole selection and the current node.
    CHECK(view.selection_mode() == selection_mode_enum::SINGLE);
    CHECK(view.interact(press(point_t{ 40, 65 }))); // row 2: A1
    CHECK(view.current_row() == 2);
    CHECK(view.current_index() == model.index_of(a->children[1]));
    CHECK(view.selected_rows() == (std::vector<int>{ 2 }));
    CHECK(view.interact(release(point_t{ 40, 65 }, true)));
    CHECK(selection_signals == 1);

    // MULTI: a plain click selects one row and sets the anchor, Ctrl toggles, Shift extends.
    view.set_selection_mode(selection_mode_enum::MULTI);
    selection_signals = 0;
    view.interact(press(point_t{ 40, 5 })); // row 0: A
    view.interact(release(point_t{ 40, 5 }, true));
    CHECK(view.selected_rows() == (std::vector<int>{ 0 }));
    view.interact(press(point_t{ 40, 65 }, modifiers_of(modifier_flag::CONTROL)));
    view.interact(release(point_t{ 40, 65 }, true));
    CHECK(view.selected_rows() == (std::vector<int>{ 0, 2 }));
    CHECK(view.current_row() == 2);
    view.interact(press(point_t{ 40, 125 }, modifiers_of(modifier_flag::SHIFT)));
    view.interact(release(point_t{ 40, 125 }, true));
    // Shift extends from the anchor (row 0) to the clicked row (4): the whole visible range.
    CHECK(view.selected_rows() == (std::vector<int>{ 0, 1, 2, 3, 4 }));
    CHECK(view.current_row() == 4);
    view.interact(press(point_t{ 40, 35 }, modifiers_of(modifier_flag::CONTROL)));
    view.interact(release(point_t{ 40, 35 }, true));
    CHECK(view.selected_rows() == (std::vector<int>{ 0, 2, 3, 4 }));
    CHECK(view.current_row() == 1);

    // The selection is a set of VISIBLE ROWS: the clicked depth-1 row is row 1, not a node path.
    view.interact(press(point_t{ 40, 35 }));
    view.interact(release(point_t{ 40, 35 }, true));
    CHECK(view.selected_rows() == (std::vector<int>{ 1 }));
    CHECK(view.current_index() == model.index_of(a0));

    // Shift extends from the anchor (row 1, set by the plain click above) to the clicked row 3: the
    // range is the VISIBLE rows in between, across depths.
    view.interact(press(point_t{ 40, 95 }, modifiers_of(modifier_flag::SHIFT)));
    view.interact(release(point_t{ 40, 95 }, true));
    CHECK(view.current_row() == 3); // B
    CHECK(view.selected_rows() == (std::vector<int>{ 1, 2, 3 }));

    // A collapse re-maps the selection through the nodes: A0 and A1 disappear, B keeps its node and
    // moves up. The current node follows its own row.
    view.collapse(model.index_of(a));
    CHECK(view.visible_row_count() == 3); // A, B, C
    CHECK(view.current_index() == model.index_of(b));
    CHECK(view.current_row() == 1);
    CHECK(view.selected_rows() == (std::vector<int>{ 1 })); // A0 and A1 are gone
    // Re-expanding gives the nodes their rows back, and the selection follows B to row 3.
    view.expand(model.index_of(a));
    CHECK(view.current_index() == model.index_of(b));
    CHECK(view.current_row() == 3);
    CHECK(view.selected_rows() == (std::vector<int>{ 3 }));

    // Switching back to SINGLE collapses the selection to the current row.
    view.set_selection_mode(selection_mode_enum::SINGLE);
    CHECK(view.selected_rows() == (std::vector<int>{ 3 }));
    // current_changed() is a real-change signal: re-setting the same node announces nothing.
    const int before_current = current_signals;
    view.set_current_index(model.index_of(b));
    CHECK(current_signals == before_current);
    view.set_current_index(model.index_of(c));
    CHECK(current_signals == before_current + 1);
    CHECK(view.current_row() == 4);
    CHECK(view.selected_rows() == (std::vector<int>{ 4 }));
    // A node the view does not show is not a current row either (and does not clear one).
    view.set_current_index(model.index_of(b->children[0])); // B0, inside collapsed B
    CHECK(view.current_row() == 4);
    CHECK(view.current_index() == model.index_of(c));
}

// ---- (8) the model notifications -------------------------------------------------------------

void test_notifications() {
    std::printf("(8) model notifications: per-row damage, rebuilds, clamping and reset\n");
    // (8a) data_changed damages exactly its rows and re-measures exactly them.
    {
        probe_tree_model_t model;
        std::vector<node_t*> roots;
        for (int row = 0; row < 5; ++row) {
            roots.push_back(model.add_node(nullptr, "r" + std::to_string(row)));
        }
        tree_view_t view;
        view.set_geometry(rect_t{ 0, 0, 240, 400 });
        view.set_model(&model);
        CHECK(view.visible_row_count() == 5);
        CHECK(view.content_width() == 240); // the widest row is narrower: the rows fill the viewport

        clear_damages(view);
        model.forget_calls();
        model.notify_data(1, 1);
        CHECK(view.damage().bounds() == (rect_t{ 0, 30, 240, 30 }));
        CHECK(view.damage().area() == static_cast<std::size_t>(30 * 240));
        CHECK(model.data_calls == 1); // only the changed row is re-measured
        CHECK(model.data_out_of_range_calls == 0);

        clear_damages(view);
        model.notify_data(3, 4);
        CHECK(view.damage().bounds() == (rect_t{ 0, 90, 240, 60 }));
        CHECK(view.damage().area() == static_cast<std::size_t>(2 * 30 * 240));

        // A range outside the visible rows damages nothing at all.
        clear_damages(view);
        model.notify_data(10, 20);
        CHECK(view.damage().is_empty());
        clear_damages(view);
        model.notify_data(2, 1); // first > last: no row at all
        CHECK(view.damage().is_empty());
    }
    // (8b) A wider text moves the content width, the horizontal range and the whole destination.
    {
        probe_tree_model_t model;
        node_t* const first = model.add_node(nullptr, "r0");
        model.add_node(nullptr, "r1");
        tree_view_t view;
        view.set_geometry(rect_t{ 0, 0, 120, 400 });
        view.set_model(&model);
        CHECK(view.content_width() == 120);
        scroll_bar_t* const horizontal = bar_of(view, orientation_enum::HORIZONTAL);
        CHECK(horizontal != nullptr);
        CHECK(!horizontal->is_visible());

        model.set_name(first, std::string(40, 'L'));
        clear_damages(view);
        model.forget_calls();
        model.notify_data(0, 0);
        CHECK(model.data_calls == 1);
        // 8 (padding) + 12 (expander column) + 8 (padding) + 40 glyphs x 6 pixels + 8 (trailing padding)
        CHECK(view.content_width() == 8 + 12 + 8 + 40 * 6 + 8);
        CHECK(horizontal->is_visible());
        // The rows' geometry moved with the horizontal range: the destination is the unit of repaint.
        CHECK(covers(view.damage(), view.destination_rect()));
    }
    // (8c) rows_inserted / rows_removed rebuild the visible list and keep the current NODE.
    {
        probe_tree_model_t model;
        std::vector<node_t*> roots;
        for (int row = 0; row < 5; ++row) {
            roots.push_back(model.add_node(nullptr, "r" + std::to_string(row)));
        }
        node_t* const kid = model.add_node(roots[2], "kid");
        tree_view_t view;
        view.set_geometry(rect_t{ 0, 0, 240, 400 });
        view.set_model(&model);
        view.expand(model.index_of(roots[2]));
        view.set_current_index(model.index_of(roots[2]));
        CHECK(view.current_row() == 2);
        CHECK(view.visible_row_count() == 6); // five roots plus the expanded child

        clear_damages(view);
        model.insert_roots(0, 1);
        CHECK(view.visible_row_count() == 7);
        // The current NODE is r2: it moved one row down, and the current row followed it.
        CHECK(view.current_row() == 3);
        CHECK(view.current_index() == model.index_of(roots[2]));
        CHECK(view.selected_rows() == (std::vector<int>{ 3 }));
        CHECK(covers(view.damage(), view.destination_rect()));
        // The expansion is a fact about the NODE, so it survives the row moving: the child is still
        // shown, one row below its parent.
        CHECK(view.is_expanded(model.index_of(roots[2])));
        CHECK(view.index_at(point_t{ 5, 4 * 30 + 5 }) == model.index_of(kid));
        // The inserted row is addressable through the rebuilt list, and the row that used to hold r0
        // now holds the inserted node.
        CHECK(view.index_at(point_t{ 5, 5 }) == model.top_index(0));
        CHECK(view.index_at(point_t{ 5, 35 }) == model.index_of(roots[0]));

        model.remove_roots(0, 1);
        CHECK(view.visible_row_count() == 6);
        CHECK(view.current_row() == 2);
        CHECK(view.current_index() == model.index_of(roots[2]));
        CHECK(view.index_at(point_t{ 5, 3 * 30 + 5 }) == model.index_of(kid));
    }
    // (8d) Removing the current node clears the current row; the list is still rebuilt.
    {
        probe_tree_model_t model;
        std::vector<node_t*> roots;
        for (int row = 0; row < 5; ++row) {
            roots.push_back(model.add_node(nullptr, "r" + std::to_string(row)));
        }
        tree_view_t view;
        view.set_geometry(rect_t{ 0, 0, 240, 400 });
        view.set_model(&model);
        view.set_current_index(model.index_of(roots[1]));
        CHECK(view.current_row() == 1);
        clear_damages(view);
        model.remove_roots(1, 2);
        CHECK(view.visible_row_count() == 3);
        CHECK(view.current_row() == -1);
        CHECK(!view.current_index().is_valid());
        CHECK(view.selected_rows().empty());
        CHECK(covers(view.damage(), view.destination_rect()));
        // Row 1 now holds r3: the clamping never points at a node that is gone.
        CHECK(view.index_at(point_t{ 5, 35 }) == model.index_of(roots[3]));
    }
    // (8e) model_reset drops the expansion state, the current row and the selection.
    {
        probe_tree_model_t model;
        node_t* const root = model.add_node(nullptr, "root");
        model.add_node(root, "kid0");
        model.add_node(root, "kid1");
        model.add_node(nullptr, "sibling");
        tree_view_t view;
        view.set_geometry(rect_t{ 0, 0, 240, 400 });
        view.set_model(&model);
        const item_index_t root_index = model.index_of(root); // the node dies in the reset below
        view.expand(root_index);
        view.set_current_index(root_index);
        CHECK(view.visible_row_count() == 4);
        CHECK(view.is_expanded(root_index));
        clear_damages(view);
        model.reset_to_one_root();
        CHECK(view.visible_row_count() == 1);
        CHECK(!view.is_expanded(root_index)); // the node is gone; so is its state
        CHECK(view.current_row() == -1);
        CHECK(!view.current_index().is_valid());
        CHECK(view.selected_rows().empty());
        CHECK(covers(view.damage(), view.destination_rect()));
        CHECK(view.index_at(point_t{ 5, 5 }).is_valid()); // the new single root is row 0
    }
}

// ---- (9) the regression: the other views are untouched --------------------------------------

void test_regressions() {
    std::printf("(9) regression: a flat model in a tree, and the list/table answers unchanged\n");
    // A flat model needs NO override for the tree: the hierarchical defaults make it a one-level tree,
    // and its data(row, role) answers the tree's data(index, role).
    {
        probe_flat_model_t model{ 3 };
        tree_view_t view;
        view.set_geometry(rect_t{ 0, 0, 240, 400 });
        view.set_model(&model);
        CHECK(view.visible_row_count() == 3);
        CHECK(view.index_at(point_t{ 5, 5 }).node == nullptr);
        CHECK(view.index_at(point_t{ 5, 5 }).row == 0);
        CHECK(view.index_at(point_t{ 5, 35 }).row == 1);
        // No children anywhere: nothing expands, so no expander is painted and no row is a parent.
        const item_model_t& source = model;
        const item_index_t first = source.index(0, 0, item_index_t{});
        view.expand(first);
        CHECK(view.visible_row_count() == 3);
        CHECK(!view.is_expanded(first));

        image_t image{ extent_t{ 240, 400 } };
        paint_into(view, image);
        CHECK(count_color(image, rect_t{ 8, 9, 12, 12 }, expander_marker_color) == 0);
        CHECK(first_color_x(image, row_band(view, 1, 240), text_color) == text_left(view, 0));
        model.forget_calls();
        paint_into(view, image);
        CHECK(model.data_calls == 3); // the three visible rows, through the flat form
        CHECK(model.out_of_range_calls == 0);
        CHECK(model.asked_rows == (std::vector<int>{ 0, 1, 2 }));
    }
    // The list asks and paints exactly as it did before: same rows, same text column, no horizontal bar.
    {
        probe_flat_model_t model{ 100 };
        list_view_t view;
        view.set_geometry(rect_t{ 0, 0, 240, 160 });
        view.set_model(&model);
        CHECK(view.row_height() == 30);
        scroll_bar_t* const vertical = bar_of(view, orientation_enum::VERTICAL);
        scroll_bar_t* const horizontal = bar_of(view, orientation_enum::HORIZONTAL);
        CHECK(vertical != nullptr);
        CHECK(horizontal != nullptr);
        CHECK(vertical->is_visible());
        CHECK(!horizontal->is_visible()); // content_width() still follows the body
        CHECK(vertical->maximum() == 100 * 30 - 160);
        image_t image{ extent_t{ 240, 160 } };
        model.forget_calls();
        paint_into(view, image);
        CHECK(model.data_calls == 6);
        CHECK(model.out_of_range_calls == 0);
        CHECK(model.asked_rows == (std::vector<int>{ 0, 1, 2, 3, 4, 5 }));
        // A list row's text still starts one TEXT_PADDING from its left edge.
        CHECK(first_color_x(image, rect_t{ 0, 0, 226, 30 }, text_color) == 8);
        view.scroll_to_row(99);
        CHECK(view.scroll_offset().y() == 100 * 30 - 160);
    }
    // The table still asks rows x columns of the cells the destination shows.
    {
        probe_cell_model_t model{ 50, 3 };
        table_view_t view;
        view.set_geometry(rect_t{ 0, 0, 240, 180 });
        view.set_model(&model);
        CHECK(view.content_width() == 3 * 80);
        scroll_bar_t* const vertical = bar_of(view, orientation_enum::VERTICAL);
        CHECK(vertical != nullptr);
        CHECK(vertical->is_visible());
        image_t image{ extent_t{ 240, 180 } };
        model.forget_calls();
        paint_into(view, image);
        // 162 pixels of row area under the 18-pixel header, 30-pixel rows: six rows, three columns.
        CHECK(model.data_calls == 6 * 3);
        CHECK(model.out_of_range_calls == 0);
        std::vector<std::pair<int, int>> expected;
        for (int row = 0; row < 6; ++row) {
            for (int column = 0; column < 3; ++column) {
                expected.emplace_back(row, column);
            }
        }
        CHECK(model.asked_cells == expected);
    }
}

} // namespace tree_view_test

int main() {
    pin_builtin_font_family(); // deterministic text: the built-in face, not the machine's UI font
    using namespace tree_view_test;
    std::printf("== tree_view_test ==\n");
    test_model_defaults();
    test_flattening();
    test_expanded_signals();
    test_virtualisation();
    test_painting();
    test_hit_test();
    test_keyboard();
    test_selection();
    test_notifications();
    test_regressions();
    std::printf("checks: %d, failures: %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
