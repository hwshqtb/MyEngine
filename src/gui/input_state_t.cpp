/// @file input_state_t.cpp
/// @brief input_state_t: the trivial accessors and the edge bookkeeping (see input_state_t.h for the
///        ownership and frame contract).

#include "hwshqtb/gui/input_state_t.h"

#include <algorithm>
#include <utility>

namespace hwshqtb::gui {
    namespace {
        /// @brief The bit of @p button inside the masks.
        constexpr std::uint8_t mask_of(mouse_button_flag button) noexcept {
            return static_cast<std::uint8_t>(button);
        }
    }

    input_state_t::input_state_t() = default;

    input_state_t::~input_state_t() = default;

    input_state_t::input_state_t(const input_state_t& other) = default;

    input_state_t& input_state_t::operator=(const input_state_t& other) = default;

    input_state_t::input_state_t(input_state_t&& other) noexcept = default;

    input_state_t& input_state_t::operator=(input_state_t&& other) noexcept = default;

    point_t input_state_t::mouse_position() const noexcept {
        return _mouse_position;
    }

    mouse_button_t input_state_t::buttons() const noexcept {
        return mouse_button_t{ static_cast<mouse_button_flag>(_buttons) };
    }

    bool input_state_t::down(mouse_button_flag button) const noexcept {
        const std::uint8_t bit = mask_of(button);
        return bit != 0 && (_buttons & bit) != 0;
    }

    bool input_state_t::pressed(mouse_button_flag button) const noexcept {
        const std::uint8_t bit = mask_of(button);
        return bit != 0 && (_pressed & bit) != 0;
    }

    bool input_state_t::released(mouse_button_flag button) const noexcept {
        const std::uint8_t bit = mask_of(button);
        return bit != 0 && (_released & bit) != 0;
    }

    bool input_state_t::key_down(int key) const noexcept {
        return std::find(_keys.begin(), _keys.end(), key) != _keys.end();
    }

    point_t input_state_t::wheel_delta() const noexcept {
        return _wheel;
    }

    std::string_view input_state_t::text_input() const noexcept {
        return _text_input;
    }

    modifier_t input_state_t::modifiers() const noexcept {
        return _modifiers;
    }

    void input_state_t::reset_frame() noexcept {
        // Only the per-frame data: the levels describe what is still held and must survive.
        _pressed = 0;
        _released = 0;
        _wheel = point_t{};
        _text_input.clear();
    }

    void input_state_t::reset_wheel() noexcept {
        _wheel = point_t{};
    }

    void input_state_t::reset_text_input() noexcept {
        _text_input.clear();
    }

    void input_state_t::set_mouse_position(point_t position) noexcept {
        _mouse_position = position;
    }

    void input_state_t::set_buttons(mouse_button_t buttons) noexcept {
        _buttons = static_cast<std::uint8_t>(buttons.data);
    }

    void input_state_t::set_button(mouse_button_flag button, bool down) noexcept {
        const std::uint8_t bit = mask_of(button);
        if (bit == 0) {
            return;
        }
        if (down) {
            if ((_buttons & bit) == 0) {
                _buttons = static_cast<std::uint8_t>(_buttons | bit);
                _pressed = static_cast<std::uint8_t>(_pressed | bit);
            }
            return;
        }
        if ((_buttons & bit) != 0) {
            _buttons = static_cast<std::uint8_t>(_buttons & ~bit);
            _released = static_cast<std::uint8_t>(_released | bit);
        }
    }

    void input_state_t::add_key(int key) noexcept {
        if (!key_down(key)) {
            _keys.push_back(key);
        }
    }

    void input_state_t::remove_key(int key) noexcept {
        std::erase(_keys, key);
    }

    void input_state_t::clear_keys() noexcept {
        _keys.clear();
    }

    void input_state_t::add_wheel(point_t delta) noexcept {
        _wheel += delta;
    }

    void input_state_t::add_text_input(std::string_view text) {
        _text_input.append(text);
    }

    void input_state_t::set_modifiers(modifier_t modifiers) noexcept {
        _modifiers = modifiers;
    }
}
