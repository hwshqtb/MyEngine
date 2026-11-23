/// @file timer_t.cpp
/// @brief A repeating or one-shot timer object (the QTimer equivalent).

#include "hwshqtb/metaobject/timer_t.h"

#include "hwshqtb/metaobject/event_t.h"

namespace hwshqtb::metaobject {
    timer_t::timer_t(object_t* parent) {
        if (parent != nullptr) {
            set_parent(parent);
        }
    }

    timer_t::~timer_t() {
        stop();
    }

    int timer_t::interval() const noexcept {
        return _interval_ms;
    }

    void timer_t::set_interval(int ms) {
        _interval_ms = (ms > 0) ? ms : 1;
        if (is_active()) {
            start();  // Qt restarts an active timer when its interval changes
        }
    }

    bool timer_t::is_single_shot() const noexcept {
        return _single_shot;
    }

    void timer_t::set_single_shot(bool on) {
        _single_shot = on;
        if (is_active()) {
            start();
        }
    }

    bool timer_t::is_active() const noexcept {
        return _timer_id > 0;
    }

    void timer_t::start() {
        if (_interval_ms <= 0) {
            _interval_ms = 1;
        }
        stop();
        _timer_id = start_timer(_interval_ms, !_single_shot);
    }

    void timer_t::start(int ms) {
        _interval_ms = (ms > 0) ? ms : 1;
        start();
    }

    void timer_t::stop() {
        if (_timer_id > 0) {
            kill_timer(_timer_id);
            _timer_id = 0;
        }
    }

    void timer_t::timer_event(timer_event_t* e) {
        object_t::timer_event(e);
        if (_single_shot) {
            _timer_id = 0;  // the dispatcher already dropped it
        }
        timeout();
    }
}

META_OBJECT(hwshqtb::metaobject::timer_t);