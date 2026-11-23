// One item tree, many outputs. Painting has no output object at all: the tree paints itself, and a
// frame decides only how many passes it makes, what it clears and which clip it sets. Every output
// paints the same item state, and none of them routes, holds input state or decides what is under the
// pointer.
//
// The acceptance criteria as executable checks:
//  1. an offscreen frame of a root paints exactly the pixels the WINDOW frame paints (same tree, same
//     damage region), and the blended overlap proves the comparison is not a flat single-colour frame;
//  2. two outputs of the same root paint identical pixels, also for a partial (two-rectangle) repaint,
//     which proves painting reads the tree and changes nothing in it;
//  3. painting does not touch input: one router step changes the item state ONCE and every output
//     paints that state;
//  4. a frame paints per damage RECTANGLE, never its bounding rectangle (the gap between two damaged
//     rectangles keeps its pixels), with the bounds-clip counter-proof, and a backend that declares
//     engine_feature_flag::CLIP_REGION is painted in ONE pass for the whole region;
//  5. the damage record belongs to the TREE (update() records it, painting consumes it), an empty
//     damage region paints nothing at all, and paint_event_t::damage() carries the item-local area;
//  5b. paint_event_t::damage() is translated into the item's coordinates and intersected with its
//     rectangle, and an opaque child hides the damage under it;
//  6. the window's sinks are wired by widget_window_t: a root update() reaches the window, and a scroll
//     asks the window to shift pixels while the tree damages exactly the region the window answers;
//  7. the public widget_window_t API is unchanged (root_widget / router / pending_damage / frames);
//  8. several scroll steps that coalesce into one frame leave the band of the ACCUMULATED shift in the
//     window's pending damage, through the real chain (container -> window sink -> window::scroll());
//  9. a scroll record schedules a frame by itself: no input step and no explicit repaint is needed.
//
// Sections 1, 6, 7, 8 and 9 use a real window under SDL's dummy video driver (still offscreen); the
// rest is headless, over image_t and a recording device.

#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/gui_application_t.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/paint_device_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/gui/region_t.h"
#include "hwshqtb/metaobject/core_application.h"
#include "hwshqtb/ui/item_t.h"
#include "hwshqtb/ui/input_router_t.h"
#include "hwshqtb/ui/scroll_view_t.h"
#include "hwshqtb/ui/scroll_bar_t.h"
#include "hwshqtb/ui/widget_t.h"
#include "hwshqtb/ui/widget_window_t.h"

using namespace hwshqtb::gui;
using hwshqtb::metaobject::core_application;
using hwshqtb::ui::input_router_t;
using hwshqtb::ui::item_t;
using hwshqtb::ui::scroll_view_t;
using hwshqtb::ui::widget_t;
using hwshqtb::ui::widget_window_t;

namespace view_test {

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

/// The interface contract of the widget layer: the composition entry is render(painter), the per-widget
/// hook is paint_event (own pixels only, protected like Qt's paintEvent), the damage query is noexcept,
/// and an item_t that is not a widget has none of them — it owns no pixels.
using widget_render_signature = void (widget_t::*)(painter_t&);
static_assert(std::is_same_v<decltype(static_cast<widget_render_signature>(&widget_t::render)),
                             widget_render_signature>);
static_assert(noexcept(std::declval<const widget_t&>().damage()));

/// A widget that fills its own rectangle with one colour, so a paint pass is visible in the pixels.
class solid_widget_t final: public widget_t {
public:
    explicit solid_widget_t(color_t color, widget_t* parent = nullptr):
        widget_t(parent),
        _color(color) { }

protected:
    void paint_event(::hwshqtb::ui::paint_event_t* event) override {
        painter_t& painter = event->painter();
        painter.set_color(_color);
        painter.fill_rect(rect());
    }

private:
    color_t _color;
};

/// A solid widget that counts its paint passes.
class counted_widget_t final: public widget_t {
public:
    explicit counted_widget_t(color_t color, widget_t* parent = nullptr):
        widget_t(parent),
        _color(color) { }

    /// @brief How many paint passes reached this widget.
    int paints() const noexcept {
        return _paints;
    }

    /// @brief The damage the last paint EVENT carried, in this widget's coordinates.
    /// @note Every pass of the fallback adds its rectangle, so one paint of a child that a sibling
    ///       splits into two passes reports the union of both.
    region_t last_damage() const noexcept {
        return _last_damage;
    }

protected:
    void paint_event(::hwshqtb::ui::paint_event_t* event) override {
        painter_t& painter = event->painter();
        ++_paints;
        _last_damage.unite(event->damage());
        painter.set_color(_color);
        painter.fill_rect(rect());
    }

private:
    color_t _color;
    int _paints = 0;
    region_t _last_damage;
};

/// A widget whose container-side scroll request is exposed: it calls the protected
/// widget_t::notify_scroll() the way a scrolling container does, so the fast path can be driven with
/// no input step at all.
class scroll_probe_widget_t final: public widget_t {
public:
    explicit scroll_probe_widget_t(widget_t* parent = nullptr):
        widget_t(parent) { }

    /// @brief Ask for a pixel shift of @p local by (@p dx, @p dy): a container's scroll fast path.
    /// @param local The shifted rectangle, in this widget's coordinates.
    /// @param dx The horizontal shift.
    /// @param dy The vertical shift.
    void request_scroll(rect_t local, int dx, int dy) {
        notify_scroll(local, dx, dy);
    }

protected:
    void paint_event(::hwshqtb::ui::paint_event_t* event) override {
        painter_t& painter = event->painter();
        painter.set_color(color_t::white_color());
        painter.fill_rect(rect());
    }
};

/// A widget whose colour is the ITEM state (pressed > hovered > idle) and that counts the interactions
/// it receives: painting must read that state without ever adding a step of its own.
class state_widget_t final: public widget_t {
public:
    using widget_t::widget_t;

    /// @brief How many interactions (steps) reached this widget.
    int interactions = 0;
    /// @brief How many hover changes the item recorded.
    int hover_changes = 0;
    /// @brief How many press changes the item recorded.
    int press_changes = 0;

    bool interact(const interaction_t&) override {
        ++interactions;
        return false; // keep the delivery chain going: the router still owns the state
    }

    void hover_event(::hwshqtb::ui::hover_event_t* event) override {
        ++hover_changes;
        ::hwshqtb::ui::item_t::hover_event(event); // keep the default repaint request
    }

    void press_event(::hwshqtb::ui::press_event_t* event) override {
        ++press_changes;
        ::hwshqtb::ui::item_t::press_event(event);
    }

protected:
    void paint_event(::hwshqtb::ui::paint_event_t* event) override {
        painter_t& painter = event->painter();
        if (is_pressed(mouse_button_enum::LEFT)) {
            painter.set_color(color_t::blue_color());
        }
        else if (is_hovered()) {
            painter.set_color(color_t::red_color());
        }
        else {
            painter.set_color(color_t::green_color());
        }
        painter.fill_rect(rect());
    }
};

/// The full device as a damage region.
region_t full_region(int width, int height) {
    return region_t{ rect_t{ 0, 0, width, height } };
}

/// A recording engine: counts draw calls and remembers region clips.
class recording_engine_t final: public paint_engine_t {
public:
    engine_feature_t features() const noexcept override {
        return engine_feature_t{ engine_feature_flag::CLIP } | engine_feature_flag::CLIP_REGION;
    }

    bool begin(paint_device_t&) override {
        draws = 0;
        region_clips = 0;
        region_rects = 0;
        return true;
    }

    bool end() override {
        return true;
    }

    void update_state(const engine_state_t& state) override {
        if ((state.dirty_flags() & engine_state_t::dirty_clip) != 0
            && state.clip_enabled() && state.clip_region_enabled()) {
            ++region_clips;
            region_rects = static_cast<int>(state.clip_region().size());
        }
    }

    void draw_rects(const rect_t*, int count, bool) override {
        draws += count;
    }

    void draw_glyphs(const glyph_run_t&) override { }
    void draw_image(const rect_t&, const image_view_t&) override { }

    /// @brief How many rectangles the device drew (clears included).
    int draws = 0;
    /// @brief How many times a REGION clip was handed to the backend.
    int region_clips = 0;
    /// @brief How many rectangles the last region clip carried.
    int region_rects = 0;
};

/// A real window whose ENGINE declares engine_feature_flag::CLIP_REGION, so the frame's region branch
/// can be driven through the window's own public frame path — the code that owns the pass policy.
///
/// It is a plain gui::window_t with the item-tree host composed into it: the pass policy is the
/// window's, the tree painting is the host's, and the paint forward is the only wire between them.
class region_backend_window_t final: public window_t {
public:
    region_backend_window_t(std::string_view title, extent_t size):
        window_t(title, size),
        host(*this) { }

    /// @brief The item-tree host this window composes.
    widget_window_t host;

    /// @brief Paint @p damage through the real frame (report it, then repaint).
    /// @param damage The region to paint, in window coordinates.
    /// @note The frame is the only caller of paint_event(), and this window's device hands out the
    ///       recording engine instead of the SDL one, so the counters describe exactly the passes the
    ///       frame made.
    void paint_with_recorded_engine(const region_t& damage) {
        set_pending_damage(damage);
        repaint();
    }

    /// @brief Paint one priming frame over @p whole and forget the draw record.
    /// @param whole The whole client area.
    /// @note The first frame of a window repaints everything (there is no persistent surface yet), so
    ///       a test that measures PARTIAL passes needs this one first.
    void prime(const region_t& whole) {
        set_pending_damage(whole);
        repaint();
        (void)engine.begin(*this); // reset the counters
    }

    paint_engine_t* paint_engine() noexcept override {
        return &engine;
    }

    recording_engine_t engine;

protected:
    void paint_event(paint_event_t* event) override {
        host.paint_event(event);
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

/// Whether two images hold the same pixel in every position.
bool same_pixels(const image_t& left, const image_t& right) {
    if (!(left.size() == right.size())) {
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

/// Paint @p damage of @p root's tree into @p device the way a FRAME does (a begun painter per call,
/// one pass per damaged rectangle with that rectangle cleared and clipped), so the checks below drive
/// exactly the path a window drives without needing a window.
void render_frame(image_t& device, widget_t& root, const region_t& damage) {
    painter_t painter{ device };
    CHECK(painter.begin());
    for (const rect_t& area : damage.rects()) {
        if (area.isEmpty()) {
            continue;
        }
        painter.save();
        painter.clear_clip();
        painter.set_clip(area);
        painter.clear(area, color_t::white_color());
        root.render(painter, region_t{ area });
        painter.restore();
    }
    CHECK(painter.end());
}

/// Read a BMP back into an ARGB colour per pixel (the window's capture path, for comparison).
bool load_bmp(const std::string& path, std::vector<color_t>& pixels, int& width, int& height) {
    SDL_Surface* const raw = SDL_LoadBMP(path.c_str());
    if (raw == nullptr) {
        return false;
    }
    width = raw->w;
    height = raw->h;
    pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), color_t{});
    bool ok = true;
    for (int y = 0; y < height && ok; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint8_t red = 0;
            std::uint8_t green = 0;
            std::uint8_t blue = 0;
            std::uint8_t alpha = 0;
            if (!SDL_ReadSurfacePixel(raw, x, y, &red, &green, &blue, &alpha)) {
                ok = false;
                break;
            }
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                   + static_cast<std::size_t>(x)] = color_t{ red, green, blue, alpha };
        }
    }
    SDL_DestroySurface(raw);
    return ok;
}

}  // namespace view_test

using namespace view_test;

int main(int argc, char** argv) {
    std::printf("== one item tree, many outputs ==\n");

    // -----------------------------------------------------------------------
    // (1) The window frame and an offscreen frame of the same root paint the same pixels.
    // -----------------------------------------------------------------------
    {
        (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        core_application application{ argc, argv };
        {
            gui_application_t gui;
            CHECK(gui_application_t::is_video_initialized());

            widget_window_t window{ "view_window_path", extent_t{ 120, 80 } };
            auto* const root = new solid_widget_t{ color_t::white_color() };
            auto* const block = new solid_widget_t{ color_t::red_color(), root };
            block->set_geometry(rect_t{ 8, 8, 40, 30 });
            // A translucent overlay crosses both the red block and the white background: if either
            // path cleared or blended differently, the comparison below would see it.
            auto* const tint = new solid_widget_t{ color_t{ 0, 0, 255, 128 }, root };
            tint->set_geometry(rect_t{ 20, 14, 44, 34 });
            auto* const far = new solid_widget_t{ color_t::green_color(), root };
            far->set_geometry(rect_t{ 78, 20, 30, 24 });
            window.set_root_widget(root);

            const std::string path = "ui_view_test_window.bmp";
            window.save_screenshot(path);
            std::vector<color_t> window_pixels;
            int width = 0;
            int height = 0;
            const bool loaded = load_bmp(path, window_pixels, width, height);
            CHECK(loaded);
            CHECK(width == 120);
            CHECK(height == 80);
            CHECK(std::remove(path.c_str()) == 0);

            // The same root, the same damage region, through a stateless offscreen output.
            image_t offscreen{ extent_t{ 120, 80 } };
            render_frame(offscreen, *root, full_region(120, 80));

            if (loaded) {
                bool same = true;
                int first_x = -1;
                int first_y = -1;
                for (int y = 0; y < 80 && same; ++y) {
                    for (int x = 0; x < 120; ++x) {
                        const color_t& captured =
                            window_pixels[static_cast<std::size_t>(y) * 120 + static_cast<std::size_t>(x)];
                        if (!(offscreen.pixel(x, y) == captured)) {
                            same = false;
                            first_x = x;
                            first_y = y;
                            break;
                        }
                    }
                }
                if (!same) {
                    std::printf("  [view] first pixel mismatch at (%d, %d): offscreen %d,%d,%d,%d vs "
                                "window %d,%d,%d,%d\n",
                                first_x, first_y, offscreen.pixel(first_x, first_y).red(),
                                offscreen.pixel(first_x, first_y).green(),
                                offscreen.pixel(first_x, first_y).blue(),
                                offscreen.pixel(first_x, first_y).alpha(),
                                window_pixels[static_cast<std::size_t>(first_y) * 120
                                              + static_cast<std::size_t>(first_x)].red(),
                                window_pixels[static_cast<std::size_t>(first_y) * 120
                                              + static_cast<std::size_t>(first_x)].green(),
                                window_pixels[static_cast<std::size_t>(first_y) * 120
                                              + static_cast<std::size_t>(first_x)].blue(),
                                window_pixels[static_cast<std::size_t>(first_y) * 120
                                              + static_cast<std::size_t>(first_x)].alpha());
                }
                CHECK(same);

                // The overlap really blended: the frame is not a flat single-colour picture.
                const color_t overlap =
                    window_pixels[static_cast<std::size_t>(25) * 120 + static_cast<std::size_t>(30)];
                CHECK(!(overlap == color_t::red_color()));
                CHECK(!(overlap == color_t{ 0, 0, 255, 128 }));
                std::printf("[view] window vs offscreen output: %dx%d pixels, overlap %d,%d,%d\n", width,
                            height, overlap.red(), overlap.green(), overlap.blue());
            }

            // save_painted_frame() reads the frame the last paint left WITHOUT painting one: the
            // frame count and the pending damage stay untouched, and the pixels it writes are the
            // ones a save_screenshot() of the same state would capture.
            {
                const std::size_t frames_before = window.painted_frame_count();
                CHECK(window.pending_damage().is_empty());
                const std::string painted_path = "ui_view_test_painted.bmp";
                window.save_painted_frame(painted_path);
                CHECK(window.painted_frame_count() == frames_before);
                CHECK(window.pending_damage().is_empty());
                std::vector<color_t> painted_pixels;
                int painted_width = 0;
                int painted_height = 0;
                CHECK(load_bmp(painted_path, painted_pixels, painted_width, painted_height));
                CHECK(painted_width == 120);
                CHECK(painted_height == 80);
                CHECK((painted_pixels == window_pixels));
                CHECK(std::remove(painted_path.c_str()) == 0);
            }
        }
    }

    // -----------------------------------------------------------------------
    // (2) Two outputs of the same tree paint the same pixels.
    // -----------------------------------------------------------------------
    {
        solid_widget_t root{ color_t::white_color() };
        root.set_geometry(rect_t{ 0, 0, 100, 60 });
        auto* const left = new solid_widget_t{ color_t::red_color(), &root };
        left->set_geometry(rect_t{ 5, 5, 30, 20 });
        auto* const tint = new solid_widget_t{ color_t{ 0, 0, 255, 128 }, &root };
        tint->set_geometry(rect_t{ 20, 10, 40, 30 });

        image_t first_device{ extent_t{ 100, 60 } };
        image_t second_device{ extent_t{ 100, 60 } };

        render_frame(first_device, root, full_region(100, 60));
        render_frame(second_device, root, full_region(100, 60));
        CHECK(same_pixels(first_device, second_device));
        // ... and the frame is not a blank canvas: the block is there and the overlay blended.
        CHECK((first_device.pixel(10, 10) == color_t::red_color()));
        CHECK(!(first_device.pixel(50, 25) == color_t::white_color()));
        CHECK(!(first_device.pixel(50, 25) == color_t{ 0, 0, 255, 128 }));
        // Painting is output-side only: the tree keeps its two children and no damage record.
        CHECK(root.item_count() == 2);
        CHECK(root.damage().is_empty());

        // Render again: painting changes nothing in the tree, so the pixels survive another pass.
        render_frame(first_device, root, full_region(100, 60));
        render_frame(second_device, root, full_region(100, 60));
        CHECK(same_pixels(first_device, second_device));
        CHECK(root.damage().is_empty());

        // A partial repaint is just as output-independent.
        region_t partial;
        partial.unite(rect_t{ 3, 3, 6, 6 });
        partial.unite(rect_t{ 60, 3, 6, 6 });
        render_frame(first_device, root, partial);
        render_frame(second_device, root, partial);
        CHECK(same_pixels(first_device, second_device));
    }

    // -----------------------------------------------------------------------
    // (3) One router step changes the item state once; every output reads that state.
    // -----------------------------------------------------------------------
    {
        state_widget_t root;
        root.set_geometry(rect_t{ 0, 0, 100, 50 });
        image_t first_device{ extent_t{ 100, 50 } };
        image_t second_device{ extent_t{ 100, 50 } };
        input_router_t router{ root }; // the ONE input session: painting never routes

        root.clear_damage();
        interaction_t move;
        move.mouse = point_t{ 20, 20 };
        (void)router.dispatch(move);
        CHECK(root.interactions == 1);
        CHECK(root.hover_changes == 1);
        CHECK(root.is_hovered());

        render_frame(first_device, root, full_region(100, 50));
        render_frame(second_device, root, full_region(100, 50));
        CHECK(same_pixels(first_device, second_device));
        CHECK((first_device.pixel(50, 25) == color_t::red_color())); // hovered: every output agrees
        CHECK(root.interactions == 1); // painting is not an input step

        interaction_t press = move;
        press.button = mouse_button_enum::LEFT;
        press.mouse_down = true;
        press.mouse_pressed = true;
        (void)router.dispatch(press);
        CHECK(root.interactions == 2);
        CHECK(root.press_changes == 1);
        CHECK(root.is_pressed(mouse_button_enum::LEFT));
        render_frame(first_device, root, full_region(100, 50));
        render_frame(second_device, root, full_region(100, 50));
        CHECK(same_pixels(first_device, second_device));
        CHECK((first_device.pixel(50, 25) == color_t::blue_color())); // pressed wins over hover
        CHECK(root.interactions == 2); // still exactly one step per dispatch

        interaction_t release = move;
        release.button = mouse_button_enum::LEFT;
        release.mouse_released = true;
        (void)router.dispatch(release);
        CHECK(root.interactions == 3);
        CHECK(root.press_changes == 2);
        CHECK(!root.is_pressed(mouse_button_enum::LEFT));
        render_frame(first_device, root, full_region(100, 50));
        render_frame(second_device, root, full_region(100, 50));
        CHECK(same_pixels(first_device, second_device));
        CHECK((first_device.pixel(50, 25) == color_t::red_color())); // still hovered
        CHECK(root.interactions == 3);

        // The inflation cap: two outputs + one router must not multiply the delivered steps.
        std::printf("[view] one router step per dispatch: %d interact() call(s) after 3 steps, "
                    "%d hover change(s), %d press change(s)\n",
                    root.interactions, root.hover_changes, root.press_changes);
    }

    // -----------------------------------------------------------------------
    // (4) Painting is per damage RECTANGLE, never its bounding rectangle.
    // -----------------------------------------------------------------------
    {
        // The root itself paints NOTHING: the white below the overlay comes from the frame's clear, so
        // repainting a rectangle without clearing it first blends the overlay a second time.
        widget_t root;
        root.set_geometry(rect_t{ 0, 0, 80, 40 });
        auto* const overlay = new solid_widget_t{ color_t{ 0, 0, 255, 128 }, &root };
        overlay->set_geometry(rect_t{ 0, 0, 60, 30 }); // covers both rects AND the gap between them

        image_t reference{ extent_t{ 80, 40 } };
        image_t partial{ extent_t{ 80, 40 } };

        render_frame(reference, root, full_region(80, 40));
        render_frame(partial, root, full_region(80, 40)); // the previous frame is in place

        region_t damage;
        damage.unite(rect_t{ 3, 3, 6, 6 });
        damage.unite(rect_t{ 50, 3, 6, 6 });
        CHECK(damage.rectangle_count() == 2);
        CHECK(damage.bounds().width() * damage.bounds().height()
              > static_cast<int>(damage.area())); // a gap is inside the bounds

        // The gap keeps its pixels: a clip of the damage's BOUNDS would paint the translucent
        // overlay there a second time, without clearing it first.
        render_frame(partial, root, damage);
        CHECK(same_pixels(partial, reference));

        // Counter-proof that the check above is not vacuous: the bounds-clip path differs.
        image_t bounds_clip{ extent_t{ 80, 40 } };
        render_frame(bounds_clip, root, full_region(80, 40));
        {
            painter_t painter{ bounds_clip };
            CHECK(painter.begin());
            painter.set_clip(damage.bounds());
            painter.clear(damage.rects()[0], color_t::white_color());
            painter.clear(damage.rects()[1], color_t::white_color());
            root.render(painter);
            CHECK(painter.end());
        }
        CHECK(!same_pixels(bounds_clip, reference));
    }

    // -----------------------------------------------------------------------
    // (4b) A backend that DECLARES engine_feature_flag::CLIP_REGION is painted in ONE pass for the
    //      whole region by the FRAME; the fallback above splits it into one pass per rectangle.
    // -----------------------------------------------------------------------
    {
        (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        core_application application{ argc, argv };
        {
            gui_application_t gui;
            CHECK(gui_application_t::is_video_initialized());

            region_backend_window_t window{ "view_region_backend", extent_t{ 80, 40 } };
            // The root paints nothing: the two clears below and the single fill are the whole frame,
            // so the draw count says exactly how many passes it took.
            auto* const root = new widget_t;
            root->set_geometry(rect_t{ 0, 0, 80, 40 });
            auto* const fill = new solid_widget_t{ color_t::red_color(), root };
            fill->set_geometry(rect_t{ 0, 0, 60, 30 });
            // One priming frame first: a frame paints the whole window until the persistent surface
            // exists at the current size, so the measured frame below is the one that can be partial.
            window.host.set_root_widget(root);
            window.prime(region_t{ rect_t{ 0, 0, 80, 40 } });
            CHECK(window.painted_frame_count() == 1);
            CHECK(window.pending_damage().is_empty());
            root->clear_damage();

            region_t damage;
            damage.unite(rect_t{ 3, 3, 6, 6 });
            damage.unite(rect_t{ 50, 3, 6, 6 });
            CHECK(damage.rectangle_count() == 2);

            // Driven through the frame's own entry point: the pass policy under test is the frame's.
            window.paint_with_recorded_engine(damage);
            // The backend was handed the region once, with both rectangles, and everything the frame
            // painted happened in that one pass: two clears plus the child's fill.
            CHECK(window.engine.region_clips == 1);
            std::printf("[view] region backend: %d region clip(s) of %d rectangle(s), %d draw(s)\n",
                        window.engine.region_clips, window.engine.region_rects, window.engine.draws);
            CHECK(window.engine.region_rects == 2);
            CHECK(window.engine.draws == 3);
        }
    }

    // -----------------------------------------------------------------------
    // (5) The damage record belongs to the tree; an empty damage paints nothing; every item receives
    //     its own area as paint_event_t::damage().
    // -----------------------------------------------------------------------
    {
        counted_widget_t root{ color_t::white_color() };
        root.set_geometry(rect_t{ 0, 0, 60, 40 });
        root.clear_damage();

        image_t device{ extent_t{ 60, 40 } };

        // A colour the tree never paints proves painting with an empty damage does nothing at all.
        {
            painter_t painter{ device };
            CHECK(painter.begin());
            painter.clear(color_t::magenta_color());
            CHECK(painter.end());
        }
        CHECK(root.damage().is_empty());
        render_frame(device, root, region_t{});
        CHECK(root.paints() == 0);
        CHECK((device.pixel(30, 20) == color_t::magenta_color()));

        // update() records the damage on the TREE: it is the item's record, not an output's flag.
        const rect_t reported{ 10, 5, 20, 12 };
        root.update(reported);
        CHECK(!root.damage().is_empty());
        CHECK(region_t{ reported }.subtracted(root.damage()).is_empty());
        CHECK(root.damage().rectangle_count() == 1);

        // ... but an explicitly empty damage still paints nothing, even though the tree has damage.
        render_frame(device, root, region_t{});
        CHECK(root.paints() == 0);
        CHECK((device.pixel(30, 20) == color_t::magenta_color()));
        CHECK(!root.damage().is_empty()); // nothing consumed the record

        // Painting the tree's own damage paints once, hands the item its local damage and consumes
        // the record for every output.
        render_frame(device, root, root.damage());
        CHECK(root.paints() == 1);
        CHECK((device.pixel(15, 10) == color_t::white_color()));    // inside the damage: painted
        CHECK((device.pixel(30, 20) == color_t::magenta_color()));  // outside it: untouched
        CHECK(root.damage().is_empty());
        CHECK(region_t{ reported }.subtracted(root.last_damage()).is_empty());
        CHECK(root.last_damage().rectangle_count() == 1);

        // A second output sees the consumed record: it belongs to the tree, not to one output.
        image_t other_device{ extent_t{ 60, 40 } };
        render_frame(other_device, root, root.damage());
        CHECK(root.paints() == 1); // nothing was damaged any more
        CHECK(root.damage().is_empty());
    }

    // -----------------------------------------------------------------------
    // (5b) paint_event_t::damage() is the item's own area: translated into its coordinates and
    //      intersected with its rectangle, and an opaque child hides the damage under it.
    // -----------------------------------------------------------------------
    {
        // The root paints nothing; two overlapping children, the upper one opaque.
        widget_t root;
        root.set_geometry(rect_t{ 0, 0, 60, 40 });
        auto* const below = new counted_widget_t{ color_t::red_color(), &root };
        below->set_geometry(rect_t{ 20, 10, 30, 20 });
        auto* const above = new counted_widget_t{ color_t::blue_color(), &root };
        above->set_geometry(rect_t{ 20, 10, 30, 20 });
        above->set_opaque(true);

        image_t device{ extent_t{ 60, 40 } };
        root.clear_damage();
        // Damage the child's rectangle only: the item-local damage is that rectangle moved into the
        // child's coordinates, so a pass that forgot the translation would report the parent's.
        root.update(rect_t{ 20, 10, 30, 20 });
        render_frame(device, root, root.damage());
        CHECK(below->paints() == 0); // fully covered by the opaque sibling: not visited at all
        CHECK(above->paints() == 1);
        CHECK(below->last_damage().is_empty());
        CHECK(region_t{ rect_t{ 0, 0, 30, 20 } }.subtracted(above->last_damage()).is_empty());
        CHECK(above->last_damage().rectangle_count() == 1);
        CHECK((device.pixel(30, 20) == color_t::blue_color())); // the opaque child won the pixels

        // A partially covered sibling is still painted, over exactly the exposed part.
        above->set_geometry(rect_t{ 20, 10, 10, 10 });
        root.clear_damage();
        root.update(rect_t{ 20, 10, 30, 20 });
        render_frame(device, root, root.damage());
        CHECK(below->paints() == 2); // one pass per exposed rectangle, both of them
        CHECK(above->paints() == 2);
        CHECK(region_t{ rect_t{ 0, 0, 10, 10 } }.subtracted(above->last_damage()).is_empty());
        // The lower child is left with the L the opaque corner did not cover: the right column and
        // the bottom strip, in its own coordinates.
        CHECK(region_t{ rect_t{ 10, 0, 20, 10 } }.subtracted(below->last_damage()).is_empty());
        CHECK(region_t{ rect_t{ 0, 10, 30, 10 } }.subtracted(below->last_damage()).is_empty());
        CHECK(below->last_damage().rectangle_count() == 2);
    }

    // -----------------------------------------------------------------------
    // (6) The window's scroll sink: the window shifts and answers a band, the tree damages exactly it.
    // -----------------------------------------------------------------------
    {
        (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        core_application application{ argc, argv };
        {
            gui_application_t gui;
            CHECK(gui_application_t::is_video_initialized());

            widget_window_t window{ "view_window_scroll_sink", extent_t{ 80, 60 } };
            auto* const root = new solid_widget_t{ color_t::white_color() };
            auto* const scroller = new scroll_view_t{ root };
            scroller->set_geometry(rect_t{ 0, 0, 80, 60 });
            auto* const content = new solid_widget_t{ color_t::red_color() };
            content->set_fixed_size(extent_t{ 80, 300 });
            scroller->set_content(content);
            window.set_root_widget(root);
            window.repaint();
            CHECK(window.pending_damage().is_empty());
            root->clear_damage();

            // The viewport is the area minus the visible bars (the rule the area uses internally), so
            // the comparison below is in the same terms as the damage.
            const rect_t area = scroller->rect();
            const int viewport_width = area.width()
                - (scroller->vertical_scroll_bar()->is_visible()
                        ? scroller->vertical_scroll_bar()->rect().width()
                        : 0);
            const int viewport_height = area.height()
                - (scroller->horizontal_scroll_bar()->is_visible()
                        ? scroller->horizontal_scroll_bar()->rect().height()
                        : 0);
            CHECK(viewport_width > 0);
            CHECK(viewport_height > 0);

            scroller->set_scroll_offset(point_t{ 0, 16 }); // one step down: the content moves up
            // The window answered a band and the tree records exactly it. The answer also covers the
            // scroll bar state the shift changed, so the band is a subset, never an equality.
            CHECK(!window.pending_damage().is_empty());
            CHECK(window.pending_damage().area() > 0);
            CHECK(window.pending_damage().bounds().bottom() <= area.bottom());
            CHECK(root->damage().area()
                  < static_cast<std::size_t>(viewport_width * viewport_height));
            // One region, two owners: the tree's record and the window's answer are the same region.
            CHECK(root->damage().subtracted(window.pending_damage()).is_empty());
            CHECK(window.pending_damage().subtracted(root->damage()).is_empty());
            std::printf("[view] scroll sink: window answered %zu px, tree damage %zu px of a %d px "
                        "viewport\n",
                        window.pending_damage().area(), root->damage().area(),
                        viewport_width * viewport_height);
            window.repaint();
            CHECK(window.pending_damage().is_empty());
        }
    }

    // -----------------------------------------------------------------------
    // (7) The public widget_window_t API still behaves as before the split.
    // -----------------------------------------------------------------------
    {
        (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        core_application application{ argc, argv };
        {
            gui_application_t gui;
            CHECK(gui_application_t::is_video_initialized());

            widget_window_t window{ "view_window_api", extent_t{ 80, 60 } };
            auto* const root = new counted_widget_t{ color_t::white_color() };
            window.set_root_widget(root);
            CHECK(window.root_widget() == root);
            CHECK(window.router() != nullptr);
            CHECK(!window.pending_damage().is_empty()); // set_root_widget() laid the root out

            window.repaint();
            CHECK(window.pending_damage().is_empty());
            CHECK(root->paints() == 1);
            const std::size_t frames_after_first = window.painted_frame_count();
            CHECK(frames_after_first >= 1);

            // The window's repaint sink still mirrors the root damage into the window.
            root->update();
            CHECK(!window.pending_damage().is_empty());
            CHECK(window.pending_damage().area()
                  == static_cast<std::size_t>(root->rect().width() * root->rect().height()));
            window.repaint();
            CHECK(window.pending_damage().is_empty());
            CHECK(root->paints() == 2);
            CHECK(window.painted_frame_count() == frames_after_first + 1);

            // The router still owns the input session of this window.
            interaction_t move;
            move.mouse = point_t{ 10, 10 };
            (void)window.router()->dispatch(move);
            CHECK(root->is_hovered());
            window.repaint();
            CHECK(window.pending_damage().is_empty());

            // Adopting a new root and dropping the tree keep the documented facade behaviour.
            auto* const second = new counted_widget_t{ color_t::green_color() };
            window.set_root_widget(second);
            CHECK(window.root_widget() == second);
            CHECK(window.router() != nullptr);
            window.set_root_widget(nullptr);
            CHECK(window.root_widget() == nullptr);
            CHECK(window.router() == nullptr);
        }
    }

    // -----------------------------------------------------------------------
    // (8) A burst of scroll steps before one frame: the real chain must damage the accumulated band.
    // -----------------------------------------------------------------------
    {
        (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        core_application application{ argc, argv };
        {
            gui_application_t gui;
            CHECK(gui_application_t::is_video_initialized());

            widget_window_t window{ "view_window_scroll", extent_t{ 200, 180 } };
            auto* const root = new counted_widget_t{ color_t::white_color() };
            auto* const scroller = new scroll_view_t{ root };
            scroller->set_geometry(rect_t{ 0, 0, 200, 180 });
            auto* const content = new counted_widget_t{ color_t::red_color() };
            content->set_fixed_size(extent_t{ 200, 400 });
            scroller->set_content(content);
            window.set_root_widget(root);
            window.repaint();
            CHECK(window.pending_damage().is_empty());

            // The viewport is the area minus the two visible bars; the horizontal one is 14 px tall.
            const int viewport_height = 180 - 14;
            constexpr int step = 16;
            constexpr int steps = 3;
            // A wheel burst: every step lands before the frame paints.
            for (int count = 1; count <= steps; ++count) {
                scroller->set_scroll_offset(point_t{ 0, step * count });
            }
            const rect_t band{ 0, viewport_height - step * steps, 200 - 14, step * steps };
            std::printf("[view] burst: pending damage %zu px, the accumulated band is %d px\n",
                        window.pending_damage().area(),
                        band.width() * band.height());
            // One step alone would damage only the last strip; the burst has to cover the band the
            // other two steps dragged into the viewport as well.
            CHECK(region_t{ band }.subtracted(window.pending_damage()).is_empty());
            CHECK(window.pending_damage().area()
                  < static_cast<std::size_t>(200 * viewport_height)); // ... and not the viewport

            // One widget update between the burst and the frame publishes the TREE's record as the
            // window's pending damage, so the tree has to hold the same band the window accumulated: a
            // sink that recorded only the last step's strip would lose the rest of the band right here.
            scroller->vertical_scroll_bar()->update();
            std::printf("[view] burst: pending damage after a widget update %zu px\n",
                        window.pending_damage().area());
            CHECK(region_t{ band }.subtracted(window.pending_damage()).is_empty());
            window.repaint();
            CHECK(window.pending_damage().is_empty());
        }
    }

    // -----------------------------------------------------------------------
    // (9) A scroll record schedules a frame: the window is asked for a repaint even when no input step
    //     follows the shift.
    // -----------------------------------------------------------------------
    {
        (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        core_application application{ argc, argv };
        {
            gui_application_t gui;
            CHECK(gui_application_t::is_video_initialized());

            widget_window_t window{ "view_window_scroll_record", extent_t{ 80, 60 } };
            auto* const root = new scroll_probe_widget_t;
            window.set_root_widget(root);
            window.repaint(); // one frame, so the backing surface holds pixels to shift
            CHECK(window.pending_damage().is_empty());
            root->clear_damage();

            // One shift of the fast path with no input step at all: the sink publishes the record, so
            // the frame is scheduled by the shift itself.
            const rect_t area{ 0, 0, 80, 60 };
            root->request_scroll(area, 0, -16);
            const rect_t strip{ 0, 44, 80, 16 };
            std::printf("[view] scroll record: %zu px of pending damage for a 16 px band\n",
                        window.pending_damage().area());
            CHECK(!window.pending_damage().is_empty());
            // The published damage is exactly the band the window answered.
            CHECK(region_t{ strip }.subtracted(window.pending_damage()).is_empty());
            CHECK(window.pending_damage().subtracted(region_t{ strip }).is_empty());
            CHECK(!root->damage().is_empty());
            window.repaint();
            CHECK(window.pending_damage().is_empty());
        }
    }

    // -----------------------------------------------------------------------
    // (10) A scroll carries the damage with it: a record D inside the shifted rectangle becomes D',
    //      D' stays inside that rectangle, and the window and the tree describe the same region.
    // -----------------------------------------------------------------------
    {
        (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        core_application application{ argc, argv };
        {
            gui_application_t gui;
            CHECK(gui_application_t::is_video_initialized());

            // -- (10a) the window side: the pending damage inside the shifted area moves with it. --
            {
                widget_window_t window{ "view_window_translate", extent_t{ 120, 80 } };
                const rect_t area{ 0, 0, 120, 80 };
                const rect_t reported{ 40, 40, 20, 20 };
                // One frame first: the shift is only worth attempting while a persistent surface
                // holds the pixels, and the surface is created by the first frame.
                window.window().set_pending_damage(region_t{ area });
                window.repaint();
                CHECK(window.pending_damage().is_empty());
                window.window().set_pending_damage(region_t{ reported });
                window.scroll(area, 0, -16);
                const region_t damage = window.pending_damage();
                const region_t translated{ rect_t{ 40, 24, 20, 20 } };
                const region_t band{ rect_t{ 0, 64, 120, 16 } };
                std::printf("[view] window scroll: a %zu px report became %zu px of damage for a "
                            "%zu px band\n",
                            region_t{ reported }.area(), damage.area(), band.area());
                CHECK(!damage.is_empty());
                // D moved with the pixels and was clipped back into the shifted rectangle ...
                CHECK(translated.subtracted(damage).is_empty());
                CHECK(translated.subtracted(region_t{ area }).is_empty());
                // ... the place it left is not claimed any more ...
                CHECK(!region_t{ reported }.subtracted(damage).is_empty());
                // ... and the exposed band is united AFTER the move, not moved with it.
                CHECK(band.subtracted(damage).is_empty());
                CHECK(damage.area() == translated.area() + band.area());
            }

            // -- (10b) the tree side moves the same way, and the window publishes exactly it. --
            {
                widget_window_t window{ "view_window_translate_tree", extent_t{ 200, 180 } };
                auto* const root = new counted_widget_t{ color_t::white_color() };
                auto* const scroller = new scroll_view_t{ root };
                scroller->set_geometry(rect_t{ 0, 0, 200, 180 });
                auto* const content = new counted_widget_t{ color_t::red_color() };
                content->set_fixed_size(extent_t{ 200, 400 });
                scroller->set_content(content);
                window.set_root_widget(root);
                window.repaint();
                CHECK(window.pending_damage().is_empty());

                // The viewport is the area minus the two 14-pixel bars this content makes necessary.
                const rect_t viewport{ 0, 0, 200 - 14, 180 - 14 };
                const rect_t reported{ 40, 40, 20, 20 };
                root->update(reported); // records the damage AND publishes it through the sink
                CHECK(region_t{ reported }.subtracted(window.pending_damage()).is_empty());

                scroller->set_scroll_offset(point_t{ 0, 40 }); // one fast step: 40 px < 166 px viewport
                CHECK(scroller->scroll_offset().y() == 40);

                const region_t translated{ rect_t{ 40, 0, 20, 20 } }; // D moved by the shift
                const region_t band{ rect_t{ 0, 166 - 40, 186, 40 } }; // the band it exposed
                std::printf("[view] tree scroll: %zu px of damage for a %zu px report\n",
                            root->damage().area(), translated.area());
                CHECK(translated.subtracted(root->damage()).is_empty());
                CHECK(band.subtracted(root->damage()).is_empty());
                CHECK(translated.subtracted(region_t{ viewport }).is_empty());
                CHECK(!region_t{ reported }.subtracted(root->damage()).is_empty());
                // One region, two owners: the tree's record was published as the window's damage.
                CHECK(root->damage().subtracted(window.pending_damage()).is_empty());
                CHECK(window.pending_damage().subtracted(root->damage()).is_empty());
                window.repaint();
                CHECK(window.pending_damage().is_empty());
            }
        }
    }

    if (g_failures == 0) {
        std::printf("view: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("view: %d checks, %d failure(s)\n", g_checks, g_failures);
    return 1;
}
