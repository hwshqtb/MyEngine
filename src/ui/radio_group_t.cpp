/// @file radio_group_t.cpp
/// @brief radio_group_t: membership, exclusivity and the two-way detach (see radio_group_t.h).

#include "hwshqtb/ui/radio_group_t.h"

#include <algorithm>
#include <cstddef>

#include "hwshqtb/ui/radio_button_t.h"

namespace hwshqtb::ui {
    radio_group_t::radio_group_t(bool exclusive):
        _exclusive(exclusive) { }

    radio_group_t::~radio_group_t() {
        // The buttons outlive the group: clear their back pointer, so none keeps a dangling group.
        for (radio_button_t* const button : _buttons) {
            if (button != nullptr && button->_group == this) {
                button->_group = nullptr;
            }
        }
    }

    void radio_group_t::add_button(radio_button_t* button) {
        if (button == nullptr || button->_group == this) {
            return;
        }
        if (button->_group != nullptr) {
            button->_group->remove_button(button); // one group per button
        }
        _buttons.push_back(button);
        button->_group = this;
        if (_exclusive && button->_checked) {
            for (radio_button_t* const peer : _buttons) {
                if (peer != button) {
                    peer->uncheck_from_peer();
                }
            }
        }
    }

    void radio_group_t::remove_button(radio_button_t* button) {
        if (button == nullptr || button->_group != this) {
            return;
        }
        const auto found = std::find(_buttons.begin(), _buttons.end(), button);
        if (found != _buttons.end()) {
            _buttons.erase(found);
        }
        button->_group = nullptr;
    }

    radio_button_t* radio_group_t::checked_button() const {
        for (radio_button_t* const button : _buttons) {
            if (button != nullptr && button->_checked) {
                return button;
            }
        }
        return nullptr;
    }

    void radio_group_t::set_exclusive(bool exclusive) {
        _exclusive = exclusive;
    }

    bool radio_group_t::is_exclusive() const {
        return _exclusive;
    }

    std::size_t radio_group_t::button_count() const {
        return _buttons.size();
    }
}
