/// @file window_t.cpp
/// @brief window_t: the frame policy of a top-level window (frames and their passes, presentation,
///        scroll accumulation, input state, capture). Nothing here names a platform type: the native
///        window, the renderer and the event source live behind platform_window_t, and the persistent
///        surface is its backing_store_t.

#include "hwshqtb/gui/window_t.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "hwshqtb/gui/backing_store_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/gui/window_coordinate.h"
#include "hwshqtb/metaobject/core_application.h"
#include "hwshqtb/metaobject/event_dispatcher.h"
#include "hwshqtb/metaobject/event_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "platform_window_factory.h"

namespace hwshqtb::gui {
    namespace {
        /// @brief The key code of the escape key (27, the value every backend's key vocabulary uses).
        /// @note The public key_press_event(int) API stays platform-free; this is the one value the
        ///       default implementation needs, and it is stable across backends.
        constexpr int escape_key = 0x1B;

        /// @brief The bit of one button identity.
        /// @param button The button identity a step's edge is about.
        /// @return The matching bit of mouse_button_flag (the two vocabularies share their values).
        /// @note An unknown identity (0) answers mouse_button_flag::NONE, which input_state_t ignores:
        ///       the level and the edges then change nothing.
        mouse_button_flag button_flag_of(mouse_button_enum button) noexcept {
            return static_cast<mouse_button_flag>(static_cast<std::uint8_t>(button));
        }

        /// @brief Whether @p step is a pointer-motion step: it carries no edge of any kind.
        /// @param step The step a platform reported.
        /// @return true when the step only moves the pointer, which is what makes its buttons field a
        ///         LEVEL the window must install.
        bool is_motion_step(const interaction_t& step) noexcept {
            return !step.mouse_pressed && !step.mouse_released && step.key == 0 && step.text_input.empty()
                && step.wheel == point_t{};
        }
    }

    window_t::window_t(std::string_view title, extent_t size) {
        // The one place a platform window is created: every platform has exactly one implementation,
        // and it is chosen there. The factory attaches it to this window before it returns.
        _platform = detail::create_platform_window(*this, title, size);
        // Seed the remembered geometry before the platform can report a change against it.
        seed_geometry();
    }

    window_t::window_t(std::unique_ptr<platform_window_t> window) {
        if (window == nullptr) {
            throw std::invalid_argument("window_t: the platform window must not be null");
        }
        _platform = std::move(window);
        // An injected platform is wired up here exactly like the factory-created one: attach() sets the
        // report target and installs the native event filter.
        _platform->attach(*this);
        seed_geometry();
    }

    window_t::~window_t() {
        // The platform goes first: its destructor removes the native filter, so no report can reach a
        // half-destroyed window. ~object_t then purges the requests still queued for this window.
        _platform.reset();
    }

    void window_t::show() {
        _platform->show();
        // A shown window may have moved and been resized by the platform, and it may already own the
        // keyboard focus: the platform follows the real focus, and the geometry is re-seeded here so a
        // queued report of the new size is not mistaken for a change.
        seed_geometry();
        // First paint, synchronously: the window never appears empty while waiting for the loop. It is
        // a FULL frame — a fresh surface has no contents to preserve.
        damage_all();
        repaint();
    }

    void window_t::hide() {
        // A hidden window cannot own the keyboard, so it must not keep text input (an input method or a
        // screen keyboard) alive. The platform stops it after a successful hide, which leaves the state
        // untouched when the hide failed and the window is still visible.
        _platform->hide();
    }

    void window_t::update() {
        if (_platform->defer_repaint()) {
            // The platform's pump is blocked (a modal drag/resize loop): a posted PAINT would wait
            // until it ends. The platform has marked the frame dirty and its own timer paints it.
            return;
        }
        if (_paint_pending) {
            return; // a request is already on its way: Qt update() coalesces the same way
        }
        _paint_pending = true;
        ::hwshqtb::metaobject::event_dispatcher::instance().post_event(
            this, new ::hwshqtb::metaobject::event_t{ ::hwshqtb::metaobject::event_type_enum::PAINT });
    }

    void window_t::repaint() {
        _paint_pending = false;
        paint_frame(true); // draws, flushes the store to the window and presents
    }

    void window_t::close() {
        if (_close_requested) {
            return;
        }
        _close_requested = true;
        _platform->set_text_input(false); // a closing window cannot own the keyboard any more
        try {
            _platform->hide();
        }
        catch (const std::runtime_error&) {
            // A window that is already going away may refuse to hide: this path never reports it.
        }
        // A single-window application: closing the window ends the main loop. A multi-window stage
        // moves the quit-on-last-window-closed policy into the application object.
        ::hwshqtb::metaobject::core_application::quit();
    }

    bool window_t::is_visible() const noexcept {
        return _platform->is_visible();
    }

    bool window_t::close_requested() const noexcept {
        return _close_requested;
    }

    std::uint32_t window_t::window_id() const noexcept {
        return _platform->native_id();
    }

    void window_t::set_title(std::string_view title) {
        _platform->set_title(title);
    }

    void window_t::set_minimum_size(extent_t size) noexcept {
        _platform->set_minimum_size(size);
    }

    extent_t window_t::size() const noexcept {
        return _platform->device_size();
    }

    extent_t window_t::window_coordinate_size() const noexcept {
        const rect_t geometry = _platform->window_geometry();
        // An EMPTY geometry means the platform's window is not on any screen: it has no client area in
        // window coordinates either, and an empty extent is exactly what window_to_pixel_point() turns
        // into the identity. Returning the rectangle's -1 x -1 directly would work by accident (a
        // non-positive side is empty too); normalising to an empty extent makes it work on purpose.
        if (geometry.isEmpty()) {
            return extent_t{};
        }
        return extent_t{ geometry.width(), geometry.height() };
    }

    extent_t window_t::pixel_size() const noexcept {
        return _platform->pixel_size();
    }

    input_state_t& window_t::input_state() noexcept {
        return _input;
    }

    const input_state_t& window_t::input_state() const noexcept {
        return _input;
    }

    paint_engine_t* window_t::paint_engine() noexcept {
        return _platform->paint_engine();
    }

    backing_store_t* window_t::surface_store() const noexcept {
        return _platform->backing_store();
    }

    void window_t::present() {
        // A minimized or hidden window may refuse to present; the next frame tries again.
        _platform->present();
    }

    const region_t& window_t::pending_damage() const noexcept {
        return _pending_damage;
    }

    const region_t& window_t::presented_region() const noexcept {
        return _presented;
    }

    void window_t::set_pending_damage(const region_t& damage) {
        _pending_damage = damage;
    }

    std::size_t window_t::painted_frame_count() const noexcept {
        return _painted_frames;
    }

    region_t window_t::scroll(rect_t rect, int dx, int dy) {
        const rect_t area = rect.intersected(client_area_pixels());
        if (area.isEmpty() || (dx == 0 && dy == 0)) {
            return region_t{};
        }
        // Find (or start) the record of this rectangle since the last painted frame. The damage of a
        // single step is not the damage of the frame when several steps arrive before it: the next
        // shift moves the band this step exposes further into the area, so the band grows by one shift
        // per step. The record keeps the accumulated shift, and the frame paints the band that the
        // accumulated shift exposes — the only band no pixel of the last painted frame can cover.
        scroll_record_t* record = nullptr;
        for (auto& candidate : _scrolls) {
            if (candidate.rect == area) {
                record = &candidate;
                break;
            }
        }
        if (record == nullptr) {
            // A rectangle that overlaps one already shifted in this frame moves those pixels too, so
            // only the whole of both rectangles is safe to repaint from now on.
            for (auto& candidate : _scrolls) {
                if (!candidate.rect.intersected(area).isEmpty()) {
                    candidate.whole = true;
                }
            }
            _scrolls.push_back(scroll_record_t{ area, dx, dy, false });
            record = &_scrolls.back();
        }
        else {
            const auto sign = [](int value) { return value > 0 ? 1 : -1; };
            if ((dx != 0 && record->dx != 0 && sign(dx) != sign(record->dx))
                || (dy != 0 && record->dy != 0 && sign(dy) != sign(record->dy))) {
                // A reversed axis: the intermediate bands may lie on either side, so fall back.
                record->whole = true;
            }
            record->dx += dx;
            record->dy += dy;
        }
        // The shift is only worth attempting while a persistent surface holds the pixels; without a
        // store (or inside a paint pass, where the surface is the current target) the whole area is
        // damaged and repainted normally.
        backing_store_t* const store = surface_store();
        const bool shifted = store != nullptr && store->scroll(area, dx, dy);
        if (!shifted) {
            record->whole = true; // the pixels did not move: nothing of the area can be reused
        }
        else {
            // The pixels inside the area moved, so the damage that describes them has to move with
            // them; what leaves the area is dropped. This runs BEFORE the new band is united: the band
            // is the damage the shift CREATED, and moving it again would shift it twice.
            _pending_damage = _pending_damage.translated_within(area, dx, dy);
        }
        const region_t exposed =
            record->whole ? region_t{ area } : scroll_exposed_region(area, record->dx, record->dy);
        _pending_damage.unite(exposed);
        update(); // schedule the frame that paints exactly the exposed band
        return exposed;
    }

    void window_t::save_screenshot(std::string_view path) {
        // Draw the frame and KEEP its target current: the store's surface can be read back directly,
        // and the window back buffer of the fallback path must be read before it is presented. The
        // capture is always a FULL frame: the file has to be the whole window, so whatever damage the
        // host reported is ignored and every rectangle of the client area is painted.
        const region_t whole = full_window_region();
        const bool into_backing = draw_frame(true, whole);
        backing_store_t* const store = surface_store();
        // The frame's target is still current here, so this reads exactly what was painted. The pass
        // (and the present) has to happen even when the read or the write fails, or the store's surface
        // would stay the render target for every later frame.
        const auto finish_frame = [&] {
            if (into_backing) {
                store->end_paint();
                store->flush(whole); // copies the whole surface to the window AND presents
            }
            else {
                _platform->present();
            }
        };
        try {
            _platform->write_frame(path);
        }
        catch (...) {
            finish_frame();
            throw;
        }
        finish_frame();
        _presented = whole;           // the capture presents the whole client area
        _pending_damage = region_t{}; // the frame consumed the reported damage
        _scrolls.clear();             // ... and every shift the records described
    }

    void window_t::save_painted_frame(std::string_view path) {
        // Read the store's surface, which holds the frame the last paint left there: paint_frame()
        // flushes the WHOLE surface to the window target on every painted frame, so the surface IS what
        // the window shows. No repaint happens here — a full repaint would repair the stale pixels a
        // diagnostic is looking for.
        // A defect of the presented frame must therefore be reproduced through THIS method:
        // save_screenshot() repaints first and fixes what is being reproduced.
        backing_store_t* const store = surface_store();
        const bool reading_surface = store != nullptr && store->begin_paint();
        try {
            _platform->write_frame(path);
        }
        catch (...) {
            if (reading_surface) {
                store->end_paint();
            }
            throw;
        }
        if (reading_surface) {
            store->end_paint();
        }
    }

    void window_t::repair_frame() {
        paint_frame(false); // draws into the surface, flushes it to the window and presents
    }

    void window_t::handle_resize(bool deliver_now) {
        const extent_t now = _platform->device_size();
        // A size already reported compares equal here and is skipped, so a receiver never sees the same
        // change twice (no duplicate layout) and a resize handled during a drag is not repeated when the
        // pump resumes.
        if (now == _last_size) {
            return;
        }
        const extent_t old = _last_size;
        _last_size = now;
        if (deliver_now) {
            // No event pump to dispatch through (a platform's modal loop): the hook runs directly, and
            // the repaint request goes through update(), which is what marks the platform's repair frame
            // dirty (defer_repaint()).
            resize_event_t event{ old, now };
            resize_event(&event);
            update();
            return;
        }
        ::hwshqtb::metaobject::event_dispatcher::instance().post_event(
            this, new resize_event_t{ old, now });
    }

    void window_t::handle_move(int global_x, int global_y) {
        const point_t new_position{ global_x, global_y };
        if (_has_position && new_position == _last_position) {
            return;
        }
        const bool was_placed = _has_position;
        const point_t old_position = _last_position;
        _last_position = new_position;
        _has_position = true;
        if (!was_placed) {
            // The window was not on any screen before (its geometry was empty), so it did not MOVE
            // here: it appeared here. A move event would have to name an old position, and there is
            // none — reporting one would be the fabrication the empty geometry exists to avoid.
            return;
        }
        ::hwshqtb::metaobject::event_dispatcher::instance().post_event(
            this, new move_event_t{ old_position, new_position });
    }

    void window_t::handle_expose() {
        // The surface may have lost its contents, and a platform's modal frame pump cannot wait for a
        // dispatch: the damage and the frame request happen HERE, the hook itself still runs from the
        // queued event (posted first, so it runs before the frame's PAINT).
        damage_all();
        ::hwshqtb::metaobject::event_dispatcher::instance().post_event(this, new expose_event_t{});
        update();
    }

    void window_t::handle_close() {
        close_event_t event;
        close_event(&event);
    }

    void window_t::handle_focus(bool gained) {
        if (gained) {
            // The window now owns the keyboard: platform text input has to be started for committed text
            // to be produced at all (backends leave it off by default).
            _platform->set_text_input(true);
            return;
        }
        // The keyboard focus left this window: stop the platform text input (and with it any input
        // method or screen keyboard it started), then drop the held-key set. No key-up arrives while
        // another window owns the keyboard, so that set is stale after a focus loss; the button level is
        // not (the pointer reports keep coming).
        _platform->set_text_input(false);
        _input.clear_keys();
        focus_out_event_t event;
        focus_out_event(&event);
    }

    void window_t::handle_input(const interaction_t& step) {
        // The pointer arrives in WINDOW coordinates and every rectangle it is compared with is in the
        // pixel space: this is the one crossing (pixel_point_of()). The step is then rebuilt in the
        // pixel space, so the hook and the host never see the platform's units.
        interaction_t converted = step;
        const point_t position = pixel_point_of(step.mouse);
        converted.mouse = position;

        // The state first: the step's payload is applied to this window's input_state, one kind per
        // step (see handle_input()'s contract). A step may only carry one edge kind, so the branches
        // below are exclusive.
        _input.set_mouse_position(position);
        _input.set_modifiers(modifier_t{ static_cast<modifier_flag>(step.modifiers) });
        if (!(step.wheel == point_t{})) {
            _input.add_wheel(step.wheel);
        }
        if (!step.text_input.empty()) {
            _input.add_text_input(step.text_input);
        }
        if (step.key != 0 && step.key_pressed) {
            _input.add_key(step.key);
            // The window-level hook runs before the step reaches a host, exactly like the old native
            // filter did: a window that closes on ESC closes in the same order.
            (void)key_press_event(step.key);
        }
        else if (step.key != 0 && step.key_released) {
            _input.remove_key(step.key);
        }
        bool routes_to_tree = true;
        if (step.mouse_pressed) {
            _input.set_button(button_flag_of(step.button), true);
            // The item tree routes the LEFT button only; the input state above recorded every button.
            routes_to_tree = step.button == mouse_button_enum::LEFT;
        }
        else if (step.mouse_released) {
            _input.set_button(button_flag_of(step.button), false);
            routes_to_tree = step.button == mouse_button_enum::LEFT;
        }
        else if (is_motion_step(step)) {
            // A motion report carries the LEVEL: a release the window never saw (the button came up
            // over another window) is corrected by the next move.
            _input.set_buttons(step.buttons);
        }
        // The level the hook reads is the one this window just derived, not the platform's word for it.
        converted.mouse_down = _input.down(mouse_button_flag::LEFT);
        if (routes_to_tree) {
            interact_event_t event{ converted };
            interact_event(&event);
        }
        // The wheel ticks and the committed text belong to the step that carried them: after it was
        // delivered the accumulator is cleared, so a later step cannot see the same payload again.
        _input.reset_wheel();
        _input.reset_text_input();
        // An input step is also a repaint request (hover/press feedback). update() coalesces, so a burst
        // of reports still paints once.
        update();
    }

    void window_t::handle_pointer_leave() {
        // The pointer left the client area: the same end of the same session as a focus loss, so the
        // hook runs and a host can drop its pointer state.
        leave_event_t event;
        leave_event(&event);
    }

    bool window_t::event(::hwshqtb::metaobject::event_t* event) {
        if (event == nullptr) {
            return false;
        }
        switch (event->type()) {
        case ::hwshqtb::metaobject::event_type_enum::PAINT:
            paint_if_pending();
            return true;
        case ::hwshqtb::metaobject::event_type_enum::RESIZE:
            resize_event(static_cast<resize_event_t*>(event));
            // The client area changed: every pixel of it is stale, so the frame is full even when the
            // widget layer only damaged the rectangles it re-laid out.
            damage_all();
            update();
            return true;
        case ::hwshqtb::metaobject::event_type_enum::MOVE:
            move_event(static_cast<move_event_t*>(event));
            return true;
        case ::hwshqtb::metaobject::event_type_enum::EXPOSE:
            // The damage and the frame request were applied when the platform reported the exposure;
            // this dispatch only runs the client hook.
            expose_event(static_cast<expose_event_t*>(event));
            return true;
        default:
            // Not one of the queued window types: the ordinary object behaviour (a timer, a deferred
            // delete, a filtered event) is the base class's.
            return ::hwshqtb::metaobject::object_t::event(event);
        }
    }

    META_OBJECT(window_t);

    void window_t::paint_if_pending() {
        if (!_paint_pending) {
            return;
        }
        _paint_pending = false;
        repaint();
    }

    void window_t::damage_all() {
        _pending_damage = full_window_region();
    }

    rect_t window_t::client_area_pixels() const noexcept {
        // Built from the PLATFORM's pixel size, never from the overridable pixel_size(): the frame's
        // grid is what the backend can actually present, and an override exists to simulate the pointer
        // conversion, not to move the damage clip.
        return rect_t{ point_t{}, _platform->pixel_size() };
    }

    region_t window_t::full_window_region() const {
        if (_platform->pixel_size().isEmpty()) {
            return region_t{};
        }
        return region_t{ client_area_pixels() };
    }

    point_t window_t::pixel_point_of(point_t window_point) const noexcept {
        // The ONE place a pointer position crosses from the platform's window coordinates into the
        // paint space. Both extents are virtual, so a test can inject a density this display does not
        // report; at a density of 1 the conversion is the identity. An empty window-coordinate size (an
        // unplaced window, see platform_window_t::window_geometry()) makes window_to_pixel_point() the
        // identity, which is the only honest answer when the density is not known.
        return window_to_pixel_point(window_point, window_coordinate_size(), pixel_size());
    }

    void window_t::seed_geometry() noexcept {
        _last_size = _platform->device_size();
        const rect_t geometry = _platform->window_geometry();
        // An EMPTY geometry means the platform's window is not on any screen (an offscreen target, a
        // headless backend): there is no position to remember, and no move can be reported until one
        // arrives. rect_t's own emptiness carries that state — no sentinel, no optional, no bool-to-
        // position mapping beyond this one.
        _has_position = !geometry.isEmpty();
        _last_position = _has_position ? geometry.top_left() : point_t{};
    }

    bool window_t::draw_frame(bool consume_input, const region_t& damage) {
        // The store owns the surface and the render-target switch; the frame only asks for the pass.
        // Resizing it to the client area first means a stale surface (a resize, a driver change) is
        // recreated before it is painted into.
        backing_store_t* const store = surface_store();
        const bool into_backing = store != nullptr
            && store->resize(_platform->pixel_size()) && store->begin_paint();
        // A fresh painter per frame: begin() resets the engine (blend, clip, scale) and the painter's
        // own snapshot is the default (opaque black, no clip, identity transform). Nothing from the
        // previous frame leaks into paint_event().
        painter_t painter{ *this };
        if (!painter.begin()) {
            if (into_backing) {
                store->end_paint(); // close the pass: it must not stay the render target
            }
            return false;
        }
        // The frame owns the pass COUNT, because it owns the clip and the clear. A backend that can clip
        // to a region is painted in one pass for the whole damage; every other backend is painted one
        // pass per rectangle, each clipped to EXACTLY that rectangle: the region is never collapsed to
        // its bounding rectangle, because repainting the gap between two damaged rectangles would blend
        // a translucent widget there a second time. The clip is pushed and popped per pass: set_clip()
        // INTERSECTS with the clip in effect, so a pass that did not restore it would narrow the next
        // one to the empty intersection.
        if (damage.rectangle_count() > 1 && painter.supports(engine_feature_flag::CLIP_REGION)) {
            painter.save();
            painter.clear_clip();
            for (const rect_t& area : damage.rects()) {
                if (!area.isEmpty()) {
                    painter.clear(area, color_t::white_color());
                }
            }
            painter.set_clip_region(damage);
            paint_event_t event{ painter, damage };
            paint_event(&event);
            painter.restore();
        }
        else {
            for (const rect_t& area : damage.rects()) {
                if (area.isEmpty()) {
                    continue;
                }
                painter.save();
                painter.clear_clip();
                painter.set_clip(area);
                painter.clear(area, color_t::white_color());
                paint_event_t pass{ painter, region_t{ area } };
                paint_event(&pass);
                painter.restore();
            }
        }
        ++_painted_frames;
        painter.end();
        // The frame has consumed the per-frame input data (press/release edges, wheel delta, text).
        // Clearing it now — after paint_event() returned, i.e. at the end of this frame and therefore at
        // the start of the next one — gives every edge exactly one frame, and keeps a press/release
        // pair that arrived between two frames visible to the frame that follows them. A platform's
        // size-move repair frame passes false: its input reports are still queued, so consuming the
        // edges here would drop those inputs.
        if (consume_input) {
            _input.reset_frame();
        }
        return into_backing;
    }

    void window_t::paint_frame(bool consume_input) {
        // The frame paints the damage the host reported, clipped to the client area. A damage rectangle
        // that reaches outside the window (a widget sticking out of the client area, a scrolled
        // rectangle) would otherwise make the backend clear pixels nobody can see.
        region_t damage = _pending_damage;
        damage.intersect(client_area_pixels());
        if (damage.is_empty()) {
            // Nothing is damaged: no clear, no paint_event(), no flush and no present. The input edges
            // still belong to "the frame that consumed them", so they are dropped here as well;
            // otherwise an edge nobody painted would survive into a much later frame.
            _scrolls.clear(); // nothing is pending, so no recorded shift is stale any more
            if (consume_input) {
                _input.reset_frame();
            }
            return;
        }
        // Without a usable store of the CURRENT size the frame must cover the whole window: a swapchain
        // back buffer is undefined after present, and a surface that has to be recreated is undefined
        // too, so repainting only the damage would leave the rest of the window stale.
        backing_store_t* const store = surface_store();
        const bool surface_ready = store != nullptr && store->is_valid()
            && store->size() == _platform->pixel_size();
        const region_t painted = surface_ready ? damage : full_window_region();
        const std::size_t frames_before = _painted_frames;
        const bool into_backing = draw_frame(consume_input, painted);
        if (_painted_frames != frames_before) {
            // The frame painted every band the recorded shifts exposed, so the surface and the frame
            // agree again and the next shift starts a new accumulation. (A shift that happened DURING
            // the pass could not be applied — the surface was the render target — and it damaged its
            // whole area, which is still pending and paints correctly on its own.)
            _scrolls.clear();
        }
        if (into_backing) {
            // Present every pixel of the client area, not just the painted rectangles. The window
            // target is NOT a persistent surface: a two-buffer flip-model swap chain makes the buffer a
            // frame renders into hold the pixels of the frame TWO presents ago. Only the store's surface
            // persists, which is what makes the partial paint above correct; the copy to the target must
            // cover everything the surface can have changed, and a scroll changes pixels far outside the
            // strip it damages. flush() presents as well, so there is no second present here.
            const region_t presented = full_window_region();
            store->end_paint();
            store->flush(presented);
            _presented = presented;
        } else {
            // No usable surface (no store, or the painter could not start): the frame is already in the
            // window target, so presenting it is all that is left. That path painted the whole client
            // area itself, so it presented the whole client area too.
            _platform->present();
            _presented = painted;
        }
        // The frame consumed the rectangles it painted. Damage reported WHILE it painted (a layout
        // changed in a paint event, a scroll that had to fall back) was never painted, so it survives
        // into the next frame instead of being dropped with the rest. What lies outside the client area
        // can never be painted, so it is dropped here rather than kept forever.
        _pending_damage = _pending_damage.subtracted(painted);
        _pending_damage.intersect(client_area_pixels());
    }

    void window_t::paint_event(paint_event_t*) { }

    void window_t::resize_event(resize_event_t*) { }

    void window_t::move_event(move_event_t*) { }

    void window_t::expose_event(expose_event_t*) { }

    void window_t::close_event(close_event_t*) {
        close();
    }

    bool window_t::key_press_event(int key) {
        if (key == escape_key) {
            close();
            return true;
        }
        return false;
    }

    void window_t::interact_event(interact_event_t*) { }

    void window_t::focus_out_event(focus_out_event_t*) { }

    void window_t::leave_event(leave_event_t*) { }
}
