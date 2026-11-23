/// @file radio_button_t.cpp
/// @brief radio_button_t: the two-level selection state machine and the painting through
///        default_style() (see radio_button_t.h).

#include "hwshqtb/ui/radio_button_t.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/radio_group_t.h"
#include "hwshqtb/ui/style_t.h"

namespace hwshqtb::ui {
    namespace {
        /// @brief The radio button an object is, or nullptr for anything else.
        /// @param object The object to test (a sibling of some radio button).
        /// @return The object as a radio_button_t; the meta-object chain owns the identity test, so
        ///         the static_cast is safe (the same pattern as object_t::is_widget()).
        radio_button_t* radio_of(::hwshqtb::metaobject::object_t* object) {
            if (object == nullptr || !object->is_widget()) {
                return nullptr;
            }
            if (!object->meta_object()->inherits(&radio_button_t::static_meta_object)) {
                return nullptr;
            }
            return static_cast<radio_button_t*>(object);
        }
    }

    radio_button_t::radio_button_t():
        widget_t(nullptr) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
    }

    radio_button_t::radio_button_t(widget_t* parent):
        widget_t(parent) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
    }

    radio_button_t::~radio_button_t() {
        if (_group != nullptr) {
            _group->remove_button(this); // the group drops its pointer to this dying button
        }
    }

    void radio_button_t::set_text(std::string_view text) {
        if (_text == text) {
            return;
        }
        _text.assign(text);
        update(); // the label is drawn content: the frame has to repaint it
        text_changed(_text);
    }

    std::string_view radio_button_t::text() const {
        return _text;
    }

    void radio_button_t::set_checked(bool checked) {
        if (_checked == checked) {
            return;
        }
        _checked = checked;
        update(); // the indicator is drawn content: the frame has to repaint it
        if (checked) {
            uncheck_exclusive_peers();
        }
        toggled(checked); // state notification: the User event stays the user's own action
    }

    bool radio_button_t::is_checked() const {
        return _checked;
    }

    void radio_button_t::set_auto_exclusive(bool auto_exclusive) {
        _auto_exclusive = auto_exclusive;
    }

    bool radio_button_t::is_auto_exclusive() const {
        return _auto_exclusive;
    }

    radio_group_t* radio_button_t::group() const {
        return _group;
    }

    bool radio_button_t::toggled() const {
        return _toggled;
    }

    ::hwshqtb::gui::extent_t radio_button_t::size_hint() const {
        const style_t& style = default_style();
        const ::hwshqtb::gui::font_metrics_t metrics{};
        const ::hwshqtb::gui::extent_t text = metrics.text_extent(_text);
        const int width = 2 * style.pixel_metric(pixel_metric_enum::TEXT_PADDING)
            + style.pixel_metric(pixel_metric_enum::INDICATOR_SIZE)
            + style.pixel_metric(pixel_metric_enum::INDICATOR_SPACING) + text.width();
        const int height =
            std::max(style.pixel_metric(pixel_metric_enum::LINE_HEIGHT),
                     text.height() + 2 * style.pixel_metric(pixel_metric_enum::TEXT_VERTICAL_PADDING));
        return ::hwshqtb::gui::extent_t{ width, height };
    }

    /// @brief The radio button refuses to be narrower than its indicator plus text (see the header).
    ::hwshqtb::gui::extent_t radio_button_t::minimum_size_hint() const {
        return size_hint();
    }

    void radio_button_t::select(bool raise_frame_event) {
        if (_checked) {
            return; // Qt: selecting an already selected radio keeps it and emits nothing
        }
        _checked = true;
        if (raise_frame_event) {
            _toggled = true;
        }
        update(); // the indicator is drawn content: the frame has to repaint it
        uncheck_exclusive_peers();
        toggled(true);
    }

    void radio_button_t::uncheck_from_peer() {
        if (!_checked) {
            return;
        }
        _checked = false;
        update(); // the indicator is drawn content: the frame has to repaint it
        toggled(false);
    }

    void radio_button_t::uncheck_exclusive_peers() {
        if (_group != nullptr) {
            // The explicit group wins: the same-parent automatic set is not consulted at all.
            if (!_group->_exclusive) {
                return;
            }
            for (radio_button_t* const peer : _group->_buttons) {
                if (peer != this) {
                    peer->uncheck_from_peer();
                }
            }
            return;
        }
        if (!_auto_exclusive) {
            return;
        }
        const ::hwshqtb::metaobject::object_t* const shared_parent = parent();
        if (shared_parent == nullptr) {
            return;
        }
        for (::hwshqtb::metaobject::object_t* const child : shared_parent->children()) {
            radio_button_t* const peer = radio_of(child);
            if (peer == nullptr || peer == this) {
                continue;
            }
            if (!peer->_auto_exclusive || peer->_group != nullptr) {
                continue; // a button that opted out, or one the explicit group owns, is not ours
            }
            peer->uncheck_from_peer();
        }
    }

    radio_button_t* radio_button_t::automatic_sibling(bool backwards) const {
        if (_group != nullptr || !_auto_exclusive) {
            return nullptr; // no automatic set for this button
        }
        const ::hwshqtb::metaobject::object_t* const shared_parent = parent();
        if (shared_parent == nullptr) {
            return nullptr;
        }
        std::vector<radio_button_t*> peers;
        for (::hwshqtb::metaobject::object_t* const child : shared_parent->children()) {
            radio_button_t* const peer = radio_of(child);
            if (peer != nullptr && peer->_auto_exclusive && peer->_group == nullptr) {
                peers.push_back(peer);
            }
        }
        if (peers.empty()) {
            return nullptr;
        }
        const auto found = std::find(peers.begin(), peers.end(), this);
        if (found == peers.end()) {
            return nullptr;
        }
        const std::size_t index = static_cast<std::size_t>(found - peers.begin());
        const std::size_t count = peers.size();
        const std::size_t next =
            backwards ? (index + count - 1) % count : (index + 1) % count;
        return peers[next];
    }

    bool radio_button_t::interact(const ::hwshqtb::gui::interaction_t& in) {
        const bool was_toggled = _toggled;
        const bool was_hovered = _hovered;
        const bool was_pressed = _pressed;
        // toggled is a PER-FRAME result: it is cleared by the next frame that drives the control.
        _toggled = false;
        _hovered = in.hovered;

        if (!is_enabled()) {
            _pressed = false;
            if (was_toggled) {
                toggled_changed(false);
            }
            if (_hovered != was_hovered || _pressed != was_pressed) {
                update(); // the appearance changed: record what has to be repainted
            }
            return false;
        }

        bool consumed = false;
        if (in.key != 0) {
            // A keyboard step: its mouse fields are only context.
            if (has_focus()) {
                if (in.key == ::hwshqtb::gui::interaction_t::space_key) {
                    consumed = true;
                    if (in.key_pressed) {
                        select(true);
                    }
                }
                else if (in.key == ::hwshqtb::gui::interaction_t::left_key
                         || in.key == ::hwshqtb::gui::interaction_t::up_key
                         || in.key == ::hwshqtb::gui::interaction_t::right_key
                         || in.key == ::hwshqtb::gui::interaction_t::down_key) {
                    const bool backwards = in.key == ::hwshqtb::gui::interaction_t::left_key
                        || in.key == ::hwshqtb::gui::interaction_t::up_key;
                    if (radio_button_t* const target = automatic_sibling(backwards)) {
                        // The arrows are this control's only when it HAS an automatic set; outside
                        // one the step falls through to the host.
                        consumed = true;
                        if (in.key_pressed && target != this) {
                            target->select(false); // the target's frame event belongs to no driven call
                            target->set_focus();   // the focus travels with the selection
                        }
                    }
                }
            }
        }
        else {
            if (in.mouse_pressed && in.hovered) {
                _pressed = true;
                consumed = true;
            }
            if (_pressed) {
                consumed = true; // keep the capture while held
            }
            if (in.mouse_released && _pressed) {
                const bool inside = in.hovered;
                _pressed = false;
                consumed = true;
                if (inside) {
                    select(true);
                }
            }
        }

        if (_hovered != was_hovered || _pressed != was_pressed) {
            update(); // hover/press feedback lives in the pixels: repaint this control
        }
        if (_toggled != was_toggled) {
            toggled_changed(_toggled);
        }
        return consumed;
    }

    void radio_button_t::paint_event(paint_event_t* event) {
        ::hwshqtb::gui::painter_t& painter = event->painter();
        const style_t& style = default_style();
        const ::hwshqtb::gui::rect_t bounds = rect();
        const int padding = style.pixel_metric(pixel_metric_enum::TEXT_PADDING);
        const int indicator_size = style.pixel_metric(pixel_metric_enum::INDICATOR_SIZE);
        const ::hwshqtb::gui::rect_t indicator{ bounds.x() + padding,
                                                bounds.y() + (bounds.height() - indicator_size) / 2,
                                                indicator_size, indicator_size };

        control_state_t state;
        state.enabled = is_enabled();
        state.hovered = is_hovered();
        state.pressed = is_pressed(::hwshqtb::gui::mouse_button_enum::LEFT);
        state.focused = has_focus();
        state.checked = _checked;
        style.draw_control(control_element_enum::RADIO_INDICATOR, painter, indicator, state);

        const ::hwshqtb::gui::font_metrics_t metrics{};
        const int text_width = metrics.text_extent(_text).width();
        state.text = _text;
        style.draw_control(
            control_element_enum::TEXT, painter,
            ::hwshqtb::gui::rect_t{ indicator.right()
                                        + style.pixel_metric(pixel_metric_enum::INDICATOR_SPACING),
                                    bounds.y(), text_width, bounds.height() },
            state);
    }

    META_OBJECT(radio_button_t);
    META_REGISTER_WIDGET(radio_button_t);
}
