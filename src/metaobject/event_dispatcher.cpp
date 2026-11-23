#include <hwshqtb/metaobject/event_dispatcher.h>

#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/event_loop_t.h>
#include <hwshqtb/metaobject/native_event_filter_t.h>
#include <hwshqtb/metaobject/property_t.h>
#include <hwshqtb/metaobject/thread_t.h>
#include <hwshqtb/metaobject/log.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <thread>
#include <utility>

namespace hwshqtb::metaobject {
    namespace {
        /// @brief Per-thread replacement installed through set_instance(); nullptr = the built-in one.
        thread_local event_dispatcher* g_replacement = nullptr;

        /// @brief How long a round waits for a native message before returning (see wait_native()).
        constexpr std::chrono::milliseconds wait_slice{ 10 };

        /// @brief The built-in dispatcher: a usable dispatcher with no platform behind it.
        ///
        /// The whole point of the layer split: the object model and the event semantics work without
        /// any window system. It pumps no native message and its wait returns at once, so a pure
        /// software simulation feeds the loop exclusively through post_event().
        class software_event_dispatcher_t final: public event_dispatcher {
        public:
            software_event_dispatcher_t() = default;
            ~software_event_dispatcher_t() override = default;

            software_event_dispatcher_t(const software_event_dispatcher_t&) = delete;
            software_event_dispatcher_t& operator=(const software_event_dispatcher_t&) = delete;

        protected:
            void start_platform() override { }
            void stop_platform() override { }
            bool fetch_native() override { return false; }
            void wait_native(std::chrono::milliseconds) override { }
        };

        /// @brief Flushes the per-thread binding work when the current process_events() round ends.
        ///
        /// A guard so that **every** exit path flushes exactly once: the normal return, and the path
        /// taken when a handler or a native event filter throws out of the round.
        class binding_flush_guard {
        public:
            binding_flush_guard() = default;
            binding_flush_guard(const binding_flush_guard&) = delete;
            binding_flush_guard& operator=(const binding_flush_guard&) = delete;

            ~binding_flush_guard() {
                // flush_bindings() already catches exceptions from recompute(); what can still
                // escape is a user logging handler. A destructor must not let an exception escape.
                try {
                    flush_bindings();
                }
                catch (...) {
                    HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, "binding flush failed");
                }
            }
        };

        /// @brief Apply a phase's compression setting for exactly the phase's lifetime.
        class event_compression_guard {
        public:
            event_compression_guard(event_dispatcher& dispatcher, bool enabled):
                _dispatcher(dispatcher),
                _previous(dispatcher.event_compression()) {
                _dispatcher.set_event_compression(enabled);
            }

            ~event_compression_guard() {
                _dispatcher.set_event_compression(_previous);
            }

            event_compression_guard(const event_compression_guard&) = delete;
            event_compression_guard& operator=(const event_compression_guard&) = delete;

        private:
            /// @brief The dispatcher whose setting is restored.
            event_dispatcher& _dispatcher;
            /// @brief The setting to restore.
            bool _previous = false;
        };

        /// @brief The compression candidate key: the same triple post_event() compares.
        struct pending_key_t {
            object_t* receiver = nullptr;
            event_type_enum type = event_type_enum::NONE;
            int priority = 0;

            /// @brief Strict order used by the phase's compaction pass.
            /// @note std::less gives pointers a strict total order (plain < is unspecified for
            ///       unrelated pointers).
            friend bool operator<(const pending_key_t& a, const pending_key_t& b) noexcept {
                const std::less<object_t*> less;
                if (less(a.receiver, b.receiver)) {
                    return true;
                }
                if (less(b.receiver, a.receiver)) {
                    return false;
                }
                if (a.type != b.type) {
                    return static_cast<std::uint16_t>(a.type) < static_cast<std::uint16_t>(b.type);
                }
                return a.priority < b.priority;
            }
        };
    }

    event_dispatcher& event_dispatcher::instance() noexcept {
        if (g_replacement != nullptr) {
            return *g_replacement;
        }
        // Never destroyed: safe to call even during static destruction (~object_t -> remove_object).
        static thread_local event_dispatcher* dispatcher = new software_event_dispatcher_t();
        return *dispatcher;
    }

    event_dispatcher* event_dispatcher::instance(thread_t* thread) noexcept {
        if (thread == nullptr) {
            return &instance();
        }
        // Read the data field directly: thread_data_t::get_dispatcher() falls back to
        // current().get_dispatcher(), which is correct only for the CALLING thread. Mirroring
        // QAbstractEventDispatcher::instance(QThread*), a foreign thread without a dispatcher answers
        // nullptr instead of handing out the caller's.
        detail::thread_data_t* data = thread->_data;
        return (data != nullptr) ? data->dispatcher : nullptr;
    }

    void event_dispatcher::set_instance(event_dispatcher* replacement) noexcept {
        g_replacement = replacement;
        // The thread's thread_data_t caches the dispatcher so a FOREIGN thread can post into it
        // without touching this thread's thread-local storage. Keep the cache in step with the
        // installation: otherwise a post to an object of this thread would land on the previous
        // dispatcher while the loop drains the new one.
        detail::thread_data_t::current().dispatcher = (replacement != nullptr) ? replacement : &instance();
    }

    event_dispatcher* event_dispatcher::replacement() noexcept {
        return g_replacement;
    }

    bool event_dispatcher::pump_system_events() {
        // The platform owns the native message type and delivers every message it obtains to the
        // installed filters (filter_native_message()), so the base never sees a raw message and only
        // decides poll vs. wait.
        return fetch_native();
    }

    void event_dispatcher::wait_system_events() {
        wait_native(wait_slice);
    }

    bool event_dispatcher::filter_native_message(void* message) {
        if (_native_filters.empty()) {
            return false;
        }
        // The installed native filters get the raw message first. When one of them claims it, the
        // message is neither interpreted as an event_t nor dispatched.
        long filter_result = 0;
        for (native_event_filter_t* filter : _native_filters) {
            if (filter != nullptr && filter->native_event_filter_t_proc(message, &filter_result)) {
                return true;
            }
        }
        return false;
    }

    bool event_dispatcher::deferred_delete_held(const deferred_delete_event_t& deferred) noexcept {
        // The loop level is a per-thread nesting depth (event_loop_t::loop_level()), so it only means
        // something on the thread that recorded it. A cross-thread DeferredDelete has no enclosing
        // loop of the poster to wait for, so its level is ignored.
        return deferred._posting_thread == std::this_thread::get_id()
            && event_loop_t::loop_level() < deferred.loop_level();
    }

    bool event_dispatcher::dispatch_one(const entry& item, process_events_type_t flags) {
        if (item.event == nullptr) {
            return true; // empty entry: treated as already consumed
        }
        if (item.receiver == nullptr) {
            // A receiver-less event (a queued functor call). It places itself on this thread; the
            // blocking form hands its completion back to the poster. The entry is consumed either
            // way, so the "keep it queued for the next round" answer (false) must NOT be returned:
            // process_events() would push the entry back with a deleted event.
            if (item.event->type() == event_type_enum::META_CALL) {
                auto* call = static_cast<meta_call_event_t*>(item.event);
                (void)call->place_functor_call();
                if (call->sync()) {
                    call->sync()->notify_done();
                }
            }
            delete item.event;
            return true;
        }
        if ((flags & process_events_type_flag::EXCLUDE_USER_INPUT_EVENTS)
            && event_t::is_input(item.event->type())) {
            return false; // excluded this round: keep it queued for the next one
        }

        // DeferredDelete special case: do not hand it to event(), destroy the receiver directly
        // (Qt semantics).
        if (item.event->type() == event_type_enum::DEFERRED_DELETE) {
            // The level rule is same-thread only (see deferred_delete_held()). At a level shallower
            // than the one that posted the request it stays queued for a later round; a cross-thread
            // request is dispatched here, because the receiving thread has no nesting level of the
            // poster to defer to.
            if (deferred_delete_held(*static_cast<const deferred_delete_event_t*>(item.event))) {
                return false; // keep it queued for the enclosing loop
            }
            object_t* victim = item.receiver;
            delete item.event;
            delete victim; // ~object_t removes itself from the queue
            return true;
        }

        core_application* app = core_application::instance();
        if (app != nullptr) {
            app->notify(item.receiver, item.event);
        }
        else {
            item.receiver->event(item.event);
        }

        // blocking form: notify the poster once dispatching is done. This is the DELIVERED outcome;
        // every path that destroys the event without dispatching it (the receiver's teardown purge in
        // remove_object(), remove_posted_events(), ...) relies on ~meta_call_event_t() to report the
        // CANCELLED outcome instead, exactly like Qt's QAbstractMetaCallEvent destructor.
        if (item.event->type() == event_type_enum::META_CALL) {
            auto* call = static_cast<meta_call_event_t*>(item.event);
            if (call->sync()) {
                call->sync()->notify_done();
            }
        }

        delete item.event;
        return true;
    }

    bool event_dispatcher::process_events(process_events_type_t flags) {
        // The round's last act is flush_bindings(). The guard is created before any work so that
        // every return (and an exception from a handler) flushes exactly once.
        binding_flush_guard binding_flush;

        // Clear-on-entry semantics: a wake that arrives before a round starts is not carried into
        // it. Atomic because wake_up() may run on another thread.
        _interrupted.store(false, std::memory_order_relaxed);

        // (0) Fire due timers first, so a timer that expired during the previous wait is dispatched
        //     in this round rather than the next one. post_event() is called outside the lock.
        fire_due_timers();

        // (1) pump one native message: wait for it (bounded) when the queue is empty, then deliver
        //     it; otherwise only poll. The platform implementation hands it to the installed filters.
        bool queue_empty = false;
        {
            std::scoped_lock lock(_mutex);
            queue_empty = _queue.empty();
        }
        if (queue_empty) {
            wait_system_events();
        }
        (void)pump_system_events();

        // (2) drain the posting queue. Only the entries present at the start of this round are
        //     processed; entries excluded in this round go back to the tail for the next one.
        //     Dispatching happens outside the lock: handlers may post more events.
        bool delivered = false;
        std::size_t remaining = 0;
        {
            std::scoped_lock lock(_mutex);
            remaining = _queue.size();
        }
        while (remaining > 0 && !_interrupted.load(std::memory_order_relaxed)) {
            entry item;
            bool have = false;
            {
                std::scoped_lock lock(_mutex);
                if (!_queue.empty()) {
                    item = std::move(_queue.front());
                    _queue.pop_front();
                    have = true;
                }
            }
            if (!have) {
                break;
            }
            --remaining;
            if (dispatch_one(item, flags)) {
                delivered = true;
            }
            else {
                std::scoped_lock lock(_mutex);
                _queue.push_back(item);
            }
        }
        return delivered;
    }

    void event_dispatcher::fire_due_timers() {
        const auto now = std::chrono::steady_clock::now();
        std::vector<timer_entry_t> due;
        {
            std::scoped_lock lock(_mutex);
            for (auto it = _timers.begin(); it != _timers.end();) {
                if (it->deadline > now) {
                    ++it;
                    continue;
                }
                due.push_back(*it);
                if (it->repeating) {
                    it->deadline = now + std::chrono::milliseconds(it->interval_ms);
                    ++it;
                }
                else {
                    it = _timers.erase(it);
                }
            }
        }
        for (const auto& timer : due) {
            post_event(timer.receiver, new timer_event_t(timer.id));
        }
    }

    void event_dispatcher::compact_posted_events() {
        std::scoped_lock lock(_mutex);
        std::map<pending_key_t, event_t*> first_pending;
        for (auto it = _queue.begin(); it != _queue.end();) {
            // Receiver-less entries are never compressed (post_event() cannot produce one either).
            if (it->receiver == nullptr || it->event == nullptr) {
                ++it;
                continue;
            }
            const pending_key_t key{ it->receiver, it->event->type(), it->priority };
            const auto found = first_pending.find(key);
            if (found == first_pending.end()) {
                first_pending.emplace(key, it->event);
                ++it;
                continue;
            }
            // Only the first pending entry is a candidate, exactly as in post_event(): a refusal of
            // the first one keeps the later entry queued (and does not retry against another event).
            if (found->second->merge_with(*it->event)) {
                delete it->event;
                it = _queue.erase(it);
                _compressed_event_count.fetch_add(1, std::memory_order_relaxed);
            }
            else {
                ++it;
            }
        }
    }

    bool event_dispatcher::process_phase(process_events_type_t flags, const event_phase_t& phase) {
        // The phase policy owns compression only for this call: every exit path restores the setting
        // that was in force before it. merge=false therefore also suppresses a globally enabled
        // compression for the duration of the phase. Declared BEFORE the flush guard so its setting
        // is still in force when the phase's binding flush runs at the end.
        event_compression_guard compression_guard(*this, phase.merge);

        // The phase is one round of work as far as bindings are concerned: flush exactly once, on
        // every exit path (including an exception from a handler).
        binding_flush_guard binding_flush;

        // Clear-on-entry semantics, same as process_events(): a wake that arrived before the phase
        // is not carried into it.
        _interrupted.store(false, std::memory_order_relaxed);

        // (0) Fire the timers that expired since the previous phase.
        fire_due_timers();

        // (1) Collect native messages. The FIRST retrieval blocks only when the posting queue has no
        //     work (so an idle phase sleeps in the platform's wait_native() rather than spinning);
        //     once one message arrived, the rest of the collection is non-blocking. The budget starts
        //     after that wait, so a phase always gets its full dispatch budget after waking up.
        const bool queue_empty = pending_event_count() == 0;
        if (queue_empty) {
            wait_system_events();
        }
        (void)pump_system_events();
        const auto deadline = std::chrono::steady_clock::now() + phase.budget;
        while (std::chrono::steady_clock::now() < deadline
            && !_interrupted.load(std::memory_order_relaxed)
            && pump_system_events()) {
        }

        // (2) Fold the entries that accumulated before this phase. New posts during the phase are
        //     compressed by post_event() itself because the phase set compression on.
        if (phase.merge) {
            compact_posted_events();
        }

        // (3) The dispatch set is the queue as it is NOW. Events posted by a handler later belong to
        //     the next phase; the PAINT rule below makes that explicit for paints, whose interleaving
        //     would otherwise cause extra frames.
        std::size_t remaining = 0;
        std::vector<const event_t*> phase_paints;
        {
            std::scoped_lock lock(_mutex);
            remaining = _queue.size();
            if (phase.paint_last) {
                phase_paints.reserve(remaining);
                for (const entry& item : _queue) {
                    if (item.event != nullptr && item.event->type() == event_type_enum::PAINT) {
                        phase_paints.push_back(item.event);
                    }
                }
            }
        }

        std::size_t dispatched = 0;
        std::deque<entry> deferred_paints;

        // (4) First pass: everything except the paints pending at the phase start.
        while (remaining > 0 && dispatched < phase.max_events
            && std::chrono::steady_clock::now() < deadline
            && !_interrupted.load(std::memory_order_relaxed)) {
            entry item;
            bool have = false;
            {
                std::scoped_lock lock(_mutex);
                if (!_queue.empty()) {
                    item = std::move(_queue.front());
                    _queue.pop_front();
                    have = true;
                }
            }
            if (!have) {
                break;
            }
            --remaining;

            if (phase.paint_last && item.event != nullptr
                && item.event->type() == event_type_enum::PAINT) {
                const bool pending_at_start = std::find(phase_paints.begin(), phase_paints.end(),
                    static_cast<const event_t*>(item.event)) != phase_paints.end();
                if (pending_at_start) {
                    deferred_paints.push_back(item);
                }
                else {
                    // update() from a handler: the next phase owns this paint.
                    std::scoped_lock lock(_mutex);
                    _queue.push_back(item);
                }
                continue;
            }

            if (dispatch_one(item, flags)) {
                ++dispatched;
            }
            else {
                // Excluded for this phase (input filter, deferred delete held, ...): next phase.
                std::scoped_lock lock(_mutex);
                _queue.push_back(item);
            }
        }

        // (5) Second pass: the paints that were pending when the phase started, last.
        while (!deferred_paints.empty() && dispatched < phase.max_events
            && std::chrono::steady_clock::now() < deadline
            && !_interrupted.load(std::memory_order_relaxed)) {
            entry item = std::move(deferred_paints.front());
            deferred_paints.pop_front();
            if (dispatch_one(item, flags)) {
                ++dispatched;
            }
            else {
                std::scoped_lock lock(_mutex);
                _queue.push_back(item);
            }
        }

        // The cap or the budget ran out before the paint pass: leave the held paints for the next
        // phase instead of exceeding the phase's bound.
        while (!deferred_paints.empty()) {
            entry item = std::move(deferred_paints.front());
            deferred_paints.pop_front();
            std::scoped_lock lock(_mutex);
            _queue.push_back(item);
        }

        return dispatched > 0;
    }

    void event_dispatcher::post_event(object_t* receiver, event_t* event, event_priority_enum priority) {
        if (event == nullptr) {
            return;
        }
        if (receiver == nullptr) {
            delete event;
            return;
        }
        // The event belongs to the receiver's thread, not to the poster's (Qt semantics).
        event_dispatcher* target = this;
        if (detail::thread_data_t* receiver_thread = receiver->thread_data(); receiver_thread != nullptr) {
            event_dispatcher& other = receiver_thread->get_dispatcher();
            if (&other != this) {
                target = &other;
            }
        }

        entry item;
        item.receiver = receiver;
        item.event = event;
        const int prio = static_cast<int>(priority);
        item.priority = prio;

        {
            std::scoped_lock lock(target->_mutex);
            // Compression, following QCoreApplicationPrivate::compressEvent: the FIRST pending entry
            // with the same receiver, type and priority may absorb the new event. The receiver check
            // is essential - a resize for window A must not swallow window B's. The priority check
            // keeps a high-priority event from being delayed behind an earlier normal-priority entry
            // by being folded into it.
            if (target->_event_compression.load(std::memory_order_relaxed)) {
                for (auto it = target->_queue.begin(); it != target->_queue.end(); ++it) {
                    if (it->receiver != receiver || it->event == nullptr
                        || it->event->type() != event->type() || it->priority != prio) {
                        continue;
                    }
                    // Only the first matching entry is a candidate: its position is the position the
                    // merged event keeps.
                    if (it->event->merge_with(*event)) {
                        delete event;
                        target->_compressed_event_count.fetch_add(1, std::memory_order_relaxed);
                        return;
                    }
                    break;
                }
            }
            if (prio > static_cast<int>(event_priority_enum::NORMAL)) {
                // High priority: insert after the existing high-priority entries (FIFO within a level).
                auto pos = target->_queue.begin();
                while (pos != target->_queue.end() && pos->priority > static_cast<int>(event_priority_enum::NORMAL)) {
                    ++pos;
                }
                target->_queue.insert(pos, item);
            }
            else {
                target->_queue.push_back(item);
            }
        }

        if (target != this) {
            // Wake the receiving thread up so it picks the event up promptly.
            target->wake_up();
        }
    }

    void event_dispatcher::set_event_compression(bool enabled) noexcept {
        _event_compression.store(enabled, std::memory_order_relaxed);
    }

    bool event_dispatcher::event_compression() const noexcept {
        return _event_compression.load(std::memory_order_relaxed);
    }

    std::size_t event_dispatcher::compressed_event_count() const noexcept {
        return _compressed_event_count.load(std::memory_order_relaxed);
    }

    void event_dispatcher::post_event_without_receiver(event_t* event, event_priority_enum priority) {
        if (event == nullptr) {
            return;
        }
        entry item;
        item.receiver = nullptr; // receiver-less: dispatch_one() places it on this thread
        item.event = event;
        item.priority = static_cast<int>(priority);
        {
            std::scoped_lock lock(_mutex);
            if (item.priority > static_cast<int>(event_priority_enum::NORMAL)) {
                // High priority: insert after the existing high-priority entries (FIFO within a level).
                auto pos = _queue.begin();
                while (pos != _queue.end() && pos->priority > static_cast<int>(event_priority_enum::NORMAL)) {
                    ++pos;
                }
                _queue.insert(pos, item);
            }
            else {
                _queue.push_back(item);
            }
        }
        if (&event_dispatcher::instance() != this) {
            // Wake the receiving thread up so it picks the event up promptly. Never wake our own
            // dispatcher: that would make the current process_events() round return early.
            wake_up();
        }
    }

    void event_dispatcher::install_native_event_filter_t(native_event_filter_t* filter) {
        if (filter == nullptr) {
            return;
        }
        if (std::find(_native_filters.begin(), _native_filters.end(), filter) != _native_filters.end()) {
            return; // already installed: keep the installation order stable
        }
        _native_filters.push_back(filter);
    }

    void event_dispatcher::remove_native_event_filter_t(native_event_filter_t* filter) {
        std::erase(_native_filters, filter);
    }

    int event_dispatcher::register_timer(object_t* receiver, int milliseconds, bool repeating) {
        if (receiver == nullptr) {
            return 0;
        }
        const int interval = (milliseconds > 0) ? milliseconds : 1;
        std::scoped_lock lock(_mutex);
        timer_entry_t t;
        t.id = _next_timer_id++;
        t.receiver = receiver;
        t.interval_ms = interval;
        t.repeating = repeating;
        t.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval);
        _timers.push_back(t);
        return t.id;
    }

    void event_dispatcher::unregister_timer(int timer_id) {
        std::scoped_lock lock(_mutex);
        std::erase_if(_timers, [timer_id](const timer_entry_t& t) { return t.id == timer_id; });
    }

    void event_dispatcher::remove_timers_of(object_t* receiver) noexcept {
        if (receiver == nullptr) {
            return;
        }
        std::scoped_lock lock(_mutex);
        std::erase_if(_timers, [receiver](const timer_entry_t& t) { return t.receiver == receiver; });
    }

    void event_dispatcher::send_posted_events(object_t* receiver, event_type_enum type) {
        std::size_t i = 0;
        while (true) {
            entry taken;
            bool found = false;
            {
                std::scoped_lock lock(_mutex);
                while (i < _queue.size()) {
                    const entry& item = _queue[i];
                    const bool match = (receiver == nullptr || item.receiver == receiver)
                        && (type == event_type_enum::NONE
                            || (item.event != nullptr && item.event->type() == type));
                    if (!match) {
                        ++i;
                        continue;
                    }
                    // A DeferredDelete whose loop level has not been reached yet must stay in the
                    // queue — do not take it here, otherwise requeueing it would spin forever.
                    // deferred_delete_held() keeps the level rule on the posting thread only, so a
                    // cross-thread request is taken and dispatched in this round.
                    if (item.event != nullptr && item.event->type() == event_type_enum::DEFERRED_DELETE
                        && deferred_delete_held(*static_cast<const deferred_delete_event_t*>(item.event))) {
                        ++i;
                        continue;
                    }
                    taken = item; // take the value first: the reference dies on erase
                    _queue.erase(_queue.begin() + static_cast<std::ptrdiff_t>(i));
                    found = true;
                    break;
                }
            }
            if (!found) {
                return;
            }
            // Dispatch without holding the lock: the handler may post more events.
            dispatch_one(taken, process_events_type_flag::ALL_EVENTS);
            // Do not advance i: after the erase the same index is the next entry.
        }
    }

    void event_dispatcher::remove_posted_events(object_t* receiver, event_type_enum type) {
        std::scoped_lock lock(_mutex);
        std::erase_if(_queue, [&](const entry& item) {
            const bool match = (receiver == nullptr || item.receiver == receiver)
                && (type == event_type_enum::NONE
                    || (item.event != nullptr && item.event->type() == type));
            if (match) {
                delete item.event;
            }
            return match;
        });
    }

    void event_dispatcher::wake_up() noexcept {
        // Waking a blocked wait and interrupting the running round are the same flag: the round
        // checks it after every dispatched entry, and it is cleared at the start of the next round.
        //
        // The flag is written under _wake_mutex, which is also the mutex the posted_only idle wait
        // evaluates its predicate under: a wake that races with the transition into the wait is
        // therefore never lost.
        {
            std::scoped_lock lock(_wake_mutex);
            _interrupted.store(true, std::memory_order_relaxed);
        }
        _wake_cv.notify_all();
    }

    bool event_dispatcher::wait_for_wakeup(std::chrono::milliseconds timeout) noexcept {
        std::unique_lock lock(_wake_mutex);
        // Consume a wake that arrived between the caller's "queue empty" check and this call.
        if (_interrupted.load(std::memory_order_relaxed)) {
            _interrupted.store(false, std::memory_order_relaxed);
            return true;
        }
        const bool woken = _wake_cv.wait_for(lock, timeout, [this] {
            return _interrupted.load(std::memory_order_relaxed);
        });
        // The flag is consumed here, not by the caller: a pending wake never makes the next idle
        // wait return immediately, which would turn the loop into a busy wait.
        if (_interrupted.load(std::memory_order_relaxed)) {
            _interrupted.store(false, std::memory_order_relaxed);
        }
        return woken;
    }

    void event_dispatcher::remove_object(object_t* obj) noexcept {
        if (obj == nullptr) {
            return;
        }
        std::scoped_lock lock(_mutex);
        std::erase_if(_queue, [&](const entry& item) {
            if (item.receiver != obj) {
                return false;
            }
            delete item.event;
            return true;
        });
    }

    bool event_dispatcher::has_pending_events(const object_t* receiver) const noexcept {
        std::scoped_lock lock(_mutex);
        if (receiver == nullptr) {
            return !_queue.empty();
        }
        for (const auto& item : _queue) {
            if (item.receiver == receiver) {
                return true;
            }
        }
        return false;
    }

    std::size_t event_dispatcher::pending_event_count() const noexcept {
        std::scoped_lock lock(_mutex);
        return _queue.size();
    }

    bool event_dispatcher::quit_requested() const noexcept {
        return _quit_requested;
    }

    void event_dispatcher::request_quit() noexcept {
        _quit_requested = true;
    }

    void event_dispatcher::clear_quit_request() noexcept {
        _quit_requested = false;
    }

    // ---- detail::post_event_to ----

    void detail::post_event_to(thread_data_t& target_thread, event_t* event, event_priority_enum priority) {
        if (event == nullptr) {
            return;
        }
        // The dispatcher of the target thread, not of the caller. Safe from a foreign thread and
        // before that thread's loop has started.
        target_thread.get_dispatcher().post_event_without_receiver(event, priority);
    }
}