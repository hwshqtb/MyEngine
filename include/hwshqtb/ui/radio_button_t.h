#ifndef HWSHQTB__UI__RADIO_BUTTON_T_H
#define HWSHQTB__UI__RADIO_BUTTON_T_H

/// @file radio_button_t.h
/// @brief radio_button_t: a radio button with the two-level exclusivity of Qt (same-parent automatic
///        exclusivity plus the explicit radio_group_t from radio_group_t.h).

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
    class radio_group_t;

    /// @brief A radio button: an indicator plus a one-line label, selecting itself on a click, on
    ///        Space and by the arrow keys among its automatic siblings.
    ///
    /// @par Two levels of exclusivity
    ///  1. Automatic (Qt's autoExclusive, on by default): the radio buttons sharing one object parent
    ///     are mutually exclusive. Selecting one clears every sibling that is also automatic (its own
    ///     auto_exclusive() is true and it belongs to no group) and each cleared sibling emits
    ///     toggled(false) — the state is never changed silently. set_auto_exclusive(false) opts a
    ///     button out.
    ///  2. Explicit (radio_group_t): a group ties buttons that may live under DIFFERENT parents
    ///     together. A button in a group is constrained by the group only and takes no part in the
    ///     same-parent automatic exclusivity, so the explicit group always wins.
    ///
    /// @par Persistent state versus the frame's event
    /// checked is PERSISTENT and has a setter (set_checked()); toggled is the class's
    /// property_trait_flag::USER property and is true only on the frame this control was selected by
    /// the user. A button that a PEER cleared (or that an arrow key moved the selection to) changes
    /// its checked state and emits toggled() but does not raise its own per-frame result: only the
    /// driven control reports a frame event.
    ///
    /// @par Selection rules
    ///  - a click inside (press and release) selects the button; clicking an ALREADY selected button
    ///    keeps it selected and emits nothing (Qt);
    ///  - Space with the focus selects it;
    ///  - Left/Up/Right/Down with the focus move the selection and the focus to the previous/next
    ///    automatic sibling (wrapping, child order); a button inside an explicit group has no
    ///    automatic siblings, so there the arrows fall through;
    ///  - set_checked(true) makes the button checked and clears its exclusive peers, but is silent for
    ///    the button itself (the caller owns the value — the frame's event comes from the user's
    ///    action);
    ///  - a disabled button consumes nothing and never changes.
    ///
    /// @par Painting and size
    /// paint_event() draws the indicator and the text through default_style(), which also provides the
    /// indicator size, the spacing, the padding and the row height the size hint uses.
    class HWSHQTB_UI_EXPORT radio_button_t: public widget_t {
        OBJECT

    public:
        /// @brief Construct a radio button with no visual parent (the class registry's creation path).
        /// @note The text starts empty, the button is unchecked, automatic exclusivity is on and the
        ///       focus policy is focus_policy_enum::STRONG_FOCUS.
        radio_button_t();

        /// @brief Construct a radio button inside a retained widget tree.
        /// @param parent The visual parent; nullptr makes this a root widget.
        /// @note The focus policy is focus_policy_enum::STRONG_FOCUS (an interactive control).
        explicit radio_button_t(widget_t* parent);

        /// @brief Detach from the explicit group; a group must never keep a pointer to a dead button.
        ~radio_button_t() override;

        /// @brief The label.
        /// @note The `text` property; the writer is set_text() and the notifier text_changed().
        [[= ::hwshqtb::metaobject::make_property("text", "set_text", "text_changed")]]
        std::string_view text() const;

        /// @brief Replace the label and emit text_changed() when it actually changes.
        /// @param text The new label; it is copied.
        void set_text(std::string_view text);

        /// @brief Whether the button is checked (the persistent state, not the frame's event).
        /// @note The `checked` property; the writer is set_checked() and the notifier is toggled().
        /// @note Restoring writes through set_checked(), so an ON value also clears the exclusive
        ///       peers, exactly like any other checked write.
        [[= ::hwshqtb::metaobject::make_property("checked", "set_checked", "toggled")]]
        bool is_checked() const;

        /// @brief Set the PERSISTENT checked state and clear the exclusive peers when it turns on.
        /// @param checked The new state.
        /// @note Turning the button ON clears the peers it is exclusive with (each emits
        ///       toggled(false)).
        /// @note Emits toggled(checked) for this button when the value actually changes (a state
        ///       notification, whatever its origin), and never raises a per-frame result: the User
        ///       event belongs to the user's own action only. Turning it OFF clears only this button
        ///       (a group may then have no checked member).
        void set_checked(bool checked);

        /// @brief Whether the same-parent automatic exclusivity is on (the default).
        bool is_auto_exclusive() const;

        /// @brief Enable or disable the same-parent automatic exclusivity.
        /// @param auto_exclusive The new flag (on by default).
        /// @note It has no effect while the button belongs to a radio_group_t: the group wins.
        void set_auto_exclusive(bool auto_exclusive);

        /// @brief The explicit group this button belongs to, or nullptr.
        radio_group_t* group() const;

        /// @brief Whether this frame selected the button.
        /// @return true only on the frame a click, Space or an arrow-key move selected THIS button;
        ///         false on every other frame.
        /// @note The class's User property (property_trait_flag::USER): the immediate host's control()
        ///       returns exactly this value.
        [[= ::hwshqtb::metaobject::make_property("toggled", "", "toggled_changed", "", ::hwshqtb::metaobject::property_trait_flag::USER)]]
        bool toggled() const;

        /// @brief The natural size: the indicator, the spacing and the text, plus the style's padding.
        /// @return {2 * TEXT_PADDING + INDICATOR_SIZE + INDICATOR_SPACING + text width,
        ///          max(LINE_HEIGHT, text height + 2 * TEXT_VERTICAL_PADDING)}.
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief The smallest size the radio button accepts: its size_hint().
        /// @note Font derived, like the hint: the indicator plus the text is the content, so shrinking it
        ///       below that overlaps the caption.
        ::hwshqtb::gui::extent_t minimum_size_hint() const override;

        /// @brief Drive the selection state machine one frame.
        /// @param in The input snapshot, in this widget's local coordinates (the host maps it).
        /// @return true while the control owns the input: on the press, while held, on the release, and
        ///         on a Space or arrow step while it holds the focus. Disabled: false.
        bool interact(const ::hwshqtb::gui::interaction_t& in) override;

        /// @brief Emitted when set_text() actually changes the label.
        /// @param text The new label.
        SIGNAL(void text_changed(std::string_view text) const)

        /// @brief Emitted when the checked state changes (by this button or by a peer clearing it).
        /// @param checked The new checked state.
        SIGNAL(void toggled(bool checked) const)

        /// @brief Emitted when the per-frame result toggled changes (true on this button's frame).
        /// @param toggled The new value of the User property.
        SIGNAL(void toggled_changed(bool toggled) const)

    protected:
        /// @brief Draw the indicator and the label through default_style().
        /// @param event The paint event; event.painter() is already translated to this widget and
        ///        clipped to rect().
        void paint_event(paint_event_t* event) override;

    private:
        /// @brief Select the button and clear its exclusive peers.
        /// @param raise_frame_event true for a user action on THIS button (the User property turns
        ///        true); false for the arrow-key target, whose frame event belongs to no driven call.
        void select(bool raise_frame_event);

        /// @brief Clear the button because an exclusive peer was selected; emits toggled(false).
        void uncheck_from_peer();

        /// @brief Clear the exclusive peers of a button that just became checked.
        void uncheck_exclusive_peers();

        /// @brief The automatic sibling to move the selection to, or nullptr when this button has no
        ///        automatic set (it is in a group or auto_exclusive() is off).
        radio_button_t* automatic_sibling(bool backwards) const;

        /// @brief The group this button belongs to, or nullptr (the group writes it on add/remove).
        radio_group_t* _group = nullptr;
        /// @brief The label.
        std::string _text;
        /// @brief The persistent checked state.
        bool _checked = false;
        /// @brief Whether the pointer was inside this control in the last interact().
        bool _hovered = false;
        /// @brief Whether a press inside is being held.
        bool _pressed = false;
        /// @brief Whether this frame selected the button (the User property).
        bool _toggled = false;
        /// @brief Whether same-parent automatic exclusivity applies (on by default).
        bool _auto_exclusive = true;

        /// @brief The group manages membership (and may read/write the state it enforces).
        friend class radio_group_t;
    };
}

#endif
