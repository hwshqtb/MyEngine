// M17 Phase A/B: type-erased property (property_t over any_t) dependency tracking, the per-thread
// deferred binding round (public flush_bindings()), the read-time recompute of a dirty value and the
// end-of-round flush in process_events.
// Phase C/D/E: binding handles (take/replace/sticky), the per-binding error channel with source
// locations, and property update groups.

#include <meta>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <hwshqtb/metaobject/annotation.h>
#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_property_t.h>
#include <hwshqtb/metaobject/meta_type_t.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/property_t.h>

using namespace hwshqtb::metaobject;

namespace m17_binding_test {

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

/// One captured log record (the log-test pattern).
struct record_t {
    std::string category;
    log_level_enum level = log_level_enum::DEBUG;
    std::string message;
};

std::vector<record_t>& records() {
    static std::vector<record_t> captured;
    return captured;
}

/// A binding whose closure counts its evaluations; it can also re-enter flush_bindings() and
/// invalidate the next binding. It is an ordinary owning property: the install evaluation is not a
/// scheduled recompute, so the counter starts at zero once set_binding() has returned.
struct counting_binding_t {
    counting_binding_t() {
        value.set_binding([this]() {
            ++recomputes;
            if (reentrant_flushes != nullptr) {
                ++*reentrant_flushes;
                flush_bindings();                       // the re-entrancy guard must return immediately
            }
            if (mark_next != nullptr) {
                counting_binding_t* next = mark_next;
                mark_next = nullptr;
                next->value.invalidate();               // an external source of next changed
            }
            return any_t(0);
        });
        recomputes = 0;
    }

    property_t value{ any_t(0) };
    int recomputes = 0;
    int* reentrant_flushes = nullptr;
    counting_binding_t* mark_next = nullptr;
};

// ---------------------------------------------------------------------------
// Theme G (first half): an object-attached property reached through the meta layer. The property is
// an ordinary data-backed property with a notifier; property_t::from() is the one way to reach it as
// a property handle. The notify subscription the deleted meta-layer hook used to install is written
// by the class itself — the approved design, and the handle it returns must be kept (the handle IS
// the registration).
// ---------------------------------------------------------------------------

/// tst_qproperty.cpp's MyQObject shape, reduced to one notifiable property.
struct bindable_object_t : object_t {
    OBJECT
public:
    /// The class's own property member -> the property's notifier signal. One line in the
    /// constructor, exactly the design the accessor layer was replaced with.
    bindable_object_t() {
        _value_notifier =
            value_data.on_value_changed([this] { value_changed(value_data.value().get<int>()); });
    }

    [[= make_property("value", "set_value", "value_changed")]]
    int value() const { return value_data.value().get<int>(); }

    void set_value(int v) { value_data.set_value(any_t(v)); }

    SIGNAL(void value_changed(int new_value) const)

    /// A property with no notifier at all: property_t::from() must refuse it.
    [[= make_property("read_only")]]
    int read_only() const { return 5; }

    /// A property that NAMES a notifier nothing declares: the run-time chain resolution fails, so the
    /// property cannot notify, from() refuses it and the miss logs one Critical.
    [[= make_property("dangling", "", "no_such_notifier")]]
    int dangling() const { return 0; }

    property_t value_data{ any_t(0) };

private:
    /// What value_data.on_value_changed() returned; destroying it unregisters the subscription.
    property_change_handler_t _value_notifier;
};
META_OBJECT(bindable_object_t);

/// The property is DECLARED BY THE BASE class; property_t::from() is handed the DERIVED object, so the
/// resolution has to walk the meta-object chain (the old "inherited accessor" case, now an inherited
/// property).
struct bindable_base_t : object_t {
    OBJECT
public:
    bindable_base_t() {
        _inherited_notifier = inherited_data.on_value_changed(
            [this] { inherited_changed(inherited_data.value().get<int>()); });
    }

    [[= make_property("inherited", "set_inherited", "inherited_changed")]]
    int inherited() const { return inherited_data.value().get<int>(); }

    void set_inherited(int v) { inherited_data.set_value(any_t(v)); }

    SIGNAL(void inherited_changed(int new_value) const)

    property_t inherited_data{ any_t(0) };

private:
    property_change_handler_t _inherited_notifier;
};
META_OBJECT(bindable_base_t);

struct bindable_derived_t : bindable_base_t {
    OBJECT
};
META_OBJECT(bindable_derived_t);

}  // namespace m17_binding_test

using namespace m17_binding_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);

    install_log_handler([](log_level_enum level, const log_context_t& context, std::string_view message) {
        records().push_back(record_t{ std::string(context.category), level, std::string(message) });
        return true;                                // consumed: the default sink stays silent
    });

    std::printf("== M17: property binding (core) ==\n");

    // (1) A binding is evaluated immediately and its result is cached.
    {
        property_t b{ any_t(0) };
        CHECK(b.value().get<int>() == 0);
        CHECK(!b.has_binding());
        b.set_binding([]() { return any_t(42); });
        CHECK(b.has_binding());
        CHECK(b.value().get<int>() == 42);
    }

    // (1b) A throwing closure keeps the old value, installs no binding and logs Critical.
    {
        property_t throwing{ any_t(5) };
        const std::size_t before = records().size();
        throwing.set_binding([]() -> any_t { throw std::runtime_error("boom"); });
        CHECK(!throwing.has_binding());
        CHECK(throwing.value().get<int>() == 5);
        CHECK(records().size() == before + 1);
        CHECK(records().back().category == "metaobject.connection");
        CHECK(records().back().level == log_level_enum::CRITICAL);
        CHECK(records().back().message.find("binding evaluation failed") != std::string::npos);
    }

    // (2) Dependency chain a -> b -> c: reading a dependant brings the chain up to date at once
    //     (Theme A: a read recomputes a dirty binding; no round is needed).
    {
        property_t a{ any_t(1) };
        property_t b{ any_t(0) };
        property_t c{ any_t(0) };
        int b_evaluations = 0;
        int c_evaluations = 0;
        b.set_binding([&]() { ++b_evaluations; return any_t(a.value().get<int>() * 2); });
        c.set_binding([&]() { ++c_evaluations; return any_t(b.value().get<int>() + 1); });
        CHECK(b.value().get<int>() == 2);
        CHECK(c.value().get<int>() == 3);

        a.set_value(any_t(5));
        CHECK(b.value().get<int>() == 10);          // read-time recompute, no flush needed
        CHECK(c.value().get<int>() == 11);
        CHECK(b_evaluations == 2 && c_evaluations == 2);   // each read evaluated once
        core_application::process_events();         // nothing is left for the round-end flush
        CHECK(b_evaluations == 2 && c_evaluations == 2);   // ... and the round evaluated neither again
        CHECK(b.value().get<int>() == 10);
        CHECK(c.value().get<int>() == 11);
    }

    // (3) Coalescing: three changes before one round cost exactly one recompute.
    {
        property_t a{ any_t(0) };
        property_t b{ any_t(0) };
        int recomputes = 0;
        b.set_binding([&]() { ++recomputes; return any_t(a.value().get<int>() + 1); });
        CHECK(recomputes == 1);                      // set_binding evaluates immediately
        recomputes = 0;

        a.set_value(any_t(1));
        a.set_value(any_t(2));
        a.set_value(any_t(3));
        CHECK(recomputes == 0);                      // three writes, nothing evaluated yet
        core_application::process_events();
        CHECK(recomputes == 1);                      // one coalesced recompute for the whole round
        CHECK(b.value().get<int>() == 4);
        flush_bindings();                            // the round has nothing left
        CHECK(recomputes == 1);
    }

    // (4) set_value breaks the binding: later dependency changes no longer affect (or recompute) it.
    {
        property_t a{ any_t(1) };
        property_t b{ any_t(0) };
        int recomputes = 0;
        b.set_binding([&]() { ++recomputes; return any_t(a.value().get<int>() * 10); });
        CHECK(b.value().get<int>() == 10);
        recomputes = 0;

        b.set_value(any_t(7));
        CHECK(!b.has_binding());
        CHECK(b.value().get<int>() == 7);
        flush_bindings();                            // the write queued no recompute
        CHECK(recomputes == 0);                      // ... so the round evaluated nothing

        a.set_value(any_t(2));
        flush_bindings();                            // b was detached from a
        CHECK(recomputes == 0);                      // ... and a's write cannot reach the removed closure
        core_application::process_events();
        CHECK(recomputes == 0);
        CHECK(b.value().get<int>() == 7);
    }

    // (5) Cycle a <-> b: the flush stops at the budget, logs one Critical and ends clean.
    {
        property_t a{ any_t(0) };
        property_t b{ any_t(0) };
        int a_evaluations = 0;
        int b_evaluations = 0;
        a.set_binding([&]() { ++a_evaluations; return any_t(b.value().get<int>() + 1); });
        b.set_binding([&]() { ++b_evaluations; return any_t(a.value().get<int>() + 1); });

        // Installing b's binding notifies its dependant a if set_binding notifies on install;
        // invalidate a explicitly so the scenario does not depend on that detail.
        a.invalidate();

        const std::size_t before = records().size();
        core_application::process_events();

        bool cycle_logged = false;
        for (std::size_t i = before; i < records().size(); ++i) {
            if (records()[i].category == "metaobject.connection"
                && records()[i].level == log_level_enum::CRITICAL
                && records()[i].message.find("binding dependency cycle detected") != std::string::npos) {
                cycle_logged = true;
            }
        }
        CHECK(cycle_logged);
        CHECK(records().size() == before + 1);       // exactly one diagnostic, then the round stops
        const int a_after_cycle = a_evaluations;
        const int b_after_cycle = b_evaluations;
        flush_bindings();                            // the round ended clean
        CHECK(a_evaluations == a_after_cycle && b_evaluations == b_after_cycle);
        CHECK(records().size() == before + 1);       // ... and a further round logged nothing

        // A cycling graph keeps raw dependency pointers in both directions, and each closure still
        // names the other property: break the cycle before the properties die. (The library's own
        // registrations are safe either way since Theme M; the closures' captures are the caller's.)
        a.clear_binding();
        b.clear_binding();
    }

    // (6) Two threads: each has its own scheduler and its own properties; no cross-talk.
    {
        std::atomic<int> thread_failures{ 0 };
        std::atomic<int> total_recomputes{ 0 };

        auto worker = [&](int seed) {
            property_t a{ any_t(seed) };
            property_t b{ any_t(0) };
            int recomputes = 0;
            b.set_binding([&]() { ++recomputes; return any_t(a.value().get<int>() * 10); });
            a.set_value(any_t(seed + 1));
            if (recomputes != 1) {                   // batched, not evaluated yet
                ++thread_failures;
            }
            flush_bindings();
            if (recomputes != 2) {                   // exactly one recompute in the round
                ++thread_failures;
            }
            if (b.value().get<int>() != (seed + 1) * 10) {
                ++thread_failures;
            }
            if (recomputes != 2) {                   // set_binding + one coalesced recompute
                ++thread_failures;
            }
            total_recomputes += recomputes;
        };

        std::thread first(worker, 11);
        std::thread second(worker, 22);
        first.join();
        second.join();

        CHECK(thread_failures.load() == 0);
        CHECK(total_recomputes.load() == 4);
    }

    // (7) flush_bindings() contract: an empty round evaluates nothing, a queued binding is
    //     recomputed once, a re-entrant call returns immediately, and a binding invalidated during
    //     the round is absorbed by the same round.
    {
        counting_binding_t binding;
        flush_bindings();
        CHECK(binding.recomputes == 0);              // an empty round evaluates nothing

        property_t nothing;                          // an invalid handle has no core to queue
        nothing.invalidate();                        // the public no-op
        flush_bindings();
        CHECK(binding.recomputes == 0);              // nothing was queued

        binding.value.invalidate();
        CHECK(binding.recomputes == 0);              // queued, not evaluated
        binding.value.invalidate();                  // already queued: de-duplicated
        CHECK(binding.recomputes == 0);              // still one pending recompute

        flush_bindings();
        CHECK(binding.recomputes == 1);              // the round evaluated the closure once
        flush_bindings();
        CHECK(binding.recomputes == 1);              // nothing left for another round

        int reentrant_flushes = 0;
        binding.reentrant_flushes = &reentrant_flushes;
        binding.value.invalidate();
        flush_bindings();
        CHECK(reentrant_flushes == 1);               // flush_bindings() was re-entered and returned at once
        CHECK(binding.recomputes == 2);

        counting_binding_t second;
        binding.reentrant_flushes = nullptr;
        binding.mark_next = &second;
        binding.value.invalidate();
        flush_bindings();                            // second is recomputed in the same round
        CHECK(binding.recomputes == 3);
        CHECK(second.recomputes == 1);
        const int second_after_round = second.recomputes;
        flush_bindings();
        CHECK(second.recomputes == second_after_round);   // the round ended clean
    }

    // (8) Theme A: a read recomputes a dirty binding at once and consumes its queued entry, so the
    //     end-of-round flush does not evaluate it a second time.
    {
        flush_bindings();
        property_t source{ any_t(1) };
        property_t bound{ any_t(0) };
        int recomputes = 0;
        bound.set_binding([&]() {
            ++recomputes;
            return any_t(source.value().get<int>() * 3);
        });
        CHECK(bound.value().get<int>() == 3);
        CHECK(recomputes == 1);

        source.set_value(any_t(2));
        CHECK(recomputes == 1);                      // queued, not evaluated yet
        CHECK(bound.value().get<int>() == 6);        // the read evaluated it: no flush required
        CHECK(recomputes == 2);
        flush_bindings();                            // the read consumed the entry
        CHECK(recomputes == 2);                      // ... so the round did not evaluate it again
        core_application::process_events();
        CHECK(recomputes == 2);
        CHECK(bound.value().get<int>() == 6);
    }

    // (8b) Theme A re-entrancy: a binding that reads its own value must not recurse. The read logs
    //      one Critical diagnostic and hands out the cached value.
    {
        property_t self{ any_t(0) };
        int evaluations = 0;
        const std::size_t before = records().size();
        self.set_binding([&]() { ++evaluations; return any_t(self.value().get<int>() + 1); });
        CHECK(self.value().get<int>() == 2);         // 1 from the install, + 1 from the read-time pass
        CHECK(records().size() == before + 1);
        CHECK(records().back().category == "metaobject.connection");
        CHECK(records().back().level == log_level_enum::CRITICAL);
        CHECK(records().back().message.find("binding cycle detected during evaluation") != std::string::npos);
        const int evaluations_before_clear = evaluations;
        self.clear_binding();                        // break the self-observing cycle
        flush_bindings();
        CHECK(evaluations == evaluations_before_clear);   // the removed closure never runs
    }

    // (8c) During a round the scheduler owns the evaluation: a binding that reads a still-queued peer
    //      sees the round's cache, and the peer's recompute re-marks it, so the round's queue
    //      extension still converges before the round ends (the peer is evaluated once by the round).
    {
        flush_bindings();
        property_t source{ any_t(0) };
        property_t peer{ any_t(0) };
        property_t reader{ any_t(0) };
        int reader_recomputes = 0;
        int peer_recomputes = 0;
        // Bind reader while peer still has no binding, so source's observer list (and therefore the
        // round queue) reaches reader before peer.
        reader.set_binding([&]() {
            ++reader_recomputes;
            return any_t(peer.value().get<int>() + source.value().get<int>());
        });
        peer.set_binding([&]() {
            ++peer_recomputes;
            return any_t(source.value().get<int>() * 2);
        });
        CHECK(reader.value().get<int>() == 0);
        CHECK(peer_recomputes == 1);

        source.set_value(any_t(1));
        CHECK(reader_recomputes == 1);               // the change evaluated neither binding
        CHECK(peer_recomputes == 1);                 // ... both direct readers are queued for the round
        core_application::process_events();
        CHECK(peer_recomputes == 2);                 // the round evaluated peer exactly once
        CHECK(reader.value().get<int>() == 3);       // re-queued after peer's recompute marked it
    }

    // (9) Theme A: an unread property is still recomputed once by the end-of-round flush.
    {
        property_t source{ any_t(1) };
        property_t unread{ any_t(0) };
        int recomputes = 0;
        unread.set_binding([&]() {
            ++recomputes;
            return any_t(source.value().get<int>() + 1);
        });
        CHECK(unread.value().get<int>() == 2);
        CHECK(recomputes == 1);

        source.set_value(any_t(4));
        CHECK(source.value().get<int>() == 4);       // the write is visible immediately...
        CHECK(recomputes == 1);                      // ... but the binding is still batched: nothing read it
        core_application::process_events();
        CHECK(recomputes == 2);
        CHECK(unread.value().get<int>() == 5);
        flush_bindings();                            // the round consumed the entry
        CHECK(recomputes == 2);
    }

    // (10) Theme F: an equal write neither assigns nor notifies, but a write still removes the
    //      binding first (Qt's setValue() order).
    {
        property_t source{ any_t(1) };
        property_t dependant{ any_t(0) };
        int recomputes = 0;
        dependant.set_binding([&]() {
            ++recomputes;
            return any_t(source.value().get<int>());
        });
        CHECK(dependant.value().get<int>() == 1);
        CHECK(recomputes == 1);

        source.set_value(any_t(1));                  // equal value: no notification at all
        CHECK(recomputes == 1);                      // the dependant was not marked dirty
        flush_bindings();                            // the equal write queued no round work
        CHECK(recomputes == 1);
        CHECK(dependant.value().get<int>() == 1);
        CHECK(recomputes == 1);

        // A bound property written with its current value loses its binding and notifies nobody.
        property_t bound{ any_t(0) };
        int bound_evaluations = 0;
        bound.set_binding([&]() {
            ++bound_evaluations;
            return any_t(source.value().get<int>() + 1);
        });
        CHECK(bound.has_binding());
        CHECK(bound.value().get<int>() == 2);
        bound.set_value(any_t(2));
        CHECK(!bound.has_binding());
        CHECK(bound.value().get<int>() == 2);
        flush_bindings();                            // the detached closure is not queued
        CHECK(bound_evaluations == 1);               // ... the round evaluated nothing
    }

    // (11) Theme K: the value read through the erased handle. The typed facade's operator-> /
    //      operator* are gone with the facade; the value is read back from the any_t and the pointer
    //      is dereferenced by the caller. There is no implicit conversion to T either.
    {
        int target = 7;
        property_t pointer{ any_t(&target) };
        CHECK(pointer.value().get<int*>() == &target);
        CHECK(pointer.value().get<int*>() == &target);
        CHECK(*pointer.value().get<int*>() == 7);

        struct holder {
            int value = 11;
        };
        holder instance;
        property_t holder_pointer{ any_t(&instance) };
        CHECK(holder_pointer.value().get<holder*>()->value == 11);

        property_t number{ any_t(9) };
        CHECK(number.value().get<int>() == 9);    // the stored value, read back
        CHECK(number.value().get<int>() == 9);    // ... and the plain read agrees
        // A non-pointer property has no member access and no dereference at all: the erased handle
        // only offers the value, so the old operator-overload assertions became value reads.
    }

    // (12) Theme M: a destroyed dependency unregisters from its dependants, which keep the value they
    //      last computed and stay readable; a destroyed dependant leaves the queue clean.
    {
        property_t flag{ any_t(1) };
        auto* dependency = new property_t{ any_t(100) };
        property_t dependant{ any_t(0) };
        int dependant_evaluations = 0;
        dependant.set_binding([&]() {
            ++dependant_evaluations;
            const int base = flag.value().get<int>();
            return any_t(base + (dependency != nullptr ? dependency->value().get<int>() : 0));
        });
        CHECK(dependant.value().get<int>() == 101);
        CHECK(dependant_evaluations == 1);

        // Destroy the dependency while the dependant is invalidated and queued.
        flag.set_value(any_t(2));
        CHECK(dependant_evaluations == 1);           // queued, not evaluated yet
        delete dependency;
        dependency = nullptr;
        CHECK(dependant_evaluations == 1);           // the dying dependency left no work
        CHECK(dependant.value().get<int>() == 101);  // frozen value: no freed storage is dereferenced
        core_application::process_events();
        CHECK(dependant_evaluations == 1);           // the flush did not evaluate the closure
        CHECK(dependant.value().get<int>() == 101);

        // The surviving dependency still invalidates the binding, which takes the other branch.
        flag.set_value(any_t(3));
        CHECK(dependant.value().get<int>() == 3);
        dependant.clear_binding();                   // stop observing flag for the next count

        // Destroying an invalidated-but-unread dependant must not leave a dangling queue entry.
        auto* unread = new property_t{ any_t(0) };
        int unread_evaluations = 0;
        unread->set_binding([&]() {
            ++unread_evaluations;
            return any_t(flag.value().get<int>());
        });
        CHECK(unread->value().get<int>() == 3);
        flag.set_value(any_t(4));
        CHECK(unread_evaluations == 1);              // queued, not evaluated yet
        delete unread;
        core_application::process_events();
        CHECK(unread_evaluations == 1);              // the round never reached the destroyed closure
        CHECK(flag.value().get<int>() == 4);
    }

    // (13a) Eager propagation: a 4-link chain must be dirty all the way down, so reading the tail
    //       directly (no intermediate reads, no flush) pulls the whole chain. This is the regression
    //       the "one dirty level is enough" model got wrong.
    {
        flush_bindings();
        property_t a{ any_t(1) };
        property_t b{ any_t(0) };
        property_t c{ any_t(0) };
        property_t d{ any_t(0) };
        int b_evaluations = 0;
        int c_evaluations = 0;
        int d_evaluations = 0;
        b.set_binding([&]() { ++b_evaluations; return any_t(a.value().get<int>() + 1); });
        c.set_binding([&]() { ++c_evaluations; return any_t(b.value().get<int>() + 1); });
        d.set_binding([&]() { ++d_evaluations; return any_t(c.value().get<int>() + 1); });
        CHECK(b.value().get<int>() == 2);
        CHECK(d.value().get<int>() == 4);

        a.set_value(any_t(10));
        CHECK(b_evaluations == 1 && c_evaluations == 1 && d_evaluations == 1);   // all marked, none evaluated
        CHECK(d.value().get<int>() == 13);           // one tail read evaluates b and c as well
        CHECK(b_evaluations == 2 && c_evaluations == 2 && d_evaluations == 2);   // ... each evaluated exactly once
        CHECK(b.value().get<int>() == 11);
        CHECK(c.value().get<int>() == 12);           // consumes c's re-mark from that evaluation
        CHECK(c_evaluations == 3);                   // the read consumed c's queued re-mark by evaluating once
        CHECK(d_evaluations == 2);                   // d was re-marked while it was evaluating
        core_application::process_events();
        const int d_after_round = d_evaluations;
        flush_bindings();
        CHECK(d_evaluations == d_after_round);       // the round ended clean
        CHECK(d.value().get<int>() == 13);

        a.set_value(any_t(100));
        CHECK(d.value().get<int>() == 103);          // second change, tail read again
        CHECK(b.value().get<int>() == 101);
        CHECK(c.value().get<int>() == 102);
        core_application::process_events();
        const int d_after_second_round = d_evaluations;
        flush_bindings();
        CHECK(d_evaluations == d_after_second_round);   // the second round ended clean
        CHECK(d.value().get<int>() == 103);
    }

    // (13b) Diamond with depth: a -> b, a -> c, b -> d, c -> d. One change at the head marks the
    //       whole diamond; a tail read converges both arms, and the round drains the re-mark.
    {
        flush_bindings();
        property_t a{ any_t(0) };
        property_t b{ any_t(0) };
        property_t c{ any_t(0) };
        property_t d{ any_t(0) };
        int b_evaluations = 0;
        int c_evaluations = 0;
        int d_evaluations = 0;
        b.set_binding([&]() { ++b_evaluations; return any_t(a.value().get<int>() + 1); });
        c.set_binding([&]() { ++c_evaluations; return any_t(a.value().get<int>() + 2); });
        d.set_binding(
            [&]() { ++d_evaluations; return any_t(b.value().get<int>() + c.value().get<int>()); });
        CHECK(d.value().get<int>() == 3);
        const int d_installed = d_evaluations;
        flush_bindings();
        CHECK(d_evaluations == d_installed);         // the installs left no queued work

        a.set_value(any_t(10));
        CHECK(b_evaluations == 1 && c_evaluations == 1 && d_evaluations == 1);   // all marked, none evaluated
        CHECK(d.value().get<int>() == 23);           // b = 11 and c = 12 through one tail read
        CHECK(b.value().get<int>() == 11);
        CHECK(c.value().get<int>() == 12);
        core_application::process_events();
        const int d_after_round = d_evaluations;
        flush_bindings();
        CHECK(d_evaluations == d_after_round);       // the round ended clean
        CHECK(d.value().get<int>() == 23);
    }

    // (13c) A feedback loop still terminates: the eager mark stops at an already-dirty node, and a
    //       read that re-enters the running evaluation logs exactly one Critical, then returns the
    //       cached value instead of recursing.
    {
        flush_bindings();
        property_t first{ any_t(0) };
        property_t second{ any_t(0) };
        int first_evaluations = 0;
        int second_evaluations = 0;
        first.set_binding([&]() { ++first_evaluations; return any_t(second.value().get<int>() + 1); });
        second.set_binding([&]() { ++second_evaluations; return any_t(first.value().get<int>() + 1); });

        const std::size_t before = records().size();
        CHECK(first.value().get<int>() == 3);        // second's pass breaks at first's evaluation
        CHECK(records().size() == before + 1);       // exactly one diagnostic: propagation did not loop
        CHECK(records().back().category == "metaobject.connection");
        CHECK(records().back().level == log_level_enum::CRITICAL);
        CHECK(records().back().message.find("binding cycle detected during evaluation") != std::string::npos);
        const int evaluations_after_read = first_evaluations + second_evaluations;
        flush_bindings();
        CHECK(first_evaluations + second_evaluations > evaluations_after_read);   // the read left work queued

        first.clear_binding();
        second.clear_binding();
        const int evaluations_after_clear = first_evaluations + second_evaluations;
        flush_bindings();
        CHECK(first_evaluations + second_evaluations == evaluations_after_clear);   // clearing dropped the queued work
    }

    // (13d) A plain, unbound link in the middle propagates too: writing it notifies its dependants
    //       directly and the eager mark reaches the tail.
    {
        flush_bindings();
        property_t source{ any_t(1) };
        property_t head{ any_t(0) };
        head.set_binding([&]() { return any_t(source.value().get<int>() * 10); });
        property_t middle{ any_t(5) };               // no binding: a plain cached value
        property_t body{ any_t(0) };
        body.set_binding(
            [&]() { return any_t(head.value().get<int>() + middle.value().get<int>()); });
        property_t tail{ any_t(0) };
        int tail_evaluations = 0;
        tail.set_binding([&]() { ++tail_evaluations; return any_t(body.value().get<int>() + 1); });
        CHECK(tail.value().get<int>() == 16);        // 10 + 5 + 1

        source.set_value(any_t(2));                  // propagate through the bound head
        CHECK(tail.value().get<int>() == 26);        // 20 + 5 + 1, no intermediate reads
        core_application::process_events();
        const int tail_after_round = tail_evaluations;
        flush_bindings();
        CHECK(tail_evaluations == tail_after_round); // the round ended clean

        middle.set_value(any_t(7));                  // plain middle written by hand
        CHECK(tail.value().get<int>() == 28);        // 20 + 7 + 1
        core_application::process_events();
        const int tail_after_second_round = tail_evaluations;
        flush_bindings();
        CHECK(tail_evaluations == tail_after_second_round);   // the second round ended clean
        CHECK(tail.value().get<int>() == 28);
    }

    // (14) Theme B: on_value_changed() registers a change handler. It is not called immediately, it is
    //      called on every real change (and only then), and destroying the handle unregisters it.
    {
        property_t property{ any_t(7) };
        std::vector<int> recorded;
        property_change_handler_t handle = property.on_value_changed([&] {
            recorded.push_back(property.value().get<int>());
        });
        CHECK(recorded.empty());                     // no immediate call (Qt's addNotifier)
        CHECK(handle.is_bound());

        property.set_value(any_t(8));
        CHECK(recorded.size() == 1);
        CHECK(recorded[0] == 8);
        CHECK(recorded[0] == property.value().get<int>());  // the handler reads the new value

        property.set_value(any_t(8));                // equal write: nobody is called
        CHECK(recorded.size() == 1);

        property.set_value(any_t(9));
        CHECK(recorded.size() == 2);
        CHECK(recorded[1] == 9);

        handle = property_change_handler_t{};          // reset: unregister
        CHECK(!handle.is_bound());
        property.set_value(any_t(10));
        CHECK(recorded.size() == 2);
        CHECK(property.value().get<int>() == 10);
    }

    // (15) Theme B: on_value_changed() is the same registration under Qt's other name, and
    //      subscribe() calls the callback immediately and then once per later change.
    {
        property_t property{ any_t(0) };
        int changed_calls = 0;
        property_change_handler_t changed = property.on_value_changed([&] { ++changed_calls; });
        CHECK(changed_calls == 0);                   // no immediate call
        property.set_value(any_t(1));
        CHECK(changed_calls == 1);
        changed = property_change_handler_t{};

        property_t subscribed_property{ any_t(42) };
        std::vector<int> recorded;
        {
            property_change_handler_t subscription = subscribed_property.subscribe([&] {
                recorded.push_back(subscribed_property.value().get<int>());
            });
            CHECK(recorded.size() == 1);             // called immediately with the current value
            CHECK(recorded[0] == 42);

            subscribed_property.set_value(any_t(1));
            subscribed_property.set_value(any_t(2));
            CHECK(recorded.size() == 3);
            CHECK(recorded[1] == 1);
            CHECK(recorded[2] == 2);

            subscribed_property.set_value(any_t(2)); // equal: not a change
            CHECK(recorded.size() == 3);
        }
        subscribed_property.set_value(any_t(3));     // the subscription is gone with its scope
        CHECK(recorded.size() == 3);
    }

    // (16) Theme B: the value-change gate. A re-evaluation that produces an equal value calls no
    //      handler and does not notify the dependants; a type without operator== always notifies.
    {
        flush_bindings();

        property_t trigger{ any_t(0) };
        property_t clamped{ any_t(0) };
        int clamped_evaluations = 0;
        clamped.set_binding([&]() {
            ++clamped_evaluations;
            return any_t(trigger.value().get<int>() < 100 ? 1 : 2);
        });
        CHECK(clamped.value().get<int>() == 1);

        int handler_calls = 0;
        property_change_handler_t handler = clamped.on_value_changed([&] { ++handler_calls; });

        int dependant_evals = 0;
        property_t dependant{ any_t(0) };
        dependant.set_binding([&]() {
            ++dependant_evals;
            return any_t(clamped.value().get<int>() * 10);
        });
        CHECK(dependant.value().get<int>() == 10);
        CHECK(dependant_evals == 1);

        trigger.set_value(any_t(50));                // clamped recomputes to 1: equal value
        CHECK(handler_calls == 0);                   // no handler call
        CHECK(dependant_evals == 1);                 // the dependant was not notified
        CHECK(dependant.value().get<int>() == 10);
        CHECK(dependant_evals == 1);
        CHECK(clamped_evaluations == 2);             // the write re-evaluated exactly once

        trigger.set_value(any_t(100));               // clamped recomputes to 2: a real change
        CHECK(handler_calls == 1);
        CHECK(dependant.value().get<int>() == 20);
        CHECK(dependant_evals == 2);
        const int clamped_after_change = clamped_evaluations;
        flush_bindings();                            // nothing else was queued
        CHECK(clamped_evaluations == clamped_after_change);

        // A type without operator== cannot be compared, so it always notifies (Qt's
        // has_operator_equal short-circuit does not apply).
        struct uncomparable {
            int value = 0;
        };
        property_t source{ any_t(1) };
        property_t plain{ any_t(uncomparable{}) };
        int plain_calls = 0;
        plain.set_binding([&]() {
            (void)source.value();                    // a real dependency, but a constant result
            return any_t(uncomparable{ 7 });
        });
        property_change_handler_t plain_handler = plain.on_value_changed([&] { ++plain_calls; });
        source.set_value(any_t(2));
        CHECK(plain_calls == 1);                     // same content, but no operator== -> notifies
        CHECK(plain.value().get<uncomparable>().value == 7);
    }

    // (17) Theme B: a handler that is destroyed while it runs is safe, and a handler may unregister
    //      a peer in the same notification pass.
    {
        property_t property{ any_t(0) };
        int self_calls = 0;
        property_change_handler_t self;
        self = property.on_value_changed([&] {
            ++self_calls;
            self = property_change_handler_t{};        // destroy the live handle from its own callback
        });
        CHECK(self.is_bound());

        property.set_value(any_t(1));
        CHECK(self_calls == 1);
        CHECK(!self.is_bound());
        property.set_value(any_t(2));
        CHECK(self_calls == 1);                      // unregistered: no further call

        int killer_calls = 0;
        int victim_calls = 0;
        property_change_handler_t victim;
        property_change_handler_t killer = property.on_value_changed([&] {
            ++killer_calls;
            victim = property_change_handler_t{};      // a handler unregisters a peer mid-run
        });
        victim = property.on_value_changed([&] { ++victim_calls; });
        property.set_value(any_t(3));
        CHECK(killer_calls == 1);
        CHECK(victim_calls == 0);                    // handlers run in registration order: skipped
    }

    // (17b) Theme B: a handler may destroy another property that is waiting in the same notification
    //       pass; its queued notification is removed and never reaches freed storage.
    {
        property_t shared_source{ any_t(0) };

        property_t destroyer{ any_t(0) };
        destroyer.set_binding([&]() { return any_t(shared_source.value().get<int>()); });
        int destroyer_calls = 0;

        property_t* doomed = new property_t{ any_t(0) };
        doomed->set_binding(
            [&]() { return any_t(shared_source.value().get<int>() * 10); });
        int doomed_calls = 0;
        property_change_handler_t doomed_handler = doomed->on_value_changed([&] { ++doomed_calls; });

        property_change_handler_t destroyer_handler = destroyer.on_value_changed([&] {
            ++destroyer_calls;
            delete doomed;                           // the other pending notification is discarded
            doomed = nullptr;
        });

        shared_source.set_value(any_t(1));
        CHECK(destroyer_calls == 1);
        CHECK(doomed_calls == 0);
        CHECK(doomed == nullptr);
    }

    // (18) Theme B: set_source() re-targets the same callback; moving a handle keeps the
    //      registration with the moved-to handle.
    {
        property_t first{ any_t(0) };
        property_t second{ any_t(0) };
        int calls = 0;
        property_change_handler_t handler([&] { ++calls; });
        CHECK(!handler.is_bound());                  // a callback-only handle observes nothing yet

        handler.set_source(first);
        CHECK(handler.is_bound());
        first.set_value(any_t(1));
        CHECK(calls == 1);

        handler.set_source(second);                  // the same callback now watches second
        first.set_value(any_t(2));
        CHECK(calls == 1);                           // first no longer reaches it
        second.set_value(any_t(3));
        CHECK(calls == 2);

        handler.set_source(first);
        second.set_value(any_t(4));
        CHECK(calls == 2);
        first.set_value(any_t(5));
        CHECK(calls == 3);

        property_t property{ any_t(0) };
        int moved_calls = 0;
        property_change_handler_t original = property.on_value_changed([&] { ++moved_calls; });
        property_change_handler_t moved = std::move(original);
        CHECK(moved.is_bound());
        CHECK(!original.is_bound());
        property.set_value(any_t(1));
        CHECK(moved_calls == 1);

        property_change_handler_t assigned;
        assigned = std::move(moved);
        CHECK(assigned.is_bound());
        CHECK(!moved.is_bound());
        property.set_value(any_t(2));
        CHECK(moved_calls == 2);
    }

    // (19) Theme B: a write from inside a handler assigns the value but does not re-enter the
    //      handlers; the dependants still see the change.
    {
        property_t property{ any_t(100) };
        int calls = 0;
        bool reset_property_on_change = false;
        property_change_handler_t handler = property.on_value_changed([&] {
            ++calls;
            if (reset_property_on_change) {
                property.set_value(any_t(100));      // written from inside its own handler
            }
        });

        CHECK(property.value().get<int>() == 100);
        reset_property_on_change = true;
        property.set_value(any_t(42));
        CHECK(property.value().get<int>() == 100);   // the handler's write was applied
        CHECK(calls == 1);                           // ... and the handler was not re-entered

        property_t dependant{ any_t(0) };
        int dependant_evals = 0;
        dependant.set_binding([&]() {
            ++dependant_evals;
            return any_t(property.value().get<int>());
        });
        CHECK(dependant.value().get<int>() == 100);
        CHECK(dependant_evals == 1);

        property.set_value(any_t(7));
        CHECK(calls == 2);
        CHECK(property.value().get<int>() == 100);   // the handler reset it again
        CHECK(dependant.value().get<int>() == 100);  // the dependant saw the change
        CHECK(dependant_evals == 2);
    }

    // (20) Theme B: a handle that outlives its property is inert (destroying it does not touch freed
    //      memory) and can still be re-targeted to a live property.
    {
        int calls = 0;
        property_change_handler_t handle;
        {
            auto* property = new property_t{ any_t(0) };
            handle = property->on_value_changed([&] { ++calls; });
            property->set_value(any_t(1));
            CHECK(calls == 1);
            delete property;
            CHECK(!handle.is_bound());              // orphaned, not dangling
        }
        handle = property_change_handler_t{};         // must not touch the destroyed property
        CHECK(!handle.is_bound());

        int reused_calls = 0;
        property_change_handler_t reusable([&] { ++reused_calls; });
        {
            auto* property = new property_t{ any_t(0) };
            reusable.set_source(*property);
            CHECK(reusable.is_bound());
            delete property;
            CHECK(!reusable.is_bound());
        }
        property_t other{ any_t(0) };
        reusable.set_source(other);                 // the callback survived the first property
        CHECK(reusable.is_bound());
        other.set_value(any_t(5));
        CHECK(reused_calls == 1);
    }

    // (21) Theme B: several handlers on one property all fire, in registration order, and an equal
    //      write calls none of them.
    {
        property_t property{ any_t(0) };
        int first_calls = 0;
        int second_calls = 0;
        int third_calls = 0;
        std::vector<int> order;
        property_change_handler_t first = property.on_value_changed([&] { ++first_calls; order.push_back(1); });
        property_change_handler_t second = property.on_value_changed([&] { ++second_calls; order.push_back(2); });
        property_change_handler_t third = property.on_value_changed([&] { ++third_calls; order.push_back(3); });

        property.set_value(any_t(1));
        CHECK(first_calls == 1);
        CHECK(second_calls == 1);
        CHECK(third_calls == 1);
        CHECK(order.size() == 3);
        CHECK(order[0] == 1);
        CHECK(order[1] == 2);
        CHECK(order[2] == 3);

        property.set_value(any_t(1));               // equal: no handler runs
        CHECK(order.size() == 3);
    }

    // (22) Theme B: a handler that mutates another property behaves like a normal
    //      write: the dependants are invalidated and batched (Theme A), not evaluated inside the
    //      handler.
    {
        flush_bindings();

        property_t source{ any_t(0) };
        property_t other{ any_t(0) };
        property_t dependant{ any_t(0) };
        int dependant_evals = 0;
        dependant.set_binding([&]() {
            ++dependant_evals;
            return any_t(other.value().get<int>() * 2);
        });
        CHECK(dependant.value().get<int>() == 0);
        CHECK(dependant_evals == 1);
        flush_bindings();

        property_change_handler_t handler = source.on_value_changed([&] { other.set_value(any_t(5)); });
        source.set_value(any_t(1));
        CHECK(dependant_evals == 1);                 // the dependant is queued, not evaluated here
        flush_bindings();                            // the deferred recompute runs now
        CHECK(dependant_evals == 2);                 // ... exactly once

        core_application::process_events();
        CHECK(dependant.value().get<int>() == 10);
        CHECK(dependant_evals == 2);
        const int dependant_after_round = dependant_evals;
        flush_bindings();                            // nothing left
        CHECK(dependant_evals == dependant_after_round);
    }

    // (23) Theme C: take_binding() detaches the binding into a move-only handle. The handle keeps
    //      the closure, the recorded location and the error alive while the value freezes; the
    //      detached binding is no longer evaluated and re-installing the handle runs the closure
    //      again and restores the dependency set.
    {
        flush_bindings();
        property_t source{ any_t(100) };
        property_t bound{ any_t(0) };
        int evaluations = 0;
        bound.set_binding([&]() {
            ++evaluations;
            return any_t(source.value().get<int>());
        });
        CHECK(bound.value().get<int>() == 100);
        CHECK(evaluations == 1);
        CHECK(!bound.binding().is_null());

        property_binding_t taken = bound.take_binding();
        CHECK(!taken.is_null());
        CHECK(bound.binding().is_null() && !bound.has_binding());
        CHECK(taken.source_location().line() != 0);  // the install site travelled with the handle

        source.set_value(any_t(101));                // the detached binding does not watch source
        CHECK(evaluations == 1);
        flush_bindings();                            // the detached closure is not queued
        CHECK(evaluations == 1);
        CHECK(bound.value().get<int>() == 100);      // the value froze at the take

        property_binding_t replaced = bound.take_binding();   // nothing installed: a null handle
        CHECK(replaced.is_null());                   // nothing was installed before
        CHECK(bound.set_binding(taken));             // installing shares, it does not consume
        CHECK(!taken.is_null());
        CHECK(bound.has_binding());
        CHECK(bound.value().get<int>() == 101);
        CHECK(evaluations == 2);

        source.set_value(any_t(102));                // the dependency set is restored
        CHECK(bound.value().get<int>() == 102);
        CHECK(evaluations == 3);
        flush_bindings();                            // the read consumed the entry
        CHECK(evaluations == 3);
    }

    // (24) Theme C (Qt's uninstalledBindingDoesNotEvaluate): a handle keeps a detached binding alive
    //      without it being evaluated; the value stays at the last computation.
    {
        property_t i{ any_t(0) };
        property_t j{ any_t(0) };
        int evaluations = 0;
        i.set_binding([&]() {
            ++evaluations;
            return any_t(j.value().get<int>());
        });
        CHECK(evaluations == 1);
        j.set_value(any_t(42));
        CHECK(i.value().get<int>() == 42);
        CHECK(evaluations == 2);

        property_binding_t kept = i.binding();
        i.set_value(any_t(10));
        CHECK(!i.has_binding());
        CHECK(!kept.is_null());
        CHECK(kept.error().kind == property_binding_t_error_t::kind_enum::NONE);
        j.set_value(any_t(12));
        CHECK(evaluations == 2);                        // the detached closure did not run again
        CHECK(i.value().get<int>() == 10);
    }

    // (25) Theme C: a sticky binding survives a write, keeps its value-change behaviour and is
    //      removed by the next write once the stickiness is cleared (Qt's stickyBinding).
    {
        property_t prop{ any_t(0) };
        property_t prop2{ any_t(2) };
        prop.set_binding([&]() { return any_t(prop2.value().get<int>()); });
        CHECK(prop.value().get<int>() == 2);

        property_binding_t priv = prop.binding();
        CHECK(!priv.is_sticky());
        priv.set_sticky();
        CHECK(priv.is_sticky());
        CHECK(prop.binding().is_sticky());              // the handle shares the installed state

        prop.set_value(any_t(1));                       // sticky: the write keeps the binding
        CHECK(prop.has_binding());
        CHECK(prop.value().get<int>() == 1);

        prop2.set_value(any_t(3));                      // the binding continues to work normally
        CHECK(prop.value().get<int>() == 3);

        priv.set_sticky(false);
        prop.set_value(any_t(42));                      // now the write removes the binding
        CHECK(!prop.has_binding());
        CHECK(prop.value().get<int>() == 42);
    }

    // (26) Theme D: the per-binding error channel. A healthy binding reports none; a closure reports
    //      its own error through set_binding_error() (Qt's currentlyEvaluatingBinding()->setError).
    {
        property_t healthy{ any_t(0) };
        healthy.set_binding([&]() { return any_t(42); });
        CHECK(healthy.binding().error().kind == property_binding_t_error_t::kind_enum::NONE);
        CHECK(!healthy.binding().error().has_error());
        CHECK(healthy.binding().error().message.empty());

        property_t reporting{ any_t(0) };
        reporting.set_binding([]() -> any_t {
            set_binding_error(property_binding_t_error_t{
                property_binding_t_error_t::kind_enum::UNKNOWN, "my error" });
            return any_t(0);
        });
        CHECK(reporting.value().get<int>() == 0);
        CHECK(reporting.binding().error().kind == property_binding_t_error_t::kind_enum::UNKNOWN);
        CHECK(reporting.binding().error().has_error());
        CHECK(reporting.binding().error().message == "my error");

        property_binding_t null_handle;
        CHECK(null_handle.error().kind == property_binding_t_error_t::kind_enum::NONE);
        CHECK(null_handle.source_location().line() == 0);
    }

    // (27) Theme D (Qt's selfBindingShouldNotCrash): a closure that reads its own property records
    //      the self-dependency and reports it as a binding loop instead of crashing.
    {
        property_t self{ any_t(0) };
        int evaluations = 0;
        const std::size_t before = records().size();
        self.set_binding([&]() { ++evaluations; return any_t(self.value().get<int>() + 1); });
        CHECK(self.binding().error().kind == property_binding_t_error_t::kind_enum::BINDING_LOOP);
        CHECK(self.binding().error().has_error());
        CHECK(!self.binding().error().message.empty());
        CHECK(records().size() == before);              // the install-time detection stays silent
        self.clear_binding();
        flush_bindings();
        CHECK(evaluations == 1);                        // the removed closure never runs
    }

    // (28) Theme D: the source location is captured at the set_binding() call site (defaulted
    //      std::source_location parameter), for the functor and the handle overload alike.
    {
        property_t property{ any_t(0) };
        const std::uint32_t expected_line = static_cast<std::uint32_t>(__LINE__) + 1;
        property.set_binding([&]() { return any_t(7); });
        CHECK(property.binding().source_location().line() == expected_line);
        CHECK(std::string_view(property.binding().source_location().file_name())
                  .find("m17_binding_test.cpp")
              != std::string_view::npos);

        const std::uint32_t handle_line = static_cast<std::uint32_t>(__LINE__) + 1;
        property_binding_t handle([&]() { return 8; });
        CHECK(handle.source_location().line() == handle_line);
        property.set_binding(handle);                   // the handle keeps its own location
        CHECK(property.binding().source_location().line() == handle_line);
        CHECK(property.value().get<int>() == 8);
    }

    // (29) Theme E: a property update group assigns immediately but suppresses propagation, so a
    //      dependant is still stale inside the group; the outermost end fires one deferred
    //      notification, so two writes cost one recompute and one handler call.
    {
        flush_bindings();
        property_t source{ any_t(0) };
        property_t dependant{ any_t(0) };
        int evaluations = 0;
        dependant.set_binding([&]() {
            ++evaluations;
            return any_t(source.value().get<int>() * 2);
        });
        int handler_calls = 0;
        property_change_handler_t handler = dependant.on_value_changed([&] { ++handler_calls; });
        CHECK(dependant.value().get<int>() == 0);
        CHECK(evaluations == 1);
        flush_bindings();

        {
            property_update_group_t group;
            source.set_value(any_t(1));
            source.set_value(any_t(2));
            CHECK(source.value().get<int>() == 2);      // a group defers propagation, not assignment
            CHECK(evaluations == 1);                    // ... and nothing was recomputed
            CHECK(handler_calls == 0);                  // no handler ran inside the group
            CHECK(dependant.value().get<int>() == 0);   // still stale inside the group
            CHECK(evaluations == 1);                    // the read did not start an evaluation
            CHECK(handler_calls == 0);
            flush_bindings();                           // the group queued no recompute
            CHECK(evaluations == 1);
        }
        CHECK(dependant.value().get<int>() == 4);       // the outermost end drained the group
        CHECK(evaluations == 2);                        // exactly one recompute
        CHECK(handler_calls == 1);                      // exactly one handler call
        flush_bindings();                               // the end-of-group round is clean
        CHECK(evaluations == 2);
        source.set_value(any_t(3));                     // outside a group a write propagates at once
        CHECK(dependant.value().get<int>() == 6);
    }

    // (30) Theme E: the free begin/end functions and nested groups. Only the outermost end drains.
    {
        flush_bindings();
        property_t source{ any_t(0) };
        property_t dependant{ any_t(0) };
        int evaluations = 0;
        dependant.set_binding([&]() {
            ++evaluations;
            return any_t(source.value().get<int>() + 1);
        });
        CHECK(dependant.value().get<int>() == 1);
        evaluations = 0;

        begin_property_update_group_t();
        source.set_value(any_t(1));
        {
            property_update_group_t inner;
            source.set_value(any_t(2));
            CHECK(evaluations == 0);
            CHECK(dependant.value().get<int>() == 1);   // the outer group still defers
        }
        CHECK(source.value().get<int>() == 2);          // the inner write was assigned
        CHECK(evaluations == 0);                        // ... but the inner end did not drain
        CHECK(dependant.value().get<int>() == 1);
        flush_bindings();                               // a nested group queues nothing
        CHECK(evaluations == 0);
        end_property_update_group_t();

        CHECK(dependant.value().get<int>() == 3);       // the outer end drained
        CHECK(evaluations == 1);
        source.set_value(any_t(5));                     // outside the group a write propagates at once
        CHECK(dependant.value().get<int>() == 6);
        CHECK(evaluations == 2);
    }

    // (31) Theme E: the handler of a written property runs only after every write of the group, so
    //      it observes a consistent state (Qt's groupedNotificationConsistency).
    {
        property_t i{ any_t(0) };
        property_t j{ any_t(0) };
        bool equal_when_notified = false;
        property_change_handler_t handler = i.on_value_changed([&] {
            equal_when_notified = (i.value().get<int>() == j.value().get<int>());
        });

        i.set_value(any_t(1));                          // outside a group the handler runs at once
        CHECK(!equal_when_notified);                    // ... before j = 1

        {
            property_update_group_t group;
            i.set_value(any_t(2));
            j.set_value(any_t(2));
            CHECK(!equal_when_notified);                // nothing ran inside the group
        }
        CHECK(equal_when_notified);                     // the handler ran after both writes
    }

    // (32) Theme E: a property destroyed while its change is deferred leaves no entry behind, and a
    //      change handler that outlives it stays inert.
    {
        property_t source{ any_t(0) };
        int doomed_calls = 0;
        property_change_handler_t doomed_handler;
        {
            auto* doomed = new property_t{ any_t(0) };
            doomed_handler = doomed->on_value_changed([&] { ++doomed_calls; });
            property_update_group_t group;
            doomed->set_value(any_t(1));
            CHECK(doomed_calls == 0);                   // deferred by the group
            delete doomed;                              // its deferred change is discarded
        }
        CHECK(!doomed_handler.is_bound());
        CHECK(doomed_calls == 0);
        CHECK(source.value().get<int>() == 0);
        flush_bindings();                            // no deferred change resurfaced
        CHECK(doomed_calls == 0);
    }

    // (33) Theme G: property_t::from() is the one way to reach a property as a handle. The handle is
    //      valid, is a REFERENCE view (it owns no value), remembers the property's value type, and a
    //      binding installed through it drives the property's reader and writes back through the
    //      property's setter.
    {
        bindable_object_t object;
        const meta_property_t property = object.meta_object()->property("value");
        CHECK(property.is_valid());
        CHECK(property.is_notifiable());            // the bindable fact: has a notifier
        CHECK(property.is_writable());
        CHECK(property.is_notifiable());

        property_t handle = property_t::from(object, property);
        CHECK(handle.is_valid());
        CHECK(!handle.is_owning());                 // a reference view, not a value owner
        CHECK(!handle.has_binding());
        CHECK(handle.type().is<int>());

        // A binding installed through the handle drives the reader, and its result is written back
        // through the property's setter (which is what publishes it to the notifier).
        property_t source{ any_t(7) };
        CHECK(handle.set_binding([&source]() { return any_t(source.value().get<int>()); }));
        CHECK(handle.has_binding());
        CHECK(object.value() == 7);
        CHECK(property.read(object).get<int>() == 7);   // the property's own reader agrees
        source.set_value(any_t(9));
        // The view publishes at the end of the dependency's notification pass (the reference backend
        // recomputes eagerly), so the object's own reader already sees the new value.
        CHECK(object.value() == 9);
        CHECK(handle.value().get<int>() == 9);      // the view agrees (a plain read, nothing pending)

        // clear_binding() through the handle detaches the binding and keeps the value.
        handle.clear_binding();
        CHECK(!handle.has_binding());
        CHECK(object.value() == 9);
        source.set_value(any_t(11));
        CHECK(handle.value().get<int>() == 9);
        CHECK(object.value() == 9);                 // detached: the value froze

        // A property with no notifier is refused: from() throws, and the refusal itself logs nothing.
        const meta_property_t read_only = object.meta_object()->property("read_only");
        CHECK(!read_only.is_notifiable());
        bool threw = false;
        try {
            (void)property_t::from(object, read_only);
        }
        catch (const bad_property_t&) {
            threw = true;
        }
        CHECK(threw);

        // A named notifier that resolves nowhere is the same refusal, and the miss logs one Critical
        // (the run-time accessor fallback, unchanged for the three remaining roles).
        const std::size_t before_dangling = records().size();
        const meta_property_t dangling = object.meta_object()->property("dangling");
        CHECK(!dangling.is_notifiable());
        threw = false;
        try {
            (void)property_t::from(object, dangling);
        }
        catch (const bad_property_t&) {
            threw = true;
        }
        CHECK(threw);
        CHECK(records().size() == before_dangling + 1);
        CHECK(records().back().level == log_level_enum::CRITICAL);
        CHECK(records().back().message.find("notifier") != std::string::npos);
    }

    // (33b) Theme G: an INHERITED property. It is declared by the base class and the handle is created
    //       on the derived object, so the resolution walks the meta-object chain; the write goes
    //       through the base's setter.
    {
        bindable_derived_t object;
        const meta_property_t property = object.meta_object()->property("inherited");
        CHECK(property.is_valid());
        CHECK(property.is_notifiable());
        property_t handle = property_t::from(object, property);
        CHECK(handle.is_valid());
        CHECK(!handle.is_owning());
        CHECK(handle.type().is<int>());
        CHECK(handle.set_value(any_t(5)));          // writes back through the inherited setter
        CHECK(object.inherited() == 5);
        CHECK(object.inherited_data.value().get<int>() == 5);
    }

    // (34) Theme G/N: the class's own subscription publishes every change of its property member, and
    //      the owning backend's manual operations stay available through that member:
    //      - a bound-value change emits the property's signal on the owning object;
    //      - set_value_bypassing_bindings() assigns, notifies nobody and keeps the binding;
    //      - notify() publishes unconditionally, exactly once per call, without re-evaluating.
    //      set_value_bypassing_bindings() and notify() are OWNING-backend operations (a reference view
    //      does not support them), so the test drives the member for those two and installs the
    //      binding under test through the property_t view.
    {
        flush_bindings();
        bindable_object_t object;
        int signal_hits = 0;
        int last_value = -1;
        const connection_handle_t probe_conn = object.connect("value_changed(int) const", [&](int value) {
            ++signal_hits;
            last_value = value;
        });
        CHECK(probe_conn.is_valid());

        property_t handle = property_t::from(object, object.meta_object()->property("value"));
        CHECK(handle.is_valid());

        int changes = 0;
        property_change_handler_t view_handler = handle.on_value_changed([&] { ++changes; });

        object.set_value(3);                        // written through the property's writer
        CHECK(changes == 1);
        CHECK(signal_hits == 1);
        CHECK(last_value == 3);

        // Theme N: a bypassing write assigns unconditionally and notifies nothing.
        object.value_data.set_value_bypassing_bindings(any_t(42));
        CHECK(changes == 1);
        CHECK(signal_hits == 1);
        CHECK(object.value_data.value().get<int>() == 42);   // the value WAS assigned
        CHECK(object.value() == 42);

        // A manual notify() publishes it: handler and notifier signal fire exactly once.
        object.value_data.notify();
        CHECK(changes == 2);
        CHECK(signal_hits == 2);
        CHECK(last_value == 42);

        // ... and it is UNCONDITIONAL (Qt's manual notify): an unchanged value still fires.
        object.value_data.notify();
        CHECK(changes == 3);
        CHECK(signal_hits == 3);

        // A bypassing write does NOT clear a binding (Qt keeps it) and does not fire anything; the
        // installed binding keeps working and overwrites the bypassed value on its next evaluation.
        property_t injected_property{ any_t(100) };
        handle.set_binding(
            [&injected_property]() { return any_t(injected_property.value().get<int>()); });
        CHECK(changes == 4);                        // 42 -> 100 is a real change
        CHECK(signal_hits == 4);
        CHECK(last_value == 100);

        object.value_data.set_value_bypassing_bindings(any_t(7));
        CHECK(handle.has_binding());                // the binding survived the bypassing write
        CHECK(changes == 4);
        CHECK(signal_hits == 4);
        CHECK(handle.value().get<int>() == 7);      // ... and the bypassed value is visible

        const int seen = changes;
        object.value_data.notify();                 // manual notify: fires, does not re-evaluate
        CHECK(changes == seen + 1);
        CHECK(signal_hits == 5);
        CHECK(last_value == 7);                     // the notifier reports the bypassed value
        CHECK(object.value() == 7);                 // ... and no evaluation happened

        injected_property.set_value(any_t(200));    // the binding re-evaluates and wins again
        CHECK(object.value() == 200);
        CHECK(changes == seen + 2);
        CHECK(signal_hits == 6);
        CHECK(last_value == 200);
    }

    // (35) Theme N/G: obtaining a view is a pure query when the property can notify, and a property
    //      that cannot is refused with bad_property_t and no diagnostic at all (there is no accessor
    //      name to resolve, so nothing can be reported).
    {
        bindable_object_t object;
        const std::size_t before = records().size();
        bool threw = false;
        try {
            (void)property_t::from(object, object.meta_object()->property("read_only"));
        }
        catch (const bad_property_t&) {
            threw = true;
        }
        CHECK(threw);
        CHECK(records().size() == before);
    }

    // (36) B1: the read-only handle. The flag lives on the HANDLE: a read-only copy refuses every
    //      write (one Warning per refusal, nothing changed) while value() / has_binding() / the change
    //      handlers keep reporting what the writable handle installed on the same variable.
    {
        // B1's flag costs the padding after the shared_ptr: 16 -> 24 bytes (+8, the agreed budget).
        static_assert(sizeof(property_t) == 2 * sizeof(void*) + sizeof(void*),
                      "property_t is one shared_ptr plus the read-only flag");

        property_t variable(any_t(1));
        property_t read_only = variable.as_read_only();
        CHECK(read_only.is_read_only());
        CHECK(!variable.is_read_only());
        CHECK(read_only.is_valid());
        CHECK(read_only.is_owning());               // the flag does not change the backend
        CHECK(read_only.value().get<int>() == 1);

        const std::size_t before = records().size();
        CHECK(!read_only.set_value(any_t(2)));
        CHECK(!read_only.set_value_bypassing_bindings(any_t(2)));
        CHECK(!read_only.notify());
        CHECK(!read_only.set_binding([]() { return any_t(2); }));
        read_only.clear_binding();
        CHECK(records().size() == before + 5);      // one Warning per refused operation
        for (std::size_t i = before; i < records().size(); ++i) {
            CHECK(records()[i].category == "metaobject.connection");
            CHECK(records()[i].level == log_level_enum::WARNING);
            CHECK(records()[i].message.find("read-only") != std::string::npos);
        }
        CHECK(variable.value().get<int>() == 1);    // nothing was changed
        CHECK(!variable.has_binding());

        // A binding installed through the writable handle is visible through the read-only one.
        CHECK(variable.set_binding([]() { return any_t(7); }));
        CHECK(read_only.has_binding());
        CHECK(read_only.value().get<int>() == 7);

        // The change handlers are read-only operations too (Qt's const QBindable::subscribe()).
        int seen = 0;
        property_change_handler_t watcher = read_only.on_value_changed([&] { ++seen; });
        variable.set_value(any_t(8));               // the writable handle still writes
        CHECK(seen == 1);
        CHECK(read_only.value().get<int>() == 8);

        // A read-only REFERENCE view behaves the same (the flag is on the handle, not the backend).
        bindable_object_t object;
        property_t view = property_t::from(object, object.meta_object()->property("value"));
        property_t read_only_view = view.as_read_only();
        CHECK(read_only_view.is_read_only());
        CHECK(!read_only_view.is_owning());
        CHECK(!read_only_view.set_value(any_t(3)));
        CHECK(object.value() == 0);
    }

    // (37) B2: the declared value type. Qt's QUntypedBindable::setBinding() compares the binding's
    //      valueMetaType with the property's type BEFORE evaluating; the port's declared-type
    //      overload does the same, and additionally verifies the first result (a lying binding).
    {
        property_t variable(any_t(0));
        const std::size_t before = records().size();
        int evaluations = 0;

        CHECK(!variable.set_binding([&evaluations]() {
            ++evaluations;
            return any_t(1.5);
        }, meta_type_t::from_type<double>()));
        CHECK(evaluations == 0);                    // refused before the closure runs (Qt's order)
        CHECK(records().size() == before + 1);
        CHECK(records().back().level == log_level_enum::WARNING);
        CHECK(records().back().message.find("double") != std::string::npos);
        CHECK(!variable.has_binding());
        CHECK(variable.value().get<int>() == 0);

        // The declaration matches but the first result does not: refused after the evaluation, and
        // the refusal carries the binding error channel's unknown kind + message.
        CHECK(!variable.set_binding([&evaluations]() {
            ++evaluations;
            return any_t(2.5);
        }, meta_type_t::from_type<int>()));
        CHECK(evaluations == 1);
        CHECK(records().size() == before + 2);
        CHECK(records().back().message.find("produced") != std::string::npos);
        CHECK(!variable.has_binding());
        CHECK(variable.value().get<int>() == 0);

        // An accepted declared-type install assigns the evaluated value.
        CHECK(variable.set_binding([]() { return any_t(100); }, meta_type_t::from_type<int>()));
        CHECK(variable.value().get<int>() == 100);

        // A refused install leaves a previously installed binding in place (Qt's setBinding() order).
        const std::size_t preserved = records().size();
        CHECK(!variable.set_binding([]() { return any_t(0.5); }, meta_type_t::from_type<double>()));
        CHECK(records().size() == preserved + 1);
        CHECK(variable.has_binding());
        CHECK(variable.value().get<int>() == 100);
    }

    // (38) B3: schedule_notify(). An equal-value install notifies exactly once when the binding asked
    //      for it, and nobody when it did not (the existing value-change gate).
    {
        property_t property{ any_t(0) };
        int notifications = 0;
        property_change_handler_t listener = property.on_value_changed([&] { ++notifications; });
        CHECK(notifications == 0);

        property_binding_t binding([]() { return 3; });
        CHECK(!binding.notify_scheduled());
        binding.schedule_notify();
        CHECK(binding.notify_scheduled());
        CHECK(notifications == 0);                  // scheduling notifies nothing
        property.set_binding(binding);
        CHECK(notifications == 1);                  // the install notifies (0 -> 3 here anyway)
        CHECK(!binding.notify_scheduled());         // ... and consumed the request
        CHECK(property.value().get<int>() == 3);

        // The one-shot case: an EQUAL-value install with the flag on notifies exactly once.
        int equal_notifications = 0;
        property_change_handler_t equal_listener = property.on_value_changed([&] { ++equal_notifications; });
        property_binding_t equal_binding([]() { return 3; });
        equal_binding.schedule_notify();
        property.set_binding(equal_binding);
        CHECK(equal_notifications == 1);
        CHECK(property.value().get<int>() == 3);

        // Without the flag an equal-value install stays silent (the behaviour before B3).
        property_binding_t quiet_binding([]() { return 3; });
        property.set_binding(quiet_binding);
        CHECK(equal_notifications == 1);
        CHECK(notifications == 2);                  // the two installs above; the silent one adds none
    }

    // (39) invalidate(): the value source of a binding changed OUTSIDE the property system. The
    //      closure reads a plain member, so the library records no dependency; invalidate() is how
    //      the member's owner says "it changed". The operation must set the dirty flag and not only
    //      append to the round queue: a read BEFORE any flush already has to see the new value.
    {
        int source = 1;
        property_t bound{ any_t(0) };
        int evaluations = 0;
        CHECK(bound.set_binding([&]() -> any_t {
            ++evaluations;
            return any_t(source);
        }));
        CHECK(bound.value().get<int>() == 1);

        source = 2;
        CHECK(bound.value().get<int>() == 1);       // nothing recorded a dependency: still the old value

        bound.invalidate();
        CHECK(evaluations == 1);                    // queued, not evaluated yet
        CHECK(bound.value().get<int>() == 2);       // ... and a read before the flush is up to date
        CHECK(evaluations == 2);                    // the read evaluated it once
        flush_bindings();                           // the read consumed the entry
        CHECK(evaluations == 2);

        // Dependants are invalidated too (the same propagation set_value() performs).
        property_t dependant{ any_t(0) };
        dependant.set_binding([&bound]() { return any_t(bound.value().get<int>() * 10); });
        CHECK(dependant.value().get<int>() == 20);
        source = 3;
        bound.invalidate();
        CHECK(bound.value().get<int>() == 3);
        CHECK(dependant.value().get<int>() == 30);

        // No installed binding: a no-op. An invalid handle: a no-op too (never crashes).
        property_t plain{ any_t(7) };
        plain.invalidate();
        CHECK(plain.value().get<int>() == 7);
        property_t none;
        none.invalidate();
        CHECK(!none.is_valid());
    }

    if (g_failures == 0) {
        std::printf("  property binding: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("  property binding: %d of %d check(s) failed\n", g_failures, g_checks);
    return 1;
}
