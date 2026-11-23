#ifndef HWSHQTB__UI__CHECK_BOX_T_H
#define HWSHQTB__UI__CHECK_BOX_T_H

/// @file check_box_t.h
/// @brief check_box_t: a check box with a style-drawn indicator and a one-line label (the QCheckBox
///        subset of the ui layer).

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
    /// @brief A check box: an indicator plus a one-line label, toggled by a click or by Space.
    ///
    /// @par Persistent state versus the frame's event
    /// checked is PERSISTENT state and has a setter (set_checked()), so a caller may keep the value
    /// itself, write it back every frame and read only toggled — the per-frame result. toggled is the
    /// class's property_trait_flag::USER property: true only on the frame this control changed the
    /// checked state (and false on every other frame, and on a disabled control).
    ///
    /// @par The toggle state machine
    /// The check box is an interactive control, so its focus policy is focus_policy_enum::STRONG_FOCUS
    /// by default (a press focuses it and Tab reaches it).
    ///  - press inside (interaction_t::mouse_pressed && hovered): remember the press, consume;
    ///  - while pressed: keep consuming, so the input does not fall through to the retained tree;
    ///  - release inside: flip checked, emit toggled(checked) once and toggled_changed(true), consume;
    ///  - release outside: cancel the press, flip nothing, consume (the press was ours);
    ///  - Space press while the control holds the focus: flip exactly like a release inside; the
    ///    release edge of Space is consumed too, and every other key falls through;
    ///  - disabled: consume nothing and never flip.
    ///
    /// @par Painting and size
    /// paint_event() draws the indicator and the text through default_style(), which also provides
    /// the indicator size, the spacing, the padding and the row height the size hint uses. The control
    /// names no colour and no pixel value.
    class HWSHQTB_UI_EXPORT check_box_t: public widget_t {
        OBJECT

    public:
        /// @brief The key code of Space (SDL_Keycode 32): with the focus it toggles the check box.
        static constexpr int space_key = 32;

        /// @brief Construct a check box with no visual parent (the class registry's creation path).
        /// @note The text starts empty and the box is unchecked.
        check_box_t();

        /// @brief Construct a check box inside a retained widget tree.
        /// @param parent The visual parent; nullptr makes this a root widget.
        explicit check_box_t(widget_t* parent);

        /// @brief The label.
        /// @note The `text` property; the writer is set_text() and the notifier text_changed().
        [[= ::hwshqtb::metaobject::make_property("text", "set_text", "text_changed")]]
        std::string_view text() const;

        /// @brief Replace the label and emit text_changed() when it actually changes.
        /// @param text The new label; it is copied.
        void set_text(std::string_view text);

        /// @brief Whether the check box is checked (the persistent state, not the frame's event).
        /// @note The `checked` property; the writer is set_checked() and the notifier is toggled().
        [[= ::hwshqtb::metaobject::make_property("checked", "set_checked", "toggled")]]
        bool is_checked() const;

        /// @brief Set the PERSISTENT checked state.
        /// @param checked The new state; this is the setter the external-value model drives.
        /// @note Emits toggled(checked) when the value actually changes (a state notification, whatever
        ///       its origin), and does NOT raise the per-frame result: toggled() stays false, because
        ///       the User event belongs to the user's own action only.
        void set_checked(bool checked);

        /// @brief Whether this frame toggled the checked state.
        /// @return true only on the frame of the flipping release or Space press; false otherwise.
        /// @note The class's User property (property_trait_flag::USER): the immediate host's control()
        ///       returns exactly this value.
        [[= ::hwshqtb::metaobject::make_property("toggled", "", "toggled_changed", "", ::hwshqtb::metaobject::property_trait_flag::USER)]]
        bool toggled() const;

        /// @brief The natural size: the indicator, the spacing and the text, plus the style's padding.
        /// @return {2 * TEXT_PADDING + INDICATOR_SIZE + INDICATOR_SPACING + text width,
        ///          max(LINE_HEIGHT, text height + 2 * TEXT_VERTICAL_PADDING)}.
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief The smallest size the check box accepts: its size_hint().
        /// @note Font derived, like the hint: the indicator plus the text is the content, so shrinking the
        ///       box below it overlaps the caption.
        ::hwshqtb::gui::extent_t minimum_size_hint() const override;

        /// @brief Drive the toggle state machine one frame.
        /// @param in The input snapshot, in this widget's local coordinates (the host maps it).
        /// @return true while the control owns the input: on the press, while held, on the release,
        ///         and on a Space press or release step while it holds the focus. Disabled: false.
        bool interact(const ::hwshqtb::gui::interaction_t& in) override;

        /// @brief Emitted when set_text() actually changes the label.
        /// @param text The new label.
        SIGNAL(void text_changed(std::string_view text) const)

        /// @brief Emitted when the control flips the checked state.
        /// @param checked The new checked state.
        SIGNAL(void toggled(bool checked) const)

        /// @brief Emitted when the per-frame result toggled changes (true on the flip frame).
        /// @param toggled The new value of the User property.
        SIGNAL(void toggled_changed(bool toggled) const)

    protected:
        /// @brief Draw the indicator and the label through default_style().
        /// @param event The paint event; event.painter() is already translated to this widget and
        ///        clipped to rect().
        void paint_event(paint_event_t* event) override;

    private:
        /// @brief The label.
        std::string _text;
        /// @brief The persistent checked state (set_checked() writes it; a release or Space flips it).
        bool _checked = false;
        /// @brief Whether the pointer was inside this control in the last interact().
        bool _hovered = false;
        /// @brief Whether a press inside is being held.
        bool _pressed = false;
        /// @brief Whether the checked state changed in the frame being driven (the User property).
        bool _toggled = false;
    };
}

#endif
