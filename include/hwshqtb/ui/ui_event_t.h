#ifndef HWSHQTB__UI__UI_EVENT_T_H
#define HWSHQTB__UI__UI_EVENT_T_H

/// @file ui_event_t.h
/// @brief The ui layer's state-change events: real event_t subclasses dispatched synchronously.

#include <cstdint>
#include <utility>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/region_t.h"
#include "hwshqtb/gui/window_event_t.h"
#include "hwshqtb/metaobject/basic_def.h"
#include "hwshqtb/metaobject/event_t.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    /// @brief Common base of the ui layer's state events.
    ///
    /// A ui event reports ONE state change of an item. It is constructed on the caller's stack,
    /// dispatched with core_application::send_event() (synchronous) and destroyed when that call
    /// returns. No ui event is posted, cloned or queued, so its payload is read only during the
    /// dispatch it was built for.
    /// @note Copying is deleted: the event's identity is the stack slot that carries the payload
    ///       (paint_event_t even holds a painter reference).
    /// @note merge_with() refuses folding: a state notification is a fact with a time, not a level
    ///       that a later event may rewrite. That keeps a future posting mistake from silently
    ///       dropping a notification.
    /// @warning Do not post a ui event. The base event_t::clone() would slice it to event_t, and the
    ///          dispatching call would return before the receiver read the payload.
    class ui_event_t: public ::hwshqtb::metaobject::event_t {
    public:
        ui_event_t(const ui_event_t&) = delete;
        ui_event_t& operator=(const ui_event_t&) = delete;

        /// @brief Refuse merging: a state notification is a fact, not a queued level.
        /// @param newer Ignored.
        /// @return false, always: the dispatcher must deliver this event instead of folding it into
        ///         an earlier one.
        bool merge_with(const ::hwshqtb::metaobject::event_t& newer) override {
            (void)newer;
            return false;
        }

    protected:
        /// @brief Construct with the ui event type.
        /// @param type The ui layer's event_type_enum value (UI_* range).
        /// @note noexcept.
        explicit ui_event_t(::hwshqtb::metaobject::event_type_enum type) noexcept:
            ::hwshqtb::metaobject::event_t(type) { }
    };

    /// @brief The paint pass of the current frame (QPaintEvent equivalent).
    ///
    /// It is gui::paint_event_t: ONE type for both layers. The payload (a painter and a region) belongs
    /// to the gui layer and the ui layer REUSES it, translating the painter into the item before the
    /// pass — a host that composes a window and paints items must not have to disambiguate two
    /// identically purposed types. The event type code is UI_PAINT, refined from the window-level
    /// event_t the window receives.
    using paint_event_t = ::hwshqtb::gui::paint_event_t;

    /// @brief An item's hover state changed.
    class hover_event_t final: public ui_event_t {
    public:
        /// @brief Construct for the new hover state.
        /// @param hovered The state the item now holds.
        /// @note noexcept.
        explicit hover_event_t(bool hovered) noexcept:
            ui_event_t(::hwshqtb::metaobject::event_type_enum::UI_HOVER),
            _hovered(hovered) { }

        /// @brief The new hover state.
        /// @note noexcept.
        bool hovered() const noexcept {
            return _hovered;
        }

    private:
        /// @brief The new hover state.
        bool _hovered = false;
    };

    /// @brief One mouse button edge was recorded on an item.
    class press_event_t final: public ui_event_t {
    public:
        /// @brief Construct for the button edge.
        /// @param button The button whose state changed.
        /// @param pressed true for a press, false for a release.
        /// @note noexcept.
        press_event_t(::hwshqtb::gui::mouse_button_enum button, bool pressed) noexcept:
            ui_event_t(::hwshqtb::metaobject::event_type_enum::UI_PRESS),
            _button(button),
            _pressed(pressed) { }

        /// @brief The button whose state changed.
        /// @note noexcept.
        ::hwshqtb::gui::mouse_button_enum button() const noexcept {
            return _button;
        }

        /// @brief The new state of that button.
        /// @note noexcept.
        bool pressed() const noexcept {
            return _pressed;
        }

    private:
        /// @brief The button whose state changed.
        ::hwshqtb::gui::mouse_button_enum _button = ::hwshqtb::gui::mouse_button_enum::LEFT;
        /// @brief The new state of that button.
        bool _pressed = false;
    };

    /// @brief An item rectangle changed.
    class geometry_change_event_t final: public ui_event_t {
    public:
        /// @brief Construct for the old and the new rectangle.
        /// @param old_geometry The rectangle the item held before the change.
        /// @param new_geometry The rectangle the item holds now.
        /// @note noexcept.
        geometry_change_event_t(::hwshqtb::gui::rect_t old_geometry,
                                ::hwshqtb::gui::rect_t new_geometry) noexcept:
            ui_event_t(::hwshqtb::metaobject::event_type_enum::UI_GEOMETRY_CHANGE),
            _old_geometry(old_geometry),
            _new_geometry(new_geometry) { }

        /// @brief The rectangle the item held before the change.
        /// @note noexcept.
        ::hwshqtb::gui::rect_t old_geometry() const noexcept {
            return _old_geometry;
        }

        /// @brief The rectangle the item holds now (the one already stored on the item).
        /// @note noexcept.
        ::hwshqtb::gui::rect_t new_geometry() const noexcept {
            return _new_geometry;
        }

    private:
        /// @brief The rectangle the item held before the change.
        ::hwshqtb::gui::rect_t _old_geometry{};
        /// @brief The rectangle the item holds now.
        ::hwshqtb::gui::rect_t _new_geometry{};
    };

    /// @brief An item gained or lost the keyboard focus.
    class focus_change_event_t final: public ui_event_t {
    public:
        /// @brief Construct for the new focus state.
        /// @param has_focus Whether the item now holds the focus.
        /// @note noexcept.
        explicit focus_change_event_t(bool has_focus) noexcept:
            ui_event_t(::hwshqtb::metaobject::event_type_enum::UI_FOCUS_CHANGE),
            _has_focus(has_focus) { }

        /// @brief Whether the item now holds the focus.
        /// @note noexcept.
        bool has_focus() const noexcept {
            return _has_focus;
        }

    private:
        /// @brief Whether the item now holds the focus.
        bool _has_focus = false;
    };

    /// @brief An item's minimum/maximum size constraint changed.
    class size_constraint_change_event_t final: public ui_event_t {
    public:
        /// @brief Construct the event.
        /// @note noexcept.
        size_constraint_change_event_t() noexcept:
            ui_event_t(::hwshqtb::metaobject::event_type_enum::UI_SIZE_CONSTRAINT_CHANGE) { }
    };

    /// @brief An item view's content extent or decoration changed.
    class content_change_event_t final: public ui_event_t {
    public:
        /// @brief Construct the event.
        /// @note noexcept.
        content_change_event_t() noexcept:
            ui_event_t(::hwshqtb::metaobject::event_type_enum::UI_CONTENT_CHANGE) { }
    };

    /// @brief An item's visibility flag changed.
    class visible_change_event_t final: public ui_event_t {
    public:
        /// @brief Construct for the new visibility.
        /// @param visible The state the item now holds.
        /// @note noexcept.
        explicit visible_change_event_t(bool visible) noexcept:
            ui_event_t(::hwshqtb::metaobject::event_type_enum::UI_VISIBLE_CHANGE),
            _visible(visible) { }

        /// @brief The new visibility.
        /// @note noexcept.
        bool visible() const noexcept {
            return _visible;
        }

    private:
        /// @brief The new visibility.
        bool _visible = false;
    };

    /// @brief A container asks for already-painted pixels to move (the payload role of QScrollEvent).
    ///
    /// @par Why an upward event
    /// The widget layer cannot move the window's pixels: only the host that owns the paint device can.
    /// Qt reaches it by calling QBackingStore::scroll() directly; here the tree has no window, so the
    /// request travels UP as an event — the same mechanism every other tree notification uses.
    ///
    /// @par Deliberate omissions (Qt carries them)
    /// No content position, no overshoot distance and no scroll state: this layer has no smooth
    /// scrolling and no kinetic overshoot, so those fields would have no producer and no consumer.
    class scroll_event_t final: public ui_event_t {
    public:
        /// @brief Construct the request.
        /// @param area The scrolled rectangle in WINDOW coordinates, already clipped to the host's area.
        /// @param dx The horizontal shift, same sign convention as window_t::scroll().
        /// @param dy The vertical shift.
        /// @note noexcept.
        scroll_event_t(::hwshqtb::gui::rect_t area, int dx, int dy) noexcept:
            ui_event_t(::hwshqtb::metaobject::event_type_enum::UI_SCROLL),
            _area(area),
            _dx(dx),
            _dy(dy) { }

        /// @brief The scrolled rectangle in window coordinates.
        /// @note noexcept.
        ::hwshqtb::gui::rect_t area() const noexcept {
            return _area;
        }

        /// @brief The horizontal shift.
        /// @note noexcept.
        int dx() const noexcept {
            return _dx;
        }

        /// @brief The vertical shift.
        /// @note noexcept.
        int dy() const noexcept {
            return _dy;
        }

        /// @brief Never merges.
        /// @return false, always: the window accumulates consecutive shifts itself and needs every
        ///         step's own (rect, dx, dy) to move the pixels in the right order.
        /// @note noexcept.
        bool merge_with(const ::hwshqtb::metaobject::event_t& newer) override {
            (void)newer;
            return false;
        }

    private:
        /// @brief The scrolled rectangle in window coordinates.
        ::hwshqtb::gui::rect_t _area;
        /// @brief The horizontal shift.
        int _dx = 0;
        /// @brief The vertical shift.
        int _dy = 0;
    };

    /// @brief An item's enabled flag changed.
    class enabled_change_event_t final: public ui_event_t {
    public:
        /// @brief Construct for the new enabled state.
        /// @param enabled The state the item now holds.
        /// @note noexcept.
        explicit enabled_change_event_t(bool enabled) noexcept:
            ui_event_t(::hwshqtb::metaobject::event_type_enum::UI_ENABLED_CHANGE),
            _enabled(enabled) { }

        /// @brief The new enabled state.
        /// @note noexcept.
        bool enabled() const noexcept {
            return _enabled;
        }

    private:
        /// @brief The new enabled state.
        bool _enabled = false;
    };

    /// @brief The content of rows @p first..@p last of a row view changed.
    class data_change_event_t final: public ui_event_t {
    public:
        /// @brief Construct for the row range.
        /// @param first The first changed row (inclusive).
        /// @param last The last changed row (inclusive).
        /// @note noexcept.
        data_change_event_t(int first, int last) noexcept:
            ui_event_t(::hwshqtb::metaobject::event_type_enum::UI_DATA_CHANGE),
            _first(first),
            _last(last) { }

        /// @brief The first changed row (inclusive).
        /// @note noexcept.
        int first() const noexcept {
            return _first;
        }

        /// @brief The last changed row (inclusive).
        /// @note noexcept.
        int last() const noexcept {
            return _last;
        }

    private:
        /// @brief The first changed row (inclusive).
        int _first = 0;
        /// @brief The last changed row (inclusive).
        int _last = 0;
    };

    /// @brief The visible row list of a row view was restructured (rows inserted or removed).
    class rows_change_event_t final: public ui_event_t {
    public:
        /// @brief Construct the event.
        /// @note noexcept.
        rows_change_event_t() noexcept:
            ui_event_t(::hwshqtb::metaobject::event_type_enum::UI_ROWS_CHANGE) { }
    };
}

#endif
