#include "hwshqtb/ui/layout_t.h"

namespace hwshqtb::ui {
    void layout_t::apply(widget_t&, ::hwshqtb::gui::rect_t) {
        // Default: arrange nothing. A layout whose rule is not known yet is still a legal layout.
    }

    ::hwshqtb::gui::extent_t layout_t::size_hint() const {
        return ::hwshqtb::gui::extent_t{};
    }

    ::hwshqtb::gui::extent_t layout_t::minimum_size() const {
        // No floor of its own: a layout that cannot say how small its arrangement may get cannot be
        // arranged smaller than the size it asks for. A derived layout with a real floor overrides it.
        return size_hint();
    }

    void layout_t::set_margins(::hwshqtb::gui::margins_t m) {
        _margins = m;
    }

    ::hwshqtb::gui::margins_t layout_t::margins() const {
        return _margins;
    }

    void layout_t::set_spacing(int s) {
        _spacing = s;
    }

    int layout_t::spacing() const {
        return _spacing;
    }
}
