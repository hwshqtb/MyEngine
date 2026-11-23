#ifndef HWSHQTB__METAOBJECT__EVENT_H
#define HWSHQTB__METAOBJECT__EVENT_H

/// @file event_t.h
/// @brief The event base class event_t and the basic concrete events (the QEvent family).

#include "any_t.h"
#include "meta_method_t.h"
#include "export.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace hwshqtb::metaobject {
    class object_t;

    namespace detail {
        /// @brief Completion synchronisation for BlockingQueued (a QSemaphore equivalent, count 0/1).
        ///
        /// The poster calls wait(); the dispatching side settles it exactly once: notify_done() when
        /// the call was placed (the normal delivery), or notify_cancelled() when the event was
        /// destroyed before it could be placed (the receiver was torn down, the queue was purged,
        /// ...). The two outcomes are kept apart so the CALLER can report a cancelled BlockingQueued
        /// invoke as a failure.
        ///
        /// Both notifications are idempotent: the FIRST one wins and every later call (the event
        /// destructor running after a normal delivery is the common one) is a no-op.
        struct event_sync_t {
            /// @brief The one-shot completion state.
            /// @note At most one transition out of Pending happens; the state is guarded by @c mutex
            ///       and read with done() / cancelled().
            enum struct state_enum : std::uint8_t {
                /// @brief No outcome recorded yet: a waiter blocks.
                PENDING,
                /// @brief The call was placed: delivered normally.
                DONE,
                /// @brief The event died unexecuted: the call was dropped.
                CANCELLED,
            };

            /// @brief Guards @c state (mutable: done()/cancelled() are const).
            mutable std::mutex mutex;
            /// @brief Wakes the poster when the state leaves Pending.
            std::condition_variable cv;
            /// @brief The current one-shot state; see state_enum.
            state_enum state = state_enum::PENDING;

            /// @brief Mark the call as delivered and wake the waiter up (idempotent).
            void notify_done();

            /// @brief Mark the call as CANCELLED (the event died unexecuted) and wake the waiter
            ///        up. Idempotent, and ineffective once notify_done() has won.
            void notify_cancelled();

            /// @brief Whether the call was delivered normally.
            /// @return true once notify_done() won the one-shot transition.
            bool done() const;

            /// @brief Whether the event was destroyed before the call was placed.
            /// @return true once notify_cancelled() won the one-shot transition.
            bool cancelled() const;

            /// @brief Block until either outcome was recorded.
            /// @note Returns also for a cancelled call: the event destructor releases the waiter, so a
            ///       BlockingQueued poster cannot hang on a receiver that died first.
            /// @warning Calling this on the event-loop thread deadlocks (the same as Qt's
            ///          BlockingQueuedConnection).
            void wait();
        };
    }

    /// @brief Event base class (QEvent equivalent).
    ///
    /// It carries three things: the type identification, the accepted / ignored state, and the
    /// ownership marker "was this posted by post_event". Concrete events support by-value copying
    /// through clone() (for paths that need a copy of the event).
    /// @note The copy constructor is protected (the same idea as Qt's Q_EVENT_DISABLE_COPY): users
    ///       cannot copy an event by value, while derived classes and clone() can.
    /// @sa event_dispatcher
    class HWSHQTB_EXPORT event_t {
    public:
        /// @brief Construct with the given type.
        /// @param type The event type.
        /// @note noexcept.
        explicit event_t(event_type_enum type) noexcept;

        /// @brief Destructor.
        virtual ~event_t();

        event_t& operator=(const event_t&) = delete;

        /// @brief Get the event type.
        /// @return The type given at construction.
        /// @note noexcept.
        event_type_enum type() const noexcept;

        /// @brief Whether an event type counts as "user input" (used by EXCLUDE_USER_INPUT_EVENTS).
        /// @param type The event type.
        /// @return true for input events.
        /// @note static constexpr, noexcept. It depends on the type alone, never on an instance.
        static constexpr bool is_input(event_type_enum type) noexcept {
            switch (type) {
                case event_type_enum::MOUSE_BUTTON_PRESS:
                case event_type_enum::MOUSE_BUTTON_RELEASE:
                case event_type_enum::MOUSE_BUTTON_DBL_CLICK:
                case event_type_enum::MOUSE_MOVE:
                case event_type_enum::KEY_PRESS:
                case event_type_enum::KEY_RELEASE:
                case event_type_enum::WHEEL:
                case event_type_enum::TOUCH_BEGIN:
                case event_type_enum::TOUCH_UPDATE:
                case event_type_enum::TOUCH_END:
                case event_type_enum::TOUCH_CANCEL:
                    return true;
                default:
                    return false;
            }
        }

        /// @brief Whether the event was produced spontaneously by the system.
        /// @return true when dispatched directly by send_event; false when posted by post_event.
        /// @note noexcept. Mirrors QEvent::spontaneous.
        bool spontaneous() const noexcept;

        /// @brief Whether the event has been accepted.
        /// @return true by default.
        /// @note noexcept.
        bool is_accepted() const noexcept;

        /// @brief Set the accepted state.
        /// @param accepted The new accepted state.
        /// @note Virtual, so concrete events can override it to keep internal state in sync.
        virtual void set_accepted(bool accepted) noexcept;

        /// @brief Accept the event (equivalent to set_accepted(true)).
        /// @note noexcept.
        void accept() noexcept;

        /// @brief Ignore the event (equivalent to set_accepted(false)).
        /// @note noexcept.
        void ignore() noexcept;

        /// @brief Register a user event type (QEvent::registerEventType).
        /// @param hint The desired id; -1 or an invalid value means allocate automatically.
        /// @return The registered type; event_type_enum::NONE when MaxUser is exceeded.
        /// @note noexcept; thread-safe (atomic counter).
        /// @note Simplification: duplicate hints are not detected (Qt does detect them).
        static event_type_enum register_event_type(int hint = -1) noexcept;

        /// @brief Clone this event.
        /// @return A newly allocated copy of the event; the caller must delete it.
        /// @note The base class returns @c new @c event_t(*this); concrete events override it with a
        ///       covariant return type.
        virtual event_t* clone() const;

        /// @brief Fold a later event of the same type for the same receiver into this one.
        /// @param newer The event that arrived later; the dispatcher owns it and deletes it when true.
        /// @return true when merged (the dispatcher must NOT deliver @p newer); the DEFAULT keeps this
        ///         event and drops the later one.
        /// @note Only ever called for events still PENDING in the queue; an event already dispatched is
        ///       never rewritten. The merged event keeps the EARLIER position in the queue, so the order
        ///       between different event types is preserved.
        /// @note There is no generic payload transfer: a class that wants the later payload to win
        ///       overrides this and moves that payload into @c *this itself.
        /// @warning The dispatcher calls this while holding its queue lock. An override must only
        ///          touch the two event objects: posting, removing or dispatching an event from here
        ///          would deadlock (or recurse into the queue that is being rewritten).
        virtual bool merge_with(const event_t& newer);

    protected:
        /// @brief Copy constructor used by derived classes and clone().
        event_t(const event_t& other) = default;

    private:
        friend class event_dispatcher;
        friend class core_application;

        /// @brief Mark the event as spontaneous (only the send_event path calls this).
        void set_spontaneous() noexcept;

        /// @brief The event type.
        event_type_enum _type;

        /// @brief Whether the event was produced spontaneously by the system (set by send_event).
        bool _spontaneous = false;

        /// @brief The accepted state.
        bool _accepted = true;

        /// @brief Whether the event was already posted by post_event (the poster owns it).
        bool _posted = false;
    };

    /// @brief Timer event (QTimerEvent equivalent).
    class HWSHQTB_EXPORT timer_event_t: public event_t {
    public:
        /// @brief Construct with a timer id.
        /// @param timer_id The timer id.
        /// @note noexcept.
        explicit timer_event_t(int timer_id) noexcept;

        /// @brief Destructor.
        ~timer_event_t() override;

        /// @brief Clone.
        /// @return A newly allocated copy of the event.
        timer_event_t* clone() const override;

        /// @brief Refuse merging: a timer tick is a one-shot callback.
        /// @param newer Ignored.
        /// @return false. Folding this event into an earlier pending one would DELETE this tick,
        ///         i.e. the receiver would silently lose one timer callback.
        bool merge_with(const event_t& newer) override;

        /// @brief Get the timer id.
        /// @return The id given at construction.
        /// @note noexcept.
        int timer_id() const noexcept;

    protected:
        timer_event_t(const timer_event_t&) = default;

    private:
        int _timer_id = -1;
    };

    /// @brief Child-object event (QChildEvent equivalent).
    class HWSHQTB_EXPORT child_event_t: public event_t {
    public:
        /// @brief Construct with an event type and a child object.
        /// @param type The event type (ChildAdded / ChildPolished / ChildRemoved).
        /// @param child The child object; may be nullptr.
        /// @note noexcept.
        child_event_t(event_type_enum type, object_t* child) noexcept;

        /// @brief Destructor.
        ~child_event_t() override;

        /// @brief Clone.
        /// @return A newly allocated copy of the event.
        child_event_t* clone() const override;

        /// @brief Refuse merging: a child notification is a FACT, not a state.
        /// @param newer Ignored.
        /// @return false. Two child events of the same type name DIFFERENT children, so folding the
        ///         later into the earlier one does not update a state — it deletes the observation
        ///         that the second child appeared (or vanished), and that child's pointer with it.
        bool merge_with(const event_t& newer) override;

        /// @brief Get the child object.
        /// @return The child given at construction.
        /// @note noexcept.
        object_t* child() const noexcept;

        /// @brief Whether this is a child-added event.
        /// @note noexcept.
        bool added() const noexcept;

        /// @brief Whether this is a child-polished event.
        /// @note noexcept.
        bool polished() const noexcept;

        /// @brief Whether this is a child-removed event.
        /// @note noexcept.
        bool removed() const noexcept;

    protected:
        child_event_t(const child_event_t&) = default;

    private:
        object_t* _child = nullptr;
    };

    /// @brief A dynamic property changed (QDynamicPropertyChangeEvent equivalent).
    class dynamic_property_change_event_t: public event_t {
    public:
        /// @brief Construct for a property name.
        /// @param name The name of the property that changed.
        explicit dynamic_property_change_event_t(std::string name)
            : event_t(event_type_enum::DYNAMIC_PROPERTY_CHANGE), _name(std::move(name)) { }

        /// @brief Refuse merging: a property notification is a FACT, not a state.
        /// @param newer Ignored.
        /// @return false. Two notifications of the same type may name DIFFERENT properties, so
        ///         folding the later into the earlier one loses one change instead of updating a
        ///         value — the observer would never hear about the second property at all.
        bool merge_with(const event_t& newer) override {
            (void)newer;
            return false;
        }

        /// @brief The name of the property that changed.
        /// @return The stored name.
        /// @note noexcept.
        const std::string& property_name() const noexcept {
            return _name;
        }

        /// @brief Clone.
        /// @return A newly allocated copy of the event.
        dynamic_property_change_event_t* clone() const override {
            return new dynamic_property_change_event_t(*this);
        }

    private:
        std::string _name;
    };

    /// @brief Deferred-delete event (QDeferredDeleteEvent equivalent).
    ///
    /// Besides the loop level it also records the thread that posted it: the level is a per-thread
    /// nesting depth, so the dispatcher applies the level rule only when the event is dispatched on
    /// that same thread. Another thread's depth is not comparable with it.
    class HWSHQTB_EXPORT deferred_delete_event_t: public event_t {
    public:
        /// @brief Construct with a loop level and a scope level.
        /// @param loop_level The event-loop nesting level.
        /// @param scope_level The scope nesting level.
        /// @note noexcept.
        /// @note The constructing thread is recorded as the posting thread (delete_later() constructs
        ///       the event on the calling thread, immediately before posting it).
        explicit deferred_delete_event_t(int loop_level = 0, int scope_level = 0) noexcept;

        /// @brief Destructor.
        ~deferred_delete_event_t() override;

        /// @brief Clone.
        /// @return A newly allocated copy of the event.
        deferred_delete_event_t* clone() const override;

        /// @brief Refuse merging: dispatch of this event deletes the receiver.
        /// @param newer Ignored.
        /// @return false. Folding this event into an earlier pending one would DELETE this deletion
        ///         request, so the object would stay alive (a leak, and a missing delete_later()).
        bool merge_with(const event_t& newer) override;

        /// @brief Get the event-loop nesting level.
        /// @note noexcept.
        int loop_level() const noexcept;

        /// @brief Get the scope nesting level.
        /// @note noexcept.
        int scope_level() const noexcept;

    protected:
        deferred_delete_event_t(const deferred_delete_event_t&) = default;

    private:
        friend class event_dispatcher;

        /// @brief The thread that posted the event (captured at construction, i.e. on the poster).
        std::thread::id _posting_thread = std::this_thread::get_id();

        int _loop_level = 0;
        int _scope_level = 0;
    };

    /// @brief Queued invocation event (QMetaCallEvent equivalent).
    ///
    /// It carries everything needed to defer one signal activation into the event loop: the receiver,
    /// the target slot, and the arguments **copied according to the parameter types** (@c any_t is
    /// the type-erased carrier, travels with the event and is safe across modules).
    /// @par Forms
    ///  - @b queued: the caller copies the arguments into this event and the event owns them;
    ///  - @b blocking: additionally carries an @c event_sync_t on which the poster waits until the slot
    ///    has run;
    ///  - @b functor: carries a copy of a functor connection's callable instead of a slot. It needs
    ///    NO receiver object: it is posted to the CONNECTION's thread (the one that created the
    ///    connection) and runs there (queued form);
    ///  - @b invoke: the queued/blocking form of a NON-SIGNAL call (meta_object_t::invoke /
    ///    meta_method_t::invoke with a Queued or BlockingQueued connection type). It carries the slot
    ///    handle plus, when a result is expected, the caller's return-value storage as @c ret_slot.
    /// @warning Posting the blocking form on the event-loop thread always deadlocks (same as Qt).
    /// @sa object_t::activate_signal
    class HWSHQTB_EXPORT meta_call_event_t: public event_t {
    public:
        /// @brief Construct the queued form.
        /// @param sender The sender; may be nullptr (for bookkeeping).
        /// @param signal The signal handle on the sender side.
        /// @param slot The slot handle on the receiver side.
        /// @param args The already-copied arguments.
        meta_call_event_t(const object_t* sender, const meta_method_t& signal, meta_method_t slot, std::vector<any_t> args);

        /// @brief Construct the blocking form.
        /// @param sender The sender; may be nullptr (for bookkeeping).
        /// @param signal The signal handle on the sender side.
        /// @param slot The slot handle on the receiver side.
        /// @param args The already-copied arguments.
        /// @param sync The completion synchronisation; notify_done() is called once dispatching ends,
        ///        and the destructor calls notify_cancelled() when the event dies unexecuted.
        meta_call_event_t(const object_t* sender, const meta_method_t& signal, meta_method_t slot, std::vector<any_t> args, std::shared_ptr<detail::event_sync_t> sync);

        /// @brief Construct the queued form of a functor (lambda) connection.
        /// @param sender The sender (kept for object_t::sender() inside the callable).
        /// @param signal The signal handle on the sender side.
        /// @param functor The callable; it gets the generic_call params array (params[0] = the
        ///        return-value slot, arguments from params[1] on).
        /// @param args The already-copied arguments.
        /// @note This form is dispatched by the thread's event loop, not by an object: it is posted
        ///       with detail::post_event_to() and placed by meta_call_event_t::place_functor_call().
        meta_call_event_t(const object_t* sender, const meta_method_t& signal,
                           std::function<void(std::span<any_t>)> functor, std::vector<any_t> args);

        /// @brief Construct the blocking form of the functor (lambda) connection.
        /// @param sender The sender (kept for object_t::sender() inside the callable).
        /// @param signal The signal handle on the sender side.
        /// @param functor The callable.
        /// @param args The already-copied arguments.
        /// @param sync The completion synchronisation; notify_done() is called once it has run, and
        ///        the destructor calls notify_cancelled() when the event dies unexecuted.
        meta_call_event_t(const object_t* sender, const meta_method_t& signal,
                           std::function<void(std::span<any_t>)> functor, std::vector<any_t> args,
                           std::shared_ptr<detail::event_sync_t> sync);

        /// @brief Construct the queued form of a NON-SIGNAL call (meta_object_t::invoke /
        ///        meta_method_t::invoke with a Queued connection type).
        /// @param slot The entry to run on the receiver's thread.
        /// @param args The already-copied arguments.
        /// @param ret_slot The caller's return-value storage, or nullptr when the call has none; it is
        ///        forwarded as params[0] when the call is placed, so the thunk writes the result
        ///        straight into the caller's storage.
        /// @param ret_any The any_t the caller's non-Ret invoke() handed back, or nullptr (`Ret&`
        ///        form, void row). It is the variable whose storage IS @p ret_slot; once the call has
        ///        been placed, the event marks it constructed, because only then does it actually hold
        ///        a value (the poster cannot do it: it returns before the target thread runs the entry).
        /// @note There is no sender and no signal: an invocation is not an emission, so
        ///       object_t::sender() answers nullptr inside the call and signal() answers an invalid
        ///       handle (Qt's QMetaCallEvent for QMetaObject::invokeMethod carries no sender either).
        /// @note The event still carries the slot HANDLE, so it replays exactly the entry the caller
        ///       resolved — a synthesised property accessor included.
        /// @note The marking is skipped when @p ret_any is null. An event destroyed unexecuted never
        ///       marks, so the variable stays DECLARED ONLY — the honest "the call never ran" answer.
        /// @warning The event stores ADDRESSES, never copies: @p ret_slot and @p ret_any must outlive
        ///          the event. For a `Ret&` invoke that is the caller's own variable (it must not be
        ///          reused or destroyed before the event runs); for the non-Ret form it is the any_t
        ///          the call handed back, which must not be moved, copied or destroyed before then (a
        ///          declared-only move takes the storage along and leaves the event's pointers on the
        ///          moved-from variable).
        meta_call_event_t(meta_method_t slot, std::vector<any_t> args, void* ret_slot, any_t* ret_any);

        /// @brief Construct the blocking form of the same non-signal call.
        /// @param slot The entry to run on the receiver's thread.
        /// @param args The already-copied arguments.
        /// @param ret_slot The caller's return-value storage; it must outlive the wait, which it does
        ///        by construction (it is the caller's own variable).
        /// @param sync The completion synchronisation; notify_done() is called once dispatching ends,
        ///        and the destructor calls notify_cancelled() when the event dies unexecuted.
        /// @note This is how a BlockingQueued invoke() delivers its result across threads: the target
        ///       thread's place_meta_call() passes @p ret_slot as params[0] to the ordinary thunk, so
        ///       the result lands in the caller's variable before notify_done() releases the wait.
        /// @note No completion slot: a blocking poster is still inside invoke() when the event runs,
        ///       so IT marks the any_t constructed (invoke() does that after the wait returns). The
        ///       queued (non-blocking) form is the one that needs the event to mark.
        meta_call_event_t(meta_method_t slot, std::vector<any_t> args, void* ret_slot, std::shared_ptr<detail::event_sync_t> sync);

        /// @brief Destructor: releases a still-waiting BlockingQueued poster.
        /// @note Every path that destroys the event unexecuted — the receiver's teardown purge
        ///       (~object_t -> remove_object()), remove_posted_events(), or a delete after a
        ///       failed/excluded dispatch — wakes the poster through notify_cancelled(), which makes
        ///       the typed invoke() report false. After a normal delivery the sync is already
        ///       settled, so this is a no-op (not a second release).
        ~meta_call_event_t() override;

        /// @brief Clone (deep-copies the arguments).
        /// @return A newly allocated copy of the event.
        meta_call_event_t* clone() const override;

        /// @brief Refuse merging: dispatch of this event EXECUTES the queued call.
        /// @param newer Ignored.
        /// @return false. Folding this event into an earlier pending one would DELETE this call
        ///         without running it; for the blocking form the destructor would additionally
        ///         report @c notify_cancelled() to the waiting poster.
        bool merge_with(const event_t& newer) override;

        /// @brief Get the sender.
        /// @note noexcept.
        const object_t* sender() const noexcept;

        /// @brief Get the signal handle.
        /// @note noexcept.
        const meta_method_t& signal() const noexcept;

        /// @brief Get the slot handle.
        /// @note noexcept.
        const meta_method_t& slot() const noexcept;

        /// @brief Get the arguments (mutable).
        /// @note noexcept.
        std::vector<any_t>& args() noexcept;

        /// @brief Get the arguments (read-only).
        /// @note noexcept.
        const std::vector<any_t>& args() const noexcept;

        /// @brief Get the completion synchronisation.
        /// @return The sync object for the blocking form; empty otherwise.
        /// @note noexcept.
        const std::shared_ptr<detail::event_sync_t>& sync() const noexcept;

        /// @brief Perform this call on @p receiver (QAbstractMetaCallEvent::placeMetaCall
        ///        equivalent).
        /// @param receiver Target object.
        /// @return true when the slot handle is valid and was invoked; false otherwise.
        /// @note The functor form has no receiver: it runs here too (a functor event delivered to an
        ///       object by sendEvent() must not be a silent no-op).
        /// @note A value-returning queued invoke() completes here: the thunk writes into @c _ret_slot
        ///       (the caller's storage) and the completion slot named by @c _ret_any is then marked
        ///       constructed. That second step is what turns "the call was posted" (declared only) into
        ///       "the value is here" (is_initialized() true on the variable the caller still holds).
        bool place_meta_call(object_t& receiver) const;

        /// @brief Run the functor form of this event (the receiver-less path).
        /// @return true when this event carries a functor and it ran; false for a slot-form event.
        /// @note Runs on the calling thread; the sender frame is pushed around the call, so
        ///       object_t::sender() works inside the callable. A slot-form event returns false
        ///       without doing anything (there is no receiver to place it on).
        bool place_functor_call() const;

    protected:
        meta_call_event_t(const meta_call_event_t&) = default;

    private:
        const object_t* _sender = nullptr;
        meta_method_t _signal;
        meta_method_t _slot;
        std::vector<any_t> _args;
        std::shared_ptr<detail::event_sync_t> _sync;
        /// @brief Set for a functor (lambda) connection; empty for the slot forms.
        std::function<void(std::span<any_t>)> _functor;

        /// @brief The caller's return-value storage (params[0] when placed), or nullptr.
        /// @note Only the non-signal form sets it: a queued signal activation has no return channel
        ///       (the signal's own return value, if any, is discarded by dispatch_signal).
        /// @note It points at storage that belongs to the POSTER: the caller's variable for a `Ret&`
        ///       invoke, the reserved storage of the returned any_t for the non-Ret form. The blocking
        ///       form keeps it alive by construction (invoke() is still on the stack until the wait
        ///       returns); the non-blocking Queued form cannot, which is why the caller must not move
        ///       the returned any_t before the event runs.
        void* _ret_slot = nullptr;

        /// @brief The any_t to mark constructed once the call has been placed, or nullptr.
        /// @note Set only by the NON-BLOCKING non-signal form with a value-returning row. Its storage
        ///       is @c _ret_slot (the event writes the value there) and its type/flags are already in
        ///       place from the declared-only slot; only the "is it constructed?" flag is missing, and
        ///       that is what the event sets after the thunk ran.
        /// @note Null for a `Ret&` call (the caller's own variable, nothing to mark), for a void row,
        ///       and for the blocking form (its poster marks after the wait).
        any_t* _ret_any = nullptr;
    };
}

#endif
