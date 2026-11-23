// M9 test: timers actually tick, can be killed, and die with their owner.

#include <meta>
#include <chrono>
#include <cstdio>
#include <thread>

#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/event_t.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/timer_t.h>

using namespace hwshqtb::metaobject;

namespace m9_timer_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

struct ticker_t : object_t {
    OBJECT
public:
    int ticks = 0;
    int last_id = -1;
    int last_events = 0;

    void timer_event(timer_event_t* e) override {
        ++ticks;
        last_id = e->timer_id();
    }
};
META_OBJECT(ticker_t);

/// Counts timeout() emissions.
struct timeout_listener : object_t {
    OBJECT
public:
    int hits = 0;
    void on_timeout() { ++hits; }
};
META_OBJECT(timeout_listener);

/// Pump the dispatcher for roughly @p ms milliseconds (timers fire inside process_events).
void pump_for(int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        event_dispatcher::instance().process_events();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}  // namespace m9_timer_test

using namespace m9_timer_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    std::printf("== M9: timers ==\n");

    // (1) One-shot timer fires exactly once.
    auto* one = new ticker_t;
    one->set_parent(root);
    const int one_id = one->start_timer(20, false);
    CHECK(one_id > 0);
    pump_for(120);
    CHECK(one->ticks == 1);
    CHECK(one->last_id == one_id);
    pump_for(60);
    CHECK(one->ticks == 1);            // a one-shot does not fire again

    // (2) Repeating timer fires repeatedly.
    auto* rep = new ticker_t;
    rep->set_parent(root);
    const int rep_id = rep->start_timer(10, true);
    CHECK(rep_id > 0);
    pump_for(150);
    CHECK(rep->ticks >= 5);            // generous lower bound: 150ms / 10ms
    CHECK(rep->ticks <= 30);           // and an upper bound so a runaway loop is caught

    // (3) kill_timer stops it.
    rep->kill_timer(rep_id);
    const int after_kill = rep->ticks;
    pump_for(80);
    CHECK(rep->ticks == after_kill);

    // (4) Destroying the owner drops its timers: no crash, no further ticks.
    auto* doomed = new ticker_t;
    doomed->set_parent(root);
    CHECK(doomed->start_timer(5, true) > 0);
    pump_for(20);
    CHECK(doomed->ticks >= 1);
    const int doomed_ticks = doomed->ticks;
    delete doomed;                     // ~object_t must unregister the timer
    pump_for(60);                      // if the timer survived, this would post to freed memory
    CHECK(doomed_ticks >= 1);          // nothing to compare against, but we reached this line alive

    // (5) A zero or negative interval is clamped instead of disabling the timer.
    auto* clamped = new ticker_t;
    clamped->set_parent(root);
    CHECK(clamped->start_timer(0, false) > 0);
    pump_for(60);
    CHECK(clamped->ticks == 1);

    // (6) timer_t wraps the raw timer and re-emits timeout().
    auto* listener = new timeout_listener;
    listener->set_parent(root);
    auto* t = new timer_t;
    t->set_parent(root);
    t->set_interval(10);
    auto conn = t->connect("timeout() const", listener, "on_timeout()");
    CHECK(conn.is_valid());
    CHECK(!t->is_active());
    t->start();
    CHECK(t->is_active());
    pump_for(120);
    CHECK(listener->hits >= 4);
    t->stop();
    CHECK(!t->is_active());
    const int stopped_at = listener->hits;
    pump_for(60);
    CHECK(listener->hits == stopped_at);

    // (7) A one-shot timer_t fires once and goes inactive by itself.
    t->set_single_shot(true);
    CHECK(t->is_single_shot());
    t->start(10);
    CHECK(t->is_active());
    pump_for(80);
    CHECK(listener->hits == stopped_at + 1);
    CHECK(!t->is_active());            // the id must be cleared after a one-shot tick

    // (8) Changing the interval while running restarts the timer at the new period.
    t->set_single_shot(false);
    t->start(100);                     // slow
    pump_for(20);
    t->set_interval(10);               // restart as fast
    const int fast_start = listener->hits;
    pump_for(120);
    CHECK(listener->hits > fast_start + 3);

    std::printf(g_failures == 0 ? "  M9: all checks passed\n" : "  M9: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}