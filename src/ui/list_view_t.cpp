/// @file list_view_t.cpp
/// @brief list_view_t: the list's one row-content answer — the DISPLAY text of column 0 — and its
///        natural size (see list_view_t.h).
#include "hwshqtb/ui/list_view_t.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/style_t.h"

namespace hwshqtb::ui {
    list_view_t::list_view_t(widget_t* parent):
        abstract_item_view_t(parent) {
    }

    ::hwshqtb::gui::extent_t list_view_t::size_hint() const {
        const style_t& style = default_style();
        return ::hwshqtb::gui::extent_t{
            style.pixel_metric(pixel_metric_enum::LIST_VIEW_WIDTH),
            style.pixel_metric(pixel_metric_enum::LIST_VIEW_HEIGHT),
        };
    }

    void list_view_t::paint_row(::hwshqtb::gui::painter_t& painter, int row,
                                ::hwshqtb::gui::rect_t rect) const {
        abstract_item_view_t::paint_row(painter, row, rect);
        std::string owned_text;
        std::string_view text;
        const item_model_t* const source = model();
        if (source != nullptr) {
            const ::hwshqtb::metaobject::any_t value =
                source->data(row, 0, item_role_enum::DISPLAY);
            if (value.is_initialized()) {
                if (value.is<std::string_view>()) {
                    text = value.get<std::string_view>();
                }
                else if (value.is<std::string>()) {
                    owned_text = value.get<std::string>();
                    text = owned_text;
                }
            }
        }
        if (text.empty()) {
            return;
        }
        const int padding = default_style().pixel_metric(pixel_metric_enum::TEXT_PADDING);
        const ::hwshqtb::gui::rect_t text_rect{
            rect.x() + padding,
            rect.y(),
            std::max(0, rect.width() - 2 * padding),
            rect.height(),
        };
        control_state_t state;
        state.enabled = is_enabled();
        state.text = text;
        painter.save();
        painter.set_clip(text_rect);
        default_style().draw_control(control_element_enum::LIST_TEXT, painter, text_rect, state);
        painter.restore();
    }

    META_OBJECT(list_view_t);
    META_REGISTER_WIDGET(list_view_t);
}
