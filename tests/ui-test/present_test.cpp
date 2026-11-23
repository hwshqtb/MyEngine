// The PRESENTED frame: the pixels a user actually sees. Every other ui test asserts on the backing
// surface (or on a mirror of the paint passes), and that is why the two defects pinned here survived
// the whole earlier test suite: the surface is correct, the presentation is not.
//
// The window target is not a persistent surface. SDL's Direct3D 11 renderer — the one a real window
// gets on Windows — creates the swap chain with DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL and two buffers
// (external/SDL3/src/render/direct3d11/SDL_render_d3d11.c:865), so the buffer a frame renders into
// holds the pixels of the frame TWO presents ago. Measured on this machine, four frames, each filling
// one small rectangle and reading the new back buffer back:
//
//   after frame 0 (full red)     (10,10)=0,0,0     (100,100)=0,0,0     <- never rendered, undefined
//   after frame 1 (green 20x20)  (10,10)=255,0,0   (100,100)=255,0,0   <- frame 0's red, not frame 1's
//   after frame 2 (blue 20x20)   (10,10)=0,255,0   (100,100)=0,0,0     <- frame 1's green + its black
//   after frame 3 (yellow 20x20) (10,10)=255,0,0   (100,100)=255,0,0   <- frame 2 is green+black again
//
// The same probe under the dummy video driver (the software renderer this suite runs on) shows the
// red surviving every partial frame: the target there IS persistent, which is exactly why an
// offscreen test cannot see either defect. So this file models the target as measured — two buffers,
// a frame writing into the one used two presents ago — instead of hoping the driver is generous.
//
// The presentation rule was measured on the same driver with a static surface (six stripes, never
// repainted) and a read-back after every present. Flushing only a 16 px strip — what a frame used to
// do, because that was all it painted — leaves every presented frame 9600 px (120x80) away from the
// surface, forever. Flushing the whole client area is exact from the second present on (0 px on
// frames 1..3). So the copy to the target is where a frame is whole, while the painting stays
// partial: that is the split the sections below pin.
//
// A frame must therefore present EVERY pixel of the client area. The tested invariants:
//  (1) the scroll fast path (defect B): shift a viewport with window_t::scroll(), then run ONE real
//      frame; the presented pixels must equal a full repaint of the same state. The surface is fine
//      (its pixels moved and the exposed strip was repainted), the presented frame is not, because
//      the moved pixels were never copied to the target.
//  (2) a hover sweep (defect A): several motions, one frame each. The hover fill of a row the pointer
//      left two frames ago comes back, because the band it painted into was overwritten by the
//      OTHER buffer two presents later — the discrete stale rows a user sees after a fast sweep.
//  (3) a wheel burst on a scroll_view_t (defect C): several wheel ticks reach the container in ONE
//      frame, so its fast path shifts the same rectangle several times before anything is painted.
//      Every shift drags the band the previous one exposed further into the viewport, so the frame
//      has to paint the band of the ACCUMULATED shift; a frame that paints only the band of the last
//      step leaves a band of duplicated content — the light stripes a fast scroll showed.
//
// Each step asserts on both buffers — the surface the frame painted and the frame the target shows —
// so a failure says which half is wrong.
//
// The window is real (SDL window, renderer and backing surface) and runs under the dummy video driver,
// so no display is needed. The surface mirror replays every pass the window paints (same clip, same
// clear, same tree) and moves its pixels for a scroll exactly as the backing surface does (a shift
// inside the rectangle, the uncovered band untouched). It does not re-verify that pixel move itself:
// the shift primitive is the subject of the surface-level check, not of this file.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/gui_application_t.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/gui/region_t.h"
#include "hwshqtb/metaobject/any_t.h"
#include "hwshqtb/metaobject/core_application.h"
#include "hwshqtb/metaobject/event_dispatcher.h"
#include "hwshqtb/ui/item_model_t.h"
#include "hwshqtb/ui/list_view_t.h"
#include "hwshqtb/ui/scroll_view_t.h"
#include "hwshqtb/ui/widget_t.h"
#include "hwshqtb/ui/widget_window_t.h"

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;
using namespace hwshqtb::ui;

namespace present_test {

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

/// The fill of a list row the pointer is not over (default_style_t's LIST_ROW).
constexpr color_t row_fill_color{ 30, 42, 56 };
/// The fill of a list row the pointer is over (default_style_t's LIST_ROW hovered variant).
constexpr color_t row_hover_fill_color{ 40, 55, 70 };

/// Whether @p region covers every pixel of @p rect.
bool covers(const region_t& region, rect_t rect) {
    return region_t{ rect }.subtracted(region).is_empty();
}

/// Print a region as "x,y,w,h" rectangles (the damage/pass/presented probe data of a failure).
void print_region(const char* what, const region_t& region) {
    std::printf("       %s: %zu rect(s)", what, region.rectangle_count());
    for (const rect_t& area : region.rects()) {
        std::printf(" [%d,%d %dx%d]", area.x(), area.y(), area.width(), area.height());
    }
    std::printf("\n");
}

/// A client area as plain pixels: what a surface or a presented frame holds.
class pixels_t {
public:
    /// @param size The client area.
    /// @param fill The colour every pixel starts with.
    pixels_t(extent_t size, color_t fill):
        _size(size),
        _words(static_cast<std::size_t>(size.width()) * static_cast<std::size_t>(size.height()),
               fill) { }

    /// @brief The client area the buffer holds.
    extent_t size() const noexcept {
        return _size;
    }

    /// @brief The colour of one pixel; out-of-range coordinates answer transparent.
    color_t pixel(int x, int y) const {
        if (x < 0 || y < 0 || x >= _size.width() || y >= _size.height()) {
            return color_t::transparent_color();
        }
        return _words[static_cast<std::size_t>(y) * static_cast<std::size_t>(_size.width())
                      + static_cast<std::size_t>(x)];
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

    /// @brief Copy @p area's pixels of @p other into this buffer.
    void copy_rect(const pixels_t& other, rect_t area) {
        const rect_t clipped = area.intersected(rect_t{ point_t{}, _size });
        for (int y = clipped.y(); y < clipped.bottom(); ++y) {
            for (int x = clipped.x(); x < clipped.right(); ++x) {
                set(x, y, other.pixel(x, y));
            }
        }
    }

    /// @brief Move the pixels inside @p area by (@p dx, @p dy), as the backing surface's scroll does:
    ///        whatever the shift uncovers keeps its previous pixels.
    void shift(rect_t area, int dx, int dy) {
        const rect_t clipped = area.intersected(rect_t{ point_t{}, _size });
        if (clipped.isEmpty() || (dx == 0 && dy == 0)) {
            return;
        }
        const pixels_t before = *this;
        for (int y = clipped.y(); y < clipped.bottom(); ++y) {
            for (int x = clipped.x(); x < clipped.right(); ++x) {
                const int source_x = x - dx;
                const int source_y = y - dy;
                if (clipped.contains(point_t{ source_x, source_y })) {
                    set(x, y, before.pixel(source_x, source_y));
                }
            }
        }
    }

    /// @brief How many pixels differ from @p expected.
    int differences(const pixels_t& expected) const {
        if (_size != expected.size()) {
            return _size.width() * _size.height();
        }
        int count = 0;
        for (int y = 0; y < _size.height(); ++y) {
            for (int x = 0; x < _size.width(); ++x) {
                if (!(pixel(x, y) == expected.pixel(x, y))) {
                    ++count;
                }
            }
        }
        return count;
    }

    /// @brief Print where and how the pixels differ from @p expected.
    void dump_differences(const pixels_t& expected, const char* what) const {
        int left = _size.width();
        int top = _size.height();
        int right = 0;
        int bottom = 0;
        int differing = 0;
        int printed = 0;
        const int limit = 8;
        for (int y = 0; y < _size.height(); ++y) {
            for (int x = 0; x < _size.width(); ++x) {
                if (pixel(x, y) == expected.pixel(x, y)) {
                    continue;
                }
                ++differing;
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x + 1);
                bottom = std::max(bottom, y + 1);
                if (printed < limit) {
                    const color_t actual = pixel(x, y);
                    const color_t wanted = expected.pixel(x, y);
                    std::printf("       %s: (%d, %d) presented %d,%d,%d expected %d,%d,%d\n", what, x,
                                y, actual.red(), actual.green(), actual.blue(), wanted.red(),
                                wanted.green(), wanted.blue());
                    ++printed;
                }
            }
        }
        if (differing > 0) {
            std::printf("       %s: %d pixel(s) differ, span (%d, %d) - (%d, %d)\n", what, differing,
                        left, top, right, bottom);
        }
    }

private:
    void set(int x, int y, color_t color) {
        _words[static_cast<std::size_t>(y) * static_cast<std::size_t>(_size.width())
               + static_cast<std::size_t>(x)] = color;
    }

    extent_t _size;
    std::vector<color_t> _words;
};

/// The window target as SDL's Direct3D 11 renderer presents it on Windows.
///
/// Two buffers and a flip: frame N renders into the buffer frame N-2 rendered into, so when the frame
/// starts that buffer holds the pixels presented two presents ago, and the frame overwrites only the
/// region it presents. A present whose region covers the whole client area refreshes both buffers,
/// which is the most generous target a frame can meet — this model never assumes anything worse.
class flip_target_t {
public:
    /// @param size The client area.
    explicit flip_target_t(extent_t size):
        _buffers{ pixels_t{ size, color_t::white_color() }, pixels_t{ size, color_t::white_color() } },
        _whole(size) { }

    /// @brief Present one frame: copy @p presented's rectangles of @p surface into the back buffer.
    /// @param surface The backing surface, as the frame left it.
    /// @param presented The region the frame presented (window_t::presented_region()).
    void present(const pixels_t& surface, const region_t& presented) {
        const std::size_t back = _presents % 2;
        for (const rect_t& area : presented.rects()) {
            _buffers[back].copy_rect(surface, area);
        }
        if (covers(presented, rect_t{ point_t{}, _whole })) {
            _buffers[1 - back] = _buffers[back]; // a full present fills both buffers
        }
        _shown = back;
        ++_presents;
    }

    /// @brief What the last present put on screen.
    const pixels_t& shown() const noexcept {
        return _buffers[_shown];
    }

    /// @brief How many frames were presented.
    std::size_t presents() const noexcept {
        return _presents;
    }

private:
    pixels_t _buffers[2];
    extent_t _whole;
    std::size_t _shown = 0;
    std::size_t _presents = 0;
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

/// A view with a scroll offset and no content of its own: horizontal stripes of different colours,
/// 16 pixels each, so a pixel says exactly which offset painted it.
class striped_view_t final: public widget_t {
public:
    /// @brief The stripe height in pixels.
    static constexpr int stripe_height = 16;

    explicit striped_view_t(widget_t* parent = nullptr):
        widget_t(parent) {
        set_opaque(true);
    }

    /// @brief The scroll offset the stripes are painted at, in pixels.
    /// @note A plain setter: it records no damage, so a test can change what the view would paint
    ///       without asking for a repaint and drive the shift through window_t::scroll() alone.
    void set_offset(int offset) noexcept {
        _offset = offset;
    }

    /// @brief The offset the stripes are painted at.
    int offset() const noexcept {
        return _offset;
    }

protected:
    void paint_event(paint_event_t* event) override {
        painter_t& painter = event->painter();
        static constexpr color_t palette[] = {
            color_t{ 200, 40, 40 }, color_t{ 40, 160, 60 }, color_t{ 40, 80, 200 },
            color_t{ 210, 180, 40 }, color_t{ 40, 170, 170 }, color_t{ 170, 60, 170 },
        };
        constexpr int stripe_count = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
        const int height = rect().height();
        const int width = rect().width();
        // The first stripe starts at or above the viewport when the offset is not a multiple of the
        // stripe height, so the top band is drawn from its clipped position down: the pattern of a
        // scrolled content, exactly like a real list's rows.
        const int first = _offset / stripe_height;
        for (int index = first; index * stripe_height - _offset < height; ++index) {
            const int top = index * stripe_height - _offset;
            painter.set_color(palette[((index % stripe_count) + stripe_count) % stripe_count]);
            painter.fill_rect(rect_t{ 0, top, width, stripe_height });
        }
    }

private:
    int _offset = 0;
};

/// The content of a scroll_view_t: horizontal row bands painted in the content's own coordinates,
/// every band a different colour and separated by a one-pixel light line. A band that moved twice,
/// moved the wrong way or was never repainted therefore differs from a full repaint of the state.
class rows_content_t final: public widget_t {
public:
    /// @brief The height of one row band, in pixels.
    static constexpr int band_height = 18;

    /// @brief A content of @p size pixels; the area asks size_hint() for its scroll range.
    explicit rows_content_t(extent_t size, widget_t* parent = nullptr):
        widget_t(parent),
        _size(size) {
        set_opaque(true);
    }

    /// @brief The content size the scroll area lays out and derives its scroll range from.
    extent_t size_hint() const override {
        return _size;
    }

protected:
    void paint_event(paint_event_t* event) override {
        painter_t& painter = event->painter();
        painter.set_color(color_t{ 30, 30, 30 });
        painter.fill_rect(rect());
        for (int band = 0; band * band_height < _size.height(); ++band) {
            const int top = band * band_height;
            // The red channel encodes the band index (40 + 8 * band), so a failure dump says which
            // content band a wrong pixel came from.
            painter.set_color(color_t{ static_cast<std::uint8_t>(40 + 8 * band), 60, 80 });
            painter.fill_rect(rect_t{ 0, top + 1, _size.width(), band_height - 1 });
            painter.set_color(color_t{ 210, 220, 230 }); // the band's separator
            painter.fill_rect(rect_t{ 0, top, _size.width(), 1 });
        }
    }

private:
    extent_t _size;
};

/// A content whose row bands are separated by TRANSPARENT gaps: it paints no background, so the
/// scroll area's own viewport fill and frame show through exactly as they do in the demo. That is
/// what makes the area's frame observable inside the scrolled pixels, and therefore catchable.
class transparent_rows_t final: public widget_t {
public:
    /// @brief The row band height in pixels.
    static constexpr int band_height = 8;
    /// @brief The gap between two row bands in pixels.
    static constexpr int band_gap = 10;
    /// @brief The first band's top offset in pixels (below the area's frame row).
    static constexpr int band_origin = 20;
    /// @brief The left/right margin of the bands in pixels (the content edge stays transparent).
    static constexpr int band_margin = 8;

    /// @brief A content of @p size pixels; the area asks size_hint() for its scroll range.
    explicit transparent_rows_t(extent_t size, widget_t* parent = nullptr):
        widget_t(parent),
        _size(size) { }

    /// @brief The content size the scroll area lays out and derives its scroll range from.
    extent_t size_hint() const override {
        return _size;
    }

protected:
    void paint_event(paint_event_t* event) override {
        painter_t& painter = event->painter();
        // The bands start below the area's frame row: the frame must stay visible in the reference.
        for (int band = 0; band_origin + band * (band_height + band_gap) + band_height < _size.height();
             ++band) {
            const int top = band_origin + band * (band_height + band_gap);
            painter.set_color(color_t{ static_cast<std::uint8_t>(40 + 6 * band), 90, 120 });
            painter.fill_rect(rect_t{ band_margin, top, _size.width() - 2 * band_margin, band_height });
        }
    }

private:
    extent_t _size;
};

/// The host, with the tree's scroll request observed: the host still does its own work and the mirror
/// surface replays the same pixel move. Overriding the host's event function replaces the old trick of
/// REPLACING its scroll sink — the request channel is one object, so an observer forwards instead.
class mirroring_host_t final: public widget_window_t {
public:
    explicit mirroring_host_t(::hwshqtb::gui::window_t& window): widget_window_t(window) { }

    /// @brief The mirror that replays the pixel move; not owned, null until adopt().
    pixels_t* surface = nullptr;

    bool event(::hwshqtb::metaobject::event_t* request) override {
        if (request != nullptr && request->type() == ::hwshqtb::metaobject::event_type_enum::UI_SCROLL) {
            const auto* const scroll = static_cast<const scroll_event_t*>(request);
            // The host does the real work first (the window scrolls, the tree's record moves, the exposed
            // band is damaged and a frame is requested), then the mirror replays the same pixel move.
            const bool handled = widget_window_t::event(request);
            if (surface != nullptr) {
                surface->shift(scroll->area(), scroll->dx(), scroll->dy());
            }
            return handled;
        }
        return widget_window_t::event(request);
    }
};

/// A real window whose surface and window target are mirrored into readable pixels. The item-tree host
/// is composed into it: the frame policy is the window's, the tree painting is the host's.
class mirror_window_t final: public window_t {
public:
    mirror_window_t(std::string_view title, extent_t size):
        window_t(title, size),
        host(*this),
        surface(size, color_t::white_color()),
        _target(size) { }

    /// @brief The item-tree host this window composes.
    mirroring_host_t host;

    /// @brief Adopt @p root; the host's mirrored copy of the scroll request replays the pixel move.
    void adopt(widget_t* root) {
        _root = root;
        host.set_root_widget(root);
        host.surface = &surface;
    }

    /// @brief The pixels of the backing surface this window painted.
    const pixels_t& surface_pixels() const noexcept {
        return surface;
    }

    /// @brief The window target, as the driver presents it.
    const flip_target_t& target() const noexcept {
        return _target;
    }

    /// @brief Apply the shift window_t::scroll() performs on the surface to the mirror.
    /// @param area The scrolled rectangle, in window coordinates.
    /// @param dx The horizontal shift.
    /// @param dy The vertical shift.
    void shift_surface(rect_t area, int dx, int dy) {
        surface.shift(area, dx, dy);
    }

    /// @brief Present the frame the window just painted (a no-op when it painted none).
    /// @return Whether a frame was presented.
    bool present_frame() {
        if (painted_frame_count() == _presented_frames) {
            return false;
        }
        const std::size_t frames = painted_frame_count() - _presented_frames;
        _presented_frames = painted_frame_count();
        if (frames != 1) {
            std::printf("       %zu frames painted by one settle(); the model presents only the last\n",
                        frames);
        }
        _target.present(surface, presented_region());
        return true;
    }

    /// @brief How many passes the last frame painted (one per damaged rectangle).
    int last_pass_count() const noexcept {
        return _last_pass_count;
    }

    /// @brief The damage the last frame painted, recorded from inside its first pass.
    const region_t& last_frame_damage() const noexcept {
        return _last_frame_damage;
    }

protected:
    void paint_event(paint_event_t* event) override {
        painter_t& painter = event->painter();
        host.render(painter); // the real frame pass: the path under test
        const rect_t area = painter.clip_rect().intersected(rect_t{ point_t{}, size() });
        if (area.isEmpty() || _root == nullptr) {
            return;
        }
        if (painted_frame_count() != _recorded_frame) {
            _recorded_frame = painted_frame_count();
            _last_pass_count = 0;
            // The window has not consumed its pending damage yet: this IS the frame's damage.
            _last_frame_damage = pending_damage();
        }
        ++_last_pass_count;
        image_t pass{ size() };
        painter_t mirror{ pass };
        if (!mirror.begin()) {
            return;
        }
        mirror.set_clip(area);
        mirror.clear(area, color_t::white_color());
        _root->render(mirror);
        mirror.end();
        surface.copy_rect(pass, area);
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
    pixels_t surface;
    flip_target_t _target;
    std::size_t _presented_frames = 0;
    std::size_t _recorded_frame = 0;
    int _last_pass_count = 0;
    region_t _last_frame_damage;
};

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

/// A full repaint of @p root into a fresh buffer: the reference the presented frame must equal.
pixels_t full_repaint(widget_t& root, extent_t size) {
    pixels_t reference(size, color_t::white_color());
    image_t image{ size };
    painter_t painter{ image };
    if (!painter.begin()) {
        return reference;
    }
    painter.clear(color_t::white_color());
    root.render(painter);
    painter.end();
    reference.copy_rect(image, rect_t{ point_t{}, size });
    return reference;
}

/// @brief Run the real event loop until a phase dispatches nothing.
/// @param window The window to settle.
/// @param painted Receives the number of frames painted while settling.
/// @return The number of phases that ran.
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

/// @brief Push one SDL mouse motion for @p window.
bool push_motion(window_t& window, point_t position) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = window.window_id();
    event.motion.x = static_cast<float>(position.x());
    event.motion.y = static_cast<float>(position.y());
    event.motion.state = 0;
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

}  // namespace present_test

using namespace present_test;

namespace {
    /// The list scenario of section (2): its window, its tree and its model.
    constexpr extent_t list_window_extent{ 220, 200 };
    constexpr rect_t list_rect{ 10, 12, 200, 180 };
    constexpr int list_row_height = 20;
    constexpr int list_model_rows = 30;
    /// The column the hover assertions sample: inside the viewport and right of the row text.
    constexpr int list_sample_x = 100;

    /// @brief The window point at the middle of the screen band that content row @p row occupies.
    point_t row_point(const list_view_t& list, int row, int offset) {
        return list.map_to_global(point_t{ list.rect().width() - 30,
                                           row * list.row_height() - offset + list.row_height() / 2 });
    }

    /// @brief The window y of the top edge of content row @p row's band at @p offset.
    int row_band_y(const list_view_t& list, int row, int offset) {
        return list.map_to_global(point_t{}).y() + row * list.row_height() - offset;
    }

    /// @brief The content row under @p window_point, computed the way a user sees it.
    int row_under_pointer(const list_view_t& list, point_t window_point) {
        const point_t local = window_point - list.map_to_global(point_t{});
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

    /// @brief The content rows whose band shows a hover fill in @p pixels.
    std::vector<int> hovered_rows(const pixels_t& pixels, const list_view_t& list) {
        std::vector<int> rows;
        const int origin_y = list.map_to_global(point_t{}).y();
        const int offset = list.scroll_offset().y();
        const int count = list.model() != nullptr ? list.model()->row_count() : 0;
        for (int row = 0; row < count; ++row) {
            const int top = std::max(row_band_y(list, row, offset), origin_y);
            const int bottom = std::min(row_band_y(list, row, offset) + list.row_height(),
                                        origin_y + list.rect().height());
            if (bottom - top < 6) {
                continue; // too thin a sliver to sample
            }
            if (pixels.pixel(list_sample_x, top + (bottom - top) / 2) == row_hover_fill_color) {
                rows.push_back(row);
            }
        }
        return rows;
    }

    /// @brief Print a row list as "{2, 7}".
    void print_rows(const char* what, const std::vector<int>& rows) {
        std::printf("       %s:", what);
        for (const int row : rows) {
            std::printf(" %d", row);
        }
        std::printf("%s\n", rows.empty() ? " (none)" : "");
    }
}

int main(int argc, char** argv) {
    std::printf("== the presented frame: partial painting must still present the whole client area ==\n");
    (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    core_application application{ argc, argv };
    {
        gui_application_t gui;
        CHECK(gui_application_t::is_video_initialized());

        // ---- (1) the scroll fast path alone: a shift plus one real frame must equal a full repaint ----
        std::printf("(1) scroll(rect, 0, -16) + one real frame == a full repaint of the same state\n");
        {
            mirror_window_t scrolled{ "present_scroll", extent_t{ 120, 96 } };
            auto* const root = new fill_widget_t{ color_t::white_color() };
            auto* const view = new striped_view_t{ root };
            view->set_geometry(rect_t{ 0, 0, 120, 96 });
            scrolled.adopt(root);

            int painted = 0;
            settle(scrolled, painted);
            CHECK(painted == 1);
            CHECK(scrolled.present_frame());
            CHECK(scrolled.painted_frame_count() == 1);
            CHECK((view->offset() == 0));

            // The first frame has to be a full one: a fresh surface has no contents to preserve.
            const region_t& first = scrolled.presented_region();
            print_region("[present] first frame: presented", first);
            CHECK(covers(first, rect_t{ 0, 0, 120, 96 }));

            const pixels_t frame_a = scrolled.target().shown();
            // Sanity: the first frame really shows the pattern's first stripe at the top.
            CHECK((frame_a.pixel(1, 1) == color_t{ 200, 40, 40 }));

            // The scroll: the surface's pixels move up by one stripe and only the bottom strip is
            // damaged. The view's offset moves with them, so a full repaint of the state is the
            // reference the frame has to match.
            const rect_t area{ 0, 0, 120, 96 };
            view->set_offset(striped_view_t::stripe_height);
            scrolled.scroll(area, 0, -striped_view_t::stripe_height);
            scrolled.shift_surface(area, 0, -striped_view_t::stripe_height);
            print_region("[present] scroll: window damage", scrolled.pending_damage());

            settle(scrolled, painted);
            CHECK(painted == 1);
            CHECK(scrolled.present_frame());
            print_region("[present] scroll: presented", scrolled.presented_region());
            std::printf("[present] scroll: %d pass(es) painted the strips\n", scrolled.last_pass_count());
            const pixels_t& frame_b = scrolled.target().shown();
            // The frame presents the whole client area — the target's other pixels belong to an older
            // frame either way — while it still PAINTS the one exposed strip. Both halves are pinned:
            // without the first the moved pixels of the shift never reach the screen, and with a whole
            // viewport ranked dirty instead the scroll would be "fixed" by repainting everything.
            CHECK(covers(scrolled.presented_region(), rect_t{ 0, 0, 120, 96 }));
            CHECK((scrolled.last_frame_damage().area()
                   == static_cast<std::size_t>(120 * striped_view_t::stripe_height)));
            CHECK(scrolled.last_pass_count() == 1);

            // The reference: a second window showing the same state, painted whole, no scroll.
            mirror_window_t reference{ "present_scroll_reference", extent_t{ 120, 96 } };
            auto* const reference_root = new fill_widget_t{ color_t::white_color() };
            auto* const reference_view = new striped_view_t{ reference_root };
            reference_view->set_geometry(rect_t{ 0, 0, 120, 96 });
            reference_view->set_offset(striped_view_t::stripe_height);
            reference.adopt(reference_root);
            settle(reference, painted);
            CHECK(painted == 1);
            CHECK(reference.present_frame());
            const pixels_t& frame_c = reference.target().shown();
            // Sanity: the state after one stripe of scroll starts with the SECOND stripe.
            CHECK((frame_c.pixel(1, 1) == color_t{ 40, 160, 60 }));

            // The SURFACE the scroll fast path left is correct: the shift moved every stripe and the
            // painted strip put the last one back. The presented frame is what differs.
            const int surface_differences = scrolled.surface_pixels().differences(frame_c);
            std::printf("[present] scroll: surface differs from the full repaint in %d pixel(s)\n",
                        surface_differences);
            CHECK(surface_differences == 0);

            const int differences = frame_b.differences(frame_c);
            std::printf("[present] scroll: presented frame differs from the full repaint in %d "
                        "pixel(s), first frame %zu present(s)\n",
                        differences, scrolled.target().presents());
            if (differences != 0) {
                frame_b.dump_differences(frame_c, "[present] scroll");
            }
            CHECK(differences == 0);
        }

        // ---- (2) a hover sweep: the discrete stale rows of defect A ----
        std::printf("(2) a hover sweep: only the row under the pointer may show the hover fill\n");
        {
            mirror_window_t window{ "present_hover", list_window_extent };
            auto* const root = new fill_widget_t{ color_t::white_color() };
            auto* const list = new list_view_t{ root };
            list->set_geometry(list_rect);
            list->set_row_height(list_row_height);
            probe_model_t model{ list_model_rows };
            list->set_model(&model);
            window.adopt(root);

            int painted = 0;
            settle(window, painted);
            CHECK(painted == 1);
            CHECK(window.present_frame());
            CHECK(window.painted_frame_count() == 1);
            {
                const pixels_t reference = full_repaint(*root, list_window_extent);
                const int differences = window.target().shown().differences(reference);
                CHECK(differences == 0);
                std::printf("[present] first list frame: %d pixel(s) differ from a full repaint\n",
                            differences);
                // Sanity for the sampling: the band of row 0 carries the plain row fill, so the
                // hover colour the checks look for is not a colour the list paints by default.
                CHECK((window.surface_pixels().pixel(list_sample_x, row_band_y(*list, 0, 0) + 8)
                       == row_fill_color));
            }

            // (2a) three motions in ONE batch: one frame paints all three bands. The damage region is
            // the union, the frame presents it, and the presented frame is correct — this injection
            // does NOT reproduce the defect.
            {
                const std::size_t before = window.target().presents();
                for (const int row : { 2, 4, 7 }) {
                    CHECK(push_motion(window, row_point(*list, row, 0)));
                }

                settle(window, painted);
                CHECK(painted == 1);
                CHECK(window.present_frame());
                CHECK(window.target().presents() == before + 1);
                print_region("[present] one batch: damage", window.last_frame_damage());
                std::printf("[present] one batch: %d pass(es)\n", window.last_pass_count());
                const point_t pointer = row_point(*list, 7, 0);
                const std::vector<int> on_screen = hovered_rows(window.target().shown(), *list);
                const std::vector<int> on_surface = hovered_rows(window.surface_pixels(), *list);
                print_rows("one batch: hovered rows on the surface", on_surface);
                print_rows("one batch: hovered rows on screen", on_screen);
                CHECK((row_under_pointer(*list, pointer) == 7));
                CHECK((on_surface == std::vector<int>{ 7 }));
                CHECK((on_screen == std::vector<int>{ 7 }));
            }

            // (2b) the same rows, one motion per frame — what a fast sweep does when the loop splits
            // the batch into phases: a row's hover and its removal land in DIFFERENT buffers, so the
            // hover comes back two presents later.
            {
                // A jumpy path on purpose: crossing rows one at a time would only ever leave the row
                // from two presents back, while a sweep that skips rows leaves the skipped ones.
                for (const int row : { 2, 4, 7, 8 }) {
                    CHECK(push_motion(window, row_point(*list, row, 0)));

                    settle(window, painted);
                    if (painted != 1) {
                        std::printf("       row %d: %d frame(s) painted\n", row, painted);
                    }
                    CHECK(painted == 1);
                    CHECK(window.present_frame());
                    const point_t pointer = row_point(*list, row, 0);
                    const int expected = row_under_pointer(*list, pointer);
                    const std::vector<int> on_surface = hovered_rows(window.surface_pixels(), *list);
                    const std::vector<int> on_screen = hovered_rows(window.target().shown(), *list);
                    std::printf("[present] sweep to row %d: surface", row);
                    for (const int hovered : on_surface) {
                        std::printf(" %d", hovered);
                    }
                    std::printf(", screen");
                    for (const int hovered : on_screen) {
                        std::printf(" %d", hovered);
                    }
                    std::printf("\n");
                    print_region("[present] sweep: damage", window.last_frame_damage());
                    print_region("[present] sweep: presented", window.presented_region());
                    // The hover change paints the bands it left and entered, nothing else: a fix that
                    // repainted the list on every motion would show up here as a full-viewport damage.
                    // Two whole-width bands are the upper bound (a band pair that touches merges into
                    // one rectangle, so the real damage is usually smaller).
                    CHECK(window.last_frame_damage().area()
                          <= static_cast<std::size_t>(2 * list->rect().width() * list_row_height));
                    CHECK(window.last_pass_count() <= 2);
                    // The surface is right — the tree painted every band the hover touched.
                    CHECK((on_surface == std::vector<int>{ expected }));
                    // The presented frame must show the hover on the row under the pointer only.
                    if (on_screen != std::vector<int>{ expected }) {
                        print_rows("sweep: stale hover rows on screen", on_screen);
                    }
                    CHECK((on_screen == std::vector<int>{ expected }));
                }
            }
        }
        // ---- (3) a wheel burst on a scroll_view_t in ONE frame ----
        // The wheel is the real input path: SDL event -> window_t's filter -> input_state_t ->
        // widget_window_t's router filter -> input_router_t -> scroll_view_t::interact -> the
        // container's fast path (widget_t::notify_scroll -> the window's scroll sink). Several ticks
        // arrive before the frame paints, which is what a fast scroll does. The damage of one step is
        // NOT enough then: the next shift drags the band the previous one exposed further into the
        // viewport, so the frame has to paint the band the ACCUMULATED shift exposed. Both directions
        // are pinned, because a burst that scrolls up leaves its stale band at the top.
        std::printf("(3) a wheel burst on a scroll_view_t must equal a full repaint of the same state\n");
        {
            constexpr extent_t burst_window_extent{ 240, 220 };
            constexpr rect_t burst_area_rect{ 10, 12, 200, 180 };
            constexpr extent_t burst_content_extent{ 240, 420 };
            constexpr int wheel_step = 16;
            constexpr int wheel_ticks = 4;
            constexpr int viewport_width = 186;   // the area minus its vertical bar
            constexpr int viewport_height = 166;  // the area minus its horizontal bar
            mirror_window_t window{ "present_scroll_area", burst_window_extent };
            auto* const root = new fill_widget_t{ color_t::white_color() };
            auto* const area = new scroll_view_t{ root };
            area->set_geometry(burst_area_rect);
            area->set_scroll_bar_policy(scroll_bar_policy_enum::AS_NEEDED);
            auto* const content = new rows_content_t{ burst_content_extent };
            area->set_content(content);
            window.adopt(root);

            // The pointer has to be over the area: the wheel is routed by the hit test, not by the
            // focus. It moves BEFORE the first frame, so the first (whole-window) frame consumes the
            // hover damage of the item it enters.
            CHECK(push_motion(window, point_t{ burst_area_rect.x() + 30, burst_area_rect.y() + 40 }));

            int painted = 0;
            settle(window, painted);
            CHECK(painted == 1);
            CHECK(window.present_frame());
            CHECK((area->scroll_offset().y() == 0));
            CHECK((content->geometry().y() == 0));
            CHECK(window.pending_damage().is_empty()); // the first frame consumed everything

            // A helper: one burst, then the frame it painted must equal a full repaint of the state.
            // @param direction -1 scrolls down (the content moves up), +1 scrolls up.
            const auto burst = [&](int direction, int ticks, const char* what) {
                const int before = area->scroll_offset().y();
                for (int tick = 0; tick < ticks; ++tick) {
                    CHECK(push_wheel(window, direction)); // one burst in one batch
                }
                settle(window, painted);
                CHECK(painted == 1);
                CHECK(window.present_frame());
                const int offset = area->scroll_offset().y();
                const int shift = offset - before;
                std::printf("[present] %s: offset %d -> %d, source y %d\n", what, before, offset,
                            area->source_rect().y());
                print_region("[present] burst damage", window.last_frame_damage());
                std::printf("[present] burst painted %d pass(es)\n", window.last_pass_count());
                CHECK((shift == -direction * ticks * wheel_step));
                CHECK((content->geometry().y() == 0)); // the content item never moves
                CHECK((area->source_rect().y() == offset)); // scrolling moved the source region
                // The exposed band of the ACCUMULATED shift, which the frame has to paint whole. A
                // step's own 16 px strip does not cover it: that gap is the defect.
                const rect_t band = shift > 0
                    ? rect_t{ burst_area_rect.x(), burst_area_rect.y() + viewport_height - shift,
                              viewport_width, shift }
                    : rect_t{ burst_area_rect.x(), burst_area_rect.y(), viewport_width, -shift };
                CHECK(covers(window.last_frame_damage(), band));
                // ... while it is still a band, not the viewport: a fix that repainted the whole area
                // on every scroll would show up here.
                CHECK(window.last_frame_damage().area()
                      < static_cast<std::size_t>(burst_area_rect.width() * burst_area_rect.height()));
                const pixels_t reference = full_repaint(*root, burst_window_extent);
                const int surface_differences = window.surface_pixels().differences(reference);
                const int differences = window.target().shown().differences(reference);
                std::printf("[present] %s: surface %d pixel(s), presented frame %d pixel(s) away "
                            "from a full repaint\n", what, surface_differences, differences);
                if (differences != 0) {
                    window.target().shown().dump_differences(reference, "[present] burst");
                }
                CHECK(surface_differences == 0);
                CHECK(differences == 0);
            };

            // (3a) four ticks down in one frame: the stale band would be 48 px of duplicated content.
            burst(-1, wheel_ticks, "burst down");
            // (3b) and back up: the stale band is now at the TOP, where the scroll area paints its
            // border, so the duplicated band is the light stripe the fast scroll showed.
            burst(1, 3, "burst up");
        }
        // ---- (4) a transparent content: the scroll area's own frame must not scroll ----
        // The area's viewport starts at the area's top-left corner, so its top row and left column
        // carry the area's 1 px border. A fast path that shifts the WHOLE viewport drags that fixed
        // border into the scrolled pixels; only the strip the shift uncovers is repainted, so every
        // scrolled-up frame leaves one more 1 px border-coloured line (the light stripes a user saw).
        // The rows_content_t above hides the frame (it paints an opaque background over it), which is
        // why defect C's test alone never saw this: the content here paints bands only, exactly like
        // the demo's row labels, and the frame stays visible.
        std::printf("(4) a transparent content: the area's own frame must stay put while scrolling\n");
        {
            constexpr extent_t frame_window_extent{ 240, 220 };
            constexpr rect_t frame_area_rect{ 10, 12, 200, 180 };
            constexpr extent_t frame_content_extent{ 240, 420 };
            constexpr int frame_viewport_width = 186;   // the area minus its vertical bar
            constexpr int frame_viewport_height = 166;  // the area minus its horizontal bar
            constexpr int wheel_step = 16;
            mirror_window_t window{ "present_scroll_frame", frame_window_extent };
            auto* const root = new fill_widget_t{ color_t::white_color() };
            auto* const area = new scroll_view_t{ root };
            area->set_geometry(frame_area_rect);
            area->set_scroll_bar_policy(scroll_bar_policy_enum::AS_NEEDED);
            auto* const content = new transparent_rows_t{ frame_content_extent };
            area->set_content(content);
            window.adopt(root);

            // No input is injected: the container's own scroll API drives the fast path, so no hover
            // change can repaint the content and mask the frame line.
            int painted = 0;
            settle(window, painted);
            CHECK(painted == 1);
            CHECK(window.present_frame());

            const int area_top = frame_area_rect.y();
            const int area_left = frame_area_rect.x();
            // Sanity: the frame is visible in the reference state, so the comparison below is about
            // the frame's position, not about a colour the viewport never paints.
            const pixels_t first = window.target().shown();
            CHECK((first.pixel(area_left + 5, area_top) == color_t{ 70, 84, 98 }));
            const int first_differences =
                first.differences(full_repaint(*root, frame_window_extent));
            CHECK(first_differences == 0);

            // Scroll down so there is room to scroll back up. The viewport's content moves, but the
            // area's frame must stay where it is.
            area->set_scroll_offset(point_t{ 0, 4 * wheel_step });
            settle(window, painted);
            CHECK(painted == 1);
            CHECK(window.present_frame());
            CHECK((area->scroll_offset().y() == 4 * wheel_step));
            CHECK((window.surface_pixels().pixel(area_left + 5, area_top)
                   == color_t{ 70, 84, 98 }));

            // Two one-tick scroll-ups, each its own frame — what a wheel event per frame does. The
            // first frame repaints the top strip and brings the border back; the second one shifts
            // that freshly painted border row one step down and repaints only the 16 px strip above
            // it. The dragged row (area_top + wheel_step) is the light stripe a user saw.
            area->set_scroll_offset(point_t{ 0, 3 * wheel_step });
            settle(window, painted);
            CHECK(painted == 1);
            CHECK(window.present_frame());
            CHECK((area->scroll_offset().y() == 3 * wheel_step));
            area->set_scroll_offset(point_t{ 0, 2 * wheel_step });
            settle(window, painted);
            CHECK(painted == 1);
            CHECK(window.present_frame());
            CHECK((area->scroll_offset().y() == 2 * wheel_step));
            print_region("[present] frame: scroll damage", window.last_frame_damage());
            std::printf("[present] frame: %d pass(es) painted\n", window.last_pass_count());
            // Still a strip, not the viewport: the frame lines cost at most 2 * (w + h) extra pixels.
            CHECK(window.last_frame_damage().area()
                  < static_cast<std::size_t>(frame_area_rect.width() * frame_area_rect.height()));

            const pixels_t reference = full_repaint(*root, frame_window_extent);
            // The reference really carries content where the dragged row would be, so the equality
            // below is not two identical wrong frames.
            CHECK((reference.pixel(area_left + 5, area_top + wheel_step)
                   != color_t{ 70, 84, 98 }));
            CHECK((reference.pixel(area_left + frame_viewport_width - 1, area_top + wheel_step)
                   != color_t{ 70, 84, 98 }));
            CHECK(frame_viewport_height > 2 * wheel_step);
            const int surface_differences = window.surface_pixels().differences(reference);
            const int differences = window.target().shown().differences(reference);
            std::printf("[present] frame: surface %d pixel(s), presented frame %d pixel(s) away from "
                        "a full repaint\n", surface_differences, differences);
            if (differences != 0) {
                window.target().shown().dump_differences(reference, "[present] frame");
            }
            CHECK(surface_differences == 0);
            CHECK(differences == 0);
        }

    }

    if (g_failures == 0) {
        std::printf("present: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("present: %d of %d checks failed\n", g_failures, g_checks);
    return 1;
}
