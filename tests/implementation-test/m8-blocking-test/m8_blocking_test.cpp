// M8 test: BlockingQueued on the same thread must be skipped instead of deadlocking.

#include <meta>
#include <atomic>
#include <cstdio>
#include <thread>

#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/thread_t.h>

using namespace hwshqtb::metaobject;

namespace m8_blocking_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

struct emitter_t : object_t {
    OBJECT
public:
    SIGNAL(void ping(int value) const)
};
META_OBJECT(emitter_t);

struct receiver_t : object_t {
    OBJECT
public:
    int calls = 0;
    int last_value = 0;
    std::thread::id last_thread;
    void on_ping(int v) {
        ++calls;
        last_value = v;
        last_thread = std::this_thread::get_id();
    }
};
META_OBJECT(receiver_t);

/// Owns a receiver on its own thread and drains that thread's posting queue.
struct worker_t : thread_t {
    OBJECT
public:
    receiver_t* receiver = nullptr;
    std::atomic<int> created{0};
    std::atomic<int> stop{0};

    void run() override {
        receiver = new receiver_t;      // owned by THIS thread (leaked on purpose)
        // It must share the root's type registry, otherwise check_args cannot resolve int and the
        // queued call is silently skipped. set_parent does not change the thread affinity.
        receiver->set_parent(core_application::instance());
        created = 1;
        while (stop.load() == 0) {
            event_dispatcher::instance().send_posted_events();
            std::this_thread::yield();
        }
    }
};
META_OBJECT(worker_t);

}  // namespace m8_blocking_test

using namespace m8_blocking_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    std::printf("== M8: BlockingQueued ==\n");

    auto* e = new emitter_t;
    e->set_parent(root);
    auto* r = new receiver_t;
    r->set_parent(root);

    // (1) Same thread + BlockingQueued: the guard must skip the call. If the guard were missing,
    //     ping() would block forever waiting for an event loop that cannot run, and this test would
    //     be killed by the outer timeout.
    auto blocking = e->connect("ping(int) const", r, "on_ping(int)",
                               connection_type_t{connection_type_flag::BLOCKING_QUEUED});
    CHECK(blocking.is_valid());
    e->ping(33);
    CHECK(r->calls == 0);       // skipped, no deadlock
    CHECK(r->last_value == 0);

    // (2) A plain Queued connection still works afterwards (the guard did not corrupt state).
    CHECK(blocking.disconnect());
    auto queued = e->connect("ping(int) const", r, "on_ping(int)",
                             connection_type_t{connection_type_flag::QUEUED});
    CHECK(queued.is_valid());
    e->ping(44);
    CHECK(r->calls == 0);       // not dispatched yet
    event_dispatcher::instance().send_posted_events();
    CHECK(r->calls == 1);
    CHECK(r->last_value == 44);

    // (3) Direct still works.
    CHECK(queued.disconnect());
    auto direct = e->connect("ping(int) const", r, "on_ping(int)");
    CHECK(direct.is_valid());
    e->ping(55);
    CHECK(r->calls == 2);
    CHECK(r->last_value == 55);

    // (4) Cross-thread BlockingQueued: emit() must block until the worker ran the slot.
    auto* w = new worker_t;
    w->set_parent(root);
    w->start();
    for (int i = 0; i < 20000000 && w->created.load() == 0; ++i) {
        std::this_thread::yield();
    }
    CHECK(w->created.load() == 1);
    CHECK(w->receiver != nullptr);

    if (w->receiver != nullptr) {
        auto cross = e->connect("ping(int) const", w->receiver, "on_ping(int)",
                                connection_type_t{connection_type_flag::BLOCKING_QUEUED});
        CHECK(cross.is_valid());
        e->ping(66);
        // Reaching the next line at all proves the blocking wait was released by the worker; the
        // slot has already run by the time ping() returns.
        CHECK(w->receiver->calls == 1);
        CHECK(w->receiver->last_value == 66);
        CHECK(cross.disconnect());
    }

    // (5) The default connection type (Auto) must resolve to Queued across threads: the slot runs on
    //     the receiver's thread, not on the emitter's.
    if (w->receiver != nullptr) {
        auto auto_conn = e->connect("ping(int) const", w->receiver, "on_ping(int)");
        CHECK(auto_conn.is_valid());
        const int before = w->receiver->calls;
        e->ping(77);
        for (int i = 0; i < 20000000 && w->receiver->calls == before; ++i) {
            std::this_thread::yield();
        }
        CHECK(w->receiver->calls == before + 1);
        CHECK(w->receiver->last_value == 77);
        CHECK(w->receiver->last_thread != std::this_thread::get_id());   // ran on the worker
        CHECK(auto_conn.disconnect());
    }

    w->stop = 1;
    CHECK(w->wait(5000));

    std::printf(g_failures == 0 ? "  M8 blocking: all checks passed\n"
                                : "  M8 blocking: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}