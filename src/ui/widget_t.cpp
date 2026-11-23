#include "hwshqtb/ui/widget_t.h"

#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/core_application.h"
#include "hwshqtb/ui/layout_t.h"
#include "hwshqtb/ui/ui_event_t.h"

#include <cstddef>
#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace hwshqtb::ui {
    widget_t::widget_t(widget_t* parent) {
        // The visual parent is a widget (the object-level parent is the same node): one tree.
        if (parent != nullptr) {
            set_parent(parent);
        }
    }

    widget_t::~widget_t() {
        delete _layout; // the widget owns its layout; ~object_t later deletes the children
        _layout = nullptr;
    }

    void widget_t::geometry_change_event(geometry_change_event_t*) {
        // The layout reads the rectangle the item already stored, so the event payload is not needed.
        // Lay the children out into the new client rect first, then request the repaint: the children
        // have already asked for their own repaints through their set_geometry() calls.
        relayout();
    }

    ::hwshqtb::gui::point_t widget_t::pos() const {
        return geometry().top_left();
    }

    ::hwshqtb::gui::extent_t widget_t::size() const {
        const ::hwshqtb::gui::rect_t bounds = geometry();
        return ::hwshqtb::gui::extent_t{ bounds.width(), bounds.height() };
    }

    ::hwshqtb::gui::extent_t widget_t::size_hint() const {
        if (_layout != nullptr) {
            return _layout->size_hint(); // the container's opinion: derived from its item children
        }
        return ::hwshqtb::gui::extent_t{}; // no opinion
    }

    ::hwshqtb::gui::extent_t widget_t::minimum_size_hint() const {
        if (_layout != nullptr) {
            return _layout->minimum_size(); // the container's floor: derived from its item children
        }
        return ::hwshqtb::gui::extent_t{};
    }

    void widget_t::set_layout(layout_t* layout) {
        if (_layout == layout) {
            return; // installing the pointer we already own must not delete it
        }
        delete _layout;
        _layout = layout;
        if (_layout != nullptr) {
            _layout->_parent = this; // the owner link size_hint() walks (friend of layout_t)
        }
        relayout();
    }

    layout_t* widget_t::layout() const {
        return _layout;
    }

    void widget_t::relayout() {
        if (_layout != nullptr) {
            // The parent's client rect is its own rect(): origin at the parent's top-left.
            _layout->apply(*this, rect());
        }
    }

    void widget_t::child_added(::hwshqtb::metaobject::object_t* child) {
        if (child != nullptr && child->is_item()) {
            relayout();
        }
    }

    void widget_t::child_removed(::hwshqtb::metaobject::object_t* child) {
        // Called from ~object_t() of the child as well, where its dynamic type is already object_t:
        // is_widget() is false even for a dying widget, so a check would miss the relayout that
        // matters most. A plain object_t child only costs one harmless extra pass.
        (void)child;
        relayout();
    }

    widget_t* widget_t::parent_widget() const {
        ::hwshqtb::metaobject::object_t* const parent_object = parent();
        if (parent_object == nullptr || !parent_object->is_widget()) {
            return nullptr;
        }
        return static_cast<widget_t*>(parent_object);
    }

    ::hwshqtb::gui::rect_t widget_t::items_rect(const item_t& child) const {
        (void)child;  // the default clip does not depend on which child is asked about
        return rect();
    }

    item_t* widget_t::hit(::hwshqtb::gui::point_t local) const {
        // A child that sticks out of this widget is clipped when it is painted, so it is not a hit
        // either: outside the parent nothing here owns the point.
        if (!rect().contains(local)) {
            return nullptr;
        }
        const std::span<::hwshqtb::metaobject::object_t* const> child_objects = children();
        for (auto it = child_objects.rbegin(); it != child_objects.rend(); ++it) {
            ::hwshqtb::metaobject::object_t* const child_object = *it;
            if (child_object == nullptr || !child_object->is_item()) {
                continue;
            }
            item_t* const child = static_cast<item_t*>(child_object);
            // Reverse order: the child added last is painted last, i.e. it is on top and wins.
            if (!child->is_visible() || !child->is_enabled()) {
                continue;
            }
            const ::hwshqtb::gui::point_t child_local = local - child->geometry().top_left();
            if (!child->rect().contains(child_local)) {
                continue;
            }
            // The container's clip: what items_rect() cuts away is not painted, so it must not be hit
            // either. A scrolled content is far larger than the viewport it is shown through, and
            // without this gate the content would win every press over the scroll bars beside it.
            if (!items_rect(*child).intersected(child->geometry()).contains(local)) {
                continue;
            }
            // Deepest visible item wins (a click on a nested control reaches that control).
            if (item_t* const deeper = child->hit(child_local); deeper != nullptr) {
                return deeper;
            }
            return child;
        }
        return nullptr;
    }

    const item_t* widget_t::paint_skip() const noexcept {
        return nullptr; // the framework paints every item child by default
    }

    void widget_t::render(::hwshqtb::gui::painter_t& painter) {
        render(painter, ::hwshqtb::gui::region_t{ painter.clip_rect() });
    }

    void widget_t::render(::hwshqtb::gui::painter_t& painter, const ::hwshqtb::gui::region_t& damage) {
        if (!is_visible()) {
            return; // hidden: neither this widget nor its subtree is painted, so the damage stays
        }
        // The painter arrives at the PARENT's origin, so both its clip and the incoming damage are in the
        // parent's coordinates. Narrowing the damage by the clip and by this widget's rectangle gives the
        // part of THIS widget the frame really paints; a subtree the damage does not touch keeps its
        // damage for the frame that does reach it.
        ::hwshqtb::gui::region_t visible = damage;
        visible.intersect(painter.clip_rect());
        visible.intersect(geometry());
        if (visible.is_empty()) {
            return;
        }
        painter.save();
        painter.translate(geometry().top_left());
        painter.set_clip(rect());
        if (!painter.clip_rect().isEmpty()) {
            // Into this widget's own coordinates (the same translation the painter just received), then
            // narrowed by the rectangle: that local remainder is what paint_event() draws.
            const ::hwshqtb::gui::point_t origin = geometry().top_left();
            ::hwshqtb::gui::region_t local = visible.translated(-origin.x(), -origin.y());
            local.intersect(rect());
            paint_event_t pass{ painter, std::move(local) };
            paint_event(&pass);                        // the widget's OWN pixels
            paint_item_children(painter, pass.damage(), paint_skip()); // then its item children (the framework)
        }
        painter.restore();
        clear_damage(); // the frame painted this widget: its record is consumed
    }

    void widget_t::paint_event(paint_event_t*) {
        // The pixels of this widget itself: nothing by default. An item whose pixels are produced
        // elsewhere (a container that only arranges children, a spacer) has nothing to draw. The item
        // children are painted by render() AFTER this returns, exactly like Qt's drawWidget().
    }

    void widget_t::paint_item_children(::hwshqtb::gui::painter_t& painter,
                                       const ::hwshqtb::gui::region_t& damage, const item_t* skip) {
        // The painter is translated to this widget, so its clip is in this widget's coordinates and so
        // is every child's geometry: the occlusion arithmetic needs no coordinate change at all.
        const ::hwshqtb::gui::rect_t clip = painter.clip_rect();
        if (clip.isEmpty() || damage.is_empty()) {
            return;
        }

        struct child_t {
            item_t* item = nullptr;              ///< The child to paint.
            ::hwshqtb::gui::rect_t rect{};       ///< Its rectangle in this widget's coordinates.
            ::hwshqtb::gui::region_t visible{};  ///< The part of @p rect the frame paints and no
                                                 ///< opaque sibling above covers.
        };
        std::vector<child_t> ordered;
        ordered.reserve(item_count());
        bool any_opaque = false;
        for (::hwshqtb::metaobject::object_t* const child_object : children()) {
            if (child_object == nullptr || !child_object->is_item()) {
                continue; // a plain object_t child is not part of the item tree
            }
            item_t* const child = static_cast<item_t*>(child_object);
            if (child == skip) {
                continue;
            }
            child_t entry;
            entry.item = child;
            // The part of the child this container shows: its rectangle narrowed by the container's
            // clip (items_rect()). For the default clip that is the rectangle itself, so nothing
            // changes; a container that shows an oversized child through a window (a scroll area's
            // content) paints it — and culls it — only where the window is. hit() asks the same
            // question, which is what keeps the painted pixels and the owned points identical.
            entry.rect = items_rect(*child).intersected(child->geometry());
            if (!child->is_visible() || entry.rect.isEmpty() || entry.rect.intersected(clip).isEmpty()) {
                continue; // not painted, so it neither needs a region nor hides anything
            }
            // The frame only has to paint the part of that area the incoming damage covers.
            entry.visible = damage;
            entry.visible.intersect(entry.rect);
            if (entry.visible.is_empty()) {
                continue; // nothing of this child is damaged: no pass, no paint event
            }
            any_opaque = any_opaque || child->is_opaque();
            ordered.push_back(entry);
        }
        if (ordered.empty()) {
            return;
        }

        if (!any_opaque) {
            // Nothing hides anything: every child paints itself, and its own set_clip() narrows the
            // frame's clip to its rectangle (so this loop needs no clip of its own).
            for (const child_t& child : ordered) {
                child.item->render(painter, child.visible);
            }
            return;
        }

        // Top-down: the area an opaque child covers is hidden from the siblings BELOW it (Qt paints
        // bottom-up, so the covered ones are the earlier children). Subtracting it from the damage is
        // the occlusion rule: what an opaque child fills does not have to be painted under it.
        ::hwshqtb::gui::region_t covered;
        for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
            it->visible = it->visible.subtracted(covered);
            if (it->item->is_opaque()) {
                covered.unite(it->rect.intersected(clip));
            }
        }
        // Bottom-up: the paint order is the z-order, and the visible sub-regions of one level are
        // disjoint, so painting them one clip at a time never blends a pixel twice. The ui layer owns
        // no pass policy: it asks the painter for one clip per visible rectangle and paints there.
        for (const child_t& child : ordered) {
            for (const ::hwshqtb::gui::rect_t& area : child.visible.rects()) {
                painter.save();
                painter.set_clip(area);
                child.item->render(painter, ::hwshqtb::gui::region_t{ area });
                painter.restore();
            }
        }
    }

    void widget_t::update() {
        update(rect());
    }

    void widget_t::update(::hwshqtb::gui::rect_t local) {
        unite_damage(local);   // exactly the rectangle the caller named, not the whole widget
        request_frame();       // and the frame, whether or not anything new was damaged
    }

    const ::hwshqtb::gui::region_t& widget_t::damage() const noexcept {
        return _damage;
    }

    void widget_t::translate_damage(::hwshqtb::gui::rect_t local, int dx, int dy) {
        if (local.isEmpty() || (dx == 0 && dy == 0) || _damage.is_empty()) {
            return;
        }
        const ::hwshqtb::gui::rect_t global =
            local.translated(map_to_global(::hwshqtb::gui::point_t{}));
        _damage = _damage.translated_within(global, dx, dy);
    }

    void widget_t::clear_damage() noexcept {
        _damage = ::hwshqtb::gui::region_t{};
    }

    void widget_t::repaint() {
        // The same damage record as update(): the frame that follows paints exactly this. The difference
        // is the REQUEST: UI_REPAINT asks the host for an immediate frame, so the pixels are current when
        // this returns (QWidget::repaint).
        unite_damage(rect());
        if (::hwshqtb::metaobject::object_t* const receiver = request_receiver(); receiver != nullptr) {
            ::hwshqtb::metaobject::event_t request{ ::hwshqtb::metaobject::event_type_enum::UI_REPAINT };
            (void)::hwshqtb::metaobject::core_application::send_event(receiver, &request);
            return;
        }
        // Without a host nobody can paint: fall back to the asynchronous request so a later frame that is
        // driven by hand still sees the damage.
        notify_repaint();
    }

    void widget_t::unite_damage(::hwshqtb::gui::rect_t local) {
        if (local.isEmpty()) {
            return;
        }
        // One rectangle, in window coordinates: the local origin mapped up the item chain, the local
        // extent unchanged. It is united into every WIDGET ancestor, so the root's damage is the union of
        // its whole subtree (Qt's QWidgetRepaintManager has exactly this shape). A non-widget ancestor has
        // no record to carry it.
        const ::hwshqtb::gui::rect_t global =
            local.translated(map_to_global(::hwshqtb::gui::point_t{}));
        for (item_t* item = this; item != nullptr;) {
            if (item->is_widget()) {
                static_cast<widget_t*>(item)->_damage.unite(global);
            }
            ::hwshqtb::metaobject::object_t* const parent = item->parent();
            item = (parent != nullptr && parent->is_item()) ? static_cast<item_t*>(parent) : nullptr;
        }
    }

    void widget_t::notify_repaint() {
        // 1. Record the damage. This is the widget layer's own concern: an item_t that is not a widget
        //    has no pixels, so it has nothing to record.
        unite_damage(rect());
        request_frame();
    }

    void widget_t::request_frame() {
        // The ONE channel the tree has to the outside world: the root's request RECEIVER (the host
        // object), which receives a plain PAINT event — the same message every other frame request uses.
        if (::hwshqtb::metaobject::object_t* const receiver = request_receiver(); receiver != nullptr) {
            ::hwshqtb::metaobject::event_t request{ ::hwshqtb::metaobject::event_type_enum::PAINT };
            (void)::hwshqtb::metaobject::core_application::send_event(receiver, &request);
        }
    }

    void widget_t::notify_scroll(::hwshqtb::gui::rect_t local, int dx, int dy) {
        if (dx == 0 && dy == 0) {
            return;
        }
        // Window coordinates: this widget's local origin mapped all the way up the same chain of
        // origin offsets the receiver's window expects.
        const ::hwshqtb::gui::point_t origin = map_to_global(::hwshqtb::gui::point_t{});
        const ::hwshqtb::gui::rect_t window_rect = local.translated(origin);
        if (::hwshqtb::metaobject::object_t* const receiver = request_receiver(); receiver != nullptr) {
            // Clipped to the host's area BEFORE it leaves the tree. A widget may be LARGER than the
            // window, so an unclipped rectangle is a coordinate a receiver mirroring pixels into a
            // buffer sized to the window cannot index — the library must never hand one over. An area
            // that is entirely out of view sends nothing at all.
            widget_t* top = this;
            while (widget_t* const parent = top->parent_widget()) {
                top = parent;
            }
            const ::hwshqtb::gui::rect_t visible = window_rect.intersected(top->rect());
            if (visible.isEmpty()) {
                return;
            }
            scroll_event_t request{ visible, dx, dy };
            (void)::hwshqtb::metaobject::core_application::send_event(receiver, &request);
            return;
        }
        // No host: the pixels cannot move, so the next frame has to repaint the whole area.
        update(local);
    }

    void widget_t::set_geometry_silently(::hwshqtb::gui::rect_t rect) {
        _geometry = rect;
    }

    void widget_t::focus_change_event(focus_change_event_t*) {
        // Nothing to add: item_t::set_has_focus() calls notify_repaint() AFTER this handler returns, so
        // the damage is recorded and the frame requested in the right order.
    }

    void widget_t::size_constraint_change_event(size_constraint_change_event_t*) {
        // The parent's layout owns this widget's place: re-apply it so a new bound takes effect now
        // rather than on the next resize, then ask for the frame (as set_geometry() does).
        if (widget_t* const parent = parent_widget(); parent != nullptr) {
            parent->relayout();
        }
    }

    void widget_t::visible_change_event(visible_change_event_t*) {
        // Nothing to add. item_t::set_visible() calls notify_repaint() AFTER this handler returns, so the
        // damage is recorded and the frame requested in that order — requesting here would publish a
        // damage region that does not contain this change yet (the old lost-frame bug).
    }

    void widget_t::enabled_change_event(enabled_change_event_t*) {
        // The same split as visible_change_event().
    }

    META_OBJECT(widget_t);
}
