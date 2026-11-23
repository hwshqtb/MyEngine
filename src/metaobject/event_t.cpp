#include <hwshqtb/metaobject/event_t.h>
#include <hwshqtb/metaobject/object_t.h>

#include <atomic>
#include <cstdint>

namespace hwshqtb::metaobject {
    // ---- detail::event_sync_t ----

    void detail::event_sync_t::notify_done() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (state != state_enum::PENDING) {
                return; // already settled: a later outcome must not overwrite the first one
            }
            state = state_enum::DONE;
        }
        cv.notify_all();
    }

    void detail::event_sync_t::notify_cancelled() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (state != state_enum::PENDING) {
                return; // already settled: a normal delivery stays a normal delivery
            }
            state = state_enum::CANCELLED;
        }
        cv.notify_all();
    }

    bool detail::event_sync_t::done() const {
        std::lock_guard<std::mutex> lock(mutex);
        return state == state_enum::DONE;
    }

    bool detail::event_sync_t::cancelled() const {
        std::lock_guard<std::mutex> lock(mutex);
        return state == state_enum::CANCELLED;
    }

    void detail::event_sync_t::wait() {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] {
            return state != state_enum::PENDING;
        });
    }

    // ---- event_t ----

    event_t::event_t(event_type_enum type) noexcept
        :
        _type(type) { }

    event_t::~event_t() = default;

    event_type_enum event_t::type() const noexcept {
        return _type;
    }

    bool event_t::spontaneous() const noexcept {
        return _spontaneous;
    }

    bool event_t::is_accepted() const noexcept {
        return _accepted;
    }

    void event_t::set_accepted(bool accepted) noexcept {
        _accepted = accepted;
    }

    void event_t::accept() noexcept {
        _accepted = true;
    }

    void event_t::ignore() noexcept {
        _accepted = false;
    }

    void event_t::set_spontaneous() noexcept {
        _spontaneous = true;
    }

    event_type_enum event_t::register_event_type(int hint) noexcept {
        static std::atomic<std::uint16_t> next{ static_cast<std::uint16_t>(event_type_enum::USER) };
        constexpr int lo = static_cast<int>(event_type_enum::USER);
        constexpr int hi = static_cast<int>(event_type_enum::MAX_USER);

        if (hint >= lo && hint <= hi) {
            // Adopt the suggested id and push the auto counter past it (duplicate hints are not
            // detected).
            const std::uint16_t target = static_cast<std::uint16_t>(hint + 1);
            std::uint16_t current = next.load(std::memory_order_relaxed);
            while (current < target
                && !next.compare_exchange_weak(current, target, std::memory_order_relaxed)) {
            }
            return static_cast<event_type_enum>(hint);
        }

        const std::uint16_t value = next.fetch_add(1, std::memory_order_relaxed);
        if (value > static_cast<std::uint16_t>(hi)) {
            return event_type_enum::NONE;
        }
        return static_cast<event_type_enum>(value);
    }

    event_t* event_t::clone() const {
        return new event_t(*this);
    }

    bool event_t::merge_with(const event_t& newer) {
        (void)newer;
        return true;
    }

    // ---- timer_event_t ----

    timer_event_t::timer_event_t(int timer_id) noexcept
        :
        event_t(event_type_enum::TIMER),
        _timer_id(timer_id) { }

    timer_event_t::~timer_event_t() = default;

    timer_event_t* timer_event_t::clone() const {
        return new timer_event_t(*this);
    }

    bool timer_event_t::merge_with(const event_t& newer) {
        (void)newer;
        return false;
    }

    int timer_event_t::timer_id() const noexcept {
        return _timer_id;
    }

    // ---- child_event_t ----

    child_event_t::child_event_t(event_type_enum type, object_t* child) noexcept
        :
        event_t(type),
        _child(child) { }

    child_event_t::~child_event_t() = default;

    child_event_t* child_event_t::clone() const {
        return new child_event_t(*this);
    }

    bool child_event_t::merge_with(const event_t& newer) {
        // A structural change is a fact, not a state: the two events name DIFFERENT children, so
        // keeping the earlier one and deleting the later one does not fold a state into its newer
        // value, it removes the second observation. The receiver would see one child appear where
        // two did, and the same loss applies to removal - the child that left is never reported.
        (void)newer;
        return false;
    }

    object_t* child_event_t::child() const noexcept {
        return _child;
    }

    bool child_event_t::added() const noexcept {
        return type() == event_type_enum::CHILD_ADDED;
    }

    bool child_event_t::polished() const noexcept {
        return type() == event_type_enum::CHILD_POLISHED;
    }

    bool child_event_t::removed() const noexcept {
        return type() == event_type_enum::CHILD_REMOVED;
    }

    // ---- deferred_delete_event_t ----

    deferred_delete_event_t::deferred_delete_event_t(int loop_level, int scope_level) noexcept
        :
        event_t(event_type_enum::DEFERRED_DELETE),
        _loop_level(loop_level),
        _scope_level(scope_level) { }

    deferred_delete_event_t::~deferred_delete_event_t() = default;

    deferred_delete_event_t* deferred_delete_event_t::clone() const {
        return new deferred_delete_event_t(*this);
    }

    bool deferred_delete_event_t::merge_with(const event_t& newer) {
        (void)newer;
        return false;
    }

    int deferred_delete_event_t::loop_level() const noexcept {
        return _loop_level;
    }

    int deferred_delete_event_t::scope_level() const noexcept {
        return _scope_level;
    }

    // ---- meta_call_event_t ----

    meta_call_event_t::meta_call_event_t(const object_t* sender, const meta_method_t& signal, meta_method_t slot, std::vector<any_t> args):
        event_t(event_type_enum::META_CALL),
        _sender(sender),
        _signal(signal),
        _slot(slot),
        _args(std::move(args)) { }

    meta_call_event_t::meta_call_event_t(const object_t* sender, const meta_method_t& signal, meta_method_t slot, std::vector<any_t> args, std::shared_ptr<detail::event_sync_t> sync):
        event_t(event_type_enum::META_CALL),
        _sender(sender),
        _signal(signal),
        _slot(slot),
        _args(std::move(args)),
        _sync(std::move(sync)) { }

    meta_call_event_t::meta_call_event_t(const object_t* sender, const meta_method_t& signal,
    std::function<void(std::span<any_t>)> functor, std::vector<any_t> args):
        event_t(event_type_enum::META_CALL),
        _sender(sender),
        _signal(signal),
        _args(std::move(args)),
        _functor(std::move(functor)) { }

    meta_call_event_t::meta_call_event_t(const object_t* sender, const meta_method_t& signal,
    std::function<void(std::span<any_t>)> functor, std::vector<any_t> args,
    std::shared_ptr<detail::event_sync_t> sync):
        event_t(event_type_enum::META_CALL),
        _sender(sender),
        _signal(signal),
        _args(std::move(args)),
        _sync(std::move(sync)),
        _functor(std::move(functor)) { }

    meta_call_event_t::meta_call_event_t(meta_method_t slot, std::vector<any_t> args, void* ret_slot, any_t* ret_any):
        event_t(event_type_enum::META_CALL),
        _slot(slot),
        _args(std::move(args)),
        _ret_slot(ret_slot),
        _ret_any(ret_any) { }

    meta_call_event_t::meta_call_event_t(meta_method_t slot, std::vector<any_t> args, void* ret_slot, std::shared_ptr<detail::event_sync_t> sync):
        event_t(event_type_enum::META_CALL),
        _slot(slot),
        _args(std::move(args)),
        _sync(std::move(sync)),
        _ret_slot(ret_slot) { }

    meta_call_event_t::~meta_call_event_t() {
        // The destructor releases a still-waiting BlockingQueued poster, so every way an event can
        // die unexecuted releases it:
        //   - the receiver's teardown purge: ~object_t -> event_dispatcher::remove_object();
        //   - a remove_posted_events() cleanup;
        //   - a delete after a failed or excluded dispatch.
        // After a normal delivery dispatch_one() already called notify_done(), so the sync is
        // settled and notify_cancelled() is not needed (and would be a no-op anyway).
        if (_sync && !_sync->done()) {
            _sync->notify_cancelled();
        }
    }

    meta_call_event_t* meta_call_event_t::clone() const {
        return new meta_call_event_t(*this);
    }

    bool meta_call_event_t::merge_with(const event_t& newer) {
        (void)newer;
        return false;
    }

    const object_t* meta_call_event_t::sender() const noexcept {
        return _sender;
    }

    const meta_method_t& meta_call_event_t::signal() const noexcept {
        return _signal;
    }

    const meta_method_t& meta_call_event_t::slot() const noexcept {
        return _slot;
    }

    std::vector<any_t>& meta_call_event_t::args() noexcept {
        return _args;
    }

    const std::vector<any_t>& meta_call_event_t::args() const noexcept {
        return _args;
    }

    const std::shared_ptr<detail::event_sync_t>& meta_call_event_t::sync() const noexcept {
        return _sync;
    }

    bool meta_call_event_t::place_meta_call(object_t& receiver) const {
        if (_functor) {
            return place_functor_call(); // no receiver needed: the callable carries its own target
        }
        if (!_slot.is_valid()) {
            return false;
        }
        // Build the generic_call params: params[0] is the return-value slot and params[1...] hold the
        // argument addresses. The non-signal form carries the POSTER's return-value storage there, so a
        // BlockingQueued invoke() gets its result written straight into the caller's variable before
        // the sync wakes that caller up; a signal activation has no return channel (nullptr).
        std::vector<void*> argv(_args.size() + 1, nullptr);
        argv[0] = _ret_slot;
        for (std::size_t i = 0; i < _args.size(); ++i) {
            argv[i + 1] = const_cast<void*>(static_cast<const void*>(&_args[i]));
        }
        // The library-internal erased entry takes the receiver as void*, and the arguments are
        // already boxed any_t values, not call-site types.
        detail::push_invoke_frame(const_cast<object_t*>(_sender));
        // A queued replay holds only the receiver's ADDRESS: the poster's receiver TYPE (and with it
        // the object's cvref) is gone, so the unknown qualifier is passed honestly. The argument match
        // already ran in the entry point that resolved the row.
        _slot.invoke_caller(static_cast<void*>(&receiver), static_cast<void**>(argv.data()),
            qualifier_flag::NONE);
        detail::pop_invoke_frame();
        if (_ret_any != nullptr) {
            // The thunk has built the object in the caller's storage (params[0] = _ret_slot, which IS
            // this variable's storage) and nothing threw, so the variable now DEFINES a value. The
            // poster could not do this: a non-blocking Queued invoke() returned before this event ran.
            // A value that was never written (a non-copyable reference return, invoked for effect
            // only) lands here too, exactly like the synchronous paths' silent corner.
            _ret_any->mark_constructed();
        }
        return true;
    }

    bool meta_call_event_t::place_functor_call() const {
        if (!_functor) {
            return false;
        }
        // The functor form receives the arguments as ANY-typed handles: a Queued connection copied
        // them by their declared types, so this event owns real values and hands out a span over them.
        // There is no params array and no raw pointer anywhere on this path.
        detail::push_invoke_frame(const_cast<object_t*>(_sender));
        // A throwing callable must not leave the frame behind: sender() would then read a frame of a
        // call that already ended.
        try {
            // The event OWNS these copies, so handing them out mutably is sound: a Direct call
            // aliases the emitter's argument, a Queued call owns a copy, and both may be written.
            _functor(std::span<any_t>(const_cast<any_t*>(_args.data()), _args.size()));
        }
        catch (...) {
            detail::pop_invoke_frame();
            throw;
        }
        detail::pop_invoke_frame();
        return true;
    }
}