/// @file tab_widget_t.cpp
/// @brief tab_widget_t: the page bookkeeping, the tab bar geometry/hit test and the painting through
///        default_style() (see tab_widget_t.h).

#include "hwshqtb/ui/tab_widget_t.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/style_t.h"

namespace hwshqtb::ui {
    tab_widget_t::tab_widget_t():
        widget_t(nullptr) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
    }

    tab_widget_t::tab_widget_t(widget_t* parent):
        widget_t(parent) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
    }

    void tab_widget_t::add_tab(widget_t* page, std::string_view title) {
        if (page == nullptr) {
            return;
        }
        for (const tab_entry_t& entry : _tabs) {
            if (entry.page.data() == page) {
                return; // the page is already a tab
            }
        }
        const bool was_empty = _tabs.empty();
        page->set_parent(this);
        _tabs.push_back(tab_entry_t{ ::hwshqtb::metaobject::object_pointer_t<widget_t>{ page },
                                     std::string(title) });
        update(); // the tab bar gained an entry: the frame has to repaint it
        if (was_empty) {
            _current = 0;
            update_pages();
            current_changed(_current);
        }
        else {
            page->set_visible(false);
        }
    }

    void tab_widget_t::remove_tab(int index) {
        if (index < 0 || index >= count()) {
            return;
        }
        const std::size_t position = static_cast<std::size_t>(index);
        widget_t* const page = _tabs[position].page.data();
        _tabs.erase(_tabs.begin() + static_cast<std::ptrdiff_t>(position));
        update(); // the tab bar lost an entry: the frame has to repaint it
        if (page != nullptr) {
            page->set_parent(nullptr); // detached, not deleted: the caller owns it (Qt's removeTab)
        }
        if (_tabs.empty()) {
            _current = -1;
            update_pages();
            current_changed(_current);
            return;
        }
        int next = _current;
        if (index < next) {
            --next; // the same page keeps the focus, its index moved
        }
        else if (index == next) {
            next = std::min(index, count() - 1); // the nearest page becomes current
        }
        const bool changed = next != _current;
        _current = next;
        update_pages();
        if (changed) {
            current_changed(_current);
        }
    }

    void tab_widget_t::set_current_index(int index) {
        if (_tabs.empty()) {
            return;
        }
        const int clamped = std::clamp(index, 0, count() - 1);
        if (clamped == _current) {
            return;
        }
        _current = clamped;
        update(); // the selected tab changed: the bar has to repaint
        update_pages();
        current_changed(_current);
    }

    int tab_widget_t::current_index() const {
        return _current;
    }

    int tab_widget_t::count() const {
        return static_cast<int>(_tabs.size());
    }

    widget_t* tab_widget_t::page_at(int index) const {
        if (index < 0 || index >= count()) {
            return nullptr;
        }
        return _tabs[static_cast<std::size_t>(index)].page.data();
    }

    std::string_view tab_widget_t::tab_title(int index) const {
        if (index < 0 || index >= count()) {
            return {};
        }
        return _tabs[static_cast<std::size_t>(index)].title;
    }

    ::hwshqtb::gui::rect_t tab_widget_t::tab_bar_rect() const {
        const ::hwshqtb::gui::rect_t bounds = rect();
        const int height = std::min(bounds.height(),
                                    default_style().pixel_metric(pixel_metric_enum::TAB_HEIGHT));
        return ::hwshqtb::gui::rect_t{ bounds.x(), bounds.y(), bounds.width(), height };
    }

    ::hwshqtb::gui::rect_t tab_widget_t::content_rect() const {
        const ::hwshqtb::gui::rect_t bounds = rect();
        const int height = std::min(bounds.height(),
                                    default_style().pixel_metric(pixel_metric_enum::TAB_HEIGHT));
        return ::hwshqtb::gui::rect_t{ bounds.x(), bounds.y() + height, bounds.width(),
                                       std::max(0, bounds.height() - height) };
    }

    ::hwshqtb::gui::rect_t tab_widget_t::tab_rect(int index) const {
        if (index < 0 || index >= count()) {
            return ::hwshqtb::gui::rect_t{};
        }
        const style_t& style = default_style();
        const int padding = style.pixel_metric(pixel_metric_enum::TEXT_PADDING);
        const ::hwshqtb::gui::font_metrics_t metrics{};
        const ::hwshqtb::gui::rect_t bar = tab_bar_rect();
        int x = bar.x();
        for (int current = 0; current < count(); ++current) {
            const int width = metrics.text_extent(_tabs[static_cast<std::size_t>(current)].title).width()
                + 2 * padding;
            if (current == index) {
                return ::hwshqtb::gui::rect_t{ x, bar.y(), width, bar.height() };
            }
            x += width;
        }
        return ::hwshqtb::gui::rect_t{};
    }

    int tab_widget_t::tab_index_at(::hwshqtb::gui::point_t local) const {
        if (!tab_bar_rect().contains(local)) {
            return -1;
        }
        for (int index = 0; index < count(); ++index) {
            if (tab_rect(index).contains(local)) {
                return index;
            }
        }
        return -1;
    }

    void tab_widget_t::update_pages() {
        const ::hwshqtb::gui::rect_t content = content_rect();
        for (int index = 0; index < count(); ++index) {
            widget_t* const page = _tabs[static_cast<std::size_t>(index)].page.data();
            if (page == nullptr) {
                continue;
            }
            if (index == _current) {
                page->set_visible(true);
                page->set_geometry(content);
            }
            else {
                page->set_visible(false);
            }
        }
    }

    bool tab_widget_t::page_has_focus() const {
        widget_t* const page = page_at(_current);
        if (page == nullptr) {
            return false;
        }
        std::vector<const item_t*> pending{ page };
        while (!pending.empty()) {
            const item_t* const item = pending.back();
            pending.pop_back();
            if (item->has_focus()) {
                return true;
            }
            for (std::size_t index = 0; index < item->item_count(); ++index) {
                if (const item_t* const child = item->item_at_index(index)) {
                    pending.push_back(child);
                }
            }
        }
        return false;
    }

    ::hwshqtb::gui::extent_t tab_widget_t::size_hint() const {
        if (_tabs.empty()) {
            return ::hwshqtb::gui::extent_t{};
        }
        ::hwshqtb::gui::extent_t hint{};
        for (const tab_entry_t& entry : _tabs) {
            const widget_t* const page = entry.page.data();
            if (page == nullptr) {
                continue;
            }
            const ::hwshqtb::gui::extent_t page_hint = page->effective_size_hint();
            hint.set_width(std::max(hint.width(), page_hint.width()));
            hint.set_height(std::max(hint.height(), page_hint.height()));
        }
        hint.set_height(hint.height()
                        + default_style().pixel_metric(pixel_metric_enum::TAB_HEIGHT));
        return hint;
    }

    ::hwshqtb::gui::extent_t tab_widget_t::minimum_size_hint() const {
        if (_tabs.empty()) {
            return ::hwshqtb::gui::extent_t{};
        }
        ::hwshqtb::gui::extent_t floor{};
        for (const tab_entry_t& entry : _tabs) {
            const widget_t* const page = entry.page.data();
            if (page == nullptr) {
                continue;
            }
            const ::hwshqtb::gui::extent_t page_floor = page->effective_minimum_size();
            floor.set_width(std::max(floor.width(), page_floor.width()));
            floor.set_height(std::max(floor.height(), page_floor.height()));
        }
        // The bar is drawn above every page, so it stays part of the floor even when the pages
        // themselves could shrink to nothing.
        floor.set_height(floor.height()
                         + default_style().pixel_metric(pixel_metric_enum::TAB_HEIGHT));
        return floor;
    }

    void tab_widget_t::geometry_change_event(geometry_change_event_t* event) {
        widget_t::geometry_change_event(event); // the widget layer's re-layout and frame request
        update_pages();                         // ... then the pages follow the new content rect
    }

    bool tab_widget_t::interact(const ::hwshqtb::gui::interaction_t& in) {
        if (!is_enabled()) {
            _bar_pressed = false;
            return false;
        }
        if (in.key != 0) {
            if (!has_focus() || page_has_focus() || _tabs.empty()) {
                return false;
            }
            int target = _current;
            if (in.key == ::hwshqtb::gui::interaction_t::left_key) {
                target = _current - 1;
            }
            else if (in.key == ::hwshqtb::gui::interaction_t::right_key) {
                target = _current + 1;
            }
            else if (in.key == ::hwshqtb::gui::interaction_t::home_key) {
                target = 0;
            }
            else if (in.key == ::hwshqtb::gui::interaction_t::end_key) {
                target = count() - 1;
            }
            else {
                return false;
            }
            if (in.key_pressed) {
                set_current_index(target);
            }
            return true; // the widget owns the key, even when it is already at the end
        }
        if (in.mouse_pressed && in.hovered && tab_bar_rect().contains(in.mouse)) {
            _bar_pressed = true;
            const int index = tab_index_at(in.mouse);
            if (index >= 0) {
                set_current_index(index);
            }
            return true;
        }
        if (in.mouse_released && _bar_pressed) {
            _bar_pressed = false;
            return true;
        }
        return false;
    }

    void tab_widget_t::paint_event(paint_event_t* event) {
        ::hwshqtb::gui::painter_t& painter = event->painter();
        const style_t& style = default_style();
        control_state_t state;
        state.enabled = is_enabled();
        style.draw_control(control_element_enum::TAB_BAR, painter, tab_bar_rect(), state);
        for (int index = 0; index < count(); ++index) {
            state.checked = index == _current;
            state.text = _tabs[static_cast<std::size_t>(index)].title;
            style.draw_control(control_element_enum::TAB, painter, tab_rect(index), state);
        }
    }

    META_OBJECT(tab_widget_t);
    META_REGISTER_WIDGET(tab_widget_t);
}
