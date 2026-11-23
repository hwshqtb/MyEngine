#ifndef HWSHQTB__UI__IM_HOST_T_H
#define HWSHQTB__UI__IM_HOST_T_H

/// @file im_host_t.h
/// @brief im_host_t: the immediate host — a leaf item whose controls are found-or-created by name
///        and driven by the frame handler every pass.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/metaobject/any_t.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/metaobject/object_pointer_t.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/im_object_pool_t.h"
#include "hwshqtb/ui/im_pass_t.h"
#include "hwshqtb/ui/im_state_store_t.h"
#include "hwshqtb/ui/item_t.h"
#include "hwshqtb/ui/widget_t.h"
namespace hwshqtb::gui {
    class painter_t;
}
namespace hwshqtb::ui {
    /// @brief The immediate host: an item of the tree that owns its controls and drives them from a
    ///        user callback during every paint.
    ///
    /// @par Immediate host: an item, not a window
    /// This class is the IMMEDIATE host, and it is an item of the tree (a leaf whose children are its
    /// internal controls), never a window. Its retained-mode counterpart is ui::widget_window_t — a
    /// HOST that composes a gui::window_t and owns the widget tree, not a window itself. The two names
    /// say what the objects ARE (item vs tree host); a host is placed by a layout like any other item,
    /// while a window is not placed by anything.
    /// @par The three invariants
    ///  - I1: interact() only COLLECTS the step; it never touches a control's state.
    ///  - I2: a control's consumption and its painting happen back to back inside one control()
    ///    call; no other control's consumption can be inserted between them.
    ///  - I3: the geometry used by hit testing is produced in the same pass (control() lays the
    ///    control out before replaying the batch, never a rectangle from a previous frame).
    /// @par One batch, replayed per control
    /// The host is hit-tested like any other item. Every interaction the router delivers while the
    /// pointer is over it is appended to the pending batch. paint() runs the frame handler; each
    /// control() then replays the WHOLE batch in arrival order, each step with its own mouse
    /// position — never a folded "last position + accumulated edges" snapshot, which would fake a
    /// click that never happened. A step's wheel ticks and text input are replayed unchanged; the
    /// neutral settle snapshot carries neither, because both are edges and not levels. After the
    /// replay the control is painted immediately.
    /// @par Frame settle and phase merging
    /// On a pass that carries NO collected interaction, a control receives one neutral snapshot (no
    /// edges, the current pointer position): it lets the control clear its per-frame result
    /// (push_button_t::clicked) exactly like the retained path's once-per-frame interact(). A pass
    /// that DOES carry a batch replays only the batch, so the control sees the same two virtual calls
    /// (and the same arguments) the router would deliver. The settle's return value is ignored.
    /// The snapshot behind that neutral step exists because of the dispatcher's PHASE, not because of
    /// the immediate model: event_phase_t merges events (merge is on by default) and a phase delivers
    /// accumulated STATE, so the intermediate steps a control should have seen may have been folded
    /// away before the host received them. The host therefore remembers the last pointer level and
    /// synthesises the step a control expects, filling the gaps the merge created. That is the reason
    /// settle_input() and the im_pass_t pointer snapshot exist; an input path without merging would
    /// not need them.
    /// The replay model assumes event_phase_t::paint_last is true (its default): every non-paint event
    /// of a phase is dispatched before the paint, so the batch collected by interact() is the input of
    /// the pass that paints it. With paint_last disabled the input is not lost; it reaches the next
    /// pass instead (one frame late).
    /// @par Leaf and focus
    /// The host is a leaf item: it is never a widget, it is not a Tab site by default and its controls
    /// are not children of any widget (parent_widget() answers nullptr for them). Focus of the router
    /// is the first level; the host keeps its own second-level focus for key delivery. Its chain is
    /// the previous pass's visit order filtered by widget_t::is_focusable() (the same rule as the
    /// retained chain, so a control must opt in with set_focus_policy()). Tab is eaten here and moves
    /// the focus WITHOUT wrapping: at the end of the chain the host refuses the key (interact()
    /// returns false), which is how the router regains control. The host also installs its own focus
    /// sink, so an internal control's set_focus() stops here instead of reaching the router root.
    /// The second level is NOT a second store: inner_focus() and captured_control() read the
    /// controls' own item state (has_focus() / captures_pointer()) through the registered controls,
    /// and the host keeps no copy of either pointer. sync_inner_focus() clears every registered
    /// control except its target and then raises the target, so the focus_in/focus_out events still
    /// fire exactly once per real change. Likewise capture lives on the control; the host only
    /// enforces the single-owner rule through its pointer sink.
    /// A press on an internal control with CLICK_FOCUS/STRONG_FOCUS moves the inner focus through
    /// sync_inner_focus() BEFORE that control's own interact(), so the control sees has_focus() in
    /// the same pass. A press on a control without those policies, or on blank host space, leaves
    /// the inner focus untouched. The host is an ITEM too: the router applies the same click rule to
    /// it, so a click-focusable host gains the outer focus and the two levels stay in step.
    /// @par Coordinates and groups
    /// geometry() is the host's rectangle in its parent item, written only by the parent's layout.
    /// Controls are stacked by alloc_row() in host-local coordinates. begin_group() saves the
    /// painter, clips to the group area and pushes a group layer with its own origin, cursor and
    /// object-name prefix; the controls' geometry is group-local, and the host remembers each
    /// control's group origin for the duration of its control() call so the replayed mouse position
    /// can be mapped into it. The cursor/prefix logic itself is group_stack_t — the procedural dual
    /// of layout_t.
    /// @par Damage
    /// A pass damages nothing by itself. The only damage sources are a collected interaction
    /// (interact()), an explicit request_frame() and a control's own setter change: a control whose
    /// row did not change keeps its geometry, so an idle host reports no damage and schedules no
    /// frame.
    /// @par Lifetime
    /// Controls are object_t children of the host, so destroying the host deletes them and an
    /// object_pointer_t to one turns null. Their cross-pass state is NOT in the object: it lives in
    /// im_state_store_t, keyed by the control's object id. The object pool (im_object_pool_t) only
    /// saves construction; collect() deletes idle controls and the store hands their state to the
    /// object created by a later control() with the same name. Dropping the pool therefore costs
    /// speed, never state. The pass context (im_pass_t) is reused every pass and owns the batch, the
    /// pointer snapshot, the current group stack and the write-back log.
    /// @par The reuse switch
    /// set_reuse_enabled(false) turns the pool off for diagnosis: controls are built for one pass,
    /// driven, written back and deleted at its end. The visible result must equal the pooled path,
    /// because the state was never in the object. The switch covers reflected property state and the
    /// pixels a pass produces; object-lifetime session state that is not a property (inner focus,
    /// pointer capture) cannot outlive a discard and is the same only while a pass is running.
    class HWSHQTB_UI_EXPORT im_host_t final : public item_t {
        OBJECT
    public:
        /// @brief Construct an empty host with no frame handler.
        im_host_t();
        /// @brief Destroy the host; the object tree deletes every control.
        ~im_host_t() override;
        im_host_t(const im_host_t&) = delete;
        im_host_t& operator=(const im_host_t&) = delete;
        im_host_t(im_host_t&&) = delete;
        im_host_t& operator=(im_host_t&&) = delete;
        /// @name Frame
        /// @{
        /// @brief Install the callback that enumerates the controls for one pass.
        /// @param handler The callback, invoked from paint() with this host; an empty function
        ///        removes the handler (the pass then paints nothing and visits no control).
        /// @note The handler must only call control()/widget()/alloc_row()/begin_group()/end_group();
        ///       the painter is only valid inside it.
        void set_frame_handler(std::function<void(im_host_t&)> handler);
        /// @brief Ask for the next frame: walk up the item parents to the nearest widget and damage
        ///        the host's own rectangle there, so the host's repaint sink reaches the window event
        ///        loop.
        /// @note A no-op when no widget ancestor exists (the host is not inside a widget tree).
        /// @note Only the host's rectangle is damaged, never the widget's whole client area.
        void request_frame();
        /// @brief The number of passes started so far (0 before the first paint()).
        /// @note noexcept. This is the clock collect() and the object pool use.
        std::uint64_t pass() const noexcept;
        /// @}
        /// @name Controls
        /// @brief Take the control of @p type_name named @p object_name (from the pool or a fresh
        ///        construction), restore its state, place it, replay the pending batch into it,
        ///        paint it and return its User property value.
        /// @param type_name The registered class name (object_t::register_type / META_REGISTER_TYPE).
        /// @param object_name The object id; it becomes the control's object name (the group prefix
        ///        is added in front of it). An empty name is rejected: one Warning, nothing taken.
        /// @return Three distinct answers, and the two failures are not the same thing:
        ///         - the class declares a User property: that property's value for this pass (a
        ///           defined any_t);
        ///         - the control exists and was driven, but its class declares NO User property: a
        ///           VALID, declared-only void any_t — is_valid() is true and is_initialized() is
        ///           false. "Driven; there is nothing to report" is a SUCCESS, so a caller reads it as
        ///           `value.get<bool>()` only after is_initialized();
        ///         - a real failure — empty object_name, unknown type, or a created object that is not
        ///           a widget: a default-constructed INVALID any_t (is_valid() is false), plus one
        ///           Warning (an unknown type keeps the create() diagnostic).
        /// @note The row height is max(1, effective_size_hint().height()) and the width is the current
        ///       layer's width; a control with a set_fixed_size() constraint keeps both fixed extents
        ///       at the row origin instead. The control is placed before the replay (I3).
        /// @note Placement damages the control only when its rectangle actually changed: an idle pass
        ///       leaves the tree's damage untouched.
        /// @note A different type under an existing name warns and KEEPS the first type while that
        ///       object is alive (the object name is the identity, the type is not). Once the object is
        ///       recycled the new type is accepted and the old state is discarded by the store.
        ::hwshqtb::metaobject::any_t control(std::string_view type_name, std::string_view object_name);
        /// @brief Take the control without driving it (for configuring typed properties).
        /// @param type_name The registered class name.
        /// @param object_name The object id (the group prefix is added in front of it).
        /// @return The widget, or nullptr for the same failure cases as control() (each logs one
        ///         Warning).
        /// @note The stored state is applied when this call CREATES the control. An existing object
        ///       keeps the state it already has, so a widget() call followed by setters and then
        ///       control() keeps the caller's writes.
        /// @note A NEW control receives a degenerate rectangle (the current layer's width and its size
        ///       hint's height) without advancing the cursor; an existing control keeps its geometry
        ///       and the cursor does not move either. The next control() places it.
        widget_t* widget(std::string_view type_name, std::string_view object_name);
        /// @brief Delete the controls that have not been visited for @p max_idle_passes passes.
        /// @param max_idle_passes The idle limit; a control is deleted when
        ///        `pass() - last_seen_pass >= max_idle_passes`. 0 deletes every control that was not
        ///        visited in the pass being collected.
        /// @note The host calls collect() itself at the end of every pass (600 by default), so an
        ///       explicit call is only needed for a different policy.
        /// @note Only the object is lost: its state stays in the store, so a later control() with the
        ///       same name restores it. The host writes the live controls' state back before the
        ///       sweep (so a value written outside a pass is kept as well) and clears the focus of a
        ///       control that is going away before the delete (one focus_out_event).
        void collect(std::uint32_t max_idle_passes = 600);
        /// @brief Turn the object pool on or off (the diagnostic switch that proves reuse is only an
        ///        optimization).
        /// @param enabled true (the default) to reuse controls across passes; false to build a control
        ///        for one pass and delete it after the pass has written its state back.
        /// @note The switch must not be changed from inside the frame handler: a pass is driving the
        ///       controls it would delete. A call there is ignored with one Warning.
        /// @note Switching persists every live control's state first and then deletes the objects, so
        ///       the next pass restores the same values in either mode. The layout facts are cleared
        ///       with them.
        /// @note "Off" disables CROSS-pass reuse only. Within one pass the same id still answers the
        ///       same object, because widget() and control() must configure and drive one control.
        void set_reuse_enabled(bool enabled);
        /// @brief Whether the object pool is enabled.
        /// @note noexcept.
        bool reuse_enabled() const noexcept;
        /// @name Inner session
        /// @brief The control that holds the host's second-level keyboard focus, or nullptr.
        /// @return The registered control whose has_focus() is true; the widget's own item state is
        ///         the truth, this is a pure query with no stored copy.
        /// @note At most one control can hold it (sync_inner_focus() clears every other one).
        widget_t* inner_focus() const noexcept;
        /// @brief The control that owns the pointer capture inside the host, or nullptr.
        /// @return The registered control whose captures_pointer() is true; the item state is the
        ///         truth, this is a pure query with no stored copy.
        widget_t* captured_control() const noexcept;
        /// @name Cursor and groups
        /// @brief The current layer's stacking cursor.
        ::hwshqtb::gui::point_t cursor() const noexcept;
        /// @brief Move the current layer's stacking cursor.
        /// @param cursor The new cursor, in the current layer's coordinates.
        void set_cursor(::hwshqtb::gui::point_t cursor);
        /// @brief Reserve one row in the current layer: {cursor.x, cursor.y, layer width, height}.
        /// @param height The row height; a value <= 0 is treated as 1.
        /// @return The row rectangle, in the current layer's coordinates.
        /// @note The cursor advances by height + the row spacing, so consecutive controls stack.
        ::hwshqtb::gui::rect_t alloc_row(int height);
        /// @brief Start a group: save the painter, clip/translate to @p area, and push a group-local
        ///        coordinate system plus an object-name prefix.
        /// @param area The group rectangle, in the CURRENT layer's coordinates. The group has no
        ///        container adaptation: its size is exactly this rectangle.
        /// @param id_prefix The name prefix; controls created inside are named
        ///        `current_prefix + id_prefix + "/" + object_name`.
        /// @note Must be matched by exactly one end_group() and, like control(), only be called from
        ///       the frame handler: it touches the frame's painter.
        void begin_group(::hwshqtb::gui::rect_t area, std::string_view id_prefix);
        /// @brief End the current group: pop its prefix/cursor and restore the painter.
        /// @note A no-op when no group is open (the host's own layer cannot be popped).
        void end_group();
        /// @name Item interface
        /// @brief Run one pass: eat Tab, start the cursor, call the frame handler, then finish the
        ///        batch and collect (see the class note).
        /// @param painter The painter of the current frame, already translated to the parent's
        ///        origin.
        /// @note The controls are painted by their control() calls, inside the frame handler; this
        ///       function itself paints no control. An invisible host skips the whole pass and its
        ///       pending batch is dropped.
        /// @note The inherited one-argument paint(painter) stays visible through this using
        ///       declaration (see widget_t's note): C++ name lookup would otherwise hide it behind the
        ///       override below.
        /// @param event The pass; event->painter() is the frame's painter and event->damage() the area to
        ///        paint, in the painter's current (parent) coordinates — it reaches this host's own
        ///        coordinates here.
        /// @note The inherited one-argument form stays reachable through this using declaration: the
        ///       override below would otherwise hide it (C++ name lookup stops at the first class that
        ///       declares that name).
        using item_t::render;
        void render(::hwshqtb::gui::painter_t& painter, const ::hwshqtb::gui::region_t& damage) override;
        /// @brief The hit item at @p local: the host itself when the point is inside rect().
        /// @param local The point in this host's coordinates.
        /// @return this, or nullptr when the point is outside the host or the host is not
        ///         visible/enabled.
        /// @note A host is a leaf: it never descends into its controls (they are not items of the
        ///       parent's tree and are replayed by the frame handler).
        item_t* hit(::hwshqtb::gui::point_t local) const override;
        /// @brief Collect one interaction for this pass (I1: the controls are not touched here).
        /// @param in The interaction snapshot; in.mouse is already in this host's local coordinates.
        /// @return For a MOUSE step: true while the host is visible and enabled (the host claims every
        ///         pointer step delivered to it). For a KEY step: true only when the host will handle
        ///         the key — Tab when the non-wrapping focusable chain has a target in that direction,
        ///         any other key while an internal focus exists. Otherwise false and the step is NOT
        ///         collected, which hands it back to the router (its Tab fallback).
        /// @note A collected step calls request_frame(), so the pass that replays it is painted even
        ///       when nothing else was damaged.
        bool interact(const ::hwshqtb::gui::interaction_t& in) override;
        /// @brief Event entry point: the immediate host applies every ui event in the pass that
        ///        raised it, without a queued or forwarded step.
        /// @param event The event; nullptr falls through to item_t::event().
        /// @return true when a ui event was applied; otherwise the base's answer.
        /// @note The retained path MAY defer: the router only records state and the tree paints on
        ///       whatever frame the host schedules. The immediate path may NOT: its batch is replayed
        ///       inside the very paint() pass that carries it, so a reaction that only became visible
        ///       on a later pass would contradict the frame the pass already drew. That is the whole
        ///       difference between the two hosts.
        /// @note The interception keeps the inner controls in step in the same call: a lost focus
        ///       clears the inner focus here, and a hide/disable drops the collected batch here,
        ///       before this call returns.
        bool event(::hwshqtb::metaobject::event_t* event) override;
    protected:
        /// @brief The host lost the ITEM-level focus: clear the inner focus one level down.
        /// @param event The focus event; event.has_focus() is false here, otherwise nothing happens.
        /// @note Without this the last focused control would keep painting its focus ring after the
        ///       router moved the outer focus away (the two levels must mirror each other).
        /// @note Hidden from the meta-info like the base hook.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void focus_change_event(focus_change_event_t* event) override;
        /// @brief The host rectangle changed: keep the top layer's row width in step.
        /// @param event The geometry event; the new rectangle is already stored on the item.
        /// @note The width is the layer's wrap width for alloc_row(), so a resized host wraps its
        ///       rows at the new width.
        void geometry_change_event(geometry_change_event_t* event) override;
        /// @brief The host was hidden: drop the batch collected for a pass that will not paint.
        /// @param event The visibility event; the flag and the signal are item_t's.
        void visible_change_event(visible_change_event_t* event) override;
        /// @brief The host was disabled: drop the batch collected before it.
        /// @param event The enabled event; the flag and the signal are item_t's.
        /// @note A disabled host is not hit (its last state stays painted until it is shown again).
        void enabled_change_event(enabled_change_event_t* event) override;
    private:
        /// @brief One control the host's layout knows about, keyed by its object id.
        struct layout_entry_t {
            /// @brief The type name used at creation (the "kept the first type" rule compares it).
            std::string type_name;
            /// @brief The host-local origin of the group the control was last laid out in.
            ::hwshqtb::gui::point_t group_origin{};
        };
        /// @brief Switch the host's second-level focus to @p next (nullptr clears it) and mirror it
        ///        into the two controls' ITEM-level has_focus().
        /// @param next The control that should hold the inner focus, or nullptr.
        /// @note Clears every registered control except @p next (setting their has_focus() false) and
        ///       then raises @p next, so a real change produces exactly one focus_out/focus_in pair.
        ///       The ONE place a control's focus flag is changed by the host (I1 never does).
        void sync_inner_focus(widget_t* next);
        /// @brief Whether @p control is registered in this host's object pool or its per-pass set.
        /// @param control The control to look for.
        /// @note The registered set is the host's control set; a widget merely parented to the host
        ///       without going through control()/widget() is not a control and cannot take the inner
        ///       focus.
        bool is_control(const widget_t& control) const noexcept;
        /// @brief The object id of @p object_name in the current layer (prefix included).
        std::string key_of(std::string_view object_name) const;
        /// @brief The second-level focus chain: the previous pass's controls that are focusable.
        /// @note The SAME filter as the retained chain (widget_t::is_focusable()), so a control joins
        ///       it only with an explicit focus policy. Pure query: no state is changed (I1).
        std::vector<widget_t*> focus_chain() const;
        /// @brief Where Tab/Shift+Tab would move the focus inside @p chain, or nullptr at its end.
        /// @note NO wrapping: when the chain ends the host refuses the key and the router acts, so the
        ///       hand-off happens by "not accepting", never by jumping back to the other end here.
        widget_t* tab_target(bool backwards, const std::vector<widget_t*>& chain) const;
        /// @brief Whether the host would HANDLE @p in as a keyboard step (I1: a pure query).
        /// @note Only keys the host can act on are collected: Tab when the non-wrapping chain has a
        ///       target in that direction, any other key while an internal focus exists. Everything
        ///       else returns false, leaving the key to the router.
        bool accepts_key(const ::hwshqtb::gui::interaction_t& in) const;
        /// @brief Eat the Tab steps from the batch and move the second-level focus (no wrapping).
        /// @note Only keys the host accepted in interact() reach the batch, so an accepted Tab always
        ///       has a target; the guard keeps the move well defined anyway.
        void handle_tab();
        /// @brief Make @p control the pointer owner inside the host, or give the pointer back.
        /// @param next The control that consumed the press, or nullptr on the release.
        /// @note Writes the controls' own captures_pointer() through capture_pointer() /
        ///       release_pointer(): the item state is the truth, and captured_control() reads it back.
        ///       The host's pointer sink enforces that only one registered control owns it.
        void set_captured(widget_t* next);
        /// @brief The neutral frame snapshot: no edges, the current pointer, the current level.
        /// @param control The control the step is for.
        /// @param group_origin The host-local origin of the group the control sits in.
        /// @note It lets a control clear its per-frame result on a pass without events (G2) and keeps
        ///       hover feedback current; its return value is ignored.
        /// @note It synthesises the step the control would have seen from the last delivered pointer
        ///       LEVEL. The reason is the dispatcher phase's merge, which folds intermediate input
        ///       steps into one and delivers state: without the merge the control would already have
        ///       been driven by each step and no reconstruction would be needed.
        ::hwshqtb::gui::interaction_t settle_input(const widget_t& control,
                                                   ::hwshqtb::gui::point_t group_origin) const;
        /// @brief Translate one pending step into @p control's local coordinates.
        /// @param step The collected step; its claim is read and updated here.
        /// @note The claim travels in the step, not in a parallel array, so erasing a step (a Tab the
        ///       host ate) can never misalign the claims of the remaining steps.
        ::hwshqtb::gui::interaction_t make_input(const widget_t& control,
                                                 ::hwshqtb::gui::point_t group_origin,
                                                 im_pass_t::step_t& step);
        /// @brief Mirror the host-computed hover/press of one step into @p control's ITEM state.
        /// @param control The control the step is about.
        /// @param in The snapshot the host computed for that control.
        /// @param consumed Whether the control consumed that snapshot.
        /// @note The item state is what a second output paints from, so the host's inner state is
        ///       written through the item API instead of staying private. A press belongs to the
        ///       control that CONSUMED it (a control that ignores the press must not report
        ///       is_pressed() afterwards); hover is refused by the item itself when the control is
        ///       disabled or not visible.
        void mirror_pointer_state(widget_t& control, const ::hwshqtb::gui::interaction_t& in,
                                  bool consumed);
        /// @brief The rectangle a control gets from widget() before its first control().
        ::hwshqtb::gui::rect_t degenerate_geometry(const widget_t& control) const;
        /// @brief Take or create the widget for (@p type_name, @p object_name).
        /// @return The control, or nullptr (a Warning has been logged by then).
        /// @note A control created by this call restores its stored state; an existing control keeps
        ///       its state (a caller write between passes is not overwritten).
        widget_t* acquire(std::string_view type_name, std::string_view object_name);
        /// @brief Visit every live control, pooled and per-pass alike.
        /// @tparam visitor_t Callable as `void(std::string_view id, object_t& object)`.
        /// @param visitor The callback.
        template <typename visitor_t>
        void for_each_control(visitor_t&& visitor) const {
            _pool.for_each(visitor);
            for (const auto& [id, control] : _volatile) {
                ::hwshqtb::metaobject::object_t* const object = control.data();
                if (object != nullptr) {
                    visitor(std::string_view(id), *object);
                }
            }
        }
        /// @brief Whether a live control is registered under @p id (pooled or per-pass).
        bool live_control(std::string_view id) const;
        /// @brief Write the state of every control acquired in the open pass back into the store.
        /// @note One write per control per pass, after the handler returns: this also captures a
        ///       widget()-only configuration and a write made after the last control() call.
        void write_back();
        /// @brief Write every live control back, then delete all of them (the mode-switch cleanup).
        /// @note The objects are gone but their state is in the store, so the next pass restores it.
        void drop_all_controls();
        /// @brief Delete the controls created for the pass that just ended (reuse disabled only).
        /// @note Their state was written back first; the focus of a control that is going away is
        ///       cleared before the delete, exactly like collect().
        void drop_volatile_controls();
        /// @brief The user callback that enumerates the controls of one pass.
        std::function<void(im_host_t&)> _handler;
        /// @brief The painter of the pass being driven, or nullptr outside the handler.
        ::hwshqtb::gui::painter_t* _painter = nullptr;
        /// @brief The number of passes started so far (collect()'s clock).
        std::uint64_t _pass_count = 0;
        /// @brief The reusable controls of this host: id -> object + last visit pass (never an owner).
        im_object_pool_t _pool;
        /// @brief The cross-pass state of the controls, keyed by the same id (owns copies only).
        im_state_store_t _store;
        /// @brief The controls built for the current pass while reuse is disabled (never pooled).
        std::unordered_map<std::string, ::hwshqtb::metaobject::object_pointer_t<widget_t>> _volatile;
        /// @brief The host's layout facts per id: creation type and last group origin.
        std::unordered_map<std::string, layout_entry_t> _layout;
        /// @brief The batch, pointer snapshot, group stack and write-back log of the current pass.
        im_pass_t _pass;
        /// @brief Whether controls are reused across passes.
        bool _reuse = true;
        /// @brief Whether paint() collects at the end of every pass.
        bool _collect_enabled = true;
        /// @brief The idle limit paint()'s collect() uses.
        std::uint32_t _collect_max = 600;
    };
}

#endif
