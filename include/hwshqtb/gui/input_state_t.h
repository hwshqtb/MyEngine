#ifndef HWSHQTB__GUI__INPUT_STATE_H
#define HWSHQTB__GUI__INPUT_STATE_H

/// @file input_state_t.h
/// @brief input_state_t: one window's input state for one frame (mouse, buttons, keys, wheel,
///        modifiers).
///
/// @par Who owns it
/// window_t owns one and maintains it from the platform's input steps (handle_input()): the platform
/// half correlates the native events by the window they belong to, fills in one interaction_t per step
/// and reports only this window's, so no backend type reaches the state. The immediate host
/// (ui::im_host_t) is an item of the retained tree, so it receives input through the router and needs
/// no copy of this state. The mutators at the bottom exist for exactly two writers: window_t and a
/// headless test.
///
/// @par Frame model and edges
/// The LEVELS (mouse position, held buttons, held keys, modifiers) describe the current state. The
/// EDGES (pressed()/released(), wheel_delta() and text_input()) belong to one frame: they are set by the
/// events that arrived since the previous frame and are cleared once the frame that consumed them has
/// painted. window_t calls reset_frame() immediately AFTER paint_event() returns — i.e. at the end of
/// the frame that has just consumed the edges, which is the same moment the next frame begins. A press
/// and a release that both arrive between two frames therefore both show up in the next frame; each edge
/// is visible to exactly one frame.
///
/// @par Why not event_t
/// The widget layer takes gui::interaction_t, never an event_t or a window. input_state_t is the
/// window-side source a host translates that snapshot from, so this header carries no SDL type at all.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "hwshqtb/gui/export.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/metaobject/flag_t.h"

namespace hwshqtb::gui {
    // The mouse-button vocabulary (mouse_button_enum / mouse_button_flag / mouse_button_t) lives in
    // interaction_t.h, next to the snapshot that carries it: the per-step buttons level and the
    // per-button identity are the same bits, so they are declared together.

    /// @brief The input state of one window for one frame (see the file note for the ownership and
    ///        frame-edge contract).
    class HWSHQTB_GUI_EXPORT input_state_t {
    public:
        /// @brief Construct the neutral state: pointer at (0, 0), nothing held, no edges, no wheel.
        input_state_t();

        /// @brief Destroy the state.
        ~input_state_t();

        /// @brief Copy the whole state (a test snapshot).
        input_state_t(const input_state_t& other);

        /// @brief Copy-assign the whole state.
        input_state_t& operator=(const input_state_t& other);

        /// @brief Move the whole state.
        input_state_t(input_state_t&& other) noexcept;

        /// @brief Move-assign the whole state.
        input_state_t& operator=(input_state_t&& other) noexcept;

        /// @name Levels and edges (queries)
        /// @{
        /// @brief The mouse position in PIXELS: the paint space every item rectangle lives in.
        ///
        /// An SDL mouse event reports the pointer in the window's own coordinates; window_t converts
        /// it with the pixel density the window reports, so this position is directly comparable with
        /// item geometry.
        /// @note noexcept. Stale until the first mouse event; (0, 0) before that.
        point_t mouse_position() const noexcept;

        /// @brief The buttons currently held down.
        /// @note noexcept.
        mouse_button_t buttons() const noexcept;

        /// @brief Whether @p button is currently held down.
        /// @note noexcept. mouse_button_flag::NONE always answers false.
        bool down(mouse_button_flag button) const noexcept;

        /// @brief Whether @p button was pressed during the current frame (the press edge).
        /// @note noexcept. True for exactly one frame (see the file note).
        bool pressed(mouse_button_flag button) const noexcept;

        /// @brief Whether @p button was released during the current frame (the release edge).
        /// @note noexcept. True for exactly one frame.
        bool released(mouse_button_flag button) const noexcept;

        /// @brief Whether @p key is currently held down.
        /// @param key The backend key code (SDL_Keycode in the SDL backend: 27 is ESC, 65 'A', ...).
        /// @note noexcept.
        bool key_down(int key) const noexcept;

        /// @brief The wheel movement accumulated since the previous frame, in whole scroll ticks.
        /// @note noexcept. x is horizontal (positive right), y vertical (positive away from the user);
        ///       cleared with the other edges.
        point_t wheel_delta() const noexcept;

        /// @brief The text input accumulated since the previous frame, as UTF-8 bytes.
        /// @note The text the platform delivered (keyboard layout and input method included); it is
        ///       NOT derived from key codes. Empty when no text event arrived. Cleared with the other
        ///       per-frame data, or alone by reset_text_input().
        std::string_view text_input() const noexcept;

        /// @brief The modifier keys currently held.
        /// @note noexcept.
        modifier_t modifiers() const noexcept;
        /// @}

        /// @name Mutators (window_t / a headless test only)
        ///
        /// These are public because the gui layer has no friend-class convention for a value type, and
        /// because an offscreen test drives the state directly. Application code reads the queries
        /// above; only window_t's native filter (and a test) writes here. The contract is documented
        /// here and implemented in input_state_t.cpp.
        /// @{
        /// @brief Clear the per-frame data: both button edges, the wheel delta and the text input.
        /// @note noexcept. Called by window_t after paint_event(), i.e. at the end of the frame that
        ///       consumed the edges. Levels (position, held buttons, held keys, modifiers) survive.
        void reset_frame() noexcept;

        /// @brief Clear the accumulated wheel movement only (the button edges stay).
        /// @note noexcept. A host that translates the accumulator into per-step interaction snapshots
        ///       calls this once it delivered the ticks, so a later step of the same frame cannot
        ///       deliver the same ticks again.
        void reset_wheel() noexcept;

        /// @brief Clear the accumulated text input only (the button edges and the wheel stay).
        /// @note noexcept. Same per-step delivery rule as reset_wheel().
        void reset_text_input() noexcept;

        /// @brief Set the mouse position without touching the button state.
        /// @note noexcept.
        void set_mouse_position(point_t position) noexcept;

        /// @brief Replace the held-button LEVEL only (used by a motion event's state word).
        /// @note noexcept. Creates no edges: edges come from button events.
        void set_buttons(mouse_button_t buttons) noexcept;

        /// @brief Update one button's level and record the matching edge.
        /// @param button The button; mouse_button_flag::NONE is ignored.
        /// @param down true for a press, false for a release.
        /// @note noexcept. Setting a button that is already in that state records no edge (a repeated
        ///       press event does not manufacture a second click).
        void set_button(mouse_button_flag button, bool down) noexcept;

        /// @brief Record that @p key is held.
        /// @note noexcept. Idempotent.
        void add_key(int key) noexcept;

        /// @brief Record that @p key is no longer held.
        /// @note noexcept.
        void remove_key(int key) noexcept;

        /// @brief Forget every held key (focus loss).
        /// @note noexcept.
        void clear_keys() noexcept;

        /// @brief Accumulate wheel movement for this frame.
        /// @note noexcept.
        void add_wheel(point_t delta) noexcept;

        /// @brief Append text the platform delivered to this frame's text input.
        /// @param text The UTF-8 text of one text-input event.
        /// @note noexcept. Appends, so several text events before a paint are delivered as one run.
        void add_text_input(std::string_view text);

        /// @brief Replace the modifier set.
        /// @note noexcept.
        void set_modifiers(modifier_t modifiers) noexcept;
        /// @}

    private:
        /// @brief The mouse position in pixels (the paint space).
        point_t _mouse_position{};
        /// @brief The held-button mask (bits of mouse_button_flag).
        std::uint8_t _buttons = 0;
        /// @brief The press-edge mask of the current frame.
        std::uint8_t _pressed = 0;
        /// @brief The release-edge mask of the current frame.
        std::uint8_t _released = 0;
        /// @brief The held keys (a small sorted-by-use vector; a keyboard holds a handful of keys).
        std::vector<int> _keys;
        /// @brief The wheel movement accumulated for the current frame.
        point_t _wheel{};
        /// @brief The text input accumulated for the current frame (UTF-8 bytes).
        std::string _text_input;
        /// @brief The modifier set.
        modifier_t _modifiers{};
    };
}

#endif
