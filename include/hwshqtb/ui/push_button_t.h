#ifndef HWSHQTB__UI__PUSH_BUTTON_T_H
#define HWSHQTB__UI__PUSH_BUTTON_T_H

/// @file push_button_t.h
/// @brief push_button_t: the first real control of the ui layer (I1) — the proof that ONE control
///        implementation serves the retained tree and the immediate host.
///
/// @par Two hosts, one state machine
/// push_button_t knows nothing about a host. interact() consumes a press inside its rectangle, keeps
/// the press while the button is held, clicks on a release inside and cancels on a release outside;
/// paint_event() draws it. The retained host calls interact()/paint() from its router; ui::im_host_t
/// calls the same two virtuals from its frame handler. The "clicked" result leaves the control
/// through the User property, which is what the immediate host's control() reads: an immediate-mode
/// caller writes
/// @code
/// if (ui.control("push_button_t", "ok").get<bool>()) { ... }
/// @endcode
/// and never sees a pointer.
///
/// @par Why clicked, not activated
/// activated is the ITEM VIEW / combo box vocabulary: select a row or an item and activate it. A
/// button's event is clicked. Using the view family's word for a button was a vocabulary error; the
/// control, its property, its getter and its signals all say clicked now.
///
/// @par Construction
/// There are two constructors and they mean different things:
///  - push_button_t() is what the class registry's create() needs (meta_object_t::new_instance runs a
///    0-argument constructor). The object is a visual root until a host parents it;
///  - push_button_t(widget_t* parent) is the retained-tree form (the constructor parents the widget).
/// There is deliberately no `push_button_t(widget_t* parent = nullptr)` single form: a defaulted
/// parameter is still a REFLECTED parameter, so new_instance() would have to be given an argument,
/// and "create by name" would break.

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
    /// @brief A push button: background + one-pixel border + centred label, with hover/press feedback.
    ///
    /// @par The click state machine
    ///  - press inside (interaction_t::mouse_pressed && hovered): remember the press, consume the input;
    ///  - while pressed: keep consuming, so the input does not fall through to the retained tree;
    ///  - release inside: the button CLICKS for that one frame and emits clicked_changed(true); consume;
    ///  - release outside: cancel, emit nothing, consume (the press was ours, so the release is too).
    /// A disabled button consumes nothing and can never click.
    ///
    /// @par Keyboard
    /// The button is an interactive control, so its focus policy is
    /// focus_policy_enum::STRONG_FOCUS by default (a press focuses it and Tab reaches it). While it
    /// holds the focus, a Space or Return press step counts as one complete press-and-release: the
    /// button clicks once (clicked() is true and clicked_changed(true) fires) and consumes the
    /// step. A disabled button consumes nothing. Any other key, and every key step without the
    /// focus, returns false so the step falls through — Tab is never this control's business.
    ///
    /// @par The User property
    /// clicked is the class's property_trait_flag::USER property: true only on the frame the button
    /// was clicked, false every other frame. That one bool is the whole immediate-mode result; a
    /// host that has access to the object can read text as well, but the frame's RESULT is clicked.
    /// It is a command event: no value property links it, so the state store records no state for a
    /// button.
    ///
    /// @par Painting
    /// paint_event() asks default_style() for the button frame and for the text, so it names no colour
    /// and no pixel value; the painter is already translated to this widget and clipped to its
    /// rectangle by widget_t::paint().
    class HWSHQTB_UI_EXPORT push_button_t: public widget_t {
        OBJECT

    public:
        /// @brief The key code of Space (SDL_Keycode 32): with the focus it clicks the button.
        static constexpr int space_key = 32;
        /// @brief The key code of Return (SDL_Keycode 13): with the focus it clicks the button.
        static constexpr int return_key = 13;

        /// @brief Construct a button with no visual parent (the class registry's creation path).
        /// @note The label starts as "Button". A host that creates the button through
        ///       object_t::create() parents it afterwards (object_t::create(..., parent)).
        push_button_t();

        /// @brief Construct a button inside a retained widget tree.
        /// @param parent The visual parent; nullptr makes this a root widget.
        explicit push_button_t(widget_t* parent);

        /// @brief The label.
        /// @note The `text` property; the writer is set_text() and the notifier text_changed().
        [[= ::hwshqtb::metaobject::make_property("text", "set_text", "text_changed")]]
        std::string_view text() const;

        /// @brief Replace the label and emit text_changed() when it actually changes.
        /// @param text The new label; it is copied.
        void set_text(std::string_view text);

        /// @brief Whether the button was clicked by the frame that drove this control.
        /// @return true only on the frame of the clicking release (or Space/Return); false afterwards.
        /// @note The class's User property (property_trait_flag::USER): the immediate host's
        ///       control() returns exactly this value, and at most one property per class may carry the
        ///       trait (enforced at compile time by build_property_infos).
        /// @note The notifier is the clicked event itself: the property IS that event's per-frame
        ///       result.
        [[= ::hwshqtb::metaobject::make_property("clicked", "", "clicked_changed", "", ::hwshqtb::metaobject::property_trait_flag::USER)]]
        bool clicked() const;

        /// @brief The natural size derived from the label: text width plus two paddings on the
        ///        horizontal axis, the fixed button height on the vertical axis.
        /// @return {2 * padding + text.size() * font advance, fixed height}.
        /// @note This is the button's OPINION. A layout asks item_t::effective_size_hint(), which
        ///       lets set_fixed_size() win and clamps to the minimum, so the button never has to
        ///       know about constraints.
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief The smallest size the button accepts: its size_hint().
        /// @note Font derived, like the hint: the text is the content, so the layout must not squeeze the
        ///       button narrower than it (that is what made button captions overlap their neighbours).
        ::hwshqtb::gui::extent_t minimum_size_hint() const override;

        /// @brief Drive the click state machine one frame.
        /// @param in The input snapshot, in this widget's local coordinates (the host maps it).
        /// @return true while the button owns the input: on the press, while held, on the release
        ///         (whether it clicked or cancelled), and on a Space/Return press step while it
        ///         holds the focus. A disabled button returns false.
        bool interact(const ::hwshqtb::gui::interaction_t& in) override;

        /// @brief Emitted when set_text() actually changes the label.
        /// @param text The new label.
        SIGNAL(void text_changed(std::string_view text) const)

        /// @brief Emitted when the per-frame clicked result changes: true on the click frame, false
        ///        when it clears.
        /// @param clicked The new value of the User property; defaults to true, so a call without an
        ///        argument means "clicked".
        /// @note This one signal is both the click event and the notifier of the clicked User
        ///       property. A click raises clicked_changed(true); the following driven frame raises
        ///       clicked_changed(false) when the per-frame result clears.
        SIGNAL(void clicked_changed(bool clicked = true) const)

    protected:
        /// @brief Draw the button frame and the label through default_style().
        /// @param event The paint event; event.painter() is already translated to this widget and
        ///        clipped to rect().
        void paint_event(paint_event_t* event) override;

    private:
        /// @brief The label.
        std::string _text = "Button";
        /// @brief Whether a press inside is being held.
        bool _pressed = false;
        /// @brief Whether the pointer was inside this button in the last interact().
        bool _hovered = false;
        /// @brief Whether the button was clicked in the frame being driven.
        bool _clicked = false;
    };
}

#endif
