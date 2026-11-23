#include "hwshqtb/gui/window_event_t.h"

#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"

namespace hwshqtb::gui {
    paint_event_t::paint_event_t(painter_t& painter, ::hwshqtb::gui::region_t damage) noexcept:
        ::hwshqtb::metaobject::event_t(::hwshqtb::metaobject::event_type_enum::PAINT),
        _painter(painter),
        _damage(std::move(damage)) { }

    painter_t& paint_event_t::painter() const noexcept {
        return _painter;
    }

    const ::hwshqtb::gui::region_t& paint_event_t::damage() const noexcept {
        return _damage;
    }

    interact_event_t::interact_event_t(const interaction_t& step) noexcept:
        // UI_PRESS is the wrong flavour; the step is not a state change but input. The core
        // USER_INPUT family is what a window-level step is, and consumers dispatch on the type code.
        ::hwshqtb::metaobject::event_t(::hwshqtb::metaobject::event_type_enum::USER),
        _step(step) { }

    const interaction_t& interact_event_t::step() const noexcept {
        return _step;
    }

    close_event_t::close_event_t() noexcept:
        ::hwshqtb::metaobject::event_t(::hwshqtb::metaobject::event_type_enum::CLOSE) { }

    focus_out_event_t::focus_out_event_t() noexcept:
        ::hwshqtb::metaobject::event_t(::hwshqtb::metaobject::event_type_enum::FOCUS_OUT) { }

    leave_event_t::leave_event_t() noexcept:
        ::hwshqtb::metaobject::event_t(::hwshqtb::metaobject::event_type_enum::LEAVE) { }

    resize_event_t::resize_event_t(extent_t old_size, extent_t size) noexcept:
        ::hwshqtb::metaobject::event_t(::hwshqtb::metaobject::event_type_enum::RESIZE),
        _old_size(old_size),
        _size(size) { }

    extent_t resize_event_t::old_size() const noexcept {
        return _old_size;
    }

    extent_t resize_event_t::size() const noexcept {
        return _size;
    }

    bool resize_event_t::merge_with(const ::hwshqtb::metaobject::event_t& newer) {
        const auto* later = dynamic_cast<const resize_event_t*>(&newer);
        if (later == nullptr) {
            return false;
        }
        // Keep the earliest old size: it is the size before the first pending change.
        _size = later->_size;
        return true;
    }

    move_event_t::move_event_t(point_t old_position, point_t position) noexcept:
        ::hwshqtb::metaobject::event_t(::hwshqtb::metaobject::event_type_enum::MOVE),
        _old_position(old_position),
        _position(position) { }

    point_t move_event_t::old_position() const noexcept {
        return _old_position;
    }

    point_t move_event_t::position() const noexcept {
        return _position;
    }

    bool move_event_t::merge_with(const ::hwshqtb::metaobject::event_t& newer) {
        const auto* later = dynamic_cast<const move_event_t*>(&newer);
        if (later == nullptr) {
            return false;
        }
        // Keep the earliest old position: it is the position before the first pending change.
        _position = later->_position;
        return true;
    }

    expose_event_t::expose_event_t() noexcept:
        ::hwshqtb::metaobject::event_t(::hwshqtb::metaobject::event_type_enum::EXPOSE) { }
}
