#include "hwshqtb/ui/item_t.h"

#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/core_application.h"

#include <utility>

namespace hwshqtb::ui {
    namespace {
        /// @brief The parent item of @p object, or nullptr when the object parent is not an item.
        item_t* parent_item_of(const ::hwshqtb::metaobject::object_t& object) {
            ::hwshqtb::metaobject::object_t* const parent = object.parent();
            if (parent == nullptr || !parent->is_item()) {
                return nullptr;
            }
            return static_cast<item_t*>(parent);
        }

        /// @brief @p size with every negative component replaced by 0.
        ::hwshqtb::gui::extent_t non_negative(::hwshqtb::gui::extent_t size) {
            if (size.width() < 0) {
                size.set_width(0);
            }
            if (size.height() < 0) {
                size.set_height(0);
            }
            return size;
        }
    }

    ::hwshqtb::gui::extent_t item_t::size_hint() const {
        return ::hwshqtb::gui::extent_t{}; // an item with no content claims no space of its own
    }

    ::hwshqtb::gui::extent_t item_t::minimum_size_hint() const {
        return ::hwshqtb::gui::extent_t{}; // ... and has no smaller usable size either
    }

    ::hwshqtb::gui::rect_t item_t::geometry() const noexcept {
        return _geometry;
    }

    void item_t::set_geometry(::hwshqtb::gui::rect_t rect) {
        if (_geometry == rect) {
            return; // not a real change: no event, no damage
        }
        const ::hwshqtb::gui::rect_t old_geometry = _geometry;
        _geometry = rect;
        // The event runs first: a container lays its children out in its handler, so their own damage
        // is recorded before this item asks for the frame that paints them.
        geometry_change_event_t geometry_event{ old_geometry, rect };
        (void)::hwshqtb::metaobject::core_application::send_event(this, &geometry_event);
        notify_repaint();
    }

    bool item_t::event(::hwshqtb::metaobject::event_t* event) {
        if (event == nullptr) {
            return false;
        }
        switch (event->type()) {
            case ::hwshqtb::metaobject::event_type_enum::PAINT:
                paint_event(static_cast<paint_event_t*>(event));
                break;
            case ::hwshqtb::metaobject::event_type_enum::UI_HOVER:
                hover_event(static_cast<hover_event_t*>(event));
                break;
            case ::hwshqtb::metaobject::event_type_enum::UI_PRESS:
                press_event(static_cast<press_event_t*>(event));
                break;
            case ::hwshqtb::metaobject::event_type_enum::UI_GEOMETRY_CHANGE:
                geometry_change_event(static_cast<geometry_change_event_t*>(event));
                break;
            case ::hwshqtb::metaobject::event_type_enum::UI_FOCUS_CHANGE:
                focus_change_event(static_cast<focus_change_event_t*>(event));
                break;
            case ::hwshqtb::metaobject::event_type_enum::UI_SIZE_CONSTRAINT_CHANGE:
                size_constraint_change_event(static_cast<size_constraint_change_event_t*>(event));
                break;
            case ::hwshqtb::metaobject::event_type_enum::UI_CONTENT_CHANGE:
                content_change_event(static_cast<content_change_event_t*>(event));
                break;
            case ::hwshqtb::metaobject::event_type_enum::UI_VISIBLE_CHANGE:
                visible_change_event(static_cast<visible_change_event_t*>(event));
                break;
            case ::hwshqtb::metaobject::event_type_enum::UI_ENABLED_CHANGE:
                enabled_change_event(static_cast<enabled_change_event_t*>(event));
                break;
            default:
                return ::hwshqtb::metaobject::object_t::event(event);
        }
        event->accept();
        return true;
    }

    item_t* item_t::hit(::hwshqtb::gui::point_t) const {
        return nullptr; // an item that owns no points is not hit-testable
    }

    bool item_t::interact(const ::hwshqtb::gui::interaction_t&) {
        return false; // a non-interactive item consumes nothing
    }

    bool item_t::is_visible() const noexcept {
        return _visible;
    }

    void item_t::set_visible(bool visible) {
        if (_visible == visible) {
            return; // not a real change: no event, no signal, no repaint
        }
        _visible = visible;
        visible_change_event_t event{ visible };
        (void)::hwshqtb::metaobject::core_application::send_event(this, &event); // the item's reaction
        visible_changed(visible);                                                // the observers'
        notify_repaint();
    }

    bool item_t::is_enabled() const noexcept {
        return _enabled;
    }

    void item_t::set_enabled(bool enabled) {
        if (_enabled == enabled) {
            return; // not a real change: no event, no signal, no repaint
        }
        _enabled = enabled;
        enabled_change_event_t event{ enabled };
        (void)::hwshqtb::metaobject::core_application::send_event(this, &event);
        enabled_changed(enabled);
        notify_repaint();
    }

    ::hwshqtb::gui::rect_t item_t::rect() const noexcept {
        const ::hwshqtb::gui::rect_t item_geometry = geometry();
        return ::hwshqtb::gui::rect_t{
            ::hwshqtb::gui::point_t{},
            ::hwshqtb::gui::extent_t{ item_geometry.width(), item_geometry.height() },
        };
    }

    ::hwshqtb::gui::point_t item_t::map_to_global(::hwshqtb::gui::point_t point) const {
        ::hwshqtb::gui::point_t result = point;
        for (const item_t* item = this; item != nullptr; item = parent_item_of(*item)) {
            result += item->geometry().top_left();
        }
        return result;
    }

    ::hwshqtb::gui::point_t item_t::map_from_global(::hwshqtb::gui::point_t point) const {
        ::hwshqtb::gui::point_t result = point;
        for (const item_t* item = this; item != nullptr; item = parent_item_of(*item)) {
            result -= item->geometry().top_left();
        }
        return result;
    }

    bool item_t::is_opaque() const noexcept {
        return _opaque;
    }

    void item_t::set_request_receiver(::hwshqtb::metaobject::object_t* receiver) noexcept {
        // Set on the ROOT: walk up so a caller holding any item configures the whole tree.
        item_t* root = this;
        while (auto* const parent = dynamic_cast<item_t*>(root->parent())) {
            root = parent;
        }
        root->_request_receiver = receiver;
    }

    ::hwshqtb::metaobject::object_t* item_t::request_receiver() const noexcept {
        const item_t* current = this;
        for (;;) {
            if (current->_request_receiver != nullptr) {
                return current->_request_receiver;
            }
            const auto* const parent = dynamic_cast<const item_t*>(current->parent());
            if (parent == nullptr) {
                return nullptr;
            }
            current = parent;
        }
    }

    void item_t::set_opaque(bool opaque) noexcept {
        _opaque = opaque;
    }

    std::size_t item_t::item_count() const {
        std::size_t count = 0;
        for (const ::hwshqtb::metaobject::object_t* child : children()) {
            if (child != nullptr && child->is_item()) {
                ++count;
            }
        }
        return count;
    }

    item_t* item_t::item_at_index(std::size_t index) const {
        std::size_t current = 0;
        for (::hwshqtb::metaobject::object_t* child : children()) {
            if (child == nullptr || !child->is_item()) {
                continue;
            }
            if (current == index) {
                return static_cast<item_t*>(child);
            }
            ++current;
        }
        return nullptr;
    }

    size_policy_enum item_t::horizontal_policy() const {
        return _horizontal_policy;
    }

    size_policy_enum item_t::vertical_policy() const {
        return _vertical_policy;
    }

    void item_t::set_size_policy(size_policy_enum horizontal, size_policy_enum vertical) {
        _horizontal_policy = horizontal;
        _vertical_policy = vertical;
    }

    int item_t::stretch() const {
        return _stretch;
    }

    void item_t::set_stretch(int stretch) {
        _stretch = stretch > 0 ? stretch : 0;
    }

    void item_t::set_alignment(alignment_t alignment) {
        // A plain store, like Qt's QLayoutItem::setAlignment() (qtbase/src/widgets/kernel/
        // qlayoutitem.cpp assigns the word): the bits are resolved when a layout places the item, so
        // there is nothing to validate or to reject here and no geometry changes by itself.
        _alignment = alignment;
    }

    alignment_t item_t::alignment() const noexcept {
        return _alignment;
    }

    ::hwshqtb::gui::extent_t item_t::minimum_size() const noexcept {
        return _minimum_size;
    }

    ::hwshqtb::gui::extent_t item_t::maximum_size() const noexcept {
        return _maximum_size;
    }

    void item_t::set_minimum_size(::hwshqtb::gui::extent_t size) {
        const ::hwshqtb::gui::extent_t minimum = non_negative(size);
        ::hwshqtb::gui::extent_t maximum = _maximum_size;
        // The invariant minimum <= maximum: a minimum above the maximum raises the maximum to it.
        if (maximum.width() < minimum.width()) {
            maximum.set_width(minimum.width());
        }
        if (maximum.height() < minimum.height()) {
            maximum.set_height(minimum.height());
        }
        if (_minimum_size == minimum && _maximum_size == maximum) {
            return; // the same constraint: a no-op requests no re-layout/repaint
        }
        _minimum_size = minimum;
        _maximum_size = maximum;
        size_constraint_change_event_t event;
        (void)::hwshqtb::metaobject::core_application::send_event(this, &event);
        notify_repaint(); // the item layer's repaint request: this item and every item ancestor
    }

    void item_t::set_maximum_size(::hwshqtb::gui::extent_t size) {
        const ::hwshqtb::gui::extent_t maximum = non_negative(size);
        ::hwshqtb::gui::extent_t minimum = _minimum_size;
        // The invariant minimum <= maximum: a maximum below the minimum lowers the minimum to it.
        if (minimum.width() > maximum.width()) {
            minimum.set_width(maximum.width());
        }
        if (minimum.height() > maximum.height()) {
            minimum.set_height(maximum.height());
        }
        if (_minimum_size == minimum && _maximum_size == maximum) {
            return; // the same constraint: a no-op requests no re-layout/repaint
        }
        _minimum_size = minimum;
        _maximum_size = maximum;
        size_constraint_change_event_t event;
        (void)::hwshqtb::metaobject::core_application::send_event(this, &event);
        notify_repaint(); // the item layer's repaint request: this item and every item ancestor
    }

    void item_t::set_fixed_size(::hwshqtb::gui::extent_t size) {
        // A fixed size is the pair of hard constraints, in this order: the minimum may raise the
        // maximum, and the maximum then pins it, so both axes end at `size`.
        set_minimum_size(size);
        set_maximum_size(size);
    }

    void item_t::set_minimum_width(int width) {
        ::hwshqtb::gui::extent_t minimum = _minimum_size;
        minimum.set_width(width);
        set_minimum_size(minimum);
    }

    void item_t::set_minimum_height(int height) {
        ::hwshqtb::gui::extent_t minimum = _minimum_size;
        minimum.set_height(height);
        set_minimum_size(minimum);
    }

    void item_t::set_maximum_width(int width) {
        ::hwshqtb::gui::extent_t maximum = _maximum_size;
        maximum.set_width(width);
        set_maximum_size(maximum);
    }

    void item_t::set_maximum_height(int height) {
        ::hwshqtb::gui::extent_t maximum = _maximum_size;
        maximum.set_height(height);
        set_maximum_size(maximum);
    }

    void item_t::set_fixed_width(int width) {
        set_minimum_width(width);
        set_maximum_width(width);
    }

    void item_t::set_fixed_height(int height) {
        set_minimum_height(height);
        set_maximum_height(height);
    }

    bool item_t::has_fixed_size() const noexcept {
        // Both axes pinned at the same non-empty size. One pinned axis is not a fixed size, and an
        // empty size is "no constraint" (the setters clamp negatives to 0, so empty means zero too).
        return !_minimum_size.isEmpty() && _minimum_size == _maximum_size;
    }

    ::hwshqtb::gui::extent_t item_t::fixed_size() const noexcept {
        return has_fixed_size() ? _minimum_size : ::hwshqtb::gui::extent_t{};
    }

    ::hwshqtb::gui::extent_t item_t::effective_size_hint() const {
        // The opinion first (Qt's QWidgetItem::sizeHint): size_hint raised to minimum_size_hint.
        ::hwshqtb::gui::extent_t hint = size_hint();
        const ::hwshqtb::gui::extent_t minimum_hint = minimum_size_hint();
        if (hint.width() < minimum_hint.width()) {
            hint.set_width(minimum_hint.width());
        }
        if (hint.height() < minimum_hint.height()) {
            hint.set_height(minimum_hint.height());
        }
        // Then clamp it into the hard constraints [minimum_size, maximum_size].
        if (hint.width() < _minimum_size.width()) {
            hint.set_width(_minimum_size.width());
        }
        if (hint.height() < _minimum_size.height()) {
            hint.set_height(_minimum_size.height());
        }
        if (hint.width() > _maximum_size.width()) {
            hint.set_width(_maximum_size.width());
        }
        if (hint.height() > _maximum_size.height()) {
            hint.set_height(_maximum_size.height());
        }
        return hint;
    }

    ::hwshqtb::gui::extent_t item_t::effective_minimum_size() const {
        // Per axis, Qt's qSmartMinSize decides the floor by whether the policy carries the shrink
        // flag. FIXED has none, so it does not shrink at all and its floor is max(size_hint,
        // minimum_size_hint); PREFERRED and EXPANDING shrink to minimum_size_hint. The hard
        // constraints then clamp the result into [minimum_size, maximum_size].
        ::hwshqtb::gui::extent_t floor = minimum_size_hint();
        if (_horizontal_policy == size_policy_enum::FIXED
            || _vertical_policy == size_policy_enum::FIXED) {
            const ::hwshqtb::gui::extent_t hint = size_hint();
            if (_horizontal_policy == size_policy_enum::FIXED && hint.width() > floor.width()) {
                floor.set_width(hint.width());
            }
            if (_vertical_policy == size_policy_enum::FIXED && hint.height() > floor.height()) {
                floor.set_height(hint.height());
            }
        }
        if (floor.width() < _minimum_size.width()) {
            floor.set_width(_minimum_size.width());
        }
        if (floor.height() < _minimum_size.height()) {
            floor.set_height(_minimum_size.height());
        }
        if (floor.width() > _maximum_size.width()) {
            floor.set_width(_maximum_size.width());
        }
        if (floor.height() > _maximum_size.height()) {
            floor.set_height(_maximum_size.height());
        }
        return floor;
    }

    ::hwshqtb::gui::extent_t item_t::effective_maximum_size() const {
        return _maximum_size;
    }

    void item_t::size_constraint_change_event(size_constraint_change_event_t*) {
        // Default: nothing. The setter already requests the repaint; a widget re-applies the parent's
        // layout in its own override.
    }

    void item_t::geometry_change_event(geometry_change_event_t*) {
        // Default: nothing. set_geometry() already stored the rectangle and will request the repaint;
        // only a subclass with work of its own (a container re-laying its children out) reacts here.
    }

    bool item_t::is_visible_in_window() const {
        for (const item_t* item = this; item != nullptr; item = parent_item_of(*item)) {
            if (!item->is_visible()) {
                return false;
            }
        }
        return true;
    }

    void item_t::set_focus_policy(focus_policy_enum policy) {
        if (_focus_policy == policy) {
            return;
        }
        _focus_policy = policy;
        // An item that can no longer hold the focus must not keep it (one focus_out_event()).
        if (policy == focus_policy_enum::NO_FOCUS && _has_focus) {
            clear_focus();
        }
    }

    focus_policy_enum item_t::focus_policy() const {
        return _focus_policy;
    }

    bool item_t::is_focusable() const {
        // "visible" means visible IN THE WINDOW: an item under a hidden ancestor is not focusable even
        // though its own flag is still true.
        return _focus_policy != focus_policy_enum::NO_FOCUS && is_enabled() && is_visible_in_window();
    }

    void item_t::set_focus() {
        // The ROUTER (or a host) is the authority: walk up the PARENT chain to the nearest item that
        // installed a focus sink and ask it. An item's own sink belongs to its DESCENDANTS, so it is
        // skipped — except for the topmost item, which has no parent and whose own sink (the router
        // installed there) is the outermost authority it can reach. In a retained tree only the root
        // has one, so this is the root's router; inside an immediate host the host's sink catches an
        // internal control's request first, so a control can never become a router-level focus
        // station, while the host itself asks the router above it.
        item_t* const parent = parent_item_of(*this);
        for (item_t* current = parent != nullptr ? parent : this; current != nullptr;
             current = parent_item_of(*current)) {
            if (current->_focus_sink) {
                current->_focus_sink(this);
                return;
            }
        }
    }

    void item_t::clear_focus() {
        if (!_has_focus) {
            return; // not the focus item: nothing to clear
        }
        item_t* const parent = parent_item_of(*this);
        for (item_t* current = parent != nullptr ? parent : this; current != nullptr;
             current = parent_item_of(*current)) {
            if (current->_focus_sink) {
                current->_focus_sink(nullptr);
                return;
            }
        }
    }

    bool item_t::has_focus() const {
        return _has_focus;
    }

    void item_t::set_focus_sink(std::function<void(item_t*)> sink) {
        _focus_sink = std::move(sink);
    }

    void item_t::set_has_focus(bool has_focus) {
        if (_has_focus == has_focus) {
            return; // exactly once per real change
        }
        _has_focus = has_focus;
        focus_change_event_t event{ has_focus };
        (void)::hwshqtb::metaobject::core_application::send_event(this, &event);
    }

    void item_t::focus_change_event(focus_change_event_t*) {
        // Default: nothing. A widget may override to request a repaint (a focus ring), a host to
        // restyle itself; the item layer owns no painting policy of its own.
    }

    bool item_t::is_hovered() const noexcept {
        return _hovered;
    }

    bool item_t::is_pressed(::hwshqtb::gui::mouse_button_enum button) const noexcept {
        return (_pressed_buttons & static_cast<std::uint8_t>(button)) != 0;
    }

    bool item_t::captures_pointer() const noexcept {
        return _captures_pointer;
    }

    void item_t::set_hovered(bool hovered) {
        if (hovered && (!is_enabled() || !is_visible_in_window())) {
            hovered = false; // a disabled or not-in-window item never enters hover
        }
        if (_hovered == hovered) {
            return; // not a real change: no event, no repaint
        }
        _hovered = hovered;
        hover_event_t event{ hovered };
        (void)::hwshqtb::metaobject::core_application::send_event(this, &event);
    }

    void item_t::set_pressed(::hwshqtb::gui::mouse_button_enum button, bool pressed) {
        const std::uint8_t bit = static_cast<std::uint8_t>(button);
        if (bit == 0) {
            return; // no button: nothing to record
        }
        const std::uint8_t next = pressed ? static_cast<std::uint8_t>(_pressed_buttons | bit)
                                          : static_cast<std::uint8_t>(_pressed_buttons & ~bit);
        if (next == _pressed_buttons) {
            return; // not a real change: no event, no repaint
        }
        _pressed_buttons = next;
        press_event_t event{ button, pressed };
        (void)::hwshqtb::metaobject::core_application::send_event(this, &event);
    }

    item_t* item_t::pointer_sink_owner() {
        item_t* const parent = parent_item_of(*this);
        for (item_t* current = parent != nullptr ? parent : this; current != nullptr;
             current = parent_item_of(*current)) {
            if (current->_pointer_sink) {
                return current;
            }
        }
        return nullptr;
    }

    bool item_t::capture_pointer() {
        item_t* const owner = pointer_sink_owner();
        if (owner == nullptr) {
            return false; // no input session: nobody would own the pointer
        }
        if (!_captures_pointer) {
            _captures_pointer = true;
            notify_repaint(); // pointer ownership is input state: every view has to repaint it
        }
        owner->_pointer_sink(this);
        return true;
    }

    void item_t::release_pointer() {
        if (!_captures_pointer) {
            return; // this item does not own the pointer: nothing to give back
        }
        _captures_pointer = false;
        notify_repaint();
        if (item_t* const owner = pointer_sink_owner()) {
            owner->_pointer_sink(nullptr);
        }
    }

    void item_t::set_pointer_sink(std::function<void(item_t*)> sink) {
        _pointer_sink = std::move(sink);
    }

    void item_t::paint_event(paint_event_t*) {
        // Default: nothing. An item without pixels of its own has nothing to draw.
    }

    void item_t::render(::hwshqtb::gui::painter_t&, const ::hwshqtb::gui::region_t&) {
        // Default: nothing. An item that is not a widget owns no pixels and drives no process, so a pass
        // has nothing to do here; widget_t and the immediate host override it.
    }

    void item_t::render(::hwshqtb::gui::painter_t& painter) {
        render(painter, ::hwshqtb::gui::region_t{ painter.clip_rect() });
    }

    void item_t::notify_repaint() {
        // Default: nothing. An item that is not a widget owns no pixels, so "needs a repaint" has no
        // meaning for it; widget_t overrides this to record the damage and ask the host for a frame.
    }

    void item_t::hover_event(hover_event_t*) {
        notify_repaint(); // the default repaint request: this item and every item ancestor
    }

    void item_t::press_event(press_event_t*) {
        notify_repaint(); // the default repaint request: this item and every item ancestor
    }

    void item_t::content_change_event(content_change_event_t*) {
        // Default: nothing. Only a subclass that paints decoration outside the content reacts.
    }

    void item_t::visible_change_event(visible_change_event_t*) {
        // Default: nothing. widget_t asks its host for a frame here; the setter already recorded the
        // item-level damage and emitted the signal.
    }

    void item_t::enabled_change_event(enabled_change_event_t*) {
        // Default: nothing (see visible_change_event()).
    }

    META_OBJECT(item_t);
}
