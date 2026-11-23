// The REAL path of the retained host: SDL events -> window_t's native filter -> input_state_t ->
// widget_window_t's filter -> input_router_t::dispatch -> the item's interact(), and the frame:
// window_t::draw_frame() calling paint_event() once per damaged rectangle ->
// widget_window_t::paint_event -> root->paint() -> the container -> list_view_t::paint().
//
// The sections below pin the two list_view defects that only show up here, and assert them on the
// pixels a user sees:
//
//  (1) a fast hover sweep between rows. The motions are pushed as REAL SDL_EVENT_MOUSE_MOTION
//      events, one event-loop phase consumes them all (no repaint in between), and the single frame
//      that follows must leave exactly ONE hovered row, the one under the pointer.
//  (2) scrolling. Wheel events go through the same path; after the incremental frame the pixels must
//      be identical to a full repaint of the same state, byte for byte, including a burst of steps
//      with no frame in between.
//
// (3) pins the wheel outside the rows, (4) walks motions and wheels in an arbitrary (but seeded, so
// reproducible) order, and (5) covers the hovered row after a scroll that is not the wheel. (6) is the
// same fast path with NO input event at all: the scroll record alone must schedule the frame. (7)
// repeats the scroll comparison on a table_view_t, whose content is narrower than its viewport: the
// view's own frame lies in the band no row covers, so a fast path that shifts it leaves a stray
// frame-coloured line there.
//
// The device pixels come from a mirror of the window's passes: the subclass replays every pass the
// window paints (same clip, same clear, same tree) into an image_t and copies that rectangle into its
// own pixel buffer, which the scroll fast path can shift. That is the construction a host with a
// read-back surface would have; the list side is untouched.
//
// The window is real (window_t owns an SDL window, a renderer and a backing surface) and runs under
// SDL's dummy video driver, so no display is needed and the frames are deterministic.

#include <SDL3/SDL.h>

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
#include "hwshqtb/gui/gui_application_t.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/gui/region_t.h"
#include "hwshqtb/metaobject/any_t.h"
#include "hwshqtb/metaobject/core_application.h"
#include "hwshqtb/metaobject/event_dispatcher.h"
#include "hwshqtb/ui/item_model_t.h"
#include "hwshqtb/ui/list_view_t.h"
#include "hwshqtb/ui/style_t.h"
#include "hwshqtb/ui/table_view_t.h"
#include "hwshqtb/ui/widget_t.h"
#include "hwshqtb/ui/widget_window_t.h"

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;
using namespace hwshqtb::ui;

namespace real_path_test {

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

/// The style colours the list paints with (default_style_t's LIST_ROW and its variants).
constexpr color_t row_fill_color{ 30, 42, 56 };
constexpr color_t row_hover_fill_color{ 40, 55, 70 };
constexpr color_t row_selected_hover_fill_color{ 58, 128, 186 };

/// The window and the list under test.
constexpr extent_t window_extent{ 220, 200 };
constexpr rect_t list_rect{ 10, 12, 200, 180 };
constexpr int row_height = 20;
constexpr int model_rows = 100;

/// The column the pixel assertions read: inside the viewport and right of the row text.
constexpr int sample_x = 100;

/// A model whose rows are "row 0", "row 1", ...: the list's data, not items of the tree.
class probe_model_t final: public item_model_t {
public:
    explicit probe_model_t(int rows = 0) {
        set_rows(rows);
    }

    int row_count() const override {
        return _rows;
    }

    any_t data(int row, item_role_enum role) const override {
        if (row < 0 || row >= _rows || role != item_role_enum::DISPLAY) {
            return {};
        }
        return any_t(std::string_view(_texts[static_cast<std::size_t>(row)]));
    }

    void set_rows(int rows) {
        _rows = std::max(0, rows);
        _texts.clear();
        _texts.reserve(static_cast<std::size_t>(_rows));
        for (int row = 0; row < _rows; ++row) {
            _texts.push_back("row " + std::to_string(row));
        }
    }

private:
    int _rows = 0;
    std::vector<std::string> _texts;
};

/// A four-column model for the table section: each cell is "r<row>c<column>", each column has a
/// short header title. The cells own their strings, so data() can answer a view into them.
class probe_table_model_t final: public item_model_t {
public:
    probe_table_model_t(int rows, int columns):
        _rows(std::max(0, rows)),
        _columns(std::max(0, columns)) {
        _cells.reserve(static_cast<std::size_t>(_rows) * static_cast<std::size_t>(_columns));
        for (int row = 0; row < _rows; ++row) {
            for (int column = 0; column < _columns; ++column) {
                _cells.push_back("r" + std::to_string(row) + "c" + std::to_string(column));
            }
        }
        for (int column = 0; column < _columns; ++column) {
            _headers.push_back("h" + std::to_string(column));
        }
    }

    int row_count() const override {
        return _rows;
    }

    int column_count() const override {
        return _columns;
    }

    any_t data(int row, int column, item_role_enum role) const override {
        if (row < 0 || row >= _rows || column < 0 || column >= _columns
            || role != item_role_enum::DISPLAY) {
            return {};
        }
        const auto index = static_cast<std::size_t>(row) * static_cast<std::size_t>(_columns)
            + static_cast<std::size_t>(column);
        return any_t(std::string_view(_cells[index]));
    }

    any_t header_data(int column, item_role_enum role) const override {
        if (column < 0 || column >= _columns || role != item_role_enum::DISPLAY) {
            return {};
        }
        return any_t(std::string_view(_headers[static_cast<std::size_t>(column)]));
    }

private:
    int _rows = 0;
    int _columns = 0;
    std::vector<std::string> _cells;
    std::vector<std::string> _headers;
};

/// A root widget that fills its whole rectangle: the frame's white clear never survives it.
class fill_widget_t final: public widget_t {
public:
    explicit fill_widget_t(color_t color, widget_t* parent = nullptr):
        widget_t(parent),
        _color(color) {
        set_opaque(true);
    }

protected:
    void paint_event(paint_event_t* event) override {
        painter_t& painter = event->painter();
        painter.set_color(_color);
        painter.fill_rect(rect());
    }

private:
    color_t _color;
};

/// The pixels a host keeps across frames: a window's client area as a buffer that can be painted
/// rectangle by rectangle and shifted by the scroll fast path.
class pixel_screen_t {
public:
    explicit pixel_screen_t(extent_t size):
        _size(size),
        _pixels(static_cast<std::size_t>(size.width()) * static_cast<std::size_t>(size.height()),
                color_t::white_color()) { }

    color_t pixel(int x, int y) const {
        if (x < 0 || y < 0 || x >= _size.width() || y >= _size.height()) {
            return color_t::transparent_color();
        }
        return at(x, y);
    }

    /// @brief Take over @p area's pixels of @p pass (one painted frame pass).
    void copy_rect(const image_t& pass, rect_t area) {
        const rect_t clipped = area.intersected(rect_t{ point_t{}, _size });
        for (int y = clipped.y(); y < clipped.bottom(); ++y) {
            for (int x = clipped.x(); x < clipped.right(); ++x) {
                set(x, y, pass.pixel(x, y));
            }
        }
    }

    /// @brief Shift the pixels inside @p rect by @p dy, leaving the uncovered band untouched.
    /// @note The shift is refused (nothing moves) for |@p dy| >= the height, which is what
    ///       backing_store_t::scroll() does: the caller then repaints the whole rectangle.
    void shift(rect_t rect, int dy) {
        const rect_t area = rect.intersected(rect_t{ point_t{}, _size });
        if (area.isEmpty() || dy == 0 || dy <= -area.height() || dy >= area.height()) {
            return;
        }
        const bool downwards = dy > 0;
        for (int step = 0; step < area.height(); ++step) {
            const int y = downwards ? area.bottom() - 1 - step : area.y() + step;
            const int source = y - dy;
            if (source < area.y() || source >= area.bottom()) {
                continue;
            }
            for (int x = area.x(); x < area.right(); ++x) {
                set(x, y, at(x, source));
            }
        }
    }

    /// @brief How many pixels differ from the full repaint stored in @p image.
    int differences(const image_t& image) const {
        int count = 0;
        for (int y = 0; y < _size.height(); ++y) {
            for (int x = 0; x < _size.width(); ++x) {
                if (!(pixel(x, y) == image.pixel(x, y))) {
                    ++count;
                }
            }
        }
        return count;
    }

    /// @brief Print where and how the pixels differ from @p image (diagnostics for a failure).
    void dump_differences(const image_t& image, const char* what) const {
        int left = _size.width();
        int top = _size.height();
        int right = 0;
        int bottom = 0;
        const int limit = 12;
        int printed = 0;
        for (int y = 0; y < _size.height(); ++y) {
            for (int x = 0; x < _size.width(); ++x) {
                if (pixel(x, y) == image.pixel(x, y)) {
                    continue;
                }
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x + 1);
                bottom = std::max(bottom, y + 1);
                if (printed < limit) {
                    const color_t actual = pixel(x, y);
                    const color_t expected = image.pixel(x, y);
                    std::printf("       %s: (%d, %d) on screen %d,%d,%d,%d expected %d,%d,%d,%d\n",
                                what, x, y, actual.red(), actual.green(), actual.blue(),
                                actual.alpha(), expected.red(), expected.green(), expected.blue(),
                                expected.alpha());
                    ++printed;
                }
            }
        }
        if (printed > 0) {
            std::printf("       %s: differing pixels span (%d, %d) - (%d, %d)\n", what, left, top,
                        right, bottom);
        }
    }

private:
    color_t at(int x, int y) const {
        return _pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(_size.width())
                       + static_cast<std::size_t>(x)];
    }

    void set(int x, int y, color_t color) {
        _pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(_size.width())
                + static_cast<std::size_t>(x)] = color;
    }

    extent_t _size;
    std::vector<color_t> _pixels;
};

/// The host, with the tree's scroll request observed: it does its own work and the mirror replays the
/// same pixel move. Overriding the host's event function replaces the old trick of REPLACING its scroll
/// sink — the request channel is one object, so an observer forwards instead.
class mirroring_host_t final: public widget_window_t {
public:
    explicit mirroring_host_t(::hwshqtb::gui::window_t& window): widget_window_t(window) { }

    /// @brief The mirror that replays the pixel move; not owned, null until adopt().
    pixel_screen_t* screen = nullptr;

    bool event(::hwshqtb::metaobject::event_t* request) override {
        if (request != nullptr && request->type() == ::hwshqtb::metaobject::event_type_enum::UI_SCROLL) {
            const auto* const scroll = static_cast<const scroll_event_t*>(request);
            const bool handled = widget_window_t::event(request);
            if (screen != nullptr && scroll->dy() != 0) {
                screen->shift(scroll->area(), scroll->dy());
            }
            return handled;
        }
        return widget_window_t::event(request);
    }
};

/// A real window whose passes are mirrored into readable pixels. The item-tree host is composed into
/// it: the frame policy is the window's, the tree painting is the host's.
class real_window_t final: public window_t {
public:
    real_window_t(std::string_view title, extent_t size):
        window_t(title, size),
        host(*this),
        _screen(size) { }

    /// @brief The item-tree host this window composes.
    mirroring_host_t host;

    /// @brief Adopt @p root; the host's mirrored copy of the scroll request replays the pixel move.
    /// @param root The widget tree; the host takes ownership.
    void adopt(widget_t* root) {
        _root = root;
        host.set_root_widget(root);
        host.screen = &_screen;
    }

    /// @brief Adopt @p root with the VIEW's own scroll sink: the unmodified production fast path.
    /// @param root The widget tree; the host takes ownership.
    /// @note adopt() wraps the sink to replay the pixel move on the mirror; this one installs nothing,
    ///       so the view records the damage and schedules the frame itself. The caller replays the
    ///       surface shift on the mirror with replay_surface_shift().
    void adopt_production(widget_t* root) {
        _root = root;
        host.set_root_widget(root);
    }

    /// @brief Replay on the mirror the pixel move window_t::scroll() performs on the real surface.
    /// @param area The scrolled rectangle, in window coordinates (the one the container shifted).
    /// @param dy The vertical shift.
    void replay_surface_shift(rect_t area, int dy) {
        _screen.shift(area, dy);
    }

    /// @brief The pixels of this window's client area.
    const pixel_screen_t& screen() const noexcept {
        return _screen;
    }

    /// @brief How many frame passes the mirror replayed (one per damaged rectangle).
    int pass_count() const noexcept {
        return _passes;
    }

    /// @brief The damaged rectangles the LAST frame painted, one per pass.
    const std::vector<rect_t>& last_frame_rects() const noexcept {
        return _last_frame_rects;
    }

protected:
    void paint_event(paint_event_t* event) override {
        painter_t& painter = event->painter();
        host.render(painter); // the real frame pass: the path under test
        const rect_t area = painter.clip_rect().intersected(rect_t{ point_t{}, size() });
        if (area.isEmpty() || _root == nullptr) {
            return;
        }
        // draw_frame() bumps the frame counter AFTER the passes, so a different count is the start
        // of a new frame: the record then describes exactly this frame's damage.
        if (painted_frame_count() != _recorded_frame) {
            _recorded_frame = painted_frame_count();
            _last_frame_rects.clear();
        }
        _last_frame_rects.push_back(area);
        image_t pass{ size() };
        painter_t mirror{ pass };
        if (!mirror.begin()) {
            return;
        }
        mirror.set_clip(area);
        mirror.clear(area, color_t::white_color());
        _root->render(mirror);
        mirror.end();
        _screen.copy_rect(pass, area);
        ++_passes;
    }

    void interact_event(interact_event_t* event) override {
        host.interact(event->step());
    }

    void focus_out_event(focus_out_event_t*) override {
        host.focus_lost();
    }

    void leave_event(leave_event_t*) override {
        host.pointer_left();
    }

private:
    widget_t* _root = nullptr;
    pixel_screen_t _screen;
    int _passes = 0;
    std::size_t _recorded_frame = 0;
    std::vector<rect_t> _last_frame_rects;
};

/// @brief Paint the whole tree into a fresh image: the "same state, fully repainted" reference.
image_t full_repaint(widget_t& root, extent_t size) {
    image_t image{ size };
    painter_t painter{ image };
    painter.begin();
    painter.clear(color_t::white_color());
    root.render(painter);
    painter.end();
    return image;
}

/// @brief Run the real event loop until a phase dispatches nothing.
/// @param window The window to settle.
/// @param painted Receives the number of frames painted while settling.
/// @return The number of phases that ran.
/// @note A phase pumps ALL the system events that are available before it dispatches the posted
///       queue, so a batch of pushed motions is delivered first and the paints their update() calls
///       posted are dispatched last: that is the demo's frame-per-batch behaviour.
int settle(window_t& window, int& painted, int maximum_phases = 64) {
    event_phase_t phase;
    phase.merge = false; // deliver every pushed event, not one accumulated state
    phase.paint_last = true;
    painted = 0;
    const std::size_t before = window.painted_frame_count();
    int phases = 0;
    for (; phases < maximum_phases; ++phases) {
        const bool dispatched =
            event_dispatcher::instance().process_phase(process_events_type_flag::ALL_EVENTS, phase);
        if (!dispatched) {
            break;
        }
    }
    painted = static_cast<int>(window.painted_frame_count() - before);
    return phases;
}

/// @brief Push one SDL mouse event for @p window.
bool push_mouse(window_t& window, std::uint32_t type, point_t position, bool down) {
    SDL_Event event{};
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.windowID = window.window_id();
        event.motion.x = static_cast<float>(position.x());
        event.motion.y = static_cast<float>(position.y());
        event.motion.state = 0;
    }
    else {
        event.button.windowID = window.window_id();
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = down;
        event.button.clicks = 1;
        event.button.x = static_cast<float>(position.x());
        event.button.y = static_cast<float>(position.y());
    }
    return SDL_PushEvent(&event);
}

/// @brief Push one SDL wheel event (whole ticks) for @p window.
bool push_wheel(window_t& window, int ticks) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.windowID = window.window_id();
    event.wheel.x = 0.0f;
    event.wheel.y = static_cast<float>(ticks);
    event.wheel.integer_x = 0;
    event.wheel.integer_y = ticks;
    return SDL_PushEvent(&event);
}

/// @brief Push a key press and release of @p key for @p window.
bool push_key(window_t& window, int key) {
    bool pushed = true;
    for (const std::uint32_t type : { SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP }) {
        SDL_Event event{};
        event.type = type;
        event.key.windowID = window.window_id();
        event.key.key = static_cast<SDL_Keycode>(key);
        event.key.down = type == SDL_EVENT_KEY_DOWN;
        pushed = SDL_PushEvent(&event) && pushed;
    }
    return pushed;
}

/// @brief The window point at the middle of the screen band that content row @p row occupies.
/// @param offset The list's scroll offset the caller expects (the test states it explicitly so a
///        wrong offset shows up as a wrong sample point, not as a silently correct one).
point_t row_point(const list_view_t& list, int row, int offset) {
    return list.map_to_global(point_t{ list.rect().width() - 30,
                                       row * list.row_height() - offset + list.row_height() / 2 });
}

/// @brief The list's viewport in WINDOW coordinates: its rectangle minus the visible scroll bar.
/// @param list The list; its geometry must be in window coordinates already (a root-level list).
rect_t viewport_in_window(const list_view_t& list) {
    int width = list.rect().width();
    const item_t* const bar = list.item_count() > 0 ? list.item_at_index(0) : nullptr;
    if (bar != nullptr && bar->is_visible()) {
        width = std::max(0, bar->geometry().x());
    }
    return rect_t{ list.map_to_global(point_t{}), extent_t{ width, list.rect().height() } };
}

/// @brief The window y of the top edge of content row @p row's band at @p offset.
int row_band_y(const list_view_t& list, int row, int offset) {
    return list.map_to_global(point_t{}).y() + row * list.row_height() - offset;
}

/// @brief The content row under @p window_point, computed the way a user sees it: the list's own
///        geometry, its scroll bar's band, the row height and the scroll offset. -1 outside the rows.
int row_under_pointer(const list_view_t& list, point_t window_point) {
    const point_t local = window_point - list.map_to_global(point_t{});
    // The bar owns the band on the right: a point there is over no row.
    int viewport_width = list.rect().width();
    const item_t* const bar = list.item_count() > 0 ? list.item_at_index(0) : nullptr;
    if (bar != nullptr && bar->is_visible()) {
        viewport_width = std::max(0, bar->geometry().x());
    }
    if (local.x() < 0 || local.x() >= viewport_width || local.y() < 0
        || local.y() >= list.rect().height()) {
        return -1;
    }
    const int content_y = local.y() + list.scroll_offset().y();
    if (content_y < 0) {
        return -1;
    }
    const int row = content_y / list.row_height();
    const int count = list.model() != nullptr ? list.model()->row_count() : 0;
    return row < count ? row : -1;
}

/// @brief The content rows whose band shows a hover fill on @p window's pixels.
/// @note A selected row paints the "selected and hovered" colour instead of the plain one, so both
///       count as hovered here: the assertion is "this row shows the pointer feedback", not "this row
///       shows one particular colour".
std::vector<int> painted_hover_rows(const real_window_t& window, const list_view_t& list) {
    std::vector<int> rows;
    const int origin_y = list.map_to_global(point_t{}).y();
    const int offset = list.scroll_offset().y();
    const int count = list.model() != nullptr ? list.model()->row_count() : 0;
    for (int row = 0; row < count; ++row) {
        const int top = std::max(row_band_y(list, row, offset), origin_y);
        const int bottom = std::min(row_band_y(list, row, offset) + list.row_height(),
                                   origin_y + list.rect().height());
        if (bottom - top < 6) {
            continue; // too thin a sliver to sample (the row is nearly scrolled out)
        }
        const color_t colour = window.screen().pixel(sample_x, top + (bottom - top) / 2);
        if (colour == row_hover_fill_color || colour == row_selected_hover_fill_color) {
            rows.push_back(row);
        }
    }
    return rows;
}

/// The frame colour default_style_t's SCROLL_VIEW_VIEWPORT draws around an item view.
constexpr color_t viewport_frame_color{ 70, 84, 98 };
/// The fill default_style_t's SCROLL_VIEW_VIEWPORT paints behind the rows.
constexpr color_t viewport_fill_color{ 30, 42, 56 };

/// @brief A table's viewport in WINDOW coordinates: its rectangle minus the vertical bar and the
///        header strip.
rect_t table_viewport_in_window(const table_view_t& table) {
    const point_t origin = table.map_to_global(point_t{});
    int width = table.rect().width();
    // The vertical bar is the table's first child; a visible one ends the viewport at its left edge.
    const item_t* const bar = table.item_count() > 0 ? table.item_at_index(0) : nullptr;
    if (bar != nullptr && bar->is_visible()) {
        width = std::min(width, bar->geometry().x());
    }
    const int header = default_style().pixel_metric(pixel_metric_enum::TABLE_HEADER_HEIGHT);
    return rect_t{ origin.x(), origin.y() + header, width,
                   std::max(0, table.rect().height() - header) };
}

/// @brief The viewport rows that carry a frame-coloured horizontal run starting INSIDE the viewport
///        rather than at its left edge.
/// @param pixels The frame to scan (a pixel_screen_t or an image_t).
/// @param table The table the viewport is derived from.
/// @return The stray rows, ascending; empty for a frame whose only frame-coloured runs start at the
///         viewport's left edge or are the viewport's own last row (the bottom border).
/// @note A run shorter than 8 px is a column grid line, not a dragged decoration line.
template <typename Pixels>
std::vector<int> stray_frame_rows(const Pixels& pixels, const table_view_t& table) {
    const rect_t viewport = table_viewport_in_window(table);
    std::vector<int> rows;
    for (int y = viewport.y(); y < viewport.bottom() - 1; ++y) {
        int run_start = -1;
        for (int x = viewport.x(); x <= viewport.right(); ++x) {
            const bool frame = x < viewport.right()
                && pixels.pixel(x, y) == viewport_frame_color;
            if (frame && run_start < 0) {
                run_start = x;
            }
            if (!frame && run_start >= 0) {
                if (x - run_start >= 8 && run_start != viewport.x()) {
                    rows.push_back(y);
                    break;
                }
                run_start = -1;
            }
        }
    }
    return rows;
}

/// @brief How many pixels of the viewport's bottom border are missing where no row covers it.
/// @param pixels The frame to scan.
/// @param table The table the viewport is derived from.
/// @return The count of pixels in the band right of the content (the band rows never paint) that are
///         not the frame colour on the viewport's last row. A correct frame answers 0.
template <typename Pixels>
int missing_frame_bottom_pixels(const Pixels& pixels, const table_view_t& table) {
    const rect_t viewport = table_viewport_in_window(table);
    const int content_right = table.map_to_global(point_t{ table.content_width(), 0 }).x();
    int missing = 0;
    const int y = viewport.bottom() - 1;
    for (int x = std::max(viewport.x(), content_right); x < viewport.right(); ++x) {
        if (!(pixels.pixel(x, y) == viewport_frame_color)) {
            ++missing;
        }
    }
    return missing;
}

/// @brief How many pixels of the band right of the content are not the viewport fill.
/// @param pixels The frame to scan.
/// @param table The table the viewport is derived from.
/// @return The count of pixels above the bottom border in the blank band (rows never paint there) that
///         differ from the fill. A correct frame answers 0; a dragged frame line answers its length.
template <typename Pixels>
int blank_band_stray_pixels(const Pixels& pixels, const table_view_t& table) {
    const rect_t viewport = table_viewport_in_window(table);
    const int content_right = table.map_to_global(point_t{ table.content_width(), 0 }).x();
    int stray = 0;
    for (int y = viewport.y(); y < viewport.bottom() - 1; ++y) {
        for (int x = std::max(viewport.x(), content_right); x < viewport.right(); ++x) {
            if (!(pixels.pixel(x, y) == viewport_fill_color)) {
                ++stray;
            }
        }
    }
    return stray;
}

/// @brief The whole state of one incremental frame, checked the way a user sees it.
/// @param window The window whose pixels are the incremental result.
/// @param root The tree root, for the full-repaint reference.
/// @param list The list inside the tree.
/// @param pointer The pointer position the frame was produced with.
/// @param what The scenario name, for the diagnostics.
/// @note Two independent facts are asserted: no stale pixels (the incremental frame equals a full
///       repaint of the same state) and no stale STATE (the hover fill is on the row under the
///       pointer and on no other row).
void check_frame(real_window_t& window, widget_t& root, const list_view_t& list, point_t pointer,
                 const char* what) {
    const int expected = row_under_pointer(list, pointer);
    const std::vector<int> hovered = painted_hover_rows(window, list);
    if (hovered != std::vector<int>{ expected } && !(hovered.empty() && expected < 0)) {
        std::printf("       %s: hovered row(s) on screen:", what);
        for (const int row : hovered) {
            std::printf(" %d", row);
        }
        std::printf("; the pointer is over row %d\n", expected);
    }
    if (expected < 0) {
        CHECK(hovered.empty());
    }
    else {
        CHECK((hovered == std::vector<int>{ expected }));
    }

    const image_t reference = full_repaint(root, window_extent);
    const int different = window.screen().differences(reference);
    if (different != 0) {
        std::printf("       %s: %d pixel(s) differ from a full repaint of the same state\n", what,
                    different);
        window.screen().dump_differences(reference, what);
    }
    CHECK(different == 0);
}

}  // namespace real_path_test

using namespace real_path_test;

int main(int argc, char** argv) {
    std::printf("== item views through the real host path (list, table) ==\n");
    (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    core_application application{ argc, argv };
    {
        gui_application_t gui;
        CHECK(gui_application_t::is_video_initialized());

        real_window_t window{ "real_path", window_extent };
        auto* const root = new fill_widget_t{ color_t::white_color() };
        auto* const list = new list_view_t{ root };
        list->set_geometry(list_rect);
        list->set_row_height(row_height);
        probe_model_t model{ model_rows };
        list->set_model(&model);
        window.adopt(root);

        int painted = 0;
        const int first_phases = settle(window, painted);
        std::printf("[real] first frame: %zu painted frame(s), %d phase(s), %d mirror pass(es)\n",
                    window.painted_frame_count(), first_phases, window.pass_count());
        CHECK(window.painted_frame_count() == 1);
        CHECK(window.pending_damage().is_empty());

        // ---- (0) the mirror and the tree agree with a full repaint before anything moves ----
        {
            const image_t reference = full_repaint(*root, window_extent);
            CHECK(window.screen().differences(reference) == 0);
            // The list really is on screen and its rows carry the row fill colour.
            CHECK(window.screen().pixel(sample_x, row_band_y(*list, 3, 0) + 8) == row_fill_color);
            CHECK(!list->is_hovered());
            CHECK(painted_hover_rows(window, *list).empty());
        }

        // ---- (1) the fast hover sweep ----
        std::printf("(1) motion bursts leave exactly one hovered row, the one under the pointer\n");
        const point_t pointer_row_2 = row_point(*list, 2, 0);
        {
            CHECK(push_mouse(window, SDL_EVENT_MOUSE_MOTION, pointer_row_2, false));
            settle(window, painted);
            CHECK(painted == 1);
            CHECK(window.pending_damage().is_empty());
            CHECK(list->is_hovered());
            check_frame(window, *root, *list, pointer_row_2, "rest on row 2");
        }

        // (1a) ONE fast jump over several rows: the two bands are DISJOINT, so the frame paints two
        //      passes (the partial-repaint shape that a slow move never produces).
        {
            const point_t pointer_row_8 = row_point(*list, 8, 0);
            const int passes_before = window.pass_count();
            CHECK(push_mouse(window, SDL_EVENT_MOUSE_MOTION, pointer_row_8, false));
            settle(window, painted);
            const int passes = window.pass_count() - passes_before;
            std::printf("[real] jump 2 -> 8: %zu damaged rectangle(s), %d frame pass(es), "
                        "%d frame(s)\n",
                        window.last_frame_rects().size(), passes, painted);
            CHECK(window.last_frame_rects().size() == 2);
            CHECK(passes == 2);
            CHECK(painted == 1);
            check_frame(window, *root, *list, pointer_row_8, "jump to row 8");
        }

        // (1b) a sweep: rows 7 back to 3 in one batch, with no repaint in between.
        {
            const point_t pointer_row_3 = row_point(*list, 3, 0);
            for (int row = 7; row >= 3; --row) {
                CHECK(push_mouse(window, SDL_EVENT_MOUSE_MOTION, row_point(*list, row, 0), false));
            }
            settle(window, painted);
            std::printf("[real] sweep 8 -> 3: %d frame(s)\n", painted);
            CHECK(painted == 1); // the batch is one frame, like the demo's 20-motion burst
            check_frame(window, *root, *list, pointer_row_3, "sweep back to row 3");
        }

        // (1c) a sweep whose last step RETURNS to a row that was hovered earlier in the same batch.
        {
            const point_t pointer_row_5 = row_point(*list, 5, 0);
            CHECK(push_mouse(window, SDL_EVENT_MOUSE_MOTION, row_point(*list, 2, 0), false));
            CHECK(push_mouse(window, SDL_EVENT_MOUSE_MOTION, row_point(*list, 6, 0), false));
            CHECK(push_mouse(window, SDL_EVENT_MOUSE_MOTION, pointer_row_5, false));
            settle(window, painted);
            check_frame(window, *root, *list, pointer_row_5, "sweep 3 -> 6 -> 5");
        }

        // (1d) the pointer leaves the rows but stays in the window: no row may keep the hover fill.
        {
            const point_t below_list =
                list->map_to_global(point_t{ 20, list->rect().height() + 4 });
            CHECK(push_mouse(window, SDL_EVENT_MOUSE_MOTION, below_list, false));
            settle(window, painted);
            CHECK(!list->is_hovered());
            CHECK(painted_hover_rows(window, *list).empty());
            check_frame(window, *root, *list, below_list, "pointer below the rows");
        }

        // Put the pointer back on a row for the scroll section.
        const point_t pointer = row_point(*list, 4, 0);
        CHECK(push_mouse(window, SDL_EVENT_MOUSE_MOTION, pointer, false));
        settle(window, painted);
        check_frame(window, *root, *list, pointer, "rest on row 4");

        // ---- (2) the scroll fast path ----
        std::printf("(2) every incremental scroll frame equals a full repaint\n");
        {
            const auto scroll_step = [&](const char* what, int ticks, int repeat) {
                const int offset_before = list->scroll_offset().y();
                for (int i = 0; i < repeat; ++i) {
                    CHECK(push_wheel(window, ticks));
                }
                const int passes_before = window.pass_count();
                settle(window, painted);
                std::printf("[real] %s: offset %d -> %d, %zu damaged rectangle(s), %d pass(es), "
                            "%d frame(s)\n",
                            what, offset_before, list->scroll_offset().y(),
                            window.last_frame_rects().size(), window.pass_count() - passes_before,
                            painted);
                check_frame(window, *root, *list, pointer, what);
            };

            // 16 pixels: not a row multiple, so the top and the bottom rows are only partly visible
            // and the hover row moves with the content.
            scroll_step("wheel down 16", -1, 1);
            scroll_step("wheel down 32", -1, 1);
            // A burst: two steps with no frame in between, so the second shift moves pixels the
            // first step had just damaged.
            scroll_step("wheel burst down 32", -1, 2);
            // Upwards, across the row boundary the previous steps crossed.
            scroll_step("wheel up 16", 1, 1);
            // A shift of a whole viewport: the fast path has to fall back to a repaint.
            scroll_step("wheel down 12 ticks", -12, 1);
            // And back to the top in one step.
            scroll_step("wheel up 30 ticks", 30, 1);
            CHECK(list->scroll_offset().y() == 0);
        }

        // ---- (3) a wheel step with the pointer not over the list: nothing scrolls ----
        {
            const point_t outside = list->map_to_global(point_t{ 20, list->rect().height() + 4 });
            CHECK(push_mouse(window, SDL_EVENT_MOUSE_MOTION, outside, false));
            settle(window, painted);
            const int offset_before = list->scroll_offset().y();
            CHECK(push_wheel(window, -1));
            settle(window, painted);
            CHECK(list->scroll_offset().y() == offset_before);
            check_frame(window, *root, *list, outside, "wheel below the rows");
        }

        // ---- (4) a deterministic walk over every row and the wheel ----
        //
        // The batches above are the shapes a user produces; this section walks them in an arbitrary
        // order so that no single ordering can be the reason the two invariants hold. The sequence is
        // fixed by the seed, so a failure is reproducible.
        {
            std::uint32_t seed = 0x5eed1234u;
            const auto next = [&seed] {
                seed = seed * 1664525u + 1013904223u;
                return seed >> 9;
            };
            point_t pointer = row_point(*list, 4, list->scroll_offset().y());
            CHECK(push_mouse(window, SDL_EVENT_MOUSE_MOTION, pointer, false));
            settle(window, painted);
            for (int step = 0; step < 48; ++step) {
                const std::uint32_t kind = next() % 10;
                if (kind < 6) {
                    // One to three motions: a sweep, possibly jumping over rows.
                    const int motions = 1 + static_cast<int>(next() % 3);
                    for (int i = 0; i < motions; ++i) {
                        const int row = static_cast<int>(next() % 10);
                        pointer = row_point(*list, row, list->scroll_offset().y());
                        CHECK(push_mouse(window, SDL_EVENT_MOUSE_MOTION, pointer, false));
                    }
                }
                else if (kind < 8) {
                    CHECK(push_wheel(window, (next() % 2) == 0 ? -1 : 1));
                }
                else {
                    // A wheel burst with no frame in between.
                    CHECK(push_wheel(window, -1));
                    CHECK(push_wheel(window, -1));
                }
                settle(window, painted);
                char what[64]{};
                std::snprintf(what, sizeof(what), "random step %d (kind %u, offset %d)", step, kind,
                              list->scroll_offset().y());
                check_frame(window, *root, *list, pointer, what);
            }
            std::printf("[real] random walk: 48 steps done\n");
        }

        // ---- (5) the hovered row follows the POINTER when the list scrolls without the wheel ----
        //
        // The wheel is the one step that recomputes the hovered row for itself. Every other way the
        // offset can change — a key, the bar, scroll_to_row() — moves the content under a pointer
        // that did not move, so the row under the pointer is a different row afterwards. The
        // highlight must be on that row, not on the content row the last pointer step saw.
        {
            // The random walk may have left the pointer anywhere, possibly outside the window; a wheel
            // step only reaches a list the pointer is over, so put it on a visible row first.
            const auto point_on_visible_row = [&] {
                const int offset = list->scroll_offset().y();
                const int row = std::min(model_rows - 1, offset / list->row_height() + 2);
                return row_point(*list, row, offset);
            };
            CHECK(push_mouse(window, SDL_EVENT_MOUSE_MOTION, point_on_visible_row(), false));
            // Back to the top so row 8 sits where the pointer can reach it.
            for (int step = 0; step < 32 && list->scroll_offset().y() > 0; ++step) {
                CHECK(push_wheel(window, 10));
                settle(window, painted);
            }
            CHECK(list->scroll_offset().y() == 0);

            const point_t pointer_row_8 = row_point(*list, 8, 0);
            CHECK(push_mouse(window, SDL_EVENT_MOUSE_MOTION, pointer_row_8, false));
            settle(window, painted);
            check_frame(window, *root, *list, pointer_row_8, "rest on row 8");

            // A click gives the list the focus and makes row 8 current (and selected).
            CHECK(push_mouse(window, SDL_EVENT_MOUSE_BUTTON_DOWN, pointer_row_8, true));
            CHECK(push_mouse(window, SDL_EVENT_MOUSE_BUTTON_UP, pointer_row_8, false));
            settle(window, painted);
            CHECK(list->has_focus());
            CHECK(list->current_row() == 8);
            check_frame(window, *root, *list, pointer_row_8, "click on row 8");

            // Down moves the current row past the last visible one, so the list scrolls by exactly one
            // row through the fast path: the pointer is over content row 9 now.
            CHECK(push_key(window, interaction_t::down_key));
            settle(window, painted);
            CHECK(list->current_row() == 9);
            CHECK(list->scroll_offset().y() == list->row_height());
            std::printf("[real] Down key: offset %d, the pointer is over row %d\n",
                        list->scroll_offset().y(), row_under_pointer(*list, pointer_row_8));
            check_frame(window, *root, *list, pointer_row_8, "after the Down key");
        }

        // ---- (6) a programmatic scroll with NO input event still schedules exactly one frame ----
        //
        // The fast path's record is the only thing that can reach the host here: the test pushes no
        // event, so no input step schedules anything afterwards. The frame must still be painted, and
        // its pixels must equal a full repaint of the same state.
        {
            real_window_t quiet{ "real_path_quiet_scroll", window_extent };
            auto* const quiet_root = new fill_widget_t{ color_t::white_color() };
            auto* const quiet_list = new list_view_t{ quiet_root };
            quiet_list->set_geometry(list_rect);
            quiet_list->set_row_height(row_height);
            probe_model_t quiet_model{ model_rows };
            quiet_list->set_model(&quiet_model);
            quiet.adopt_production(quiet_root); // the view's own sink: the chain an application runs

            int painted = 0;
            (void)settle(quiet, painted);
            CHECK(painted == 1);
            CHECK(quiet.screen().differences(full_repaint(*quiet_root, window_extent)) == 0);

            const std::size_t frames_before = quiet.painted_frame_count();
            // Row 9 is the first row below the 180-pixel viewport, so revealing it scrolls exactly one
            // row (20 px) through the fast path: one shift, no event, no pointer step.
            quiet_list->scroll_to_row(9);
            CHECK(quiet_list->scroll_offset().y() == row_height);
            const rect_t viewport = viewport_in_window(*quiet_list);
            quiet.replay_surface_shift(viewport, -row_height); // the move window_t::scroll() made

            const int passes_before = quiet.pass_count();
            (void)settle(quiet, painted);
            int painted_area = 0;
            for (const rect_t& pass : quiet.last_frame_rects()) {
                painted_area += pass.width() * pass.height();
            }
            std::printf("[real] quiet scroll: offset %d, %d frame(s), %d pass(es), %d px painted of a "
                        "%d px viewport\n",
                        quiet_list->scroll_offset().y(), painted, quiet.pass_count() - passes_before,
                        painted_area, viewport.width() * viewport.height());
            CHECK(painted == 1); // the record scheduled exactly one frame
            CHECK(quiet.painted_frame_count() == frames_before + 1);
            CHECK(quiet.pending_damage().is_empty());
            // The fast path stayed a fast path: the frame repainted the exposed band and the bar, not
            // the whole viewport.
            CHECK(painted_area < viewport.width() * viewport.height());
            const image_t reference = full_repaint(*quiet_root, window_extent);
            const int different = quiet.screen().differences(reference);
            if (different != 0) {
                std::printf("       quiet scroll: %d pixel(s) differ from a full repaint\n", different);
                quiet.screen().dump_differences(reference, "quiet scroll");
            }
            CHECK(different == 0);
        }

        // ---- (7) the table's own frame: a fast scroll must not drag it into the blank band ----
        //
        // The table's content is narrower than its viewport, so the viewport's bottom border stays
        // visible in the band right of the last column. That band is exactly where a fast path that
        // shifts the whole viewport leaves the old border line and loses the border itself.
        std::printf("(7) table scrolls: presented pixels equal a full repaint, no stray frame line\n");
        constexpr extent_t table_window_extent{ 300, 250 };
        constexpr rect_t table_rect{ 10, 10, 280, 220 };
        constexpr int table_row_height = 30;
        constexpr int table_columns = 4;
        constexpr int table_column_width = 60;
        const int table_wheel_step =
            default_style().pixel_metric(pixel_metric_enum::SCROLL_WHEEL_STEP);
        for (const int ticks : { 1, 2, 3, 6 }) {
            real_window_t table_window{ "real_path_table", table_window_extent };
            auto* const table_root = new fill_widget_t{ color_t::white_color() };
            auto* const table = new table_view_t{ table_root };
            table->set_geometry(table_rect);
            table->set_row_height(table_row_height);
            for (int column = 0; column < table_columns; ++column) {
                table->set_column_width(column, table_column_width);
            }
            probe_table_model_t table_model{ 100, table_columns };
            table->set_model(&table_model);
            table_window.adopt(table_root);

            int table_painted = 0;
            (void)settle(table_window, table_painted);
            CHECK(table_painted == 1);
            CHECK(table_window.screen().differences(full_repaint(*table_root, table_window_extent))
                  == 0);

            const point_t table_pointer = table->map_to_global(
                point_t{ table->rect().width() / 2, table->rect().height() / 2 });
            CHECK(push_mouse(table_window, SDL_EVENT_MOUSE_MOTION, table_pointer, false));
            (void)settle(table_window, table_painted);

            // Prime: 8 ticks scroll down 128 px, then the burst continues DOWN so the view's bottom
            // border stays in place and its stale copy is dragged upward (the user's direction).
            for (int tick = 0; tick < 8; ++tick) {
                CHECK(push_wheel(table_window, -1));
            }
            (void)settle(table_window, table_painted);
            CHECK(table_painted == 1);
            CHECK(table->scroll_offset().y() == 8 * table_wheel_step);

            for (int tick = 0; tick < ticks; ++tick) {
                CHECK(push_wheel(table_window, -1));
            }
            (void)settle(table_window, table_painted);
            CHECK(table_painted == 1);
            CHECK(table->scroll_offset().y() == (8 + ticks) * table_wheel_step);

            const image_t reference = full_repaint(*table_root, table_window_extent);
            const std::vector<int> stray = stray_frame_rows(table_window.screen(), *table);
            const int blank = blank_band_stray_pixels(table_window.screen(), *table);
            const int missing = missing_frame_bottom_pixels(table_window.screen(), *table);
            const int different = table_window.screen().differences(reference);
            std::printf("[real] table burst %d: offset %d, %zu stray row(s), %d blank-band "
                        "pixel(s), %d missing border pixel(s), %d differing pixel(s)\n",
                        ticks, table->scroll_offset().y(), stray.size(), blank, missing, different);
            if (different != 0) {
                table_window.screen().dump_differences(reference, "table scroll");
            }
            // The structural criteria: no frame-coloured run may start inside the viewport, the blank
            // band may hold nothing but the viewport fill, and the bottom border must be intact where
            // no row covers it.
            CHECK(stray.empty());
            CHECK(blank == 0);
            CHECK(missing == 0);
            // The whole frame must equal a full repaint of the same state, pixel for pixel.
            CHECK(different == 0);
            // The criteria are meaningful: a correct frame (the reference) passes them too.
            CHECK(stray_frame_rows(reference, *table).empty());
            CHECK(blank_band_stray_pixels(reference, *table) == 0);
            CHECK(missing_frame_bottom_pixels(reference, *table) == 0);
        }
    }

    if (g_failures == 0) {
        std::printf("real path: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("real path: %d of %d checks failed\n", g_failures, g_checks);
    return 1;
}
