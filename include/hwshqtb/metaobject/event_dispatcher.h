#ifndef HWSHQTB__METAOBJECT__EVENT_DISPATCHER_H
#define HWSHQTB__METAOBJECT__EVENT_DISPATCHER_H

/// @file event_dispatcher.h
/// @brief The event dispatcher event_dispatcher (QAbstractEventDispatcher equivalent): the posting
///        queue plus the platform event-pump boundary.

#include "event_t.h"
#include "export.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace hwshqtb::metaobject {
    /// @brief A platform-message filter (defined in native_event_filter_t.h).
    class native_event_filter_t;

    class object_t;

    /// @brief A thread object (defined in thread_t.h): the public thread identity.
    class thread_t;

    /// @brief The event loop (defined in event_loop_t.h): the waiter on the idle condition variable.
    class event_loop_t;

    namespace detail {
        /// @brief Per-thread bookkeeping (defined in thread_data_t.h): the owner of a dispatcher.
        struct thread_data_t;
    }

    /// @brief One event phase: the unit that "two frames between" means.
    ///
    /// A phase collects the events currently available (bounded by @c budget and @c max_events),
    /// merges what the policy allows, and dispatches them as one batch; the caller loops.
    /// @note The policy is per call: process_phase() applies @c merge for its duration and restores
    ///       the previous compression setting on return, so phase mode is never global state.
    struct event_phase_t {
        /// @brief Wall-clock budget for collecting and dispatching one phase.
        std::chrono::milliseconds budget{2};

        /// @brief Hard cap on events dispatched in one phase (flood backstop).
        /// @note Events over the cap stay queued for the next phase.
        std::size_t max_events{1024};

        /// @brief Dispatch PAINT after every other event of the phase.
        /// @note Only the PAINT entries pending when the dispatch pass starts are deferred; a PAINT
        ///       posted DURING the phase belongs to the next one.
        bool paint_last{true};

        /// @brief Merge events of the same type for the same receiver. ONE switch for all types —
        ///        window state and input alike. ON in phase mode: a phase delivers accumulated STATE,
        ///        not every intermediate step.
        /// @warning Lossy for events whose payload IS their identity (two key presses of different
        ///          keys, two button presses): the phase delivers ONE of them. Events that EXECUTE
        ///          work on dispatch (timer / meta call / deferred delete) refuse the merge
        ///          themselves, so their count is never reduced.
        bool merge{true};
    };

    /// @brief Event dispatcher (QAbstractEventDispatcher equivalent): event semantics plus the
    ///        platform boundary.
    ///
    /// **One dispatcher per thread**: instance() returns the calling thread's dispatcher, which owns
    /// that thread's @c post_event queue. The posting queue, the merge/compression rules, the phase
    /// budget, the deferred dispatch and the quit flag are implemented here; a platform
    /// implementation supplies the native message source through the four pure virtuals
    /// start_platform() / stop_platform() / fetch_native() / wait_native().
    ///
    /// @par Without a platform implementation
    /// A thread on which no implementation is installed answers a built-in software dispatcher whose
    /// platform side does nothing: it pumps no native message and its wait returns at once. Every
    /// event that arrives through post_event() still runs, so a pure software simulation drives the
    /// loop without a window system.
    ///
    /// @par Threads
    /// Each thread gets its own dispatcher on first use (thread-local). Only the dispatcher installed
    /// on a thread pumps that thread's native messages; every other thread answers the built-in
    /// software dispatcher.
    ///
    /// @warning Dispatchers are **never destroyed** (thread-local pointers to heap objects): this
    ///          keeps @c ~object_t calls to remove_object() safe during static destruction as well.
    /// @sa event_loop_t
    class HWSHQTB_EXPORT event_dispatcher {
    public:
        /// @brief Get the calling thread's dispatcher.
        /// @return A reference to this thread's dispatcher: the replacement installed with
        ///         set_instance(), or the built-in software one, created on first use and never
        ///         destroyed.
        /// @note noexcept.
        static event_dispatcher& instance() noexcept;

        /// @brief Get a thread's dispatcher (QAbstractEventDispatcher::instance(QThread*)).
        /// @param thread The thread whose dispatcher to read; nullptr asks for the calling thread.
        /// @return That thread's dispatcher, or nullptr when it has none yet.
        /// @note Unlike instance(), this does NOT fall back to the calling thread: a foreign thread
        ///       without a dispatcher answers nullptr, exactly like QAbstractEventDispatcher::instance.
        /// @note A thread_t that was never started has no data yet, so it answers nullptr too.
        static event_dispatcher* instance(thread_t* thread) noexcept;

        /// @brief Replace the calling thread's dispatcher (QCoreApplication::setEventDispatcher).
        /// @param replacement The dispatcher to use; nullptr restores the built-in software
        ///        implementation.
        /// @note The dispatcher is **not** owned. Install it from the thread that will run the loop,
        ///       before exec() starts. Other threads keep their own dispatchers, so the per-thread
        ///       queue semantics of post_event() are unaffected.
        /// @note The calling thread's event routing is updated with the replacement at once, so an
        ///       object created before or after the installation posts to the same dispatcher.
        /// @note noexcept.
        static void set_instance(event_dispatcher* replacement) noexcept;

        /// @brief The replacement installed on the calling thread, if any.
        /// @return The pointer passed to set_instance(), or nullptr.
        /// @note noexcept.
        static event_dispatcher* replacement() noexcept;

        /// @brief Start the platform side (QAbstractEventDispatcher::startingUp).
        /// @note Pure virtual: the base has no platform dependency. Called by the owner of the
        ///       platform integration before the loop runs; implementations are idempotent.
        /// @note A failure is reported by the implementation (it logs and leaves its own state
        ///       unstarted), not by this signature.
        virtual void start_platform() = 0;

        /// @brief Shut the platform side down (QAbstractEventDispatcher::closingDown).
        /// @note Pure virtual: the counterpart of start_platform(); implementations are idempotent.
        virtual void stop_platform() = 0;

        /// @brief Pump ONE native platform message and deliver it, without translating it.
        /// @return true when a native message was delivered; false when the source is empty (or the
        ///         platform side is not started).
        /// @note Pure virtual; non-blocking, and the ONLY delivery path: a message obtained by
        ///       wait_native() is delivered here too.
        /// @note The implementation must hand every message to filter_native_message() and must not
        ///       interpret it itself: translation is the job of the installed native_event_filter_t
        ///       implementations.
        virtual bool fetch_native() = 0;

        /// @brief Wait for a native platform message for at most @p timeout.
        /// @param timeout The longest wait; a value <= 0 polls without blocking.
        /// @note Pure virtual. The message the wait obtains is delivered by the following
        ///       fetch_native(), not here.
        /// @note wake_up() cannot interrupt this wait: the platform owns it and it expires on its
        ///       own.
        virtual void wait_native(std::chrono::milliseconds timeout) = 0;

        /// @brief Process one round of events: pump one native message first, then drain the posting
        ///        queue.
        /// @param flags Event-processing flags.
        /// @return true if at least one event was dispatched in this round.
        /// @note When the queue is empty it waits in the platform's wait_native() instead of
        ///       busy-waiting; a built-in software dispatcher has nothing to wait for and returns at
        ///       once.
        /// @note Virtual: the round is implemented here, but a specialist replacement may override
        ///       it; the platform boundary itself is the four pure virtuals.
        virtual bool process_events(process_events_type_t flags = process_events_type_flag::ALL_EVENTS);

        /// @brief Run ONE phase: pump everything currently available (bounded by budget/max_events),
        ///        dispatch the posted queue with the phase's rules, then return. The caller loops.
        /// @param flags Event-processing flags; the same meaning as in process_events().
        /// @param phase The phase policy.
        /// @return true when at least one event was dispatched.
        /// @note The FIRST native-message retrieval blocks (the platform's wait_native()) when no
        ///       posted work is pending; every later one in the same phase is non-blocking. That
        ///       keeps an idle phase asleep instead of spinning.
        /// @note With @c phase.merge the dispatcher turns compression on for the whole phase and
        ///       also folds the entries already pending when the phase starts; the previous setting
        ///       is restored on return, including exceptional exits.
        /// @note At most @c phase.max_events events are dispatched and the phase stops once
        ///       @c phase.budget is spent; the remaining entries stay queued for the next phase.
        /// @note A PAINT queued DURING the phase (for example update() from a resize handler) is
        ///       left for the next phase; the paint_last pass dispatches only the paints that were
        ///       pending when the pass started.
        bool process_phase(process_events_type_t flags, const event_phase_t& phase);

        /// @brief Install a native event filter (QAbstractEventDispatcher::installNativeEventFilter).
        /// @param filter The filter; nullptr is ignored.
        /// @note The dispatcher does not own the filter; it must outlive its installation. Filters are
        ///       called in installation order and only on the thread that owns this dispatcher.
        void install_native_event_filter_t(native_event_filter_t* filter);

        /// @brief Remove a previously installed filter.
        /// @param filter The filter; unknown pointers are ignored.
        void remove_native_event_filter_t(native_event_filter_t* filter);

        /// @brief Post an event (takes ownership and deletes it after dispatching).
        /// @param receiver The receiver; nullptr -> the event is deleted immediately.
        /// @param event The event; nullptr -> no-op.
        /// @param priority Priority; higher priorities are dispatched first (Qt::EventPriority).
        /// @note With compression on, the event is offered to the first pending event of the same
        ///       receiver, type and priority through event_t::merge_with(); when that returns true the
        ///       new event is deleted here and never enqueued.
        void post_event(object_t* receiver, event_t* event, event_priority_enum priority = event_priority_enum::NORMAL);

        /// @brief Turn event compression on or off (QCoreApplication's event compression).
        /// @param enabled true to let a posted event be folded into a pending event of the same type
        ///        and receiver; false to enqueue every event.
        /// @note Default OFF: compression is opt-in, so no existing event stream changes behaviour.
        /// @note Thread-safe: post_event() may run on another thread. The setting governs events
        ///       posted TO this dispatcher's thread (the receiver's dispatcher), not the caller's.
        void set_event_compression(bool enabled) noexcept;

        /// @brief Whether event compression is on.
        /// @return The current setting.
        /// @note noexcept.
        bool event_compression() const noexcept;

        /// @brief How many events compression has absorbed so far (diagnostics and tests).
        /// @return The cumulative count for this dispatcher.
        /// @note noexcept; a relaxed atomic read, safe from any thread.
        std::size_t compressed_event_count() const noexcept;

        /// @brief Post an event that has NO receiver object onto THIS thread's queue.
        /// @param event The event; nullptr -> no-op. Ownership is taken.
        /// @param priority Priority; higher priorities are dispatched first.
        /// @note This is the receiver-less sibling of post_event(): the entry is dispatched through
        ///       meta_call_event_t::place_functor_call() instead of through an object's event()
        ///       handler. Callers normally use detail::post_event_to() instead of this member.
        /// @note Safe before the thread's loop starts: the event simply stays queued and the first
        ///       process_events()/send_posted_events() drains it.
        void post_event_without_receiver(event_t* event, event_priority_enum priority = event_priority_enum::NORMAL);

        /// @brief Start a timer whose ticks are delivered as timer_event_t to @p receiver.
        /// @param receiver The target object.
        /// @param milliseconds The interval; a value <= 0 is treated as 1.
        /// @param repeating Whether the timer re-arms after each tick.
        /// @return The timer id, always > 0.
        /// @note Owned by this dispatcher, i.e. by the calling thread's event loop.
        int register_timer(object_t* receiver, int milliseconds, bool repeating);

        /// @brief Stop a timer started with register_timer().
        /// @param timer_id The id; unknown ids are ignored.
        void unregister_timer(int timer_id);

        /// @brief Drop every timer owned by @p receiver (called by @c ~object_t).
        /// @param receiver The object about to be destroyed.
        /// @note noexcept.
        void remove_timers_of(object_t* receiver) noexcept;

        /// @brief Dispatch posted events immediately (QCoreApplication::sendPostedEvents).
        /// @param receiver Dispatch only to this receiver; nullptr -> all.
        /// @param type Dispatch only this type; None -> all types.
        /// @note Drains the posting queue WITHOUT pumping native messages, so it is callable from any
        ///       thread: worker threads must not call process_events(), because a native pump belongs
        ///       to the thread that owns the platform integration and blocks elsewhere.
        /// @note Takes no flags, like QCoreApplication::sendPostedEvents(): the matching entries queued
        ///       when the call starts are dispatched (a DeferredDelete held for an enclosing loop stays
        ///       queued; an event posted during the call may also run). Event filtering is NOT
        ///       implemented here — Qt's ProcessEventsFlag subset (EXCLUDE_USER_INPUT_EVENTS,
        ///       WaitForMoreEvents, ...) has no effect on this entry point, so there is no parameter
        ///       to carry it.
        void send_posted_events(object_t* receiver = nullptr, event_type_enum type = event_type_enum::NONE);

        /// @brief Remove and destroy posted events (QCoreApplication::removePostedEvents).
        /// @param receiver Target receiver; nullptr -> all.
        /// @param type Target type; None -> all types.
        void remove_posted_events(object_t* receiver, event_type_enum type = event_type_enum::NONE);

        /// @brief Wake a blocked wait up (the next process_events round returns immediately).
        /// @note noexcept.
        /// @note Waking a wait and interrupting the running round are the same flag, so Qt's separate
        ///       interrupt() (QAbstractEventDispatcher::interrupt) is not provided; this name also stops
        ///       a running process_events() round.
        /// @note The flag is @c std::atomic because a foreign thread sets it while the owning thread
        ///       reads it in process_events(). It is cleared on entry to a round.
        /// @note The call also notifies the dispatcher-owned idle condition variable, so a
        ///       @c posted_only worker loop sleeping in it handles the new event at once instead of
        ///       at the end of its 1 ms backstop timeout. A platform wait_native() is not
        ///       interruptible by this call (the platform owns that wait; it expires on its own).
        void wake_up() noexcept;

        /// @brief Drop every event posted to @p obj (called by @c ~object_t).
        /// @param obj The object about to be destroyed.
        /// @note noexcept.
        void remove_object(object_t* obj) noexcept;

        /// @brief Whether any event is pending dispatch.
        /// @param receiver Count only this receiver; nullptr -> all.
        /// @return true if there is one.
        /// @note noexcept.
        bool has_pending_events(const object_t* receiver = nullptr) const noexcept;

        /// @brief Number of events pending dispatch.
        /// @return The count.
        /// @note noexcept.
        std::size_t pending_event_count() const noexcept;

        /// @brief Whether a quit request has been seen.
        /// @return true if one was seen.
        /// @note noexcept.
        bool quit_requested() const noexcept;

        /// @brief Record a quit request.
        /// @note noexcept. A platform implementation calls it when it sees its quit-type native
        ///       message; the flag stays set until clear_quit_request().
        void request_quit() noexcept;

        /// @brief Clear the quit request.
        /// @note noexcept.
        void clear_quit_request() noexcept;

    private:
    protected:
        event_dispatcher() = default;
        virtual ~event_dispatcher() = default;
        event_dispatcher(const event_dispatcher&) = delete;
        event_dispatcher& operator=(const event_dispatcher&) = delete;

        /// @brief Hand one raw platform message to the installed native event filters.
        /// @param message The platform message; never nullptr.
        /// @return true when a filter claimed the message; it must then be dropped uninterpreted.
        /// @note A platform implementation calls this for every message it delivers through
        ///       fetch_native(). The base owns the filter list, so the claiming order is identical
        ///       on every platform, and the filters run on the thread that owns this dispatcher.
        bool filter_native_message(void* message);

    private:
        /// @brief The @c posted_only loop is the one waiter on @c _wake_cv.
        friend class event_loop_t;

        /// @brief Wait until wake_up() is called (or the timeout expires), consuming the wake.
        /// @param timeout The backstop timeout; wake_up() normally returns before it.
        /// @return true when wake_up() (or an already-pending wake) ended the wait; false on timeout.
        /// @note The predicate is evaluated under @c _wake_mutex, and wake_up() sets the flag under
        ///       the same mutex, so a wake that arrives between the caller's queue check and the
        ///       wait here cannot be lost. Only the idle wait of a @c posted_only loop uses this;
        ///       the flag itself also interrupts a running process_events() round.
        bool wait_for_wakeup(std::chrono::milliseconds timeout) noexcept;

        /// @brief One entry of the posting queue.
        struct entry {
            /// @brief Receiver.
            object_t* receiver = nullptr;
            /// @brief Event (owned).
            event_t* event = nullptr;
            /// @brief Priority.
            int priority = 0;
        };

        /// @brief One registered timer.
        struct timer_entry_t {
            /// @brief Timer id (always > 0).
            int id = 0;
            /// @brief Target object.
            object_t* receiver = nullptr;
            /// @brief Interval in milliseconds.
            int interval_ms = 1;
            /// @brief Whether the timer re-arms after each tick.
            bool repeating = false;
            /// @brief When the next tick is due.
            std::chrono::steady_clock::time_point deadline;
        };

        /// @brief Pump one native message without blocking (platform fetch_native()).
        /// @return true when a native message was delivered.
        bool pump_system_events();

        /// @brief Reserve one native message in the platform's wait_native() for the next pump.
        /// @note Bounded by the platform; wake_up() cannot interrupt it. The reserved message is
        ///       delivered by the following pump_system_events(), never by the wait itself.
        void wait_system_events();

        /// @brief Post a timer_event_t for every timer whose deadline has passed.
        /// @note One-shot timers are removed; repeating timers are re-armed from now.
        void fire_due_timers();

        /// @brief Fold duplicate pending entries with the event_t::merge_with rule.
        /// @note Uses the same (receiver, type, priority) key and the same "only the first pending
        ///       entry is a candidate" rule as post_event(); a merged event is deleted and counted in
        ///       compressed_event_count(). Receiver-less entries are left alone.
        void compact_posted_events();

        /// @brief Dispatch a single entry (including the DeferredDelete special case and the
        ///        blocking wake-up of a MetaCall).
        /// @param item The entry.
        /// @param flags Event-processing flags.
        /// @return true if it was dispatched (ownership of the entry is handled); false if it was
        ///         excluded for this round.
        bool dispatch_one(const entry& item, process_events_type_t flags);

        /// @brief Whether a DeferredDelete must stay queued for an enclosing loop of the thread that
        ///        posted it (the same-thread loop-level rule).
        /// @param deferred The event; its type is DeferredDelete.
        /// @return true when the event must wait for a later/outer round; false when this round may
        ///         dispatch it.
        /// @note The loop level is thread-local, so it is compared only when the dispatch runs on the
        ///       posting thread. A cross-thread DeferredDelete never waits on it.
        static bool deferred_delete_held(const deferred_delete_event_t& deferred) noexcept;

        /// @brief Whether event compression is enabled (see set_event_compression()).
        /// @note Atomic: written by set_event_compression() and read by post_event(), which may run on
        ///       different threads.
        std::atomic<bool> _event_compression{ false };

        /// @brief How many events compression has absorbed.
        /// @note Atomic: post_event() increments it under the queue mutex, while the accessor reads it
        ///       without taking that lock.
        std::atomic<std::size_t> _compressed_event_count{ 0 };

        /// @brief Posting queue.
        /// @note Guarded by _mutex: it is written by other threads through post_event().
        std::deque<entry> _queue;

        /// @brief Registered timers, guarded by _mutex.
        std::vector<timer_entry_t> _timers;

        /// @brief Installed native event filters, in installation order (event-thread only).
        std::vector<native_event_filter_t*> _native_filters;

        /// @brief Next timer id to hand out.
        int _next_timer_id = 1;
        /// @brief Guards _queue (cross-thread posting).
        mutable std::mutex _mutex;
        /// @brief Return-immediately flag (set by wake_up()).
        /// @note Atomic: a foreign thread stores it (cross-thread post_event / object move) while the
        ///       owning thread reads it in process_events(). Relaxed is enough for the flag itself;
        ///       the mutex pairing below is what makes a wait wake-up lossless.
        std::atomic<bool> _interrupted{ false };
        /// @brief Guards the idle-wait pairing of @c _interrupted with @c _wake_cv.
        std::mutex _wake_mutex;
        /// @brief Notifies the @c posted_only idle wait from wake_up() (the primary wake path).
        std::condition_variable _wake_cv;
        /// @brief Whether a quit request has been seen.
        bool _quit_requested = false;
    };

    namespace detail {
        /// @brief Post @p event into @p target_thread's dispatcher, with no receiver object.
        /// @param target_thread The thread whose event loop must run the event.
        /// @param event The event; nullptr -> no-op. Ownership is taken, the event is deleted after
        ///        dispatching (or by remove_posted_events()).
        /// @param priority Priority; higher priorities are dispatched first.
        /// @note Used by functor connections (object_t::dispatch_signal), which have no receiver object
        ///       to route post_event() by.
        /// @note Safe to call from a foreign thread: the target thread's dispatcher already exists
        ///       there, so posting never touches another thread's thread-local storage.
        /// @note Safe for a thread that has NOT started its loop yet: the entry is merely queued, and
        ///       the first loop round drains it. The target thread is woken up when it is a different
        ///       thread (waking one's own dispatcher would cut the current round short).
        HWSHQTB_EXPORT void post_event_to(thread_data_t& target_thread, event_t* event,
            event_priority_enum priority = event_priority_enum::NORMAL);
    }
}

#endif
