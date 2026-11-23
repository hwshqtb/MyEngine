// M8 test: thread_t (the thread_t equivalent).
//

#include <meta>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <atomic>
#include <thread>
#include <vector>

#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/event_loop_t.h>
#include <hwshqtb/metaobject/event_t.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/thread_t.h>

using namespace hwshqtb::metaobject;

namespace m8_thread_t_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// Records what run() observed on the worker thread.
struct probe_thread : thread_t {
    OBJECT
public:
    std::atomic<int> ran{0};
    std::atomic<int> code{-1};
    thread_t* seen_current = nullptr;
    /// @brief An object created ON the worker; leaked on purpose, read from the main thread.
    object_t* worker_object = nullptr;
    /// @brief worker_object->thread() as observed ON the worker (the started object, per Qt).
    thread_t* object_identity = nullptr;

    void run() override {
        seen_current = thread_t::current_thread();
        // The started object supersedes the identity current() adopted for this thread, so an object
        // created here must report `this` — exactly QThreadPrivate::start storing itself in the data.
        worker_object = new object_t;
        object_identity = worker_object->thread();
        ran = 1;
        code = exec();
    }
};
META_OBJECT(probe_thread);

/// Returns from run() at once: exercises the "worker finished, _finished is set" path.
struct one_shot_thread : thread_t {
    OBJECT
public:
    std::atomic<int> runs{0};

    void run() override {
        ++runs;
    }
};
META_OBJECT(one_shot_thread);

/// T2: a receiver created ON the worker thread, so its affinity (and therefore its posting queue)
/// is the worker's. custom_event() runs from inside thread_t::exec()'s posted_only round, which is
/// the point where event_loop_t::loop_level() answers 1 instead of 0.
struct level_receiver_t : object_t {
public:
    std::atomic<int>* observed = nullptr;

    void custom_event(event_t* event) override {
        if (event != nullptr && observed != nullptr) {
            observed->store(event_loop_t::loop_level());
        }
    }
};

/// Records the thread-local loop level before, inside and after thread_t::exec().
struct level_thread_t : thread_t {
    OBJECT
public:
    std::atomic<int> before{-1};
    std::atomic<int> inside{-1};
    std::atomic<int> after{-1};
    std::atomic<int> code{-1};

    void run() override {
        before.store(event_loop_t::loop_level());
        // The receiver is constructed here, on the worker: object_t::thread() then points at this
        // thread's thread_data_t, so post_event() targets THIS dispatcher (the worker's queue).
        level_receiver_t receiver;
        receiver.observed = &inside;
        event_dispatcher::instance().post_event(&receiver, new event_t(event_type_enum::USER));
        code.store(exec());
        after.store(event_loop_t::loop_level());
    }
};
META_OBJECT(level_thread_t);

/// L11: a receiver created ON the worker thread (so its posting queue is the worker's). Its event
/// handler timestamps the delivery so the test can measure the idle-wait wake-up latency.
struct wake_receiver_t : object_t {
    OBJECT
public:
    std::atomic<int>* handled = nullptr;
    std::atomic<long long>* handled_at_ns = nullptr;

    bool event(event_t* e) override {
        if (e->type() == event_type_enum::USER) {
            // Timestamp BEFORE publishing the count (release), so a reader that sees the count
            // also sees the timestamp of that same delivery.
            if (handled_at_ns != nullptr) {
                const long long now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count();
                handled_at_ns->store(now, std::memory_order_relaxed);
            }
            if (handled != nullptr) {
                handled->fetch_add(1, std::memory_order_release);
            }
            e->accept();
            return true;
        }
        return object_t::event(e);
    }
};
META_OBJECT(wake_receiver_t);

/// L11 worker: runs thread_t::exec() (loop_pump_enum::POSTED_ONLY) with a receiver whose affinity is
/// this thread. The values are plain atomics plus one pointer published under receiver_ready.
struct wake_worker_t : thread_t {
    OBJECT
public:
    std::atomic<bool> receiver_ready{ false };
    std::atomic<int> handled{ 0 };
    std::atomic<long long> handled_at_ns{ 0 };
    std::atomic<int> code{ -1 };
    wake_receiver_t* receiver = nullptr;   // valid while receiver_ready is true

    void run() override {
        wake_receiver_t local;
        local.handled = &handled;
        local.handled_at_ns = &handled_at_ns;
        receiver = &local;
        receiver_ready.store(true, std::memory_order_release);
        code.store(exec(), std::memory_order_release);
        receiver_ready.store(false, std::memory_order_release);
    }
};
META_OBJECT(wake_worker_t);

/// L1 regression: the victim is created ON the worker thread (so its affinity, and therefore the
/// queue its DeferredDelete must reach, is the worker's). Its destructor publishes the deletion.
struct deferred_victim_t : object_t {
public:
    std::atomic<bool>* destroyed = nullptr;

    ~deferred_victim_t() override {
        if (destroyed != nullptr) {
            destroyed->store(true, std::memory_order_release);
        }
    }
};

/// L1 regression worker: owns the victim on its own thread and drains it through thread_t::exec()
/// (loop_pump_enum::POSTED_ONLY), i.e. its loop level never exceeds 1.
struct deferred_worker_t : thread_t {
    OBJECT
public:
    std::atomic<bool> victim_ready{ false };
    std::atomic<bool> destroyed{ false };
    std::atomic<int> code{ -1 };
    deferred_victim_t* victim = nullptr;   // valid from victim_ready == true until the deletion

    void run() override {
        auto* created = new deferred_victim_t;
        created->destroyed = &destroyed;
        victim = created;
        victim_ready.store(true, std::memory_order_release);
        code.store(exec(), std::memory_order_release);
    }
};
META_OBJECT(deferred_worker_t);

/// L1 regression probe: the test enters its OUTER loop (level 1); this probe's User slot starts an
/// INNER loop (level 2), whose User slot calls delete_later() on the worker-owned victim. Before the
/// fix the event recorded level 2 and the worker (level 1) skipped it on every round, forever.
struct nested_delete_probe_t : object_t {
public:
    deferred_victim_t* victim = nullptr;
    event_loop_t* outer_loop = nullptr;
    event_loop_t* inner_loop = nullptr;
    std::atomic<int> posted_level{ -1 };
    std::atomic<bool> posted{ false };
    int outer_level = -1;

    bool event(event_t* e) override {
        if (e->type() != event_type_enum::USER) {
            return object_t::event(e);
        }
        e->accept();
        if (outer_level < 0) {
            outer_level = event_loop_t::loop_level();
            // Queue one more event for the inner loop, then enter it: that slot runs at level 2.
            event_dispatcher::instance().post_event(this, new event_t(event_type_enum::USER));
            event_loop_t inner;
            inner_loop = &inner;
            inner.exec();
            inner_loop = nullptr;
            if (outer_loop != nullptr) {
                outer_loop->exit(0);
            }
        }
        else {
            posted_level.store(event_loop_t::loop_level(), std::memory_order_release);
            if (victim != nullptr) {
                victim->delete_later();   // cross-thread: posted from the main thread at level 2
            }
            posted.store(true, std::memory_order_release);
            if (inner_loop != nullptr) {
                inner_loop->exit(0);
            }
        }
        return true;
    }
};

}  // namespace m8_thread_t_test

using namespace m8_thread_t_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    std::printf("== M8: thread_t ==\n");

    // (1) Before start(): not running, and the main thread has no thread_t.
    auto* t = new probe_thread;
    t->set_parent(root);
    CHECK(!t->is_running());
    CHECK(thread_t::current_thread() == nullptr);

    // (2) start() runs run() on a new thread, where current_thread() is the thread_t.
    t->start();
    for (int i = 0; i < 20000000 && t->ran.load() == 0; ++i) {
        std::this_thread::yield();
    }
    CHECK(t->ran.load() == 1);
    CHECK(t->seen_current == t);
    CHECK(t->is_running());

    // (2b) Identity agreement: the started thread_t supersedes the identity current() adopted for the
    //      worker (Qt: QThreadPrivate::start stores `this` in the thread data), so an object created
    //      in run() reports the started thread_t — both on the worker and from the main thread.
    CHECK(t->object_identity == t);
    object_t* const created_on_worker = t->worker_object;
    thread_t* const main_identity = root->thread();
    CHECK(created_on_worker != nullptr && created_on_worker->thread() == t);
    CHECK(created_on_worker != nullptr && created_on_worker->thread() != main_identity);

    // (3) wait(0) on a running thread times out and reports false.
    CHECK(t->wait(0) == false);

    // (4) exit(code) stops the loop, wait() succeeds and exec() returned the code.
    t->exit(7);
    CHECK(t->wait(5000) == true);
    CHECK(!t->is_running());
    CHECK(t->code.load() == 7);

    // Deliberately NOT asserted after the join: the worker's thread data is gone, so the leaked
    // worker_object's identity would be stale — the same contract as Qt, where an object must not be
    // used after its thread has finished. object_identity is only meaningful while the thread lives.

    // (5) A finished thread can be waited on again without hanging.
    CHECK(t->wait(0) == true);

    // (6) Regression: start() immediately followed by destruction used to leave a joinable
    //     std::thread behind — wait() saw _running == false before the worker ran, returned without
    //     joining, and std::thread::~thread called std::terminate. Reaching the next statement is
    //     the assertion: the process survived all 200 cycles.
    for (int i = 0; i < 200; ++i) {
        thread_t rapid;
        rapid.start();
    }

    // (7) wait() on a never-started thread returns without blocking and joins nothing.
    {
        thread_t idle;
        CHECK(!idle.is_running());
        CHECK(idle.wait(-1) == false);    // would hang forever if "started" were not tracked
        CHECK(idle.wait(1000) == false);  // the timeout branch must not sleep either
        CHECK(!idle.is_running());
    }

    // (8) A normal one-shot cycle: start() -> wait(-1) returns true via _finished -> destructor.
    {
        one_shot_thread once;
        once.start();
        CHECK(once.wait(-1) == true);
        CHECK(!once.is_running());
        CHECK(once.runs.load() == 1);
    }   // ~thread_t: quit() + wait(-1) on an already joined thread must stay quiet

    // (9) wait(timeout) on a started thread times out (false); the destructor still quits and joins.
    {
        thread_t slow;
        slow.start();
        CHECK(slow.wait(10) == false);   // exec() runs until quit(), so it cannot have finished
    }   // ~thread_t: must join rather than call std::terminate

    // (10) Restart after the worker finished: the second start() reaps the old thread and runs again,
    //      even when the first cycle was never wait()ed on.
    {
        one_shot_thread again;
        again.start();
        for (int i = 0; i < 20000000 && again.runs.load() == 0; ++i) {
            std::this_thread::yield();
        }
        for (int i = 0; i < 20000000 && again.is_running(); ++i) {
            std::this_thread::yield();
        }
        CHECK(again.runs.load() == 1);
        again.start();
        CHECK(again.wait(-1) == true);
        CHECK(again.runs.load() == 2);
    }

    // (11) T2: a worker executing through thread_t::exec() observes event_loop_t::loop_level() == 1.
    //      The hand-written worker loop this replaced never touched the thread-local counter, so the
    //      level stayed 0 inside run(); the one shared loop body (run_exec) owns the counter now.
    //      The level is thread-local and scoped to exec(): 0 before it, 1 inside it, 0 after it.
    //      NOTE: the cross-thread nested-loop deferred-delete regression is section (13).
    {
        level_thread_t level_worker;
        level_worker.start();
        for (int i = 0; i < 20000000 && level_worker.inside.load() < 0; ++i) {
            std::this_thread::yield();
        }
        CHECK(level_worker.inside.load() == 1);
        CHECK(level_worker.before.load() == 0);
        level_worker.exit(21);
        CHECK(level_worker.wait(5000) == true);
        CHECK(level_worker.code.load() == 21);
        CHECK(level_worker.after.load() == 0);
    }

    // (12) L11: a cross-thread post must WAKE the posted_only idle wait, not be picked up by the
    //      1 ms timeout. The worker sits in its dispatcher-owned condition-variable wait; the main
    //      thread posts and measures post -> handler. 1 ms is the OLD latency (the timeout is the
    //      only thing the old loop looked at), so a median well below it is the notification path.
    {
        wake_worker_t worker;
        worker.set_parent(root);
        worker.start();
        for (int i = 0; i < 5000 && !worker.receiver_ready.load(std::memory_order_acquire); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(worker.receiver_ready.load(std::memory_order_acquire));
        wake_receiver_t* receiver = worker.receiver;
        CHECK(receiver != nullptr);
        if (receiver != nullptr) {
            using clock = std::chrono::steady_clock;
            auto post_and_measure = [&](int target_count) -> long long {
                const long long posted_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                clock::now().time_since_epoch())
                                                .count();
                event_dispatcher::instance().post_event(receiver, new event_t(event_type_enum::USER));
                const auto deadline = clock::now() + std::chrono::seconds(2);
                while (worker.handled.load(std::memory_order_acquire) < target_count
                       && clock::now() < deadline) {
                    std::this_thread::yield();
                }
                const long long handled_ns = worker.handled_at_ns.load(std::memory_order_acquire);
                return handled_ns - posted_ns;
            };

            // Warm-up: the first post may land while the worker is still entering its wait; it is
            // not part of the measurement.
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            (void)post_and_measure(1);
            CHECK(worker.handled.load(std::memory_order_acquire) == 1);

            constexpr int trials = 12;
            std::vector<long long> latencies;
            latencies.reserve(trials);
            for (int trial = 0; trial < trials; ++trial) {
                // Let the loop drain and go idle again; the post then targets a sleeping wait.
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                latencies.push_back(post_and_measure(trial + 2));
            }
            CHECK(worker.handled.load(std::memory_order_acquire) == 1 + trials);

            std::sort(latencies.begin(), latencies.end());
            const long long median_ns = latencies[latencies.size() / 2];
            std::printf("  [probe] L11 wake latency over %d posts: min=%lldus median=%lldus "
                        "max=%lldus (1 ms backstop)\n",
                        trials,
                        latencies.front() / 1000,
                        median_ns / 1000,
                        latencies.back() / 1000);
            // The notification returns the wait immediately; the 1 ms timeout is only the backstop.
            // The budget is far above the measured notification latency (tens of microseconds) yet
            // far below a timeout-driven wait, whose latency is uniform over [0, 1 ms] and therefore
            // has a ~0.5 ms median. The MEDIAN over 12 posts is compared, so a single scheduler
            // hiccup cannot make the test flaky.
            constexpr long long wake_budget_ns = 250000;   // 0.25 ms
            CHECK(median_ns < wake_budget_ns);

            // Two concurrent posters must not lose events: every posted User event is delivered
            // exactly once (event_t is not compressible, so the count is 2 * N).
            constexpr int per_poster = 200;
            const int before_concurrent = worker.handled.load(std::memory_order_acquire);
            auto poster = [&] {
                for (int i = 0; i < per_poster; ++i) {
                    event_dispatcher::instance().post_event(receiver, new event_t(event_type_enum::USER));
                }
            };
            std::thread first(poster);
            std::thread second(poster);
            first.join();
            second.join();
            const int wanted = before_concurrent + 2 * per_poster;
            const auto concurrent_deadline = clock::now() + std::chrono::seconds(10);
            while (worker.handled.load(std::memory_order_acquire) < wanted
                   && clock::now() < concurrent_deadline) {
                std::this_thread::yield();
            }
            CHECK(worker.handled.load(std::memory_order_acquire) == wanted);

            worker.exit(0);
            CHECK(worker.wait(5000));
            CHECK(worker.code.load(std::memory_order_acquire) == 0);
        }
    }

    // (13) L1 regression: a DeferredDelete posted to ANOTHER thread's object must not compare the
    //      poster's loop level with the receiving thread's. The main thread is at level 2 (outer loop
    //      -> slot -> inner loop -> slot) when it calls delete_later() on the worker-owned victim; the
    //      worker's posted_only loop is level 1. The old unconditional comparison (1 < 2) skipped the
    //      event on every worker round, so the victim was never destroyed.
    {
        deferred_worker_t worker;
        worker.start();
        for (int i = 0; i < 20000000 && !worker.victim_ready.load(std::memory_order_acquire); ++i) {
            std::this_thread::yield();
        }
        CHECK(worker.victim_ready.load(std::memory_order_acquire));
        deferred_victim_t* victim = worker.victim;
        CHECK(victim != nullptr);
        if (victim != nullptr) {
            event_loop_t outer;
            nested_delete_probe_t probe;
            probe.victim = victim;
            probe.outer_loop = &outer;
            core_application::post_event(&probe, new event_t(event_type_enum::USER));

            const int rc = outer.exec();
            CHECK(rc == 0);
            CHECK(probe.outer_level == 1);
            CHECK(probe.posted_level.load(std::memory_order_acquire) == 2);   // posted at level 2
            CHECK(probe.posted.load(std::memory_order_acquire));

            // The destruction happens on the worker. Poll with a bound: a reverted dispatcher leaves
            // the event queued forever, and the test must report that instead of hanging.
            bool destroyed = false;
            for (int i = 0; i < 5000 && !destroyed; ++i) {
                destroyed = worker.destroyed.load(std::memory_order_acquire);
                if (!destroyed) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            CHECK(destroyed);

            worker.exit(0);
            CHECK(worker.wait(5000));
            CHECK(worker.code.load(std::memory_order_acquire) == 0);
        }
    }

    std::printf(g_failures == 0 ? "  M8 thread_t: all checks passed\n"
                                : "  M8 thread_t: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}