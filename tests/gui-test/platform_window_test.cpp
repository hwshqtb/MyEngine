// platform_window_t: the window policy is platform-independent.
//
// This test creates NO window and initialises no video subsystem: it injects a fake platform_window_t
// into a window_t and drives the public frame path (show / repaint / set_pending_damage / scroll /
// capture). What it pins is exactly the half of window_t that must not depend on a platform:
//  - the store's surface is resized and made the frame target every frame, and the frame is closed and
//    flushed (whole client area) afterwards — the flip-model "partial paint, whole presentation";
//  - one clipped pass per damaged rectangle, or ONE clipped pass for the whole region on a backend
//    that declares engine_feature_flag::CLIP_REGION;
//  - a frame with no damage draws nothing (no clear, no paint_event(), no flush, no present);
//  - scroll() accumulates the shifts since the last painted frame and damages the band of the
//    ACCUMULATED shift, and a refused shift degrades to the whole rectangle;
//  - without a store (the driver refused it) the frame covers the whole client area and presents the
//    window target;
//  - one input step (interaction_t) maintains input_state and reaches the interaction hook.
//
// The fake engine records what the backend was asked to do, and the fake store is a real
// backing_store_t over an offscreen surface + software renderer (the same trick image_t uses), so no
// window, no driver and no display are involved.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "hwshqtb/gui/backing_store_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/input_state_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/paint_device_t.h"
#include "hwshqtb/gui/paint_engine_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/gui/platform_window_t.h"
#include "hwshqtb/gui/region_t.h"
#include "hwshqtb/gui/window_t.h"
#include "hwshqtb/metaobject/event_dispatcher.h"

using namespace hwshqtb::gui;

namespace platform_window_test {

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

/// The interface contract: the platform half is an abstract, non-copyable interface with a protected
/// constructor, and a window can be built from one.
static_assert(std::is_abstract_v<platform_window_t>);
static_assert(std::has_virtual_destructor_v<platform_window_t>);
static_assert(!std::is_copy_constructible_v<platform_window_t>);
static_assert(!std::is_copy_assignable_v<platform_window_t>);
static_assert(std::is_constructible_v<window_t, std::unique_ptr<platform_window_t>>);

/// A recording paint engine: it rasterises nothing and counts what the frame asked for.
class fake_engine_t final: public paint_engine_t {
public:
    /// @param region_capable Whether this backend declares engine_feature_flag::CLIP_REGION.
    explicit fake_engine_t(bool region_capable):
        _region_capable(region_capable) { }

    engine_feature_t features() const noexcept override {
        engine_feature_t flags{ engine_feature_flag::CLIP };
        if (_region_capable) {
            flags |= engine_feature_flag::CLIP_REGION;
        }
        return flags;
    }

    bool begin(paint_device_t& device) override {
        ++begins;
        device_size = device.size();
        return begin_ok;
    }

    bool end() override {
        return true;
    }

    void update_state(const engine_state_t& state) override {
        if ((state.dirty_flags() & engine_state_t::dirty_clip) == 0) {
            return;
        }
        ++clip_updates;
        clip_enabled = state.clip_enabled();
        clip = state.clip();
        if (state.clip_region_enabled()) {
            ++region_clips;
            region_rects = static_cast<int>(state.clip_region().size());
        }
    }

    void draw_rects(const rect_t*, int count, bool fill) override {
        if (fill) {
            draws += count;
        }
        else {
            outlines += count;
        }
    }

    void draw_glyphs(const glyph_run_t&) override { }
    void draw_image(const rect_t&, const image_view_t&) override { }

    /// @brief Whether begin() accepts the next pass (the knob that forces a failed painter).
    bool begin_ok = true;
    /// @brief How many passes the frame started.
    int begins = 0;
    /// @brief How many clip updates the backend received.
    int clip_updates = 0;
    /// @brief How many of them carried a REGION (more than one rectangle).
    int region_clips = 0;
    /// @brief How many rectangles the last region clip carried.
    int region_rects = 0;
    /// @brief Filled rectangles the backend was asked to draw (clears included).
    int draws = 0;
    /// @brief Outlined rectangles the backend was asked to draw.
    int outlines = 0;
    /// @brief Whether the last clip update enabled clipping.
    bool clip_enabled = false;
    /// @brief The last clip rectangle the backend received.
    rect_t clip{};
    /// @brief The device extent the last pass was started on.
    extent_t device_size{};

private:
    /// @brief Whether this backend declares CLIP_REGION.
    bool _region_capable = false;
};

/// A platform half with no window system behind it: it records every call and answers from knobs.
///
/// With @c with_store it hands out a REAL backing_store_t over an offscreen surface and a software
/// renderer — the same construction image_t uses, so no window and no video subsystem is involved — and
/// the window then paints into that surface and flushes it. Without it, backing_store() answers nullptr
/// (the "the driver refused the surface" fallback) and the window paints straight into its window
/// target.
class fake_platform_window_t final: public platform_window_t {
public:
    /// @param size The client area; device, pixel and window coordinates all report it.
    /// @param region_capable Whether the engine declares engine_feature_flag::CLIP_REGION.
    /// @param with_store Whether backing_store() hands out a real store.
    fake_platform_window_t(extent_t size, bool region_capable, bool with_store = true):
        engine(region_capable),
        _size(size) {
        if (!with_store || size.isEmpty()) {
            return;
        }
        _target = SDL_CreateSurface(size.width(), size.height(), SDL_PIXELFORMAT_ARGB8888);
        if (_target == nullptr) {
            return;
        }
        _renderer = SDL_CreateSoftwareRenderer(_target);
        if (_renderer == nullptr) {
            return;
        }
        _store = std::make_unique<backing_store_t>(_renderer);
    }

    ~fake_platform_window_t() override {
        // The store was created from the renderer, and the renderer draws into the surface: reverse
        // order, exactly like the real platform's destructor.
        _store.reset();
        if (_renderer != nullptr) {
            SDL_DestroyRenderer(_renderer);
            _renderer = nullptr;
        }
        if (_target != nullptr) {
            SDL_DestroySurface(_target);
            _target = nullptr;
        }
    }

    /// @brief Whether the real store could be created on this machine.
    bool store_available() const noexcept {
        return _store != nullptr;
    }

    void show() override {
        visible = true;
        ++shows;
    }

    void hide() override {
        visible = false;
        ++hides;
    }

    bool is_visible() const noexcept override {
        return visible;
    }

    void set_title(std::string_view new_title) override {
        title = std::string(new_title);
    }

    /// @brief Records the floor the host asked for (see window_t::set_minimum_size()).
    void set_minimum_size(::hwshqtb::gui::extent_t size) noexcept override {
        minimum_size = size;
        ++minimum_size_calls;
    }

    std::uint32_t native_id() const noexcept override {
        return 0x1234u;
    }

    extent_t device_size() const noexcept override {
        return _size;
    }

    extent_t pixel_size() const noexcept override {
        return _size;
    }

    rect_t window_geometry() const noexcept override {
        return rect_t{ point_t{ 40, 50 }, _size };
    }

    paint_engine_t* paint_engine() noexcept override {
        return &engine;
    }

    backing_store_t* backing_store() noexcept override {
        return _store.get();
    }

    void present() override {
        ++presents;
    }

    void write_frame(std::string_view path) override {
        ++writes;
        last_write_path = std::string(path);
    }

    void set_text_input(bool enabled) noexcept override {
        ++text_input_changes;
        text_input = enabled;
    }

    void attach(window_t& new_owner) override {
        owner = &new_owner;
        ++attaches;
    }

    bool defer_repaint() noexcept override {
        return defer;
    }

    /// @brief The engine this platform hands out.
    fake_engine_t engine;
    /// @brief The window this platform reports to.
    window_t* owner = nullptr;
    /// @brief The window title the last set_title() asked for.
    std::string title;
    /// @brief The last minimum size the host asked for.
    ::hwshqtb::gui::extent_t minimum_size{};
    /// @brief How many times set_minimum_size() ran.
    int minimum_size_calls = 0;
    /// @brief The path of the last write_frame() call.
    std::string last_write_path;
    /// @brief Whether the platform owns the frame schedule (defer_repaint()).
    bool defer = false;
    /// @brief Whether the window is shown.
    bool visible = false;
    /// @brief The platform text input state.
    bool text_input = false;
    /// @brief How many times each platform entry was called.
    int shows = 0;
    int hides = 0;
    int attaches = 0;
    int presents = 0;
    int writes = 0;
    int text_input_changes = 0;

private:
    /// @brief The client area every extent query reports.
    extent_t _size;
    /// @brief The offscreen pixels the software renderer draws into (the "window target").
    SDL_Surface* _target = nullptr;
    /// @brief The software renderer over @c _target (the real platform's renderer stand-in).
    SDL_Renderer* _renderer = nullptr;
    /// @brief The store handed out; null when @c _renderer could not be created or with_store was false.
    std::unique_ptr<backing_store_t> _store;
};

/// A window that records the frame passes and the interaction steps it is handed.
class probe_window_t final: public window_t {
public:
    explicit probe_window_t(std::unique_ptr<platform_window_t> platform):
        window_t(std::move(platform)) { }

    /// @brief The clip rectangle of each paint pass, in the order the frame made them.
    std::vector<rect_t> passes;
    /// @brief The clip region of each paint pass (one rectangle for a rectangle clip).
    std::vector<region_t> region_passes;
    /// @brief The interaction steps the reports produced.
    std::vector<interaction_t> steps;
    /// @brief How many times paint_event() ran.
    int paint_calls = 0;
    /// @brief How many times the focus-loss hook ran.
    int focus_lost_calls = 0;
    /// @brief How many times the pointer-leave hook (leave_event) ran.
    int leave_calls = 0;

protected:
    void paint_event(paint_event_t* event) override {
        painter_t& painter = event->painter();
        ++paint_calls;
        passes.push_back(painter.clip_rect());
        region_passes.push_back(painter.clip_region());
        // One fill per pass: the backend's draw count then says how many passes ran.
        painter.set_color(color_t::black_color());
        painter.fill_rect(rect_t{ 0, 0, 2, 2 });
    }

    void interact_event(interact_event_t* event) override {
        steps.push_back(event->step());
    }

    void focus_out_event(focus_out_event_t*) override {
        ++focus_lost_calls;
    }

    void leave_event(leave_event_t*) override {
        ++leave_calls;
    }
};

}  // namespace platform_window_test

using namespace platform_window_test;

int main() {
    std::printf("== platform_window_t: the window policy with no window system ==\n");

    // ---- (1) construction: the injected platform is attached, and the window is its owner -------------
    auto platform = std::make_unique<fake_platform_window_t>(extent_t{ 100, 60 }, false);
    auto* const fake = platform.get();
    probe_window_t window{ std::move(platform) };
    CHECK(fake->attaches == 1);
    CHECK(fake->owner == &window);
    CHECK((fake->engine.features() & engine_feature_flag::CLIP_REGION) == engine_feature_flag::NONE);
    CHECK(window.window_id() == 0x1234u);
    CHECK((window.size() == extent_t{ 100, 60 }));
    CHECK(window.paint_engine() == &fake->engine);
    CHECK(!window.is_visible());
    CHECK(window.painted_frame_count() == 0);
    if (!fake->store_available()) {
        std::printf("platform_window: the offscreen renderer is unavailable; the suite cannot run\n");
        return 1;
    }

    // ---- (2) show() paints the first frame synchronously into the store and presents all of it ------
    window.show();
    CHECK(fake->shows == 1);
    CHECK(window.is_visible());
    CHECK(window.painted_frame_count() == 1);
    // The frame went into the store's surface: the store flushes it (and presents) itself, so the
    // window target is never presented separately.
    CHECK(fake->presents == 0);
    CHECK((fake->engine.device_size == extent_t{ 100, 60 }));
    CHECK(window.pending_damage().is_empty()); // the frame consumed what it painted
    CHECK((window.presented_region().bounds() == rect_t{ 0, 0, 100, 60 }));

    // ---- (3) a two-rectangle damage is TWO passes, each clipped to exactly one rectangle -------------
    region_t damage;
    damage.unite(rect_t{ 5, 5, 10, 10 });
    damage.unite(rect_t{ 40, 20, 10, 10 });
    CHECK(damage.rectangle_count() == 2);
    window.passes.clear();
    window.region_passes.clear();
    fake->engine.draws = 0;
    window.set_pending_damage(damage);
    window.repaint();
    CHECK(window.passes.size() == 2);
    CHECK((window.passes.at(0) == rect_t{ 5, 5, 10, 10 }));
    CHECK((window.passes.at(1) == rect_t{ 40, 20, 10, 10 }));
    CHECK(window.region_passes.at(0).rectangle_count() == 1);
    CHECK(fake->engine.region_clips == 0); // this backend cannot clip to a region
    CHECK(fake->engine.draws == 4);        // one clear + one fill per pass
    // The presentation still covers the whole client area, not the two painted rectangles.
    CHECK((window.presented_region().bounds() == rect_t{ 0, 0, 100, 60 }));
    std::printf("[platform_window] per-rectangle backend: %zu pass(es), %d draw(s), presented %s\n",
                window.passes.size(), fake->engine.draws, "the whole client area");

    // ---- (4) a region-capable backend paints the whole damage in ONE pass ----------------------------
    auto region_platform = std::make_unique<fake_platform_window_t>(extent_t{ 100, 60 }, true);
    auto* const region_fake = region_platform.get();
    probe_window_t region_window{ std::move(region_platform) };
    CHECK((region_fake->engine.features() & engine_feature_flag::CLIP_REGION) != engine_feature_flag::NONE);
    region_window.show();
    region_window.passes.clear();
    region_window.region_passes.clear();
    region_fake->engine.draws = 0;
    region_window.set_pending_damage(damage);
    region_window.repaint();
    CHECK(region_window.passes.size() == 1);
    CHECK(region_window.region_passes.at(0).rectangle_count() == 2);
    CHECK(region_fake->engine.region_clips == 1);
    CHECK(region_fake->engine.region_rects == 2);
    CHECK(region_fake->engine.draws == 3); // two clears + the single fill of the pass
    CHECK(region_window.painted_frame_count() == 2); // the frame counts ONCE, not once per rectangle
    std::printf("[platform_window] region backend: %zu pass(es) of %d rectangle(s), %d draw(s)\n",
                region_window.passes.size(), region_fake->engine.region_rects,
                region_fake->engine.draws);

    // ---- (5) a frame with no damage paints nothing at all -------------------------------------------
    const std::size_t frames_before = window.painted_frame_count();
    const int draws_before = fake->engine.draws;
    const int begins_before = fake->engine.begins;
    window.paint_calls = 0;
    window.passes.clear();
    window.set_pending_damage(region_t{});
    window.repaint();
    CHECK(window.painted_frame_count() == frames_before);
    CHECK(window.paint_calls == 0);
    CHECK(fake->engine.begins == begins_before); // no frame was even started
    CHECK(fake->engine.draws == draws_before);   // no clear, no fill, no flush

    // ---- (6) capture: the pixel work is delegated, the frame bookkeeping stays here -------------------
    window.save_screenshot("unused-screenshot.bmp");
    CHECK(fake->writes == 1);
    CHECK(fake->last_write_path == "unused-screenshot.bmp");
    CHECK(window.painted_frame_count() == frames_before + 1);
    CHECK(window.pending_damage().is_empty());
    CHECK((window.presented_region().bounds() == rect_t{ 0, 0, 100, 60 })); // the capture is a full frame
    CHECK(fake->presents == 0); // the store's flush presented it

    window.save_painted_frame("unused-painted.bmp");
    CHECK(fake->writes == 2);
    CHECK(fake->last_write_path == "unused-painted.bmp");
    CHECK(window.painted_frame_count() == frames_before + 1); // it paints nothing
    CHECK(window.pending_damage().is_empty());

    window.present();
    CHECK(fake->presents == 1);

    // ---- (7) scroll(): the shift moves the store's pixels and damages the ACCUMULATED band -----------
    const region_t first = window.scroll(rect_t{ 0, 0, 100, 60 }, 0, -10);
    CHECK((first.bounds() == rect_t{ 0, 50, 100, 10 }));
    // The second step of the same frame uncovers the band of the SUM of both shifts.
    const region_t second = window.scroll(rect_t{ 0, 0, 100, 60 }, 0, -10);
    CHECK((second.bounds() == rect_t{ 0, 40, 100, 20 }));
    CHECK(second.subtracted(window.pending_damage()).is_empty());
    CHECK(window.pending_damage().subtracted(second).is_empty());
    // A shifted axis that reverses cannot be described by one band: the whole area is damaged.
    const region_t reversed = window.scroll(rect_t{ 0, 0, 100, 60 }, 0, 5);
    CHECK((reversed.bounds() == rect_t{ 0, 0, 100, 60 }));
    // A shift the STORE refuses (at least the extent: no old pixel stays inside the area) degrades to
    // the whole rectangle as well, and the damage stays inside the client area.
    window.set_pending_damage(region_t{});
    const region_t refused_shift = window.scroll(rect_t{ 0, 0, 100, 60 }, 0, -60);
    CHECK((refused_shift.bounds() == rect_t{ 0, 0, 100, 60 }));
    CHECK(refused_shift.subtracted(window.pending_damage()).is_empty());
    CHECK(window.pending_damage().subtracted(region_t{ rect_t{ 0, 0, 100, 60 } }).is_empty());
    std::printf("[platform_window] scroll: step %zu px, accumulated %zu px, reversed %s\n",
                first.area(), second.area(), "the whole area");
    window.set_pending_damage(region_t{});

    // ---- (8) without a store the frame is FULL and the window target is presented --------------------
    auto plain_platform = std::make_unique<fake_platform_window_t>(extent_t{ 80, 40 }, false, false);
    auto* const plain = plain_platform.get();
    CHECK(plain->backing_store() == nullptr);
    probe_window_t plain_window{ std::move(plain_platform) };
    plain_window.show();
    CHECK(plain->presents == 1); // no surface: present the window target
    CHECK((plain_window.presented_region().bounds() == rect_t{ 0, 0, 80, 40 }));
    plain_window.passes.clear();
    plain_window.set_pending_damage(region_t{ rect_t{ 1, 1, 2, 2 } });
    plain_window.repaint();
    CHECK(plain_window.passes.size() == 1);
    CHECK((plain_window.passes.at(0) == rect_t{ 0, 0, 80, 40 })); // the whole client area, not the damage
    // A refused shift degrades the same way: the whole rectangle, united into the pending damage.
    const region_t degraded = plain_window.scroll(rect_t{ 0, 0, 80, 40 }, 0, -10);
    CHECK((degraded.bounds() == rect_t{ 0, 0, 80, 40 }));
    CHECK(degraded.subtracted(plain_window.pending_damage()).is_empty());

    // ---- (9) a painter that cannot start paints nothing, still presents, and closes the store's pass --
    auto refusing_platform = std::make_unique<fake_platform_window_t>(extent_t{ 30, 20 }, false);
    auto* const refusing = refusing_platform.get();
    refusing->engine.begin_ok = false; // the store's pass opens, the painter refuses it
    probe_window_t refusing_window{ std::move(refusing_platform) };
    refusing_window.set_pending_damage(region_t{ rect_t{ 0, 0, 5, 5 } });
    refusing_window.repaint();
    CHECK(refusing_window.painted_frame_count() == 0);
    CHECK(refusing_window.paint_calls == 0);
    CHECK(refusing->presents == 1);
    // The pass was closed again (not left as the render target): a later frame paints and flushes.
    refusing->engine.begin_ok = true;
    refusing_window.set_pending_damage(region_t{ rect_t{ 0, 0, 30, 20 } });
    refusing_window.repaint();
    CHECK(refusing_window.painted_frame_count() == 1);
    CHECK(refusing_window.paint_calls == 1);
    CHECK(refusing->presents == 1); // the successful frame went through the store's flush

    // ---- (10) the platform may own the frame schedule (defer_repaint) --------------------------------
    auto scheduled_platform = std::make_unique<fake_platform_window_t>(extent_t{ 60, 40 }, false);
    auto* const scheduled_fake = scheduled_platform.get();
    probe_window_t scheduled_window{ std::move(scheduled_platform) };
    scheduled_window.set_pending_damage(region_t{ rect_t{ 0, 0, 10, 10 } });
    const std::size_t scheduled_before = scheduled_window.painted_frame_count();
    scheduled_fake->defer = true; // the platform's pump is blocked: update() hands the frame over
    scheduled_window.update();
    (void)hwshqtb::metaobject::event_dispatcher::instance().process_events();
    CHECK(scheduled_window.painted_frame_count() == scheduled_before);
    CHECK(!scheduled_window.pending_damage().is_empty());
    scheduled_fake->defer = false; // back to the event-loop-driven schedule
    scheduled_window.update();
    (void)hwshqtb::metaobject::event_dispatcher::instance().process_events();
    CHECK(scheduled_window.painted_frame_count() == scheduled_before + 1);
    CHECK(scheduled_window.pending_damage().is_empty());

    // ---- (11) ONE input entry: a step in window coordinates maintains the state and reaches the hook --
    window.steps.clear();
    interaction_t motion;
    motion.mouse = point_t{ 12, 7 }; // WINDOW coordinates, the platform's own space
    motion.buttons = mouse_button_flag::LEFT;
    motion.modifiers = modifier_bits(modifier_flag::SHIFT);
    window.handle_input(motion);
    CHECK((window.input_state().mouse_position() == point_t{ 12, 7 })); // density 1: the identity
    CHECK(window.input_state().down(mouse_button_flag::LEFT));
    CHECK(has_modifier(modifier_bits(window.input_state().modifiers()), modifier_flag::SHIFT));
    CHECK(window.steps.size() == 1);
    CHECK((window.steps.back().mouse == point_t{ 12, 7 }));
    CHECK(window.steps.back().mouse_down);
    CHECK(has_modifier(window.steps.back().modifiers, modifier_flag::SHIFT));

    // A press of a button the motion reports as held records no new edge, but still reaches the tree.
    interaction_t press;
    press.mouse = point_t{ 20, 30 };
    press.buttons = mouse_button_flag::LEFT;
    press.button = mouse_button_enum::LEFT;
    press.mouse_down = true;
    press.mouse_pressed = true;
    window.handle_input(press);
    CHECK(!window.input_state().pressed(mouse_button_flag::LEFT));
    CHECK(window.steps.size() == 2);
    CHECK(window.steps.back().mouse_pressed);
    CHECK(window.steps.back().button == mouse_button_enum::LEFT);

    interaction_t release;
    release.mouse = point_t{ 20, 30 };
    release.button = mouse_button_enum::LEFT;
    release.mouse_released = true;
    window.handle_input(release);
    CHECK(window.input_state().released(mouse_button_flag::LEFT));
    CHECK(!window.input_state().down(mouse_button_flag::LEFT));
    CHECK(window.steps.size() == 3);
    CHECK(window.steps.back().mouse_released);
    CHECK(!window.steps.back().mouse_down);

    window.handle_input(press);
    CHECK(window.input_state().pressed(mouse_button_flag::LEFT));
    CHECK(window.steps.size() == 4);
    CHECK(window.steps.back().mouse_down);

    // The input state records every button; the item tree routes the LEFT button only.
    interaction_t right;
    right.mouse = point_t{ 21, 31 };
    right.button = mouse_button_enum::RIGHT;
    right.mouse_pressed = true;
    window.handle_input(right);
    CHECK(window.input_state().down(mouse_button_flag::RIGHT));
    CHECK(window.steps.size() == 4);

    interaction_t wheel;
    wheel.mouse = point_t{ 20, 30 };
    wheel.buttons = mouse_button_flag::LEFT;
    wheel.wheel = point_t{ 0, 2 };
    window.handle_input(wheel);
    CHECK(window.steps.size() == 5);
    CHECK((window.steps.back().wheel == point_t{ 0, 2 }));
    CHECK((window.input_state().wheel_delta() == point_t{ 0, 0 })); // one step, one delivery

    interaction_t key_down;
    key_down.mouse = point_t{ 20, 30 };
    key_down.key = 65;
    key_down.key_pressed = true;
    key_down.modifiers = modifier_bits(modifier_flag::CONTROL);
    window.handle_input(key_down);
    CHECK(window.input_state().key_down(65));
    CHECK(window.steps.size() == 6);
    CHECK(window.steps.back().key == 65);
    CHECK(window.steps.back().key_pressed);

    interaction_t key_up;
    key_up.mouse = point_t{ 20, 30 };
    key_up.key = 65;
    key_up.key_released = true;
    window.handle_input(key_up);
    CHECK(!window.input_state().key_down(65));
    CHECK(window.steps.size() == 7);
    CHECK(window.steps.back().key_released);

    interaction_t text;
    text.mouse = point_t{ 20, 30 };
    text.text_input = "ab";
    window.handle_input(text);
    CHECK(window.steps.size() == 8);
    CHECK(window.steps.back().text_input == "ab");
    CHECK(window.input_state().text_input().empty()); // the step consumed the committed text

    // ---- (12) focus and pointer-session reports reach the hooks --------------------------------------
    interaction_t held;
    held.mouse = point_t{ 20, 30 };
    held.key = 66;
    held.key_pressed = true;
    window.handle_input(held);
    CHECK(window.input_state().key_down(66));
    window.handle_focus(true);
    CHECK(fake->text_input);
    window.handle_focus(false);
    CHECK(!fake->text_input);
    CHECK(!window.input_state().key_down(66)); // no key-up arrives without the focus
    CHECK(window.focus_lost_calls == 1);
    window.handle_pointer_leave();
    CHECK(window.leave_calls == 1);

    // ---- (13) title and visibility are the platform's, the flag is the window's -----------------------
    window.set_title("platform_window");
    CHECK(fake->title == "platform_window");
    // The floor reaches the platform half, which is what the window manager enforces.
    CHECK(fake->minimum_size_calls == 0);
    window.set_minimum_size(extent_t{ 240, 120 });
    CHECK(fake->minimum_size_calls == 1);
    CHECK(fake->minimum_size == (extent_t{ 240, 120 }));
    window.set_minimum_size(extent_t{ 0, 0 }); // zero means "no floor": still forwarded
    CHECK(fake->minimum_size_calls == 2);
    window.hide();
    CHECK(fake->hides == 1);
    CHECK(!window.is_visible());
    CHECK(!window.close_requested());

    // ---- (14) a null platform window is rejected up front ---------------------------------------------
    bool rejected = false;
    try {
        window_t broken{ std::unique_ptr<platform_window_t>{} };
    }
    catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);

    if (g_failures == 0) {
        std::printf("platform_window: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("platform_window: %d of %d checks failed\n", g_failures, g_checks);
    return 1;
}
