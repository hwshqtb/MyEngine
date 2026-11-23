#ifndef HWSHQTB__UI__LINE_EDIT_T_H
#define HWSHQTB__UI__LINE_EDIT_T_H

/// @file line_edit_t.h
/// @brief line_edit_t: one line of editable text with a caret and a selection (the QLineEdit subset
///        of the ui layer).

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/widget_t.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    /// @brief How a line edit shows its text.
    enum struct echo_mode_enum : std::uint8_t {
        /// @brief Show the text as it is.
        NORMAL,
        /// @brief Show one placeholder character per text character (never the text itself).
        PASSWORD,
    };

    /// @brief A single-line text editor: UTF-8 text, a caret, a selection and keyboard editing.
    ///
    /// @par The text input channel
    /// Characters come ONLY from interaction_t::text_input — the UTF-8 text the platform delivered for
    /// that step (keyboard layout and input method applied). They are never derived from key codes:
    /// a key code names a physical key, not a character. Because the bitmap font only has glyphs for
    /// printable ASCII (font_t covers 32..126), every other codepoint is DROPPED on insertion: the
    /// control cannot display what the font cannot draw.
    ///
    /// @par State and model
    /// _text is a UTF-8 byte string; the caret and the selection anchor are BYTE indices kept on
    /// code-point boundaries, so an edit never splits a multi-byte sequence. set_text() is the
    /// external write: it emits text_changed() (a state notification) and never edited()/the User
    /// event. The control's own edits emit text_changed() AND edited() and raise the per-frame
    /// result. The User property is text_changed — the same event that names text's notifier — so
    /// the generic state store finds text as this control's state; edited is the ordinary commit
    /// signal.
    ///
    /// @par Editing (with the focus)
    ///  - printable ASCII from the text input is inserted at the caret; a selection is REPLACED first;
    ///  - Backspace removes the previous code point, Delete the next one (a selection is removed first);
    ///  - Left/Right move the caret (Shift extends the selection), Home/End go to the line bounds;
    ///  - Ctrl+A selects everything;
    ///  - Enter emits return_pressed() then editing_finished(), Esc emits editing_finished();
    ///  - read-only: navigation and the selection work, editing keys and the text input are not
    ///    consumed; disabled: nothing is consumed.
    ///
    /// @par Deliberately absent
    ///  - NO clipboard: Ctrl+C/Ctrl+X/Ctrl+V do nothing, because the gui layer has no system
    ///    clipboard;
    ///  - NO blinking caret: the caret is a plain one-pixel line whenever the widget has the focus
    ///    (there is no timer in this layer);
    ///  - NO input-method integration beyond accepting the delivered text: the bitmap font has no
    ///    glyphs outside ASCII and there is no candidate window, so an IME's composed text would
    ///    mostly be dropped.
    ///
    /// @par Painting
    /// The frame, the text, the selection and the caret are drawn by default_style() (LINE_EDIT_FRAME,
    /// TEXT, SELECTION, LINE_EDIT_CURSOR). Password mode shows one style-provided placeholder per
    /// character. When the text is wider than the content area the visible window scrolls in WHOLE
    /// character cells, so the caret stays visible and a character is never cut in half.
    class HWSHQTB_UI_EXPORT line_edit_t: public widget_t {
        OBJECT

    public:
        /// @brief Construct an empty editable line edit (the class registry's creation path).
        /// @note The focus policy is focus_policy_enum::STRONG_FOCUS.
        line_edit_t();

        /// @brief Construct a line edit inside a retained widget tree.
        /// @param parent The visual parent; nullptr makes this a root widget.
        explicit line_edit_t(widget_t* parent);

        /// @brief The text (UTF-8 bytes).
        /// @note The `text` property; the writer is set_text() and the notifier text_changed().
        [[= ::hwshqtb::metaobject::make_property("text", "set_text", "text_changed")]]
        std::string_view text() const;

        /// @brief Replace the whole text (the external write).
        /// @param text The new UTF-8 text; it is copied.
        /// @note Emits text_changed() when the value really changes (a state notification), moves the
        ///       caret to the end, clears the selection, and never emits edited() nor raises the
        ///       User event: those belong to the user's own edits.
        void set_text(std::string_view text);

        /// @brief The display mode (NORMAL by default).
        echo_mode_enum echo_mode() const;

        /// @brief Set how the text is displayed (the stored text is never changed).
        /// @param mode NORMAL or PASSWORD.
        void set_echo_mode(echo_mode_enum mode);

        /// @brief Whether editing is disabled.
        bool is_read_only() const;

        /// @brief Enable or disable editing (a read-only field still navigates and selects).
        /// @param read_only The new state.
        void set_read_only(bool read_only);

        /// @brief The caret position, as a byte index on a code-point boundary.
        std::size_t cursor_position() const;

        /// @brief Move the caret and collapse the selection.
        /// @param position The wanted byte index; it is clamped into the text and snapped back to a
        ///        code-point boundary, so a multi-byte sequence is never split.
        void set_cursor_position(std::size_t position);

        /// @brief Whether the selection is not empty (the caret differs from the anchor).
        bool has_selection() const;

        /// @brief The selected bytes (empty without a selection).
        /// @note The view points into the control's own text and is invalidated by the next edit.
        std::string_view selected_text() const;

        /// @brief Select the whole text (the caret moves to the end).
        void select_all();

        /// @brief Clear the text (same state changes as set_text("") plus a caret reset).
        void clear();

        /// @brief Whether this frame had an edit made by the USER.
        /// @return true only on a frame where the text input, Backspace or Delete changed the text;
        ///         false on every other frame, including frames whose value came from set_text().
        /// @note The class's User property: the immediate host's control() returns exactly this
        ///       value. Its name is text_changed, the event text's notifier points at, so the generic
        ///       state store links the text property to this event.
        [[= ::hwshqtb::metaobject::make_property("text_changed", "", "", "", ::hwshqtb::metaobject::property_trait_flag::USER)]]
        bool text_changed() const;

        /// @brief The natural size: the displayed text width plus the style padding, at least
        ///        LINE_EDIT_WIDTH, and one line height plus the vertical padding.
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief The smallest useful width: eight '0' advances plus the two text paddings.
        /// @note Font derived on purpose: a line edit narrower than a few characters cannot show what is
        ///       being typed, so the floor follows the resolved face's digits rather than a pixel constant
        ///       (Qt's QLineEdit does the same with a character count). The height is size_hint()'s, which
        ///       already comes from the engine's line height.
        ::hwshqtb::gui::extent_t minimum_size_hint() const override;

        /// @brief Drive the keyboard editing state machine one frame.
        /// @param in The input snapshot, in this widget's local coordinates (the host maps it).
        /// @return true when the control consumed the step (text input or a recognised key while it
        ///         holds the focus); false otherwise (no focus, disabled, or a read-only edit key).
        bool interact(const ::hwshqtb::gui::interaction_t& in) override;

        /// @brief Emitted whenever the text changes, whatever the source (the user or set_text()).
        /// @param text The new text.
        SIGNAL(void text_changed(std::string_view text) const)

        /// @brief Emitted only when the USER's edit changed the text (the commit notification).
        /// @param text The new text.
        SIGNAL(void edited(std::string_view text) const)

        /// @brief Emitted on Enter.
        SIGNAL(void return_pressed() const)

        /// @brief Emitted on Enter and on Escape (the edit session is over).
        SIGNAL(void editing_finished() const)

    protected:
        /// @brief Draw the frame, the selection, the text and the caret.
        /// @param event The paint event; event.painter() is already translated to this widget and
        ///        clipped to rect().
        void paint_event(paint_event_t* event) override;

    private:
        /// @brief Apply an edit the USER made: store the text, move the caret, raise the event and
        ///        emit text_changed() + edited().
        /// @param updated The new text.
        /// @param cursor The caret after the edit (a code-point boundary).
        void edit_text(std::string updated, std::size_t cursor);

        /// @brief Insert the displayable part of @p input at the caret (replacing a selection).
        /// @param input The text input of the step (UTF-8).
        /// @return Whether the text changed.
        bool insert_text(std::string_view input);

        /// @brief Remove the selected range.
        /// @return Whether there was a selection to remove.
        bool delete_selection();

        /// @brief Remove the code point before the caret.
        /// @return Whether the text changed.
        bool backspace();

        /// @brief Remove the code point after the caret.
        /// @return Whether the text changed.
        bool delete_forward();

        /// @brief Move the caret by one code point.
        /// @param forward true for Right, false for Left.
        /// @param extend true to keep the anchor (Shift).
        void move_cursor(bool forward, bool extend);

        /// @brief The selection's first byte index (the smaller of caret and anchor).
        std::size_t selection_start() const;

        /// @brief The selection's one-past-last byte index.
        std::size_t selection_end() const;

        /// @brief The text as the user sees it (the raw text, or the password placeholder run).
        std::string display_text() const;

        /// @brief The DISPLAY cell of a byte position: the byte index in NORMAL mode, the code-point
        ///        index in PASSWORD mode (the masked run has one cell per character).
        std::size_t cell_index(std::size_t byte_position) const;

        /// @brief The text (UTF-8 bytes; the caret and the anchor are byte indices).
        std::string _text;
        /// @brief The caret position (a byte index on a code-point boundary).
        std::size_t _cursor = 0;
        /// @brief The selection's other end (equal to _cursor when there is no selection).
        std::size_t _anchor = 0;
        /// @brief How the text is displayed.
        echo_mode_enum _mode = echo_mode_enum::NORMAL;
        /// @brief Whether editing is disabled.
        bool _read_only = false;
        /// @brief Whether this frame had a user edit (the User property).
        bool _edited = false;
        /// @brief How many PIXELS of the display text are scrolled out on the left.
        /// @note Pixels, not cells: with a proportional face every code point has its own advance, so
        ///       "whole cells" does not exist any more. The caret is kept inside the content area by
        ///       moving this offset, and it never goes negative.
        int _offset_pixels = 0;
    };
}

#endif
