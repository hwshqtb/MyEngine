/// @file group_box_t.cpp
/// @brief group_box_t: the content-area layout and the frame/title painting through default_style()
///        (see group_box_t.h).

#include "hwshqtb/ui/group_box_t.h"

#include <algorithm>

#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/layout_t.h"
#include "hwshqtb/ui/style_t.h"

namespace hwshqtb::ui {
    group_box_t::group_box_t():
        widget_t(nullptr) {
        set_opaque(true); // the frame fills the whole rectangle before the border and the title
    }

    group_box_t::group_box_t(widget_t* parent):
        widget_t(parent) {
        set_opaque(true);
    }

    void group_box_t::set_title(std::string_view title) {
        if (_title == title) {
            return;
        }
        _title.assign(title);
        update(); // the title is drawn content: the frame has to repaint it
        title_changed(_title);
    }

    std::string_view group_box_t::title() const {
        return _title;
    }

    ::hwshqtb::gui::rect_t group_box_t::content_rect() const {
        const style_t& style = default_style();
        const ::hwshqtb::gui::rect_t bounds = rect();
        const int border = style.pixel_metric(pixel_metric_enum::BORDER_WIDTH);
        const int title_height = style.pixel_metric(pixel_metric_enum::TITLE_HEIGHT);
        const int width = std::max(0, bounds.width() - 2 * border);
        const int height = std::max(0, bounds.height() - title_height - border);
        return ::hwshqtb::gui::rect_t{ bounds.x() + border, bounds.y() + title_height, width, height };
    }

    ::hwshqtb::gui::extent_t group_box_t::size_hint() const {
        const style_t& style = default_style();
        const int border = style.pixel_metric(pixel_metric_enum::BORDER_WIDTH);
        const int title_height = style.pixel_metric(pixel_metric_enum::TITLE_HEIGHT);
        const int padding = style.pixel_metric(pixel_metric_enum::TEXT_PADDING);
        const ::hwshqtb::gui::font_metrics_t metrics{};
        const int title_width = metrics.text_extent(_title).width() + 2 * padding;
        // The content area is the layout's business: its hint aggregates the effective hints of the
        // item children, and without a layout there is no aggregate at all.
        const ::hwshqtb::gui::extent_t content =
            layout() != nullptr ? layout()->size_hint() : ::hwshqtb::gui::extent_t{};
        return ::hwshqtb::gui::extent_t{
            std::max(content.width() + 2 * border, title_width),
            content.height() + title_height + border };
    }

    ::hwshqtb::gui::extent_t group_box_t::minimum_size_hint() const {
        const style_t& style = default_style();
        const int border = style.pixel_metric(pixel_metric_enum::BORDER_WIDTH);
        const int title_height = style.pixel_metric(pixel_metric_enum::TITLE_HEIGHT);
        const int padding = style.pixel_metric(pixel_metric_enum::TEXT_PADDING);
        const ::hwshqtb::gui::font_metrics_t metrics{};
        const int title_width = metrics.text_extent(_title).width() + 2 * padding;
        // The floor comes from the same content area: the layout's minimum_size() is the children's
        // aggregate floor, so a child whose minimum_size() rose raises the box's own floor.
        const ::hwshqtb::gui::extent_t content =
            layout() != nullptr ? layout()->minimum_size() : ::hwshqtb::gui::extent_t{};
        return ::hwshqtb::gui::extent_t{
            std::max(content.width() + 2 * border, title_width),
            content.height() + title_height + border };
    }

    void group_box_t::relayout() {
        if (layout() != nullptr) {
            layout()->apply(*this, content_rect());
        }
    }

    void group_box_t::paint_event(paint_event_t* event) {
        ::hwshqtb::gui::painter_t& painter = event->painter();
        const style_t& style = default_style();
        const ::hwshqtb::gui::rect_t bounds = rect();
        control_state_t state;
        state.enabled = is_enabled();
        state.text = _title;
        style.draw_control(control_element_enum::GROUP_BOX_FRAME, painter, bounds, state);

        const int padding = style.pixel_metric(pixel_metric_enum::TEXT_PADDING);
        const int title_height = style.pixel_metric(pixel_metric_enum::TITLE_HEIGHT);
        const ::hwshqtb::gui::font_metrics_t metrics{};
        const int title_width = metrics.text_extent(_title).width();
        style.draw_control(
            control_element_enum::GROUP_BOX_TITLE_TEXT, painter,
            ::hwshqtb::gui::rect_t{ bounds.x() + padding, bounds.y(), title_width, title_height }, state);
    }

    META_OBJECT(group_box_t);
    META_REGISTER_WIDGET(group_box_t);
}
