/// @file progress_bar_t.cpp
/// @brief progress_bar_t: the clamped value range and the scaled chunk painting (see progress_bar_t.h).

#include "hwshqtb/ui/progress_bar_t.h"

#include <algorithm>
#include <utility>

#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/style_t.h"

namespace hwshqtb::ui {
    progress_bar_t::progress_bar_t():
        widget_t(nullptr) { }

    progress_bar_t::progress_bar_t(widget_t* parent):
        widget_t(parent) { }

    void progress_bar_t::set_range(int minimum, int maximum) {
        if (maximum < minimum) {
            std::swap(minimum, maximum);
        }
        _minimum = minimum;
        _maximum = maximum;
        if (_value < _minimum || _value > _maximum) {
            _value = std::clamp(_value, _minimum, _maximum);
            update(); // the filled part changed: the frame has to repaint it
            value_changed(_value);
        }
    }

    int progress_bar_t::minimum() const {
        return _minimum;
    }

    int progress_bar_t::maximum() const {
        return _maximum;
    }

    void progress_bar_t::set_value(int value) {
        const int clamped = std::clamp(value, _minimum, _maximum);
        if (clamped == _value) {
            return;
        }
        _value = clamped;
        update(); // the filled part is drawn content: the frame has to repaint it
        value_changed(_value);
    }

    int progress_bar_t::value() const {
        return _value;
    }

    void progress_bar_t::set_direction(orientation_enum direction) {
        if (_direction == direction) {
            return;
        }
        _direction = direction;
        update(); // the groove changes axis: the frame has to repaint it
    }

    orientation_enum progress_bar_t::direction() const {
        return _direction;
    }

    ::hwshqtb::gui::extent_t progress_bar_t::size_hint() const {
        const style_t& style = default_style();
        const int thickness = style.pixel_metric(pixel_metric_enum::PROGRESS_GROOVE_HEIGHT)
            + 2 * style.pixel_metric(pixel_metric_enum::BORDER_WIDTH);
        const int length = style.pixel_metric(pixel_metric_enum::PROGRESS_BAR_WIDTH);
        return _direction == orientation_enum::HORIZONTAL
            ? ::hwshqtb::gui::extent_t{ length, thickness }
            : ::hwshqtb::gui::extent_t{ thickness, length };
    }

    void progress_bar_t::paint_event(paint_event_t* event) {
        ::hwshqtb::gui::painter_t& painter = event->painter();
        const style_t& style = default_style();
        const ::hwshqtb::gui::rect_t bounds = rect();
        const int thickness = style.pixel_metric(pixel_metric_enum::PROGRESS_GROOVE_HEIGHT);
        const int border = style.pixel_metric(pixel_metric_enum::BORDER_WIDTH);

        // The groove spans the long axis and is centred on the short one, at most `thickness` thick.
        ::hwshqtb::gui::rect_t groove;
        if (_direction == orientation_enum::HORIZONTAL) {
            const int height = std::min(bounds.height(), thickness);
            groove = ::hwshqtb::gui::rect_t{ bounds.x(), bounds.y() + (bounds.height() - height) / 2,
                                             bounds.width(), height };
        }
        else {
            const int width = std::min(bounds.width(), thickness);
            groove = ::hwshqtb::gui::rect_t{ bounds.x() + (bounds.width() - width) / 2, bounds.y(),
                                             width, bounds.height() };
        }

        control_state_t state;
        state.enabled = is_enabled();
        style.draw_control(control_element_enum::PROGRESS_GROOVE, painter, groove, state);

        const ::hwshqtb::gui::rect_t inner{ groove.x() + border, groove.y() + border,
                                            groove.width() - 2 * border,
                                            groove.height() - 2 * border };
        if (inner.isEmpty() || _maximum <= _minimum || _value <= _minimum) {
            return; // no room inside the border, an empty value range, or a zero value
        }
        // 64-bit intermediate: inner extent * offset can exceed int for a wide bar and a fine range.
        const std::int64_t span = static_cast<std::int64_t>(_maximum) - _minimum;
        const std::int64_t offset = static_cast<std::int64_t>(_value) - _minimum;
        ::hwshqtb::gui::rect_t chunk;
        if (_direction == orientation_enum::HORIZONTAL) {
            const int chunk_width =
                static_cast<int>(static_cast<std::int64_t>(inner.width()) * offset / span);
            chunk = ::hwshqtb::gui::rect_t{ inner.x(), inner.y(), chunk_width, inner.height() };
        }
        else {
            const int chunk_height =
                static_cast<int>(static_cast<std::int64_t>(inner.height()) * offset / span);
            chunk = ::hwshqtb::gui::rect_t{ inner.x(), inner.bottom() - chunk_height, inner.width(),
                                            chunk_height };
        }
        if (!chunk.isEmpty()) {
            style.draw_control(control_element_enum::PROGRESS_CHUNK, painter, chunk, state);
        }
    }

    META_OBJECT(progress_bar_t);
    META_REGISTER_WIDGET(progress_bar_t);
}
