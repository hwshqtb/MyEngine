// offscreen_window_t: the whole window_t frame policy with no native window behind it.
//
// This test constructs NO core_application, NO gui_application_t and sets NO SDL hint: it must run
// with no video subsystem and no display, because that is the property the second platform backend
// exists to prove. If window_t ever starts needing the platform application, this file stops working
// — deliberately.
//
// Sections:
//  1. construction, size() and the out-of-range pixel answer;
//  2. show() paints the first frame: whole-window white, one counted frame, full presented region;
//  3. a subclass's paint_event() draws through the real painter_t / paint_engine_t (exact pixels);
//  4. an empty damage paints nothing at all;
//  5. a two-rectangle damage is painted in two passes (the recording subclass counts them);
//  6. scroll() moves real pixels and repaints exactly the band it uncovered;
//  7. save_screenshot() / save_painted_frame() write BMPs; the second one does not repaint;
//  8. headless input: one interaction_t step, converted to the pixel space, LEFT only;
//  9. the platform answers that have no native window behind them (id 0, visibility, defer_repaint);
// 10. the geometry of an unplaced window: the position is the documented (-1,-1) sentinel.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/input_state_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/offscreen_window_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/gui/region_t.h"
#include "hwshqtb/metaobject/event_dispatcher.h"

using namespace hwshqtb::gui;

namespace offscreen_window_test {

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

/// A window that paints a known two-band pattern and records what the frame handed it.
///
/// It exists to prove that the value path is the real one: paint_event() receives an ordinary
/// painter_t over an ordinary paint_engine_t, and the pixels it draws are read back with pixel().
class band_window_t final: public offscreen_window_t {
public:
    band_window_t(extent_t size, color_t left_color, color_t right_color):
        offscreen_window_t(size),
        _left(left_color),
        _right(right_color) { }

    /// @brief How many paint_event() passes ran.
    int paints() const noexcept {
        return _paints;
    }

    /// @brief Forget the pass count (so a section can measure one frame).
    void reset_paints() noexcept {
        _paints = 0;
    }

    /// @brief The clip rectangle of each pass, in the order the frame made them.
    const std::vector<rect_t>& passes() const noexcept {
        return _passes;
    }

    /// @brief Forget the recorded clips.
    void reset_passes() {
        _passes.clear();
    }

    /// @brief The interaction steps the window routed here.
    const std::vector<interaction_t>& steps() const noexcept {
        return _steps;
    }

    /// @brief Forget the routed steps.
    void reset_steps() {
        _steps.clear();
    }

protected:
    void paint_event(paint_event_t* event) override {
        painter_t& painter = event->painter();
        ++_paints;
        _passes.push_back(painter.clip_rect());
        // Two solid bands: the left half one colour, the right half another. Both are opaque and
        // cover the whole clip, so a scroll that fails to move pixels is visible immediately.
        const extent_t window_size = size();
        const int half = window_size.width() / 2;
        painter.set_color(_left);
        painter.fill_rect(rect_t{ 0, 0, half, window_size.height() });
        painter.set_color(_right);
        painter.fill_rect(rect_t{ half, 0, window_size.width() - half, window_size.height() });
    }

    void interact_event(interact_event_t* event) override {
        _steps.push_back(event->step());
    }

private:
    color_t _left;
    color_t _right;
    int _paints = 0;
    std::vector<rect_t> _passes;
    std::vector<interaction_t> _steps;
};

/// A window that records the move events the platform reports, and the input steps routed to it.
///
/// It exists for the "no screen position" contract: an offscreen buffer is placed nowhere, so the
/// platform reports an EMPTY geometry (a non-positive client-area size), and the observable
/// consequences are what this class records — the first move report becomes the position without an
/// event, and the pointer conversion has no window-coordinate extent to divide by.
class moving_window_t final: public offscreen_window_t {
public:
    using offscreen_window_t::offscreen_window_t;

    /// @brief How many move_event() calls ran.
    std::size_t moves() const noexcept {
        return _moves;
    }

    /// @brief The old position of the last move event.
    point_t last_old_position() const noexcept {
        return _old_position;
    }

    /// @brief The new position of the last move event.
    point_t last_position() const noexcept {
        return _position;
    }

    /// @brief The input steps that reached interact_event().
    const std::vector<interaction_t>& steps() const noexcept {
        return _steps;
    }

protected:
    void move_event(move_event_t* event) override {
        ++_moves;
        _old_position = event->old_position();
        _position = event->position();
    }

    void interact_event(interact_event_t* event) override {
        _steps.push_back(event->step());
    }

private:
    std::size_t _moves = 0;
    point_t _old_position{};
    point_t _position{};
    std::vector<interaction_t> _steps;
};

/// A window that paints nothing: the frame's clear is the only content.
class blank_window_t final: public offscreen_window_t {
public:
    using offscreen_window_t::offscreen_window_t;
};

}  // namespace offscreen_window_test

using namespace offscreen_window_test;

int main() {
    std::printf("== offscreen_window_t: window_t with no native window ==\n");

    constexpr extent_t window_size{ 60, 40 };
    const color_t left_color{ 200, 30, 40 };
    const color_t right_color{ 20, 90, 200 };

    // ---- (1) construction: the extent is the pixel buffer, and out-of-range answers transparent ------
    band_window_t window{ window_size, left_color, right_color };
    CHECK((window.size() == window_size));
    CHECK(window.painted_frame_count() == 0);
    CHECK(!window.is_visible());
    CHECK(window.window_id() == 0); // no native window: no native event stream either
    CHECK((window.pixel(-1, 0) == color_t::transparent_color()));
    CHECK((window.pixel(0, -1) == color_t::transparent_color()));
    CHECK((window.pixel(window_size.width(), 0) == color_t::transparent_color()));
    CHECK((window.pixel(0, window_size.height()) == color_t::transparent_color()));
    {
        // The documented contract: an empty extent is refused up front, under this window's name.
        bool rejected = false;
        std::string message;
        try {
            blank_window_t empty{ extent_t{} };
        }
        catch (const std::invalid_argument& error) {
            rejected = true;
            message = error.what();
        }
        CHECK(rejected);
        CHECK(message.rfind("offscreen_window_t:", 0) == 0);
    }

    // ---- (2) show() paints the first frame: the whole client area, white, presented in full ---------
    {
        blank_window_t blank{ window_size };
        blank.show();
        CHECK(blank.is_visible());
        CHECK(blank.painted_frame_count() == 1);
        CHECK((blank.presented_region().bounds() == rect_t{ 0, 0, window_size.width(), window_size.height() }));
        CHECK((blank.pixel(0, 0) == color_t::white_color()));
        CHECK((blank.pixel(window_size.width() - 1, window_size.height() - 1) == color_t::white_color()));
        std::printf("[offscreen] first frame: %zu painted frame(s), presented the whole client area\n",
                    blank.painted_frame_count());
    }

    // ---- (3) a subclass paints through the real painter/engine, and pixel() reads it back -------------
    window.show();
    CHECK(window.painted_frame_count() == 1);
    CHECK((window.pixel(5, 5) == left_color));
    CHECK((window.pixel(window_size.width() - 6, 5) == right_color));
    CHECK((window.pixel(5, window_size.height() - 1) == left_color));
    {
        // The frame clears to white FIRST, so a colour that is not white can only come from paint_event.
        CHECK(!(left_color == color_t::white_color()));
        CHECK(!(right_color == color_t::white_color()));
    }

    // ---- (4) no damage: no clear, no paint_event, no present -----------------------------------------
    {
        const std::size_t frames_before = window.painted_frame_count();
        const color_t before = window.pixel(5, 5);
        window.reset_paints();
        window.set_pending_damage(region_t{});
        window.repaint();
        CHECK(window.painted_frame_count() == frames_before);
        CHECK(window.paints() == 0);
        CHECK((window.pixel(5, 5) == before));
        CHECK(window.pending_damage().is_empty());
    }

    // ---- (5) two damaged rectangles are TWO passes, each clipped to exactly one of them --------------
    {
        region_t damage;
        damage.unite(rect_t{ 2, 3, 8, 6 });
        damage.unite(rect_t{ 40, 20, 10, 10 });
        CHECK(damage.rectangle_count() == 2);
        window.reset_paints();
        window.reset_passes();
        window.set_pending_damage(damage);
        window.repaint();
        CHECK(window.paints() == 2);
        CHECK(window.passes().size() == 2);
        CHECK((window.passes().at(0) == rect_t{ 2, 3, 8, 6 }));
        CHECK((window.passes().at(1) == rect_t{ 40, 20, 10, 10 }));
        // The presentation still covered the whole client area.
        CHECK((window.presented_region().bounds() == rect_t{ 0, 0, window_size.width(), window_size.height() }));
        std::printf("[offscreen] partial frame: %d pass(es) for %zu damaged rectangle(s)\n",
                    window.paints(), damage.rectangle_count());
    }

    // ---- (6) scroll(): the pixels move and the uncovered band is repainted ---------------------------
    {
        // A horizontal scroll moves the band pattern sideways: the left colour appears where the
        // right one was, and the strip the shift uncovered is repainted from scratch.
        const rect_t area{ 0, 0, window_size.width(), window_size.height() };
        const int dx = 10;
        const region_t exposed = window.scroll(area, dx, 0);
        CHECK(!exposed.is_empty());
        // The band a positive shift exposes is the vertical strip at the rectangle's left edge.
        CHECK((exposed.bounds() == rect_t{ 0, 0, dx, window_size.height() }));
        CHECK(exposed.subtracted(window.pending_damage()).is_empty());
        const int passes_before = window.paints();
        window.repaint();
        CHECK(window.paints() > passes_before); // the exposed strip was painted again
        // The pixel that was at x is now at x + dx: the left colour has moved rightwards.
        CHECK((window.pixel(dx + 2, 5) == left_color));
        // The uncovered strip was repainted by paint_event with the colour of the moved-in content.
        CHECK((window.pixel(0, 5) == left_color));
        // The right edge moved with the rest: the right colour ends dx further right (clipped away),
        // so the last column still shows the right colour from the previous frame's content.
        CHECK((window.pixel(window_size.width() - 1, 5) == right_color));
        std::printf("[offscreen] scroll: exposed %zu px band, %d paint pass(es) after it\n",
                    exposed.area(), window.paints() - passes_before);
    }

    // ---- (7) capture: both entry points write a BMP, and the diagnostic one does not repaint ---------
    {
        const std::string screenshot_path = "offscreen_window_screenshot.bmp";
        const std::string painted_path = "offscreen_window_painted.bmp";
        window.save_screenshot(screenshot_path);
        CHECK(std::filesystem::exists(screenshot_path));
        CHECK(std::filesystem::file_size(screenshot_path) > 0);
        const std::size_t frames_after_capture = window.painted_frame_count();
        window.save_painted_frame(painted_path);
        CHECK(std::filesystem::exists(painted_path));
        CHECK(std::filesystem::file_size(painted_path) > 0);
        CHECK(window.painted_frame_count() == frames_after_capture); // no repaint
        // Both files are 24-bit BMPs of the client area: the header is 54 bytes plus 3 bytes per pixel
        // (rows padded to 4), so the size is a fixed function of the extent.
        const std::uintmax_t row_bytes = static_cast<std::uintmax_t>(window_size.width()) * 3u;
        const std::uintmax_t padded_row = (row_bytes + 3u) / 4u * 4u;
        const std::uintmax_t expected = 54u + padded_row * static_cast<std::uintmax_t>(window_size.height());
        CHECK(std::filesystem::file_size(screenshot_path) == expected);
        CHECK(std::filesystem::file_size(painted_path) == expected);
        std::printf("[offscreen] capture: %ju byte BMP(s) for %dx%d\n",
                    static_cast<std::uintmax_t>(std::filesystem::file_size(screenshot_path)),
                    window_size.width(), window_size.height());
        std::filesystem::remove(screenshot_path);
        std::filesystem::remove(painted_path);
    }

    // ---- (8) headless input: one step, converted to the pixel space, LEFT only ----------------------
    {
        window.reset_steps();
        interaction_t motion;
        motion.mouse = point_t{ 12, 7 }; // WINDOW coordinates: this backend's are the pixel space
        motion.buttons = mouse_button_flag::LEFT;
        motion.modifiers = modifier_bits(modifier_flag::SHIFT);
        window.handle_input(motion);
        CHECK((window.input_state().mouse_position() == point_t{ 12, 7 }));
        CHECK(window.input_state().down(mouse_button_flag::LEFT));
        CHECK(has_modifier(modifier_bits(window.input_state().modifiers()), modifier_flag::SHIFT));
        CHECK(window.steps().size() == 1);
        CHECK((window.steps().back().mouse == point_t{ 12, 7 }));
        CHECK(window.steps().back().mouse_down);

        // A button step carries the edge and reaches the tree for the LEFT button only.
        interaction_t press;
        press.mouse = point_t{ 20, 30 };
        press.buttons = mouse_button_flag::LEFT;
        press.button = mouse_button_enum::LEFT;
        press.mouse_down = true;
        press.mouse_pressed = true;
        window.handle_input(press);
        CHECK(window.steps().size() == 2);
        CHECK(window.steps().back().mouse_pressed);
        CHECK(window.steps().back().button == mouse_button_enum::LEFT);

        interaction_t right;
        right.mouse = point_t{ 21, 31 };
        right.button = mouse_button_enum::RIGHT;
        right.mouse_pressed = true;
        window.handle_input(right);
        CHECK(window.input_state().down(mouse_button_flag::RIGHT));
        CHECK(window.steps().size() == 2); // the tree routes LEFT only

        interaction_t text;
        text.mouse = point_t{ 20, 30 };
        text.text_input = "ab";
        window.handle_input(text);
        CHECK(window.steps().size() == 3);
        CHECK(window.steps().back().text_input == "ab");
        CHECK(window.input_state().text_input().empty()); // the step consumed it
        std::printf("[offscreen] input: %zu step(s) routed, pointer at %d,%d\n",
                    window.steps().size(), window.input_state().mouse_position().x(),
                    window.input_state().mouse_position().y());
    }

    // ---- (9) the "no native window" answers ---------------------------------------------------------
    {
        CHECK(window.window_id() == 0);
        window.hide();
        CHECK(!window.is_visible());
        window.show();
        CHECK(window.is_visible());
        // defer_repaint() is the platform's answer to "do you own the frame schedule?". It must be
        // false here, which shows up as: an update() really posts a PAINT, and dispatching the queue
        // really paints. A platform that deferred would paint nothing here.
        window.set_pending_damage(region_t{ rect_t{ 0, 0, 4, 4 } });
        const std::size_t frames_before = window.painted_frame_count();
        window.update();
        (void)hwshqtb::metaobject::event_dispatcher::instance().process_events();
        CHECK(window.painted_frame_count() == frames_before + 1);
        CHECK(window.pending_damage().is_empty());
    }

    // ---- (10) an offscreen buffer is not placed on any screen: its geometry is EMPTY ---------------
    //
    // platform_window_t::window_geometry() has to answer a position and a client area, and this backend
    // has neither: rect_t already carries the way to say that — a NON-POSITIVE size, i.e. isEmpty().
    // That is why no sentinel position and no optional<rect_t> exist: a (-1, -1) POSITION is legal on a
    // multi-monitor desktop, while a negative client AREA is impossible for a real window.
    //
    // The observable consequences are the two asserted here: the pointer conversion has no
    // window-coordinate extent (so it is the identity), and the window has no position, so its first
    // move report becomes its position WITHOUT emitting a move event (there is no old position to name).
    {
        moving_window_t moved{ window_size };
        // No window-coordinate extent: the conversion is the identity, so a window-space point arrives
        // in the pixel space unchanged.
        interaction_t motion;
        motion.mouse = point_t{ 20, 30 };
        moved.handle_input(motion);
        CHECK(moved.steps().size() == 1);
        CHECK((moved.steps().back().mouse == point_t{ 20, 30 }));

        // The first move report is the window's position, not a move: nothing moved, it appeared.
        moved.handle_move(40, 30);
        (void)hwshqtb::metaobject::event_dispatcher::instance().process_events();
        CHECK(moved.moves() == 0);

        // A second, DIFFERENT report is a real move: there is now an old position to name.
        moved.handle_move(42, 31);
        (void)hwshqtb::metaobject::event_dispatcher::instance().process_events();
        CHECK(moved.moves() == 1);
        CHECK((moved.last_old_position() == point_t{ 40, 30 }));
        CHECK((moved.last_position() == point_t{ 42, 31 }));

        // The same position again is not a change.
        moved.handle_move(42, 31);
        (void)hwshqtb::metaobject::event_dispatcher::instance().process_events();
        CHECK(moved.moves() == 1);

        std::printf("[offscreen] geometry: empty (no screen position), client area %dx%d in pixels\n",
                    window_size.width(), window_size.height());
    }

    if (g_failures == 0) {
        std::printf("offscreen_window: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("offscreen_window: %d of %d checks failed\n", g_failures, g_checks);
    return 1;
}
