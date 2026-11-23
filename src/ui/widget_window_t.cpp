/// @file widget_window_t.cpp
/// @brief widget_window_t: the retained host — the router owns the routing, the tree paints itself, and
///        this file owns the tree (root ownership, the two item sinks) plus the composition of the
///        window it drives.

#include "hwshqtb/ui/widget_window_t.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/gui/window_event_t.h"
#include "hwshqtb/ui/ui_event_t.h"
#include "hwshqtb/ui/widget_t.h"

namespace hwshqtb::ui {
    namespace detail {
        /// @brief The window this host creates for itself: it forwards every frame and input hook to the
        ///        host and adds nothing.
        ///
        /// It exists because a host that owns its window must still receive the hooks, and a
        /// gui::window_t cannot know about a widget tree. Nothing else lives here: the frame policy is
        /// the window's, the tree is the host's, and this class is the wire between them.
        struct tree_window_t final: ::hwshqtb::gui::window_t {
            /// @brief Create the window (see window_t's (title, size) constructor).
            /// @param title The window title.
            /// @param size The client area, in window coordinates.
            tree_window_t(std::string_view title, ::hwshqtb::gui::extent_t size):
                ::hwshqtb::gui::window_t(title, size) { }

            /// @brief The host every hook is forwarded to; wired by the host right after construction.
            widget_window_t* host = nullptr;

        protected:
            void paint_event(::hwshqtb::gui::paint_event_t* event) override {
                if (host != nullptr) {
                    host->paint_event(event);
                }
            }

            void resize_event(::hwshqtb::gui::resize_event_t* event) override {
                if (host != nullptr) {
                    host->resized(*event);
                }
            }

            void interact_event(::hwshqtb::gui::interact_event_t* event) override {
                if (host != nullptr) {
                    host->interact(event->step());
                }
            }

            void focus_out_event(::hwshqtb::gui::focus_out_event_t*) override {
                if (host != nullptr) {
                    host->focus_lost();
                }
            }

            void leave_event(::hwshqtb::gui::leave_event_t*) override {
                if (host != nullptr) {
                    host->pointer_left();
                }
            }
        };
    }

    widget_window_t::widget_window_t(std::string_view title, ::hwshqtb::gui::extent_t size) {
        auto owned = std::make_unique<detail::tree_window_t>(title, size);
        _window = owned.get();
        // The host pointer is wired AFTER the window is fully constructed: the adapter must never call
        // back into a host whose members are still being initialised.
        owned->host = this;
        _owned_window = std::move(owned);
        _owned = true;
    }

    widget_window_t::widget_window_t(::hwshqtb::gui::window_t& window):
        _window(&window) { }

    widget_window_t::~widget_window_t() {
        // The tree goes first: the router's destructor fires the focus sinks, and neither the tree nor a
        // frame may reach a window this host is about to release. Only then is an owned window destroyed
        // (a window the caller passed in stays the caller's).
        destroy_tree();
        if (_owned) {
            _owned_window.reset();
        }
    }

    widget_t* widget_window_t::root_widget() const {
        return _root.get();
    }

    void widget_window_t::set_root_widget(widget_t* widget) {
        if (_root.get() == widget) {
            return; // already adopted: leave its geometry, sink and router alone
        }
        if (widget != nullptr) {
            // Detach FIRST: the previous root may be an ancestor of the new one, and adopting the widget
            // after the reset below would mean adopting an object the reset already deleted.
            widget->set_parent(nullptr);
        }
        destroy_tree();
        _root.reset(widget);
        if (_root != nullptr) {
            // The ONE channel between the tree and this host: the root carries an object_t* back to
            // this object, so a descendant's update() or a container's pixel shift arrives as an event
            // (plain PAINT, or a scroll_event_t) instead of a stored callable.
            _root->set_request_receiver(this);
            _root->set_geometry(::hwshqtb::gui::rect_t{ ::hwshqtb::gui::point_t{}, _window->size() });
            apply_minimum_size();
            // The router is the focus authority and owns the routing logic; the host only adapts.
            _router = std::make_unique<input_router_t>(*_root);
        }
    }

    bool widget_window_t::event(::hwshqtb::metaobject::event_t* request) {
        if (request == nullptr || _root == nullptr || _window == nullptr) {
            return false;
        }
        switch (request->type()) {
        case ::hwshqtb::metaobject::event_type_enum::PAINT:
            // A frame is due: the tree's damage becomes the window's pending damage and the window
            // schedules the frame (QWidget::update). Its frame calls paint_event(), which repaints the
            // tree — there is deliberately no separate "publish" step in between.
            //
            // A size-constraint change is one of the things that asks for a frame (widget_t raises the
            // request through update()), so this is also where the window's floor is refreshed.
            apply_minimum_size();
            _window->set_pending_damage(_root->damage());
            _window->update();
            return true;
        case ::hwshqtb::metaobject::event_type_enum::UI_REPAINT:
            // An IMMEDIATE frame was asked for (QWidget::repaint): the same damage, painted before this
            // request returns instead of being coalesced into the next loop round.
            _window->set_pending_damage(_root->damage());
            _window->repaint();
            return true;
        case ::hwshqtb::metaobject::event_type_enum::UI_SCROLL: {
            const auto* const scroll = static_cast<const scroll_event_t*>(request);
            // The window moves the pixels on the device and answers the region it marked damaged: the
            // band of every shift since the last painted frame, not this step's strip. When the input
            // outruns the frames the following shift drags the strip this one exposed further into the
            // area, so only the accumulated band covers all of it.
            const ::hwshqtb::gui::region_t exposed =
                _window->scroll(scroll->area(), scroll->dx(), scroll->dy());
            if (exposed.is_empty()) {
                return true; // nothing became stale, so there is no damage to move
            }
            // The pixels moved, so the tree's record of the same pixels moves with them; the band is
            // recorded AFTER that, because it is the damage the shift CREATED.
            _root->translate_damage(scroll->area(), scroll->dx(), scroll->dy());
            for (const ::hwshqtb::gui::rect_t& strip : exposed.rects()) {
                _root->update(strip);
            }
            // The record alone paints nothing: a frame is requested the same way every other one is.
            _window->set_pending_damage(_root->damage());
            _window->update();
            return true;
        }
        default:
            return ::hwshqtb::metaobject::object_t::event(request);
        }
    }

    input_router_t* widget_window_t::router() const {
        return _router.get();
    }

    void widget_window_t::render(::hwshqtb::gui::painter_t& painter) {
        if (_root == nullptr) {
            return; // no tree: the painter's content is all there is
        }
        _root->render(painter);
    }

    void widget_window_t::paint_event(paint_event_t* event) {
        if (_root == nullptr || event == nullptr) {
            return; // no tree: the frame's clear is all there is to paint
        }
        // The event is the pass: the gui layer built it with the clipped painter and the area it cleared,
        // and the tree consumes the very same type — so it is forwarded UNCHANGED, with no wrapper.
        _root->render(event->painter(), event->damage());
    }

    void widget_window_t::resized(const ::hwshqtb::gui::resize_event_t& event) {
        if (_root != nullptr) {
            // set_geometry() requests the repaint, so the new layout is drawn without another call.
            _root->set_geometry(
                ::hwshqtb::gui::rect_t{ ::hwshqtb::gui::point_t{}, event.size() });
        }
        if (_router != nullptr) {
            // An item that became invisible while it held the focus must not keep receiving keys.
            if (item_t* const focused = _router->focus_item();
                focused != nullptr && !focused->is_visible_in_window()) {
                _router->set_focus(nullptr);
            }
        }
    }

    void widget_window_t::interact(const ::hwshqtb::gui::interaction_t& event) {
        if (_router == nullptr) {
            return; // no tree, no session to route into
        }
        // The step's edges were written into the window's input state before this call, so routing it is
        // all this host has to do. A consumed step asks for the frame here as well (the window also asks
        // for one for the step itself, and update() coalesces both): interact() stays correct when a
        // caller drives the routing without a window report.
        if (_router->dispatch(event) && _window != nullptr) {
            _window->update();
        }
    }

    void widget_window_t::focus_lost() {
        // The pointer session of this window has ended with the focus: no later step can clear what the
        // last one set, and the router owns that state. The damage the clearing recorded on the items
        // becomes the window's pending damage, exactly like the repaint request does.
        if (_router != nullptr) {
            _router->clear_pointer_state();
        }
        if (_root != nullptr && _window != nullptr) {
            _window->set_pending_damage(_root->damage());
            _window->update();
        }
    }

    void widget_window_t::pointer_left() {
        // The pointer left the client area: the same end of the same session as focus_lost().
        if (_router != nullptr) {
            _router->clear_pointer_state();
        }
        if (_root != nullptr && _window != nullptr) {
            _window->set_pending_damage(_root->damage());
            _window->update();
        }
    }

    void widget_window_t::apply_minimum_size() {
        if (_root == nullptr || _window == nullptr) {
            return;
        }
        const ::hwshqtb::gui::extent_t floor = _root->minimum_size_hint();
        if (floor == _last_minimum_size) {
            return; // the window manager already has this floor: do not poke it every frame
        }
        _last_minimum_size = floor;
        _window->set_minimum_size(floor);
    }

    void widget_window_t::destroy_tree() {
        // Order matters: the router holds the focus sink on the root and fires focus_out_event() from its
        // destructor, so it has to go first. The request channel and the sinks are detached next, while
        // the root they are installed on is still alive.
        _router.reset();
        if (_root != nullptr) {
            _root->set_request_receiver(nullptr);
        }
        _root.reset();
    }

    void widget_window_t::show() {
        _window->show();
    }

    void widget_window_t::hide() {
        _window->hide();
    }

    void widget_window_t::update() {
        _window->update();
    }

    void widget_window_t::repaint() {
        _window->repaint();
    }

    void widget_window_t::present() {
        _window->present();
    }

    void widget_window_t::close() {
        _window->close();
    }

    bool widget_window_t::is_visible() const noexcept {
        return _window->is_visible();
    }

    bool widget_window_t::close_requested() const noexcept {
        return _window->close_requested();
    }

    std::uint32_t widget_window_t::window_id() const noexcept {
        return _window->window_id();
    }

    void widget_window_t::set_title(std::string_view title) {
        _window->set_title(title);
    }

    ::hwshqtb::gui::extent_t widget_window_t::size() const noexcept {
        return _window->size();
    }

    const ::hwshqtb::gui::region_t& widget_window_t::pending_damage() const noexcept {
        return _window->pending_damage();
    }

    const ::hwshqtb::gui::region_t& widget_window_t::presented_region() const noexcept {
        return _window->presented_region();
    }

    std::size_t widget_window_t::painted_frame_count() const noexcept {
        return _window->painted_frame_count();
    }

    ::hwshqtb::gui::region_t widget_window_t::scroll(::hwshqtb::gui::rect_t rect, int dx, int dy) {
        return _window->scroll(rect, dx, dy);
    }

    void widget_window_t::save_screenshot(std::string_view path) {
        _window->save_screenshot(path);
    }

    void widget_window_t::save_painted_frame(std::string_view path) {
        _window->save_painted_frame(path);
    }

    ::hwshqtb::gui::input_state_t& widget_window_t::input_state() noexcept {
        return _window->input_state();
    }

    const ::hwshqtb::gui::input_state_t& widget_window_t::input_state() const noexcept {
        return _window->input_state();
    }

    ::hwshqtb::gui::window_t& widget_window_t::window() noexcept {
        return *_window;
    }

    const ::hwshqtb::gui::window_t& widget_window_t::window() const noexcept {
        return *_window;
    }
}
