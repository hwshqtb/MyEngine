// W3/M4 demo: the retained half is INTERACTIVE, one control implementation serves two hosts, and the
// third column shows the control set itself.
//
//   ┌──────────────────────────┬───────────────────┬─────────────────────────────┐
//   │ retained host (W1/W2/W3) │ immediate (I1)    │ control showcase (M1-M4)    │
//   │                          │                   │                             │
//   │  panel A  vbox           │  [ OK ]           │  ┌ controls ───────────────┐│
//   │    child A1              │  [ Cancel ]       │  │ label_t                 ││
//   │      A1.1                │  [ Apply ]        │  │ check_box_t             ││
//   │  panel B  hbox           │  [ check_box_t ]  │  │ radio A / B / C         ││
//   │    B1        B2          │  [ slider_t ]     │  │ progress_bar_t  slider_t││
//   │  [ One ] [ Two ]  <- Tab │                   │  │ line_edit_t             ││
//   │  retained: - focus: -    │  cb=0 slider=0 v=30│  └─────────────────────────┘│
//   │                          │  clicked 0x last:- │  [tab 1][tab 2] pages      │
//   │                          │                   │  ┌ list_view_t ───────────┐│
//   │                          │                   │  │ row 0 ... row N        ││
//   │                          │                   │  └─────────────────────────┘│
//   │                          │                   │  ┌ table_view_t ──────────┐│
//   │                          │                   │  │ row  value  state  note ││
//   │                          │                   │  └─────────────────────────┘│
//   │                          │                   │  ┌ tree_view_t ───────────┐│
//   │                          │                   │  │ root N / child N.M     ││
//   │                          │                   │  └─────────────────────────┘│
//   │                          │                   │  ┌ scroll_view_t ─────────┐│
//   │                          │                   │  │ rows ...               ││
//   └──────────────────────────┴───────────────────┴─────────────────────────────┘
//
// Left: widget_window_t owns a root widget and an input_router_t. The router hit-tests the tree,
// bubbles, keeps the implicit grab, and moves the keyboard focus; clicking a panel highlights it and
// updates the readout, Tab moves the focus ring between the two retained push buttons. The routing
// itself lives in the plain input_router_t class (tests/ui-test/input_router_test.cpp drives it
// headless); widget_window_t only hands it the interaction step the window reported.
//
// Middle: a ui::im_host_t is an ITEM child of the root, laid out by the root's hbox_layout next to
// the retained column. Its frame handler drives push_button_t controls through
// control("push_button_t", name); the frame result is the button's `clicked` User property. It also
// drives a check_box_t and a slider_t with the value the CALLER owns (the external-value model: the
// demo keeps the value, feeds it every pass and only adopts the control's value when the User event
// says the user changed it). The host participates in the ordinary item traversal (root -> im_host ->
// its controls), and the router hit-tests it like any other item: a click over the immediate side
// never reaches the retained widgets, and a click on the retained side never reaches the host.
// --self-test proves exactly that.
//
// Right: the control showcase — a group box with one of every single-value control, a tab widget with
// two pages, a list view and a table view over ONE 100-row, four-column model (their rows are model
// data, not items), a tree view over a hierarchical model (three roots, each with three children, each
// with two leaves) and a scroll area whose content is taller than the viewport. All six are ordinary
// widgets of the retained tree, so the router drives them too; --self-test clicks, types, keys and
// wheels them through the same input path.
//
// Modes:
//   (no arguments)       persistent window; close it or press ESC to exit
//   --duration <ms>      close the window automatically after <ms> milliseconds
//   --screenshot <path>  render one deterministic frame (frame 0) to a BMP and exit
//   --wheel-burst <N>    prime the showcase scroll area, then push one mouse motion and N wheel
//                        events in ONE SDL batch, let the loop paint exactly one frame and capture
//                        that frame's pixels (with --screenshot, or build/burst_<N>.bmp)
//   --wheel-burst-point <centre|content>
//                        pointer position of the burst: the area centre (default), or the content's
//                        edge (no hover repaint can mask the frame)
//   --self-test          inject clicks, keys, text input and a wheel through the native event filter
//                        chain, then print and verify every counter (headless, exits 0/1)
//   --size <W>x<H>       start with this client size instead of the default 920x1280
//
// DIAGNOSTIC USE: --size and the two burst modes exist to reproduce a defect of the PRESENTED frame,
// which is why the burst captures go through window_t::save_painted_frame(). save_screenshot()
// repaints the whole frame before reading it, so it repairs the very stale pixels a reproduction is
// after; a reproduction must use the painted-frame capture, never the screenshot one.
//
// The first frame is static (no clock, no animation, no interaction), so the screenshot is
// deterministic: no panel is selected, nothing has focus and both readouts show "-".

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/gui_application_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/image_decoder_t.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/any_t.h"
#include "hwshqtb/metaobject/core_application.h"
#include "hwshqtb/metaobject/event_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/ui/box_layout_t.h"
#include "hwshqtb/ui/check_box_t.h"
#include "hwshqtb/ui/group_box_t.h"
#include "hwshqtb/ui/im_host_t.h"
#include "hwshqtb/ui/item_model_t.h"
#include "hwshqtb/ui/label_t.h"
#include "hwshqtb/ui/line_edit_t.h"
#include "hwshqtb/ui/list_view_t.h"
#include "hwshqtb/ui/progress_bar_t.h"
#include "hwshqtb/ui/push_button_t.h"
#include "hwshqtb/ui/radio_button_t.h"
#include "hwshqtb/ui/scroll_view_t.h"
#include "hwshqtb/ui/scroll_bar_t.h"
#include "hwshqtb/ui/slider_t.h"
#include "hwshqtb/ui/style_t.h"
#include "hwshqtb/ui/tab_widget_t.h"
#include "hwshqtb/ui/table_view_t.h"
#include "hwshqtb/ui/tree_view_t.h"
#include "hwshqtb/ui/widget_t.h"
#include "hwshqtb/gui/font_database_t.h"
#include "hwshqtb/ui/widget_window_t.h"

#if defined(_WIN32)
// Win32 is included AFTER the project headers on purpose: windows.h defines a CONST macro that would
// otherwise collide with qualifier_flag::CONST while basic_def.h is being parsed.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef CONST
#endif

using hwshqtb::gui::color_t;
using hwshqtb::gui::extent_t;
using hwshqtb::gui::image_t;
using hwshqtb::gui::load_image;
using hwshqtb::gui::font_metrics_t;
using hwshqtb::gui::gui_application_t;
using hwshqtb::gui::interaction_t;
using hwshqtb::gui::margins_t;
using hwshqtb::gui::mouse_button_flag;
using hwshqtb::gui::painter_t;
using hwshqtb::gui::point_t;
using hwshqtb::gui::rect_t;
using hwshqtb::metaobject::any_t;
using hwshqtb::metaobject::core_application;
using hwshqtb::metaobject::event_phase_t;
using hwshqtb::metaobject::index_t;
using hwshqtb::metaobject::meta_method_t;
using hwshqtb::metaobject::meta_object_t;
using hwshqtb::metaobject::object_t;
using hwshqtb::metaobject::timer_event_t;
using hwshqtb::ui::check_box_t;
using hwshqtb::ui::default_style;
using hwshqtb::ui::group_box_t;
using hwshqtb::ui::im_host_t;
using hwshqtb::ui::item_index_t;
using hwshqtb::ui::item_model_t;
using hwshqtb::ui::item_role_enum;
using hwshqtb::ui::label_t;
using hwshqtb::ui::text_alignment_enum;
using hwshqtb::ui::line_edit_t;
using hwshqtb::ui::list_view_t;
using hwshqtb::ui::focus_policy_enum;
using hwshqtb::ui::hbox_layout_t;
using hwshqtb::ui::paint_event_t;
using hwshqtb::ui::pixel_metric_enum;
using hwshqtb::ui::progress_bar_t;
using hwshqtb::ui::push_button_t;
using hwshqtb::ui::radio_button_t;
using hwshqtb::ui::scroll_view_t;
using hwshqtb::ui::scroll_bar_policy_enum;
using hwshqtb::ui::scroll_bar_t;
using hwshqtb::ui::size_policy_enum;
using hwshqtb::ui::slider_t;
using hwshqtb::ui::tab_widget_t;
using hwshqtb::ui::table_view_t;
using hwshqtb::ui::tree_view_t;
using hwshqtb::ui::vbox_layout_t;
using hwshqtb::ui::widget_t;
using hwshqtb::ui::widget_window_t;

namespace {
    /// @brief Palette of the demo (one place, so the screenshot is easy to reason about).
    constexpr color_t background_color{ 30, 42, 56 };   // dark blue-grey
    constexpr color_t header_color{ 46, 109, 164 };     // blue
    constexpr color_t panel_a_color{ 47, 122, 79 };     // green
    constexpr color_t child_a1_color{ 192, 80, 77 };    // red
    constexpr color_t grandchild_color{ 232, 179, 58 }; // yellow
    constexpr color_t panel_b_color{ 106, 76, 147 };    // purple
    constexpr color_t child_b1_color{ 75, 172, 198 };   // cyan
    constexpr color_t child_b2_color{ 221, 132, 82 };   // orange
    constexpr color_t border_color{ 220, 226, 232 };    // light grey
    constexpr color_t faint_text_color{ 150, 165, 180 };
    constexpr color_t im_panel_color{ 24, 33, 44 };     // darker: the immediate side reads as a layer
    constexpr color_t divider_color{ 70, 84, 98 };
    constexpr color_t focus_ring_color{ 255, 214, 10 }; // the Tab focus ring
    constexpr color_t selected_text_color{ 20, 24, 30 };

    /// @brief A copy of @p color with @p delta added to every channel (clamped).
    color_t lighten(color_t color, int delta) {
        const auto channel = [delta](std::uint8_t value) {
            const int sum = static_cast<int>(value) + delta;
            return static_cast<std::uint8_t>(sum < 0 ? 0 : (sum > 255 ? 255 : sum));
        };
        return color_t{ channel(color.red()), channel(color.green()), channel(color.blue()), color.alpha() };
    }

    /// @brief The inset of the immediate host's control group inside the host rectangle.
    constexpr point_t im_group_origin{ 8, 26 };

    /// @brief What the two hosts report: the readouts, and the proof counters of --self-test.
    struct demo_state_t {
        /// @brief Number of activated retained panels.
        int retained_clicks = 0;
        /// @brief Label of the last clicked retained panel ("-" before the first click).
        std::string last_retained = "-";
        /// @brief Number of activated immediate controls.
        int im_clicks = 0;
        /// @brief Label of the last activated immediate control.
        std::string last_im = "-";
        /// @brief The immediate check box's checked value; the CALLER owns it and feeds it back.
        bool im_check_checked = false;
        /// @brief How many times the immediate check box's User event fired.
        int im_check_toggles = 0;
        /// @brief The immediate slider's value; the CALLER owns it and feeds it back.
        int im_slider_value = 30;
        /// @brief How many times the immediate slider's User event fired.
        int im_slider_changes = 0;
        /// @brief How many times the showcase line edit's edited commit signal fired.
        int showcase_line_edits = 0;
        /// @brief How many times the showcase check box's toggled signal fired.
        int showcase_toggles = 0;
    };

    /// @brief Whether two readout snapshots report the same values.
    /// @param left One snapshot.
    /// @param right The other snapshot.
    /// @return true when every field a readout shows is equal.
    bool same_state(const demo_state_t& left, const demo_state_t& right) {
        return left.retained_clicks == right.retained_clicks
            && left.last_retained == right.last_retained && left.im_clicks == right.im_clicks
            && left.last_im == right.last_im
            && left.im_check_toggles == right.im_check_toggles
            && left.im_slider_value == right.im_slider_value
            && left.im_slider_changes == right.im_slider_changes;
    }

    /// @brief A filled panel with a one-pixel border and a label. In W3 it also CONSUMES clicks: a
    ///        press inside is grabbed, a release inside selects the panel (and repaints it brighter).
    class panel_t final: public widget_t {
    public:
        panel_t(color_t fill, color_t label_color, std::string label, color_t border, widget_t* parent):
            widget_t(parent),
            _fill(fill),
            _label_color(label_color),
            _label(std::move(label)),
            _border(border) { }

        /// @brief Share the demo state (the last selected label drives the highlight).
        void set_state(demo_state_t* state) noexcept {
            _state = state;
        }

        int clicks() const noexcept {
            return _clicks;
        }

        const std::string& label() const noexcept {
            return _label;
        }

        /// @brief Install the panel's content-derived natural size (a demo panel is a coloured block).
        void set_preferred_size(extent_t size) noexcept {
            _preferred = size;
            _has_preferred = true;
        }

        /// @brief The panel's own size opinion: the configured block size when one was given;
        ///        otherwise whatever its item children ask for (a panel with a layout is a container).
        extent_t size_hint() const override {
            if (_has_preferred) {
                return _preferred;
            }
            return widget_t::size_hint();
        }

        /// @brief Click handling: press inside -> grabbed; release inside -> selected + counted.
        bool interact(const interaction_t& in) override {
            if (!is_enabled()) {
                return false;
            }
            if (in.hovered != _hovered) {
                _hovered = in.hovered;
                update();
            }
            bool consumed = false;
            if (in.mouse_pressed && in.hovered) {
                _pressed = true;
                consumed = true;
                update();
            }
            if (_pressed) {
                consumed = true; // keep the grab while the button is held
            }
            if (in.mouse_released && _pressed) {
                _pressed = false;
                consumed = true;
                if (in.hovered) {
                    ++_clicks;
                    if (_state != nullptr) {
                        _state->last_retained = _label;
                        ++_state->retained_clicks;
                    }
                    std::printf("[ui_demo] retained panel '%s' clicked (%d)\n", _label.c_str(), _clicks);
                }
                update();
            }
            return consumed;
        }

    protected:
        void paint_event(paint_event_t* event) override {
            painter_t& painter = event->painter();
            const bool selected = _state != nullptr && _state->last_retained == _label;
            color_t fill = _fill;
            if (selected) {
                fill = lighten(_fill, 40);
            }
            else if (_pressed) {
                fill = lighten(_fill, -28);
            }
            else if (_hovered) {
                fill = lighten(_fill, 18);
            }
            painter.set_color(fill);
            painter.fill_rect(rect());
            painter.set_color(selected ? color_t::white_color() : _border);
            painter.draw_rect(rect());
            if (selected) {
                const rect_t inner{ 2, 2, rect().width() - 4, rect().height() - 4 };
                if (inner.width() > 2 && inner.height() > 2) {
                    painter.draw_rect(inner);
                }
            }
            painter.set_color(selected ? selected_text_color : _label_color);
            painter.draw_text(point_t{ 8, 17 }, _label);
        }

    private:
        color_t _fill;
        color_t _label_color;
        std::string _label;
        color_t _border;
        demo_state_t* _state = nullptr;
        bool _hovered = false;
        bool _pressed = false;
        /// @brief The configured natural size of this coloured block.
        extent_t _preferred{};
        /// @brief Whether _preferred carries an opinion (a container uses its layout's instead).
        bool _has_preferred = false;
        int _clicks = 0;
    };

    /// @brief A demo container/spacer with a configured NATURAL size. Unlike a fixed size this is
    ///        only an opinion: a vertical box still gives it the column's full cross width.
    class preferred_widget_t final: public widget_t {
    public:
        using widget_t::widget_t;

        void set_preferred_size(extent_t size) noexcept {
            _preferred = size;
            _has_preferred = true;
        }

        extent_t size_hint() const override {
            if (_has_preferred) {
                return _preferred;
            }
            return widget_t::size_hint();
        }

    private:
        extent_t _preferred{};
        bool _has_preferred = false;
    };

    /// @brief The demo's row source for the showcase list AND table: a fixed row count whose rows are
    ///        four cells ("row N", "value N", "on|off", "note N").
    ///
    /// @par One model, two views
    /// The list and the table read the SAME instance. The list asks only for column 0, so its content
    /// is exactly the "row N" text it showed when the model had a single column; the table asks for
    /// every column and draws the grid.
    ///
    /// @par Notifications
    /// The rows never change in this demo, so the model is a plain item_model_t: the views install
    /// themselves with set_model() and follow the inherited data_changed()/rows_inserted()/
    /// rows_removed()/model_reset() signals exactly as they would for a model that does change. A
    /// subclass only has to answer row_count(), column_count(), data() and header_data(); the whole
    /// notification set is already reflected on item_model_t.
    class demo_rows_model_t final: public item_model_t {
    public:
        /// @brief Build a model with @p rows rows (a value below 0 is treated as 0).
        explicit demo_rows_model_t(int rows):
            _rows(rows > 0 ? rows : 0) {
            _cells.reserve(static_cast<std::size_t>(_rows) * column_total);
            for (int row = 0; row < _rows; ++row) {
                const std::string number = std::to_string(row);
                _cells.push_back("row " + number);
                _cells.push_back("value " + number);
                _cells.push_back(row % 2 == 0 ? "on" : "off");
                _cells.push_back("note " + number);
            }
        }

        /// @brief How many rows this model holds.
        int row_count() const override {
            return _rows;
        }

        /// @brief How many columns every row has: the four titles "row", "value", "state", "note".
        int column_count() const override {
            return column_total;
        }

        /// @brief The DISPLAY text of @p row's column 0 ("row N"); every other role is empty.
        /// @param row The row, already in range (the view clamps before asking).
        /// @param role The kind of value asked for; only DISPLAY is served.
        /// @note This is the form the LIST reads (it asks for column 0 through the cell form, which
        ///       forwards here), so its rows keep the text they had before the model had columns.
        any_t data(int row, item_role_enum role) const override {
            if (role != item_role_enum::DISPLAY || row < 0 || row >= _rows) {
                return {};
            }
            return any_t(std::string_view{ _cells[static_cast<std::size_t>(row) * column_total] });
        }

        /// @brief The DISPLAY text of one CELL; every other role and every out-of-range column is
        ///        empty.
        /// @param row The row, already in range (the view clamps before asking).
        /// @param column The column, inside [0, column_count()) (the view asks only those).
        /// @param role The kind of value asked for; only DISPLAY is served.
        any_t data(int row, int column, item_role_enum role) const override {
            if (column == 0) {
                return data(row, role); // column 0 is the row form: one text, one source of truth
            }
            if (role != item_role_enum::DISPLAY || row < 0 || row >= _rows || column < 0
                || column >= column_total) {
                return {};
            }
            return any_t(std::string_view{
                _cells[static_cast<std::size_t>(row) * column_total + static_cast<std::size_t>(column)] });
        }

        /// @brief The DISPLAY title of @p column ("row", "value", "state", "note").
        /// @param column The column, inside [0, column_count()) (the view asks only those).
        /// @param role The kind of value asked for; only DISPLAY is served.
        any_t header_data(int column, item_role_enum role) const override {
            if (role != item_role_enum::DISPLAY || column < 0 || column >= column_total) {
                return {};
            }
            return any_t(std::string_view{ column_titles[static_cast<std::size_t>(column)] });
        }

    private:
        /// @brief How many columns every row has.
        static constexpr int column_total = 4;
        /// @brief The DISPLAY title of each column, indexed by column.
        static constexpr std::string_view column_titles[column_total]{ "row", "value", "state", "note" };

        /// @brief How many rows the model has.
        int _rows = 0;
        /// @brief The DISPLAY text of every cell, row-major: column_total entries per row.
        std::vector<std::string> _cells;
    };

    /// @brief The demo's hierarchical row source for the showcase tree: three roots, each with three
    ///        children, each child with two leaves (3 + 9 + 18 = 30 nodes).
    ///
    /// @par Identity is the node pointer
    /// Every index carries the address of its node_t, so a node is never addressed by its row: the
    /// (row, column) pair is unique only among ONE parent's children, while the pointer identifies the
    /// node across a rebuild, an expansion or a sibling that moved.
    ///
    /// @par The shape is fixed
    /// The tree is built once and never notified, so this model only answers the four hierarchical
    /// queries and data(): item_model_t already reflects the notifications a changing model would emit.
    class demo_tree_model_t final: public item_model_t {
    public:
        /// @brief One node: the text the tree shows and its place in the hierarchy.
        struct node_t {
            /// @brief The DISPLAY text: "root N", "child N.M" or "leaf N.M.K".
            std::string name;
            /// @brief The parent, nullptr for a top-level node.
            node_t* parent = nullptr;
            /// @brief The children, in row order.
            std::vector<node_t*> children;
        };

        /// @brief Build the fixed three-level tree.
        demo_tree_model_t() {
            for (int root_row = 0; root_row < root_total; ++root_row) {
                node_t* const root =
                    append(nullptr, "root " + std::to_string(root_row));
                for (int child_row = 0; child_row < child_total; ++child_row) {
                    node_t* const child = append(root, "child " + std::to_string(root_row) + "."
                                                          + std::to_string(child_row));
                    for (int leaf_row = 0; leaf_row < leaf_total; ++leaf_row) {
                        append(child, "leaf " + std::to_string(root_row) + "."
                                      + std::to_string(child_row) + "."
                                      + std::to_string(leaf_row));
                    }
                }
            }
        }

        ~demo_tree_model_t() override {
            for (node_t* const root : _roots) {
                delete_subtree(root);
            }
        }

        demo_tree_model_t(const demo_tree_model_t&) = delete;
        demo_tree_model_t& operator=(const demo_tree_model_t&) = delete;
        demo_tree_model_t(demo_tree_model_t&&) = delete;
        demo_tree_model_t& operator=(demo_tree_model_t&&) = delete;

        /// @brief How many top-level nodes the model has: the three roots.
        int row_count() const override {
            return static_cast<int>(_roots.size());
        }

        /// @brief How many children @p parent has; 0 for an index that names no node.
        int row_count(const item_index_t& parent) const override {
            const node_t* const node = node_of(parent);
            return node != nullptr ? static_cast<int>(node->children.size()) : 0;
        }

        /// @brief The index of child @p row of @p parent (column 0 only).
        /// @param row The row among the parent's children.
        /// @param column The column; this model has one, so any other column is invalid.
        /// @param parent The parent node; an invalid index asks for a top-level node.
        /// @return The node's index with its node_t pointer, or an invalid index out of range.
        item_index_t index(int row, int column, const item_index_t& parent) const override {
            if (row < 0 || column != 0) {
                return {};
            }
            const std::vector<node_t*>& siblings = siblings_of(parent);
            if (row >= static_cast<int>(siblings.size())) {
                return {};
            }
            return item_index_t{ row, column, siblings[static_cast<std::size_t>(row)] };
        }

        /// @brief The parent of @p index; an invalid index for a top-level node.
        item_index_t parent(const item_index_t& index) const override {
            const node_t* const node = node_of(index);
            return node != nullptr ? index_of(node->parent) : item_index_t{};
        }

        /// @brief The DISPLAY text of the node @p index names; every other role is empty.
        any_t data(const item_index_t& index, item_role_enum role) const override {
            const node_t* const node = node_of(index);
            if (node == nullptr || role != item_role_enum::DISPLAY) {
                return {};
            }
            return any_t(std::string_view{ node->name });
        }

        /// @brief The index that names @p node, or an invalid index when it is not in this tree.
        item_index_t index_of(const node_t* node) const {
            if (node == nullptr) {
                return {};
            }
            const std::vector<node_t*>& siblings =
                node->parent != nullptr ? node->parent->children : _roots;
            for (std::size_t row = 0; row < siblings.size(); ++row) {
                if (siblings[row] == node) {
                    return item_index_t{ static_cast<int>(row), 0, node };
                }
            }
            return {};
        }

        /// @brief The index of "root 0", the node the self-test clicks.
        item_index_t first_root_index() const {
            return index_of(_roots.empty() ? nullptr : _roots.front());
        }

        /// @brief The index of "child 0.0", the node the self-test reaches with Down.
        item_index_t first_child_index() const {
            const node_t* const root = _roots.empty() ? nullptr : _roots.front();
            return root != nullptr && !root->children.empty() ? index_of(root->children.front())
                                                              : item_index_t{};
        }

    private:
        /// @brief The node @p index names, or nullptr for an invalid index.
        static const node_t* node_of(const item_index_t& index) {
            return index.is_valid() ? static_cast<const node_t*>(index.node) : nullptr;
        }

        /// @brief Delete @p node and its whole subtree.
        static void delete_subtree(node_t* node) {
            for (node_t* const child : node->children) {
                delete_subtree(child);
            }
            delete node;
        }

        /// @brief Append a node called @p name under @p parent (nullptr makes a new top-level row).
        node_t* append(node_t* parent, std::string name) {
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

        /// @brief The children a node under @p parent has; the top level for an invalid parent.
        const std::vector<node_t*>& siblings_of(const item_index_t& parent) const {
            static const std::vector<node_t*> none;
            if (!parent.is_valid()) {
                return _roots;
            }
            const node_t* const node = node_of(parent);
            return node != nullptr ? node->children : none;
        }

        /// @brief How many roots the fixed tree has.
        static constexpr int root_total = 3;
        /// @brief How many children every root has.
        static constexpr int child_total = 3;
        /// @brief How many leaves every child has.
        static constexpr int leaf_total = 2;

        /// @brief The top-level nodes, in row order; the model owns every node of the tree.
        std::vector<node_t*> _roots;
    };

    /// @brief A push button with a focus ring: the SAME push_button_t state machine, one paint override.
    ///        Tab moves the focus between two of them (their policy is strong_focus).
    class focus_ring_button_t final: public push_button_t {
    public:
        explicit focus_ring_button_t(widget_t* parent):
            push_button_t(parent) {
            set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        }

        int activations() const noexcept {
            return _activations;
        }

        bool interact(const interaction_t& in) override {
            const bool was_clicked = clicked();
            const bool consumed = push_button_t::interact(in);
            if (!was_clicked && clicked()) {
                ++_activations;
            }
            return consumed;
        }

    protected:
        void paint_event(paint_event_t* event) override {
            painter_t& painter = event->painter();
            push_button_t::paint_event(event);
            if (!has_focus()) {
                return;
            }
            painter.set_color(focus_ring_color);
            painter.draw_rect(rect());
            const rect_t inner{ 2, 2, rect().width() - 4, rect().height() - 4 };
            if (inner.width() > 2 && inner.height() > 2) {
                painter.draw_rect(inner);
            }
        }

    private:
        int _activations = 0;
    };

    /// @brief The normalised signature of the signal @p name of @p object, or an empty string.
    /// @param object The object whose meta-object is walked.
    /// @param name The bare signal name ("expanded_changed").
    /// @return The signature connect() expects; the raw C++ spelling is not normalised by the caller.
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

    /// @brief The root widget: header, the divider between the two hosts, the retained layout, the
    ///        IMMEDIATE host item (driven from its frame handler) and the two readouts.
    class demo_root_t final: public widget_t {
    public:
        demo_root_t() = default;

        /// @brief The immediate host, an item child of this root (created by build_tree).
        im_host_t& im() noexcept {
            return *_im;
        }

        demo_state_t& state() noexcept {
            return _state;
        }

        int retained_clicks() const noexcept {
            return _state.retained_clicks;
        }

        int im_clicks() const noexcept {
            return _state.im_clicks;
        }

        /// @brief The immediate host item (given by build_tree, owned by the object tree).
        void set_im_host(im_host_t* host) noexcept {
            _im = host;
        }

        void set_panels(panel_t* panel_a, panel_t* panel_b) noexcept {
            _panel_a = panel_a;
            _panel_b = panel_b;
        }

        /// @brief The retained column (the left half of the root's hbox), for the divider position.
        void set_left_widget(widget_t* left) noexcept {
            _left = left;
        }

        /// @brief The Tab-row container, so the self-test can prove it follows the column width.
        void set_button_row(widget_t* row) noexcept {
            _button_row = row;
        }

        /// @brief Whether the Tab row takes the whole column width (a preferred size, not fixed).
        bool button_row_fills_column() const noexcept {
            return _button_row != nullptr && _left != nullptr
                && _button_row->geometry().width() == _left->geometry().width();
        }

        panel_t* panel_a() const noexcept {
            return _panel_a;
        }

        panel_t* panel_b() const noexcept {
            return _panel_b;
        }

        void set_focus_buttons(widget_t* first, widget_t* second) noexcept {
            _focus_first = first;
            _focus_second = second;
        }

        widget_t* first_focus_button() const noexcept {
            return _focus_first;
        }

        widget_t* second_focus_button() const noexcept {
            return _focus_second;
        }

        void set_status_widget(widget_t* status) noexcept {
            _status = status;
        }

        /// @brief How many times this root's paint_event() ran: one pass per damaged rectangle, so a
        ///        frame with several damage fragments counts several times.
        /// @note NOT a frame count. Use window_t::painted_frame_count() to count painted frames.
        int painted_frames() const noexcept {
            return _frames;
        }

        /// @name Control showcase (built by build_showcase)
        /// @{
        void set_showcase_column(widget_t* column) noexcept {
            _showcase = column;
        }

        /// @brief Remember the CJK column, when the run built one (interactive / system-font runs).
        void set_cjk_column(widget_t* column) noexcept {
            _cjk_column = column;
        }

        /// @brief The CJK column, or nullptr when this run has none (the hash-gated captures).
        /// @note The --screenshot-font capture prints this widget's global rectangle, so the caller can
        ///       crop exactly that region out of the BMP and count its ink.
        widget_t* cjk_column() const noexcept {
            return _cjk_column;
        }

        void set_showcase_box(group_box_t* box) noexcept {
            _showcase_box = box;
        }

        void set_showcase_check(check_box_t* check) noexcept {
            _showcase_check = check;
        }

        void set_showcase_radios(radio_button_t* first, radio_button_t* second,
                                 radio_button_t* third) noexcept {
            _showcase_radio_first = first;
            _showcase_radio_second = second;
            _showcase_radio_third = third;
        }

        void set_showcase_slider(slider_t* slider) noexcept {
            _showcase_slider = slider;
        }

        void set_showcase_line_edit(line_edit_t* line) noexcept {
            _showcase_line_edit = line;
        }

        void set_showcase_tabs(tab_widget_t* tabs, widget_t* first_page, widget_t* second_page) noexcept {
            _showcase_tabs = tabs;
            _showcase_first_page = first_page;
            _showcase_second_page = second_page;
        }

        void set_showcase_scroll(scroll_view_t* scroll) noexcept {
            _showcase_scroll = scroll;
        }

        /// @brief Install the image showcase label (a picture shown by label_t::set_image()).
        void set_showcase_image(label_t* image) noexcept {
            _showcase_image = image;
        }

        /// @brief The band the root paints the list frame and the "list_view_t" caption into.
        void set_showcase_list_band(widget_t* band) noexcept {
            _showcase_list_band = band;
        }

        /// @brief The showcase list (the self-test clicks its rows and keys it).
        void set_showcase_list(list_view_t* list) noexcept {
            _showcase_list = list;
        }

        /// @brief The band the root paints the table frame and the "table_view_t" caption into.
        void set_showcase_table_band(widget_t* band) noexcept {
            _showcase_table_band = band;
        }

        /// @brief The showcase table: the SAME model as the list, four columns (the self-test clicks a
        ///        cell and keys the view).
        void set_showcase_table(table_view_t* table) noexcept {
            _showcase_table = table;
        }

        /// @brief The band the root paints the tree frame and the "tree_view_t" caption into.
        void set_showcase_tree_band(widget_t* band) noexcept {
            _showcase_tree_band = band;
        }

        /// @brief The showcase tree over the demo's 30-node hierarchical model.
        void set_showcase_tree(tree_view_t* tree) noexcept {
            _showcase_tree = tree;
        }

        /// @brief The hierarchical model the showcase tree shows (the self-test compares node pointers).
        void set_showcase_tree_model(demo_tree_model_t* model) noexcept {
            _showcase_tree_model = model;
        }

        check_box_t* showcase_check() const noexcept {
            return _showcase_check;
        }

        radio_button_t* showcase_radio_first() const noexcept {
            return _showcase_radio_first;
        }

        radio_button_t* showcase_radio_second() const noexcept {
            return _showcase_radio_second;
        }

        slider_t* showcase_slider() const noexcept {
            return _showcase_slider;
        }

        line_edit_t* showcase_line_edit() const noexcept {
            return _showcase_line_edit;
        }

        tab_widget_t* showcase_tabs() const noexcept {
            return _showcase_tabs;
        }

        widget_t* showcase_first_page() const noexcept {
            return _showcase_first_page;
        }

        widget_t* showcase_second_page() const noexcept {
            return _showcase_second_page;
        }

        scroll_view_t* showcase_scroll() const noexcept {
            return _showcase_scroll;
        }

        widget_t* showcase_list_band() const noexcept {
            return _showcase_list_band;
        }

        list_view_t* showcase_list() const noexcept {
            return _showcase_list;
        }

        widget_t* showcase_table_band() const noexcept {
            return _showcase_table_band;
        }

        table_view_t* showcase_table() const noexcept {
            return _showcase_table;
        }

        widget_t* showcase_tree_band() const noexcept {
            return _showcase_tree_band;
        }

        tree_view_t* showcase_tree() const noexcept {
            return _showcase_tree;
        }

        demo_tree_model_t* showcase_tree_model() const noexcept {
            return _showcase_tree_model;
        }

        /// @name Tree expansion record (the self-test's proof of the expanded_changed signal)
        /// @{
        /// @brief Remember one expanded_changed(node, expanded) emission.
        /// @param expanded true when the node was expanded, false when it was collapsed.
        void note_expanded(bool expanded) noexcept {
            ++_tree_expansions;
            _tree_last_expanded = expanded;
        }

        /// @brief How many expanded_changed signals this demo's tree emitted.
        int tree_expansions() const noexcept {
            return _tree_expansions;
        }

        /// @brief The expanded flag of the last expanded_changed signal.
        bool tree_last_expanded() const noexcept {
            return _tree_last_expanded;
        }
        /// @}

        /// @name Table activation record (the self-test's proof of the cell_activated signal)
        /// @{
        /// @brief Remember one cell_activated(row, column) emission.
        /// @param row The row the signal carried.
        /// @param column The column the signal carried.
        void note_cell_activated(int row, int column) noexcept {
            ++_cell_activations;
            _cell_activated_row = row;
            _cell_activated_column = column;
        }

        /// @brief How many cell_activated signals this demo's table emitted.
        int cell_activations() const noexcept {
            return _cell_activations;
        }

        /// @brief The row of the last cell_activated signal, or -1 before the first one.
        int cell_activated_row() const noexcept {
            return _cell_activated_row;
        }

        /// @brief The column of the last cell_activated signal, or -1 before the first one.
        int cell_activated_column() const noexcept {
            return _cell_activated_column;
        }
        /// @}
        /// @}

        /// @brief The host's frame handler: stack the controls in an inset group and fold their
        ///        results in.
        void drive_frame(im_host_t& host) {
            // Leave the panel title (top) and the readout (bottom) to the root. The group gives the
            // rows the 8-pixel horizontal inset and the 26-pixel top offset the old panel had, and
            // makes the names group-local ("im/ok", ...).
            const rect_t area{ im_group_origin.x(), im_group_origin.y(),
                               host.rect().width() - 16, host.rect().height() - 34 };
            host.begin_group(area, "im");
            drive_button(host, "ok", "OK", _im_ok);
            drive_button(host, "cancel", "Cancel", _im_cancel);
            drive_button(host, "apply", "Apply", _im_apply);
            drive_check(host);
            drive_slider(host);
            host.end_group();
        }

    protected:
        void paint_event(paint_event_t* event) override {
            painter_t& painter = event->painter();
            const extent_t extent = size();
            const font_metrics_t metrics{};

            painter.set_color(background_color);
            painter.fill_rect(rect());
            painter.set_color(header_color);
            // The header band and the title baseline follow the FONT, not a literal: the band must hold
            // one line of the resolved face (a taller face otherwise clips the title). Both expressions
            // equal the old 26 / 18 for the built-in 8 px face, so its pixels are unchanged.
            painter.fill_rect(rect_t{ 0, 0, extent.width(), metrics.line_height() + 18 });
            painter.set_color(color_t::white_color());
            painter.draw_text(point_t{ 8, metrics.line_height() + 10 },
                              "ui_demo - retained + immediate hosts, controls");

            // The divider and the immediate side's background: the split follows the ACTUAL layout
            // geometry (the columns are sized by their natural hints and stretch weights), so the
            // divider sits on the retained column's right edge, not on the window's midpoint.
            const int divider_x = _left != nullptr ? _left->geometry().right() : extent.width() / 2;
            painter.set_color(divider_color);
            painter.fill_rect(rect_t{ divider_x, 30, 1, extent.height() - 36 });

            // The immediate side's frame: the host is an item CHILD of this root, so the tree paints
            // it after this paint_event returns. Everything drawn here is therefore the previous
            // pass's readout — the accepted cost of coupling consumption and paint in one pass.
            const rect_t im_area = _im->geometry();
            painter.set_color(im_panel_color);
            painter.fill_rect(im_area);
            painter.set_color(border_color);
            painter.draw_rect(im_area);

            // The host's rectangle is the boundary of everything the root draws for the immediate
            // side: the title, the focus rings and the two readout rows. The host paints only its
            // controls, so its own paint() clip does not cover these decorations — this pass has to
            // carry the same boundary itself, or a narrow middle column lets the readouts run over
            // the neighbouring controls column.
            painter.save();
            painter.set_clip(im_area);
            painter.set_color(faint_text_color);
            painter.draw_text(point_t{ im_area.x() + 8, im_area.y() + metrics.ascent() + 10 },
                              "im_host_t");

            // Focus feedback for the IM side: the controls' item-level has_focus() is mirrored by the
            // host, and a ring drawn just OUTSIDE a control's rectangle survives the host's own paint
            // (the root paints the panel before its item children).
            const widget_t* const im_buttons[] = { _im_ok, _im_cancel, _im_apply };
            for (const widget_t* const button : im_buttons) {
                if (button == nullptr || !button->has_focus()) {
                    continue;
                }
                const rect_t row = button->geometry(); // group-local
                painter.set_color(focus_ring_color);
                painter.draw_rect(rect_t{ im_area.x() + im_group_origin.x() + row.x() - 2,
                                          im_area.y() + im_group_origin.y() + row.y() - 2,
                                          row.width() + 4, row.height() + 4 });
            }

            // ---- the immediate readout (previous pass; the host itself is painted below) --------
            const int line_y = im_area.bottom() - 30;
            painter.set_color(color_t::white_color());
            const std::string clicked = "clicked " + std::to_string(_state.im_clicks) + "x";
            painter.draw_text(point_t{ im_area.x() + 10, line_y }, clicked);
            painter.set_color(faint_text_color);
            painter.draw_text(
                point_t{ im_area.x() + 10 + metrics.horizontal_advance(clicked) + 12,
                         line_y },
                "last: " + _state.last_im);

            // The immediate host's check box and slider: their User events folded into the readout
            // (the caller owns both values and feeds them back every pass).
            painter.set_color(faint_text_color);
            painter.draw_text(point_t{ im_area.x() + 10, line_y - (metrics.line_height() + 6) },
                              "cb=" + std::to_string(_state.im_check_toggles) + " slider="
                                  + std::to_string(_state.im_slider_changes) + " v="
                                  + std::to_string(_state.im_slider_value));
            painter.restore();

            // ---- the list showcase: the frame and the caption are the root's drawing ------------
            // list_view_t paints every pixel of its own rectangle (the viewport background covers the
            // rows), so a caption drawn inside that rectangle would be overpainted. The band reserves
            // a caption row above the list instead, and the whole band is the root's drawing area:
            // the clip keeps the caption from ever running into the neighbouring columns, exactly
            // like the im_host_t frame above.
            if (_showcase_list_band != nullptr) {
                const point_t band_origin = _showcase_list_band->map_to_global(point_t{});
                const rect_t list_area{ band_origin.x(), band_origin.y(),
                                        _showcase_list_band->size().width(),
                                        _showcase_list_band->size().height() };
                painter.save();
                painter.set_clip(list_area);
                painter.set_color(im_panel_color);
                painter.fill_rect(list_area);
                painter.set_color(border_color);
                painter.draw_rect(list_area);
                painter.set_color(faint_text_color);
                painter.draw_text(point_t{ list_area.x() + 8, list_area.y() + metrics.ascent() + 6 },
                                  "list_view_t");
                painter.restore();
            }

            // ---- the table showcase: the same frame + caption treatment as the list band ----------
            if (_showcase_table_band != nullptr) {
                const point_t band_origin = _showcase_table_band->map_to_global(point_t{});
                const rect_t table_area{ band_origin.x(), band_origin.y(),
                                         _showcase_table_band->size().width(),
                                         _showcase_table_band->size().height() };
                painter.save();
                painter.set_clip(table_area);
                painter.set_color(im_panel_color);
                painter.fill_rect(table_area);
                painter.set_color(border_color);
                painter.draw_rect(table_area);
                painter.set_color(faint_text_color);
                painter.draw_text(point_t{ table_area.x() + 8, table_area.y() + metrics.ascent() + 6 },
                                  "table_view_t");
                painter.restore();
            }

            // ---- the tree showcase: the same frame + caption treatment as the list band -----------
            if (_showcase_tree_band != nullptr) {
                const point_t band_origin = _showcase_tree_band->map_to_global(point_t{});
                const rect_t tree_area{ band_origin.x(), band_origin.y(),
                                        _showcase_tree_band->size().width(),
                                        _showcase_tree_band->size().height() };
                painter.save();
                painter.set_clip(tree_area);
                painter.set_color(im_panel_color);
                painter.fill_rect(tree_area);
                painter.set_color(border_color);
                painter.draw_rect(tree_area);
                painter.set_color(faint_text_color);
                painter.draw_text(point_t{ tree_area.x() + 8, tree_area.y() + metrics.ascent() + 6 },
                                  "tree_view_t");
                painter.restore();
            }

            // ---- the retained readout (the two status lines reserved by build_tree) --------------
            if (_status != nullptr) {
                const point_t origin = _status->map_to_global(point_t{});
                // Two readout lines stacked by the FONT's line height: a 12 px step collides as soon as
                // the face is taller than the built-in 8 px one. Both expressions equal the old 11 / 23
                // for that face, so its pixels are unchanged.
                painter.set_color(faint_text_color);
                painter.draw_text(point_t{ origin.x(), origin.y() + metrics.ascent() + 5 },
                                  "retained: " + _state.last_retained + " ("
                                      + std::to_string(_state.retained_clicks) + "x)");
                std::string focus = "-";
                if (_focus_first != nullptr && _focus_first->has_focus()) {
                    focus = "One";
                }
                else if (_focus_second != nullptr && _focus_second->has_focus()) {
                    focus = "Two";
                }
                painter.set_color(color_t::white_color());
                painter.draw_text(
                    point_t{ origin.x(),
                             origin.y() + metrics.ascent() + metrics.line_height() + 9 },
                    "focus: " + focus);
            }

            painter.set_color(faint_text_color);
            painter.draw_text(point_t{ 10, extent.height() - 8 },
                              "retained router (left) + im_host_t (middle) + controls (right)");
            const std::string geometry =
                std::to_string(extent.width()) + "x" + std::to_string(extent.height());
            // Right-aligned by the MEASURED width, not by char count * one advance: a proportional face
            // gives every character its own advance. Equal to the old count * advance for the built-in
            // face, whose advance is constant.
            const int text_width = metrics.horizontal_advance(geometry);
            painter.draw_text(point_t{ extent.width() - 10 - text_width, extent.height() - 8 }, geometry);

            ++_frames;
            std::printf("[ui_demo] paint #%d %dx%d, retained clicks %d, immediate clicks %d\n", _frames - 1,
                        extent.width(), extent.height(), _state.retained_clicks, _state.im_clicks);

            // The readouts above are drawn by THIS widget but report state another widget changed. The
            // pass that repainted that widget does not cover the readout row, so the values would stay
            // stale in a partial repaint; one repaint of the whole root per real change keeps them
            // current. Comparing the snapshot converges: the next pass sees the same values.
            if (!same_state(_painted_state, _state)) {
                _painted_state = _state;
                update();
            }
        }

    private:
        /// @brief Drive one immediate control and fold its User property into the readout.
        /// @param slot Receives the control pointer (the root paints its focus ring).
        void drive_button(im_host_t& host, const char* object_name, const char* label,
                          push_button_t*& slot) {
            // Create/find the control first so the label sizes its hint before the first placement;
            // set_text() is a no-op once the label matches.
            if (auto* const button = static_cast<push_button_t*>(
                    host.widget("push_button_t", object_name))) {
                button->set_text(label);
                button->set_focus_policy(focus_policy_enum::TAB_FOCUS); // opt into the host's Tab chain
                slot = button;
            }
            const any_t value = host.control("push_button_t", object_name);
            if (!value.is_initialized()) {
                return;
            }
            if (value.get<bool>()) {
                ++_state.im_clicks;
                _state.last_im = label;
                std::printf("[ui_demo] immediate control '%s' activated (total %d)\n", object_name,
                            _state.im_clicks);
            }
        }

        /// @brief Drive the immediate check box with the CALLER-owned value (the external model).
        /// @note The demo keeps the value in demo_state_t, feeds it in every pass and only flips it
        ///       when the control's User event says the USER toggled it.
        void drive_check(im_host_t& host) {
            if (auto* const box = static_cast<check_box_t*>(host.widget("check_box_t", "check"))) {
                box->set_text("check_box_t");
                box->set_checked(_state.im_check_checked); // the caller's value, every pass
            }
            const any_t value = host.control("check_box_t", "check");
            if (value.is_initialized() && value.get<bool>()) {
                _state.im_check_checked = !_state.im_check_checked; // the USER toggled it
                ++_state.im_check_toggles;
                std::printf("[ui_demo] immediate check_box_t toggled (total %d)\n", _state.im_check_toggles);
            }
        }

        /// @brief Drive the immediate slider with the CALLER-owned value (the external model).
        void drive_slider(im_host_t& host) {
            if (auto* const slider = static_cast<slider_t*>(host.widget("slider_t", "slide"))) {
                slider->set_range(0, 100);
                slider->set_value(_state.im_slider_value); // the caller's value, every pass
            }
            const any_t value = host.control("slider_t", "slide");
            if (value.is_initialized() && value.get<bool>()) {
                if (auto* const slider = static_cast<slider_t*>(host.widget("slider_t", "slide"))) {
                    _state.im_slider_value = slider->value(); // adopt the USER's value
                }
                ++_state.im_slider_changes;
                std::printf("[ui_demo] immediate slider_t changed (value %d, total %d)\n",
                            _state.im_slider_value, _state.im_slider_changes);
            }
        }

        /// @brief The immediate host: an item child of this root, owned by the object tree.
        im_host_t* _im = nullptr;
        /// @brief The immediate controls (the root paints their focus rings).
        push_button_t* _im_ok = nullptr;
        push_button_t* _im_cancel = nullptr;
        push_button_t* _im_apply = nullptr;
        /// @brief The retained column, used to place the divider from the ACTUAL geometry.
        widget_t* _left = nullptr;
        /// @brief The Tab-row container (the self-test checks its width follows the column).
        widget_t* _button_row = nullptr;
        /// @brief The two clickable retained panels (the self-test points at one of them).
        panel_t* _panel_a = nullptr;
        panel_t* _panel_b = nullptr;
        /// @brief The two retained buttons of the Tab chain.
        widget_t* _focus_first = nullptr;
        widget_t* _focus_second = nullptr;
        /// @brief The reserved status row the retained readout is drawn into.
        widget_t* _status = nullptr;
        /// @brief The control showcase column and its controls.
        widget_t* _showcase = nullptr;
        /// @brief The CJK column, only on runs that use the platform font (nullptr otherwise).
        widget_t* _cjk_column = nullptr;
        group_box_t* _showcase_box = nullptr;
        check_box_t* _showcase_check = nullptr;
        radio_button_t* _showcase_radio_first = nullptr;
        radio_button_t* _showcase_radio_second = nullptr;
        radio_button_t* _showcase_radio_third = nullptr;
        slider_t* _showcase_slider = nullptr;
        line_edit_t* _showcase_line_edit = nullptr;
        tab_widget_t* _showcase_tabs = nullptr;
        widget_t* _showcase_first_page = nullptr;
        widget_t* _showcase_second_page = nullptr;
        scroll_view_t* _showcase_scroll = nullptr;
        /// @brief The image showcase label (a picture shown by label_t::set_image()).
        label_t* _showcase_image = nullptr;
        /// @brief The band the root paints the list frame and caption into (the list's parent).
        widget_t* _showcase_list_band = nullptr;
        /// @brief The showcase list over the demo's 100-row model.
        list_view_t* _showcase_list = nullptr;
        /// @brief The band the root paints the table frame and caption into (the table's parent).
        widget_t* _showcase_table_band = nullptr;
        /// @brief The showcase table over the SAME model instance the list shows.
        table_view_t* _showcase_table = nullptr;
        /// @brief The band the root paints the tree frame and caption into (the tree's parent).
        widget_t* _showcase_tree_band = nullptr;
        /// @brief The showcase tree over the demo's hierarchical model.
        tree_view_t* _showcase_tree = nullptr;
        /// @brief The hierarchical model the showcase tree shows: borrowed by the view, owned by the
        ///        root's object tree (the self-test reads its node identity).
        demo_tree_model_t* _showcase_tree_model = nullptr;
        /// @brief How many expanded_changed signals the showcase tree emitted.
        int _tree_expansions = 0;
        /// @brief The expanded flag of the last expanded_changed signal.
        bool _tree_last_expanded = false;
        /// @brief How many cell_activated signals the showcase table emitted.
        int _cell_activations = 0;
        /// @brief The row of the last cell_activated signal (-1 before the first one).
        int _cell_activated_row = -1;
        /// @brief The column of the last cell_activated signal (-1 before the first one).
        int _cell_activated_column = -1;
        /// @brief The shared readout/self-test counters.
        demo_state_t _state;
        /// @brief The state the last pass drew into the readouts (drives the freshness repaint).
        demo_state_t _painted_state;
        /// @brief paint_event() passes this root has drawn (one per damaged rectangle, not frames).
        int _frames = 0;
    };

    /// @brief Build the control-showcase column: a group box, a tab widget, a list view, a table view,
    ///        a tree view and a scroll area.
    /// @param root The demo root that keeps the pointers the readout/self-test need.
    void build_showcase(demo_root_t& root) {
        auto* const column = new widget_t{ &root };
        column->set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::EXPANDING);
        column->set_stretch(1);
        root.set_showcase_column(column);

        // ---- group box: one of every single-value control --------------------------------------
        auto* const box = new group_box_t{ column };
        box->set_title("controls");
        box->set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::PREFERRED);
        root.set_showcase_box(box);

        auto* const label = new label_t{ box };
        label->set_text("label_t");

        auto* const check = new check_box_t{ box };
        check->set_text("check_box_t");
        check->connect("toggled(bool) const", [&root]() { ++root.state().showcase_toggles; });
        root.set_showcase_check(check);

        auto* const radio_first = new radio_button_t{ box };
        radio_first->set_text("radio A");
        auto* const radio_second = new radio_button_t{ box };
        radio_second->set_text("radio B");
        auto* const radio_third = new radio_button_t{ box };
        radio_third->set_text("radio C");
        radio_first->set_checked(true); // the first one starts selected (auto-exclusive)
        root.set_showcase_radios(radio_first, radio_second, radio_third);

        auto* const progress = new progress_bar_t{ box };
        progress->set_range(0, 100);
        progress->set_value(40);

        auto* const slider = new slider_t{ box };
        slider->set_range(0, 100);
        slider->set_value(30);
        root.set_showcase_slider(slider);

        auto* const line = new line_edit_t{ box };
        line->set_text("line edit");
        line->connect(line->meta_object()->normalized_signature("edited(std::string_view) const"),
                      [&root]() { ++root.state().showcase_line_edits; });
        root.set_showcase_line_edit(line);

        auto* const box_layout = new vbox_layout_t;
        box_layout->set_margins(margins_t{ 8, 6, 8, 8 });
        box_layout->set_spacing(6);
        box->set_layout(box_layout);

        // ---- tab widget: two pages -----------------------------------------------------------------
        auto* const tabs = new tab_widget_t{ column };
        tabs->set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::PREFERRED);

        auto* const page_one = new widget_t;
        auto* const apply = new push_button_t{ page_one };
        apply->set_text("apply");
        auto* const reset = new push_button_t{ page_one };
        reset->set_text("reset");
        auto* const page_one_label = new label_t{ page_one };
        page_one_label->set_text("page 1");
        auto* const page_one_layout = new vbox_layout_t;
        page_one_layout->set_margins(margins_t{ 8, 8, 8, 8 });
        page_one_layout->set_spacing(6);
        page_one->set_layout(page_one_layout);

        auto* const page_two = new widget_t;
        auto* const enabled = new check_box_t{ page_two };
        enabled->set_text("enabled");
        auto* const page_two_slider = new slider_t{ page_two };
        page_two_slider->set_range(0, 100);
        page_two_slider->set_value(60);
        auto* const page_two_label = new label_t{ page_two };
        page_two_label->set_text("page 2");
        auto* const page_two_layout = new vbox_layout_t;
        page_two_layout->set_margins(margins_t{ 8, 8, 8, 8 });
        page_two_layout->set_spacing(6);
        page_two->set_layout(page_two_layout);

        tabs->add_tab(page_one, "tab 1");
        tabs->add_tab(page_two, "tab 2");
        root.set_showcase_tabs(tabs, page_one, page_two);

        // ---- list view: a virtualised row list over a 100-row model -------------------------------
        // The band is the root's drawing area: it reserves the caption row the root paints and gives
        // the list the rest. The band does not paint; only the root does.
        auto* const list_band = new widget_t{ column };
        list_band->set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::EXPANDING);
        list_band->set_stretch(1);
        root.set_showcase_list_band(list_band);

        auto* const list_caption = new preferred_widget_t{ list_band };
        // The caption band is the style's title height, NOT a literal: that metric is font-derived, so
        // a taller face needs a taller band or the caption is clipped by the view below it.
        list_caption->set_preferred_size(
            extent_t{ 100, default_style().pixel_metric(pixel_metric_enum::TITLE_HEIGHT) });
        list_caption->set_size_policy(size_policy_enum::FIXED, size_policy_enum::FIXED);

        // The model is a plain object child of the root: the tree owns it and the view only borrows
        // the pointer (set_model() connects to the model's reflected notifications).
        auto* const model = new demo_rows_model_t{ 100 };
        model->set_parent(&root);

        auto* const list = new list_view_t{ list_band };
        list->set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::EXPANDING);
        list->set_stretch(1);
        list->set_model(model);
        root.set_showcase_list(list);

        auto* const list_band_layout = new vbox_layout_t;
        list_band_layout->set_margins(margins_t{ 6, 4, 6, 4 });
        list_band_layout->set_spacing(0);
        list_band->set_layout(list_band_layout);

        // ---- table view: the SAME model instance, four columns ------------------------------------
        // One model serving two views: the list above asks it for column 0 only, the table asks for
        // every column. The band is the root's drawing area, exactly like the list band: it reserves
        // the caption row the root paints and gives the table the rest.
        auto* const table_band = new widget_t{ column };
        table_band->set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::EXPANDING);
        table_band->set_stretch(1);
        root.set_showcase_table_band(table_band);

        auto* const table_caption = new preferred_widget_t{ table_band };
        table_caption->set_preferred_size(
            extent_t{ 100, default_style().pixel_metric(pixel_metric_enum::TITLE_HEIGHT) });
        table_caption->set_size_policy(size_policy_enum::FIXED, size_policy_enum::FIXED);

        auto* const table = new table_view_t{ table_band };
        table->set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::EXPANDING);
        table->set_stretch(1);
        table->set_model(model); // the model the list above installed: one source for both views
        table->connect("cell_activated(int,int) const", [&root](int row, int column) {
            root.note_cell_activated(row, column);
        });
        root.set_showcase_table(table);

        auto* const table_band_layout = new vbox_layout_t;
        table_band_layout->set_margins(margins_t{ 6, 4, 6, 4 });
        table_band_layout->set_spacing(0);
        table_band->set_layout(table_band_layout);

        // ---- tree view: a hierarchical model (3 roots x 3 children x 2 leaves) ---------------------
        // The same band recipe as the list and the table: the band is the root's drawing area, it
        // reserves the caption row the root paints and gives the tree the rest.
        auto* const tree_band = new widget_t{ column };
        tree_band->set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::EXPANDING);
        tree_band->set_stretch(1);
        root.set_showcase_tree_band(tree_band);

        auto* const tree_caption = new preferred_widget_t{ tree_band };
        tree_caption->set_preferred_size(
            extent_t{ 100, default_style().pixel_metric(pixel_metric_enum::TITLE_HEIGHT) });
        tree_caption->set_size_policy(size_policy_enum::FIXED, size_policy_enum::FIXED);

        // Like the rows model: an object child of the root, borrowed by the view.
        auto* const tree_model = new demo_tree_model_t;
        tree_model->set_parent(&root);

        auto* const tree = new tree_view_t{ tree_band };
        tree->set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::EXPANDING);
        tree->set_stretch(1);
        tree->set_model(tree_model);
        tree->connect(signal_signature(*tree, "expanded_changed"), [&root](const item_index_t& index, bool expanded) {
            root.note_expanded(expanded);
        });
        root.set_showcase_tree(tree);
        root.set_showcase_tree_model(tree_model);

        auto* const tree_band_layout = new vbox_layout_t;
        tree_band_layout->set_margins(margins_t{ 6, 4, 6, 4 });
        tree_band_layout->set_spacing(0);
        tree_band->set_layout(tree_band_layout);

        // ---- scroll area: content clearly taller than the viewport ---------------------------------
        auto* const scroller = new scroll_view_t{ column };
        scroller->set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::EXPANDING);
        scroller->set_stretch(1);
        scroller->set_scroll_bar_policy(scroll_bar_policy_enum::AS_NEEDED);
        root.set_showcase_scroll(scroller);

        auto* const content = new preferred_widget_t;
        content->set_preferred_size(extent_t{ 420, 420 });
        // 0-based, like the list view's rows above: one row index means the same number everywhere.
        for (int row = 0; row < 18; ++row) {
            auto* const row_label = new label_t{ content };
            row_label->set_text("row " + std::to_string(row));
        }
        auto* const content_layout = new vbox_layout_t;
        content_layout->set_margins(margins_t{ 8, 8, 8, 8 });
        content_layout->set_spacing(6);
        content->set_layout(content_layout);
        scroller->set_content(content);

        auto* const column_layout = new vbox_layout_t;
        column_layout->set_spacing(10);
        column->set_layout(column_layout);

        // ---- image: a picture shown by a plain label_t (QLabel::setPixmap) --------------------------
        // The picture is committed next to this file, so the frame stays deterministic: the search below
        // only has to bridge "the exe runs in the build directory". A BMP and a PNG are offered in turn —
        // load_image() sniffs the magic bytes, so either one proves the decoding path end to end.
        auto* const picture = new label_t{ column };
        // A fixed SQUARE band: the picture is square, so scaled_contents() fills it without distorting
        // the aspect ratio, and the column keeps the height it had before the picture arrived.
        picture->set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::PREFERRED);
        picture->set_fixed_size(extent_t{ 150, 150 });
        picture->set_scaled_contents(true);
        picture->set_text_alignment(text_alignment_enum::CENTER);
        root.set_showcase_image(picture);
        for (const char* const name : { "hwshqtb2.png", "hwshqtb2.bmp" }) {
            for (const char* const prefix : { "examples/ui_demo/", "../examples/ui_demo/",
                                              "../../examples/ui_demo/", "../../../examples/ui_demo/",
                                              "../../../../examples/ui_demo/" }) {
                try {
                    picture->set_image(hwshqtb::gui::load_image(std::string(prefix) + name));
                    break;
                } catch (const std::exception&) {
                    continue; // try the next spelling; the label keeps showing nothing
                }
            }
            if (picture->has_image()) {
                break;
            }
        }
    }

    /// @brief Build the demo tree and install the layouts.
    ///
    /// Left half: the retained tree (a nested vbox inside panel A, an hbox inside panel B), plus the
    /// two focus-ring buttons and the status row. Middle: the immediate host item. Right: the control
    /// showcase column. One root hbox lays out the three columns.
    void build_tree(demo_root_t& root, bool with_cjk_panel) {
        // The retained column on the left. It has NO size override: its vbox derives the natural
        // width from its children (panel B's hbox needs 116 px), and the root hbox expands both
        // columns by the same stretch-1 weight — the resulting split (303/187) is the real answer.
        auto* const left = new widget_t{ &root };
        left->set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::EXPANDING);
        left->set_stretch(1);
        root.set_left_widget(left);

        auto* const panel_a = new panel_t{ panel_a_color, color_t::white_color(), "panel A", border_color, left };
        auto* const panel_b = new panel_t{ panel_b_color, color_t::white_color(), "panel B", border_color, left };
        panel_a->set_state(&root.state());
        panel_b->set_state(&root.state());
        // No size opinion: both panels own a layout now, so their hint comes from their children
        // (layout_t::size_hint()) — the container's natural size.
        panel_a->set_size_policy(size_policy_enum::PREFERRED, size_policy_enum::EXPANDING);
        panel_b->set_size_policy(size_policy_enum::PREFERRED, size_policy_enum::EXPANDING);
        panel_a->set_stretch(1);
        panel_b->set_stretch(1);
        root.set_panels(panel_a, panel_b);

        // Panel A: a vertical stack; child A1 takes the height the fixed grandchild leaves over.
        auto* const child_a1 = new panel_t{ child_a1_color, color_t::white_color(), "child A1", border_color, panel_a };
        auto* const grandchild = new panel_t{ grandchild_color, color_t::black_color(), "A1.1", border_color, panel_a };
        child_a1->set_state(&root.state());
        grandchild->set_state(&root.state());
        child_a1->set_preferred_size(extent_t{ 80, 40 });
        grandchild->set_preferred_size(extent_t{ 45, 24 });   // tall enough for the 7-row glyph cell + border
        child_a1->set_size_policy(size_policy_enum::PREFERRED, size_policy_enum::EXPANDING);
        child_a1->set_stretch(1);

        auto* const layout_a = new vbox_layout_t;
        // The top margin is the caption band the root paints the panel title into; it must hold one
        // font line. For the built-in 8 px face this is the old 22.
        layout_a->set_margins(margins_t{ 8, font_metrics_t{}.line_height() + 14, 8, 8 });
        layout_a->set_spacing(6);
        panel_a->set_layout(layout_a);

        // Panel B: a horizontal row; B1 takes the width the fixed B2 leaves over.
        auto* const child_b1 = new panel_t{ child_b1_color, color_t::black_color(), "B1", border_color, panel_b };
        auto* const child_b2 = new panel_t{ child_b2_color, color_t::white_color(), "B2", border_color, panel_b };
        child_b1->set_state(&root.state());
        child_b2->set_state(&root.state());
        child_b1->set_preferred_size(extent_t{ 60, 34 });
        child_b2->set_preferred_size(extent_t{ 34, 34 });
        child_b1->set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::PREFERRED);
        child_b1->set_stretch(1);

        auto* const layout_b = new hbox_layout_t;
        layout_b->set_margins(margins_t{ 8, font_metrics_t{}.line_height() + 14, 8, 8 });
        layout_b->set_spacing(6);
        panel_b->set_layout(layout_b);

        // The Tab chain: two retained push buttons with a focus ring (strong_focus). The row's height
        // is its opinion; its WIDTH follows the column (a preferred size, not a fixed constraint).
        auto* const button_row = new preferred_widget_t{ left };
        button_row->set_preferred_size(extent_t{ 100, 28 });
        button_row->set_size_policy(size_policy_enum::FIXED, size_policy_enum::FIXED);
        root.set_button_row(button_row);
        auto* const first = new focus_ring_button_t{ button_row };
        auto* const second = new focus_ring_button_t{ button_row };
        first->set_text("One");
        second->set_text("Two");
        // No size hint here: push_button_t::size_hint() is content-derived.
        auto* const row_layout = new hbox_layout_t;
        row_layout->set_spacing(6);
        button_row->set_layout(row_layout);
        root.set_focus_buttons(first, second);

        // The status row the root paints the retained readout into (reserved space, no painting).
        // Its height is its opinion; its width follows the column.
        auto* const status = new preferred_widget_t{ left };
        // Two font lines fit here; the literal 26 is what that comes to for the built-in face.
        status->set_preferred_size(
            extent_t{ 100, 2 * font_metrics_t{}.line_height() + 10 });
        status->set_size_policy(size_policy_enum::FIXED, size_policy_enum::FIXED);
        root.set_status_widget(status);

        auto* const left_layout = new vbox_layout_t;
        left_layout->set_spacing(6);
        left->set_layout(left_layout);

        // The immediate host is an ITEM child of the root: the hbox lays it out next to `left`, and
        // the item traversal paints it and hit-tests it. Its frame handler drives the three buttons.
        auto* const im = new im_host_t;
        im->set_parent(&root);
        im->set_size_policy(size_policy_enum::EXPANDING, size_policy_enum::EXPANDING);
        im->set_stretch(1);
        // The readout this column paints below the controls ("cb=… slider=… v=…", "clicked …x last: …")
        // is the root's drawing, so im_host_t never learns its width from it. Left alone the column is
        // sized by its CONTROLS and the readout runs into the next column. The floor is font-derived so
        // a wider face widens the column instead of spilling.
        im->set_minimum_width(font_metrics_t{}.horizontal_advance("cb=00 slider=000 v=000") + 20);
        im->set_focus_policy(focus_policy_enum::TAB_FOCUS); // the host itself is a Tab station
        im->set_frame_handler([&root](im_host_t& host) {
            root.drive_frame(host);
        });
        root.set_im_host(im);

        // The control showcase occupies the third column of the same root hbox.
        build_showcase(root);

        // ---- the CJK panel: built ONLY when the platform font is in use -------------------------
        // The hash-gated --screenshot must not contain one pixel that depends on the user's installed
        // fonts, and --self-test asserts numbers a fourth column would change, so this column exists
        // exactly for the interactive/system-font runs. Its texts are written as UTF-8 escapes so this
        // source file stays pure ASCII.
        if (with_cjk_panel) {
            auto* const cjk = new group_box_t{ &root };
            cjk->set_title("CJK");
            cjk->set_size_policy(size_policy_enum::PREFERRED, size_policy_enum::PREFERRED);
            // Content-driven floor with air on both sides: the widest text is 4 CJK cells (72 px at the
            // system face's 18 px advance), plus the two 8 px text paddings, plus the box's own two 8 px
            // margins, plus 6 px of slack so no glyph can touch the frame. PREFERRED alone already sized
            // the column from the children (they came out exactly text + 2*padding), but the floor makes
            // the guarantee explicit instead of incidental.
            const hwshqtb::gui::font_metrics_t cjk_metrics{};
            const int widest_text = cjk_metrics.text_extent("\xE8\xBE\x93\xE5\x85\xA5\xE4\xB8\xAD\xE6\x96\x87").width();
            cjk->set_minimum_size(
                hwshqtb::gui::extent_t{ widest_text + 4 * 8 + 6, 0 });
            root.set_cjk_column(cjk);

            auto* const cjk_label = new label_t{ cjk };
            cjk_label->set_text("\xE4\xB8\xAD\xE6\x96\x87\xE6\xB5\x8B\xE8\xAF\x95"); // 中文测试
            auto* const cjk_button = new push_button_t{ cjk };
            cjk_button->set_text("\xE7\xA1\xAE\xE5\xAE\x9A"); // 确定
            auto* const cjk_cancel = new push_button_t{ cjk };
            cjk_cancel->set_text("\xE5\x8F\x96\xE6\xB6\x88"); // 取消
            auto* const cjk_input = new line_edit_t{ cjk };
            cjk_input->set_text("\xE8\xBE\x93\xE5\x85\xA5\xE4\xB8\xAD\xE6\x96\x87"); // 输入中文

            auto* const cjk_layout = new vbox_layout_t;
            cjk_layout->set_margins(margins_t{ 8, 6, 8, 8 });
            cjk_layout->set_spacing(6);
            cjk->set_layout(cjk_layout);
        }

        // Root: header margin on top, footer margin at the bottom, then the three columns with even
        // spacing. The bottom margin leaves room for the footer text (paint_event draws it BEFORE the
        // children, so a panel that reached the bottom edge would paint over it).
        auto* const root_layout = new hbox_layout_t;
        // Header and footer margins: each must hold one font line (the title at the top, the footer
        // text at the bottom). Both reduce to the old 36 / 24 for the built-in 8 px face.
        const int root_line = font_metrics_t{}.line_height();
        root_layout->set_margins(margins_t{ 10, root_line + 28, 10, root_line + 16 });
        root_layout->set_spacing(10);
        root.set_layout(root_layout);
    }

    /// @brief Parsed command line.
    struct options_t {
        std::string screenshot_path;
        /// @brief --screenshot-font: capture the SAME interface rendered with the platform's UI font.
        /// @note Not part of the hash gate: the system face is machine dependent. It exists to prove
        ///       that the font database really resolves and rasterises a platform face.
        std::string screenshot_font_path;
        int duration_ms = 0;
        bool self_test = false;
        /// @brief Run the application loop in phased mode (default on; --no-phase turns it off).
        bool phase = true;
        /// @brief The client extent requested with --size; 0x0 selects the default 920x1280.
        extent_t window_size{};
        /// @brief Wheel ticks of the --wheel-burst debug capture (0 = not requested).
        int wheel_burst = 0;
        /// @brief Whether --wheel-burst places the pointer over the content edge instead of the centre.
        bool wheel_burst_content = false;
        /// @brief Wheel ticks of the --table-burst debug capture (0 = not requested).
        int table_burst = 0;
    };

    void print_usage() {
        std::printf(
            "usage: ui_demo [--duration <ms>] [--screenshot <path>] [--self-test] [--no-phase]\n"
            "               [--size <W>x<H>] [--wheel-burst <N>] [--table-burst <N>]\n"
            "  (no arguments)       persistent window; close it or press ESC to exit\n"
            "  --duration <ms>      close the window automatically after <ms> milliseconds\n"
            "  --screenshot <path>  render one deterministic frame (frame 0) to a BMP and exit\n"
            "  --screenshot-font <path>\n"
            "                       render the same frame with the PLATFORM UI font (machine dependent;\n"
            "                       proves the font database resolves and rasterises a system face)\n"
            "  --wheel-burst <N>    prime the scroll area, then inject one mouse motion + N wheel\n"
            "                       events in one SDL batch, paint exactly one frame and capture it\n"
            "  --table-burst <N>    the same capture with the pointer over the showcase table: prime\n"
            "                       its offset, push one motion + N wheel events, paint one frame\n"
            "  --wheel-burst-point <centre|content>\n"
            "                       pointer position of --wheel-burst (default centre; 'content'\n"
            "                       avoids the content's hover repaint so the stripe is not masked)\n"
            "  --self-test          inject clicks, keys, text input and a wheel through the native\n"
            "                       event filter chain and verify both hosts and the showcases\n"
            "  --no-phase           use event-by-event processing instead of the default phased loop\n"
            "  --size <W>x<H>       start with this client size instead of 920x1280\n");
    }

    /// @brief Parse argv; false means the arguments were rejected (the caller exits with 2).
    bool parse_options(int argc, char** argv, options_t& options) {
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument = argv[i];
            if (argument == "--screenshot") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "ui_demo: --screenshot needs a path\n");
                    return false;
                }
                options.screenshot_path = argv[++i];
            } else if (argument == "--screenshot-font") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "ui_demo: --screenshot-font needs a path\n");
                    return false;
                }
                options.screenshot_font_path = argv[++i];
            } else if (argument == "--duration") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "ui_demo: --duration needs a millisecond count\n");
                    return false;
                }
                const std::string value = argv[++i];
                char* end = nullptr;
                const long parsed = std::strtol(value.c_str(), &end, 10);
                if (end == value.c_str() || *end != '\0' || parsed < 0) {
                    std::fprintf(stderr, "ui_demo: invalid --duration value '%s'\n", value.c_str());
                    return false;
                }
                options.duration_ms = static_cast<int>(parsed);
            } else if (argument == "--size") {
                // --size <W>x<H>: both sides must be positive integers, the 'x' is required.
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "ui_demo: --size needs <W>x<H>\n");
                    return false;
                }
                const std::string value = argv[++i];
                const std::size_t separator = value.find('x');
                char* width_end = nullptr;
                char* height_end = nullptr;
                const long width = std::strtol(value.c_str(), &width_end, 10);
                long height = 0;
                if (separator != std::string::npos && width_end == value.c_str() + separator) {
                    height = std::strtol(value.c_str() + separator + 1, &height_end, 10);
                }
                if (separator == std::string::npos || width_end != value.c_str() + separator
                    || width <= 0 || height <= 0 || height_end == nullptr || *height_end != '\0') {
                    std::fprintf(stderr, "ui_demo: invalid --size value '%s'\n", value.c_str());
                    return false;
                }
                options.window_size = extent_t{ static_cast<int>(width), static_cast<int>(height) };
            } else if (argument == "--self-test") {
                options.self_test = true;
            } else if (argument == "--wheel-burst") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "ui_demo: --wheel-burst needs a tick count\n");
                    return false;
                }
                const std::string value = argv[++i];
                char* end = nullptr;
                const long parsed = std::strtol(value.c_str(), &end, 10);
                if (end == value.c_str() || *end != '\0' || parsed <= 0) {
                    std::fprintf(stderr, "ui_demo: invalid --wheel-burst value '%s'\n", value.c_str());
                    return false;
                }
                options.wheel_burst = static_cast<int>(parsed);
            } else if (argument == "--wheel-burst-point") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "ui_demo: --wheel-burst-point needs 'centre' or 'content'\n");
                    return false;
                }
                const std::string_view point = argv[++i];
                if (point == "content") {
                    options.wheel_burst_content = true;
                }
                else if (point == "centre" || point == "center") {
                    options.wheel_burst_content = false;
                }
                else {
                    std::fprintf(stderr, "ui_demo: invalid --wheel-burst-point value '%.*s'\n",
                                 static_cast<int>(point.size()), point.data());
                    return false;
                }
            } else if (argument == "--table-burst") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "ui_demo: --table-burst needs a tick count\n");
                    return false;
                }
                const std::string value = argv[++i];
                char* end = nullptr;
                const long parsed = std::strtol(value.c_str(), &end, 10);
                if (end == value.c_str() || *end != '\0' || parsed <= 0) {
                    std::fprintf(stderr, "ui_demo: invalid --table-burst value '%s'\n", value.c_str());
                    return false;
                }
                options.table_burst = static_cast<int>(parsed);
            } else if (argument == "--no-phase") {
                options.phase = false;
            } else if (argument == "--help" || argument == "-h") {
                print_usage();
                std::exit(0);
            } else {
                std::fprintf(stderr, "ui_demo: unknown argument '%.*s'\n",
                             static_cast<int>(argument.size()), argument.data());
                return false;
            }
        }
        if (options.wheel_burst > 0 && options.table_burst > 0) {
            std::fprintf(stderr, "ui_demo: --wheel-burst and --table-burst are exclusive\n");
            return false;
        }
        return true;
    }

    /// @brief Push one synthetic mouse event into SDL's queue (the self-test's input).
    void push_mouse_event(std::uint32_t window_id, std::uint32_t type, point_t position, bool down) {
        SDL_Event event{};
        event.type = type;
        if (type == SDL_EVENT_MOUSE_MOTION) {
            event.motion.windowID = window_id;
            event.motion.x = static_cast<float>(position.x());
            event.motion.y = static_cast<float>(position.y());
            event.motion.state = 0;
        }
        else {
            event.button.windowID = window_id;
            event.button.button = SDL_BUTTON_LEFT;
            event.button.down = down;
            event.button.clicks = 1;
            event.button.x = static_cast<float>(position.x());
            event.button.y = static_cast<float>(position.y());
        }
        if (!SDL_PushEvent(&event)) {
            std::fprintf(stderr, "ui_demo: SDL_PushEvent failed: %s\n", SDL_GetError());
        }
    }

    /// @brief Push one synthetic key event (a Tab press in the self-test).
    void push_key_event(std::uint32_t window_id, int key, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = window_id;
        event.key.key = static_cast<SDL_Keycode>(key);
        event.key.down = down;
        if (!SDL_PushEvent(&event)) {
            std::fprintf(stderr, "ui_demo: SDL_PushEvent failed: %s\n", SDL_GetError());
        }
    }

    /// @brief Push one synthetic text-input event into SDL's queue.
    /// @param window_id The window the event belongs to.
    /// @param text The UTF-8 text; it must stay valid until the loop processed the event (the test
    ///        passes literals).
    void push_text_event(std::uint32_t window_id, const char* text) {
        SDL_Event event{};
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.windowID = window_id;
        event.text.text = text;
        if (!SDL_PushEvent(&event)) {
            std::fprintf(stderr, "ui_demo: SDL_PushEvent failed: %s\n", SDL_GetError());
        }
    }

    /// @brief Push one synthetic wheel event into SDL's queue (whole ticks).
    void push_wheel_event(std::uint32_t window_id, int x, int y) {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.windowID = window_id;
        event.wheel.x = static_cast<float>(x);
        event.wheel.y = static_cast<float>(y);
        event.wheel.integer_x = x;
        event.wheel.integer_y = y;
        if (!SDL_PushEvent(&event)) {
            std::fprintf(stderr, "ui_demo: SDL_PushEvent failed: %s\n", SDL_GetError());
        }
    }

    /// @brief The widget a debug wheel burst scrolls.
    enum struct burst_target_enum : std::uint8_t {
        /// @brief The showcase scroll view (--wheel-burst).
        SCROLL_VIEW,
        /// @brief The showcase table (--table-burst).
        TABLE,
    };

    /// @brief What --wheel-burst / --table-burst observed and wrote.
    struct wheel_burst_result_t {
        /// @brief The widget the burst scrolled.
        burst_target_enum target = burst_target_enum::SCROLL_VIEW;
        /// @brief Wheel ticks injected in the capture burst.
        int ticks = 0;
        /// @brief Frames the priming batch painted.
        int prime_frames = 0;
        /// @brief Frames the capture burst painted (must be 1).
        int burst_frames = 0;
        /// @brief The scroll offset after priming.
        int offset_primed = -1;
        /// @brief The scroll offset after the capture burst.
        int offset_after = -1;
        /// @brief Whether the frame pixels were captured to disk.
        bool captured = false;
        /// @brief Whether the whole schedule ran.
        bool completed = false;
        /// @brief The capture path.
        std::string path;
        /// @brief The full-repaint reference of the same state written next to @p path.
        std::string reference_path;
        /// @brief The scroll area's rectangle in window coordinates.
        rect_t area_global{};
        /// @brief The content's geometry in the area's coordinates.
        rect_t content{};
        /// @brief The window client extent.
        extent_t window_size{};
    };

    /// @brief Drives --wheel-burst / --table-burst from a timer on the library event loop.
    ///
    /// Phase 0 primes the target widget with one batch of wheel ticks and lets its frame paint;
    /// phase 1 injects the capture batch — ONE mouse motion to the pointer point plus N wheel events,
    /// all in one SDL batch, which the phased loop delivers in a single phase — and waits for the
    /// frame; phase 2 reads the painted frame's pixels and quits. The capture never repaints:
    /// save_painted_frame() reads the backing surface the frame presented, so stale pixels stay
    /// stale.
    class wheel_burst_driver_t final: public object_t {
    public:
        wheel_burst_driver_t(widget_window_t& window, demo_root_t& root, int ticks, bool content_point,
                             burst_target_enum target, std::string path, wheel_burst_result_t& result):
            _window(&window),
            _root(&root),
            _result(&result),
            _content_point(content_point),
            _target(target) {
            _result->target = target;
            _result->ticks = ticks;
            _result->path = std::move(path);
            _timer = start_timer(20);
        }

    protected:
        void timer_event(timer_event_t* event) override {
            if (event == nullptr || event->timer_id() != _timer || _window == nullptr
                || _root == nullptr) {
                return;
            }
            step();
        }

    private:
        /// @brief Wheel ticks the priming batch injects (8 x 16 px gives the capture room to scroll up).
        static constexpr int prime_ticks = 8;

        void step() {
            if (++_ticks_seen > 500) {
                std::fprintf(stderr, "ui_demo: wheel burst timed out\n");
                core_application::quit();
                return;
            }
            widget_t* const target = target_widget();
            if (target == nullptr) {
                std::fprintf(stderr, "ui_demo: wheel burst has no %s\n",
                             _target == burst_target_enum::TABLE ? "table" : "scroll area");
                core_application::quit();
                return;
            }
            const std::uint32_t id = _window->window_id();
            const std::size_t frames = _window->painted_frame_count();
            switch (_phase) {
            case 0:
                if (frames < 1) {
                    return; // the synchronous base frame has not been counted yet
                }
                _pointer = target->map_to_global(point_t{ target->rect().width() / 2,
                                                          target->rect().height() / 2 });
                if (_content_point && _target == burst_target_enum::SCROLL_VIEW) {
                    // A point over the CONTENT but in its left margin (the row labels start 8 px in).
                    // The content stays the hit target while it scrolls, so its hover state never
                    // changes and the incidental whole-content hover repaint cannot repair the
                    // defect inside the captured frame. The wheel still bubbles to the area.
                    const rect_t bounds = target->rect();
                    _pointer = target->map_to_global(point_t{ 4, bounds.height() / 2 });
                }
                // Prime in one batch: the pointer over the target plus wheel ticks toward the user.
                push_mouse_event(id, SDL_EVENT_MOUSE_MOTION, _pointer, false);
                for (int tick = 0; tick < prime_ticks; ++tick) {
                    push_wheel_event(id, 0, -1);
                }
                _prime_start = frames;
                _phase = 1;
                break;
            case 1: {
                if (frames <= _prime_start) {
                    return; // the priming frame is still pending
                }
                _result->prime_frames = static_cast<int>(frames - _prime_start);
                _result->offset_primed = scroll_offset_y();
                if (_result->offset_primed <= 0) {
                    std::fprintf(stderr, "ui_demo: wheel burst could not prime the %s\n",
                                 _target == burst_target_enum::TABLE ? "table" : "scroll area");
                    core_application::quit();
                    return;
                }
                // The capture batch: one motion and N wheel ticks, ALL pushed before the phase pumps
                // them, so they reach the target in one frame. The scroll area bursts against the
                // priming direction (a wheel away from the user drags the area's frame into the
                // viewport); the table bursts on down, the direction that reaches the deep rows and
                // leaves the view's bottom border in place while its stale copy is dragged upward.
                push_mouse_event(id, SDL_EVENT_MOUSE_MOTION, _pointer, false);
                const int direction = _target == burst_target_enum::TABLE ? -1 : 1;
                for (int tick = 0; tick < _result->ticks; ++tick) {
                    push_wheel_event(id, 0, direction);
                }
                _burst_start = frames;
                _phase = 2;
                break;
            }
            case 2: {
                if (frames <= _burst_start) {
                    return; // the burst frame is still pending
                }
                _result->burst_frames = static_cast<int>(frames - _burst_start);
                _result->offset_after = scroll_offset_y();
                const point_t origin = target->map_to_global(point_t{});
                _result->area_global =
                    rect_t{ origin.x(), origin.y(), target->rect().width(), target->rect().height() };
                _result->content = content_rect();
                _result->window_size = _window->size();
                // Read the frame the loop just painted and presented; do NOT paint a new one.
                _window->save_painted_frame(_result->path);
                _result->captured = true;
                _result->completed = true;
                // The reference: a FULL repaint of the same tree state. A correct fast path leaves
                // the presented frame pixel-identical to it; a stale band (or a dragged frame line)
                // shows up as the exact differing pixels. This repaint happens AFTER the capture, so
                // it cannot repair the frame under test.
                const std::filesystem::path capture{ _result->path };
                _result->reference_path = _result->path;
                const std::string extension = capture.extension().string();
                if (!extension.empty()) {
                    _result->reference_path.erase(_result->reference_path.size() - extension.size());
                }
                _result->reference_path += "_ref" + (extension.empty() ? ".bmp" : extension);
                _window->save_screenshot(_result->reference_path);
                const std::filesystem::path absolute =
                    std::filesystem::absolute(std::filesystem::path{ _result->path });
                const std::filesystem::path absolute_reference =
                    std::filesystem::absolute(std::filesystem::path{ _result->reference_path });
                const char* const kind =
                    _target == burst_target_enum::TABLE ? "table burst" : "wheel burst";
                std::printf(
                    "[ui_demo] %s %d: prime %d tick(s) -> offset %d in %d frame(s); burst "
                    "%d tick(s) -> offset %d in %d frame(s), pointer %s\n",
                    kind, _result->ticks, prime_ticks, _result->offset_primed,
                    _result->prime_frames, _result->ticks, _result->offset_after,
                    _result->burst_frames,
                    _content_point && _target == burst_target_enum::SCROLL_VIEW ? "content"
                                                                                 : "centre");
                std::printf(
                    "[ui_demo] %s %d: %s [%d,%d %dx%d] content [%d,%d %dx%d] "
                    "window %dx%d; wrote %s (framed pixels) and %s (full-repaint reference)\n",
                    kind, _result->ticks,
                    _target == burst_target_enum::TABLE ? "table" : "scroll area",
                    _result->area_global.x(), _result->area_global.y(),
                    _result->area_global.width(), _result->area_global.height(), _result->content.x(),
                    _result->content.y(), _result->content.width(), _result->content.height(),
                    _result->window_size.width(), _result->window_size.height(),
                    absolute.string().c_str(), absolute_reference.string().c_str());
                core_application::quit();
                break;
            }
            default:
                core_application::quit();
                break;
            }
        }

        /// @brief The widget the burst scrolls, or nullptr when the demo did not build it.
        widget_t* target_widget() const {
            if (_target == burst_target_enum::TABLE) {
                return _root->showcase_table();
            }
            return _root->showcase_scroll();
        }

        /// @brief The target's current vertical scroll offset, or -1 when the target is gone.
        int scroll_offset_y() const {
            if (_target == burst_target_enum::TABLE) {
                const table_view_t* const table = _root->showcase_table();
                return table != nullptr ? table->scroll_offset().y() : -1;
            }
            const scroll_view_t* const area = _root->showcase_scroll();
            return area != nullptr ? area->scroll_offset().y() : -1;
        }

        /// @brief The scrolled content extent in the target's own coordinates.
        ::hwshqtb::gui::rect_t content_rect() const {
            if (_target == burst_target_enum::TABLE) {
                const table_view_t* const table = _root->showcase_table();
                if (table == nullptr) {
                    return {};
                }
                const int rows = table->model() != nullptr ? table->model()->row_count() : 0;
                return ::hwshqtb::gui::rect_t{ 0, 0, table->content_width(),
                                               rows * table->row_height() };
            }
            const scroll_view_t* const area = _root->showcase_scroll();
            return area != nullptr && area->content() != nullptr ? area->content()->geometry()
                                                                 : ::hwshqtb::gui::rect_t{};
        }

        widget_window_t* _window = nullptr;
        demo_root_t* _root = nullptr;
        wheel_burst_result_t* _result = nullptr;
        int _timer = 0;
        int _phase = 0;
        int _ticks_seen = 0;
        bool _content_point = false;
        burst_target_enum _target = burst_target_enum::SCROLL_VIEW;
        point_t _pointer{};
        std::size_t _prime_start = 0;
        std::size_t _burst_start = 0;
    };

#if defined(_WIN32)
    /// @brief The Win32 HWND behind @p window (SDL's window property), or nullptr.
    HWND win32_hwnd_of(const widget_window_t& window) {        SDL_Window* const sdl_window =
            SDL_GetWindowFromID(static_cast<SDL_WindowID>(window.window_id()));
        if (sdl_window == nullptr) {
            return nullptr;
        }
        return static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(sdl_window),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    }
#endif

    /// @brief What --self-test observed.
    struct self_test_result_t {
        /// @brief Activated retained panels after the retained click.
        int retained_clicks = 0;
        /// @brief The retained panel that was clicked.
        std::string last_retained;
        /// @brief Activated immediate controls after the IM click.
        int im_clicks = 0;
        /// @brief The immediate control that was activated.
        std::string last_im;
        /// @brief Retained clicks AFTER the immediate click (must still be 1: no steal either way).
        int retained_clicks_after_im = 0;
        /// @brief The retained label AFTER the immediate click (must still be "panel A").
        std::string last_retained_after_im;
        /// @brief Whether Tab moved the focus to the first retained button.
        bool focus_on_first = false;
        /// @brief Whether the Tab row took the whole column width (preferred size, not a fixed box).
        bool button_row_fills_column = false;
        /// @brief Whether Tab moved the OUTER focus onto the immediate host item.
        bool host_focused = false;
        /// @brief Whether exactly the first/second/third IM control held the inner focus, in order.
        bool im_first_focused = false;
        bool im_second_focused = false;
        bool im_third_focused = false;
        /// @brief Whether the next Tab left the host (the focus moved on and the ring cleared).
        bool host_left = false;
        /// @brief The showcase check box after the click: checked, and how many toggles fired.
        bool checkbox_checked = false;
        int checkbox_toggles = 0;
        /// @brief The showcase radios after clicking the second one.
        bool radio_first_checked = true;
        bool radio_second_checked = false;
        /// @brief The showcase slider's value after one Right arrow step.
        int slider_value = -1;
        /// @brief The showcase line edit after one text-input step.
        bool text_expected = false;
        int text_edits = 0;
        /// @brief The showcase tab widget after clicking the second tab.
        int tab_current = -1;
        bool tab_first_visible = true;
        bool tab_second_visible = false;
        /// @brief The showcase scroll area after one wheel step.
        int scroll_y = -1;
        int scroll_max = -1;
        /// @brief The row count of the list's model (the End key must land on the last row).
        int list_rows = 0;
        /// @brief The list's current row after the click on row 3 (-1 when the click did nothing).
        int list_click_row = -1;
        /// @brief Whether row 3 was selected after that click.
        bool list_click_selected = false;
        /// @brief Whether that click also put the item focus on the list (the End key needs it).
        bool list_click_focused = false;
        /// @brief The list's scroll offset after the click on row 3.
        int list_click_offset = -1;
        /// @brief The list's current row after End (must be list_rows - 1).
        int list_end_row = -1;
        /// @brief Whether the current row was selected after End.
        bool list_end_selected = false;
        /// @brief The list's scroll offset after End (must differ from list_click_offset).
        int list_end_offset = -1;
        /// @brief The list's current row after clicking the current row again.
        int list_reclick_row = -1;
        /// @brief Whether the current row was still selected after that second click.
        bool list_reclick_selected = false;
        /// @brief The table's current row after the click on cell (2, 1).
        int table_click_row = -1;
        /// @brief The table's current column after that click.
        int table_click_column = -1;
        /// @brief Whether the clicked row (2) was selected after that click.
        bool table_click_selected = false;
        /// @brief The table's current row after Right (must not move: 2).
        int table_right_row = -1;
        /// @brief The table's current column after Right (must be 2).
        int table_right_column = -1;
        /// @brief How many cell_activated signals the table emitted.
        int table_activations = 0;
        /// @brief How many cell_activated signals had fired after the second click (must still be 1).
        int table_reclick_activations = 0;
        /// @brief The row the last cell_activated signal carried.
        int table_activated_row = -1;
        /// @brief The column the last cell_activated signal carried.
        int table_activated_column = -1;
        /// @brief The table's current row after clicking the same cell again.
        int table_reclick_row = -1;
        /// @brief The table's current column after clicking the same cell again.
        int table_reclick_column = -1;
        /// @brief Whether the current row was still selected after that second click.
        bool table_reclick_selected = false;
        /// @brief The tree's current row after the click on the first root's row.
        int tree_click_row = -1;
        /// @brief Whether that click also selected the row.
        bool tree_click_selected = false;
        /// @brief Whether the current NODE after that click is the model's first root (its pointer,
        ///        not its row number).
        bool tree_click_node = false;
        /// @brief Whether that click also put the item focus on the tree (the arrows need it).
        bool tree_click_focused = false;
        /// @brief The tree's visible row count before Right: the collapsed top level (3).
        int tree_rows_collapsed = -1;
        /// @brief The tree's visible row count after Right: the first root's children added (6).
        int tree_rows_expanded = -1;
        /// @brief The current row after Right (must stay on the expanded node: 0).
        int tree_right_row = -1;
        /// @brief How many expanded_changed signals had fired after Right (must be 1).
        int tree_expand_events = -1;
        /// @brief Whether that signal carried expanded=true.
        bool tree_expand_flag = false;
        /// @brief The current row after Down (the first root's first child: 1).
        int tree_down_row = -1;
        /// @brief Whether the current NODE after Down is that first child.
        bool tree_down_node = false;
        /// @brief The current row after the first Left (back on the parent: 0).
        int tree_left_row = -1;
        /// @brief Whether the current NODE after that Left is the first root again.
        bool tree_left_node = false;
        /// @brief Whether the first root was still expanded: that Left climbed, it did not collapse.
        bool tree_left_still_expanded = false;
        /// @brief The visible row count after the second Left (the collapsed top level: 3).
        int tree_collapsed_rows = -1;
        /// @brief How many expanded_changed signals had fired after the second Left (must be 2).
        int tree_collapse_events = -1;
        /// @brief Whether that signal carried expanded=false.
        bool tree_collapse_flag = false;
        /// @brief The current row after clicking the same row again.
        int tree_reclick_row = -1;
        /// @brief Whether that row was still selected after the re-click.
        bool tree_reclick_selected = false;
        /// @brief Whether the current NODE is still the first root after the re-click.
        bool tree_reclick_node = false;
        /// @brief How many expanded_changed signals had fired after the re-click (must still be 2).
        int tree_reclick_events = -1;
        /// @brief Whether the schedule ran to its end.
        bool completed = false;
        /// @brief Whether the application loop ran in phased mode (from --no-phase).
        bool phased = false;
        /// @brief Frames painted by the 20-motion input burst; -1 until measured.
        int burst_frames = -1;
        /// @brief How many MOUSE_MOTION events the burst injected (20).
        int burst_events = 0;
        /// @brief Whether the Win32 size-move modal pump check passed (true on non-Windows: N/A).
        bool modal_ok = false;
        /// @brief Frames painted by the two injected WM_TIMER ticks; -1 when not measured.
        int modal_frames = -1;
        /// @brief Whether the undelivered press edge survived the repair frames.
        bool modal_input_kept = false;
        /// @brief Whether a WM_TIMER after WM_EXITSIZEMOVE painted no frame.
        bool modal_timer_stopped = false;
    };

    /// @brief Drives the arbitration proof from timers on the library event loop.
    ///
    /// Each step pushes one synthetic event batch into SDL's queue and lets the REAL loop pump it, so
    /// the platform half reports it to window_t (which records the input) and the host's router
    /// dispatches the step to the item tree; a click over the immediate host is claimed by the host
    /// item itself. The loop runs for many rounds between two 60 ms steps, so a batch is always fully
    /// processed (and its pass painted) before the next step reads the counters.
    class self_test_driver_t final: public object_t {
    public:
        self_test_driver_t(widget_window_t& window, demo_root_t& root, self_test_result_t& result):
            _window(&window),
            _root(&root),
            _result(&result) {
            _timer = start_timer(60);
        }

    protected:
        void timer_event(timer_event_t* event) override {
            if (event == nullptr || event->timer_id() != _timer || _window == nullptr || _root == nullptr) {
                return;
            }
            step();
        }

    private:
        void step() {
            const std::uint32_t id = _window->window_id();
            switch (_step) {
            case 0: {
                // A click on the retained panel A. (4, 10) is inside panel A but outside its child A1
                // (which starts at the layout's 8,22 margin), so the hit is the panel itself.
                auto* const panel = _root->panel_a();
                const point_t point = panel->map_to_global(point_t{ 4, 10 });
                push_mouse_event(id, SDL_EVENT_MOUSE_MOTION, point, false);
                push_mouse_event(id, SDL_EVENT_MOUSE_BUTTON_DOWN, point, true);
                push_mouse_event(id, SDL_EVENT_MOUSE_BUTTON_UP, point, false);
                break;
            }
            case 1:
                _result->retained_clicks = _root->state().retained_clicks;
                _result->last_retained = _root->state().last_retained;
                break;
            case 2: {
                // A click on the immediate host's OK button, in the host's own coordinates: the frame
                // handler opens a group at {8, 26}, so the first row is {0, 0, width, 26} inside the
                // group and the button-local (10, 13) is the host-local (18, 39). The host is hit as
                // an item; the controls' geometry is produced in the same pass that replays the batch.
                im_host_t& host = _root->im();
                const point_t point = host.map_to_global(point_t{ 18, 39 });
                push_mouse_event(id, SDL_EVENT_MOUSE_MOTION, point, false);
                push_mouse_event(id, SDL_EVENT_MOUSE_BUTTON_DOWN, point, true);
                push_mouse_event(id, SDL_EVENT_MOUSE_BUTTON_UP, point, false);
                break;
            }
            case 3:
                _window->repaint(); // the pass that replays the batch into the host's controls
                _result->im_clicks = _root->state().im_clicks;
                _result->last_im = _root->state().last_im;
                _result->retained_clicks_after_im = _root->state().retained_clicks;
                _result->last_retained_after_im = _root->state().last_retained;
                break;
            case 4:
                // Tab moves the retained focus ring (the router handles it before any delivery).
                push_key_event(id, SDLK_TAB, true);
                push_key_event(id, SDLK_TAB, false);
                break;
            case 5:
                _result->focus_on_first = _window->router() != nullptr
                    && _window->router()->focus_item() == _root->first_focus_button();
                _result->button_row_fills_column = _root->button_row_fills_column();
                push_key_event(id, SDLK_TAB, true);   // first retained button -> second
                push_key_event(id, SDLK_TAB, false);
                break;
            case 6:
                push_key_event(id, SDLK_TAB, true);   // second retained button -> the IM host
                push_key_event(id, SDLK_TAB, false);
                break;
            case 7:
                _result->host_focused = _window->router() != nullptr
                    && _window->router()->focus_item() == &_root->im()
                    && _root->im().has_focus();
                push_key_event(id, SDLK_TAB, true);   // into the host: inner focus = first control
                push_key_event(id, SDLK_TAB, false);
                break;
            case 8:
                _result->im_first_focused = im_button("im/ok") != nullptr
                    && im_button("im/ok")->has_focus() && im_focused_count() == 1;
                push_key_event(id, SDLK_TAB, true);   // inner: first -> second
                push_key_event(id, SDLK_TAB, false);
                break;
            case 9:
                _result->im_second_focused = im_button("im/cancel") != nullptr
                    && im_button("im/cancel")->has_focus() && im_focused_count() == 1;
                push_key_event(id, SDLK_TAB, true);   // inner: second -> third
                push_key_event(id, SDLK_TAB, false);
                break;
            case 10:
                _result->im_third_focused = im_button("im/apply") != nullptr
                    && im_button("im/apply")->has_focus() && im_focused_count() == 1;
                push_key_event(id, SDLK_TAB, true);   // inner: third -> the host's check box
                push_key_event(id, SDLK_TAB, false);
                break;
            case 11:
                push_key_event(id, SDLK_TAB, true);   // inner: check box -> the slider
                push_key_event(id, SDLK_TAB, false);
                break;
            case 12:
                push_key_event(id, SDLK_TAB, true);   // at the inner end: the host refuses, the router moves
                push_key_event(id, SDLK_TAB, false);
                break;
            case 13: {
                // The host let the focus go: the router's focus is no longer the host and its inner
                // focus (and every ring) is cleared. Then click the showcase check box.
                _result->host_left = _window->router() != nullptr
                    && _window->router()->focus_item() != &_root->im() && !_root->im().has_focus()
                    && im_focused_count() == 0;
                auto* const check = _root->showcase_check();
                if (check != nullptr) {
                    _result->radio_first_checked = _root->showcase_radio_first() != nullptr
                        && _root->showcase_radio_first()->is_checked();
                    const point_t point = check->map_to_global(point_t{ 6, 13 });
                    push_mouse_event(id, SDL_EVENT_MOUSE_MOTION, point, false);
                    push_mouse_event(id, SDL_EVENT_MOUSE_BUTTON_DOWN, point, true);
                    push_mouse_event(id, SDL_EVENT_MOUSE_BUTTON_UP, point, false);
                }
                break;
            }
            case 14: {
                auto* const check = _root->showcase_check();
                _result->checkbox_checked = check != nullptr && check->is_checked();
                _result->checkbox_toggles = _root->state().showcase_toggles;
                // Click the second radio: the auto-exclusive selection must move to it.
                auto* const radio = _root->showcase_radio_second();
                if (radio != nullptr) {
                    const point_t point = radio->map_to_global(point_t{ 6, 13 });
                    push_mouse_event(id, SDL_EVENT_MOUSE_MOTION, point, false);
                    push_mouse_event(id, SDL_EVENT_MOUSE_BUTTON_DOWN, point, true);
                    push_mouse_event(id, SDL_EVENT_MOUSE_BUTTON_UP, point, false);
                }
                break;
            }
            case 15: {
                _result->radio_first_checked = _root->showcase_radio_first() != nullptr
                    && _root->showcase_radio_first()->is_checked();
                _result->radio_second_checked = _root->showcase_radio_second() != nullptr
                    && _root->showcase_radio_second()->is_checked();
                // Click the second tab of the tab widget (titles "tab 1"/"tab 2" are 46 px wide).
                auto* const tabs = _root->showcase_tabs();
                if (tabs != nullptr) {
                    const point_t point = tabs->map_to_global(point_t{ 56, 10 });
                    push_mouse_event(id, SDL_EVENT_MOUSE_MOTION, point, false);
                    push_mouse_event(id, SDL_EVENT_MOUSE_BUTTON_DOWN, point, true);
                    push_mouse_event(id, SDL_EVENT_MOUSE_BUTTON_UP, point, false);
                }
                break;
            }
            case 16:
                _result->tab_current = _root->showcase_tabs() != nullptr
                    ? _root->showcase_tabs()->current_index() : -1;
                _result->tab_first_visible = _root->showcase_first_page() != nullptr
                    && _root->showcase_first_page()->is_visible();
                _result->tab_second_visible = _root->showcase_second_page() != nullptr
                    && _root->showcase_second_page()->is_visible();
                // Click the showcase slider in the middle of its groove: the press focuses it AND
                // jumps the value to 50 (the click rule), then step it once with the keyboard (real
                // key delivery to the clicked focus).
                if (slider_t* const slider = _root->showcase_slider()) {
                    const rect_t area = slider->rect();
                    const point_t point = slider->map_to_global(
                        point_t{ area.width() / 2, area.height() / 2 });
                    push_mouse_event(id, SDL_EVENT_MOUSE_MOTION, point, false);
                    push_mouse_event(id, SDL_EVENT_MOUSE_BUTTON_DOWN, point, true);
                    push_mouse_event(id, SDL_EVENT_MOUSE_BUTTON_UP, point, false);
                }
                push_key_event(id, SDLK_RIGHT, true);
                push_key_event(id, SDLK_RIGHT, false);
                break;
            case 17:
                _result->slider_value = _root->showcase_slider() != nullptr
                    ? _root->showcase_slider()->value() : -1;
                // Click the line edit: the press focuses it (the line edit does not move the caret on
                // a click), then type through the real text-input path.
                if (line_edit_t* const line = _root->showcase_line_edit()) {
                    const rect_t area = line->rect();
                    const point_t point = line->map_to_global(
                        point_t{ area.width() - 6, area.height() / 2 });
                    push_mouse_event(id, SDL_EVENT_MOUSE_MOTION, point, false);
                    push_mouse_event(id, SDL_EVENT_MOUSE_BUTTON_DOWN, point, true);
                    push_mouse_event(id, SDL_EVENT_MOUSE_BUTTON_UP, point, false);
                }
                push_text_event(id, "OK");
                break;
            case 18:
                _result->text_expected = _root->showcase_line_edit() != nullptr
                    && _root->showcase_line_edit()->text() == "line editOK";
                _result->text_edits = _root->state().showcase_line_edits;
                // Wheel over the scroll area (the motion sets the pointer it is routed with).
                if (scroll_view_t* const scroller = _root->showcase_scroll()) {
                    const point_t point = scroller->map_to_global(point_t{ 20, 20 });
                    push_mouse_event(id, SDL_EVENT_MOUSE_MOTION, point, false);
                    push_wheel_event(id, 0, -1);
                }
                break;
            case 19:
                if (scroll_view_t* const scroller = _root->showcase_scroll()) {
                    _result->scroll_y = scroller->scroll_offset().y();
                    _result->scroll_max = scroller->vertical_scroll_bar() != nullptr
                        ? scroller->vertical_scroll_bar()->maximum() : -1;
                }
                // Input storm: 20 REAL MOUSE_MOTION events pushed in one batch, all over the same
                // control. window_t::update() coalesces the repaints they ask for, so the batch is
                // painted once in either loop mode (measured: 1 frame with and without --no-phase).
                // The frame counter is window_t::painted_frame_count() (one per painted frame), NOT
                // the root's paint_event() count: a partial frame paints one pass per damaged
                // rectangle, so the paint_event count would drift with the number of damage fragments.
                _burst_start = _window->painted_frame_count();
                {
                    auto* const panel = _root->panel_a();
                    const point_t point = panel->map_to_global(point_t{ 4, 10 });
                    for (int i = 0; i < 20; ++i) {
                        push_mouse_event(id, SDL_EVENT_MOUSE_MOTION, point, false);
                    }
                }
                break;
            case 20:
                _result->burst_frames =
                    static_cast<int>(_window->painted_frame_count() - _burst_start);
                _result->burst_events = 20;
                // Click row 3 of the showcase list. The press is hit as an ITEM (the rows are model
                // data, not items), moves the click focus onto the list, and the release lands on
                // the row it pressed, so the current row and the selected set follow.
                push_list_row_click(id, 3);
                break;
            case 21:
                if (list_view_t* const list = _root->showcase_list()) {
                    _result->list_rows = list->model() != nullptr ? list->model()->row_count() : 0;
                    _result->list_click_row = list->current_row();
                    _result->list_click_selected = list->is_row_selected(3);
                    _result->list_click_focused = list->has_focus();
                    _result->list_click_offset = list->scroll_offset().y();
                    // End moves the current row to the last row; the view scrolls it into view by
                    // itself (the last row is far outside the viewport).
                    push_key_event(id, SDLK_END, true);
                    push_key_event(id, SDLK_END, false);
                }
                break;
            case 22:
                if (list_view_t* const list = _root->showcase_list()) {
                    _result->list_end_row = list->current_row();
                    _result->list_end_selected = list->is_row_selected(list->current_row());
                    _result->list_end_offset = list->scroll_offset().y();
                    // A second click on the row that is already current: no state change, no crash.
                    push_list_row_click(id, _result->list_end_row);
                }
                break;
            case 23:
                if (list_view_t* const list = _root->showcase_list()) {
                    _result->list_reclick_row = list->current_row();
                    _result->list_reclick_selected = list->is_row_selected(_result->list_reclick_row);
                }
                // Click cell (2, 1) of the showcase table: the press is hit as an ITEM (the cells are
                // model data, not items), takes the click focus, and the release lands on the cell it
                // pressed.
                push_table_cell_click(id, 2, 1);
                break;
            case 24:
                if (table_view_t* const table = _root->showcase_table()) {
                    _result->table_click_row = table->current_row();
                    _result->table_click_column = table->current_column();
                    _result->table_click_selected = table->is_row_selected(2);
                    // Right moves the CURRENT COLUMN and leaves the row where it is.
                    push_key_event(id, SDLK_RIGHT, true);
                    push_key_event(id, SDLK_RIGHT, false);
                }
                break;
            case 25:
                if (table_view_t* const table = _root->showcase_table()) {
                    _result->table_right_row = table->current_row();
                    _result->table_right_column = table->current_column();
                    // Return activates the current CELL (not the row): one cell_activated(2, 2).
                    push_key_event(id, SDLK_RETURN, true);
                    push_key_event(id, SDLK_RETURN, false);
                }
                break;
            case 26:
                _result->table_activations = _root->cell_activations();
                _result->table_activated_row = _root->cell_activated_row();
                _result->table_activated_column = _root->cell_activated_column();
                // A second click on the cell that is already current: no state change, no crash.
                push_table_cell_click(id, 2, 2);
                break;
            case 27:
                if (table_view_t* const table = _root->showcase_table()) {
                    _result->table_reclick_row = table->current_row();
                    _result->table_reclick_column = table->current_column();
                    _result->table_reclick_selected = table->is_row_selected(2);
                }
                // The second click is a press/release on the cell that is already current: it must
                // not activate anything.
                _result->table_reclick_activations = _root->cell_activations();
                // Click the first root's row of the showcase tree. The press is hit as an ITEM (the
                // rows are model data, not items), moves the click focus onto the tree and the
                // release lands on the row it pressed.
                push_tree_row_click(id, 0);
                break;
            case 28:
                if (tree_view_t* const tree = _root->showcase_tree()) {
                    _result->tree_click_row = tree->current_row();
                    _result->tree_click_selected = tree->is_row_selected(0);
                    _result->tree_click_focused = tree->has_focus();
                    _result->tree_rows_collapsed = tree->visible_row_count();
                    if (demo_tree_model_t* const model = _root->showcase_tree_model()) {
                        // The NODE, not the row number: the index carries the model's own pointer.
                        _result->tree_click_node = tree->current_index() == model->first_root_index();
                    }
                }
                // Right expands the current node and leaves the current row on it.
                push_key_event(id, SDLK_RIGHT, true);
                push_key_event(id, SDLK_RIGHT, false);
                break;
            case 29:
                if (tree_view_t* const tree = _root->showcase_tree()) {
                    _result->tree_rows_expanded = tree->visible_row_count();
                    _result->tree_right_row = tree->current_row();
                }
                _result->tree_expand_events = _root->tree_expansions();
                _result->tree_expand_flag = _root->tree_last_expanded();
                // Down moves to the next VISIBLE row, which right below an expanded node is its first
                // child.
                push_key_event(id, SDLK_DOWN, true);
                push_key_event(id, SDLK_DOWN, false);
                break;
            case 30:
                if (tree_view_t* const tree = _root->showcase_tree()) {
                    _result->tree_down_row = tree->current_row();
                    if (demo_tree_model_t* const model = _root->showcase_tree_model()) {
                        _result->tree_down_node = tree->current_index() == model->first_child_index();
                    }
                }
                // Left on a collapsed node climbs to its parent (the other branch, collapsing an
                // expanded node, comes next).
                push_key_event(id, SDLK_LEFT, true);
                push_key_event(id, SDLK_LEFT, false);
                break;
            case 31:
                if (tree_view_t* const tree = _root->showcase_tree()) {
                    _result->tree_left_row = tree->current_row();
                    if (demo_tree_model_t* const model = _root->showcase_tree_model()) {
                        const item_index_t root_index = model->first_root_index();
                        _result->tree_left_node = tree->current_index() == root_index;
                        _result->tree_left_still_expanded = tree->is_expanded(root_index);
                    }
                }
                // The current node is the first root and it is still expanded, so this Left takes the
                // other branch: it collapses the node instead of climbing to a parent.
                push_key_event(id, SDLK_LEFT, true);
                push_key_event(id, SDLK_LEFT, false);
                break;
            case 32:
                if (tree_view_t* const tree = _root->showcase_tree()) {
                    _result->tree_collapsed_rows = tree->visible_row_count();
                }
                _result->tree_collapse_events = _root->tree_expansions();
                _result->tree_collapse_flag = _root->tree_last_expanded();
                // A second click on the row that is already current: no state change, no crash.
                push_tree_row_click(id, 0);
                break;
            case 33:
                if (tree_view_t* const tree = _root->showcase_tree()) {
                    _result->tree_reclick_row = tree->current_row();
                    _result->tree_reclick_selected = tree->is_row_selected(0);
                    if (demo_tree_model_t* const model = _root->showcase_tree_model()) {
                        _result->tree_reclick_node =
                            tree->current_index() == model->first_root_index();
                    }
                }
                _result->tree_reclick_events = _root->tree_expansions();
                break;
            case 34:
#if defined(_WIN32)
                // The Win32 size-move modal loop: prove the message hook paints a repair frame
                // without consuming the not-yet-dispatched input edge, and that WM_EXITSIZEMOVE
                // tears the timer down.
                if (HWND hwnd = win32_hwnd_of(*_window)) {
                    auto& input = _window->input_state();
                    input.set_button(mouse_button_flag::LEFT, false);
                    input.reset_frame();
                    input.set_button(mouse_button_flag::LEFT, true);
                    const bool edge_before = input.pressed(mouse_button_flag::LEFT);
                    const int before = _root->painted_frames();
                    SendMessage(hwnd, WM_ENTERSIZEMOVE, 0, 0);
                    SendMessage(hwnd, WM_TIMER,
                        static_cast<WPARAM>(hwshqtb::gui::window_t::size_move_timer_id), 0);
                    SendMessage(hwnd, WM_TIMER,
                        static_cast<WPARAM>(hwshqtb::gui::window_t::size_move_timer_id), 0);
                    _result->modal_frames = _root->painted_frames() - before;
                    _result->modal_input_kept =
                        edge_before && input.pressed(mouse_button_flag::LEFT);
                    SendMessage(hwnd, WM_EXITSIZEMOVE, 0, 0);
                    const int after_exit = _root->painted_frames();
                    SendMessage(hwnd, WM_TIMER,
                        static_cast<WPARAM>(hwshqtb::gui::window_t::size_move_timer_id), 0);
                    _result->modal_timer_stopped = _root->painted_frames() == after_exit;
                    _result->modal_ok = _result->modal_frames >= 1
                        && _result->modal_input_kept && _result->modal_timer_stopped;
                    // Do not leak the synthetic edge into the final frame.
                    input.set_button(mouse_button_flag::LEFT, false);
                    input.reset_frame();
                }
#else
                _result->modal_ok = true; // not applicable outside Windows
#endif
                _result->completed = true;
                core_application::quit();
                break;
            default:
                core_application::quit();
                break;
            }
            ++_step;
        }

        /// @brief Push one motion/press/release batch that clicks @p row of the showcase list.
        /// @param window_id The window the events belong to.
        /// @param row The row to click; it must be visible, so the caller reveals it first.
        /// @note The row's band is derived from the list's own row height and scroll offset: the rows
        ///       are model data, so there is no item to ask for a rectangle.
        void push_list_row_click(std::uint32_t window_id, int row) {
            list_view_t* const list = _root->showcase_list();
            if (list == nullptr || row < 0) {
                return;
            }
            const int top = row * list->row_height() - list->scroll_offset().y();
            const point_t point =
                list->map_to_global(point_t{ 20, top + list->row_height() / 2 });
            push_mouse_event(window_id, SDL_EVENT_MOUSE_MOTION, point, false);
            push_mouse_event(window_id, SDL_EVENT_MOUSE_BUTTON_DOWN, point, true);
            push_mouse_event(window_id, SDL_EVENT_MOUSE_BUTTON_UP, point, false);
        }

        /// @brief Push one motion/press/release batch that clicks cell (@p row, @p column) of the
        ///        showcase table.
        /// @param window_id The window the events belong to.
        /// @param row The row; it must be visible, so the caller reveals it first.
        /// @param column The column; its band comes from the table's own column widths.
        /// @note The point is the CENTRE of the cell: the content origin shifted by the view's scroll
        ///       offset and pushed below the header strip. The strip is a child item of the table, so
        ///       its height is the header item's own rectangle; the destination starts below it.
        void push_table_cell_click(std::uint32_t window_id, int row, int column) {
            table_view_t* const table = _root->showcase_table();
            if (table == nullptr || row < 0 || column < 0) {
                return;
            }
            const int header = table->header_item()->rect().height();
            const point_t offset = table->scroll_offset();
            const point_t point = table->map_to_global(point_t{
                table->column_left(column) + table->column_width(column) / 2 - offset.x(),
                header + row * table->row_height() + table->row_height() / 2 - offset.y() });
            push_mouse_event(window_id, SDL_EVENT_MOUSE_MOTION, point, false);
            push_mouse_event(window_id, SDL_EVENT_MOUSE_BUTTON_DOWN, point, true);
            push_mouse_event(window_id, SDL_EVENT_MOUSE_BUTTON_UP, point, false);
        }

        /// @brief Push one motion/press/release batch that clicks visible row @p row of the showcase
        ///        tree.
        /// @param window_id The window the events belong to.
        /// @param row The visible row to click: the rows are the flattened visible NODES, so row 0 is
        ///        the first root while the tree is collapsed.
        /// @note The point is the row's TEXT column at depth 0 — past the expander (padding 8 + the
        ///       12-pixel expander + padding 8 = 28) — so the press is an ordinary row press, not an
        ///       expander toggle.
        void push_tree_row_click(std::uint32_t window_id, int row) {
            tree_view_t* const tree = _root->showcase_tree();
            if (tree == nullptr || row < 0) {
                return;
            }
            const int top = row * tree->row_height() - tree->scroll_offset().y();
            const point_t point =
                tree->map_to_global(point_t{ 40, top + tree->row_height() / 2 });
            push_mouse_event(window_id, SDL_EVENT_MOUSE_MOTION, point, false);
            push_mouse_event(window_id, SDL_EVENT_MOUSE_BUTTON_DOWN, point, true);
            push_mouse_event(window_id, SDL_EVENT_MOUSE_BUTTON_UP, point, false);
        }

        /// @brief The IM control with the full (group-prefixed) cache name, or nullptr.
        push_button_t* im_button(const char* name) const {
            return static_cast<push_button_t*>(_root->im().widget("push_button_t", name));
        }

        /// @brief How many IM controls currently hold the item-level focus (their ring is visible).
        int im_focused_count() const {
            int count = 0;
            if (push_button_t* const ok = im_button("im/ok")) {
                count += ok->has_focus() ? 1 : 0;
            }
            if (push_button_t* const cancel = im_button("im/cancel")) {
                count += cancel->has_focus() ? 1 : 0;
            }
            if (push_button_t* const apply = im_button("im/apply")) {
                count += apply->has_focus() ? 1 : 0;
            }
            return count;
        }

        widget_window_t* _window = nullptr;
        demo_root_t* _root = nullptr;
        self_test_result_t* _result = nullptr;
        int _timer = 0;
        int _step = 0;
        /// @brief painted_frame_count() when the input burst was injected (frame 0 is a valid value).
        std::size_t _burst_start = 0;
    };

    /// @brief Print and score the self-test findings.
    /// @return 0 when both hosts behaved and neither stole from the other, 1 otherwise.
    int report_self_test(const self_test_result_t& result) {
        const bool retained_ok = result.retained_clicks == 1 && result.last_retained == "panel A";
        const bool im_ok = result.im_clicks == 1 && result.last_im == "OK";
        const bool no_steal = result.retained_clicks_after_im == 1
            && result.last_retained_after_im == "panel A";
        const bool focus_ok = result.focus_on_first;
        const bool row_ok = result.button_row_fills_column;
        const bool im_keys_ok = result.host_focused && result.im_first_focused
            && result.im_second_focused && result.im_third_focused && result.host_left;
        // The control showcase: every value below comes from a real input step.
        const bool controls_ok = result.checkbox_checked && result.checkbox_toggles == 1;
        const bool radio_ok = result.radio_second_checked && !result.radio_first_checked;
        const bool slider_ok = result.slider_value == 51; // clicked to 50 + one Right arrow step
        const bool text_ok = result.text_expected && result.text_edits == 1;
        const bool tabs_ok = result.tab_current == 1 && !result.tab_first_visible
            && result.tab_second_visible;
        const bool scroll_ok = result.scroll_y > 0 && result.scroll_max > 0
            && result.scroll_y <= result.scroll_max;
        // The list: the click on row 3 makes that row current and selected AND takes the focus; End
        // moves the current row to the last row and scrolls it into view (the offset has to change,
        // because the last row starts far below the viewport); a second click on the current row
        // moves nothing.
        const bool list_ok = result.list_rows > 0 && result.list_click_row == 3
            && result.list_click_selected && result.list_click_focused
            && result.list_end_row == result.list_rows - 1 && result.list_end_selected
            && result.list_end_offset > result.list_click_offset
            && result.list_reclick_row == result.list_end_row && result.list_reclick_selected;
        // The table: the click on cell (2, 1) makes row 2 current AND selected and puts the current
        // cell in column 1; Right moves the current column to 2 and leaves the row at 2; Return
        // activates the CELL — one cell_activated(2, 2) — instead of the row; a second click on the
        // same cell moves nothing and does not throw.
        const bool table_ok = result.table_click_row == 2 && result.table_click_column == 1
            && result.table_click_selected && result.table_right_row == 2
            && result.table_right_column == 2 && result.table_activations == 1
            && result.table_activated_row == 2 && result.table_activated_column == 2
            && result.table_reclick_row == 2 && result.table_reclick_column == 2
            && result.table_reclick_selected && result.table_reclick_activations == 1;
        // The tree: the click on the first root's row makes that NODE current (the model's pointer, not
        // the row number), selects the row and takes the focus; Right expands the node — the visible
        // list grows from 3 to 6, one expanded_changed, the current row stays — and Down moves to the
        // first child; the first Left climbs back to the parent because the child is collapsed (that
        // branch announced nothing), the second collapses the root (3 rows, the second
        // expanded_changed); a second click on the same row moves nothing, announces nothing and does
        // not throw.
        const bool tree_ok = result.tree_click_row == 0 && result.tree_click_selected
            && result.tree_click_node && result.tree_click_focused
            && result.tree_rows_collapsed == 3 && result.tree_rows_expanded == 6
            && result.tree_right_row == 0 && result.tree_expand_events == 1
            && result.tree_expand_flag && result.tree_down_row == 1 && result.tree_down_node
            && result.tree_left_row == 0 && result.tree_left_node && result.tree_left_still_expanded
            && result.tree_collapsed_rows == 3 && result.tree_collapse_events == 2
            && !result.tree_collapse_flag && result.tree_reclick_row == 0
            && result.tree_reclick_selected && result.tree_reclick_node
            && result.tree_reclick_events == 2;
        // The input burst must coalesce into one or two frames when the loop is phased (1 ideal,
        // 2 allowed for an extra expose/border case). Without --no-phase the measurement is still
        // reported, but it is not required to hold.
        const bool burst_ok = result.burst_frames >= 0
            && (!result.phased || (result.burst_frames >= 1 && result.burst_frames <= 2));
        const bool modal_ok = result.modal_ok;
        const bool passed = result.completed && retained_ok && im_ok && no_steal && focus_ok
            && row_ok && im_keys_ok && controls_ok && radio_ok && slider_ok && text_ok && tabs_ok
            && scroll_ok && list_ok && table_ok && tree_ok && burst_ok && modal_ok;

        std::printf("[ui_demo] self-test: retained clicks %d (last '%s'), immediate clicks %d (last '%s')\n",
                    result.retained_clicks, result.last_retained.c_str(), result.im_clicks,
                    result.last_im.c_str());
        std::printf("[ui_demo] self-test: retained state after the immediate click: %d click(s), last '%s'; "
                    "Tab focus on the first retained button: %s; Tab row fills the column: %s\n",
                    result.retained_clicks_after_im, result.last_retained_after_im.c_str(),
                    result.focus_on_first ? "yes" : "no", result.button_row_fills_column ? "yes" : "no");
        std::printf("[ui_demo] self-test: IM keyboard: host focused %s, inner ok/cancel/apply %s/%s/%s, "
                    "host left %s\n",
                    result.host_focused ? "yes" : "no", result.im_first_focused ? "yes" : "no",
                    result.im_second_focused ? "yes" : "no", result.im_third_focused ? "yes" : "no",
                    result.host_left ? "yes" : "no");
        std::printf("[ui_demo] self-test: controls=1? %s radio=1? %s slider=%d text=1? %s tabs=1? %s "
                    "scroll=%d/%d\n",
                    controls_ok ? "yes" : "no", radio_ok ? "yes" : "no", result.slider_value,
                    text_ok ? "yes" : "no", tabs_ok ? "yes" : "no", result.scroll_y,
                    result.scroll_max);
        std::printf("[ui_demo] self-test: input burst %d motion event(s) -> %d frame(s), phase %s\n",
                    result.burst_events, result.burst_frames, result.phased ? "on" : "off");
        std::printf("[ui_demo] self-test: list: click row 3 -> current %d (selected %s, focused %s), "
                    "End -> row %d/%d (selected %s), offset %d -> %d, re-click row %d (selected %s)\n",
                    result.list_click_row, result.list_click_selected ? "yes" : "no",
                    result.list_click_focused ? "yes" : "no", result.list_end_row,
                    result.list_rows > 0 ? result.list_rows - 1 : -1,
                    result.list_end_selected ? "yes" : "no", result.list_click_offset,
                    result.list_end_offset, result.list_reclick_row,
                    result.list_reclick_selected ? "yes" : "no");
        std::printf("[ui_demo] self-test: table: click cell (2,1) -> current (%d,%d) (row selected %s), "
                    "Right -> (%d,%d), Return -> %d activation(s) of (%d,%d), re-click (2,2) -> (%d,%d) "
                    "(selected %s), activations %d\n",
                    result.table_click_row, result.table_click_column,
                    result.table_click_selected ? "yes" : "no", result.table_right_row,
                    result.table_right_column, result.table_activations,
                    result.table_activated_row, result.table_activated_column,
                    result.table_reclick_row, result.table_reclick_column,
                    result.table_reclick_selected ? "yes" : "no",
                    result.table_reclick_activations);
        std::printf("[ui_demo] self-test: tree: click row 0 -> current row %d (node is the first root %s, "
                    "selected %s, focused %s), rows %d; Right -> rows %d (row %d, expansions %d, last "
                    "expanded %s); Down -> row %d (node is the first child %s); Left -> row %d (node is "
                    "the first root %s, still expanded %s); Left -> rows %d (expansions %d, last expanded "
                    "%s); re-click row %d (selected %s, node is the first root %s, expansions %d)\n",
                    result.tree_click_row, result.tree_click_node ? "yes" : "no",
                    result.tree_click_selected ? "yes" : "no", result.tree_click_focused ? "yes" : "no",
                    result.tree_rows_collapsed, result.tree_rows_expanded, result.tree_right_row,
                    result.tree_expand_events, result.tree_expand_flag ? "yes" : "no",
                    result.tree_down_row, result.tree_down_node ? "yes" : "no", result.tree_left_row,
                    result.tree_left_node ? "yes" : "no", result.tree_left_still_expanded ? "yes" : "no",
                    result.tree_collapsed_rows, result.tree_collapse_events,
                    result.tree_collapse_flag ? "yes" : "no", result.tree_reclick_row,
                    result.tree_reclick_selected ? "yes" : "no",
                    result.tree_reclick_node ? "yes" : "no", result.tree_reclick_events);
#if defined(_WIN32)
        std::printf("[ui_demo] self-test: modal pump -> %d frame(s), input edge kept %s, "
                    "timer stopped %s\n",
                    result.modal_frames, result.modal_input_kept ? "yes" : "no",
                    result.modal_timer_stopped ? "yes" : "no");
#endif
        std::printf("[ui_demo] self-test: %s (retained=%d im=%d no-steal=%d focus=%d row-width=%d im-keys=%d "
                    "controls=%d radio=%d slider=%d text=%d tabs=%d scroll=%d list=%d burst=%d/%d "
                    "modal=%d table=%d tree=%d)\n",
                    passed ? "PASS" : "FAIL", retained_ok ? 1 : 0, im_ok ? 1 : 0, no_steal ? 1 : 0,
                    focus_ok ? 1 : 0, row_ok ? 1 : 0, im_keys_ok ? 1 : 0, controls_ok ? 1 : 0,
                    radio_ok ? 1 : 0, result.slider_value, text_ok ? 1 : 0, tabs_ok ? 1 : 0,
                    scroll_ok ? 1 : 0, list_ok ? 1 : 0, result.burst_frames, result.burst_events,
                    modal_ok ? 1 : 0, table_ok ? 1 : 0, tree_ok ? 1 : 0);
        return passed ? 0 : 1;
    }

    /// @brief Score a --wheel-burst / --table-burst run.
    /// @return true when exactly one frame painted the capture batch, the burst scrolled the expected
    ///         number of pixels, and the frame's pixels were written.
    bool report_wheel_burst(const wheel_burst_result_t& result) {
        const char* const kind =
            result.target == burst_target_enum::TABLE ? "table burst" : "wheel burst";
        const int delta = result.offset_after - result.offset_primed;
        // One wheel tick scrolls default_style_t's SCROLL_WHEEL_STEP pixels (16, the same step the
        // frame analysis uses). The table burst scrolls further down, the scroll area burst scrolls
        // back up. The primed offset must leave room for the whole burst, so no tick is clamped away.
        const int expected_delta =
            result.target == burst_target_enum::TABLE ? result.ticks * 16 : -result.ticks * 16;
        const bool offsets_ok = result.offset_primed > 0 && delta == expected_delta;
        const bool frames_ok = result.prime_frames == 1 && result.burst_frames == 1;
        const bool passed = result.completed && result.captured && offsets_ok && frames_ok;
        std::printf("[ui_demo] %s: offset %d -> %d (delta %d), frames prime/burst %d/%d, "
                    "captured %s\n",
                    kind, result.offset_primed, result.offset_after, delta, result.prime_frames,
                    result.burst_frames, result.captured ? "yes" : "no");
        std::printf("[ui_demo] %s: %s\n", kind, passed ? "PASS" : "FAIL");
        return passed;
    }

    /// @brief Drives the persistent window from timers on the library event loop: one timer asks the
    ///        root widget for a repaint (proving the widget -> window sink path), an optional one-shot
    ///        timer closes the window (--duration).
    class demo_pump_t final: public object_t {
    public:
        demo_pump_t(widget_t& root, int duration_ms):
            _root(&root) {
            _repaint_timer = start_timer(250);
            if (duration_ms > 0) {
                _duration_timer = start_timer(duration_ms, false);
            }
        }

    protected:
        void timer_event(timer_event_t* event) override {
            if (event == nullptr || _root == nullptr) {
                return;
            }
            if (_duration_timer != 0 && event->timer_id() == _duration_timer) {
                std::printf("[ui_demo] duration elapsed, closing\n");
                core_application::quit();
                return;
            }
            if (event->timer_id() == _repaint_timer) {
                _root->update();   // widget_t::update() -> sink -> widget_window_t -> window_t::update()
            }
        }

    private:
        widget_t* _root = nullptr;
        int _repaint_timer = 0;
        int _duration_timer = 0;
    };
}

int main(int argc, char** argv) {
    options_t options;
    if (!parse_options(argc, argv, options)) {
        print_usage();
        return 2;
    }

    try {
        // Text determinism, decided BEFORE any control exists: the hash-gated capture pins the built-in
        // face, so its pixels never depend on the machine's installed fonts. The interactive run and
        // --screenshot-font use the platform UI face instead, and only those two build the CJK column.
        const bool system_font = !options.self_test && options.screenshot_path.empty();
        if (system_font) {
            // An empty family means "the platform default", i.e. the system UI face.
            hwshqtb::gui::font_database_t::instance().set_default_family(std::string_view{});
        }
        else {
            hwshqtb::gui::pin_builtin_font_family();
        }
        // The application owns the object model and the event loop; the platform side (video and the
        // native event pump) lives in the inner scope, so every window is gone before
        // ~gui_application_t stops SDL and the platform side is gone before ~core_application.
        core_application application{ argc, argv };
        int exit_code = 0;
        {
            gui_application_t gui;
            const std::string driver{ gui_application_t::video_driver_name() };
            std::printf("[ui_demo] video driver: %s\n", driver.empty() ? "(none)" : driver.c_str());
            // Phased mode is the default for the demo. The application snapshots this policy when
            // exec() starts and hands it to the loop; --no-phase restores event-by-event processing.
            application.set_event_phase(options.phase
                    ? std::optional<event_phase_t>{ event_phase_t{} }
                    : std::nullopt);
            std::printf("[ui_demo] event phase: %s\n", options.phase ? "on" : "off");

            // --size <W>x<H> overrides the default client extent (0x0 means "not given"). The height
            // is what the third column needs for SIX bands: a shortage would shrink them in proportion
            // to the room above their floors (the biggest first once the floors no longer fit), and
            // the new tree band would lose its rows. At 1280 every band keeps a viewport well above
            // its floor, so the tree is visible in the first frame like the list and the table.
            const extent_t window_size = options.window_size.width() > 0
                ? options.window_size
                : extent_t{ 920, 1280 };
            widget_window_t window{ "ui_demo", window_size };
            auto* const root = new demo_root_t;
            build_tree(*root, system_font);

            window.set_root_widget(root);   // takes ownership; root fills the client area

            // The CJK panel's line edit takes the focus, so its caret is part of the frame. The caret is
            // the visible proof that its x is the advance of the text before it: only the runs that use
            // the system font build this panel, so the hash-gated --screenshot has neither caret nor focus.
            if (widget_t* const cjk = root->cjk_column(); cjk != nullptr && window.router() != nullptr) {
                for (std::size_t index = 0; index < cjk->item_count(); ++index) {
                    if (auto* const edit = dynamic_cast<line_edit_t*>(cjk->item_at_index(index))) {
                        window.router()->set_focus(edit);
                        break;
                    }
                }
            }

            std::printf("[ui_demo] root widget %dx%d with %zu item child(ren); %zu registered type(s)\n",
                        root->size().width(), root->size().height(), root->item_count(),
                        object_t::registered_type_names().size());

            if (options.self_test) {
                // The proof runs on the real event loop: the driver's timers inject the event batches
                // and the loop pumps them through the installed native filter chain.
                self_test_result_t result;
                result.phased = options.phase;
                self_test_driver_t self_test{ window, *root, result };
                const int loop_result = core_application::exec();
                if (!options.screenshot_path.empty()) {
                    // Optional: capture the final frame, so the interaction state (selected panel,
                    // focus ring) can be inspected offline. Frame N, not the deterministic frame 0.
                    window.save_screenshot(options.screenshot_path);
                    std::printf("[ui_demo] self-test: wrote %s\n", options.screenshot_path.c_str());
                }
                exit_code = report_self_test(result);
                if (loop_result != 0) {
                    std::printf("[ui_demo] self-test: event loop returned %d\n", loop_result);
                }
            }
            else if (options.wheel_burst > 0 || options.table_burst > 0) {
                // One deterministic burst: paint the base frame synchronously (a fresh backing surface
                // has no contents), then let the timer driver inject the batch and capture the frame
                // the loop paints for it. The capture reads the presented pixels without repainting.
                const bool table = options.table_burst > 0;
                const int ticks = table ? options.table_burst : options.wheel_burst;
                window.repaint();
                const std::string path = options.screenshot_path.empty()
                    ? (table ? "build/table_burst_" : "build/burst_") + std::to_string(ticks) + ".bmp"
                    : options.screenshot_path;
                wheel_burst_result_t result;
                wheel_burst_driver_t driver{ window, *root, ticks, options.wheel_burst_content,
                                             table ? burst_target_enum::TABLE
                                                   : burst_target_enum::SCROLL_VIEW,
                                             path, result };
                exit_code = core_application::exec();
                if (!report_wheel_burst(result)) {
                    exit_code = 1;
                }
            }
            else if (!options.screenshot_font_path.empty()) {
                // The SAME interface, drawn with the platform's UI font: the deterministic capture above
                // is a hash gate, so it must never depend on the machine, while this one exists to prove
                // that a platform face really resolves and rasterises.
                window.save_screenshot(options.screenshot_font_path);
                const std::filesystem::path absolute =
                    std::filesystem::absolute(std::filesystem::path{ options.screenshot_font_path });
                std::printf("[ui_demo] wrote %s (%dx%d, frame 0, system font '%s')\n",
                            absolute.string().c_str(), window.size().width(), window.size().height(),
                            std::string(hwshqtb::gui::font_database_t::instance().default_family()).c_str());
                // The CJK column's global rectangle: the caller crops exactly this region out of the BMP
                // and counts its ink, which is the only machine-independent proof that the platform face
                // really painted the characters (a wrong/empty face would leave the region blank).
                if (widget_t* const cjk = root->cjk_column()) {
                    const point_t origin = cjk->map_to_global(point_t{});
                    std::printf("[ui_demo] cjk panel: x=%d y=%d w=%d h=%d\n", origin.x(), origin.y(),
                                cjk->rect().width(), cjk->rect().height());
                    // Per-child geometry and the metrics that produced it: the panel's texts must fit
                    // their own widget rectangles, and this is the evidence, printed by the process that
                    // painted them.
                    const hwshqtb::gui::font_metrics_t metrics{};
                    std::printf("[ui_demo] metrics: ascent=%d descent=%d line_height=%d "
                                "advance(U+4E2D)=%d extent(4 CJK)=%d\n",
                                metrics.ascent(), metrics.descent(), metrics.line_height(),
                                metrics.advance(0x4E2D),
                                metrics.text_extent("\xE4\xB8\xAD\xE6\x96\x87\xE6\xB5\x8B\xE8\xAF\x95").width());
                    for (std::size_t index = 0; index < cjk->item_count(); ++index) {
                        const auto* const child = cjk->item_at_index(index);
                        if (child == nullptr) {
                            continue;
                        }
                        const point_t child_origin = child->map_to_global(point_t{});
                        std::printf("[ui_demo]   cjk child %zu: x=%d y=%d w=%d h=%d\n", index,
                                    child_origin.x(), child_origin.y(), child->rect().width(),
                                    child->rect().height());
                    }
                    // The immediate-mode host's inner controls (OK / Cancel / Apply): the same rectangle
                    // evidence, so a caller can crop each caption out of the BMP and check it.
                    for (std::size_t index = 0; index < root->im().item_count(); ++index) {
                        const auto* const child = root->im().item_at_index(index);
                        if (child == nullptr) {
                            continue;
                        }
                        const point_t child_origin = child->map_to_global(point_t{});
                        std::printf("[ui_demo]   im child %zu: x=%d y=%d w=%d h=%d\n", index,
                                    child_origin.x(), child_origin.y(), child->rect().width(),
                                    child->rect().height());
                    }
                    // The showcase column and its bands (list / table / tree): the same rectangle
                    // evidence, so a caller can crop the title band and row 0 out of the BMP.
                    for (std::size_t i = 0; i < root->item_count(); ++i) {
                        const auto* const child = root->item_at_index(i);
                        if (child == nullptr) {
                            continue;
                        }
                        const point_t o = child->map_to_global(point_t{});
                        std::printf("[ui_demo] root child %zu: x=%d y=%d w=%d h=%d children=%zu\n", i,
                                    o.x(), o.y(), child->rect().width(), child->rect().height(),
                                    child->item_count());
                        for (std::size_t j = 0; j < child->item_count(); ++j) {
                            const auto* const grandchild = child->item_at_index(j);
                            if (grandchild == nullptr) {
                                continue;
                            }
                            const point_t g = grandchild->map_to_global(point_t{});
                            std::printf("[ui_demo]   child %zu.%zu: x=%d y=%d w=%d h=%d\n", i, j, g.x(),
                                        g.y(), grandchild->rect().width(), grandchild->rect().height());
                        }
                    }
                }
            }
            else if (!options.screenshot_path.empty()) {
                // Deterministic single frame: save_screenshot() paints frame 0 with a fresh painter,
                // reads the pixels back and writes the BMP. No loop, no show() and no interaction.
                window.save_screenshot(options.screenshot_path);
                const std::filesystem::path absolute =
                    std::filesystem::absolute(std::filesystem::path{ options.screenshot_path });
                std::printf("[ui_demo] wrote %s (%dx%d, frame 0, retained clicks %d, immediate clicks %d)\n",
                            absolute.string().c_str(), window.size().width(), window.size().height(),
                            root->retained_clicks(), root->im_clicks());
            } else {
                demo_pump_t pump{ *root, options.duration_ms };
                window.show();
                exit_code = core_application::exec();
                std::printf(
                    "[ui_demo] event loop returned %d; painted %d frame(s), retained clicks %d, "
                    "immediate clicks %d\n",
                    exit_code, root->painted_frames(), root->retained_clicks(), root->im_clicks());
            }
        }
        return exit_code;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ui_demo: %s\n", error.what());
        return 1;
    }
}
