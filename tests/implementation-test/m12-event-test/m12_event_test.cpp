// M12 test: event compression and the core_application additions.

#include <meta>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include <SDL3/SDL.h>

#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/event_loop_t.h>
#include <hwshqtb/metaobject/event_t.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/native_event_filter_t.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/thread_t.h>

using namespace hwshqtb::metaobject;

namespace m12_event_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// A user event that carries a value and merges later values into itself.
struct counted_event_t : event_t {
    explicit counted_event_t(int v) : event_t(event_type_enum::USER), _value(v) { }

    counted_event_t* clone() const override {
        return new counted_event_t(*this);
    }

    bool merge_with(const event_t& newer) override {
        if (newer.type() != type()) {
            return false;
        }
        _value = static_cast<const counted_event_t&>(newer).value();
        return true;
    }

    int value() const noexcept {
        return _value;
    }

private:
    int _value = 0;
};

/// Same type, but it declines the merge: the dispatcher must enqueue every instance.
struct plain_event_t : event_t {
    explicit plain_event_t(int v) : event_t(event_type_enum::USER), _value(v) { }

    plain_event_t* clone() const override {
        return new plain_event_t(*this);
    }

    bool merge_with(const event_t&) override {
        return false;
    }

    int value() const noexcept {
        return _value;
    }

private:
    int _value = 0;
};

/// Counts the User events reaching it.
struct counter_t : object_t {
    OBJECT
public:
    int received = 0;
    int last_value = -1;

    bool event(event_t* e) override {
        if (e->type() == event_type_enum::USER) {
            ++received;
            if (auto* c = dynamic_cast<counted_event_t*>(e); c != nullptr) {
                last_value = c->value();
            }
            else if (auto* p = dynamic_cast<plain_event_t*>(e); p != nullptr) {
                last_value = p->value();
            }
            e->accept();
            return true;
        }
        return object_t::event(e);
    }
};
META_OBJECT(counter_t);

/// Quits the application from a queued slot invocation.
struct quitter_t : object_t {
    OBJECT
public:
    int quits = 0;

    void do_quit() {
        ++quits;
        core_application::quit();
    }

    SIGNAL(void fire() const)
};
META_OBJECT(quitter_t);

/// Counts about_to_quit emissions.
struct about_probe_t : object_t {
    OBJECT
public:
    int fired = 0;

    void on_about_to_quit() {
        ++fired;
    }
};
META_OBJECT(about_probe_t);

/// Counts native messages and optionally swallows them.
struct counting_filter_t : native_event_filter_t {
public:
    int calls = 0;
    int claimed = 0;
    bool claim_all = false;

    bool native_event_filter_t_proc(void* message, long* result) override {
        (void)message;
        ++calls;
        if (result != nullptr) {
            *result = 0;
        }
        if (claim_all) {
            ++claimed;
            return true;
        }
        return false;
    }
};

/// The platform side this test installs: SDL's event queue as the dispatcher's native message
/// source.
///
/// The core library ships no platform code of its own, so the test supplies the same pump the GUI
/// layer supplies in production: one SDL_Init(SDL_INIT_EVENTS) at start, one SDL_PollEvent per round,
/// SDL_WaitEventTimeout when a round would otherwise idle, the installed native filters for every
/// message, and the dispatcher's quit flag for a quit-type message.
class sdl_test_dispatcher_t final : public event_dispatcher {
public:
    ~sdl_test_dispatcher_t() override = default;

    void start_platform() override {
        _started = SDL_Init(SDL_INIT_EVENTS);
    }

    void stop_platform() override {
        if (_started) {
            SDL_Quit();
            _started = false;
        }
    }

    bool fetch_native() override {
        if (!_started) {
            return false;
        }
        // The wait reserves its message for this delivery path; every message is delivered here.
        if (_has_pending) {
            _has_pending = false;
            deliver_native(&_pending);
            return true;
        }
        SDL_Event message{};
        if (!SDL_PollEvent(&message)) {
            return false;
        }
        deliver_native(&message);
        return true;
    }

    void wait_native(std::chrono::milliseconds timeout) override {
        if (!_started || _has_pending) {
            return;
        }
        if (SDL_WaitEventTimeout(&_pending, static_cast<int>(timeout.count()))) {
            _has_pending = true;
        }
    }

private:
    /// @brief Filters first, then the quit-type check — the production delivery order.
    void deliver_native(void* message) {
        if (filter_native_message(message)) {
            return;
        }
        if (static_cast<SDL_Event*>(message)->type == SDL_EVENT_QUIT) {
            request_quit();
        }
    }

    /// @brief The message a wait reserved, until fetch_native() delivers it.
    SDL_Event _pending{};

    /// @brief Whether @c _pending holds a reserved message.
    bool _has_pending = false;

    /// @brief Whether SDL_Init succeeded and SDL_Quit has not run yet.
    bool _started = false;
};

/// L3: starts a NESTED event loop from inside a dispatched event. The nested loop must observe a
/// pending SDL quit request itself and leave the flag for the outer loop instead of clearing it
/// (only the outermost loop, loop_level() == 1, may clear it). The watchdog below is a fail-safe
/// for a reverted implementation, where the nested loop would never see the request and would
/// spin forever: it reports the hang as a failure instead of hanging the suite.
struct nested_loop_probe_t : object_t {
    OBJECT
public:
    int nested_runs = 0;
    int nested_code = -1;
    bool quit_after_nested = false;
    event_loop_t* outer = nullptr;

    bool event(event_t* e) override {
        if (e->type() == event_type_enum::USER) {
            ++nested_runs;
            event_loop_t inner;
            publish_inner(&inner);
            nested_code = inner.exec();
            clear_inner();
            // The nested loop must have LEFT the request pending: the outer loop still has to see
            // it and exit through it.
            quit_after_nested = event_dispatcher::instance().quit_requested();
            if (!quit_after_nested && outer != nullptr) {
                outer->exit(99);   // fail-safe: do not hang the suite on a reverted fix
            }
            e->accept();
            return true;
        }
        return object_t::event(e);
    }

    /// Publish/clear the nested loop for the watchdog thread (guarded: the loop object lives on
    /// this thread's stack and must not be touched after it returns).
    void publish_inner(event_loop_t* loop) {
        std::scoped_lock lock(_inner_mutex);
        _inner = loop;
    }

    void clear_inner() {
        std::scoped_lock lock(_inner_mutex);
        _inner = nullptr;
    }

    void stop_inner_if_running() {
        std::scoped_lock lock(_inner_mutex);
        if (_inner != nullptr) {
            _inner->exit(99);
        }
    }

private:
    std::mutex _inner_mutex;
    event_loop_t* _inner = nullptr;
};
META_OBJECT(nested_loop_probe_t);

/// L1 same-thread control victim: publishes its destruction through a flag owned by the test.
struct deferred_victim_t : object_t {
public:
    std::atomic<bool>* destroyed = nullptr;

    ~deferred_victim_t() override {
        if (destroyed != nullptr) {
            destroyed->store(true, std::memory_order_release);
        }
    }
};

/// L1 same-thread control probe: runs inside the OUTER loop (level 1) and enters an INNER loop
/// (level 2). Its inner slot calls delete_later() at level 2, so the event records level 2 on THIS
/// thread. Back at level 1 the event must still be queued (the rule the dispatcher must keep); a
/// second level-2 loop is then allowed to dispatch it.
struct same_thread_deferred_probe_t : object_t {
public:
    deferred_victim_t* victim = nullptr;
    std::atomic<bool>* destroyed = nullptr;
    event_loop_t* outer_loop = nullptr;
    event_loop_t* inner_loop = nullptr;
    int phase = 0;

    bool event(event_t* e) override {
        if (e->type() != event_type_enum::USER) {
            return object_t::event(e);
        }
        e->accept();
        if (phase == 0) {
            phase = 1;
            // One queued event for the inner loop: its slot is the level-2 delete_later() site.
            event_dispatcher::instance().post_event(this, new event_t(event_type_enum::USER));
            event_loop_t inner;
            inner_loop = &inner;
            inner.exec();
            inner_loop = nullptr;

            CHECK(event_loop_t::loop_level() == 1);
            // (a) Same-thread rule: level 1 < recorded 2, so the event must NOT be dispatched.
            //     If the rule were deleted entirely, this call would destroy the victim here.
            event_dispatcher::instance().send_posted_events();
            CHECK(!destroyed->load(std::memory_order_acquire));

            // (b) A loop at the recorded level dispatches it (2 < 2 is false).
            event_dispatcher::instance().post_event(this, new event_t(event_type_enum::USER));
            phase = 2;
            event_loop_t inner_again;
            inner_loop = &inner_again;
            inner_again.exec();
            inner_loop = nullptr;
            CHECK(destroyed->load(std::memory_order_acquire));
            if (outer_loop != nullptr) {
                outer_loop->exit(0);
            }
        }
        else if (phase == 1) {
            CHECK(event_loop_t::loop_level() == 2);
            victim->delete_later();   // same thread: records the level-2 nesting
            if (inner_loop != nullptr) {
                inner_loop->exit(0);
            }
        }
        else {
            CHECK(event_loop_t::loop_level() == 2);
            if (inner_loop != nullptr) {
                inner_loop->exit(0);
            }
        }
        return true;
    }
};

/// L2: starts a nested core_application::exec() from a slot. The nested loop ends through a pending
/// SDL quit, i.e. WITHOUT calling exit(), so the outer loop can only stop through the exit request
/// that was already pending when the nested exec() started. The old shared loop cleared that request
/// in prepare_exec().
struct outer_pending_probe_t : object_t {
    OBJECT
public:
    int stage = 0;
    int inner_code = -1;
    bool pushed_quit = false;

    bool event(event_t* e) override {
        if (e->type() != event_type_enum::USER) {
            return object_t::event(e);
        }
        e->accept();
        if (stage != 0) {
            return true;
        }
        stage = 1;
        // This message is pumped by the nested loop: the outer round already read its queue.
        SDL_Event quit_message{};
        quit_message.type = SDL_EVENT_QUIT;
        pushed_quit = SDL_PushEvent(&quit_message);
        // Outer-directed exit request: pending, because this thread is inside the outer loop's slot.
        core_application::exit(9);
        inner_code = core_application::exec();
        return true;
    }
};
META_OBJECT(outer_pending_probe_t);

/// L2: outer exec() -> slot -> inner exec() -> slot -> exit(7). The broadcast must reach both loops:
/// the inner returns 7 and the outer then returns 7 as well, and about_to_quit fires once.
struct nested_exit_probe_t : object_t {
    OBJECT
public:
    int stage = 0;
    int inner_code = -1;

    bool event(event_t* e) override {
        if (e->type() != event_type_enum::USER) {
            return object_t::event(e);
        }
        e->accept();
        if (stage == 0) {
            stage = 1;
            // A round only drains the entries present at its start, so this event is dispatched by
            // the nested loop, not by the outer one.
            core_application::post_event(this, new event_t(event_type_enum::USER));
            inner_code = core_application::exec();
        }
        else {
            stage = 2;
            core_application::exit(7);
        }
        return true;
    }
};
META_OBJECT(nested_exit_probe_t);

/// L12: outer core_application::exec() -> slot -> BARE event_loop_t::exec() -> slot ->
/// core_application::quit(). The quit must be broadcast to BOTH loops; the old single-target quit
/// reached only the application loop and left the bare inner loop running.
struct nested_bare_quit_probe_t : object_t {
    OBJECT
public:
    int stage = 0;
    int inner_code = -1;

    bool event(event_t* e) override {
        if (e->type() != event_type_enum::USER) {
            return object_t::event(e);
        }
        e->accept();
        if (stage == 0) {
            stage = 1;
            event_loop_t inner;
            publish_inner(&inner);
            core_application::post_event(this, new event_t(event_type_enum::USER));
            inner_code = inner.exec();
            clear_inner();
        }
        else {
            stage = 2;
            core_application::quit();
        }
        return true;
    }

    /// Publish/clear the nested loop for the watchdog thread (the loop is a stack object of this
    /// thread and must not be touched after it returns).
    void publish_inner(event_loop_t* loop) {
        std::scoped_lock lock(_inner_mutex);
        _inner = loop;
    }

    void clear_inner() {
        std::scoped_lock lock(_inner_mutex);
        _inner = nullptr;
    }

    void stop_inner_if_running() {
        std::scoped_lock lock(_inner_mutex);
        if (_inner != nullptr) {
            _inner->exit(99);
        }
    }

private:
    std::mutex _inner_mutex;
    event_loop_t* _inner = nullptr;
};
META_OBJECT(nested_bare_quit_probe_t);

/// Control: a single-level exec() that leaves through exit(42) still returns 42 and emits
/// about_to_quit exactly once.
struct single_exit_probe_t : object_t {
    OBJECT
public:
    bool event(event_t* e) override {
        if (e->type() != event_type_enum::USER) {
            return object_t::event(e);
        }
        e->accept();
        core_application::exit(42);
        return true;
    }
};
META_OBJECT(single_exit_probe_t);

/// L12 follow-up: signals from INSIDE the application loop that the loop is registered. A worker
/// waits for this slot before calling quit()/exit(), so the request is made while the application
/// thread's active-loop stack is non-empty (the property under test).
struct loop_entered_probe_t : object_t {
    OBJECT
public:
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;

    bool event(event_t* e) override {
        if (e->type() != event_type_enum::USER) {
            return object_t::event(e);
        }
        e->accept();
        {
            const std::scoped_lock lock(mutex);
            entered = true;
        }
        cv.notify_all();
        return true;
    }

    /// @brief Wait (bounded) until the slot above has run.
    /// @return true if it ran; false on timeout.
    bool wait_entered(int milliseconds) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, std::chrono::milliseconds(milliseconds),
            [this] { return entered; });
    }
};
META_OBJECT(loop_entered_probe_t);

/// L12 follow-up (13)/(14): run the application loop on THIS thread, have a worker thread call
/// quit() (or exit(code)) once the loop is registered, and return what exec() returned.
///
/// The worker waits for the probe's slot, which runs inside the loop, so the application thread's
/// active-loop stack is registered when the request is made. The watchdog breaks the loop through an
/// SDL quit — a path that does not go through core_application::exit() — if the request did not stop
/// it, so the reverted ("calling thread's stack") implementation FAILS the assertions instead of
/// hanging the suite.
int run_cross_thread_request(core_application* root, int code, bool quit, bool& requested,
    bool& watchdog_fired) {
    auto* probe = new loop_entered_probe_t;
    probe->set_parent(root);
    core_application::post_event(probe, new event_t(event_type_enum::USER));

    std::atomic<bool> request_done{ false };
    std::atomic<bool> main_returned{ false };
    std::atomic<bool> fired{ false };

    std::thread worker([&] {
        if (!probe->wait_entered(5000)) {
            return;
        }
        if (quit) {
            core_application::quit();
        }
        else {
            core_application::exit(code);
        }
        request_done.store(true, std::memory_order_release);
    });

    std::thread watchdog([&] {
        // Wait for the worker's request first (bounded), then give the loop a generous margin to
        // return: the request itself is two atomic stores plus a condition-variable notify.
        const auto request_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!request_done.load(std::memory_order_acquire)
               && std::chrono::steady_clock::now() < request_deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto return_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (!main_returned.load(std::memory_order_acquire)
               && std::chrono::steady_clock::now() < return_deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!main_returned.load(std::memory_order_acquire)) {
            fired.store(true, std::memory_order_release);
            SDL_Event quit_message{};
            quit_message.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit_message);
        }
    });

    const int rc = core_application::exec();
    main_returned.store(true, std::memory_order_release);
    worker.join();
    watchdog.join();
    requested = request_done.load(std::memory_order_acquire);
    watchdog_fired = fired.load(std::memory_order_acquire);
    return rc;
}

/// L12 follow-up (15): a receiver created ON the worker thread, so its posting queue is the worker's.
/// The first delivery publishes "the worker's loop is running"; the second calls
/// core_application::exit() from INSIDE that loop, i.e. from a thread that is not the application
/// thread and has a running loop of its own.
struct worker_loop_receiver_t : object_t {
public:
    std::atomic<bool>* entered = nullptr;
    std::atomic<bool>* self_exit_requested = nullptr;

    bool event(event_t* e) override {
        if (e->type() != event_type_enum::USER) {
            return object_t::event(e);
        }
        e->accept();
        if (!entered->exchange(true, std::memory_order_acq_rel)) {
            return true;
        }
        // Second delivery: the loop is up. Ask the APPLICATION to exit; this worker's own loop is
        // not an application loop and must keep running.
        core_application::exit(56);
        self_exit_requested->store(true, std::memory_order_release);
        return true;
    }
};

/// L12 follow-up (15): runs a posted_only loop on this worker thread and publishes its exit code.
struct worker_loop_thread_t : thread_t {
    OBJECT
public:
    std::atomic<bool> entered{ false };
    std::atomic<bool> self_exit_requested{ false };
    std::atomic<int> code{ -1 };

    void run() override {
        // Constructed here, on the worker: object_t::thread() is this thread, so both events land on
        // the worker's queue and are dispatched by the loop below.
        worker_loop_receiver_t receiver;
        receiver.entered = &entered;
        receiver.self_exit_requested = &self_exit_requested;
        event_dispatcher::instance().post_event(&receiver, new event_t(event_type_enum::USER));
        event_dispatcher::instance().post_event(&receiver, new event_t(event_type_enum::USER));
        code.store(exec(), std::memory_order_release);
    }
};
META_OBJECT(worker_loop_thread_t);

}  // namespace m12_event_test

using namespace m12_event_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    std::printf("== M12: event and application ==\n");

    // (1) arguments() keeps argv[0].
    const auto args = core_application::arguments();
    CHECK(!args.empty());
    CHECK(args[0] == std::string_view(argv[0]));

    // (2) application_dir_path() is not empty.
    const auto dir = core_application::application_dir_path();
    CHECK(!dir.empty());

    // (3) With compression ON, three events of the same type and receiver collapse into one,
    //     carrying the newest value.
    auto& dispatcher = event_dispatcher::instance();
    dispatcher.set_event_compression(true);
    CHECK(dispatcher.event_compression());
    auto* receiver = new counter_t;
    receiver->set_parent(root);
    core_application::post_event(receiver, new counted_event_t(1));
    core_application::post_event(receiver, new counted_event_t(2));
    core_application::post_event(receiver, new counted_event_t(3));
    event_dispatcher::instance().send_posted_events();
    CHECK(receiver->received == 1);
    CHECK(receiver->last_value == 3);

    // (4) The same type going to a different receiver is NOT merged.
    auto* other = new counter_t;
    other->set_parent(root);
    core_application::post_event(receiver, new counted_event_t(10));
    core_application::post_event(other, new counted_event_t(20));
    event_dispatcher::instance().send_posted_events();
    CHECK(receiver->received == 2);
    CHECK(receiver->last_value == 10);
    CHECK(other->received == 1);
    CHECK(other->last_value == 20);

    // (5) An event that declines the merge is never merged.
    core_application::post_event(receiver, new plain_event_t(100));
    core_application::post_event(receiver, new plain_event_t(200));
    event_dispatcher::instance().send_posted_events();
    CHECK(receiver->received == 4);
    CHECK(receiver->last_value == 200);

    dispatcher.set_event_compression(false);
    CHECK(!dispatcher.event_compression());

    // (6) about_to_quit fires exactly once when exec() returns. The quit request is queued BEFORE
    //     exec() so the loop dispatches it on its first round.
    auto* quitter = new quitter_t;
    quitter->set_parent(root);
    auto* probe = new about_probe_t;
    probe->set_parent(root);
    CHECK(root->connect("about_to_quit() const", probe, "on_about_to_quit()").is_valid());
    CHECK(quitter->connect("fire() const", quitter, "do_quit()", connection_type_flag::QUEUED).is_valid());
    quitter->fire();                       // queued, not dispatched yet
    CHECK(quitter->quits == 0);
    const int rc = core_application::exec();
    CHECK(rc == 0);
    CHECK(quitter->quits == 1);
    CHECK(probe->fired == 1);

    // (7) A replaced dispatcher takes over process_events. The four platform hooks are pure virtual,
    //     so the replacement has to state its platform side explicitly.
    struct stub_dispatcher_t : event_dispatcher {
    public:
        int rounds = 0;
        bool process_events(process_events_type_t) override {
            ++rounds;
            return false;
        }

    protected:
        void start_platform() override { }
        void stop_platform() override { }
        bool fetch_native() override { return false; }
        void wait_native(std::chrono::milliseconds) override { }
    };
    stub_dispatcher_t stub;
    event_dispatcher::set_instance(&stub);
    CHECK(event_dispatcher::replacement() == &stub);
    event_dispatcher::instance().process_events();
    event_dispatcher::instance().process_events();
    CHECK(stub.rounds == 2);
    event_dispatcher::set_instance(nullptr);
    CHECK(event_dispatcher::replacement() == nullptr);

    // (8) A DeferredDelete recorded at a deeper loop level must NOT run at level 0: the dispatcher
    //     requeues it instead of deleting the receiver (and must not leak it either).
    struct victim_t : object_t {
    public:
        bool* flag = nullptr;
        ~victim_t() override {
            if (flag != nullptr) {
                *flag = true;
            }
        }
    };
    CHECK(event_loop_t::loop_level() == 0);

    bool destroyed_deep = false;
    auto* deep = new victim_t;
    deep->set_parent(root);
    deep->flag = &destroyed_deep;
    core_application::post_event(deep, new deferred_delete_event_t(1, 0));
    event_dispatcher::instance().send_posted_events();
    CHECK(!destroyed_deep);              // level 0 < recorded level 1 -> deferred

    // (9) At level 0 an ordinary delete_later() still runs immediately, as before M12.
    bool destroyed_now = false;
    auto* now = new victim_t;
    now->set_parent(root);
    now->flag = &destroyed_now;
    now->delete_later();
    event_dispatcher::instance().send_posted_events();
    CHECK(destroyed_now);

    // (9b) L1 control: the same-thread half of the loop-level rule must still apply. delete_later()
    //      is called at level 2 (outer loop -> slot -> inner loop -> slot) on this thread; back at
    //      level 1 the event must stay queued, and only a loop at level 2 may dispatch it. This is
    //      what keeps the fix from being "delete the level comparison entirely".
    {
        std::atomic<bool> destroyed{ false };
        auto* victim = new deferred_victim_t;
        victim->destroyed = &destroyed;
        event_loop_t outer;
        same_thread_deferred_probe_t probe;
        probe.victim = victim;
        probe.destroyed = &destroyed;
        probe.outer_loop = &outer;
        core_application::post_event(&probe, new event_t(event_type_enum::USER));

        const int rc = outer.exec();
        CHECK(rc == 0);
        CHECK(destroyed.load(std::memory_order_acquire));
    }

    // (10) A native filter sees pushed SDL messages; one that claims them stops the quit request
    //      from being registered, proving it runs before the dispatcher interprets the message.
    //      Sections (1)-(9) ran on the built-in software dispatcher (no platform at all); a native
    //      message source is installed here, exactly the way the GUI layer installs one.
    sdl_test_dispatcher_t platform_dispatcher;
    platform_dispatcher.start_platform();
    event_dispatcher::set_instance(&platform_dispatcher);
    auto& native_dispatcher = event_dispatcher::instance();
    CHECK(event_dispatcher::replacement() == &platform_dispatcher);

    counting_filter_t filter;
    native_dispatcher.clear_quit_request();
    native_dispatcher.install_native_event_filter_t(&filter);

    SDL_Event plain{};
    plain.type = SDL_EVENT_USER;
    plain.user.code = 1;
    CHECK(SDL_PushEvent(&plain));
    native_dispatcher.process_events();
    CHECK(filter.calls >= 1);
    CHECK(filter.claimed == 0);              // not claiming: the message passes through

    filter.claim_all = true;
    const int calls_before = filter.calls;
    SDL_Event quit_message{};
    quit_message.type = SDL_EVENT_QUIT;
    CHECK(SDL_PushEvent(&quit_message));
    // One native message per round and the SDL queue may still hold leftovers, so drain a few
    // rounds instead of assuming exactly one.
    for (int i = 0; i < 8 && filter.calls == calls_before; ++i) {
        native_dispatcher.process_events();
    }
    CHECK(filter.calls > calls_before);
    CHECK(filter.claimed >= 1);
    CHECK(!native_dispatcher.quit_requested());     // the filter swallowed the quit before it was seen

    native_dispatcher.remove_native_event_filter_t(&filter);
    filter.claim_all = false;
    const int calls_after_remove = filter.calls;
    SDL_Event quit_again{};
    quit_again.type = SDL_EVENT_QUIT;
    CHECK(SDL_PushEvent(&quit_again));
    for (int i = 0; i < 8 && !native_dispatcher.quit_requested(); ++i) {
        native_dispatcher.process_events();
    }
    CHECK(filter.calls == calls_after_remove);   // removed: no more calls
    CHECK(native_dispatcher.quit_requested());   // and the quit request now goes through
    native_dispatcher.clear_quit_request();

    // (11) L3: a NESTED event_loop.exec() started from a slot must not swallow the outer loop's SDL
    //      quit request. A quit is already pending (pumped into the dispatcher by the outer round,
    //      before it dispatches the slot) when the slot starts the nested loop: the nested loop
    //      leaves the flag alone, observes it and returns; the outer loop still sees it and exits.
    {
        // Start from an empty SDL queue so the pending quit below is the message the outer round
        // pumps. (The earlier sections pushed and pumped several messages.)
        SDL_Event stale{};
        while (SDL_PollEvent(&stale)) {
        }
        SDL_Event pending_quit{};
        pending_quit.type = SDL_EVENT_QUIT;
        CHECK(SDL_PushEvent(&pending_quit));
        // One spare system message: the nested round's pump then does not block for 10 ms in
        // SDL_WaitEventTimeout. The outer round consumes exactly one message per pump.
        SDL_Event spare{};
        spare.type = SDL_EVENT_USER;
        CHECK(SDL_PushEvent(&spare));

        event_loop_t outer_loop;
        auto* nested = new nested_loop_probe_t;
        nested->set_parent(root);
        nested->outer = &outer_loop;
        core_application::post_event(nested, new event_t(event_type_enum::USER));

        // Fail-safe for a reverted fix: the nested loop would clear the flag and never return.
        std::thread watchdog([nested] {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            nested->stop_inner_if_running();
        });
        const int rc = outer_loop.exec();
        watchdog.join();

        CHECK(rc == 0);                      // exited through the SDL quit, not the fail-safe 99
        CHECK(nested->nested_runs == 1);
        CHECK(nested->nested_code == 0);     // the nested loop itself observed the pending quit
        CHECK(nested->quit_after_nested);    // and left it pending for the outer loop
        CHECK(event_dispatcher::instance().quit_requested());
        event_dispatcher::instance().clear_quit_request();
    }

    // (12) L2/L12: each core_application::exec() owns its loop, quit()/exit() broadcast to every
    //      loop of the calling thread, and about_to_quit belongs to the outermost exec() only.
    {
        // (12a) A nested exec() must not wipe an exit request the outer loop already has pending.
        //       The nested loop leaves through a system quit (it never calls exit()), so the outer
        //       loop can only return 9 - the code requested before the nested exec() - if that
        //       request survived it. Old code: prepare_exec() of the shared loop cleared it and both
        //       loops returned 0.
        SDL_Event stale{};
        while (SDL_PollEvent(&stale)) {
        }
        event_dispatcher::instance().clear_quit_request();

        auto* pending_probe = new outer_pending_probe_t;
        pending_probe->set_parent(root);
        const int about_before_pending = probe->fired;
        core_application::post_event(pending_probe, new event_t(event_type_enum::USER));
        const int pending_rc = core_application::exec();
        CHECK(pending_probe->pushed_quit);
        CHECK(pending_probe->stage == 1);
        CHECK(pending_probe->inner_code == 0);        // the inner loop left through the SDL quit
        CHECK(pending_rc == 9);                       // the outer request survived the nested exec()
        CHECK(probe->fired - about_before_pending == 1);
        event_dispatcher::instance().clear_quit_request();

        // (12b) A request issued INSIDE the nested loop is broadcast: the inner returns 7 AND stops
        //       the outer loop; about_to_quit still fires once for the pair (old code: twice).
        auto* nested_exit = new nested_exit_probe_t;
        nested_exit->set_parent(root);
        const int about_before_exit = probe->fired;
        core_application::post_event(nested_exit, new event_t(event_type_enum::USER));
        const int nested_rc = core_application::exec();
        CHECK(nested_exit->stage == 2);
        CHECK(nested_exit->inner_code == 7);
        CHECK(nested_rc == 7);
        CHECK(probe->fired - about_before_exit == 1);

        // (12c) L12: a quit() issued inside a nested BARE loop stops both loops. The watchdog is a
        //       fail-safe for the old single-target behaviour (where the bare loop never sees the
        //       request and would spin forever): it reports the hang as inner_code 99, a failure.
        auto* nested_bare = new nested_bare_quit_probe_t;
        nested_bare->set_parent(root);
        const int about_before_bare = probe->fired;
        core_application::post_event(nested_bare, new event_t(event_type_enum::USER));
        std::thread watchdog([nested_bare] {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            nested_bare->stop_inner_if_running();
        });
        const int bare_rc = core_application::exec();
        watchdog.join();
        CHECK(nested_bare->stage == 2);
        CHECK(nested_bare->inner_code == 0);          // old code: 99 from the watchdog
        CHECK(bare_rc == 0);
        CHECK(probe->fired - about_before_bare == 1);

        // (12d) Control: the plain single-level case is unchanged - the exit code is returned and
        //       about_to_quit fires exactly once.
        auto* single_exit = new single_exit_probe_t;
        single_exit->set_parent(root);
        const int about_before_single = probe->fired;
        core_application::post_event(single_exit, new event_t(event_type_enum::USER));
        const int single_rc = core_application::exec();
        CHECK(single_rc == 42);
        CHECK(probe->fired - about_before_single == 1);
    }

    // (13) L12 follow-up: core_application REMEMBERS the thread that constructed it, so a worker's
    //      quit() reaches the application thread's loop. With the calling-thread target introduced by
    //      L2/L12, the worker walks its own empty stack: the request is a no-op and the watchdog has
    //      to break the loop (the from-any-thread capability this restores).
    {
        SDL_Event stale{};
        while (SDL_PollEvent(&stale)) {
        }
        event_dispatcher::instance().clear_quit_request();

        const int about_before = probe->fired;
        bool requested = false;
        bool watchdog_fired = false;
        const int rc = run_cross_thread_request(root, 0, /*quit=*/true, requested, watchdog_fired);

        CHECK(requested);                         // the worker really called quit()
        CHECK(!watchdog_fired);                   // ...and it stopped the application loop
        CHECK(rc == 0);                           // quit() is exit(0)
        CHECK(probe->fired - about_before == 1);  // outermost-only about_to_quit still holds
        CHECK(event_loop_t::loop_level() == 0);   // the application thread's loop unregistered
    }

    // (14) L12 follow-up: the cross-thread exit(rc) delivers its code to the application loop.
    {
        SDL_Event stale{};
        while (SDL_PollEvent(&stale)) {
        }
        event_dispatcher::instance().clear_quit_request();

        const int about_before = probe->fired;
        bool requested = false;
        bool watchdog_fired = false;
        const int rc = run_cross_thread_request(root, 23, /*quit=*/false, requested, watchdog_fired);

        CHECK(requested);
        CHECK(!watchdog_fired);
        CHECK(rc == 23);
        CHECK(probe->fired - about_before == 1);
        CHECK(event_loop_t::loop_level() == 0);
    }

    // (15) L12 follow-up: a worker's OWN loop is not an application loop. core_application::exit()
    //      targets the application thread's stack, so neither the application thread nor the worker
    //      itself can stop a thread_t loop through it; thread_t::exit() is the way to stop one.
    {
        worker_loop_thread_t worker;
        worker.set_parent(root);
        worker.start();
        for (int i = 0; i < 5000 && !worker.entered.load(std::memory_order_acquire); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(worker.entered.load(std::memory_order_acquire));   // its loop is registered

        // (a) The application thread asks first: the worker is not a target.
        core_application::exit(55);
        // (b) The worker itself asks from inside its own loop (the receiver's second delivery):
        //     also not a target, because the request goes to the application thread, not the caller.
        for (int i = 0; i < 2000 && !worker.self_exit_requested.load(std::memory_order_acquire); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(worker.self_exit_requested.load(std::memory_order_acquire));

        // Bounded negative check: exec() must NOT have returned after either request.
        CHECK(worker.wait(200) == false);
        CHECK(worker.code.load(std::memory_order_acquire) == -1);

        worker.exit(0);
        CHECK(worker.wait(5000) == true);
        CHECK(worker.code.load(std::memory_order_acquire) == 0);
    }

    // Uninstall the platform side again: the built-in software dispatcher takes over, so the
    // platform stop owned by ~core_application stays a no-op.
    event_dispatcher::set_instance(nullptr);
    platform_dispatcher.stop_platform();
    CHECK(event_dispatcher::replacement() == nullptr);

    std::printf(g_failures == 0 ? "  M12: all checks passed\n" : "  M12: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}