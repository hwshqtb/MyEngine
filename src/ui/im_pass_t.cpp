/// @file im_pass_t.cpp
/// @brief im_pass_t: the batch, the pointer snapshot and the visit order of one pass.

#include "hwshqtb/ui/im_pass_t.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/metaobject/object_pointer_t.h"
#include "hwshqtb/ui/group_stack_t.h"
#include "hwshqtb/ui/widget_t.h"

namespace hwshqtb::ui {
    im_pass_t::im_pass_t() = default;

    im_pass_t::~im_pass_t() = default;

    void im_pass_t::begin(int root_width) {
        _groups.reset(root_width);
        _visited.clear();
        _accesses.clear();
        for (step_t& step : _input) {
            step.claimed = false;
        }
        _active = true;
    }

    void im_pass_t::end() {
        _previous = std::move(_visited);
        _visited.clear();
        _accesses.clear();
        _input.clear();
        _active = false;
    }

    bool im_pass_t::active() const noexcept {
        return _active;
    }

    void im_pass_t::push(const ::hwshqtb::gui::interaction_t& in) {
        _input.push_back(step_t{ in, false });
    }

    std::size_t im_pass_t::input_count() const noexcept {
        return _input.size();
    }

    im_pass_t::step_t& im_pass_t::input(std::size_t index) {
        return _input[index];
    }

    const im_pass_t::step_t& im_pass_t::input(std::size_t index) const {
        return _input[index];
    }

    void im_pass_t::erase_input(std::size_t index) {
        _input.erase(_input.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void im_pass_t::clear_input() noexcept {
        _input.clear();
    }

    void im_pass_t::record_pointer(const ::hwshqtb::gui::interaction_t& in) noexcept {
        _last_mouse = in.mouse;
        _last_mouse_down = in.mouse_down;
        _last_modifiers = in.modifiers;
    }

    ::hwshqtb::gui::point_t im_pass_t::last_mouse() const noexcept {
        return _last_mouse;
    }

    bool im_pass_t::last_mouse_down() const noexcept {
        return _last_mouse_down;
    }

    std::uint32_t im_pass_t::last_modifiers() const noexcept {
        return _last_modifiers;
    }

    void im_pass_t::visit(widget_t& control) {
        _visited.emplace_back(&control);
    }

    bool im_pass_t::note_access(std::string_view id, widget_t& control) {
        for (const access_t& access : _accesses) {
            if (access.id == id) {
                return false;
            }
        }
        _accesses.push_back(access_t{ std::string(id), &control });
        return true;
    }

    const std::vector<im_pass_t::access_t>& im_pass_t::accesses() const noexcept {
        return _accesses;
    }

    const std::vector<::hwshqtb::metaobject::object_pointer_t<widget_t>>& im_pass_t::previous()
        const noexcept {
        return _previous;
    }

    group_stack_t& im_pass_t::groups() noexcept {
        return _groups;
    }

    const group_stack_t& im_pass_t::groups() const noexcept {
        return _groups;
    }
}
