/// @file group_stack_t.cpp
/// @brief group_stack_t: the cursor arithmetic and the group layer stack (see group_stack_t.h).

#include "hwshqtb/ui/group_stack_t.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwshqtb::ui {
    namespace {
        /// @brief The extra gap alloc_row() leaves between two rows.
        constexpr int row_spacing = 4;
    }

    group_stack_t::group_stack_t() {
        reset(0);
    }

    group_stack_t::~group_stack_t() = default;

    void group_stack_t::reset(int width) {
        _layers.clear();
        layer_t root;
        root.width = width;
        _layers.push_back(std::move(root));
    }

    std::size_t group_stack_t::depth() const noexcept {
        return _layers.size();
    }

    const group_stack_t::layer_t& group_stack_t::layer() const noexcept {
        return _layers.back();
    }

    ::hwshqtb::gui::point_t group_stack_t::cursor() const noexcept {
        return _layers.back().cursor;
    }

    void group_stack_t::set_cursor(::hwshqtb::gui::point_t cursor) {
        _layers.back().cursor = cursor;
    }

    void group_stack_t::set_root_width(int width) noexcept {
        if (_layers.size() == 1) {
            _layers.front().width = width;
        }
    }

    ::hwshqtb::gui::rect_t group_stack_t::alloc_row(int height) {
        if (height <= 0) {
            height = 1;
        }
        layer_t& current = _layers.back();
        const ::hwshqtb::gui::rect_t row{ current.cursor.x(), current.cursor.y(), current.width,
                                          height };
        current.cursor.set_y(current.cursor.y() + height + row_spacing);
        return row;
    }

    void group_stack_t::begin_group(::hwshqtb::gui::rect_t area, std::string_view id_prefix) {
        const layer_t& parent = _layers.back();
        layer_t group;
        group.origin = parent.origin + area.top_left();
        group.width = std::max(0, area.width());
        group.id_prefix = parent.id_prefix + std::string(id_prefix) + "/";
        _layers.push_back(std::move(group));
    }

    bool group_stack_t::end_group() noexcept {
        if (_layers.size() <= 1) {
            return false; // the root layer cannot be popped
        }
        _layers.pop_back();
        return true;
    }

    std::string group_stack_t::key_of(std::string_view object_name) const {
        return _layers.back().id_prefix + std::string(object_name);
    }
}
