/// @file signal_blocker_t.cpp
/// @brief RAII scope that suppresses signal emission from an object.

#include "hwshqtb/metaobject/signal_blocker_t.h"

namespace hwshqtb::metaobject {
    signal_blocker_t::signal_blocker_t(object_t* object) noexcept:
        _object(object) {
        if (_object != nullptr) {
            _object->block_signals(true);
            _blocked = true;
        }
    }

    signal_blocker_t::~signal_blocker_t() {
        unblock();
    }

    signal_blocker_t::signal_blocker_t(signal_blocker_t&& other) noexcept:
        _object(other._object), _blocked(other._blocked) {
        other._object = nullptr;
        other._blocked = false;
    }

    signal_blocker_t& signal_blocker_t::operator=(signal_blocker_t&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        unblock();                // release our level first (counter-aware)
        _object = other._object;  // adopt the source's level
        _blocked = other._blocked;
        other._object = nullptr;  // leave the source inert
        other._blocked = false;
        return *this;
    }

    object_t* signal_blocker_t::blocker_object() const noexcept {
        return _object;
    }

    void signal_blocker_t::unblock() noexcept {
        if (_blocked) {
            _object->block_signals(false);
            _blocked = false;
        }
    }

    void signal_blocker_t::reblock() noexcept {
        if (!_blocked && _object != nullptr) {
            _object->block_signals(true);
            _blocked = true;
        }
    }

    void signal_blocker_t::dismiss() noexcept {
        _blocked = false;
    }
}
