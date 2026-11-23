// thread_pool_t / runnable_t test (QThreadPool / QRunnable equivalents).
//
// The pool runs plain std::thread workers with a queue; this test covers the start/try_start/clear
// contract, the two counts, the drain-on-destruction rule, the global instance, exception handling
// and worker expiry.

#include <meta>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/runnable_t.h>
#include <hwshqtb/metaobject/thread_pool_t.h>

using namespace hwshqtb::metaobject;

namespace thread_pool_test {

using namespace std::chrono_literals;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// A task that counts its runs and its destruction (the auto_delete proof).
struct counting_task : runnable_t {
    static std::atomic<int> destroyed;

    std::atomic<int>* runs = nullptr;

    explicit counting_task(std::atomic<int>& counter):
        runs(&counter) { }

    void run() override {
        ++(*runs);
    }

    ~counting_task() override {
        ++destroyed;
    }
};
std::atomic<int> counting_task::destroyed{ 0 };

/// A task the pool must NOT delete after run().
struct kept_task : runnable_t {
    std::atomic<int>* runs = nullptr;

    explicit kept_task(std::atomic<int>& counter):
        runs(&counter) { }

    void run() override {
        ++(*runs);
    }

    bool auto_delete() const override {
        return false;
    }
};

/// A task that occupies its worker until released (used to force queueing). It lives on the stack,
/// so it must opt out of auto-deletion (runnable_t's default is Qt's "delete after run").
struct blocking_task : runnable_t {
    std::atomic<bool> started{ false };
    std::atomic<bool> release{ false };

    void run() override {
        started.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(1ms);
        }
    }

    bool auto_delete() const override {
        return false;
    }
};

/// A task whose run() throws; the pool must log it and keep the worker alive.
struct throwing_task : runnable_t {
    void run() override {
        throw std::runtime_error("boom");
    }
};

/// Poll @p predicate until it holds or the timeout expires.
template <typename predicate_t>
bool wait_until(predicate_t predicate, std::chrono::milliseconds timeout = 5000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

}  // namespace thread_pool_test

using namespace thread_pool_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    std::printf("== thread pool ==\n");

    // (1) N tasks increment a counter; wait_for_done(); every auto_delete task is gone afterwards.
    {
        thread_pool_t pool;
        pool.set_max_thread_count(4);
        std::atomic<int> runs{ 0 };
        counting_task::destroyed = 0;
        constexpr int count = 100;
        for (int i = 0; i < count; ++i) {
            pool.start(new counting_task(runs));
        }
        pool.wait_for_done();
        CHECK(runs.load() == count);
        CHECK(counting_task::destroyed.load() == count);
    }

    // (2) auto_delete() == false: the pool never deletes the task, it stays usable.
    {
        thread_pool_t pool;
        std::atomic<int> runs{ 0 };
        kept_task task(runs);
        pool.start(&task);
        pool.wait_for_done();
        CHECK(runs.load() == 1);
        runs = 0;
        pool.start(&task); // a second run of the same stack object: a delete would have corrupted it
        pool.wait_for_done();
        CHECK(runs.load() == 1);
    }

    // (3) max_thread_count() / active_thread_count() semantics.
    {
        thread_pool_t pool;
        CHECK(pool.max_thread_count() >= 1);
        pool.set_max_thread_count(3);
        CHECK(pool.max_thread_count() == 3);
        CHECK(pool.active_thread_count() == 0);

        blocking_task blocker;
        pool.start(&blocker);
        CHECK(wait_until([&] { return blocker.started.load(); }));
        CHECK(pool.active_thread_count() == 1);
        CHECK(pool.active_thread_count() <= pool.max_thread_count());

        blocker.release = true;
        pool.wait_for_done();
        CHECK(pool.active_thread_count() == 1); // the worker is idle, but still alive
    }

    // (4) clear() drops queued tasks without running them, deleting the auto_delete ones.
    {
        thread_pool_t pool;
        pool.set_max_thread_count(1);
        blocking_task blocker;
        pool.start(&blocker);
        CHECK(wait_until([&] { return blocker.started.load(); }));

        std::atomic<int> runs{ 0 };
        counting_task::destroyed = 0;
        pool.start(new counting_task(runs)); // queued: the only worker is busy
        pool.clear();
        CHECK(counting_task::destroyed.load() == 1);
        CHECK(runs.load() == 0);

        blocker.release = true;
        pool.wait_for_done();
        CHECK(runs.load() == 0);
    }

    // (5) destroying the pool without wait_for_done() still runs every queued task (drain).
    {
        std::atomic<int> runs{ 0 };
        counting_task::destroyed = 0;
        auto* pool = new thread_pool_t;
        pool->set_max_thread_count(2);
        constexpr int count = 50;
        for (int i = 0; i < count; ++i) {
            pool->start(new counting_task(runs));
        }
        delete pool;
        CHECK(runs.load() == count);
        CHECK(counting_task::destroyed.load() == count);
    }

    // (6) global_instance() is a single instance.
    {
        thread_pool_t& first = thread_pool_t::global_instance();
        thread_pool_t& second = thread_pool_t::global_instance();
        CHECK(&first == &second);
        CHECK(&first == &thread_pool_t::global_instance());
    }

    // (7) a throwing run() is logged as Critical, and the pool keeps working.
    {
        std::atomic<int> critical{ 0 };
        std::atomic<int> runs{ 0 };
        install_log_handler([&](log_level_enum level, const log_context_t&, std::string_view) {
            if (level == log_level_enum::CRITICAL) {
                ++critical;
            }
            return true;
        });

        {
            thread_pool_t pool;
            pool.set_max_thread_count(2);
            pool.start(new throwing_task);
            pool.start(new counting_task(runs));
            pool.wait_for_done();
            CHECK(runs.load() == 1);
            CHECK(critical.load() == 1);

            pool.start(new counting_task(runs)); // the worker survived the exception
            pool.wait_for_done();
            CHECK(runs.load() == 2);
        }
        install_log_handler({ });
    }

    // (8) try_start(): false when the task had to be queued, true when an idle worker took it.
    {
        thread_pool_t pool;
        pool.set_max_thread_count(1);
        blocking_task blocker;
        pool.start(&blocker);
        CHECK(wait_until([&] { return blocker.started.load(); }));

        std::atomic<int> runs{ 0 };
        counting_task::destroyed = 0;
        auto* queued = new counting_task(runs);
        CHECK(!pool.try_start(queued)); // no idle worker: queued

        blocker.release = true;
        pool.wait_for_done();
        CHECK(runs.load() == 1); // the queued task still ran
        CHECK(counting_task::destroyed.load() == 1);

        auto* immediate = new counting_task(runs);
        CHECK(pool.try_start(immediate)); // the worker is idle now: taken immediately
        pool.wait_for_done();
        CHECK(runs.load() == 2);
        CHECK(counting_task::destroyed.load() == 2);
    }

    // (9) expiry: a short timeout retires idle workers; the pool respawns one on demand.
    {
        thread_pool_t pool;
        pool.set_expiry_timeout(200);
        std::atomic<int> runs{ 0 };
        pool.start(new counting_task(runs));
        pool.wait_for_done();
        CHECK(runs.load() == 1);
        CHECK(pool.active_thread_count() == 1);

        pool.set_expiry_timeout(10); // now let the idle worker retire
        CHECK(wait_until([&] { return pool.active_thread_count() == 0; }));

        pool.start(new counting_task(runs)); // a new worker has to be spawned
        pool.wait_for_done();
        CHECK(runs.load() == 2);
    }

    if (g_failures == 0) {
        std::printf("  thread_pool: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("  thread_pool: %d of %d check(s) failed\n", g_failures, g_checks);
    return 1;
}
