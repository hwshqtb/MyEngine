/// @file label_t.cpp
/// @brief label_t: the text storage, the aligned text rectangle and the painting (see label_t.h).

#include "hwshqtb/ui/label_t.h"

#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/style_t.h"

namespace hwshqtb::ui {
    label_t::label_t():
        widget_t(nullptr) { }

    label_t::label_t(widget_t* parent):
        widget_t(parent) { }

    void label_t::set_text(std::string_view text) {
        if (_text == text && !_image.has_value()) {
            return;
        }
        _image.reset(); // a label shows text OR an image: taking text drops the picture
        _text.assign(text);
        update(); // the text is drawn content: the frame has to repaint it
        text_changed(_text);
    }

    std::string_view label_t::text() const {
        return _text;
    }

    void label_t::set_image(::hwshqtb::gui::image_t image) {
        _image = std::move(image);
        _text.clear(); // the same exclusivity set_text() enforces from the other side
        if (widget_t* const parent = parent_widget()) {
            parent->relayout(); // the size hints follow the content
        }
        update();
    }

    void label_t::clear_image() {
        if (!_image.has_value()) {
            return;
        }
        _image.reset();
        if (widget_t* const parent = parent_widget()) {
            parent->relayout();
        }
        update();
    }

    const ::hwshqtb::gui::image_t* label_t::image() const noexcept {
        return _image.has_value() ? &*_image : nullptr;
    }

    bool label_t::has_image() const noexcept {
        return _image.has_value();
    }

    bool label_t::scaled_contents() const noexcept {
        return _scaled_contents;
    }

    void label_t::set_scaled_contents(bool scaled) {
        if (_scaled_contents == scaled) {
            return;
        }
        _scaled_contents = scaled;
        update();
    }
    void label_t::set_text_alignment(text_alignment_enum alignment) {
        if (_text_alignment == alignment) {
            return;
        }
        _text_alignment = alignment;
        update(); // the text moved: the frame has to repaint it
    }

    text_alignment_enum label_t::text_alignment() const {
        return _text_alignment;
    }

    ::hwshqtb::gui::extent_t label_t::size_hint() const {
        if (_image.has_value()) {
            return _image->size(); // a picture's natural size is the picture
        }
        const ::hwshqtb::gui::font_metrics_t metrics{};
        const style_t& style = default_style();
        const ::hwshqtb::gui::extent_t text = metrics.text_extent(_text);
        return ::hwshqtb::gui::extent_t{
            text.width() + 2 * style.pixel_metric(pixel_metric_enum::TEXT_PADDING),
            text.height() + 2 * style.pixel_metric(pixel_metric_enum::TEXT_VERTICAL_PADDING) };
    }

    /// @brief The label refuses to be narrower than its text (see the header).
    ::hwshqtb::gui::extent_t label_t::minimum_size_hint() const {
        return size_hint();
    }

    void label_t::paint_event(paint_event_t* event) {
        ::hwshqtb::gui::painter_t& painter = event->painter();
        if (_image.has_value()) {
            // A picture is the label's content: scaled fills the rectangle (QLabel::scaledContents),
            // unscaled draws 1:1 at the top-left with the horizontal text alignment applied.
            const ::hwshqtb::gui::extent_t source = _image->size();
            ::hwshqtb::gui::rect_t target = rect();
            if (!_scaled_contents) {
                target = ::hwshqtb::gui::rect_t{ 0, 0, source.width(), source.height() };
                const int slack = rect().width() - source.width();
                if (slack > 0) {
                    const int offset = _text_alignment == text_alignment_enum::CENTER ? slack / 2
                        : _text_alignment == text_alignment_enum::RIGHT                       ? slack
                                                                                             : 0;
                    target = target.translated(::hwshqtb::gui::point_t{ offset, 0 });
                }
            }
            if (!target.isEmpty()) {
                painter.draw_image(target, *_image);
            }
            return;
        }
        const style_t& style = default_style();
        const ::hwshqtb::gui::rect_t bounds = rect();
        const ::hwshqtb::gui::font_metrics_t metrics{};
        const int text_width = metrics.text_extent(_text).width();
        const int padding = style.pixel_metric(pixel_metric_enum::TEXT_PADDING);
        // The style centres the text in the rectangle it is given, so the control expresses the
        // TEXT alignment by MOVING a text-width rectangle: centring inside it leaves the text where it
        // is. This is placement inside the label, unrelated to the layout alignment that places the
        // whole label in its cell (item_t::alignment()).
        int x = bounds.x() + padding;
        if (_text_alignment == text_alignment_enum::CENTER) {
            x = bounds.x() + (bounds.width() - text_width) / 2;
        }
        else if (_text_alignment == text_alignment_enum::RIGHT) {
            x = bounds.right() - padding - text_width;
        }
        control_state_t state;
        state.enabled = is_enabled();
        state.text = _text;
        style.draw_control(
            control_element_enum::TEXT, painter,
            ::hwshqtb::gui::rect_t{ x, bounds.y(), text_width, bounds.height() }, state);
    }

    META_OBJECT(label_t);
    META_REGISTER_WIDGET(label_t);
}
