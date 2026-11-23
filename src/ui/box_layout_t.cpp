#include "hwshqtb/ui/box_layout_t.h"

#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/ui/widget_t.h"

#include "layout_util.h"

#include <cstddef>
#include <vector>

namespace hwshqtb::ui {
    box_layout_t::box_layout_t(box_orientation_enum orientation):
        _orientation(orientation) { }

    box_orientation_enum box_layout_t::orientation() const {
        return _orientation;
    }

    void box_layout_t::apply(widget_t& parent, ::hwshqtb::gui::rect_t area) {
        const bool horizontal = _orientation == box_orientation_enum::HORIZONTAL;
        int content_left = area.left() + _margins.left();
        int content_top = area.top() + _margins.top();
        int content_width = area.width() - _margins.horizontal();
        int content_height = area.height() - _margins.vertical();
        if (content_width < 0) {
            content_width = 0;
        }
        if (content_height < 0) {
            content_height = 0;
        }

        // Only item children take part; item_at_index() already skips plain object_t children. The
        // size policy, the stretch and the hints are item-level, so the layout never needs to know
        // whether a child is a widget or another kind of item.
        std::vector<item_t*> children;
        children.reserve(parent.item_count());
        for (std::size_t index = 0; index < parent.item_count(); ++index) {
            if (item_t* const child = parent.item_at_index(index)) {
                children.push_back(child);
            }
        }
        const std::size_t count = children.size();
        if (count == 0) {
            return;
        }

        const int available = horizontal ? content_width : content_height;
        const int cross = horizontal ? content_height : content_width;
        const int gap = _spacing > 0 ? _spacing : 0;
        // The bits that act on the MAIN axis. Each axis is independent: a vertical bit never shapes the
        // main axis of a row, and a horizontal bit never shapes the main axis of a column.
        const alignment_t main_bits =
            horizontal ? horizontal_alignment_mask : vertical_alignment_mask;

        std::vector<int> hints(count);
        std::vector<int> minimums(count);
        std::vector<int> maximums(count);
        std::vector<int> cross_maximums(count);
        std::vector<int> weights(count, 0);
        std::vector<int> sizes(count, 0);
        for (std::size_t i = 0; i < count; ++i) {
            const item_t& child = *children[i];
            const bool fixed = child.has_fixed_size();
            // The layout asks the effective triple, never size_hint(): the hard constraint wins over
            // the child's opinion, which is clamped into [minimum_size, maximum_size].
            const ::hwshqtb::gui::extent_t hint = child.effective_size_hint();
            const ::hwshqtb::gui::extent_t minimum = child.effective_minimum_size();
            const ::hwshqtb::gui::extent_t maximum = child.effective_maximum_size();
            const int minimum_main = detail::clamped_extent(
                horizontal ? minimum.width() : minimum.height(), 0);
            minimums[i] = minimum_main;
            // The hard bounds are already ordered by item_t; the max() keeps a hand-built span safe.
            maximums[i] = detail::clamped_extent(horizontal ? maximum.width() : maximum.height(),
                                                 minimum_main);
            const int hint_main = horizontal ? hint.width() : hint.height();
            hints[i] = detail::clamped_extent(hint_main, minimum_main);
            if (hints[i] > maximums[i]) {
                hints[i] = maximums[i];
            }
            cross_maximums[i] = detail::clamped_extent(
                horizontal ? maximum.height() : maximum.width(), 0);
            const bool aligned_main = static_cast<bool>(child.alignment() & main_bits);
            if (aligned_main && maximums[i] > hints[i]) {
                // The allocation ceiling of an aligned axis is the child's own hint (Qt's
                // qSmartMaxSize(), reached through QWidgetItem::maximumSize() in
                // qtbase/src/widgets/kernel/qlayoutitem.cpp). The child therefore never grows past the
                // size it asked for; the spare space goes to the expanding siblings or stays unused.
                // The ceiling enters the resolver as an ordinary maximum, so resolve_extents() keeps
                // its one algorithm: no branch of it knows about alignment, it only receives a
                // narrower [minimum, maximum] window on the caller's side.
                maximums[i] = hints[i];
            }
            if (!fixed) {
                const size_policy_enum policy = horizontal ? child.horizontal_policy()
                                                        : child.vertical_policy();
                // An aligned axis takes no share of the spare space even under an EXPANDING policy: the
                // main axis' expandingDirections() is cleared here. Qt keeps that flag in
                // QLayoutStruct::expansive (QBoxLayoutPrivate::setupGeom(), qboxlayout.cpp) without
                // consulting the alignment and relies on the narrowed maximum instead, which is the
                // same outcome: a slot already at its maximum takes no pixels. The zero weight spells
                // the exclusion in the distribution itself as well.
                if (policy == size_policy_enum::EXPANDING && !aligned_main) {
                    // A zero stretch still grows (equally with its zero-stretch siblings): weight 1.
                    weights[i] = child.stretch() > 0 ? child.stretch() : 1;
                }
            }
        }

        detail::resolve_extents(available, hints, minimums, maximums, weights, gap, sizes);

        int cursor = horizontal ? content_left : content_top;
        for (std::size_t i = 0; i < count; ++i) {
            item_t& child = *children[i];
            const alignment_t alignment = child.alignment();
            // Cross axis: a fixed child keeps its fixed cross extent; every other child fills the
            // whole content area, capped by its effective maximum on that axis (the cross axis is not
            // a compression stage, so no minimum is enforced here: there is no distribution to do).
            int child_cross = child.has_fixed_size()
                ? (horizontal ? child.fixed_size().height() : child.fixed_size().width())
                : cross;
            if (child_cross > cross_maximums[i]) {
                child_cross = cross_maximums[i];
            }
            // MAIN axis: the resolved slot IS the cell. Qt's QBoxLayout hands every item
            // QRect(a.at(i).pos, ..., a.at(i).size, ...) (qtbase/src/widgets/kernel/qboxlayout.cpp),
            // so the cell is exactly as wide as the item and horizontal_alignment_offset() answers 0
            // by its own contract ("no room inside the cell"). The bit acted on this axis in the size
            // model instead: the maximum was narrowed to the hint above, which is what stops an
            // aligned expanding child from filling the row. The call is still made here so the main
            // axis goes through the same placement rule as the cross axis.
            const int main_offset = horizontal
                ? horizontal_alignment_offset(sizes[i], sizes[i], alignment)
                : vertical_alignment_offset(sizes[i], sizes[i], alignment);
            // CROSS axis: the cell is the whole content extent, so a child that came out smaller than
            // it (capped by its maximum, or pinned to a fixed size) is placed inside that cell by the
            // alignment bits of this axis.
            const int cross_offset = horizontal
                ? vertical_alignment_offset(cross, child_cross, alignment)
                : horizontal_alignment_offset(cross, child_cross, alignment);
            if (horizontal) {
                child.set_geometry(::hwshqtb::gui::rect_t{ cursor + main_offset,
                                                           content_top + cross_offset, sizes[i],
                                                           child_cross });
            } else {
                child.set_geometry(::hwshqtb::gui::rect_t{ content_left + cross_offset,
                                                           cursor + main_offset, child_cross,
                                                           sizes[i] });
            }
            cursor += sizes[i] + gap;
        }
    }

    ::hwshqtb::gui::extent_t box_layout_t::size_hint() const {
        return aggregate(false);
    }

    ::hwshqtb::gui::extent_t box_layout_t::minimum_size() const {
        return aggregate(true);
    }

    ::hwshqtb::gui::extent_t box_layout_t::aggregate(bool minimum) const {
        const bool horizontal = _orientation == box_orientation_enum::HORIZONTAL;
        const int gap = _spacing > 0 ? _spacing : 0;
        int main = 0;
        int cross = 0;
        std::size_t count = 0;
        if (_parent != nullptr) {
            for (std::size_t index = 0; index < _parent->item_count(); ++index) {
                item_t* const child = _parent->item_at_index(index);
                if (child == nullptr) {
                    continue;
                }
                // The layout asks the effective triple, never the bare opinion: a child's hard
                // constraint has to reach the container's own extent.
                const ::hwshqtb::gui::extent_t extent =
                    minimum ? child->effective_minimum_size() : child->effective_size_hint();
                const int child_main = horizontal ? extent.width() : extent.height();
                const int child_cross = horizontal ? extent.height() : extent.width();
                main += child_main > 0 ? child_main : 0;
                if (child_cross > cross) {
                    cross = child_cross;
                }
                ++count;
            }
        }
        if (count > 1) {
            main += gap * static_cast<int>(count - 1);
        }
        main += horizontal ? _margins.horizontal() : _margins.vertical();
        cross += horizontal ? _margins.vertical() : _margins.horizontal();
        return horizontal ? ::hwshqtb::gui::extent_t{ main, cross }
                          : ::hwshqtb::gui::extent_t{ cross, main };
    }

    hbox_layout_t::hbox_layout_t():
        box_layout_t(box_orientation_enum::HORIZONTAL) { }

    vbox_layout_t::vbox_layout_t():
        box_layout_t(box_orientation_enum::VERTICAL) { }
}
