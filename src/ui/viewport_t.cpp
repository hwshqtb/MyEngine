/// @file viewport_t.cpp
/// @brief viewport_t: the frame, the source-to-destination mapping and the content paint (see
///        viewport_t.h).

#include "hwshqtb/ui/viewport_t.h"

#include <algorithm>

#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/core_application.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/style_t.h"

namespace hwshqtb::ui {
    viewport_t::viewport_t(widget_t* parent):
        widget_t(parent) { }

    void viewport_t::set_content(item_t* content) {
        _content = content;
        if (content == nullptr) {
            return; // the source stays as it is: the next content is shown through the same region
        }
        // The content's own extent is the default source: a viewport that is not scrolled shows the
        // whole content. An explicit set_source_rect() later replaces it.
        if (_source.isEmpty()) {
            const ::hwshqtb::gui::rect_t extent = content->rect();
            _source = ::hwshqtb::gui::rect_t{ 0, 0, extent.width(), extent.height() };
        }
    }

    item_t* viewport_t::content() const noexcept {
        return _content.data();
    }

    void viewport_t::set_source_rect(::hwshqtb::gui::rect_t source) {
        _source = source;
    }

    ::hwshqtb::gui::rect_t viewport_t::source_rect() const noexcept {
        return _source;
    }

    void viewport_t::set_destination_rect(::hwshqtb::gui::rect_t destination) {
        _destination = destination;
        _has_destination = true;
    }

    ::hwshqtb::gui::rect_t viewport_t::destination_rect() const noexcept {
        return _has_destination ? _destination : body_rect();
    }

    ::hwshqtb::gui::rect_t viewport_t::body_rect() const {
        const ::hwshqtb::gui::rect_t bounds = rect();
        const int frame =
            std::max(0, default_style().pixel_metric(pixel_metric_enum::BORDER_WIDTH));
        return ::hwshqtb::gui::rect_t{
            bounds.x() + frame,
            bounds.y() + frame,
            std::max(0, bounds.width() - 2 * frame),
            std::max(0, bounds.height() - 2 * frame),
        };
    }

    ::hwshqtb::gui::rect_t viewport_t::items_rect(const item_t& child) const {
        if (content() != nullptr && &child == content()) {
            return destination_rect(); // the content is shown through the destination and nowhere else
        }
        return widget_t::items_rect(child); // every other child keeps the whole rectangle
    }

    void viewport_t::paint_event(paint_event_t* event) {
        // The viewport's OWN pixels: the frame and the content, the latter with the
        // source-to-destination translation. The item children are traversed by the framework
        // (widget_t::render()) AFTER this returns, exactly like Qt's drawWidget().
        ::hwshqtb::gui::painter_t& painter = event->painter();
        control_state_t state;
        state.enabled = is_enabled();
        default_style().draw_control(control_element_enum::SCROLL_VIEW_VIEWPORT, painter, rect(),
                                     state);
        paint_content(painter);
    }
    const item_t* viewport_t::paint_skip() const noexcept {
        // paint_event() paints the content through the source-to-destination translation, so the framework
        // must not paint it again as an ordinary item child.
        return content();
    }

    void viewport_t::paint_content(::hwshqtb::gui::painter_t& painter) {
        item_t* const shown = content();
        if (shown == nullptr) {
            return;
        }
        const ::hwshqtb::gui::rect_t destination = destination_rect();
        if (destination.isEmpty()) {
            return;
        }
        const ::hwshqtb::gui::rect_t source = source_rect();
        painter.save();
        painter.set_clip(destination);
        // The content is painted through the FRAMEWORK entry (render), which translates by the content's
        // own geometry origin and then traverses its item children. Only the source->destination part is
        // this function's job, so the content's geometry is NOT subtracted here.
        painter.translate(::hwshqtb::gui::point_t{
            destination.x() - source.x(),
            destination.y() - source.y(),
        });
        shown->render(painter, ::hwshqtb::gui::region_t{ painter.clip_rect() });
        painter.restore();
    }

    META_OBJECT(viewport_t);
}
