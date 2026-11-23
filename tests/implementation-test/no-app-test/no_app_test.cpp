// Can the event loop run without a core_application instance?
//
// This test constructs no core_application at all: it initialises the dispatcher by hand, registers
// the builtin types on a plain object, and drives a timer through event_loop_t::exec().

#include <meta>
#include <chrono>
#include <cstdio>
#include <thread>

#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/event_loop_t.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_type_t.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/timer_t.h>

using namespace hwshqtb::metaobject;

namespace no_app_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// Counts the events dispatched to it.
struct software_probe_t : object_t {
    OBJECT
public:
    int received = 0;

    bool event(event_t* e) override {
        if (e->type() != event_type_enum::USER) {
            return object_t::event(e);
        }
        ++received;
        e->accept();
        return true;
    }
};
META_OBJECT(software_probe_t);

}  // namespace no_app_test

using namespace no_app_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    std::printf("== event loop without core_application ==\n");
    CHECK(core_application::instance() == nullptr);

    // L2 guard: without an application object there is no application loop to run and no
    // application loop to stop. These must keep the old "_main_loop == nullptr" answers: exec()
    // returns 0, and quit()/exit() are no-ops (they must not touch this thread's loop stack).
    CHECK(core_application::exec() == 0);
    core_application::quit();
    core_application::exit(5);
    CHECK(core_application::exec() == 0);

    // The builtin names are registered by core_application's constructor, and this test deliberately
    // has no application: ask for the one type it looks up by name.
    (void)meta_type_t::register_type<int>();

    object_t owner;
    CHECK(meta_type_t::resolve_global("int").is_valid());

    // No platform implementation is installed, so instance() answers the built-in software
    // dispatcher: its platform side pumps nothing and its wait returns at once. Posted events are
    // the only source, and a phase must still dispatch them.
    CHECK(event_dispatcher::replacement() == nullptr);
    {
        software_probe_t probe;
        event_dispatcher& dispatcher = event_dispatcher::instance();
        dispatcher.post_event(&probe, new event_t(event_type_enum::USER));
        CHECK(dispatcher.pending_event_count() == 1);
        CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, event_phase_t{}));
        CHECK(probe.received == 1);
        CHECK(dispatcher.pending_event_count() == 0);
        CHECK(!dispatcher.quit_requested());   // no platform, no quit request to be seen
    }

    event_loop_t loop;
    timer_t ticker;
    ticker.set_interval(10);

    int ticks = 0;
    const auto handle = ticker.connect("timeout() const", [&]() {
        ++ticks;
        if (ticks >= 3) {
            loop.quit();               // leave exec() from the timer itself
        }
    });
    CHECK(handle.is_valid());

    ticker.start();
    const int rc = loop.exec();
    ticker.stop();

    CHECK(rc == 0);
    CHECK(ticks >= 3);

    // Queued signal delivery also works without an application object. With no platform installed
    // there is nothing to block on, so a round returns at once: the loop below sleeps between rounds
    // instead of relying on a pump to pace it, otherwise the rounds would all run out before a 10 ms
    // timer can fire.
    int delivered = 0;
    object_t receiver;
    receiver.set_parent(&owner);
    auto queued = ticker.connect("timeout() const", [&]() {
        ++delivered;
    });
    CHECK(queued.is_valid());
    ticker.start(10);
    for (int i = 0; i < 60 && delivered == 0; ++i) {
        event_dispatcher::instance().process_events();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ticker.stop();
    CHECK(delivered >= 1);

    event_dispatcher::instance().stop_platform();
    std::printf(g_failures == 0 ? "  no-app loop: all checks passed\n"
                                : "  no-app loop: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}