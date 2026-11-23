#ifndef HWSHQTB__UI__WIDGET_T_H
#define HWSHQTB__UI__WIDGET_T_H

/// @file widget_t.h
/// @brief widget_t: the visual control shared by the retained and the immediate host.

#include <cstddef>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/item_t.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    class layout_t;
    class input_router_t;
    class scroll_view_t;

    /// @brief A visual control: an item with a filtered view of the object tree, interaction,
    ///        properties and a layout (the QWidget subset W1-W3 needs).
    ///
    /// @par One tree, two views
    /// A widget IS an item_t and an object_t: ownership and lifetime stay entirely with the object
    /// tree (a widget's destructor detaches it from its parent, and a parent deletes its children).
    /// Visual traversal — rendering, hit testing, layout, z-order — only looks at the children whose
    /// object_t::is_item() is true, so a widget may own plain object_t children (a controller, a
    /// model, a timer) without them ever being arranged, rendered or hit-tested. The reflected
    /// visible/enabled properties and their signals belong to item_t, because every visual node has
    /// that state; keyboard focus is ITEM-level too, so a widget and an immediate host are peers in
    /// the router's Tab chain. is_widget() is narrowed to "is a ui::widget_t" (parent_widget(), type
    /// checks).
    ///
    /// @par Geometry
    /// geometry is relative to the parent item (the top-left corner is the offset the parent's paint
    /// translates by). rect(), map_to_global() and map_from_global() come from item_t and accumulate
    /// the whole item ancestor chain, so a point can be moved between any two levels of the tree.
    ///
    /// @par Host neutrality
    /// interact() takes a gui::interaction_t, never an event_t, and paint() takes a painter_t, never
    /// a window. update() only ASKS for a repaint: a host installs the sink (widget_window_t installs
    /// one on the root that reaches window_t::update()) and decides when the frame is painted. The
    /// same two functions therefore serve the retained host and the immediate host.
    ///
    /// @par Control contract
    /// A concrete control of this layer (a widget_t subclass with reflected properties) follows three
    /// rules, and every control shipped here is written against them:
    ///  1. Its User property is THIS FRAME'S EVENT, never persistent state. push_button_t::clicked
    ///     and check_box_t::toggled are true only on the frame that produced them and false every
    ///     other frame; a caller collects the frame's result with them and must not treat them as the
    ///     control's value.
    ///  2. Every persistent state has a setter (set_text, set_checked, set_value, ...). The caller
    ///     keeps that value itself and feeds it into the control every frame, so the control never
    ///     owns the only copy of a state.
    ///  3. A setter announces a real change with the state's SIGNAL and nothing else: set_checked()
    ///     emits toggled(), set_value() emits value_changed(), when — and only when — the value
    ///     actually changes. A setter never raises the User property. The two channels answer
    ///     different questions: a signal is a state notification from ANY origin, while the User
    ///     property is the frame's interaction result, produced only by the control's own action (a
    ///     click, a drag or a key step). The control's own action does both: it emits the signal and
    ///     raises the User event for that frame.
    ///  4. A control is therefore drivable without frame-to-frame identity: it derives nothing from
    ///     "the same object as last frame" and never stores this frame's event as persistent state. A
    ///     host may delete a control and create a fresh one under the same name at any pass boundary,
    ///     and the same driving code must keep working.
    ///
    /// @par Keyboard
    /// A focusable control handles keys in its OWN interact(): it consumes only the keys it recognises
    /// (Space and Return activate a push button, Space toggles a check box) and returns false for
    /// every other key and for every key step without the focus. Navigation stays outside the control:
    /// Tab, the focus chain and the focus policy belong to the host or the router, which are the focus
    /// authority. The rule is host-neutral — the retained router delivers a key step to the focused
    /// item, ui::im_host_t delivers it to its focused control, and both call the same interact(), so
    /// one control implementation serves both hosts.
    ///
    /// @par Painting
    /// paint() is save() -> translate(geometry's top-left) -> set_clip(rect()) -> dispatch a
    /// paint_event_t (which runs paint_event()) -> each item child's paint() -> restore(). Every level
    /// clips to its own rectangle, so a child that sticks out of its parent is clipped away. A child
    /// can be clipped further by the container: items_rect() is the rectangle a child is both PAINTED
    /// and HIT in, so the two sides of the same question can never answer differently. An invisible
    /// widget is skipped together with its whole subtree.
    ///
    /// @par Partial repaint and occlusion
    /// A frame paints the damaged rectangles of the window, so paint() first asks the painter for its
    /// clip and returns without painting when this widget's rectangle does not intersect it: an item
    /// the frame never reaches keeps its damage for the frame that does. Among the children, one that
    /// is completely covered by an OPAQUE sibling above it (item_t::is_opaque()) is not painted at all,
    /// and a partly covered one is painted clipped to its visible part — the same pixels a full frame
    /// would leave there, without the work.
    ///
    /// @par Parents
    /// The constructor's parent must be a widget (that is the visual parent). object_t::set_parent()
    /// stays available unchanged: attaching a widget under a NON-item object is safe but makes it a
    /// visual root — it is not arranged, painted or hit-tested by any widget traversal, parent_widget()
    /// answers nullptr and map_to_global() stops there, because a non-item object has no coordinate
    /// system to contribute. Use the widget constructor (or set the parent on a widget) for normal
    /// trees.
    ///
    /// @par Layout
    /// A widget may own one layout_t (set_layout() takes ownership). The layout arranges the widget's
    /// ITEM children; it is re-applied whenever the widget's rectangle really changes (so a resize
    /// lays the children out again) and whenever an item child is added to or removed from this
    /// widget. relayout() runs it on demand; a widget without a layout ignores all of this and keeps
    /// manual child geometry.
    /// The layout also answers the CONTAINER's natural size (layout_t::size_hint()) and its floor
    /// (layout_t::minimum_size()), so a widget with a layout asks for what its children need; without
    /// one it has no size opinion of its own.
    /// The size hint / minimum hint accessors below (together with item_t's policy and stretch) are
    /// what the layouts consult.
    ///
    /// @par Deliberately absent (W1)
    /// geometry is NOT a reflected property yet: rect_t has no canonical name in the gui layer's
    /// reflection surface and the type registry work is deferred to the designer stage. The only
    /// annotated properties of the item hierarchy are visible / enabled, declared on item_t and found
    /// on a widget through the meta-object chain.
    class HWSHQTB_UI_EXPORT widget_t : public item_t {
        OBJECT

    public:
        /// @brief Construct a widget, optionally parented to another widget.
        /// @param parent The visual parent; nullptr makes this a root widget. The parent owns this
        ///        widget (the object tree deletes it).
        explicit widget_t(widget_t* parent = nullptr);

        /// @brief Detach from the parent (done by ~object_t) and destroy the whole widget subtree.
        ~widget_t() override;

        widget_t(const widget_t&) = delete;
        widget_t& operator=(const widget_t&) = delete;
        widget_t(widget_t&&) = delete;
        widget_t& operator=(widget_t&&) = delete;

        /// @brief The top-left corner in parent-relative coordinates (geometry().top_left()).
        ::hwshqtb::gui::point_t pos() const;

        /// @brief The widget extent (geometry()'s width/height).
        ::hwshqtb::gui::extent_t size() const;

        /// @brief The natural size this widget asks for (Qt's sizeHint) — its OPINION.
        /// @return The installed layout's size_hint() when there is one, otherwise {0, 0}: a widget
        ///         with neither content nor a layout has no opinion and a layout will collapse it.
        /// @note A layout does NOT ask this directly: it asks item_t::effective_size_hint(), which
        ///       applies the fixed constraint and clamps the opinion to minimum_size_hint(). A
        ///       content-derived control overrides THIS and never needs to know about constraints.
        /// @note Deliberately not size(): a hint that follows the current geometry would be circular
        ///       (the layout uses the hint to set the geometry that would then be the hint).
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief The smallest size a layout may shrink this widget to (Qt's minimumSizeHint).
        /// @return The installed layout's minimum_size() when there is one, otherwise {0, 0}.
        /// @note Virtual: a control with a content-derived minimum overrides this. A shrinking layout
        ///       stops at it, and only the overfull rule (which cuts the biggest item first) can go
        ///       below it when even the minimums do not fit the available space.
        /// @note A container answers the floor its children impose through this; it is what makes a
        ///       child's minimum_size() reach the ancestors' floors.
        ::hwshqtb::gui::extent_t minimum_size_hint() const override;

        /// @brief The installed layout, or nullptr.
        layout_t* layout() const;

        /// @brief Install (or clear) the layout of this widget's item children.
        /// @param layout The new layout; this widget takes ownership and deletes the previously
        ///        installed one. nullptr removes the layout and leaves child geometry alone.
        /// @note A newly installed layout is applied immediately. Passing the same pointer again is a
        ///       no-op (the ownership transfer would otherwise delete the object being installed).
        void set_layout(layout_t* layout);

        /// @brief Apply the installed layout to this widget's client rect (no-op without a layout).
        /// @note Called automatically by geometry_change_event() (a real rectangle change) and by
        ///       child_added()/child_removed(). Each placed child receives its rectangle through
        ///       set_geometry(), so it is repainted.
        /// @note Virtual: a container whose content area is INSET inside its rectangle (group_box_t
        ///       reserves a title strip) overrides it to apply the layout to the content area; every
        ///       automatic relayout call above dispatches to the override.
        virtual void relayout();

        /// @brief The visual parent widget, or nullptr for a root widget.
        /// @note Answers nullptr when the object-level parent is not a widget (see the class note on
        ///       parents).
        widget_t* parent_widget() const;

        /// @brief The rectangle of this widget's coordinates in which @p child is visible and
        ///        hit-testable (the container-level clip of that child's subtree).
        ///
        /// This is the ONE rule shared by the paint side and the input side: paint_item_children()
        /// clips a child to it and hit() refuses a point outside it, so the pixels a child may draw
        /// and the points it may receive cannot diverge.
        ///
        /// @param child An item child of this widget.
        /// @return rect() by default: a child is shown and hit everywhere inside the widget, and the
        ///         partly-outside case is handled by the painter's clip and the rect() test in hit().
        /// @note A container that shows one OVERSIZED child through a window into it overrides this
        ///       and answers that window (scroll_view_t does for its content); the child then owns
        ///       only the points inside the window, and what it draws outside is clipped away.
        /// @note Per child, not per container: a container may clip one child while its siblings —
        ///       the scroll bars of a scroll area, which lie BESIDE the viewport — stay unclipped.
        virtual ::hwshqtb::gui::rect_t items_rect(const item_t& child) const;

        /// @brief Request a repaint of the whole widget (QWidget::update()).
        ///
        /// Records the damage through item_t::update() (this widget and every item ancestor) and
        /// then asks the host for a frame through the request channel; a tree without a host only
        /// records the damage. widget_window_t receives the request as a PAINT event and hands it to
        /// window_t::update(), so a repaint from any nested widget is COALESCED with every other one
        /// into the next frame.
        void update();

        /// @brief Paint the whole widget NOW (QWidget::repaint()).
        ///
        /// Same damage record as update(), but the host is asked for an IMMEDIATE frame — the request
        /// travels as a UI_REPAINT event and the host answers with window_t::repaint(), which clears the
        /// damage and calls paint_event() before returning. Use it when the caller needs the pixels to be
        /// current at the end of the call (a synchronous capture, a test that drives frames itself);
        /// ordinary redraws go through update(), which coalesces.
        /// @note Repainting from INSIDE a paint pass is not allowed, exactly like Qt: the frame is
        ///       already running and would be re-entered.
        void repaint();

        /// @brief Request a repaint of @p local (QWidget::update(rect)).
        /// @param local The damaged rectangle in THIS widget's coordinates; it is mapped to window
        ///        coordinates and recorded on the widget and every item ancestor.
        /// @note The damage record alone does not schedule a frame: like update(), this form also
        ///       asks the host for a frame through the request channel, so the host paints it.
        void update(::hwshqtb::gui::rect_t local);

        /// @brief The damage recorded on this widget's subtree, in window coordinates.
        /// @return The region; empty when nothing was recorded or the paint path cleared it. The view is
        ///         invalidated by the next update() / clear_damage() of this widget.
        /// @note Damage belongs to the WIDGET layer: an item_t that is not a widget has no pixels and
        ///       therefore no repaint record.
        const ::hwshqtb::gui::region_t& damage() const noexcept;

        /// @brief Drop the recorded damage (called by the paint path once the widget has been painted).
        /// @note A widget whose paint is not reached by a frame (a hidden subtree) keeps its damage:
        ///       "not painted" must stay visible to the next partial repaint.
        /// @note noexcept.
        void clear_damage() noexcept;

        /// @brief Move the part of this widget's damage record inside @p local by (@p dx, @p dy).
        /// @param local The shifted rectangle in THIS widget's coordinates (the form update() takes).
        /// @param dx The horizontal shift: a recorded pixel at x() ends up at x() + @p dx.
        /// @param dy The vertical shift: a recorded pixel at y() ends up at y() + @p dy.
        /// @note Damage describes pixels, so when those pixels move, their damage has to move with
        ///       them; what leaves the rectangle is dropped. The scroll fast path calls this on the ROOT
        ///       with the same rectangle it hands the host, so the tree's record and the host's pending
        ///       damage keep describing the same region.
        void translate_damage(::hwshqtb::gui::rect_t local, int dx, int dy);

        /// @brief Unite @p local (this widget's coordinates) into this widget and every WIDGET ancestor.
        /// @param local The damaged rectangle; empty is ignored.
        /// @note A non-widget item ancestor has no damage record, so the walk only unites where a record
        ///       exists — that is what keeps the record on the layer that can render.
        void unite_damage(::hwshqtb::gui::rect_t local);

        /// @brief Whether this object is a visual widget; always true for widget_t.
        /// @note The core-layer contract: is_widget() == true implies a widget_t, so the widget layer
        ///       may static_cast after this check.
        /// @note Hidden from the meta-info, like the base declaration: not scriptable API.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        bool is_widget() const noexcept override {
            return true;
        }

        /// @brief Re-lay out @p child after it was attached to this widget.
        /// @param child The newly attached child (never nullptr in the notification).
        /// @note The widget layer's only use: when @p child is an item, the layout runs again.
        /// @note Hidden from the meta-info: an internal tree hook, not scriptable API. A reflected
        ///       virtual would add an entry to widget_t's table (and its subclasses' counts).
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void child_added(::hwshqtb::metaobject::object_t* child) override;

        /// @brief Re-lay out the remaining children after @p child was detached.
        /// @param child The detached child.
        /// @note The notification runs from the child's destructor too, where the dynamic type is
        ///       already object_t (so is_widget() would answer false for a dying widget); the
        ///       override therefore re-lays out unconditionally. A plain object_t child triggers one
        ///       harmless extra pass.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void child_removed(::hwshqtb::metaobject::object_t* child) override;

        /// @brief Paint this widget's subtree into @p painter (QWidget::render()).
        ///
        /// This is the composition entry: a caller that owns no window (a one-off capture, a host painting
        /// into an off-screen device) hands over a painter and the tree paints itself into it. The damage
        /// is the painter's current clip, which is the area the caller wants covered.
        ///
        /// @param painter The painter of this pass. Its clip is the area to paint; the tree narrows it to
        ///        each widget as it descends.
        /// @note The traversal lives HERE and not in paint_event(), exactly like Qt's
        ///       QWidgetPrivate::drawWidget(): paint_event() is the virtual that draws ONE widget's own
        ///       pixels, and this function is the framework code that localises the painter, runs that
        ///       virtual and then descends into the item children. A subclass that only draws its own
        ///       pixels therefore never breaks the traversal.
        void render(::hwshqtb::gui::painter_t& painter);

        /// @brief Paint @p damage of this widget's subtree into @p painter (QWidget::render() with a
        ///        region).
        /// @param painter The painter of this pass; its clip is narrowed by @p damage.
        /// @param damage The area to paint, in the painter's CURRENT (parent) coordinates.
        void render(::hwshqtb::gui::painter_t& painter, const ::hwshqtb::gui::region_t& damage) override;

        /// @brief The deepest visible, enabled item child at @p local.
        ///
        /// Children are searched in REVERSE order, so the one added last (painted last, i.e. on top)
        /// wins where two overlap. Invisible and disabled items are skipped together with their
        /// subtrees. A point inside this widget but outside every child yields nullptr, i.e. the hit
        /// is this widget itself.
        ///
        /// A child only owns the points inside items_rect(child) ∩ child->geometry(): a child that a
        /// container clips is NOT hit where it is clipped away, which is what keeps a scrolled content
        /// from stealing the presses meant for the scroll bars beside the viewport.
        ///
        /// @param local The point in THIS widget's coordinates.
        /// @return The hit item, or nullptr.
        item_t* hit(::hwshqtb::gui::point_t local) const override;

    protected:
        /// @brief Draw THIS widget's own pixels for one pass (QWidget::paintEvent).
        /// @param event The pass. event->painter() is already translated to this widget's origin and
        ///        clipped to its rect(), and event->damage() is in this widget's own coordinates — the
        ///        framework (render()) did that before calling here.
        /// @note Own pixels ONLY: the item children are painted by the framework after this returns, so an
        ///       override never has to call the base to keep the subtree visible.
        /// @note A container that paints decoration (a group box's frame, a viewport's border) overrides
        ///       this and draws; it does not traverse.
        /// @note Hidden from the meta-info: an internal tree hook, not scriptable API.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        virtual void paint_event(paint_event_t* event);
        /// @brief React to this widget having gained or lost the focus: request a repaint.
        /// @param event The focus event; event->has_focus() is the new state.
        /// @note Requests a repaint, which is what a focus ring needs. This belongs to the WIDGET
        ///       layer (it owns the repaint sink path); item_t's default handler is deliberately empty.
        ///       An override that wants the repaint too must call widget_t::focus_change_event().
        /// @note Hidden from the meta-info: an internal tree hook, not scriptable API.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void focus_change_event(focus_change_event_t* event) override;

        /// @brief React to the widget rectangle having really changed: lay the children out again and
        ///        ask for the frame.
        /// @param event The geometry event; the new rectangle is already stored on the item.
        /// @note item_t::set_geometry() stored the new rectangle already; this layer adds the two
        ///       reactions a widget owns: re-applying its own layout (the children then ask for their
        ///       own repaints through their set_geometry()) and reaching the host's frame sink through
        ///       widget_t::update() (item_t::update() records damage but schedules nothing).
        /// @note Hidden from the meta-info for the same reason as focus_change_event().
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void geometry_change_event(geometry_change_event_t* event) override;

        /// @brief React to a size constraint of this widget having really changed.
        /// @note Re-applies the PARENT widget's layout — the parent owns this widget's place, so its
        ///       layout is the one that has to honour a new bound — and then requests a repaint
        ///       through widget_t::update() (which also reaches the host's frame sink).
        /// @note Hidden from the meta-info for the same reason as focus_change_event().
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void size_constraint_change_event(size_constraint_change_event_t* event) override;

        /// @brief React to this widget's visibility having changed: ask the host for a frame.
        /// @param event The visibility event; the flag and the signal are item_t's.
        /// @note The widget layer owns the repaint SINK, so it adds that request here; the state, the
        ///       item-level damage and the visible_changed() signal all happen in item_t's setter.
        /// @note Hidden from the meta-info for the same reason as focus_change_event().
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void visible_change_event(visible_change_event_t* event) override;

        /// @brief React to this widget's enabled state having changed: ask the host for a frame.
        /// @param event The enabled event; the flag and the signal are item_t's.
        /// @note The same split as visible_change_event().
        /// @note Hidden from the meta-info for the same reason as focus_change_event().
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void enabled_change_event(enabled_change_event_t* event) override;

        /// @brief Paint the item children of this widget (bottom to top), culling what the opaque ones
        ///        above already cover.
        ///
        /// Children are painted in child order — the last one added is painted last, i.e. on top — and a
        /// child is only painted over the part of its rectangle that no opaque sibling ABOVE it covers
        /// AND that lies inside items_rect(child) (the container's clip, the same rectangle hit() uses).
        /// A child left with no damage is skipped, so its paint_event() is not called at all (Qt's
        /// WA_OpaquePaintEvent culling). The painter must already be translated to this widget and
        /// clipped to its rect(), which paint() guarantees.
        ///
        /// @param painter The painter of the current frame.
        /// @param damage The part of this widget's rectangle the frame paints, in this widget's
        ///        coordinates. The occlusion rule subtracts an opaque child's area from it, and every
        ///        child receives its own remainder as paint_event_t::damage().
        /// @param skip An item child to leave out (a container that painted it separately, such as the
        ///        content of a scroll area); nullptr paints every item child.
        /// @note Painting is one pass per visible damage rectangle, because the ui layer owns no pass
        ///       policy: it asks the painter for one clip per rectangle and paints there.
        /// @note A container that overrides paint_event() draws its own decoration and then calls
        ///       widget_t::paint_event(), which runs this traversal — so the occlusion rule has exactly
        ///       one implementation and the recursion stays the container's own call.
        void paint_item_children(::hwshqtb::gui::painter_t& painter,
                                 const ::hwshqtb::gui::region_t& damage,
                                 const item_t* skip = nullptr);

        /// @brief The item child this widget paints ITSELF during paint_event(), so the framework skips it.
        /// @return nullptr (the default) to let the traversal paint every item child.
        /// @note A container that paints one child through its own translation (a viewport's content, which
        ///       is mapped through source_rect() -> destination_rect()) answers that child here; painting it
        ///       again as an ordinary child would draw it twice, the second time without the translation.
        virtual const item_t* paint_skip() const noexcept;

        /// @brief Ask the nearest widget on the parent chain that installed a scroll sink to shift
        ///        already-painted pixels.
        /// @param local The scrolled rectangle in THIS widget's coordinates.
        /// @param dx The horizontal shift of the painted content, in pixels.
        /// @param dy The vertical shift of the painted content, in pixels.
        /// @note Without a sink the container damages the whole rectangle instead, which is the correct
        ///       but slow path.
        /// @note Protected, not public: a subclass container uses it to request a pixel shift instead
        ///       of repainting the whole area (受保护：子类容器用它请求像素位移，而不是整块重绘).
        void notify_scroll(::hwshqtb::gui::rect_t local, int dx, int dy);

    private:
        /// @brief This widget's state changed: record the damage and ask the host for a frame.
        /// @note The item layer calls this from every setter that changes something visible; an item that
        ///       is not a widget leaves it as the empty default, because it has no pixels to repaint.
        /// @note This is the override of item_t::notify_repaint(), and it is what makes "a setter asks for
        ///       a repaint" reach the host in a widget tree.
        void notify_repaint() override;

        /// @brief Ask the host for a frame on the request channel, without recording any damage.
        void request_frame();

        /// @brief Move the widget to @p rect without recording damage.
        /// @param rect The new geometry in the parent's coordinates.
        /// @note For a container that has already accounted for the pixels: a scrolled child moves over
        ///       the shifted pixels, so damaging its whole rectangle again would repaint the entire
        ///       viewport on every scroll step and defeat the scroll fast path.
        /// @note Private and reachable only through member functions of this class or the scroll
        ///       container: a general caller must go through set_geometry().
        void set_geometry_silently(::hwshqtb::gui::rect_t rect);

        /// @brief The owned layout; nullptr means "no layout installed".
        layout_t* _layout = nullptr;
        /// @brief The damage recorded on this widget's subtree, in window coordinates.
        /// @note Lives HERE and not on item_t: an item that is not a widget has no pixels, so it can
        ///       neither record nor clear damage.
        ::hwshqtb::gui::region_t _damage;

        /// @brief The scroll container of the tree may move a child's geometry without damage.
        friend class scroll_view_t;
        /// @brief The immediate host paints its internal controls through the ONE paint entry.
        /// @note paint_event() is protected (a subclass overrides it), so a host that is not a widget
        ///       subclass must be allowed to drive a control's pass.
        friend class im_host_t;
    };
}

#endif
