#ifndef HWSHQTB__GUI__INTERACTION_H
#define HWSHQTB__GUI__INTERACTION_H

/// @file interaction_t.h
/// @brief interaction_t: the host-neutral input snapshot the widget layer consumes, plus the
///        modifier-key flags it carries.

#include <cstdint>
#include <string>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/metaobject/flag_t.h"

namespace hwshqtb::gui {
    /// @brief One modifier key as a bit of interaction_t::modifiers.
    ///
    /// A plain enum so the bits are a stable, host-independent vocabulary: a backend translates its
    /// native modifiers (SDL_KMOD_*, a Win32 key state, ...) into these and nothing above this layer
    /// ever sees a backend modifier word.
    enum struct modifier_flag : std::uint32_t {
        /// @brief No modifier held.
        NONE = 0,
        /// @brief Shift.
        SHIFT = 1u << 0,
        /// @brief Control (the Windows/Linux Ctrl key).
        CONTROL = 1u << 1,
        /// @brief Alt (the Windows Alt / macOS Option key).
        ALT = 1u << 2,
        /// @brief GUI key: Windows key, Command on macOS, Super on Linux.
        GUI = 1u << 3,
    };

    /// @brief A bit set of modifier_flag values (the project's typed flag wrapper, QFlags shape).
    using modifier_t = ::hwshqtb::metaobject::flag_t<modifier_flag>;

    /// @brief The raw bit mask of @p flags, ready for interaction_t::modifiers.
    /// @param flags The modifier set to convert.
    /// @return The underlying std::uint32_t bits.
    /// @note Header-only constexpr: a value-layer helper, not an exported function.
    constexpr std::uint32_t modifier_bits(modifier_t flags) noexcept {
        return static_cast<std::uint32_t>(flags.data);
    }

    /// @brief Whether @p bits (an interaction_t::modifiers value) contains @p flag.
    /// @param bits The raw modifier mask.
    /// @param flag The modifier to test.
    /// @return true when the flag's bit is set.
    /// @note Header-only constexpr: a value-layer helper, not an exported function.
    constexpr bool has_modifier(std::uint32_t bits, modifier_flag flag) noexcept {
        return (bits & modifier_bits(flag)) != 0;
    }

    /// @brief ONE mouse button, as an identity rather than as a bit of a set.
    ///
    /// A per-button question ("is THIS button held here?") needs a single button, not a mask; the
    /// bit set of held buttons is mouse_button_t, the flag form of the same vocabulary. The values
    /// deliberately equal the bits of mouse_button_flag, so the identity and the set need no mapping
    /// table and a backend translates its native button index onto one vocabulary only.
    /// @note A plain enumeration, not a flag: it is never combined with | / &.
    enum struct mouse_button_enum: std::uint8_t {
        /// @brief The left button.
        LEFT = 1,
        /// @brief The middle button.
        MIDDLE = 2,
        /// @brief The right button.
        RIGHT = 4,
    };

    /// @brief One mouse button, as a bit of mouse_button_t.
    ///
    /// The bit set itself: what is held down right now, and what one press/release step acts on. A
    /// backend maps its native button index onto these; nothing above the platform sees an SDL button.
    enum struct mouse_button_flag: std::uint8_t {
        /// @brief No button (the empty flag value).
        NONE = 0,
        /// @brief The left button.
        LEFT = 1,
        /// @brief The middle button.
        MIDDLE = 2,
        /// @brief The right button.
        RIGHT = 4,
    };

    /// @brief A bit set of mouse_button_flag values (the project's typed flag wrapper).
    using mouse_button_t = ::hwshqtb::metaobject::flag_t<mouse_button_flag>;

    // The per-button identity and the held-button set are two views of ONE vocabulary: the item
    // layer stores a pressed button as its bit, so the values must stay equal.
    static_assert(static_cast<std::uint8_t>(mouse_button_enum::LEFT)
                  == static_cast<std::uint8_t>(mouse_button_flag::LEFT));
    static_assert(static_cast<std::uint8_t>(mouse_button_enum::MIDDLE)
                  == static_cast<std::uint8_t>(mouse_button_flag::MIDDLE));
    static_assert(static_cast<std::uint8_t>(mouse_button_enum::RIGHT)
                  == static_cast<std::uint8_t>(mouse_button_flag::RIGHT));

    /// @brief A host-neutral snapshot of the input a widget sees for one frame.
    ///
    /// @par Why not event_t
    /// The widget layer must serve two hosts: the retained host routes platform events, and the
    /// immediate host polls a frame state. Both can fill this small value in, so neither `event_t`
    /// nor a window type appears in the widget API and the same `interact()` implementation works
    /// under both.
    ///
    /// @par One step is one payload
    /// A platform does not call one window method per input kind: it fills ONE of these in and hands
    /// it over as a step (window_t::handle_input()). Which fields are set says what the step is:
    ///  - pointer motion: only mouse (and buttons, the level);
    ///  - button press/release: button plus exactly one of mouse_pressed / mouse_released;
    ///  - wheel: wheel, a non-zero point;
    ///  - key: key, non-zero, plus key_pressed or key_released;
    ///  - committed text: text_input, a non-empty string.
    /// The window derives the rest (mouse_down, the held-button set, the edges) from its own
    /// input_state_t, so the platform never maintains per-window input state.
    ///
    /// @par Coordinates are LOCAL
    /// mouse is expressed in the receiving widget's own coordinate system, with (0, 0) at its
    /// geometry's top-left. The HOST is responsible for the mapping: it hit-tests the item tree
    /// (widget_t::hit) and hands each widget the mouse position minus that widget's geometry
    /// origin. interact() never translates, so a widget that is moved by the layout sees the same
    /// numbers a host computed for it. The one exception is the ROUTER's own input
    /// (ui::input_router_t::dispatch), which is in the GLOBAL space item_t::map_to_global()
    /// produces (window coordinates for a root at the origin) because no single widget owns it yet:
    /// the router translates it per delivered item.
    ///
    /// @par Edge flags
    /// mouse_pressed is the press edge of THIS frame and mouse_released the release edge; they are
    /// true for one frame only. mouse_down is the level (still held). A host that samples a polled
    /// state twice must compute the edges itself. button names the single button the edges are about
    /// (mouse_down alone does not say which one).
    ///
    /// @par Wheel
    /// wheel is the wheel movement of THIS STEP, in whole scroll ticks taken from the host's frame
    /// accumulator (input_state_t::wheel_delta() has the same shape). It is an EDGE, not a level: a
    /// zero point on every step that carries no wheel event, and the host clears the accumulator once
    /// it delivered the ticks, so one notch is consumed exactly once. x is horizontal (positive right),
    /// y vertical (positive away from the user).
    ///
    /// @par Text input
    /// text_input is the text the system delivered for THIS STEP (UTF-8, possibly empty), taken from
    /// the host's frame accumulator (input_state_t::text_input()). It is an EDGE like the wheel: an
    /// empty string on steps that carry no text event, and the host clears the accumulator once it
    /// delivered the text. The text is what the platform's keyboard layout and input method produced —
    /// it is NEVER derived from key codes, so a control must not guess characters from interaction_t::key.
    ///
    /// @par Levels versus edges
    /// mouse is a LEVEL (where the pointer is now); mouse_pressed/mouse_released, wheel and text_input
    /// are EDGES of the step.
    ///
    /// @par Keys
    /// Keys are not positional, so the retained router picks a different TARGET for them (the focus
    /// widget) while this payload and the item_t::interact() entry stay identical — that is what
    /// keeps "one control implementation" true for keys as well. key == 0 means "no key this step";
    /// a non-zero key makes the snapshot a KEY step (mouse fields are then only context).
    ///
    /// @par Value type
    /// Header-only (like geometry.h and color_t.h), so it is not exported. The text input makes it a
    /// non-trivial value (a step owns its string): a step that carries no text costs no allocation,
    /// because std::string keeps the empty value in place.
    struct interaction_t {
        /// @name Key codes
        /// The host-neutral key vocabulary the controls and their callers share: the platform half
        /// translates its native keys into these values (window_t reports the platform key code
        /// through), so no header above the platform names a backend type.
        /// @{
        /// @brief Tab (SDL_Keycode 9), the one key the retained router handles itself.
        static constexpr int tab_key = 9;
        /// @brief Space (SDL_Keycode 32).
        static constexpr int space_key = 32;
        /// @brief Return (SDL_Keycode 13).
        static constexpr int return_key = 13;
        /// @brief Escape (SDL_Keycode 27).
        static constexpr int escape_key = 27;
        /// @brief Backspace (SDL_Keycode 8).
        static constexpr int backspace_key = 8;
        /// @brief Delete (SDL_Keycode 127).
        static constexpr int delete_key = 127;
        /// @brief The Left arrow (SDL_Keycode 0x40000050).
        static constexpr int left_key = 0x40000050;
        /// @brief The Right arrow (SDL_Keycode 0x4000004F).
        static constexpr int right_key = 0x4000004F;
        /// @brief The Up arrow (SDL_Keycode 0x40000052).
        static constexpr int up_key = 0x40000052;
        /// @brief The Down arrow (SDL_Keycode 0x40000051).
        static constexpr int down_key = 0x40000051;
        /// @brief Page Up (SDL_Keycode 0x4000004B).
        static constexpr int page_up_key = 0x4000004B;
        /// @brief Page Down (SDL_Keycode 0x4000004E).
        static constexpr int page_down_key = 0x4000004E;
        /// @brief Home (SDL_Keycode 0x4000004A).
        static constexpr int home_key = 0x4000004A;
        /// @brief End (SDL_Keycode 0x4000004D).
        static constexpr int end_key = 0x4000004D;
        /// @}

        /// @brief The mouse position in the receiving widget's local coordinates.
        point_t mouse{};
        /// @brief Whether a mouse button is currently held down.
        bool mouse_down = false;
        /// @brief The button LEVEL as a bit set: which buttons are held down right now.
        /// @note The motion reports carry it, so a release the window never saw (the button came up
        ///       over another window) is corrected on the next move. mouse_down is the LEFT shorthand
        ///       the item tree reads.
        mouse_button_t buttons{};
        /// @brief Whether a mouse button was pressed in this frame (the press edge).
        bool mouse_pressed = false;
        /// @brief Whether a mouse button was released in this frame (the release edge).
        bool mouse_released = false;
        /// @brief The button that the press/release edges of this step are about.
        /// @note An EDGE needs the identity (the item records its pressed state per button); the
        ///       mouse_down LEVEL does not ("some button is held"). LEFT when no button is involved.
        mouse_button_enum button = mouse_button_enum::LEFT;
        /// @brief Whether the host already decided that the cursor is inside this widget.
        bool hovered = false;
        /// @brief The wheel movement of this step, in whole scroll ticks (zero when no wheel event).
        /// @note x: positive right; y: positive away from the user. See the class note on wheel.
        point_t wheel{};
        /// @brief The text the system delivered for this step (UTF-8; empty when there is none).
        /// @note This is the ONLY character source: key codes describe a physical key, not the text a
        ///       layout or an input method produced. See the class note on text input.
        std::string text_input{};
        /// @brief Modifier bits; build them with modifier_bits(modifier_flag::...).
        std::uint32_t modifiers = 0;
        /// @brief The backend key code of this step, or 0 when no key is involved.
        int key = 0;
        /// @brief Whether a key was pressed in this step (the press edge).
        bool key_pressed = false;
        /// @brief Whether a key was released in this step (the release edge).
        bool key_released = false;
    };
}

#endif
