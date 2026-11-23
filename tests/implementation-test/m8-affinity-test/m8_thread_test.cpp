// M8 test: per-thread dispatchers and object thread affinity.
//

#include <meta>
#include <cstdio>
#include <atomic>
#include <thread>

#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/event_t.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/thread_t.h>

using namespace hwshqtb::metaobject;

namespace m8_thread_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// A receiver that does nothing but exist.
struct counter_t : object_t {
    OBJECT
public:
    int local = 0;
    int events = 0;
    void bump(int v) { local += v; }
    bool event(event_t* e) override {
        ++events;
        e->accept();
        return true;
    }
};
META_OBJECT(counter_t);

}  // namespace m8_thread_test

using namespace m8_thread_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    std::printf("== M8: per-thread dispatcher ==\n");

    // (1) The same thread always sees the same dispatcher.
    event_dispatcher& main_a = event_dispatcher::instance();
    event_dispatcher& main_b = event_dispatcher::instance();
    CHECK(&main_a == &main_b);

    // (2) A different thread sees a different dispatcher.
    event_dispatcher* from_thread = nullptr;
    std::thread worker([&from_thread] {
        from_thread = &event_dispatcher::instance();
    });
    worker.join();
    CHECK(from_thread != nullptr);
    CHECK(from_thread != &main_a);

    // (3) Queues are per thread: an event posted on the main thread is invisible to a worker's
    //     dispatcher.
    auto* receiver = new counter_t;
    receiver->set_parent(root);
    event_dispatcher::instance().post_event(receiver, new event_t(event_type_enum::USER));
    CHECK(event_dispatcher::instance().pending_event_count() == 1);

    std::size_t worker_pending = 1;
    std::thread worker2([&worker_pending] {
        worker_pending = event_dispatcher::instance().pending_event_count();
    });
    worker2.join();
    CHECK(worker_pending == 0);

    // (4) Draining on the main thread really empties its own queue.
    event_dispatcher::instance().process_events();
    CHECK(event_dispatcher::instance().pending_event_count() == 0);

    // (5) A worker can post and dispatch on its own queue; the main thread's queue is unaffected.
    std::atomic<int> dispatched{0};
    std::thread worker3([&dispatched] {
        auto* r = new counter_t;      // leaked on purpose: the test is short-lived
        event_dispatcher::instance().post_event(r, new event_t(event_type_enum::USER));
        if (event_dispatcher::instance().process_events()) {
            dispatched = 1;
        }
    });
    worker3.join();
    CHECK(dispatched.load() == 1);
    CHECK(event_dispatcher::instance().pending_event_count() == 0);

    // (6) Thread affinity: an object captures the thread it was constructed on, children inherit
    //     their parent's thread, and objects built on different threads differ.
    auto* main_obj = new counter_t;
    main_obj->set_parent(root);
    auto* main_thread = main_obj->thread();
    CHECK(main_thread != nullptr);
    CHECK(main_thread == root->thread());

    counter_t* worker_obj = nullptr;
    std::thread worker4([&worker_obj] {
        worker_obj = new counter_t;      // leaked on purpose: the test is short-lived
    });
    worker4.join();
    CHECK(worker_obj != nullptr);
    CHECK(worker_obj->thread() != main_thread);

    // (7) Cross-thread delivery, bounded so a failure is reported instead of hanging. The bound is
    //     generous on purpose: the worker must still be spinning when the main thread posts.
    std::atomic<counter_t*> foreign_ptr{nullptr};
    std::atomic<int> worker_saw{0};
    std::thread worker5([&foreign_ptr, &worker_saw] {
        auto* own = new counter_t;               // owned by this thread (leaked on purpose)
        foreign_ptr = own;
        for (long long i = 0; i < 200000000LL && worker_saw.load() == 0; ++i) {
            event_dispatcher::instance().send_posted_events();
            if (own->events > 0) {
                worker_saw = 1;
            }
            std::this_thread::yield();
        }
    });
    for (long long i = 0; i < 200000000LL && foreign_ptr.load() == nullptr; ++i) {
        std::this_thread::yield();
    }
    counter_t* foreign = foreign_ptr.load();
    CHECK(foreign != nullptr);
    if (foreign != nullptr) {
        event_dispatcher::instance().post_event(foreign, new event_t(event_type_enum::USER));
        // Decisive probe: did the entry actually land on the worker's queue?
        event_dispatcher* worker_dispatcher = event_dispatcher::instance(foreign->thread());
        CHECK(worker_dispatcher != nullptr);
        if (worker_dispatcher != nullptr) {
            std::printf("  [probe] worker queue after post = %zu\n",
                        worker_dispatcher->pending_event_count());
        }
    }
    worker5.join();
    CHECK(worker_saw.load() == 1);
    CHECK(foreign != nullptr && foreign->events == 1);

    // (8) move_to_thread: the whole subtree follows the target thread, and an object that still has
    //     a parent refuses to move (its affinity is inherited).
    auto* wt = new thread_t;
    wt->start();
    for (int i = 0; i < 20000000 && !wt->is_running(); ++i) {
        std::this_thread::yield();
    }
    CHECK(wt->is_running());

    auto* subject = new counter_t;
    auto* child = new counter_t;
    child->set_parent(subject);
    CHECK(subject->thread() == main_thread);
    subject->move_to_thread(wt);
    CHECK(subject->thread() != main_thread);       // now on the worker
    CHECK(child->thread() == subject->thread());   // subtree migrated

    auto* wedged = new counter_t;
    wedged->set_parent(root);
    wedged->move_to_thread(wt);
    CHECK(wedged->thread() == root->thread());    // unchanged: it has a parent

    wt->quit();
    CHECK(wt->wait(5000));

    std::printf(g_failures == 0 ? "  M8: all checks passed\n" : "  M8: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}