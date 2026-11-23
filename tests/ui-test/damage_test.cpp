// Damaged-region bookkeeping for the item tree (the repaint-manager shape of Qt): item_t::update()
// records the mapped rectangle on the item AND on every item ancestor, damage() reads the record
// back, clear_damage() drops it, and the paint path consumes the damage of the items it painted.
//
// Sections (1)-(5) are pure item-tree work over image_t (offscreen, no window). Section (6) wires
// the tree to a widget_window_t under SDL's dummy video driver, because the window's pending_damage()
// mirror is only observable through a real window: it proves that an update reaches the window and
// that the root's damage is exactly what the window reports.
//
// Sections (7)-(10) are the partial-repaint stage: one clipped pass per damaged rectangle (never the
// bounding rectangle), the occlusion culling of opaque siblings, a frame that paints nothing when
// nothing is damaged, and the scroll fast path (pixel shift + strip damage). Section (11) checks the
// surface-level scroll copy byte for byte, and (12) pins the idle immediate host: a pass that changes
// nothing damages nothing, so it schedules no frame. Section (13) pins the pointer's coordinate
// space: the drawing target (window_t::size()), the damage rectangles and the backing surface are one
// pixel extent; a pushed SDL motion keeps its raw position at density 1, and an injected 200x120 ->
// 300x180 density (the 1.5 case no driver here reports) moves the real filter's point to 150,90 and
// the real hit test onto the control there.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "hwshqtb/gui/backing_store_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/gui_application_t.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/gui/region_t.h"
#include "hwshqtb/metaobject/core_application.h"
#include "hwshqtb/metaobject/event_dispatcher.h"
#include "hwshqtb/ui/check_box_t.h"
#include "hwshqtb/ui/im_host_t.h"
#include "hwshqtb/ui/item_t.h"
#include "hwshqtb/ui/push_button_t.h"
#include "hwshqtb/ui/scroll_view_t.h"
#include "hwshqtb/ui/scroll_bar_t.h"
#include "hwshqtb/ui/widget_t.h"
#include "hwshqtb/ui/widget_window_t.h"

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;
using namespace hwshqtb::ui;

namespace damage_test {

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

/// The interface contract of the WIDGET layer: damage() is a const view and clear_damage() cannot throw.
static_assert(std::is_same_v<decltype(std::declval<const widget_t&>().damage()), const region_t&>);
static_assert(noexcept(std::declval<const widget_t&>().damage()));
static_assert(noexcept(std::declval<widget_t&>().clear_damage()));

/// Whether two regions hold the same rectangles in the same order.
bool same_rects(const region_t& a, const region_t& b) {
    if (a.rectangle_count() != b.rectangle_count()) {
        return false;
    }
    const std::span<const rect_t> left = a.rects();
    const std::span<const rect_t> right = b.rects();
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (!(left[i] == right[i])) {
            return false;
        }
    }
    return true;
}

/// Whether @p region covers every pixel of @p rect (removing the region from the rectangle leaves
/// nothing).
bool covers(const region_t& region, rect_t rect) {
    return region_t{ rect }.subtracted(region).is_empty();
}

/// A widget that fills its own rectangle with one colour, so a paint pass is visible in the pixels.
class solid_widget_t final: public widget_t {
public:
    explicit solid_widget_t(color_t color, widget_t* parent = nullptr):
        widget_t(parent),
        _color(color) { }

protected:
    void paint_event(paint_event_t* event) override {
        painter_t& painter = event->painter();
        painter.set_color(_color);
        painter.fill_rect(rect());
    }

private:
    color_t _color;
};

/// A solid widget that declares its opacity and counts its paint passes: the probe of the partial
/// repaint (how often was this item painted, and with which colour did it end up).
class counted_widget_t final: public widget_t {
public:
    /// @param color The colour its whole rectangle is filled with.
    /// @param opaque Whether it declares is_opaque() (a promise that the fill covers rect()).
    /// @param parent The visual parent.
    counted_widget_t(color_t color, bool opaque, widget_t* parent = nullptr):
        widget_t(parent),
        _color(color) {
        set_opaque(opaque);
    }

    /// @brief How many paint passes reached this widget.
    int paints() const noexcept {
        return _paints;
    }

    /// @brief Forget the paint count (so a section can measure one frame).
    void reset_paints() noexcept {
        _paints = 0;
    }

protected:
    void paint_event(paint_event_t* event) override {
        painter_t& painter = event->painter();
        ++_paints;
        painter.set_color(_color);
        painter.fill_rect(rect()); // the whole rectangle: the opaque promise, made true
    }

private:
    color_t _color;
    int _paints = 0;
};

/// Whether two images hold the same pixel in every position.
bool same_pixels(const image_t& left, const image_t& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (int y = 0; y < left.size().height(); ++y) {
        for (int x = 0; x < left.size().width(); ++x) {
            if (!(left.pixel(x, y) == right.pixel(x, y))) {
                return false;
            }
        }
    }
    return true;
}

/// Paint one FULL frame of @p root into @p device (clear + paint), exactly as window_t does.
void paint_full_frame(image_t& device, widget_t& root) {
    painter_t painter{ device };
    painter.begin();
    painter.clear(color_t::white_color());
    root.render(painter);
    painter.end();
}

/// The partial repaint of window_t::paint_frame: one clipped, cleared pass per damaged rectangle.
void paint_damaged(image_t& device, widget_t& root, const region_t& damage) {
    painter_t painter{ device };
    painter.begin();
    for (const rect_t& area : damage.rects()) {
        painter.save(); // set_clip() INTERSECTS, so each pass starts from no clip
        painter.clear_clip();
        painter.set_clip(area);
        painter.clear(area, color_t::white_color());
        root.render(painter);
        painter.restore();
    }
    painter.end();
}

/// A window that records the clip rectangle of every pass it paints: the frame's per-rectangle clip,
/// which is the damage AFTER window_t clipped it to the client area's pixel grid.
///
/// It is a plain gui::window_t plus the composed item-tree host: the frame policy under test is the
/// window's, the tree painting is the host's, and this class is the wire between the two.
class clip_recording_window_t final: public window_t {
public:
    clip_recording_window_t(std::string_view title, extent_t size):
        window_t(title, size),
        host(*this) { }

    /// @brief The item-tree host this window composes.
    widget_window_t host;

    /// @brief The clip rectangle of each pass, in the order the frame painted them.
    std::vector<rect_t> passes;

    /// @brief Forget the recorded passes (so a section can measure one frame).
    void clear_passes() {
        passes.clear();
    }

protected:
    void paint_event(paint_event_t* event) override {
        painter_t& painter = event->painter();
        passes.push_back(painter.clip_rect());
        host.render(painter);
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
};

/// A window whose two pointer-conversion extents are INJECTED: it reports 200x120 in WINDOW
/// coordinates while its pixel grid is 300x180 — the 1.5 density no driver on this machine reports.
/// Everything else (the SDL event queue, the native filter, input_state, the ui router's hit test)
/// is the real path; only the pair window_to_pixel_point() divides is replaced.
class simulated_density_window_t final: public window_t {
public:
    simulated_density_window_t(std::string_view title, extent_t size):
        window_t(title, size),
        host(*this) { }

    /// @brief The item-tree host this window composes.
    widget_window_t host;

    /// @brief The extents the pointer conversion must use.
    void set_pointer_extents(extent_t window_size, extent_t pixel_extent) {
        _window_size = window_size;
        _pixel_size = pixel_extent;
    }

protected:
    extent_t window_coordinate_size() const noexcept override {
        return _window_size;
    }

    extent_t pixel_size() const noexcept override {
        return _pixel_size;
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
    extent_t _window_size{ 200, 120 };
    extent_t _pixel_size{ 300, 180 };
};

/// @brief Pump SDL events and dispatch the posted queue until a phase finds nothing to do.
/// @return The number of phases that ran.
int settle_events(int maximum_phases = 32) {
    event_phase_t phase;
    phase.merge = false; // deliver every pushed event, not one accumulated state
    phase.paint_last = true;
    int phases = 0;
    for (; phases < maximum_phases; ++phases) {
        if (!event_dispatcher::instance().process_phase(process_events_type_flag::ALL_EVENTS, phase)) {
            break;
        }
    }
    return phases;
}

/// @brief Push one mouse motion in WINDOW coordinates through the real SDL queue and settle the loop.
/// @param window The window the event is stamped with.
/// @param x The event's x, in WINDOW coordinates.
/// @param y The event's y, in WINDOW coordinates.
/// @param position Receives the position window_t's input state holds afterwards (the paint space).
/// @return true when the event was pushed and the loop settled.
bool motion_position(window_t& window, float x, float y, point_t& position) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = window.window_id();
    event.motion.x = x;
    event.motion.y = y;
    event.motion.state = 0;
    if (!SDL_PushEvent(&event)) {
        std::printf("       SDL_PushEvent failed: %s\n", SDL_GetError());
        return false;
    }
    (void)settle_events();
    position = window.input_state().mouse_position();
    return true;
}

}  // namespace damage_test

using namespace damage_test;

int main(int argc, char** argv) {
    std::printf("== item damage: per-item records, ancestor propagation, paint consumption ==\n");

    // ---- (0) an empty damaged rectangle is ignored ----
    {
        widget_t root;
        root.set_geometry(rect_t{ 5, 6, 40, 30 });
        root.clear_damage();

        root.update(rect_t{});
        CHECK(root.damage().is_empty());
        root.update(rect_t{ 3, 3, 0, 5 }); // zero width
        CHECK(root.damage().is_empty());
        root.update(rect_t{ 3, 3, 5, -2 }); // inverted height
        CHECK(root.damage().is_empty());
    }

    // ---- (1) update(local) records the rectangle in WINDOW coordinates on the item itself ----
    {
        widget_t root;
        root.set_geometry(rect_t{ 10, 20, 100, 100 });
        widget_t child{ &root };
        child.set_geometry(rect_t{ 5, 7, 30, 40 });
        widget_t grandchild{ &child };
        grandchild.set_geometry(rect_t{ 2, 3, 10, 10 });
        root.clear_damage();
        child.clear_damage();
        grandchild.clear_damage();

        CHECK(child.damage().is_empty());
        child.update(rect_t{ 1, 2, 3, 4 });
        // (10 + 5 + 1, 20 + 7 + 2) = (16, 29), size unchanged.
        CHECK(child.damage().rectangle_count() == 1);
        CHECK((child.damage().bounds() == rect_t{ 16, 29, 3, 4 }));
        CHECK(child.damage().area() == 12);
        CHECK(covers(child.damage(), rect_t{ 16, 29, 3, 4 }));

        // map_to_global and the record agree: the damage origin is the mapped local origin.
        const point_t mapped = child.map_to_global(point_t{ 1, 2 });
        CHECK((mapped == point_t{ 16, 29 }));
        CHECK((child.damage().bounds().top_left() == mapped));

        // The whole-item form maps rect() (the item's local {{0,0}, size}).
        child.clear_damage();
        child.update();
        CHECK(child.damage().rectangle_count() == 1);
        CHECK((child.damage().bounds() == rect_t{ 15, 27, 30, 40 }));
        CHECK(child.damage().area() == static_cast<std::size_t>(30 * 40));

        // The grandchild's update() maps three levels, not one.
        grandchild.update(rect_t{ 0, 0, 2, 2 });
        CHECK((grandchild.damage().bounds() == rect_t{ 17, 30, 2, 2 }));
    }

    // ---- (2) every ITEM ancestor records it; a sibling records nothing ----
    {
        widget_t root;
        root.set_geometry(rect_t{ 0, 0, 200, 200 });
        widget_t left{ &root };
        left.set_geometry(rect_t{ 10, 10, 40, 40 });
        widget_t right{ &root };
        right.set_geometry(rect_t{ 60, 60, 40, 40 });
        root.clear_damage();
        left.clear_damage();
        right.clear_damage();

        left.update(rect_t{ 1, 2, 3, 4 }); // window (11, 12, 3, 4)
        CHECK(covers(left.damage(), rect_t{ 11, 12, 3, 4 }));
        CHECK(covers(root.damage(), rect_t{ 11, 12, 3, 4 }));
        CHECK(root.damage().area() == 12); // the union of the child's rectangle only
        CHECK(right.damage().is_empty()); // propagation goes UP, never sideways

        // The descendant and ancestor records are independent objects, not one shared region: a
        // clear of the child leaves the ancestor's copy in place.
        left.clear_damage();
        CHECK(left.damage().is_empty());
        CHECK(covers(root.damage(), rect_t{ 11, 12, 3, 4 }));

        // ... and a further child update grows both again.
        left.update(rect_t{ 5, 6, 1, 1 }); // window (15, 16, 1, 1)
        CHECK(left.damage().rectangle_count() == 1);
        CHECK(root.damage().rectangle_count() == 2);
        CHECK(covers(root.damage(), rect_t{ 11, 12, 3, 4 }));
        CHECK(covers(root.damage(), rect_t{ 15, 16, 1, 1 }));
    }

    // ---- (3) two updates are a UNION, not an overwrite ----
    {
        widget_t root;
        root.set_geometry(rect_t{ 10, 20, 100, 100 });
        widget_t child{ &root };
        child.set_geometry(rect_t{ 5, 7, 30, 40 });
        root.clear_damage();
        child.clear_damage();

        child.update(rect_t{ 0, 0, 2, 2 }); // window (15, 27, 2, 2)
        child.update(rect_t{ 10, 10, 2, 2 }); // window (25, 37, 2, 2), disjoint
        CHECK(child.damage().rectangle_count() == 2);
        CHECK(child.damage().area() == 8);
        CHECK(covers(child.damage(), rect_t{ 15, 27, 2, 2 }));
        CHECK(covers(child.damage(), rect_t{ 25, 37, 2, 2 }));
        CHECK((child.damage().bounds() == rect_t{ 15, 27, 12, 12 }));
        CHECK(!covers(child.damage(), rect_t{ 20, 32, 1, 1 })); // the gap between them stays uncovered

        // An OVERLAPPING update is united, not appended: the first rectangle is still covered, and
        // the covered area is the union's (4 + 4 - 1 overlapping pixel + the far 4).
        child.update(rect_t{ 1, 1, 2, 2 }); // window (16, 28, 2, 2): overlaps (15,27,2,2) in one pixel
        CHECK(covers(child.damage(), rect_t{ 15, 27, 2, 2 }));
        CHECK(covers(child.damage(), rect_t{ 16, 28, 2, 2 }));
        CHECK(covers(child.damage(), rect_t{ 25, 37, 2, 2 }));
        CHECK(child.damage().area() == 11); // 4 + 4 - 1 + 4
        CHECK(!covers(child.damage(), rect_t{ 20, 32, 1, 1 }));

        // The ancestor saw the same union.
        CHECK(child.damage().area() <= root.damage().area());
        CHECK(covers(root.damage(), rect_t{ 15, 27, 2, 2 }));
        CHECK(covers(root.damage(), rect_t{ 25, 37, 2, 2 }));
        CHECK(covers(root.damage(), rect_t{ 16, 28, 2, 2 }));
    }

    // ---- (4) clear_damage() empties exactly that item's record ----
    {
        widget_t root;
        root.set_geometry(rect_t{ 0, 0, 50, 50 });
        widget_t child{ &root };
        child.set_geometry(rect_t{ 5, 5, 20, 20 });
        root.clear_damage();
        child.clear_damage();

        child.update(rect_t{ 1, 1, 4, 4 });
        CHECK(!child.damage().is_empty());
        CHECK(!root.damage().is_empty());
        child.clear_damage();
        CHECK(child.damage().is_empty());
        CHECK(child.damage().rectangle_count() == 0);
        CHECK(child.damage().area() == 0);
        CHECK(!root.damage().is_empty()); // the ancestor keeps its own copy

        child.update(rect_t{ 1, 1, 4, 4 });
        CHECK(!child.damage().is_empty()); // and can be damaged again after a clear
        child.clear_damage();
        child.clear_damage(); // idempotent
        CHECK(child.damage().is_empty());
    }

    // ---- (5) a painted item drops its damage; an item the frame did not reach keeps it ----
    {
        solid_widget_t root{ color_t::white_color() };
        root.set_geometry(rect_t{ 0, 0, 120, 90 });
        solid_widget_t painted{ color_t::red_color(), &root };
        painted.set_geometry(rect_t{ 10, 10, 30, 20 });
        solid_widget_t hidden{ color_t::blue_color(), &root };
        hidden.set_geometry(rect_t{ 60, 40, 20, 15 });
        hidden.set_visible(false);
        root.clear_damage();
        painted.clear_damage();
        hidden.clear_damage();

        root.update(rect_t{ 0, 0, 120, 90 });
        painted.update();
        hidden.update();
        CHECK(covers(root.damage(), rect_t{ 0, 0, 120, 90 }));
        CHECK(!painted.damage().is_empty());
        CHECK(!hidden.damage().is_empty());

        image_t device{ extent_t{ 120, 90 } };
        painter_t painter{ device };
        CHECK(painter.begin());
        root.render(painter);
        CHECK(painter.end());

        // The painted widgets consumed their records ...
        CHECK(root.damage().is_empty());
        CHECK(painted.damage().is_empty());
        // ... but the hidden subtree was never reached, so its damage survives the frame.
        CHECK(!hidden.damage().is_empty());
        CHECK(covers(hidden.damage(), rect_t{ 60, 40, 20, 15 }));

        // Sanity: the frame really painted the visible child where the damage said it was.
        CHECK((device.pixel(11, 11) == color_t::red_color()));
        CHECK((device.pixel(61, 41) == color_t::white_color())); // hidden: only the root's fill
    }

    // ---- (6) the root's damage is the window's pending damage, and a frame consumes both ----
    {
        // The window is real (window_t owns an SDL window and a renderer), so the test forces the
        // dummy video driver: the section then runs offscreen and deterministically.
        (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        core_application application{ argc, argv };
        {
            gui_application_t gui;
            CHECK(gui_application_t::is_video_initialized());

            widget_window_t window{ "damage_test", extent_t{ 120, 80 } };
            auto* const root = new solid_widget_t{ color_t::white_color() };
            auto* const child = new solid_widget_t{ color_t::red_color(), root };
            child->set_geometry(rect_t{ 10, 12, 20, 16 });
            window.set_root_widget(root); // takes ownership; the root fills the client area

            CHECK(window.root_widget() == root);
            CHECK((root->geometry() == rect_t{ 0, 0, 120, 80 }));
            CHECK(!window.pending_damage().is_empty()); // set_geometry -> update -> sink

            // One painted frame clears the tree's damage and the window's mirror together.
            window.repaint();
            CHECK(root->damage().is_empty());
            CHECK(child->damage().is_empty());
            CHECK(window.pending_damage().is_empty());

            // A nested update records on the whole ancestor chain and reaches the window.
            child->update(rect_t{ 1, 2, 3, 4 }); // window (11, 14, 3, 4)
            CHECK((child->damage().bounds() == rect_t{ 11, 14, 3, 4 }));
            CHECK(covers(root->damage(), rect_t{ 11, 14, 3, 4 }));
            CHECK(covers(window.pending_damage(), rect_t{ 11, 14, 3, 4 }));
            CHECK(same_rects(root->damage(), window.pending_damage()));

            // The next frame consumes both records.
            window.repaint();
            CHECK(root->damage().is_empty());
            CHECK(window.pending_damage().is_empty());

            // The whole-root form damages the whole client area, and only the root (upward only).
            root->update();
            CHECK((root->damage().bounds() == rect_t{ 0, 0, 120, 80 }));
            CHECK(window.pending_damage().area() == static_cast<std::size_t>(120 * 80));
            CHECK(same_rects(root->damage(), window.pending_damage()));
            CHECK(child->damage().is_empty());
            window.repaint();
            CHECK(window.pending_damage().is_empty());
        }
    }

    // ---- (7) a partial repaint paints EXACTLY the damaged rectangles ----
    //
    // Two stacked semi-transparent overlays over the frame's white background: nothing opaque is
    // painted between them and the surface, so repainting a rectangle without clearing it first
    // blends both overlays a second time and the pixels change. The two "sloppy" devices below are
    // the mutation proof — the same comparison must CATCH them.
    {
        widget_t root; // paints nothing: the white below the overlays comes from the frame's clear
        root.set_geometry(rect_t{ 0, 0, 80, 40 });
        counted_widget_t under{ color_t{ 200, 0, 0, 96 }, false, &root }; // semi-transparent red
        under.set_geometry(rect_t{ 0, 0, 80, 40 });
        counted_widget_t over{ color_t{ 0, 0, 200, 96 }, false, &root }; // semi-transparent blue
        over.set_geometry(rect_t{ 0, 0, 80, 40 });

        image_t reference{ extent_t{ 80, 40 } };
        paint_full_frame(reference, root);
        const color_t blended = reference.pixel(30, 20);
        CHECK(!(blended == color_t::white_color())); // the overlays really did blend

        // The damage: two DISJOINT rectangles inside the overlays, so the region's bounds contains
        // the gap between them.
        root.clear_damage();
        under.clear_damage();
        under.update(rect_t{ 5, 5, 6, 6 });
        under.update(rect_t{ 60, 5, 6, 6 });
        const region_t damage = root.damage();
        CHECK(damage.rectangle_count() == 2);
        CHECK(damage.bounds().width() * damage.bounds().height() > damage.area()); // a gap is inside

        // (a) The real path: the pixels already hold the previous frame, then one clipped, cleared
        // pass per rectangle. The result must be byte-identical to a full frame.
        image_t partial{ extent_t{ 80, 40 } };
        paint_full_frame(partial, root);
        paint_damaged(partial, root, damage);
        CHECK(same_pixels(partial, reference));

        // (b) Mutation proof 1: no clear() -> both overlays blend twice inside the damaged rects.
        image_t uncleared{ extent_t{ 80, 40 } };
        paint_full_frame(uncleared, root);
        {
            painter_t painter{ uncleared };
            painter.begin();
            for (const rect_t& area : damage.rects()) {
                painter.save();
                painter.clear_clip();
                painter.set_clip(area);
                root.render(painter);
                painter.restore();
            }
            painter.end();
        }
        CHECK(!same_pixels(uncleared, reference)); // the check above is not vacuous

        // (c) Mutation proof 2: clip to the damage's BOUNDS while clearing only the rectangles ->
        // the gap is painted without being cleared, which blends the overlays there a second time.
        image_t collapsed{ extent_t{ 80, 40 } };
        paint_full_frame(collapsed, root);
        {
            painter_t painter{ collapsed };
            painter.begin();
            painter.set_clip(damage.bounds());
            for (const rect_t& area : damage.rects()) {
                painter.clear(area, color_t::white_color());
            }
            root.render(painter);
            painter.end();
        }
        CHECK(!same_pixels(collapsed, reference));

        // (d) The gap keeps its previous pixels: a correct partial repaint never touches them at all.
        image_t gap_probe{ extent_t{ 80, 40 } };
        paint_full_frame(gap_probe, root);
        {
            painter_t painter{ gap_probe };
            painter.begin();
            for (const rect_t& area : damage.rects()) {
                painter.save();
                painter.clear_clip();
                painter.set_clip(area);
                painter.clear(area, color_t::magenta_color());
                painter.restore();
            }
            painter.end();
        }
        CHECK((gap_probe.pixel(30, 20) == reference.pixel(30, 20))); // between the two rects
        CHECK((gap_probe.pixel(6, 6) == color_t::magenta_color()));  // inside a damaged rect
    }

    // ---- (8) occlusion culling: an opaque sibling above hides the one below ----
    {
        counted_widget_t root{ color_t::white_color(), true };
        root.set_geometry(rect_t{ 0, 0, 100, 60 });

        counted_widget_t below{ color_t::red_color(), true, &root };
        below.set_geometry(rect_t{ 10, 10, 30, 20 });
        counted_widget_t cover{ color_t::blue_color(), true, &root }; // added last: painted on top
        cover.set_geometry(rect_t{ 10, 10, 30, 20 });
        counted_widget_t beside{ color_t::green_color(), true, &root };
        beside.set_geometry(rect_t{ 60, 10, 30, 20 });

        image_t device{ extent_t{ 100, 60 } };
        root.reset_paints();
        below.reset_paints();
        cover.reset_paints();
        beside.reset_paints();
        paint_full_frame(device, root);
        CHECK(root.paints() == 1);
        CHECK(cover.paints() == 1);
        CHECK(beside.paints() == 1);
        CHECK(below.paints() == 0); // completely covered: its paint_event() is never called
        CHECK((device.pixel(15, 15) == color_t::blue_color())); // the cover owns those pixels

        // A translucent cover does NOT hide its sibling: the blend needs the pixels underneath.
        counted_widget_t under_tinted{ color_t::red_color(), true, &root };
        under_tinted.set_geometry(rect_t{ 10, 35, 30, 20 });
        counted_widget_t tinted{ color_t{ 0, 0, 255, 128 }, false, &root };
        tinted.set_geometry(rect_t{ 10, 35, 30, 20 });
        root.reset_paints();
        under_tinted.reset_paints();
        tinted.reset_paints();
        paint_full_frame(device, root);
        CHECK(under_tinted.paints() == 1); // still painted: is_opaque() == false does not occlude
        CHECK(tinted.paints() == 1);
        CHECK(below.paints() == 0); // and the opaque pair is still culled

        // Partial coverage: the sibling is painted, clipped to the visible part (not skipped).
        counted_widget_t half_below{ color_t::red_color(), true, &root };
        half_below.set_geometry(rect_t{ 60, 35, 30, 20 });
        counted_widget_t half_cover{ color_t::blue_color(), true, &root };
        half_cover.set_geometry(rect_t{ 60, 35, 15, 20 }); // left half only
        root.reset_paints();
        half_below.reset_paints();
        half_cover.reset_paints();
        paint_full_frame(device, root);
        CHECK(half_below.paints() == 1);
        CHECK(half_cover.paints() == 1);
        CHECK((device.pixel(62, 40) == color_t::blue_color()));  // covered half
        CHECK((device.pixel(80, 40) == color_t::red_color()));   // visible half of the sibling
    }

    // ---- (9) a static window paints no frame; two damage rectangles are two clipped passes ----
    {
        (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        core_application application{ argc, argv };
        {
            gui_application_t gui;
            CHECK(gui_application_t::is_video_initialized());

            widget_window_t window{ "damage_partial", extent_t{ 120, 80 } };
            auto* const root = new counted_widget_t{ color_t::white_color(), true };
            auto* const left = new counted_widget_t{ color_t::red_color(), true, root };
            left->set_geometry(rect_t{ 10, 10, 20, 20 });
            auto* const middle = new counted_widget_t{ color_t::green_color(), true, root };
            middle->set_geometry(rect_t{ 45, 10, 20, 20 });
            auto* const right = new counted_widget_t{ color_t::blue_color(), true, root };
            right->set_geometry(rect_t{ 80, 10, 20, 20 });
            window.set_root_widget(root);
            window.repaint(); // the first frame is full (a fresh surface has no contents)
            CHECK(root->paints() == 1);
            CHECK(left->paints() == 1);

            // (9a) a window at rest: N frames, zero paint_event() and zero painted frames.
            root->reset_paints();
            left->reset_paints();
            middle->reset_paints();
            right->reset_paints();
            const std::size_t frames_before = window.painted_frame_count();
            for (int frame = 0; frame < 5; ++frame) {
                window.repaint();
            }
            CHECK(root->paints() == 0);
            CHECK(left->paints() == 0);
            CHECK(window.painted_frame_count() == frames_before); // no draw, no flush, no present
            CHECK(window.pending_damage().is_empty());
            std::printf("[damage] static window: 5 frames -> %d paint_event(), %zu painted frame(s)\n",
                        root->paints(), window.painted_frame_count() - frames_before);

            // (9b) two disjoint damaged rectangles are two passes, and the widget in the gap between
            // them is not repainted at all: the clip is the REGION, never its bounding rectangle.
            root->reset_paints();
            left->reset_paints();
            middle->reset_paints();
            right->reset_paints();
            left->update();
            right->update();
            CHECK(window.pending_damage().rectangle_count() == 2);
            window.repaint();
            CHECK(right->paints() == 1);
            CHECK(middle->paints() == 0); // the gap's widget: untouched
            CHECK(root->paints() == 2);   // one pass per damaged rectangle
            CHECK(window.pending_damage().is_empty());
            std::printf("[damage] two damage rects: root painted %d time(s), gap widget %d time(s)\n",
                        root->paints(), middle->paints());

            // (9c) hovering a check box damages only that control.
            check_box_t* const box = new check_box_t{ root };
            box->set_text("check_box_t");
            box->set_geometry(rect_t{ 10, 45, 70, 20 });
            window.repaint(); // settle: the new control is painted once
            box->update();
            window.repaint();
            CHECK(window.pending_damage().is_empty()); // nothing outstanding before the hover

            interaction_t hover;
            hover.mouse = point_t{ 6, 10 }; // inside the control
            hover.hovered = true;
            box->interact(hover);
            const region_t& hover_damage = window.pending_damage();
            const rect_t box_global =
                box->rect().translated(box->map_to_global(point_t{}));
            CHECK(!hover_damage.is_empty());
            CHECK(hover_damage.area() <= static_cast<std::size_t>(box_global.width() * box_global.height()));
            CHECK(covers(region_t{ box_global }, hover_damage.bounds()));
            std::printf("[damage] check box hover: damage %zu px, control %d px (ratio %.3f)\n",
                        hover_damage.area(),
                        box_global.width() * box_global.height(),
                        static_cast<double>(hover_damage.area())
                            / static_cast<double>(box_global.width() * box_global.height()));
            window.repaint();
            CHECK(window.pending_damage().is_empty());

            // (9d) a fully covered control is not painted by a PARTIAL frame either.
            auto* const bottom = new counted_widget_t{ color_t::yellow_color(), true, root };
            bottom->set_geometry(rect_t{ 10, 45, 70, 20 });
            auto* const top = new counted_widget_t{ color_t::cyan_color(), true, root };
            top->set_geometry(rect_t{ 10, 45, 70, 20 }); // added last: completely covers `bottom`
            bottom->update();
            bottom->reset_paints();
            top->reset_paints();
            window.repaint();
            CHECK(bottom->paints() == 0);
            CHECK(top->paints() == 1);
        }
    }

    // ---- (10) the scroll fast path: a shift plus a strip, not a viewport repaint ----
    {
        (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        core_application application{ argc, argv };
        {
            gui_application_t gui;

            // (10a) the exposed region of a shift is the strip the content left behind.
            CHECK(window_t::scroll_exposed_region(rect_t{ 0, 0, 100, 50 }, 0, -10).area()
                  == static_cast<std::size_t>(100 * 10));
            CHECK(window_t::scroll_exposed_region(rect_t{ 0, 0, 100, 50 }, 0, 10).area()
                  == static_cast<std::size_t>(100 * 10));
            CHECK(window_t::scroll_exposed_region(rect_t{ 0, 0, 100, 50 }, 10, 0).area()
                  == static_cast<std::size_t>(10 * 50));
            CHECK(window_t::scroll_exposed_region(rect_t{ 0, 0, 100, 50 }, 0, 0).is_empty());
            CHECK(window_t::scroll_exposed_region(rect_t{ 0, 0, 100, 50 }, 100, 0).area()
                  == static_cast<std::size_t>(100 * 50)); // a full-width shift exposes everything
            CHECK(covers(window_t::scroll_exposed_region(rect_t{ 0, 0, 100, 50 }, 10, -5),
                         rect_t{ 0, 0, 10, 50 }));   // dx > 0: the content moved right, the LEFT band
            CHECK(covers(window_t::scroll_exposed_region(rect_t{ 0, 0, 100, 50 }, -10, -5),
                         rect_t{ 90, 0, 10, 50 }));  // dx < 0: the RIGHT band
            CHECK(covers(window_t::scroll_exposed_region(rect_t{ 0, 0, 100, 50 }, 10, -5),
                         rect_t{ 0, 45, 100, 5 }));  // dy < 0: the BOTTOM band

            // (10b) a scrolling container damages a strip, not the viewport.
            widget_window_t window{ "damage_scroll", extent_t{ 120, 90 } };
            auto* const root = new counted_widget_t{ color_t::white_color(), true };
            auto* const scroller = new scroll_view_t{ root };
            scroller->set_geometry(rect_t{ 0, 0, 120, 90 });
            auto* const content = new counted_widget_t{ color_t::red_color(), true };
            content->set_fixed_size(extent_t{ 120, 400 });
            scroller->set_content(content);
            window.set_root_widget(root);
            window.repaint();

            const rect_t area = scroller->rect();
            // The viewport is the area minus the thickness of every VISIBLE bar (the rule the area
            // uses internally); computing it here keeps the ratio in the same terms as the damage.
            const int viewport_width = area.width()
                - (scroller->vertical_scroll_bar()->is_visible()
                        ? scroller->vertical_scroll_bar()->rect().width()
                        : 0);
            const int viewport_height = area.height()
                - (scroller->horizontal_scroll_bar()->is_visible()
                        ? scroller->horizontal_scroll_bar()->rect().height()
                        : 0);
            const std::size_t viewport_area =
                static_cast<std::size_t>(std::max(0, viewport_width) * std::max(0, viewport_height));
            CHECK(viewport_area > 0);
            CHECK(scroller->vertical_scroll_bar()->maximum() > 0);

            scroller->set_scroll_offset(point_t{ 0, 16 }); // one step down
            CHECK(scroller->scroll_offset().y() == 16);
            const std::size_t damage_area = window.pending_damage().area();
            CHECK(!window.pending_damage().is_empty());
            CHECK(damage_area < viewport_area);
            CHECK((content->geometry().y() == 0)); // the content never moves ...
            CHECK((scroller->source_rect().y() == 16)); // ... the source region does
            const double ratio = static_cast<double>(damage_area) / static_cast<double>(viewport_area);
            CHECK(ratio < 0.5);
            std::printf("[damage] scroll by 16 px: damage %zu px / viewport %zu px (ratio %.3f)\n",
                        damage_area, viewport_area, ratio);
            window.repaint();
            CHECK(window.pending_damage().is_empty());

            // (10c) a shift of more than the viewport falls back to "damage and repaint": the pixels
            // cannot be moved, so the frame has to redraw instead of shifting.
            scroller->set_scroll_offset(point_t{ 0, 200 });
            CHECK(scroller->scroll_offset().y() == 200);
            CHECK(!window.pending_damage().is_empty());
            CHECK(window.pending_damage().area() >= viewport_area); // the whole viewport at least
            window.repaint();
            CHECK(window.pending_damage().is_empty());
        }
    }

    // ---- (11) the surface-level scroll copy is byte-exact (the self-copy through the scratch) ----
    {
        (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        core_application application{ argc, argv };
        {
            gui_application_t gui;
            SDL_Window* const raw_window =
                SDL_CreateWindow("damage_scroll_surface", 64, 48, SDL_WINDOW_HIDDEN);
            CHECK(raw_window != nullptr);
            if (raw_window != nullptr) {
                SDL_Renderer* const renderer = SDL_CreateRenderer(raw_window, nullptr);
                CHECK(renderer != nullptr);
                if (renderer != nullptr) {
                    backing_store_t store{ renderer };
                    CHECK(store.resize(extent_t{ 64, 48 }));
                    CHECK(store.begin_paint());
                    // Four horizontal bands: red, green, blue, yellow, 12 rows each.
                    const color_t bands[] = { color_t::red_color(), color_t::green_color(),
                                              color_t::blue_color(), color_t::yellow_color() };
                    for (int band = 0; band < 4; ++band) {
                        const color_t colour = bands[band];
                        (void)SDL_SetRenderDrawColor(renderer, colour.red(), colour.green(),
                                                     colour.blue(), colour.alpha());
                        const SDL_FRect row{ 0.0f, static_cast<float>(band * 12), 64.0f, 12.0f };
                        (void)SDL_RenderFillRect(renderer, &row);
                    }
                    store.end_paint();

                    // Shift the content up by one band: the rows must move, the bottom band must be
                    // exposed (and still hold the last row until it is repainted).
                    CHECK(store.scroll(rect_t{ 0, 0, 64, 48 }, 0, -12));
                    // Read the SURFACE, not the window: a flip-model swapchain's back buffer is
                    // undefined after present, so begin_paint() (the surface as the target) is what
                    // makes the pixels readable on every driver.
                    CHECK(store.begin_paint());
                    SDL_Surface* const captured = SDL_RenderReadPixels(renderer, nullptr);
                    store.end_paint();
                    CHECK(captured != nullptr);
                    if (captured != nullptr) {
                        // Sample one pixel of each band row: the shift is a real pixel move, not a
                        // clearing of the area.
                        const auto pixel_at = [captured](int x, int y, color_t& colour) {
                            std::uint8_t red = 0;
                            std::uint8_t green = 0;
                            std::uint8_t blue = 0;
                            std::uint8_t alpha = 0;
                            if (!SDL_ReadSurfacePixel(captured, x, y, &red, &green, &blue, &alpha)) {
                                return false;
                            }
                            colour = color_t{ red, green, blue, alpha };
                            return true;
                        };
                        color_t top{};
                        color_t second{};
                        color_t third{};
                        color_t exposed{};
                        CHECK(pixel_at(32, 1, top));
                        CHECK(pixel_at(32, 13, second));
                        CHECK(pixel_at(32, 25, third));
                        CHECK(pixel_at(32, 40, exposed));
                        // Before the shift rows 0..11 were red and 12..23 green; shifting the content
                        // up by one band leaves green on top and blue in the second band.
                        CHECK((top == color_t::green_color()));
                        CHECK((second == color_t::blue_color()));
                        CHECK((third == color_t::yellow_color()));
                        // The exposed bottom band is untouched by the shift (the caller repaints it).
                        CHECK((exposed == color_t::yellow_color()));
                        SDL_DestroySurface(captured);
                    }
                }
                SDL_DestroyRenderer(renderer);
            }
            SDL_DestroyWindow(raw_window);
        }
    }

    // ---- (12) an idle immediate host damages nothing ----
    //
    // The host drives its controls on every pass, so the pass itself is the danger spot: a control that
    // kept its row must NOT be re-placed through set_geometry() (which requests a repaint), and neither
    // paint() nor control() may call request_frame(). Part (a) reads the damage the pass records — it is
    // visible inside the frame handler, because the host drops its own record only at the end of the
    // pass. Part (b) runs the same host inside a real (offscreen) window and counts painted frames.
    {
        // (12a) five passes over image_t, one control created in the first: the four later passes
        // re-place the same row and must leave the host's record empty.
        widget_t root;
        root.set_geometry(rect_t{ 0, 0, 120, 90 });
        im_host_t host;
        host.set_parent(&root);
        host.set_geometry(rect_t{ 0, 0, 120, 90 });
        int passes = 0;
        int damaging_passes = 0;
        host.set_frame_handler([&passes, &damaging_passes](im_host_t& driven) {
            (void)driven.control("push_button_t", "ok");
            // Pass 0 creates and places the control (a real change); every later pass finds the row
            // unchanged, so the placement must not damage the control. The HOST owns no repaint record
            // (it is an item_t that drives a process, not a widget with pixels).
            if (passes >= 1) {
                const widget_t* const placed = driven.widget("push_button_t", "ok");
                if (placed != nullptr && !placed->damage().is_empty()) {
                    ++damaging_passes;
                }
            }
            ++passes;
        });

        image_t device{ extent_t{ 120, 90 } };
        for (int pass = 0; pass < 5; ++pass) {
            painter_t painter{ device };
            CHECK(painter.begin());
            root.render(painter);
            CHECK(painter.end());
            CHECK(root.damage().is_empty()); // the painted root consumed its record
        }
        CHECK(passes == 5);
        CHECK(damaging_passes == 0);
        std::printf("[damage] idle immediate host: %d pass(es) over an image, %d damaging pass(es)\n",
                    passes, damaging_passes);
    }
    {
        // (12b) the same host in a window: five idle repaints paint NO frame, one injected pointer
        // move paints exactly one.
        (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        core_application application{ argc, argv };
        {
            gui_application_t gui;
            CHECK(gui_application_t::is_video_initialized());

            widget_window_t window{ "damage_idle_im", extent_t{ 120, 80 } };
            auto* const root = new solid_widget_t{ color_t::white_color() };
            auto* const host = new im_host_t;
            host->set_parent(root);
            // Smaller than the window on purpose: the damage of an interaction must stay the host's own
            // rectangle instead of the whole client area.
            host->set_geometry(rect_t{ 8, 6, 60, 30 });
            int window_passes = 0;
            host->set_frame_handler([&window_passes](im_host_t& driven) {
                (void)driven.control("push_button_t", "ok");
                ++window_passes;
            });
            window.set_root_widget(root);
            window.repaint(); // the first frame is full: it runs the host's pass once
            CHECK(window_passes == 1);
            CHECK(window.pending_damage().is_empty());

            // A PARTIAL frame over a small rectangle inside the host runs the pass too, but paints only
            // that rectangle: a pass that damages its own (larger) rectangle would leave the rest
            // behind, and the next idle frame would paint it. So this frame proves the pass damaged
            // nothing it did not paint.
            root->update(rect_t{ 12, 10, 4, 4 }); // root coordinates, inside the host's rectangle
            CHECK(!window.pending_damage().is_empty());
            window.repaint();
            CHECK(window_passes == 2);
            CHECK(window.pending_damage().is_empty());

            const std::size_t frames_before = window.painted_frame_count();
            for (int frame = 0; frame < 5; ++frame) {
                window.repaint();
                CHECK(window.pending_damage().is_empty());
            }
            // Nothing was damaged, so paint_frame() neither drew nor presented and the pass did not
            // even run: the host schedules no frame of its own.
            CHECK(window.painted_frame_count() == frames_before);
            const std::size_t idle_frames = window.painted_frame_count() - frames_before;
            CHECK(window_passes == 2);
            CHECK(root->damage().is_empty());
            // The host owns no repaint record (it is an item_t driving a process, not a widget with
            // pixels), so the root's empty damage above is the whole statement about idleness.

            // A pointer move is INPUT: the host claims it, requests a frame and damages exactly its
            // own rectangle.
            interaction_t move;
            move.mouse = point_t{ 4, 3 };
            move.hovered = true;
            CHECK(host->interact(move));
            CHECK(!window.pending_damage().is_empty());
            const rect_t host_global = host->rect().translated(host->map_to_global(point_t{}));
            CHECK(covers(window.pending_damage(), host_global));
            CHECK(window.pending_damage().area() <= static_cast<std::size_t>(60 * 30));
            window.repaint();
            CHECK(window.painted_frame_count() == frames_before + 1);
            CHECK(window_passes == 3); // the replay pass ran
            CHECK(window.pending_damage().is_empty());
            std::printf("[damage] idle immediate host in a window: 5 idle frames -> %zu painted "
                        "frame(s), one pointer move -> %zu\n",
                        idle_frames, window.painted_frame_count() - frames_before - idle_frames);
        }
    }

    // ---- (13) the pointer's coordinate space: WINDOW coordinates -> the frame's pixel grid ----
    //
    // window_t carries three extents (see its class note): the device size in PIXELS (size(), the
    // space geometry, damage and painting live in), the window's pixel size
    // (SDL_GetWindowSizeInPixels, the backing surface's grid) and the window's size in WINDOW
    // coordinates (SDL_GetWindowSize, the space SDL reports pointer positions in). Part (a) proves
    // with a REAL window that the first two and the pixels a frame presents agree — the invariant
    // every frame relies on — and that the third equals them at this platform's density of 1.
    // Part (b) proves the pointer wiring at that density: the converted position is the raw one, byte
    // for byte. Part (c) injects the two extents the conversion divides (200x120 -> 300x180, the 1.5
    // density this machine's driver cannot report) into a REAL window, and checks the point the real
    // filter stores and the control the real hit test finds. The arithmetic itself (every density,
    // the rounding rule, the degenerate extents) is pinned by gui_window_coordinate_test.
    {
        (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        core_application application{ argc, argv };
        {
            gui_application_t gui;
            CHECK(gui_application_t::is_video_initialized());

            {
                const extent_t requested{ 200, 120 };
                clip_recording_window_t window{ "damage_pixel_grid", requested };
                auto* const root = new solid_widget_t{ color_t::white_color() };
                window.host.set_root_widget(root);

                // (a1) The drawing target: the ui layer lays the root out to the device size in pixels.
                CHECK((window.size() == requested));
                CHECK((root->geometry() == rect_t{ 0, 0, requested.width(), requested.height() }));

                window.clear_passes();
                window.repaint();
                // (a2) The frame painted ONE pass, clipped to exactly the whole client area, and
                //      presented all of it: the damage grid IS the drawing target IS the backing
                //      surface's extent.
                CHECK(window.passes.size() == 1);
                CHECK((window.passes.at(0) == rect_t{ 0, 0, window.size().width(), window.size().height() }));
                CHECK(window.presented_region().rectangle_count() == 1);
                CHECK((window.presented_region().bounds() == window.passes.at(0)));
                CHECK(window.pending_damage().is_empty()); // the frame consumed what it painted

                // (a3) A damage rectangle that reaches past the client area is CLIPPED to the pixel
                //      grid: the frame never paints a pixel the surface does not have.
                root->update(rect_t{ 190, 110, 40, 40 }); // the tree's space == the window's pixel space
                window.clear_passes();
                window.repaint();
                CHECK(window.passes.size() == 1);
                CHECK((window.passes.at(0) == rect_t{ 190, 110, 10, 10 }));
                CHECK((window.passes.at(0).intersected(
                           rect_t{ 0, 0, window.size().width(), window.size().height() })
                       == window.passes.at(0)));

                // (a4) The platform's numbers, straight from SDL: the window's size (the space a
                //      pointer position arrives in) equals its pixel size, so at this density the
                //      pointer conversion has to be the identity.
                SDL_Window* const sdl_window = SDL_GetWindowFromID(window.window_id());
                CHECK(sdl_window != nullptr);
                int window_width = 0;
                int window_height = 0;
                int pixel_width = 0;
                int pixel_height = 0;
                CHECK(SDL_GetWindowSize(sdl_window, &window_width, &window_height));
                CHECK(SDL_GetWindowSizeInPixels(sdl_window, &pixel_width, &pixel_height));
                CHECK(window_width == pixel_width && window_height == pixel_height);
                CHECK(SDL_GetWindowPixelDensity(sdl_window) == 1.0f);
                CHECK(window_width == window.size().width() && window_height == window.size().height());

                // (b) The wiring at density 1: a pushed SDL motion keeps the exact position the OLD
                //     input path stored (static_cast<int> of each coordinate), byte for byte. The
                //     expected values are literals on purpose: computing them with the same formula
                //     the implementation uses would make the section a tautology.
                const struct {
                    float x;
                    float y;
                    int expected_x;
                    int expected_y;
                } samples[] = {
                    { 0.0f, 0.0f, 0, 0 },
                    { 100.75f, 60.25f, 100, 60 },
                    { 199.99f, 119.99f, 199, 119 },
                    { 12.5f, 7.5f, 12, 7 },
                    { -3.5f, -4.5f, -3, -4 },
                };
                for (const auto& sample : samples) {
                    point_t converted{};
                    CHECK(motion_position(window, sample.x, sample.y, converted));
                    if (!(converted == point_t{ sample.expected_x, sample.expected_y })) {
                        std::printf("       motion (%.2f, %.2f) -> %d,%d, expected %d,%d\n",
                                    static_cast<double>(sample.x), static_cast<double>(sample.y),
                                    converted.x(), converted.y(), sample.expected_x,
                                    sample.expected_y);
                    }
                    CHECK((converted == point_t{ sample.expected_x, sample.expected_y }));
                }
                std::printf("[damage] pointer wiring at density 1: %zu motion(s) kept the raw position\n",
                            sizeof(samples) / sizeof(samples[0]));
            }

            {
                // (c) The same REAL filter at a SIMULATED 1.5 density: only the two extents the
                //     conversion divides are injected (200x120 window coordinates -> 300x180 pixels);
                //     the SDL queue, the native filter, input_state and the ui hit test are untouched.
                simulated_density_window_t window{ "pointer_density", extent_t{ 200, 120 } };
                CHECK((window.size() == extent_t{ 200, 120 })); // the real device is still density 1
                SDL_Window* const sdl_window = SDL_GetWindowFromID(window.window_id());
                CHECK(sdl_window != nullptr);
                CHECK(SDL_GetWindowPixelDensity(sdl_window) == 1.0f);

                auto* const root = new solid_widget_t{ color_t::white_color() };
                auto* const at_converted = new solid_widget_t{ color_t::red_color(), root };
                auto* const at_window = new solid_widget_t{ color_t::blue_color(), root };
                at_converted->set_geometry(rect_t{ 150 - 8, 90 - 8, 16, 16 });
                at_window->set_geometry(rect_t{ 100 - 8, 60 - 8, 16, 16 });
                window.host.set_root_widget(root);
                CHECK(!at_converted->is_hovered());
                CHECK(!at_window->is_hovered());

                point_t converted{};
                CHECK(motion_position(window, 100.0f, 60.0f, converted));
                // (c1) The REAL filter stored the point the injected extents produce:
                //      floor(100 * 300 / 200) = 150 and floor(60 * 180 / 120) = 90.
                CHECK((converted == point_t{ 150, 90 }));
                // (c2) The REAL hit test found the control that occupies (150,90) ...
                CHECK(at_converted->is_hovered());
                // (c3) ... and NOT the control at the raw window point.
                CHECK(!at_window->is_hovered());
                // (c4) The raw point names the other control, so (c2)/(c3) fail if the conversion is
                //      removed or turned into the identity: the section is not a tautology.
                const rect_t at_window_rect =
                    at_window->rect().translated(at_window->map_to_global(point_t{}));
                const rect_t at_converted_rect =
                    at_converted->rect().translated(at_converted->map_to_global(point_t{}));
                CHECK(at_window_rect.contains(point_t{ 100, 60 }));
                CHECK(!at_converted_rect.contains(point_t{ 100, 60 }));
                std::printf("[damage] pointer at a simulated 1.5 density: motion 100,60 -> %d,%d; "
                            "control at 150,90 hovered, control at 100,60 not\n",
                            converted.x(), converted.y());
            }
        }
    }

    if (g_failures == 0) {
        std::printf("damage: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("damage: %d of %d checks failed\n", g_failures, g_checks);
    return 1;
}
