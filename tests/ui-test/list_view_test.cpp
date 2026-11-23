// M5: list_view_t — the model-driven row list.
//
// The acceptance criteria as executable checks:
//  1. row count / row height / scroll range / fixed size hint (independent of the row count) and the
//     minimum hint;
//  2. virtualisation: a 1,000-row model has data() asked only for the visible rows, at the top and at
//     the bottom of the range;
//  3. keyboard navigation (arrows / pages / Home / End) with the automatic scroll_to_row() and Enter;
//  4. the wheel, its style step and the [0, max] clamping;
//  5. clicks: current row, selected set, row_activated() on an internal release only (a release that
//     leaves the list, or lands on another row, activates nothing);
//  6. SINGLE and MULTI selection: Ctrl toggles without moving the anchor, Shift extends from the
//     anchor, and selection_changed() fires only on a real set change;
//  7. the three model notifications damage exactly their own rows (asserted through damage()), and
//     model_reset() clears the state;
//  8. an empty model, and row-count shrinkage clamping the current row and the scroll offset;
//  9. the internal scroll bar stays in sync (range, page step, geometry, value, AS_NEEDED visibility);
// 10. a fast hover sweep over several rows in ONE frame leaves exactly one hovered row (the row the
//     pointer left must be repainted in that same frame);
// 11. a viewport that is not a multiple of the row height paints the last row WHOLE and clips it: its
//     text cell and its current outline must not be squeezed into the visible remainder;
// 12. the scroll fast path (shift + the band the window accumulates over the steps of one frame) must
//     leave EXACTLY the pixels a full repaint of the same state leaves, for wheel steps in both
//     directions, a bar drag, a burst of steps with no frame in between and the repaint fallback;
// 13. a current-row or selection change repaints the row it left as well as the one it entered (no
//     stale outline/selected fill may survive on screen).
//
// Offscreen only (image_t + painter_t); no window and no video subsystem are created. The frame tests
// emulate a host frame: one clipped paint pass per damaged rectangle, and a surface that the scroll
// fast path can shift.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/gui/region_t.h"
#include "hwshqtb/gui/window_t.h"
#include "hwshqtb/metaobject/any_t.h"
#include "hwshqtb/ui/input_router_t.h"
#include "hwshqtb/ui/item_model_t.h"
#include "hwshqtb/ui/list_view_t.h"
#include "hwshqtb/ui/scroll_bar_t.h"
#include "hwshqtb/gui/font_database_t.h"
#include "hwshqtb/ui/style_t.h"
#include "hwshqtb/ui/widget_t.h"

/// @brief The row band the views use right now: the resolved face's line band plus the vertical padding.
/// @return The row height in pixels, derived from the CURRENT font face.
/// @note Font independent by construction: it moves when the face changes, so an assertion written with
///       it holds under both the built-in bitmap face and a platform UI face.
int row_h() {
    const ::hwshqtb::ui::style_t& style = ::hwshqtb::ui::default_style();
    return style.pixel_metric(::hwshqtb::ui::pixel_metric_enum::LINE_HEIGHT)
        + 2 * style.pixel_metric(::hwshqtb::ui::pixel_metric_enum::TEXT_VERTICAL_PADDING);
}

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;
using namespace hwshqtb::ui;

namespace list_view_test {

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

/// @brief A test-side receiver for the tree's host requests: the body is a caller-provided lambda.
/// @note std::function is fine HERE because this is test code; the LIBRARY's request channel is the
///       event itself (item_t::set_request_receiver + PAINT / scroll_event_t), precisely so that the
///       library never stores a callable.
class request_sink_t final : public ::hwshqtb::metaobject::object_t {
public:
    /// @brief The body run for a scroll request.
    std::function<void(const scroll_event_t&)> on_scroll;
    /// @brief The body run for a frame request.
    std::function<void()> on_paint;

    bool event(::hwshqtb::metaobject::event_t* request) override {
        if (request == nullptr) {
            return false;
        }
        if (request->type() == ::hwshqtb::metaobject::event_type_enum::UI_SCROLL && on_scroll) {
            on_scroll(*static_cast<const scroll_event_t*>(request));
            return true;
        }
        if (request->type() == ::hwshqtb::metaobject::event_type_enum::PAINT && on_paint) {
            on_paint();
            return true;
        }
        return ::hwshqtb::metaobject::object_t::event(request);
    }
};

/// @brief A probe model whose rows are the strings "row 0", "row 1", ... and whose data() calls are
///        counted. Out-of-range calls are counted separately: a virtualised view never makes one.
class probe_model_t final : public item_model_t {
public:
    explicit probe_model_t(int rows = 0) {
        set_rows(rows);
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

    void set_rows(int rows) {
        _rows = std::max(0, rows);
        _texts.clear();
        _texts.reserve(static_cast<std::size_t>(_rows));
        for (int row = 0; row < _rows; ++row) {
            _texts.push_back("row " + std::to_string(row));
        }
    }

    void forget_calls() {
        data_calls = 0;
        out_of_range_calls = 0;
        asked_rows.clear();
    }

    void notify_data_changed(int first, int last) {
        data_changed(first, last);
    }

    void notify_rows_inserted(int first, int count) {
        rows_inserted(first, count);
    }

    void notify_rows_removed(int first, int count) {
        rows_removed(first, count);
    }

    void notify_model_reset() {
        model_reset();
    }

    mutable int data_calls = 0;
    mutable int out_of_range_calls = 0;
    mutable std::vector<int> asked_rows;

private:
    int _rows = 0;
    std::vector<std::string> _texts;
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

interaction_t release(point_t local, bool inside, std::uint32_t modifiers = 0) {
    interaction_t in;
    in.mouse = local;
    in.mouse_released = true;
    in.hovered = inside;
    in.modifiers = modifiers;
    return in;
}

interaction_t move(point_t local, bool inside = true) {
    interaction_t in;
    in.mouse = local;
    in.hovered = inside;
    return in;
}

interaction_t wheel_step(point_t ticks) {
    interaction_t in;
    in.mouse = point_t{ -1, -1 }; // the pointer is not over a row: the wheel is not a hover step
    in.wheel = ticks;
    return in;
}

/// @brief A wheel step with the pointer over the list: the wheel scrolls AND the hover row follows.
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

std::uint32_t modifiers_of(modifier_flag flag) {
    return modifier_bits(flag);
}

/// @brief Whether @p region covers every pixel of @p rect.
bool covers(const region_t& region, rect_t rect) {
    return region_t{ rect }.subtracted(region).is_empty();
}

/// @brief Paint one full frame of @p view into a fresh 240x160 image (the default viewport).
void paint_full(list_view_t& view) {
    image_t image{ extent_t{ 240, 160 } };
    painter_t painter{ image };
    painter.begin();
    painter.clear(color_t::white_color());
    view.render(painter);
    painter.end();
}

/// @brief Click @p local: press, then release inside. @p modifiers are the modifier bits of the step.
void click(list_view_t& view, point_t local, std::uint32_t modifiers = 0) {
    (void)view.interact(press(local, modifiers));
    (void)view.interact(release(local, true, modifiers));
}

/// @brief A click row: the local y of row @p row's middle, at the given row height.
point_t row_point(int row, int row_height = 30, int x = 10) {
    return point_t{ x, row * row_height + row_height / 2 };
}

/// @brief The internal scroll bar (the list's only item child).
scroll_bar_t* bar_of(list_view_t& view) {
    return dynamic_cast<scroll_bar_t*>(view.item_at_index(0));
}

/// @brief Scroll @p row into view and click its middle in view coordinates.
void click_row_in_view(list_view_t& view, int row, std::uint32_t modifiers = 0) {
    view.scroll_to_row(row);
    const int top = row * view.row_height() - view.scroll_offset().y();
    click(view, point_t{ 10, top + view.row_height() / 2 }, modifiers);
}

/// @brief Drop the damage of the view AND its scroll bar (the bar is a child item with its own
///        record; a stale record would otherwise be united into the view's).
void clear_damages(list_view_t& view) {
    view.clear_damage();
    if (scroll_bar_t* const bar = bar_of(view)) {
        bar->clear_damage();
    }
}

// The colours of the painted rows, as the style paints them. They are hard-coded here on purpose: the
// assertions are about PIXELS, so the expectation must not be derived from the code under test.
constexpr color_t row_fill_color{ 30, 42, 56 };             // LIST_ROW
constexpr color_t row_hover_fill_color{ 40, 55, 70 };       // LIST_ROW hovered
constexpr color_t row_selected_fill_color{ 46, 109, 164 };  // LIST_ROW_SELECTED
constexpr color_t row_current_outline_color{ 255, 214, 10 };  // LIST_ROW_CURRENT
constexpr color_t row_text_color{ 245, 248, 250 };            // LIST_TEXT

/// @brief A host surface that keeps the window's pixels across frames: it can be painted by full or
///        damaged frame passes and shifted by the scroll fast path, like window_t's backing surface.
class frame_surface_t {
public:
    /// @param size The surface extent.
    explicit frame_surface_t(extent_t size):
        _size(size),
        _pixels(static_cast<std::size_t>(size.width()) * static_cast<std::size_t>(size.height()),
                color_t::transparent_color()) {
    }

    /// @brief Paint one full frame of the view (the deterministic frame 0).
    /// @param view The view, a root at the surface origin.
    void paint_whole(list_view_t& view) {
        image_t image{ _size };
        painter_t painter{ image };
        painter.begin();
        painter.clear(color_t::white_color());
        view.render(painter);
        painter.end();
        copy_all(image);
    }

    /// @brief Paint the view's recorded damage, one clipped pass per rectangle: the host's frame.
    /// @param view The view, a root at the surface origin.
    /// @return The number of damaged rectangles painted (0: the frame painted nothing).
    int paint_damage(list_view_t& view) {
        const region_t damage = view.damage();
        int passes = 0;
        for (const rect_t& area : damage.rects()) {
            image_t image{ _size };
            painter_t painter{ image };
            painter.begin();
            painter.save();
            painter.clear_clip();
            painter.set_clip(area);
            painter.clear(area, color_t::white_color());
            view.render(painter);
            painter.restore();
            painter.end();
            copy_rect(image, area);
            ++passes;
        }
        return passes;
    }

    /// @brief Shift the pixels inside @p rect by @p dy, leaving the uncovered band untouched: a
    ///        pixel at (@p x, @p y) moves to (@p x, @p y + @p dy) and reads from y - dy.
    /// @param rect The shifted rectangle in window coordinates.
    /// @param dy The vertical shift of the content.
    void shift(rect_t rect, int dy) {
        // Walk towards the shift so a destination never reads a source this loop already overwrote.
        const bool downwards = dy > 0;
        for (int step = 0; step < rect.height(); ++step) {
            const int y = downwards ? rect.bottom() - 1 - step : rect.y() + step;
            const int source = y - dy;
            if (!rect.contains(point_t{ rect.x(), source })) {
                continue;
            }
            for (int x = rect.x(); x < rect.right(); ++x) {
                set(x, y, at(x, source));
            }
        }
    }

    /// @brief The pixel at (@p x, @p y).
    color_t pixel(int x, int y) const {
        if (x < 0 || y < 0 || x >= _size.width() || y >= _size.height()) {
            return color_t::transparent_color();
        }
        return _pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(_size.width())
                       + static_cast<std::size_t>(x)];
    }

    /// @brief How many pixels differ from @p other.
    int differences(const frame_surface_t& other) const {
        int count = 0;
        for (int y = 0; y < _size.height(); ++y) {
            for (int x = 0; x < _size.width(); ++x) {
                if (pixel(x, y) != other.pixel(x, y)) {
                    ++count;
                }
            }
        }
        return count;
    }

    /// @brief The first y at which the two surfaces differ, or -1 when they are identical.
    int first_different_row(const frame_surface_t& other) const {
        for (int y = 0; y < _size.height(); ++y) {
            for (int x = 0; x < _size.width(); ++x) {
                if (pixel(x, y) != other.pixel(x, y)) {
                    return y;
                }
            }
        }
        return -1;
    }

private:
    color_t at(int x, int y) const {
        // Bounds-checked exactly like pixel(): the mirror is only as large as the surface. A read
        // outside it answers transparent; a WRITE outside it is caught in set() below.
        if (x < 0 || y < 0 || x >= _size.width() || y >= _size.height()) {
            return color_t::transparent_color();
        }
        return _pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(_size.width())
                       + static_cast<std::size_t>(x)];
    }

    void set(int x, int y, color_t color) {
        // A write outside the mirror is ALWAYS a bug: the surface and whatever paints into it no longer
        // agree on their size. Indexing the vector there stomps the heap, and the crash then lands
        // hundreds of frames later inside the allocator with no clue where it came from — so it fails
        // HERE, naming the coordinate and the mirror's size.
        if (x < 0 || y < 0 || x >= _size.width() || y >= _size.height()) {
            std::fprintf(stderr,
                         "list_view_test: frame_surface_t write at (%d, %d) outside the %dx%d mirror — the "
                         "surface and the widget painting into it disagree on their size\n",
                         x, y, _size.width(), _size.height());
            std::abort();
        }
        _pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(_size.width())
                + static_cast<std::size_t>(x)] = color;
    }

    void copy_all(const image_t& image) {
        for (int y = 0; y < _size.height(); ++y) {
            for (int x = 0; x < _size.width(); ++x) {
                set(x, y, image.pixel(x, y));
            }
        }
    }

    void copy_rect(const image_t& image, rect_t rect) {
        const rect_t area = rect.intersected(rect_t{ 0, 0, _size.width(), _size.height() });
        for (int y = area.y(); y < area.bottom(); ++y) {
            for (int x = area.x(); x < area.right(); ++x) {
                set(x, y, image.pixel(x, y));
            }
        }
    }

    extent_t _size;
    std::vector<color_t> _pixels;
};

/// @brief The image row a list row occupies at @p offset, for reading a background pixel outside the
///        text band.
int row_sample_y(int row, int row_height, int offset) {
    return row * row_height - offset + row_height / 2;
}

/// @brief Whether the pixel at (@p x, @p y) of @p surface (image_t or frame_surface_t) is @p color.
template <typename SurfaceT>
bool pixel_is(const SurfaceT& surface, int x, int y, color_t color) {
    return surface.pixel(x, y) == color;
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

/// @brief A host that keeps the window's pixels in a frame_surface_t and replays the scroll fast path:
///        the WINDOW half shifts them and answers the band, the VIEW half records that band on the tree.
class image_scroll_host_t {
public:
    /// @param surface The persistent frame surface (the list must be its root at the origin).
    explicit image_scroll_host_t(frame_surface_t& surface): _surface(surface) {
    }

    /// @brief The window half of the fast path: shift the pixels and answer the band that is now stale.
    /// @param rect The shifted rectangle, in window coordinates.
    /// @param dx The horizontal shift (the list never uses it).
    /// @param dy The vertical shift of the painted content.
    /// @return The band the ACCUMULATED shift of this frame uncovered.
    /// @note window_t::scroll()'s rule: several steps land before the frame, and the next step drags
    ///       the band the previous one exposed further into the area, so the answer is recomputed from
    ///       the sum. A reversed axis falls back to the whole rectangle. Only one rectangle is tracked
    ///       (the lists in this file scroll one viewport at a time).
    region_t view_scroll(rect_t rect, int dx, int dy) {
        if (!_tracking || !(_rect == rect)) {
            _rect = rect;
            _dx = 0;
            _dy = 0;
            _whole = false;
            _tracking = true;
        }
        const auto sign = [](int value) { return value > 0 ? 1 : -1; };
        if ((dx != 0 && _dx != 0 && sign(dx) != sign(_dx))
            || (dy != 0 && _dy != 0 && sign(dy) != sign(_dy))) {
            _whole = true;
        }
        _dx += dx;
        _dy += dy;
        _surface.shift(rect, dy); // the pixel move of this step (backing_store_t::scroll)
        return _whole ? region_t{ rect } : window_t::scroll_exposed_region(rect, _dx, _dy);
    }

    /// @brief A frame painted: the next shift starts a new accumulation (window_t::paint_frame()).
    void frame_painted() {
        _tracking = false;
    }

    /// @brief The scroll sink of the tree: the host half of the fast path (widget_window_t's sink).
    /// @param view The scrolling view.
    /// @param rect The shifted rectangle, in window coordinates.
    /// @param dx The horizontal shift (the list never uses it).
    /// @param dy The vertical shift of the painted content.
    /// @note The view moves the record's part inside the shifted rectangle with the pixels and records
    ///       exactly the region the window answered, which is what makes the tree's record and the
    ///       surface agree when several steps coalesce into one frame. The list is the surface's root,
    ///       so its local coordinates are the window ones.
    void operator()(list_view_t& view, rect_t rect, int dx, int dy) {
        ++shifts;
        last_dy = dy;
        if (dx != 0 || dy == 0) {
            return;
        }
        const region_t exposed = view_scroll(rect, dx, dy);
        view.translate_damage(rect, dx, dy);
        for (const rect_t& strip : exposed.rects()) {
            view.update(strip);
        }
    }

    /// @brief How many shifts the list requested.
    int shifts = 0;
    /// @brief The last vertical shift.
    int last_dy = 0;

private:
    frame_surface_t& _surface;
    /// @brief The rectangle of the record kept since the last painted frame.
    rect_t _rect{};
    /// @brief The horizontal shift accumulated since the last painted frame.
    int _dx = 0;
    /// @brief The vertical shift accumulated since the last painted frame.
    int _dy = 0;
    /// @brief Whether only a whole-rectangle repaint is correct (a reversed axis).
    bool _whole = false;
    /// @brief Whether @p _rect holds a record that has not been painted yet.
    bool _tracking = false;
};

void test_geometry_and_size() {
    std::printf("(1) row height, scroll range, fixed size hint, minimum hint\n");
    probe_model_t model{ 100 };
    list_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 160 });

    const style_t& style = default_style();
    const int expected_row_height = style.pixel_metric(pixel_metric_enum::LINE_HEIGHT)
        + 2 * style.pixel_metric(pixel_metric_enum::TEXT_VERTICAL_PADDING);
    CHECK(view.row_height() == expected_row_height);
    CHECK(view.size_hint() == (extent_t{ style.pixel_metric(pixel_metric_enum::LIST_VIEW_WIDTH),
                                         style.pixel_metric(pixel_metric_enum::LIST_VIEW_HEIGHT) }));
    CHECK(view.size_hint() == (extent_t{ 160, 160 }));
    CHECK(view.minimum_size_hint()
          == (extent_t{ style.pixel_metric(pixel_metric_enum::LIST_VIEW_MIN_WIDTH),
                        expected_row_height
                            + style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS) }));
    CHECK(view.minimum_size_hint()
          == (extent_t{ 60, expected_row_height
                                + style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS) }));

    view.set_model(&model);
    CHECK(view.model() == &model);
    CHECK(view.current_row() == -1);
    CHECK(view.selected_rows().empty());
    CHECK(view.scroll_offset() == (point_t{ 0, 0 }));

    scroll_bar_t* const bar = bar_of(view);
    CHECK(bar != nullptr);
    CHECK(bar->is_visible()); // 100 * row_h() > 160
    CHECK(bar->geometry() == (rect_t{ 226, 0, 14, 160 }));
    CHECK(bar->minimum() == 0);
    CHECK(bar->maximum() == 100 * expected_row_height - 160);
    CHECK(bar->page_step() == 160);
    CHECK(bar->value() == 0);
    CHECK(bar->orientation() == orientation_enum::VERTICAL);

    // The hint does not follow the row count: an empty model and a 1,000-row model ask for the same
    // viewport (the content only moves the scroll range).
    probe_model_t huge{ 1000 };
    list_view_t other;
    other.set_model(&huge);
    CHECK(other.size_hint() == view.size_hint());
    CHECK(other.minimum_size_hint() == view.minimum_size_hint());

    // Row height changes re-derive the range; a non-positive value is clamped to 1.
    view.set_row_height(20);
    CHECK(view.row_height() == 20);
    CHECK(bar->maximum() == 100 * 20 - 160);
    view.set_row_height(0);
    CHECK(view.row_height() == 1);
    CHECK(bar->maximum() == 0);
    CHECK(!bar->is_visible());
}

void test_virtualisation() {
    std::printf("(2) virtualisation: only the visible rows are asked for\n");
    probe_model_t model{ 1000 };
    list_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 160 });
    view.set_model(&model);

    model.forget_calls();
    paint_full(view);
    CHECK(model.data_calls == 6); // rows 0..5: 160 / 30 rounds up to six partly/fully visible bands
    CHECK(model.out_of_range_calls == 0);
    CHECK((model.asked_rows == std::vector<int>{ 0, 1, 2, 3, 4, 5 }));

    // The DISPLAY string_view reaches the pixels: row 0's text paints over its row background.
    image_t painted{ extent_t{ 240, 160 } };
    painter_t row_painter{ painted };
    row_painter.begin();
    row_painter.clear(color_t::white_color());
    view.render(row_painter);
    row_painter.end();
    bool drew_text = false;
    for (int y = 0; y < view.row_height() && !drew_text; ++y) {
        for (int x = 8; x < 100 && !drew_text; ++x) {
            drew_text = !(painted.pixel(x, y) == color_t{ 30, 42, 56 }); // LIST_ROW fill
        }
    }
    CHECK(drew_text);

    // The same cost at the bottom of a 1,000-row model.
    view.scroll_to_row(999);
    CHECK(view.scroll_offset().y() == 1000 * row_h() - 160);
    model.forget_calls();
    paint_full(view);
    CHECK(model.data_calls == 6);
    CHECK(model.out_of_range_calls == 0);
    CHECK((model.asked_rows == std::vector<int>{ 994, 995, 996, 997, 998, 999 }));

    // A hidden list asks for nothing.
    view.set_visible(false);
    model.forget_calls();
    paint_full(view);
    CHECK(model.data_calls == 0);
    view.set_visible(true);
}

void test_keyboard() {
    std::printf("(3) keyboard navigation and automatic scrolling\n");
    probe_model_t model{ 100 };
    list_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 160 });
    view.set_model(&model);
    input_router_t router{ view };
    router.set_focus(&view);
    CHECK(view.has_focus());

    int activations = 0;
    int activated_row = -1;
    view.connect("row_activated(int) const", [&activations, &activated_row](int row) {
        ++activations;
        activated_row = row;
    });

    CHECK(router.dispatch(key_step(interaction_t::down_key)));
    CHECK(view.current_row() == 0);
    CHECK(view.selected_rows() == std::vector<int>{ 0 });
    CHECK(view.scroll_offset().y() == 0);

    for (int step = 0; step < 5; ++step) {
        CHECK(router.dispatch(key_step(interaction_t::down_key)));
    }
    CHECK(view.current_row() == 5);
    CHECK(view.scroll_offset().y() == 5 * row_h() + row_h() - 160); // row 5 was partly clipped
    CHECK(router.dispatch(key_step(interaction_t::down_key)));
    CHECK(view.current_row() == 6);
    CHECK(view.scroll_offset().y() == 6 * row_h() + row_h() - 160);

    CHECK(router.dispatch(key_step(interaction_t::up_key)));
    CHECK(view.current_row() == 5);

    // PageDown/PageUp move by the number of whole visible rows (160 / 30 == 5).
    CHECK(router.dispatch(key_step(interaction_t::page_down_key)));
    CHECK(view.current_row() == 10);
    CHECK(view.scroll_offset().y() == 10 * row_h() + row_h() - 160);
    CHECK(router.dispatch(key_step(interaction_t::page_up_key)));
    CHECK(view.current_row() == 5);
    CHECK(view.scroll_offset().y() == 5 * row_h());

    CHECK(router.dispatch(key_step(interaction_t::end_key)));
    CHECK(view.current_row() == 99);
    CHECK(view.scroll_offset().y() == 100 * row_h() - 160);
    CHECK(router.dispatch(key_step(interaction_t::home_key)));
    CHECK(view.current_row() == 0);
    CHECK(view.scroll_offset().y() == 0);

    // Enter activates the current row.
    CHECK(router.dispatch(key_step(interaction_t::return_key)));
    CHECK(activations == 1);
    CHECK(activated_row == 0);

    // A key step without the focus is not the list's.
    view.clear_focus();
    CHECK(!view.has_focus());
    CHECK(!router.dispatch(key_step(interaction_t::down_key)));
    CHECK(view.current_row() == 0);
}

void test_wheel() {
    std::printf("(4) the wheel and its clamping\n");
    probe_model_t model{ 100 };
    list_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 160 });
    view.set_model(&model);

    const int step = default_style().pixel_metric(pixel_metric_enum::SCROLL_WHEEL_STEP);
    CHECK(step > 0);
    CHECK(view.interact(wheel_step(point_t{ 0, -1 })));
    CHECK(view.scroll_offset().y() == step);
    CHECK(view.interact(wheel_step(point_t{ 0, 1 })));
    CHECK(view.scroll_offset().y() == 0);

    // At the top a wheel away from the user changes nothing and is not consumed.
    CHECK(!view.interact(wheel_step(point_t{ 0, 1 })));
    CHECK(view.scroll_offset().y() == 0);

    // A huge step clamps to the bottom and is consumed.
    CHECK(view.interact(wheel_step(point_t{ 0, -1000 })));
    CHECK(view.scroll_offset().y() == 100 * row_h() - 160);
    CHECK(!view.interact(wheel_step(point_t{ 0, -1 })));
    CHECK(view.scroll_offset().y() == 100 * row_h() - 160);

    // A horizontal-only wheel is not this view's.
    CHECK(!view.interact(wheel_step(point_t{ 1, 0 })));
}

void test_click_and_activation() {
    std::printf("(5) click, current row, activation only on an internal release\n");
    probe_model_t model{ 100 };
    list_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 160 });
    view.set_model(&model);

    int activations = 0;
    int activated_row = -1;
    view.connect("row_activated(int) const", [&activations, &activated_row](int row) {
        ++activations;
        activated_row = row;
    });

    CHECK(view.interact(press(row_point(2))));
    CHECK(view.current_row() == 2);
    CHECK(view.selected_rows() == std::vector<int>{ 2 });
    CHECK(activations == 0); // a press does not activate

    // A release outside the list does not activate (the press still belongs to the list).
    CHECK(view.interact(release(point_t{ 10, 300 }, false)));
    CHECK(activations == 0);

    // A release on the pressed row activates exactly once.
    CHECK(view.interact(press(row_point(3))));
    CHECK(view.interact(release(row_point(3), true)));
    CHECK(activations == 1);
    CHECK(activated_row == 3);

    // A release on a different row does not activate.
    CHECK(view.interact(press(row_point(4))));
    CHECK(view.interact(release(point_t{ 10, view.rect().height() - 5 }, true))); // row 5's visible part
    CHECK(activations == 1);
    CHECK(view.current_row() == 4);

    // The retained host's implicit grab delivers the release to the list even when the pointer left
    // its rectangle: row_activated() must still stay silent.
    activations = 0;
    input_router_t router{ view };

    // Hover is item state: a move sets it, and the router's "pointer can no longer reach the tree"
    // clear drops the remembered row (and repaints exactly that row).
    interaction_t hover_move;
    hover_move.mouse = point_t{ 10, 2 * row_h() + 5 };
    hover_move.hovered = true;
    CHECK(!router.dispatch(hover_move));
    CHECK(view.is_hovered());
    clear_damages(view);
    router.clear_pointer_state();
    CHECK(!view.is_hovered());
    CHECK(covers(view.damage(), rect_t{ 0, 2 * row_h(), 226, row_h() }));

    interaction_t down = press(point_t{ 10, 2 * row_h() + 5 });
    CHECK(router.dispatch(down));
    CHECK(view.captures_pointer());
    interaction_t up = release(point_t{ 10, 300 }, true);
    CHECK(router.dispatch(up));
    CHECK(activations == 0);
    CHECK(!view.captures_pointer());

    // A click in the empty area below a short model clears the current row and the selection.
    probe_model_t short_model{ 3 };
    list_view_t short_view;
    short_view.set_geometry(rect_t{ 0, 0, 240, 160 });
    short_view.set_model(&short_model);
    short_view.set_current_row(1);
    CHECK(short_view.current_row() == 1);
    CHECK(short_view.interact(press(point_t{ 10, 140 }))); // past the third row (90)
    CHECK(short_view.current_row() == -1);
    CHECK(short_view.selected_rows().empty());
}

void test_selection_modes() {
    std::printf("(6) SINGLE / MULTI: Ctrl toggle, Shift range, anchor, real-change signal\n");
    probe_model_t model{ 50 };
    list_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 160 });
    view.set_model(&model);

    int selection_signals = 0;
    int current_signals = 0;
    view.connect("selection_changed() const", [&selection_signals]() { ++selection_signals; });
    view.connect("current_changed(int) const", [&current_signals]() { ++current_signals; });

    // SINGLE: the current row is the only selected row.
    CHECK(view.selection_mode() == selection_mode_enum::SINGLE);
    view.set_current_row(2);
    CHECK(view.selected_rows() == std::vector<int>{ 2 });
    const int single_selection_signals = selection_signals;
    view.set_current_row(3);
    CHECK(view.selected_rows() == std::vector<int>{ 3 });
    CHECK(selection_signals == single_selection_signals + 1);
    const int single_current_signals = current_signals;
    view.set_current_row(3); // no change
    CHECK(selection_signals == single_selection_signals + 1);
    CHECK(current_signals == single_current_signals);
    view.set_current_row(-1);
    CHECK(view.selected_rows().empty());
    CHECK(current_signals == single_current_signals + 1);

    // MULTI: a plain click selects one row and sets the anchor; Ctrl toggles without moving it.
    view.set_selection_mode(selection_mode_enum::MULTI);
    CHECK(view.selection_mode() == selection_mode_enum::MULTI);
    click_row_in_view(view, 1);
    CHECK(view.current_row() == 1);
    CHECK(view.selected_rows() == std::vector<int>{ 1 });
    selection_signals = 0;
    current_signals = 0;

    click_row_in_view(view, 3, modifiers_of(modifier_flag::CONTROL));
    CHECK(view.current_row() == 3);
    CHECK((view.selected_rows() == std::vector<int>{ 1, 3 }));
    CHECK(selection_signals == 1);
    CHECK(current_signals == 1);
    CHECK(view.is_row_selected(1));
    CHECK(view.is_row_selected(3));

    // Ctrl again removes the row; the anchor is still row 1, so Shift extends from 1.
    click_row_in_view(view, 1, modifiers_of(modifier_flag::CONTROL));
    CHECK(view.current_row() == 1);
    CHECK(view.selected_rows() == std::vector<int>{ 3 });
    CHECK(selection_signals == 2);

    click_row_in_view(view, 4, modifiers_of(modifier_flag::SHIFT));
    CHECK(view.current_row() == 4);
    CHECK((view.selected_rows() == std::vector<int>{ 1, 2, 3, 4 }));
    CHECK(selection_signals == 3);

    click_row_in_view(view, 2, modifiers_of(modifier_flag::SHIFT));
    CHECK(view.selected_rows() == (std::vector<int>{ 1, 2 }));
    CHECK(selection_signals == 4);

    // A plain click resets the anchor to the clicked row.
    click_row_in_view(view, 5);
    CHECK(view.selected_rows() == std::vector<int>{ 5 });
    CHECK(selection_signals == 5);
    // Ctrl on the already selected row toggles it off and leaves the current row at 5.
    click_row_in_view(view, 5, modifiers_of(modifier_flag::CONTROL));
    CHECK(view.current_row() == 5);
    CHECK(view.selected_rows().empty());
    CHECK(selection_signals == 6);

    // A click that selects the already selected row changes nothing: no signal.
    click_row_in_view(view, 6);
    CHECK(view.selected_rows() == std::vector<int>{ 6 });
    selection_signals = 0;
    current_signals = 0;
    click_row_in_view(view, 6);
    CHECK(selection_signals == 0);
    CHECK(current_signals == 0);

    // Shift + arrow keys extend from the anchor through the keyboard.
    input_router_t router{ view };
    router.set_focus(&view);
    click_row_in_view(view, 2);
    CHECK(view.selected_rows() == std::vector<int>{ 2 });
    CHECK(router.dispatch(key_step(interaction_t::down_key,
                                   modifiers_of(modifier_flag::SHIFT))));
    CHECK(view.current_row() == 3);
    CHECK((view.selected_rows() == std::vector<int>{ 2, 3 }));
    CHECK(router.dispatch(key_step(interaction_t::down_key,
                                   modifiers_of(modifier_flag::SHIFT))));
    CHECK(view.current_row() == 4);
    CHECK((view.selected_rows() == std::vector<int>{ 2, 3, 4 }));
    CHECK(router.dispatch(key_step(interaction_t::up_key,
                                   modifiers_of(modifier_flag::SHIFT))));
    CHECK(view.current_row() == 3);
    CHECK((view.selected_rows() == std::vector<int>{ 2, 3 }));

    // An out-of-range row is never selected.
    CHECK(!view.is_row_selected(-1));
    CHECK(!view.is_row_selected(50));
}

void test_model_notifications() {
    std::printf("(7) model notifications damage exactly their rows; model_reset clears state\n");
    probe_model_t model{ 100 };
    list_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 160 });
    view.set_model(&model);

    // data_changed(2, 4): rows 2..4, three full 30-pixel bands, viewport width 226 (bar visible).
    clear_damages(view);
    model.notify_data_changed(2, 4);
    CHECK(covers(view.damage(), rect_t{ 0, 2 * row_h(), 226, 3 * row_h() }));
    CHECK(view.damage().bounds() == (rect_t{ 0, 2 * row_h(), 226, 3 * row_h() }));
    CHECK(view.damage().area() == static_cast<std::size_t>(3 * row_h() * 226));

    // A notification that only touches rows outside the viewport damages nothing.
    clear_damages(view);
    model.notify_data_changed(90, 99);
    CHECK(view.damage().is_empty());

    // Out-of-range rows are clamped; the visible part of the range is what gets damaged.
    clear_damages(view);
    model.notify_data_changed(-5, 1);
    CHECK(covers(view.damage(), rect_t{ 0, 0, 226, 2 * row_h() }));
    CHECK(view.damage().area() == static_cast<std::size_t>(2 * row_h() * 226));

    // rows_inserted: the whole viewport is damaged and the bar range grows. 3 rows -> 20 rows also
    // makes the bar appear, so the damaged viewport narrows to 226.
    probe_model_t small{ 3 };
    list_view_t small_view;
    small_view.set_geometry(rect_t{ 0, 0, 240, 160 });
    small_view.set_model(&small);
    CHECK(!bar_of(small_view)->is_visible());
    small.set_rows(20);
    clear_damages(small_view);
    small.notify_rows_inserted(3, 17);
    // The viewport AND the newly visible bar are repainted: together they cover the whole list.
    CHECK(covers(small_view.damage(), rect_t{ 0, 0, 240, 160 }));
    CHECK(small_view.damage().area()
          == static_cast<std::size_t>(240 * 160));
    CHECK(bar_of(small_view)->is_visible());
    CHECK(bar_of(small_view)->maximum() == 20 * row_h() - 160);
    CHECK(bar_of(small_view)->geometry() == (rect_t{ 226, 0, 14, 160 }));

    // rows_removed: current row and offset are clamped, the viewport is damaged.
    probe_model_t big{ 100 };
    list_view_t big_view;
    big_view.set_geometry(rect_t{ 0, 0, 240, 160 });
    big_view.set_model(&big);
    big_view.scroll_to_row(90);
    big_view.set_current_row(95);
    CHECK(big_view.scroll_offset().y() == 90 * row_h() + row_h() - 160);
    big.set_rows(10);
    clear_damages(big_view);
    big.notify_rows_removed(10, 90);
    CHECK(big_view.current_row() == 9);
    CHECK(big_view.scroll_offset().y() == 10 * row_h() - 160);
    // The viewport is damaged, and the bar's value/range also moved, so its band is repainted too.
    CHECK(covers(big_view.damage(), rect_t{ 0, 0, 226, 160 }));
    CHECK(covers(big_view.damage(), rect_t{ 226, 0, 14, 160 }));
    CHECK(bar_of(big_view)->maximum() == 10 * row_h() - 160);
    CHECK(bar_of(big_view)->value() == big_view.scroll_offset().y());

    // model_reset: offset 0, current -1, no selection; the bar disappears for a short model.
    probe_model_t reset_model{ 40 };
    list_view_t reset_view;
    reset_view.set_geometry(rect_t{ 0, 0, 240, 160 });
    reset_view.set_model(&reset_model);
    reset_view.set_selection_mode(selection_mode_enum::MULTI);
    reset_view.scroll_to_row(30);
    reset_view.set_current_row(30);
    reset_view.set_current_row(31); // MULTI: current changes, selection is set by the click path below
    click_row_in_view(reset_view, 30);
    CHECK(!reset_view.selected_rows().empty());
    CHECK(reset_view.scroll_offset().y() > 0);
    reset_model.set_rows(3);
    clear_damages(reset_view);
    reset_model.notify_model_reset();
    CHECK(reset_view.current_row() == -1);
    CHECK(reset_view.scroll_offset() == (point_t{ 0, 0 }));
    CHECK(reset_view.selected_rows().empty());
    CHECK(!bar_of(reset_view)->is_visible());
    CHECK(bar_of(reset_view)->maximum() == 0);
    CHECK(covers(reset_view.damage(), rect_t{ 0, 0, 240, 160 }));
}

void test_empty_and_shrink() {
    std::printf("(8) empty model, row-count shrinkage, clamping\n");
    probe_model_t empty{ 0 };
    list_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 160 });
    view.set_model(&empty);
    CHECK(view.model() == &empty);
    CHECK(empty.row_count() == 0);
    CHECK(view.current_row() == -1);
    CHECK(view.selected_rows().empty());
    CHECK(view.scroll_offset() == (point_t{ 0, 0 }));
    CHECK(!bar_of(view)->is_visible());
    CHECK(bar_of(view)->maximum() == 0);
    CHECK(view.size_hint() == (extent_t{ 160, 160 }));
    empty.forget_calls();
    paint_full(view);
    CHECK(empty.data_calls == 0);
    CHECK(!view.interact(wheel_step(point_t{ 0, -1 })));
    view.scroll_to_row(0); // out of range: ignored
    CHECK(view.scroll_offset() == (point_t{ 0, 0 }));

    // Shrink: 100 rows -> 10 rows clamps the current row and the offset.
    probe_model_t model{ 100 };
    list_view_t shrink;
    shrink.set_geometry(rect_t{ 0, 0, 240, 160 });
    shrink.set_model(&model);
    shrink.scroll_to_row(90);
    shrink.set_current_row(95);
    model.set_rows(10);
    model.notify_rows_removed(10, 90);
    CHECK(shrink.current_row() == 9);
    CHECK(shrink.selected_rows() == std::vector<int>{ 9 }); // SINGLE: current is the selection
    CHECK(shrink.scroll_offset().y() == 10 * row_h() - 160);

    // Further shrink to three rows clamps the offset to zero.
    model.set_rows(3);
    model.notify_rows_removed(3, 7);
    CHECK(shrink.current_row() == 2);
    CHECK(shrink.selected_rows() == std::vector<int>{ 2 });
    CHECK(shrink.scroll_offset().y() == 0);
    CHECK(!bar_of(shrink)->is_visible());

    // To zero rows empties the view.
    model.set_rows(0);
    model.notify_rows_removed(0, 3);
    CHECK(shrink.current_row() == -1);
    CHECK(shrink.selected_rows().empty());
    CHECK(shrink.scroll_offset().y() == 0);
    CHECK(!bar_of(shrink)->is_visible());

    // The model dies while installed: the view forgets it and resets its state.
    auto* const dying = new probe_model_t{ 12 };
    list_view_t survivor;
    survivor.set_geometry(rect_t{ 0, 0, 240, 160 });
    survivor.set_model(dying);
    survivor.set_current_row(4);
    survivor.scroll_to_row(4);
    delete dying;
    CHECK(survivor.model() == nullptr);
    CHECK(survivor.current_row() == -1);
    CHECK(survivor.selected_rows().empty());
    CHECK(survivor.scroll_offset().y() == 0);
    CHECK(!bar_of(survivor)->is_visible());
}

void test_scroll_bar_sync() {
    std::printf("(9) scroll bar synchronisation and the scroll fast path\n");
    probe_model_t model{ 10 };
    list_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 160 });
    view.set_model(&model);

    scroll_bar_t* const bar = bar_of(view);
    CHECK(bar != nullptr);
    CHECK(bar->is_visible());
    CHECK(bar->geometry() == (rect_t{ 226, 0, 14, 160 }));
    CHECK(bar->minimum() == 0);
    CHECK(bar->maximum() == 10 * row_h() - 160);
    CHECK(bar->page_step() == 160);

    view.scroll_to_row(9);
    CHECK(view.scroll_offset().y() == 140);
    CHECK(bar->value() == 140);

    // The bar is an ordinary control: writing its value is followed by the list.
    bar->set_value(70);
    CHECK(view.scroll_offset().y() == 70);

    // Resize: the bar follows the new width, and the range follows the height.
    view.set_geometry(rect_t{ 0, 0, 300, 100 });
    CHECK(bar->geometry() == (rect_t{ 286, 0, 14, 100 }));
    CHECK(bar->maximum() == 10 * row_h() - 100);

    // The scroll fast path: with a scroll sink installed, a small step shifts already-painted pixels
    // and does not damage the viewport; a step of at least one viewport repaints instead.
    int shifts = 0;
    int last_dy = 0;
    // The receiver plays both halves of the fast path: the window answers the band the shift exposed and
    // moves its pending damage with the pixels; the tree records exactly that region.
    request_sink_t sink;
    sink.on_scroll = [&view, &shifts, &last_dy](const scroll_event_t& scroll) {
        CHECK(scroll.dx() == 0);
        ++shifts;
        last_dy = scroll.dy();
        const region_t exposed = window_t::scroll_exposed_region(scroll.area(), scroll.dx(), scroll.dy());
        view.translate_damage(scroll.area(), scroll.dx(), scroll.dy());
        for (const rect_t& strip : exposed.rects()) {
            view.update(strip);
        }
    };
    view.set_request_receiver(&sink);

    // 70 -> 200 is a 130-pixel step under a 100-pixel viewport: the repaint path.
    shifts = 0;
    clear_damages(view);
    view.scroll_to_row(9);
    CHECK(view.scroll_offset().y() == 200);
    CHECK(shifts == 0);
    CHECK(covers(view.damage(), rect_t{ 0, 0, 286, 100 }));
    CHECK(view.damage().area() == static_cast<std::size_t>(300 * 100));

    // A small wheel step shifts pixels: the host sees the shift, and the tree records the bar band
    // (the handle moved), the strip the shift exposed and the three frame lines the shift does NOT
    // move: the viewport's top and bottom rows and its left column (the bar owns the right edge).
    // Those lines are repainted in place, so the viewport's interior is still not repainted.
    shifts = 0;
    clear_damages(view);
    CHECK(view.interact(wheel_step(point_t{ 0, 1 }))); // a wheel away from the user scrolls up
    CHECK(view.scroll_offset().y() == 200 - default_style().pixel_metric(pixel_metric_enum::SCROLL_WHEEL_STEP));
    CHECK(shifts == 1);
    CHECK(last_dy == default_style().pixel_metric(pixel_metric_enum::SCROLL_WHEEL_STEP));
    CHECK(covers(view.damage(), bar->geometry()));
    // The content moved DOWN by one wheel step, so the band it uncovered is the TOP one.
    CHECK(covers(view.damage(), rect_t{ 0, 0, 286, default_style().pixel_metric(
                                                   pixel_metric_enum::SCROLL_WHEEL_STEP) }));
    // The shifted content is the viewport minus the frame lines: 285 x 98 px of a 286 x 100 viewport.
    // The damage is therefore the bar (14 x 100), that content's exposed 285 x 16 strip and the three
    // frame lines (286 + 286 + 98 px).
    const std::size_t frame_area = 286u + 286u + (100u - 2u);
    CHECK(view.damage().area()
          == static_cast<std::size_t>(14 * 100 + 285 * default_style().pixel_metric(
                                                        pixel_metric_enum::SCROLL_WHEEL_STEP))
              + frame_area);
    // ... and the fast path really is not a whole-viewport repaint.
    CHECK(!covers(view.damage(), rect_t{ 0, 0, 300, 100 }));

    // A full-viewport step back to the top takes the repaint path again.
    shifts = 0;
    clear_damages(view);
    view.scroll_to_row(0);
    CHECK(view.scroll_offset().y() == 0);
    CHECK(shifts == 0);
    CHECK(covers(view.damage(), rect_t{ 0, 0, 286, 100 }));

    // Reset hides the bar again.
    model.set_rows(2);
    model.notify_model_reset();
    CHECK(!bar->is_visible());
    CHECK(bar->maximum() == 0);
    CHECK(bar->value() == 0);
}

void test_fast_hover_move() {
    std::printf("(10) a fast hover move leaves exactly one hovered row after the next frame\n");
    probe_model_t model{ 100 };
    list_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 160 });
    view.set_model(&model);
    input_router_t router{ view };

    frame_surface_t surface{ extent_t{ 240, 160 } };
    surface.paint_whole(view);

    // Frame 1: the pointer rests on row 2 and the frame paints that hover.
    CHECK(!router.dispatch(move(row_point(2))));
    CHECK(view.is_hovered());
    CHECK(surface.paint_damage(view) > 0);
    CHECK(pixel_is(surface, 200, row_sample_y(2, row_h(), 0), row_hover_fill_color));
    clear_damages(view);

    // Frame 2: one motion batch, the pointer sweeps 3 -> 4 -> 5 with no frame in between. Row 5 is
    // the one only partly visible under the 160-pixel viewport, so its centre is at y = view.rect().height() - 5.
    for (int row = 3; row <= 5; ++row) {
        CHECK(!router.dispatch(move(row == 5 ? point_t{ 10, view.rect().height() - 5 } : row_point(row))));
    }
    CHECK(view.is_hovered());

    // The frame that follows must repaint the row the pointer LEFT (row 2, painted by frame 1) as
    // well as the one it reached.
    CHECK(covers(view.damage(), rect_t{ 0, 2 * row_h(), 226, row_h() }));
    CHECK(covers(view.damage(), rect_t{ 0, 5 * row_h(), 226, view.rect().height() - 5 * row_h() }));
    CHECK(surface.paint_damage(view) > 0);

    int hovered_rows = 0;
    for (int row = 0; row < 6; ++row) {
        const int y = row == 5 ? view.rect().height() - 5 : row_sample_y(row, row_h(), 0);
        hovered_rows += pixel_is(surface, 200, y, row_hover_fill_color) ? 1 : 0;
    }
    CHECK(hovered_rows == 1);
    CHECK(pixel_is(surface, 200, row_sample_y(2, row_h(), 0), row_fill_color));
    CHECK(pixel_is(surface, 200, view.rect().height() - 5, row_hover_fill_color));
}

void test_partial_row_drawn_then_clipped() {
    std::printf("(11) a non-multiple viewport: the last row is painted whole and clipped\n");
    probe_model_t model{ 100 };
    list_view_t view;
    view.set_geometry(rect_t{ 0, 0, 300, 100 });
    view.set_model(&model);
    view.set_row_height(30);
    view.set_current_row(3); // rows 0..2 are whole, row 3 is the 10-pixel remainder

    image_t image{ extent_t{ 300, 100 } };
    painter_t painter{ image };
    painter.begin();
    painter.clear(color_t::white_color());
    view.render(painter);
    painter.end();

    // The row background covers the row's whole visible remainder, down to the viewport's last row;
    // the outline sits on the row's true top edge (y = 90), not on the clip boundary.
    CHECK(pixel_is(image, 250, 90, row_current_outline_color));
    CHECK(pixel_is(image, 250, 91, row_selected_fill_color));
    CHECK(pixel_is(image, 250, 99, row_selected_fill_color));
    // The rows above are unaffected.
    CHECK(pixel_is(image, 250, 89, row_fill_color));

    // The text cell is centred in the FULL 30-pixel row, so the 10 visible pixels cut it away whole:
    // a text pixel down here means the row was painted as a 10-pixel-high row.
    CHECK(count_color(image, rect_t{ 0, 90, 286, 10 }, row_text_color) == 0);
    // The current-row outline is a full-row rectangle too: its bottom edge is at y = 119 and is
    // clipped, so the viewport's last pixel row may only carry its two vertical edges.
    CHECK(count_color(image, rect_t{ 1, 99, 284, 1 }, row_current_outline_color) == 0);
}

void test_scroll_fast_path_matches_full_repaint() {
    std::printf("(12) the scroll fast path and a full repaint produce the same pixels\n");
    probe_model_t model{ 100 };
    list_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 160 });
    view.set_model(&model);
    view.set_row_height(30);
    view.set_current_row(3);

    frame_surface_t fast{ extent_t{ 240, 160 } };
    fast.paint_whole(view);
    image_scroll_host_t host{ fast };
    request_sink_t scroll_sink;
    scroll_sink.on_scroll = [&host, &view](const scroll_event_t& scroll) {
        host(view, scroll.area(), scroll.dx(), scroll.dy());
    };
    view.set_request_receiver(&scroll_sink);

    /// Paint the frame the scroll path asked for, then compare it with the same state painted from
    /// scratch. A mismatch means the shifted pixels and the repainted band disagree.
    const auto frame_matches_full_repaint = [&](const char* what) {
        CHECK(fast.paint_damage(view) > 0);
        host.frame_painted(); // the next shift starts a new band
        frame_surface_t whole{ extent_t{ 240, 160 } };
        whole.paint_whole(view);
        const int different = fast.differences(whole);
        if (different != 0) {
            std::printf("       %s: fast path differs in %d pixel(s), first at y = %d\n", what,
                        different, fast.first_different_row(whole));
        }
        CHECK(different == 0);
    };

    // A wheel step of 16 pixels: not a row multiple, so both the top and the bottom rows are only
    // partly visible and the shift's result can only match if the rows are painted position-relative.
    // The pointer stays over the list while it scrolls, so the hover row moves with the content and
    // the frame has to repaint the old and the new hover band as well.
    CHECK(view.interact(wheel_at(point_t{ 10, 75 }, point_t{ 0, -1 })));
    CHECK(view.scroll_offset().y() > 0);
    CHECK(view.scroll_offset().y() % view.row_height() != 0);
    CHECK(host.shifts == 1);
    CHECK(host.last_dy == -view.scroll_offset().y());
    frame_matches_full_repaint("wheel down 16");

    // A second step, then back up across the old top row, then a bar drag of 45 pixels.
    CHECK(view.interact(wheel_at(point_t{ 10, 105 }, point_t{ 0, -1 })));
    frame_matches_full_repaint("wheel down 32");
    CHECK(view.interact(wheel_at(point_t{ 10, 65 }, point_t{ 0, 1 })));
    frame_matches_full_repaint("wheel up 16");
    scroll_bar_t* const bar = bar_of(view);
    CHECK(bar != nullptr);
    bar->set_value(view.scroll_offset().y() + 45);
    CHECK(view.scroll_offset().y() == 45 + 16);
    frame_matches_full_repaint("bar drag 45");

    // A step of at least one viewport takes the repaint path; it has to match too.
    view.scroll_to_row(20);
    CHECK(host.shifts == 4); // no shift was requested: the whole viewport was damaged instead
    frame_matches_full_repaint("scroll_to_row 20");

    // A wheel BURST: two steps with no frame in between. The second shift moves the pixels that the
    // first step had just damaged, so the single frame that follows has to repaint the moved band as
    // well; a damage band left in window coordinates would keep half a hover highlight on screen.
    probe_model_t burst_model{ 100 };
    list_view_t burst;
    burst.set_geometry(rect_t{ 0, 0, 240, 160 });
    burst.set_model(&burst_model);
    burst.set_row_height(30);
    frame_surface_t burst_surface{ extent_t{ 240, 160 } };
    burst_surface.paint_whole(burst);
    image_scroll_host_t burst_host{ burst_surface };
    request_sink_t burst_scroll_sink;
    burst_scroll_sink.on_scroll = [&burst_host, &burst](const scroll_event_t& scroll) {
        burst_host(burst, scroll.area(), scroll.dx(), scroll.dy());
    };
    burst.set_request_receiver(&burst_scroll_sink);
    CHECK(!burst.interact(move(point_t{ 10, 75 }))); // hover row 2
    burst_surface.paint_whole(burst);                // frame: the hover reaches the pixels
    CHECK(burst.interact(wheel_at(point_t{ 10, 75 }, point_t{ 0, -1 })));
    CHECK(burst.interact(wheel_at(point_t{ 10, 75 }, point_t{ 0, -1 })));
    CHECK(burst.scroll_offset().y() == 2 * default_style().pixel_metric(
                                                pixel_metric_enum::SCROLL_WHEEL_STEP));
    CHECK(burst_host.shifts == 2);
    CHECK(burst_surface.paint_damage(burst) > 0);
    burst_host.frame_painted(); // the burst is over: the next shift starts a new band
    frame_surface_t burst_whole{ extent_t{ 240, 160 } };
    burst_whole.paint_whole(burst);
    const int burst_different = burst_surface.differences(burst_whole);
    if (burst_different != 0) {
        std::printf("       burst: fast path differs in %d pixel(s), first at y = %d\n",
                    burst_different, burst_surface.first_different_row(burst_whole));
    }
    CHECK(burst_different == 0);
}

void test_current_row_change_repaints_both_rows() {
    std::printf("(13) a current-row/selection change repaints the old and the new row\n");
    probe_model_t model{ 100 };
    list_view_t view;
    view.set_geometry(rect_t{ 0, 0, 240, 160 });
    view.set_model(&model);
    input_router_t router{ view };
    router.set_focus(&view);
    CHECK(view.has_focus());

    frame_surface_t surface{ extent_t{ 240, 160 } };
    surface.paint_whole(view);
    clear_damages(view);

    // The keyboard walks the current row down while every row is already visible: nothing scrolls,
    // so no scroll damage can hide the fact that the highlight moved to another row.
    CHECK(router.dispatch(key_step(interaction_t::down_key)));
    CHECK(view.current_row() == 0);
    CHECK(surface.paint_damage(view) > 0);
    CHECK(pixel_is(surface, 200, row_sample_y(0, row_h(), 0), row_selected_fill_color));

    CHECK(router.dispatch(key_step(interaction_t::down_key)));
    CHECK(view.current_row() == 1);
    CHECK(surface.paint_damage(view) > 0);

    // Row 0 lost the selection and the outline; row 1 carries both now, and nothing else does.
    CHECK(pixel_is(surface, 200, row_sample_y(0, row_h(), 0), row_fill_color));
    CHECK(pixel_is(surface, 200, row_sample_y(1, row_h(), 0), row_selected_fill_color));
    int outline_rows = 0;
    for (int row = 0; row < 6; ++row) {
        outline_rows += pixel_is(surface, 200, row * row_h(), row_current_outline_color) ? 1 : 0;
    }
    CHECK(outline_rows == 1);

    // A selection change that leaves the old selection behind: MULTI + Shift+Down selects rows 1..2,
    // so row 1 keeps its fill but loses the current outline while row 2 gains both.
    view.set_selection_mode(selection_mode_enum::MULTI);
    surface.paint_whole(view);
    clear_damages(view);
    CHECK(router.dispatch(
        key_step(interaction_t::down_key, modifiers_of(modifier_flag::SHIFT))));
    CHECK(view.current_row() == 2);
    CHECK((view.selected_rows() == std::vector<int>{ 1, 2 }));
    CHECK(surface.paint_damage(view) > 0);
    CHECK(pixel_is(surface, 200, row_sample_y(1, row_h(), 0), row_selected_fill_color));
    CHECK(!pixel_is(surface, 200, 1 * row_h(), row_current_outline_color));
    CHECK(pixel_is(surface, 200, row_sample_y(2, row_h(), 0), row_selected_fill_color));
    CHECK(pixel_is(surface, 200, 2 * row_h(), row_current_outline_color));

    // MULTI + a PROGRAMMATIC current row: the selected set stays {1, 2}, so only the outline moves
    // and the damage has to come from the current-row change itself.
    view.set_current_row(4);
    CHECK(view.current_row() == 4);
    CHECK((view.selected_rows() == std::vector<int>{ 1, 2 }));
    CHECK(surface.paint_damage(view) > 0);
    CHECK(!pixel_is(surface, 200, 2 * row_h(), row_current_outline_color));
    CHECK(pixel_is(surface, 200, 4 * row_h(), row_current_outline_color));
    CHECK(pixel_is(surface, 200, row_sample_y(2, row_h(), 0), row_selected_fill_color));

    // A selection change that does NOT move the current row: switching MULTI -> SINGLE collapses the
    // set onto the current row, so rows 1 and 2 lose the fill while row 4 stays current and gains it.
    view.set_selection_mode(selection_mode_enum::SINGLE);
    CHECK(view.current_row() == 4);
    CHECK((view.selected_rows() == std::vector<int>{ 4 }));
    CHECK(surface.paint_damage(view) > 0);
    CHECK(pixel_is(surface, 200, row_sample_y(1, row_h(), 0), row_fill_color));
    CHECK(pixel_is(surface, 200, row_sample_y(2, row_h(), 0), row_fill_color));
    CHECK(pixel_is(surface, 200, row_sample_y(4, row_h(), 0), row_selected_fill_color));
    CHECK(pixel_is(surface, 200, 4 * row_h(), row_current_outline_color));
}

}  // namespace list_view_test

int main() {
    pin_builtin_font_family(); // deterministic text: the built-in face, not the machine's UI font
    using namespace list_view_test;
    std::printf("== list_view_test ==\n");
    test_geometry_and_size();
    test_virtualisation();
    test_keyboard();
    test_wheel();
    test_click_and_activation();
    test_selection_modes();
    test_model_notifications();
    test_empty_and_shrink();
    test_scroll_bar_sync();
    test_fast_hover_move();
    test_partial_row_drawn_then_clipped();
    test_scroll_fast_path_matches_full_repaint();
    test_current_row_change_repaints_both_rows();
    // ---- dual-face verification --------------------------------------------------------------
    // The SAME font-independent assertions run under both faces: the built-in bitmap face and the
    // platform UI face. If any of them had a number baked in, the second pass would fail (the line
    // height changes from 8 to the system face's, and every derived band grows with it).
    {
        const auto check_face = []() {
            const style_t& face_style = default_style();
            const ::hwshqtb::gui::font_metrics_t face_metrics{};
            CHECK(face_style.pixel_metric(pixel_metric_enum::LINE_HEIGHT) >= face_metrics.line_height());
            CHECK(face_style.pixel_metric(pixel_metric_enum::TITLE_HEIGHT) >= face_metrics.line_height());
            CHECK(face_style.pixel_metric(pixel_metric_enum::TAB_HEIGHT) >= face_metrics.line_height());
            CHECK(face_style.pixel_metric(pixel_metric_enum::BUTTON_HEIGHT) >= face_metrics.line_height());
            list_view_t probe;
            probe.set_geometry(rect_t{ 0, 0, 240, 160 });
            CHECK(probe.row_height() == row_h());
            CHECK(probe.row_height() >= face_metrics.line_height());
        };

        check_face(); // pass 1: the pinned built-in face
        ::hwshqtb::gui::font_database_t::instance().set_default_family(std::string_view{});
        if (::hwshqtb::gui::font_database_t::instance().default_family()
            == ::hwshqtb::gui::font_database_t::builtin_family()) {
            std::printf("[list_view] no system font face: the second pass is skipped\n");
        }
        else {
            check_face(); // pass 2: the platform face, with different metrics
            std::printf("[list_view] dual-face pass: built-in row %d, system row %d\n",
                        hwshqtb::gui::font_metrics_t{
                            font_t{ ::hwshqtb::gui::font_database_t::builtin_family(), 0 } }
                                .line_height(),
                        ::hwshqtb::gui::font_metrics_t{}.line_height());
        }
        pin_builtin_font_family(); // restore the deterministic face for anything after this
    }

    std::printf("checks: %d, failures: %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
