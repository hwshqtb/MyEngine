#include <hwshqtb/metaobject/event_loop_t.h>

#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/thread_data_t.h>

#include <chrono>
#include <cstddef>

namespace hwshqtb::metaobject {
    namespace {
        /// @brief Backstop idle timeout of a posted_only loop.
        ///
        /// A posted event wakes the idle wait through the dispatcher's condition variable, so this is
        /// NOT the normal wake path: it only bounds how long the loop can sleep when a wake is lost
        /// (for example a post that raced with the loop's own round) and how long a quit whose wake
        /// raced the wait can be delayed.
        constexpr std::chrono::milliseconds worker_idle_time{ 1 };
    }

    int event_loop_t::loop_level() noexcept {
        // One source of truth: the depth of the calling thread's active-loop stack. The counter is
        // atomic, so reading it needs no lock and cannot tear.
        return detail::thread_data_t::current().loop_level.load(std::memory_order_relaxed);
    }

    event_loop_t::event_loop_t(object_t* parent) {
        if (parent != nullptr) {
            set_parent(parent);
        }
    }

    event_loop_t::~event_loop_t() = default;

    bool event_loop_t::process_events(process_events_type_t flags) {
        return event_dispatcher::instance().process_events(flags);
    }

    void event_loop_t::process_events(process_events_type_t flags, int maximum_time) {
        auto& dispatcher = event_dispatcher::instance();
        const auto span = std::chrono::milliseconds(maximum_time > 0 ? maximum_time : 0);
        const auto deadline = std::chrono::steady_clock::now() + span;
        // Each round blocks for a short while at most (the platform wait inside the dispatcher)
        // and returns once the deadline is reached. A nested call on a loop that has a phase uses
        // the same phase policy.
        const std::optional<event_phase_t> phase_policy = _phase;
        if (phase_policy.has_value()) {
            do {
                dispatcher.process_phase(flags, *phase_policy);
            }
            while (std::chrono::steady_clock::now() < deadline);
        }
        else {
            do {
                dispatcher.process_events(flags);
            }
            while (std::chrono::steady_clock::now() < deadline);
        }
    }

    void event_loop_t::set_phase(std::optional<event_phase_t> phase) {
        _phase = phase;
    }

    std::optional<event_phase_t> event_loop_t::phase() const noexcept {
        return _phase;
    }

    void event_loop_t::prepare_exec() noexcept {
        _quit.store(false, std::memory_order_relaxed);
        _exit_code.store(0, std::memory_order_relaxed);
    }

    int event_loop_t::exec(process_events_type_t flags, loop_pump_enum pump) {
        prepare_exec();
        return run_exec(flags, pump);
    }

    int event_loop_t::run_exec(process_events_type_t flags, loop_pump_enum pump) {
        // Registers this loop on the calling thread's active-loop stack (and maintains
        // loop_level()) for the duration of this run: the node lives here, on the stack, so
        // registration is undone on every exit path, including an exception from a slot.
        // core_application::exit() broadcasts through the same stack.
        struct loop_stack_guard {
            detail::thread_data_t& data;
            detail::event_loop_stack_node_t node;

            explicit loop_stack_guard(event_loop_t* loop) noexcept
                : data(detail::thread_data_t::current()) {
                node.loop = loop;
                data.push_loop(node);
            }

            ~loop_stack_guard() {
                data.pop_loop(node);
            }

            loop_stack_guard(const loop_stack_guard&) = delete;
            loop_stack_guard& operator=(const loop_stack_guard&) = delete;
        } guard(this);

        _running.store(true, std::memory_order_relaxed);

        auto& dispatcher = event_dispatcher::instance();
        // The loop snapshots the policy when it starts: a set_phase() from inside a slot applies to
        // the next exec(), not to the loop that is already running.
        const std::optional<event_phase_t> phase_policy = _phase;
        // Remember THIS thread's dispatcher: exit() may run on another thread and must wake this
        // loop's idle condition variable, not the caller's. Dispatchers are never destroyed.
        _dispatcher.store(&dispatcher, std::memory_order_release);
        if (pump == loop_pump_enum::SYSTEM_AND_POSTED) {
            // Only the OUTERMOST loop clears a pending system quit request. A nested exec() (for
            // example one started from a slot of the outer loop) must leave the request pending:
            // clearing it here would swallow it, the outer loop would then see quit_requested()
            // == false and never exit. The nested loop observes the flag through its own
            // quit_requested() check below and returns, leaving the request for the outer loop.
            if (loop_level() == 1) {
                dispatcher.clear_quit_request();
            }
            while (!_quit.load(std::memory_order_acquire)) {
                if (phase_policy.has_value()) {
                    (void)dispatcher.process_phase(flags, *phase_policy);
                }
                else {
                    dispatcher.process_events(flags);
                }
                if (dispatcher.quit_requested()) {
                    break; // quit requested from the platform side
                }
            }
        }
        else {
            // A worker thread drains its own posting queue only: the native pump is the main
            // thread's job and would block here (see loop_pump_enum). It never consults
            // quit_requested() and never pumps native messages. Phase mode is therefore ignored
            // here: the phase pump is a native pump, not a posting-queue pump.
            while (!_quit.load(std::memory_order_acquire)) {
                // Whether the drain made progress is only a heuristic: a handler may post while the
                // queue is drained, so a round that dispatched can still end no smaller. The 1 ms
                // backstop below bounds the wait when it says "no progress".
                const std::size_t before = dispatcher.pending_event_count();
                dispatcher.send_posted_events();
                if (dispatcher.pending_event_count() >= before) {
                    // Nothing to do: wait on the dispatcher's condition variable instead of burning
                    // a core. A cross-thread post (or exit()) notifies it, so the event is handled
                    // promptly; the 1 ms timeout is only the backstop, not the wake path.
                    dispatcher.wait_for_wakeup(worker_idle_time);
                }
            }
        }

        _running.store(false, std::memory_order_relaxed);
        return _exit_code.load(std::memory_order_acquire);
    }

    bool event_loop_t::is_running() const noexcept {
        return _running.load(std::memory_order_relaxed);
    }

    void event_loop_t::wake_up() {
        event_dispatcher::instance().wake_up();
    }

    void event_loop_t::exit(int return_code) {
        // The code is stored before the request: a thread that observes _quit (acquire) also
        // observes the code (release). Both are atomic, so this is safe from any thread.
        _exit_code.store(return_code, std::memory_order_release);
        _quit.store(true, std::memory_order_release);
        // Wake this loop's idle wait (a posted_only loop sleeps on its dispatcher's condition
        // variable). The dispatcher is the LOOP thread's, not the caller's: it was published by
        // run_exec(). When the pointer is not visible yet the loop has not reached its wait
        // (it checks _quit before any wait), or the 1 ms backstop bounds the delay.
        if (event_dispatcher* dispatcher = _dispatcher.load(std::memory_order_acquire);
            dispatcher != nullptr) {
            dispatcher->wake_up();
        }
    }

    void event_loop_t::quit() {
        exit(0);
    }

    bool event_loop_t::event(event_t* e) {
        if (e != nullptr && e->type() == event_type_enum::QUIT) {
            quit();
            e->accept();
            return true;
        }
        return object_t::event(e);
    }
}

META_OBJECT(hwshqtb::metaobject::event_loop_t);
