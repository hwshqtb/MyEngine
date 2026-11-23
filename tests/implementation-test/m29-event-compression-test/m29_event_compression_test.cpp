// M29 test: event compression (merge_with + the dispatcher's opt-in switch), the payload transfer
// rules of the queued window events, and the phase mode (event_phase_t / process_phase).

#include <meta>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <hwshqtb/gui/window_event_t.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/event_loop_t.h>
#include <hwshqtb/metaobject/event_t.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;

namespace m29_event_compression_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// A user event that relies on the DEFAULT merge rule (keep the earlier event, drop the later one)
/// and carries a value so the surviving instance is identifiable.
struct probe_event_t: event_t {
    explicit probe_event_t(event_type_enum type, int value):
        event_t(type),
        _value(value) { }

    int value() const noexcept {
        return _value;
    }

private:
    int _value = 0;
};

/// Records every event reaching it, including the event object's identity.
struct recorder_t: object_t {
    struct record_t {
        event_type_enum type = event_type_enum::NONE;
        int value = -1;
        event_t* event = nullptr;
    };

    std::vector<record_t> records;

    bool event(event_t* e) override {
        if (e == nullptr) {
            return false;
        }
        int value = -1;
        if (auto* probe = dynamic_cast<probe_event_t*>(e); probe != nullptr) {
            value = probe->value();
        }
        records.push_back(record_t{ e->type(), value, e });
        e->accept();
        return true;
    }
};

/// The value of record @p index, or -1 when the receiver has fewer records.
/// @note Out-of-range reads must not crash a FAILING assertion; the size checks carry the verdict.
int recorded_value(const recorder_t& recorder, std::size_t index) {
    return index < recorder.records.size() ? recorder.records[index].value : -1;
}

/// The event object of record @p index, or nullptr when the receiver has fewer records.
event_t* recorded_event(const recorder_t& recorder, std::size_t index) {
    return index < recorder.records.size() ? recorder.records[index].event : nullptr;
}

/// Counts events and makes each dispatch cost well over the phase budget (the budget check).
struct slow_recorder_t: object_t {
    int received = 0;

    bool event(event_t* e) override {
        ++received;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        e->accept();
        return true;
    }
};

/// Counts paints and can post a NEW paint while handling a user event, i.e. from inside a phase.
struct paint_poster_t: object_t {
    int paints = 0;
    bool post_paint_on_user = false;

    bool event(event_t* e) override {
        if (e->type() == event_type_enum::PAINT) {
            ++paints;
            e->accept();
            return true;
        }
        if (e->type() == event_type_enum::USER) {
            if (post_paint_on_user) {
                post_paint_on_user = false;
                event_dispatcher::instance().post_event(this, new event_t(event_type_enum::PAINT));
            }
            e->accept();
            return true;
        }
        return object_t::event(e);
    }
};

/// Receives timer events posted directly by the phase test (no real timer is registered).
struct timer_phase_receiver_t: object_t {
    int ticks = 0;
    int first_id = -1;
    int last_id = -1;

    void timer_event(timer_event_t* e) override {
        if (ticks == 0 && e != nullptr) {
            first_id = e->timer_id();
        }
        if (e != nullptr) {
            last_id = e->timer_id();
        }
        ++ticks;
    }
};

/// A queued signal connection: two emitted pings must produce two meta calls.
struct phase_signal_receiver_t: object_t {
    OBJECT
public:
    int hits = 0;

    void on_ping() {
        ++hits;
    }

    SIGNAL(void ping() const)
};
META_OBJECT(phase_signal_receiver_t);

/// Publishes its destruction, so the deferred-delete phase test can observe both deletes.
struct phase_victim_t: object_t {
    bool* destroyed = nullptr;

    ~phase_victim_t() override {
        if (destroyed != nullptr) {
            *destroyed = true;
        }
    }
};

/// Posts three user events from inside a phase (used to prove merge=false is phase-local).
struct fanout_t: object_t {
    object_t* target = nullptr;
    int posted = 0;

    bool event(event_t* e) override {
        if (e->type() == event_type_enum::USER && target != nullptr) {
            for (int i = 0; i < 3; ++i) {
                event_dispatcher::instance().post_event(
                    target, new probe_event_t(event_type_enum::USER, 120 + i));
            }
            posted += 3;
            e->accept();
            return true;
        }
        return object_t::event(e);
    }
};

/// Calls core_application::process_events() from inside the running loop after queueing a PAINT
/// before a USER event: the nested call must inherit the loop's phase, so the PAINT still comes last.
struct nested_process_probe_t: object_t {
    recorder_t* target = nullptr;

    bool event(event_t* e) override {
        if (e->type() == event_type_enum::USER && target != nullptr) {
            core_application::post_event(target, new event_t(event_type_enum::PAINT));
            core_application::post_event(target,
                new probe_event_t(event_type_enum::USER, 151));
            core_application::process_events(process_events_type_flag::ALL_EVENTS, 0);
            core_application::quit();
            e->accept();
            return true;
        }
        return object_t::event(e);
    }
};

/// The second user event type of the "different types" checks.
constexpr event_type_enum other_type =
    static_cast<event_type_enum>(static_cast<std::uint16_t>(event_type_enum::USER) + 1);

/// A plain object used as the child a child_event_t names.
struct child_object_t: object_t { };

/// Records every child notification and WHICH child it named.
struct child_notice_recorder_t: object_t {
    std::vector<object_t*> children;

    bool event(event_t* e) override {
        if (auto* notice = dynamic_cast<child_event_t*>(e); notice != nullptr) {
            children.push_back(notice->child());
            e->accept();
            return true;
        }
        return object_t::event(e);
    }
};

/// Records every dynamic-property notification and WHICH property it named.
struct property_notice_recorder_t: object_t {
    std::vector<std::string> names;

    bool event(event_t* e) override {
        if (auto* notice = dynamic_cast<dynamic_property_change_event_t*>(e); notice != nullptr) {
            names.push_back(notice->property_name());
            e->accept();
            return true;
        }
        return object_t::event(e);
    }
};

/// The child named by notification @p index, or nullptr when fewer notifications arrived.
/// @note Out-of-range reads must not crash a FAILING assertion; the size checks carry the verdict.
object_t* notified_child(const child_notice_recorder_t& recorder, std::size_t index) {
    return index < recorder.children.size() ? recorder.children[index] : nullptr;
}

/// The property named by notification @p index, or "" when fewer notifications arrived.
std::string notified_property(const property_notice_recorder_t& recorder, std::size_t index) {
    return index < recorder.names.size() ? recorder.names[index] : std::string{ };
}

}  // namespace m29_event_compression_test

using namespace m29_event_compression_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    auto& dispatcher = event_dispatcher::instance();

    std::printf("== M29: event compression ==\n");

    // (1) Compression is OFF by default: three events of the same type and receiver are all
    //     delivered, in posting order, and nothing is counted as compressed.
    CHECK(!dispatcher.event_compression());
    CHECK(dispatcher.compressed_event_count() == 0);

    recorder_t first;
    dispatcher.post_event(&first, new probe_event_t(event_type_enum::USER, 1));
    dispatcher.post_event(&first, new probe_event_t(event_type_enum::USER, 2));
    dispatcher.post_event(&first, new probe_event_t(event_type_enum::USER, 3));
    CHECK(dispatcher.pending_event_count() == 3);
    dispatcher.send_posted_events();
    CHECK(first.records.size() == 3);
    CHECK(recorded_value(first, 0) == 1);
    CHECK(recorded_value(first, 1) == 2);
    CHECK(recorded_value(first, 2) == 3);
    CHECK(dispatcher.compressed_event_count() == 0);

    // (2) Compression ON: the same three posts collapse into ONE dispatch, and the surviving event
    //     is the FIRST object posted at the FIRST position (its payload is unchanged).
    dispatcher.set_event_compression(true);
    CHECK(dispatcher.event_compression());
    probe_event_t* const first_event = new probe_event_t(event_type_enum::USER, 11);
    probe_event_t* const second_event = new probe_event_t(event_type_enum::USER, 12);
    probe_event_t* const third_event = new probe_event_t(event_type_enum::USER, 13);
    dispatcher.post_event(&first, first_event);
    dispatcher.post_event(&first, second_event);
    dispatcher.post_event(&first, third_event);
    CHECK(dispatcher.pending_event_count() == 1);
    dispatcher.send_posted_events();
    CHECK(first.records.size() == 4);
    CHECK(recorded_event(first, 3) == first_event);   // the FIRST object survived
    CHECK(recorded_value(first, 3) == 11);
    CHECK(dispatcher.compressed_event_count() == 2);

    const std::size_t base_count = dispatcher.compressed_event_count();

    // (3) Different event types never merge, even though both use the default merge rule.
    dispatcher.post_event(&first, new probe_event_t(event_type_enum::USER, 21));
    dispatcher.post_event(&first, new probe_event_t(other_type, 22));
    CHECK(dispatcher.pending_event_count() == 2);
    dispatcher.send_posted_events();
    CHECK(first.records.size() == 6);
    CHECK(recorded_value(first, 4) == 21);
    CHECK(recorded_value(first, 5) == 22);
    CHECK(dispatcher.compressed_event_count() == base_count);

    // (4) Different receivers never merge: window A's event must not swallow window B's.
    recorder_t second;
    dispatcher.post_event(&first, new probe_event_t(event_type_enum::USER, 31));
    dispatcher.post_event(&second, new probe_event_t(event_type_enum::USER, 32));
    CHECK(dispatcher.pending_event_count() == 2);
    dispatcher.send_posted_events();
    CHECK(first.records.size() == 7);
    CHECK(recorded_value(first, 6) == 31);
    CHECK(second.records.size() == 1);
    CHECK(recorded_value(second, 0) == 32);
    CHECK(dispatcher.compressed_event_count() == base_count);

    // (5) Priority is not broken by compression: the first pending event of another priority is not
    //     a candidate, so the high-priority event stays separate and is delivered first.
    const std::size_t before_priority = first.records.size();
    dispatcher.post_event(&first, new probe_event_t(event_type_enum::USER, 41));
    dispatcher.post_event(&first, new probe_event_t(event_type_enum::USER, 42), event_priority_enum::HIGH);
    CHECK(dispatcher.pending_event_count() == 2);
    CHECK(dispatcher.compressed_event_count() == base_count);
    dispatcher.send_posted_events();
    CHECK(first.records.size() == before_priority + 2);
    CHECK(recorded_value(first, before_priority) == 42);      // high priority first
    CHECK(recorded_value(first, before_priority + 1) == 41);

    // (6) The default merge rule keeps the earlier event (the dispatcher's compression path is the
    //     same rule, checked here without a queue).
    event_t plain_first{ event_type_enum::USER };
    event_t plain_later{ event_type_enum::USER };
    CHECK(plain_first.merge_with(plain_later));

    // (7) A resize event keeps the earliest old size and takes the latest size.
    const extent_t old_size{ 100, 80 };
    const extent_t middle_size{ 200, 160 };
    const extent_t new_size{ 300, 240 };
    resize_event_t resize{ old_size, middle_size };
    CHECK(resize.type() == event_type_enum::RESIZE);
    CHECK(resize.old_size() == old_size);
    CHECK(resize.size() == middle_size);
    resize_event_t resize_later{ middle_size, new_size };
    CHECK(resize.merge_with(resize_later));
    CHECK(resize.old_size() == old_size);   // the earliest old size is kept
    CHECK(resize.size() == new_size);       // the latest size wins

    // (8) A move event follows the same rule for positions.
    const point_t old_position{ 10, 20 };
    const point_t middle_position{ 30, 40 };
    const point_t new_position{ 50, 60 };
    move_event_t move{ old_position, middle_position };
    CHECK(move.type() == event_type_enum::MOVE);
    CHECK(move.old_position() == old_position);
    CHECK(move.position() == middle_position);
    move_event_t move_later{ middle_position, new_position };
    CHECK(move.merge_with(move_later));
    CHECK(move.old_position() == old_position);
    CHECK(move.position() == new_position);

    // (9) An expose event has no payload: the inherited rule keeps the first pending one.
    expose_event_t expose;
    expose_event_t expose_later;
    CHECK(expose.type() == event_type_enum::EXPOSE);
    CHECK(expose.merge_with(expose_later));

    // (10) Merging is type-safe: a different concrete event is rejected and the payload is untouched.
    CHECK(!resize.merge_with(move));
    CHECK(resize.old_size() == old_size);
    CHECK(resize.size() == new_size);

    // (11) Concurrent posters: post_event() may run on other threads. Every post after the first
    //      must be absorbed by the single pending entry, with no lost or double-counted event.
    recorder_t concurrent;
    constexpr int thread_count = 4;
    constexpr int posts_per_thread = 25;
    const std::size_t before_concurrent = dispatcher.compressed_event_count();
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (int t = 0; t < thread_count; ++t) {
        workers.emplace_back([&concurrent, t] {
            for (int i = 0; i < posts_per_thread; ++i) {
                event_dispatcher::instance().post_event(
                    &concurrent, new probe_event_t(event_type_enum::USER, t * 1000 + i));
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    CHECK(dispatcher.pending_event_count() == 1);
    CHECK(dispatcher.compressed_event_count() - before_concurrent
        == static_cast<std::size_t>(thread_count * posts_per_thread - 1));
    dispatcher.send_posted_events();
    CHECK(concurrent.records.size() == 1);

    // The compression experiments above end here: the phase tests start from the default (off).
    dispatcher.set_event_compression(false);
    CHECK(!dispatcher.event_compression());

    // (12) No phase installed: the loop keeps the event-by-event path, and the phase accessors
    //      round-trip a policy without installing it.
    event_loop_t plain_loop;
    CHECK(!plain_loop.phase().has_value());
    recorder_t sequential;
    dispatcher.post_event(&sequential, new probe_event_t(event_type_enum::USER, 61));
    dispatcher.post_event(&sequential, new probe_event_t(event_type_enum::USER, 62));
    dispatcher.post_event(&sequential, new probe_event_t(event_type_enum::USER, 63));
    CHECK(plain_loop.process_events());
    CHECK(sequential.records.size() == 3);
    CHECK(recorded_value(sequential, 0) == 61);
    CHECK(recorded_value(sequential, 1) == 62);
    CHECK(recorded_value(sequential, 2) == 63);
    event_phase_t installed_phase;
    CHECK(installed_phase.budget == std::chrono::milliseconds(2));
    CHECK(installed_phase.max_events == 1024);
    CHECK(installed_phase.paint_last);
    CHECK(installed_phase.merge);
    plain_loop.set_phase(installed_phase);
    CHECK(plain_loop.phase().has_value());
    CHECK(plain_loop.phase()->merge);
    CHECK(plain_loop.phase()->max_events == 1024);
    plain_loop.set_phase(std::nullopt);
    CHECK(!plain_loop.phase().has_value());

    // (13) Phase + merge: a batch already queued is compacted and delivered once. The inherited
    //      merge rule keeps the EARLIEST object/payload; the absorbed events are counted.
    event_phase_t merging_phase;
    merging_phase.budget = std::chrono::milliseconds(1000);
    merging_phase.paint_last = false;
    CHECK(merging_phase.merge);
    recorder_t phase_merged;
    const std::size_t before_phase_merge = dispatcher.compressed_event_count();
    dispatcher.post_event(&phase_merged, new probe_event_t(event_type_enum::USER, 71));
    dispatcher.post_event(&phase_merged, new probe_event_t(event_type_enum::USER, 72));
    dispatcher.post_event(&phase_merged, new probe_event_t(event_type_enum::USER, 73));
    CHECK(dispatcher.pending_event_count() == 3);
    CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, merging_phase));
    CHECK(phase_merged.records.size() == 1);
    CHECK(recorded_value(phase_merged, 0) == 71);
    CHECK(dispatcher.compressed_event_count() - before_phase_merge == 2);
    CHECK(dispatcher.pending_event_count() == 0);
    CHECK(!dispatcher.event_compression());   // the phase restored the previous (off) setting

    // (14) Phase + merge=false: the same batch is dispatched one by one.
    event_phase_t sequential_phase;
    sequential_phase.budget = std::chrono::milliseconds(1000);
    sequential_phase.paint_last = false;
    sequential_phase.merge = false;
    recorder_t phase_sequential;
    dispatcher.post_event(&phase_sequential, new probe_event_t(event_type_enum::USER, 81));
    dispatcher.post_event(&phase_sequential, new probe_event_t(event_type_enum::USER, 82));
    dispatcher.post_event(&phase_sequential, new probe_event_t(event_type_enum::USER, 83));
    CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, sequential_phase));
    CHECK(phase_sequential.records.size() == 3);
    CHECK(recorded_value(phase_sequential, 0) == 81);
    CHECK(recorded_value(phase_sequential, 1) == 82);
    CHECK(recorded_value(phase_sequential, 2) == 83);
    CHECK(dispatcher.pending_event_count() == 0);

    // (15) paint_last: the PAINT pending when the dispatch pass starts is delivered LAST, while the
    //      other events keep their queue order.
    event_phase_t paints_last;
    paints_last.budget = std::chrono::milliseconds(1000);
    paints_last.merge = false;
    paints_last.paint_last = true;
    recorder_t paint_order;
    dispatcher.post_event(&paint_order, new probe_event_t(event_type_enum::USER, 91));
    dispatcher.post_event(&paint_order, new event_t(event_type_enum::PAINT));
    dispatcher.post_event(&paint_order, new probe_event_t(event_type_enum::USER, 92));
    CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, paints_last));
    CHECK(paint_order.records.size() == 3);
    CHECK(paint_order.records[0].type == event_type_enum::USER);
    CHECK(recorded_value(paint_order, 0) == 91);
    CHECK(paint_order.records[1].type == event_type_enum::USER);
    CHECK(recorded_value(paint_order, 1) == 92);
    CHECK(paint_order.records[2].type == event_type_enum::PAINT);

    // (16) A PAINT posted DURING the phase (update() from a handler) belongs to the next phase.
    paint_poster_t poster;
    poster.post_paint_on_user = true;
    dispatcher.post_event(&poster, new event_t(event_type_enum::USER));
    dispatcher.post_event(&poster, new event_t(event_type_enum::PAINT));
    CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, paints_last));
    CHECK(poster.paints == 1);                     // only the paint pending at phase start
    CHECK(dispatcher.pending_event_count() == 1);  // the handler's paint waits
    CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, paints_last));
    CHECK(poster.paints == 2);
    CHECK(dispatcher.pending_event_count() == 0);

    // (17) max_events bounds one phase; the rest stays queued and arrives in the following phases.
    event_phase_t bounded_phase;
    bounded_phase.budget = std::chrono::milliseconds(1000);
    bounded_phase.merge = false;
    bounded_phase.paint_last = false;
    bounded_phase.max_events = 2;
    recorder_t bounded;
    for (int i = 0; i < 5; ++i) {
        dispatcher.post_event(&bounded, new probe_event_t(event_type_enum::USER, 100 + i));
    }
    CHECK(dispatcher.pending_event_count() == 5);
    CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, bounded_phase));
    CHECK(bounded.records.size() == 2);
    CHECK(dispatcher.pending_event_count() == 3);
    CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, bounded_phase));
    CHECK(bounded.records.size() == 4);
    CHECK(dispatcher.pending_event_count() == 1);
    CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, bounded_phase));
    CHECK(bounded.records.size() == 5);
    CHECK(dispatcher.pending_event_count() == 0);
    CHECK(recorded_value(bounded, 4) == 104);

    // (18) The budget bounds a phase too: a slow handler consumes it and the flood is cut short.
    event_phase_t budget_phase;
    budget_phase.budget = std::chrono::milliseconds(2);
    budget_phase.merge = false;
    budget_phase.paint_last = false;
    budget_phase.max_events = 100;
    slow_recorder_t slow;
    for (int i = 0; i < 10; ++i) {
        dispatcher.post_event(&slow, new probe_event_t(event_type_enum::USER, 110 + i));
    }
    CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, budget_phase));
    CHECK(slow.received >= 1);
    CHECK(slow.received < 10);
    dispatcher.remove_posted_events(&slow, event_type_enum::USER);
    CHECK(dispatcher.pending_event_count() == 0);

    // (19) One-shot/work events refuse the merge even while the phase merges everything else:
    //      both timer ticks are delivered (not folded into one).
    timer_phase_receiver_t timing;
    timer_event_t timer_first(101);
    timer_event_t timer_second(102);
    CHECK(!timer_first.merge_with(timer_second));
    dispatcher.set_event_compression(true);
    dispatcher.post_event(&timing, new timer_event_t(101));
    dispatcher.post_event(&timing, new timer_event_t(102));
    CHECK(dispatcher.pending_event_count() == 2);   // post-time compression refuses too
    dispatcher.set_event_compression(false);
    CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, merging_phase));
    CHECK(timing.ticks == 2);
    CHECK(timing.first_id == 101);
    CHECK(timing.last_id == 102);

    // (20) Two queued meta calls execute twice: the meta call refuses the merge, so phase merging
    //      cannot silently drop an invocation (the blocking form would report CANCELLED instead).
    phase_signal_receiver_t signal_receiver;
    CHECK(signal_receiver.connect("ping() const", &signal_receiver, "on_ping()",
        connection_type_flag::QUEUED).is_valid());
    dispatcher.set_event_compression(true);
    signal_receiver.ping();
    signal_receiver.ping();
    CHECK(dispatcher.pending_event_count() == 2);   // post-time compression refuses too
    dispatcher.set_event_compression(false);
    CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, merging_phase));
    CHECK(signal_receiver.hits == 2);
    CHECK(dispatcher.pending_event_count() == 0);

    // (21) Deferred deletes are work too: both receivers are destroyed, and the event itself refuses
    //      a merge (a merged delete would leak the object it was meant to release).
    bool victim_a_destroyed = false;
    bool victim_b_destroyed = false;
    auto* victim_a = new phase_victim_t;
    victim_a->destroyed = &victim_a_destroyed;
    auto* victim_b = new phase_victim_t;
    victim_b->destroyed = &victim_b_destroyed;
    dispatcher.post_event(victim_a, new deferred_delete_event_t(0, 0));
    dispatcher.post_event(victim_b, new deferred_delete_event_t(0, 0));
    CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, merging_phase));
    CHECK(victim_a_destroyed);
    CHECK(victim_b_destroyed);
    deferred_delete_event_t deferred_first(0, 0);
    deferred_delete_event_t deferred_second(0, 0);
    CHECK(!deferred_first.merge_with(deferred_second));

    // (22) The phase restores the compression flag it found, and merge=false is phase-local: with a
    //      globally ON switch the three events posted from a handler are NOT compressed.
    dispatcher.set_event_compression(true);
    event_phase_t no_merge_phase;
    no_merge_phase.budget = std::chrono::milliseconds(1000);
    no_merge_phase.merge = false;
    no_merge_phase.paint_last = false;
    fanout_t fanout;
    recorder_t fanout_target;
    fanout.target = &fanout_target;
    dispatcher.post_event(&fanout, new event_t(event_type_enum::USER));
    CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, no_merge_phase));
    CHECK(fanout.posted == 3);
    CHECK(dispatcher.pending_event_count() == 3);   // not 1: the phase suppressed compression
    CHECK(dispatcher.event_compression());          // restored to the previous ON
    dispatcher.set_event_compression(false);
    dispatcher.remove_posted_events(&fanout_target, event_type_enum::USER);
    CHECK(dispatcher.pending_event_count() == 0);

    // (23) event_loop_t::exec() runs the phase path when a phase is installed: the three queued
    //      user events merge into one, then the QUIT event ends the loop.
    event_loop_t phased_loop;
    event_phase_t exec_phase;
    exec_phase.budget = std::chrono::milliseconds(1000);
    exec_phase.merge = true;
    exec_phase.paint_last = false;
    phased_loop.set_phase(exec_phase);
    recorder_t exec_receiver;
    dispatcher.post_event(&exec_receiver, new probe_event_t(event_type_enum::USER, 131));
    dispatcher.post_event(&exec_receiver, new probe_event_t(event_type_enum::USER, 132));
    dispatcher.post_event(&exec_receiver, new probe_event_t(event_type_enum::USER, 133));
    dispatcher.post_event(&phased_loop, new event_t(event_type_enum::QUIT));
    CHECK(phased_loop.exec() == 0);
    CHECK(exec_receiver.records.size() == 1);
    CHECK(recorded_value(exec_receiver, 0) == 131);
    CHECK(dispatcher.pending_event_count() == 0);
    phased_loop.set_phase(std::nullopt);

    // (24) process_events(flags, maximum_time) also takes the phase path when one is installed.
    event_loop_t timed_loop;
    event_phase_t timed_phase;
    timed_phase.budget = std::chrono::milliseconds(1000);
    timed_phase.merge = false;
    timed_phase.paint_last = false;
    timed_phase.max_events = 1;
    timed_loop.set_phase(timed_phase);
    recorder_t timed_receiver;
    dispatcher.post_event(&timed_receiver, new probe_event_t(event_type_enum::USER, 141));
    dispatcher.post_event(&timed_receiver, new probe_event_t(event_type_enum::USER, 142));
    dispatcher.post_event(&timed_receiver, new probe_event_t(event_type_enum::USER, 143));
    timed_loop.process_events(process_events_type_flag::ALL_EVENTS, 0);
    CHECK(timed_receiver.records.size() == 1);
    CHECK(dispatcher.pending_event_count() == 2);
    dispatcher.remove_posted_events(&timed_receiver, event_type_enum::USER);
    CHECK(dispatcher.pending_event_count() == 0);
    timed_loop.set_phase(std::nullopt);

    // (25) The application forwards its phase into the loop exec() creates, and a nested
    //      core_application::process_events(flags, maximum_time) goes through that same loop object
    //      (so paint_last applies to the nested call). Queue PAINT before USER, then dispatch from a
    //      slot: a nested call that ignored the loop would deliver PAINT first.
    event_phase_t routed_phase;
    routed_phase.budget = std::chrono::milliseconds(1000);
    routed_phase.merge = false;
    routed_phase.paint_last = true;
    app.set_event_phase(routed_phase);
    CHECK(app.phase().has_value());
    recorder_t nested_target;
    nested_process_probe_t nested_probe;
    nested_probe.target = &nested_target;
    core_application::post_event(&nested_probe, new event_t(event_type_enum::USER));
    CHECK(core_application::exec() == 0);
    CHECK(nested_target.records.size() == 2);
    CHECK(nested_target.records[0].type == event_type_enum::USER);
    CHECK(nested_target.records[1].type == event_type_enum::PAINT);
    app.set_event_phase(std::nullopt);
    CHECK(!app.phase().has_value());

    // (26) Structural and property notifications are FACTS, not state: a phase may fold a STATE
    //      (the same receiver + type + priority), but folding a NOTIFICATION deletes the observation
    //      instead of updating a value. Three child-added events naming three DIFFERENT children and
    //      three dynamic-property events naming three DIFFERENT properties must therefore all be
    //      delivered, while three mouse-move events (state) still collapse into one.
    //
    //      The counter is the witness that the events really were merge CANDIDATES: it stays flat for
    //      the notifications (nothing was absorbed) and grows by two for the moves. Without that, a
    //      test that merely counts deliveries could pass because the events never compared equal.
    {
        CHECK(dispatcher.pending_event_count() == 0);

        // (26.1) The unit-level refusal, next to the timer / meta-call / deferred-delete precedents.
        child_object_t child_head_object;
        child_object_t child_tail_object;
        child_event_t child_head(event_type_enum::CHILD_ADDED, &child_head_object);
        child_event_t child_tail(event_type_enum::CHILD_ADDED, &child_tail_object);
        CHECK(!child_head.merge_with(child_tail));
        dynamic_property_change_event_t property_head("caption");
        dynamic_property_change_event_t property_tail("enabled");
        CHECK(!property_head.merge_with(property_tail));

        // (26.2) Three child notifications, three children: all three arrive, in order.
        child_notice_recorder_t child_observer;
        child_object_t child_a;
        child_object_t child_b;
        child_object_t child_c;
        const std::size_t child_compressed_before = dispatcher.compressed_event_count();
        dispatcher.set_event_compression(true);
        dispatcher.post_event(&child_observer, new child_event_t(event_type_enum::CHILD_ADDED, &child_a));
        dispatcher.post_event(&child_observer, new child_event_t(event_type_enum::CHILD_ADDED, &child_b));
        dispatcher.post_event(&child_observer, new child_event_t(event_type_enum::CHILD_ADDED, &child_c));
        CHECK(dispatcher.pending_event_count() == 3);           // nothing was absorbed at post time
        CHECK(dispatcher.compressed_event_count() == child_compressed_before);
        dispatcher.set_event_compression(false);
        CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, merging_phase));
        CHECK(child_observer.children.size() == 3);
        CHECK(notified_child(child_observer, 0) == &child_a);
        CHECK(notified_child(child_observer, 1) == &child_b);
        CHECK(notified_child(child_observer, 2) == &child_c);

        // (26.3) Three property notifications, three properties: same rule.
        property_notice_recorder_t property_observer;
        const std::size_t property_compressed_before = dispatcher.compressed_event_count();
        dispatcher.set_event_compression(true);
        dispatcher.post_event(&property_observer, new dynamic_property_change_event_t("caption"));
        dispatcher.post_event(&property_observer, new dynamic_property_change_event_t("enabled"));
        dispatcher.post_event(&property_observer, new dynamic_property_change_event_t("tooltip"));
        CHECK(dispatcher.pending_event_count() == 3);
        CHECK(dispatcher.compressed_event_count() == property_compressed_before);
        dispatcher.set_event_compression(false);
        CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, merging_phase));
        CHECK(property_observer.names.size() == 3);
        CHECK(notified_property(property_observer, 0) == "caption");
        CHECK(notified_property(property_observer, 1) == "enabled");
        CHECK(notified_property(property_observer, 2) == "tooltip");

        // (26.4) The contrast that keeps the two checks above from being vacuous: the SAME receiver
        //        and phase, with a state event, still merges - the pointer position keeps only the
        //        last move, and the counter shows the two absorbed entries.
        recorder_t pointer_observer;
        const std::size_t state_compressed_before = dispatcher.compressed_event_count();
        dispatcher.set_event_compression(true);
        dispatcher.post_event(&pointer_observer, new event_t(event_type_enum::MOUSE_MOVE));
        dispatcher.post_event(&pointer_observer, new event_t(event_type_enum::MOUSE_MOVE));
        dispatcher.post_event(&pointer_observer, new event_t(event_type_enum::MOUSE_MOVE));
        CHECK(dispatcher.pending_event_count() == 1);
        CHECK(dispatcher.compressed_event_count() == state_compressed_before + 2);
        dispatcher.set_event_compression(false);
        CHECK(dispatcher.process_phase(process_events_type_flag::ALL_EVENTS, merging_phase));
        CHECK(pointer_observer.records.size() == 1);
        CHECK(pointer_observer.records.size() == 1
            && pointer_observer.records[0].type == event_type_enum::MOUSE_MOVE);
        CHECK(dispatcher.pending_event_count() == 0);
    }

    dispatcher.set_event_compression(false);
    CHECK(!dispatcher.event_compression());

    std::printf(g_failures == 0 ? "  M29: all checks passed\n" : "  M29: %d check(s) failed\n",
        g_failures);
    return g_failures == 0 ? 0 : 1;
}
