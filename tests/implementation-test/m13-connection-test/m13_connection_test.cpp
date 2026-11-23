// M13 test: functor connections, context objects and find_child().

#include <meta>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/cvref_meta_type_t.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_constructor_t.h>
#include <hwshqtb/metaobject/meta_info_t.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/meta_type_t.h>
#include <hwshqtb/metaobject/object_pointer_t.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/signal_blocker_t.h>
#include <hwshqtb/metaobject/signal_spy_t.h>
#include <hwshqtb/metaobject/thread_t.h>

using namespace hwshqtb::metaobject;

namespace m13_connection_test {

int g_checks = 0;
int g_failures = 0;

/// Every invoke_target_t::compute() call in this test, including the calls that run on the worker
/// thread. Block (17h) needs "the method never ran" evidence AFTER the receiver was deleted, when its
/// own `calls` member is gone.
std::atomic<int> g_compute_calls{ 0 };

/// @brief Record one check (the count is reported at the end, so a phase's delta is measurable).
void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// Spin/sleep until @p done, or give up after @p milliseconds. Used by block (17h), where the test
/// thread must NOT block on the call itself: the dummy call runs on a poster thread and is watched
/// with a timeout, so a liveness regression fails the test instead of hanging it.
template <typename Done>
bool wait_until(Done done, int milliseconds = 10000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (!done()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return done();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

/// One captured log record (the connection paths report through log_connection).
struct record_t {
    std::string category;
    log_level_enum level = log_level_enum::DEBUG;
    std::string message;
};

std::vector<record_t>& log_records() {
    static std::vector<record_t> captured;
    return captured;
}

struct emitter_t : object_t {
    OBJECT
public:
    SIGNAL(void ping(int value) const)
    SIGNAL(void plain() const)
};
META_OBJECT(emitter_t);

struct receiver_t : object_t {
    OBJECT
public:
    int hits = 0;
    int last = -1;

    void on_ping(int value) {
        ++hits;
        last = value;
    }
};
META_OBJECT(receiver_t);

/// A distinct type used to exercise the type filter of find_child / find_children.
struct special_t : object_t {
    OBJECT
};
META_OBJECT(special_t);

/// Root that receives convention-named slots for its children.
struct convention_root_t : object_t {
    OBJECT
public:
    int from_button = 0;
    int from_slider = 0;

    void on_button_ping(int v) {
        from_button = v;
    }

    void on_slider_pressed() {
        ++from_slider;
    }
};
META_OBJECT(convention_root_t);

/// A receiver whose class NAME is also a signature. `collide_t(int)` is a perfectly good table entry
/// (a constructor), and `method()` finds it — but it must never become a connection endpoint: its thunk
/// would placement-new over the receiver.
struct collide_t : object_t {
    OBJECT
public:
    collide_t() = default;

    explicit collide_t(int seed):
        value(seed) { }

    int value = 0;
    int hits = 0;

    void on_ping(int v) {
        ++hits;
        value = v;
    }
};
META_OBJECT(collide_t);

/// The target of the connection-type invoke() block (M19). Every method records the thread it ran on,
/// so "queued" and "blocking queued" can be told apart from "direct" without trusting the caller.
/// The counters are atomics because the cross-thread cases read them from the main thread.
struct invoke_target_t : object_t {
    OBJECT
public:
    std::atomic<int> calls{ 0 };
    std::atomic<int> value{ 0 };
    std::thread::id last_thread{};

    /// @brief Returns twice the argument (the return value that must cross threads for BlockingQueued).
    /// @note The atomic counter is written LAST: a caller that observes it has also observed the
    ///       thread id and the value (release/acquire through the same atomic), so the plain
    ///       last_thread member can be read after the spin without a data race.
    int compute(int v) {
        last_thread = std::this_thread::get_id();
        value = v;
        calls = calls.load() + 1;
        g_compute_calls = g_compute_calls.load() + 1;
        return v * 2;
    }

    /// @brief A void method, for the no-return-value forms.
    void set_value(int v) {
        last_thread = std::this_thread::get_id();
        value = v;
        calls = calls.load() + 1;
    }
};
META_OBJECT(invoke_target_t);

/// A GADGET: a value type with a meta-object and NO thread affinity, so a Queued invoke has nowhere to
/// post and must be refused.
struct invoke_value_t {
    GADGET

public:
    int doubled(int v) const {
        return v * 2;
    }
};
META_OBJECT(invoke_value_t);

/// A target whose methods return a REFERENCE: the rows' return columns keep the cv/ref affixes
/// ("const std::string&"), which is exactly what the bare-spelling comparison has to peel. The
/// referents are members, so a captured value stays observable after the call. Two referent types:
/// std::string and std::vector<int>. Both are spelled at BOTH levels: the object-first order puts the
/// out slot THIRD, so a std::string slot no longer competes with the std::string_view signature (it
/// did when the out slot was first — that call was ambiguous), and `label() const` is the fixture the
/// object-level std::string check in (17a'') uses.
struct invoke_ref_target_t : object_t {
    OBJECT
public:
    std::string text{ "referenced" };
    std::vector<int> numbers{ 1, 2, 3 };

    /// @brief Returns a reference the caller must be able to capture as a value AND as a reference.
    const std::string& label() const {
        return text;
    }

    /// @brief Same shape, a referent type that does not collide with the signature overload.
    const std::vector<int>& values() const {
        return numbers;
    }
};
META_OBJECT(invoke_ref_target_t);

/// The OBJECT-CVREF fixture: one row per cv/ref shape, so "which object may call which member" can be
/// pinned through every entry point (the new rule the table's matchers share).
/// @note object_t is non-movable, but binding `std::move(obj)` to the invoke forms' `T&&` never moves
///       anything: they only read the reference's value category, which is exactly what is under test.
struct cvref_target_t : object_t {
    OBJECT
public:
    /// @brief A NON-const, non-volatile, parameterised row: the row a const object must not reach.
    int value(int v) { return v + 1; }
    /// @brief The const twin, reachable from a const object.
    int value_const(int v) const { return v + 2; }
    /// @brief The no-argument const row (the zero-argument erased path).
    int get_const() const { return 1; }
    /// @brief `&`-qualified: an lvalue object only.
    int get_lref() & { return 2; }
    /// @brief `&&`-qualified: an rvalue object only.
    int get_rref() && { return 3; }
};
META_OBJECT(cvref_target_t);

/// Owns an invoke_target_t on its own thread and drains that thread's posting queue on demand. The
/// `paused` handshake makes "deferred until that thread's loop runs" deterministic: once `parked` is 1,
/// no posted call can be in flight and none will be dispatched until `paused` is cleared.
struct invoke_worker_t : thread_t {
    OBJECT
public:
    invoke_target_t* target = nullptr;
    /// Positive control for (17h): a second target on the same thread, used for the normal
    /// BlockingQueued delivery after the cancellable one has been destroyed.
    invoke_target_t* control = nullptr;
    std::thread::id worker_thread{};
    std::atomic<int> created{ 0 };
    std::atomic<int> paused{ 0 };
    std::atomic<int> parked{ 0 };
    std::atomic<int> destroy_requested{ 0 };
    std::atomic<int> destroyed{ 0 };
    std::atomic<int> stop{ 0 };

    void run() override {
        worker_thread = std::this_thread::get_id();
        target = new invoke_target_t;   // owned by THIS thread (leaked on purpose)
        // It must share the root's type registry, otherwise the queued argument copy cannot resolve
        // int and the call is silently dropped. set_parent does not change the thread affinity.
        target->set_parent(core_application::instance());
        control = new invoke_target_t;  // (17h)'s positive control: same thread, same lifetime rules
        control->set_parent(core_application::instance());
        created = 1;
        while (stop.load() == 0) {
            if (paused.load() != 0) {
                parked = 1;             // the round is over: nothing of ours is in flight now
                if (destroy_requested.load() != 0 && target != nullptr) {
                    // (17h): destroy the receiver WITHOUT processing the queue. ~object_t ->
                    // event_dispatcher::remove_object() purges the pending meta-call event; the
                    // event's destructor must cancel the sync and wake the BlockingQueued poster.
                    delete target;
                    target = nullptr;
                    destroyed = 1;
                }
                std::this_thread::yield();
                continue;
            }
            parked = 0;
            event_dispatcher::instance().send_posted_events();
            std::this_thread::yield();
        }
    }
};
META_OBJECT(invoke_worker_t);

/// The cancellable BlockingQueued call of (17h). It runs on its own thread so the test thread can
/// watch it with a timeout instead of joining a thread that a regression might leave blocked forever.
struct invoke_poster_t {
    const meta_object_t* meta = nullptr;
    invoke_target_t* target = nullptr;
    int result = 0;                 // the caller's ret_slot: must stay untouched on cancellation
    std::atomic<int> started{ 0 };
    std::atomic<int> returned{ 0 };
    std::atomic<int> finished{ 0 };
};

/// A receiver of object_t's own signals: the destroyed() section needs a real slot (not only a
/// functor) to prove the signal reaches connected receivers, and a second signal of the same object
/// to prove the connection set is still live during the emission.
struct destroyed_receiver_t : object_t {
    OBJECT
public:
    int destroyed_calls = 0;
    object_t* destroyed_arg = nullptr;
    object_t* sender_seen = nullptr;
    /// When set, on_destroyed() also bumps this counter (used when the receiver itself is deleted
    /// from a destroyed() slot, so the count survives it).
    int* external_destroyed_calls = nullptr;

    int name_calls = 0;
    std::string last_name;

    void on_destroyed(object_t* obj) {
        ++destroyed_calls;
        if (external_destroyed_calls != nullptr) {
            ++*external_destroyed_calls;
        }
        destroyed_arg = obj;
        sender_seen = sender();
    }

    void on_name_changed(const std::string& name) {
        ++name_calls;
        last_name = name;
    }
};
META_OBJECT(destroyed_receiver_t);

}  // namespace m13_connection_test

using namespace m13_connection_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    install_log_handler([](log_level_enum level, const log_context_t& context, std::string_view message) {
        log_records().push_back(record_t{ std::string(context.category), level, std::string(message) });
        return true;   // consumed: the default sink stays silent
    });

    std::printf("== M13: connections and lookups ==\n");

    // (1) A functor connection receives the arguments through the params array.
    emitter_t sender;
    sender.set_parent(root);
    int seen = -1;
    int calls = 0;
    const auto fn_handle = sender.connect("ping(int) const", [&](int value) {
        ++calls;
        seen = value;
    });
    CHECK(fn_handle.is_valid());
    sender.ping(21);
    CHECK(calls == 1);
    CHECK(seen == 21);

    // (2) Functor connections can be cut through the handle.
    CHECK(sender.disconnect(fn_handle));
    sender.ping(22);
    CHECK(calls == 1);

    // (3) An empty functor and an unknown signal produce invalid handles.
    CHECK(!sender.connect("ping(int) const", std::function<void(void**)>()).is_valid());
    CHECK(!sender.connect("no_such_signal()", []() { }).is_valid());

    // (4) A context object: destroying it cuts the connection.
    receiver_t receiver;
    receiver.set_parent(root);
    emitter_t* ctx_sender = new emitter_t;
    ctx_sender->set_parent(root);
    receiver_t* ctx_receiver = new receiver_t;
    ctx_receiver->set_parent(root);
    auto* context = new object_t;
    context->set_parent(root);
    const auto ctx_handle = ctx_sender->connect("ping(int) const", context, ctx_receiver, "on_ping(int)");
    CHECK(ctx_handle.is_valid());
    ctx_sender->ping(5);
    CHECK(ctx_receiver->hits == 1);
    delete context;                       // the context dies: the connection must be cut
    ctx_sender->ping(6);
    CHECK(ctx_receiver->hits == 1);       // unchanged
    CHECK(!ctx_handle.is_valid());

    // (5) A plain connection without a context behaves as before.
    CHECK(sender.connect("ping(int) const", &receiver, "on_ping(int)").is_valid());
    sender.ping(7);
    CHECK(receiver.hits == 1);
    CHECK(receiver.last == 7);

    // (6) find_child / find_children: depth-first, by name and by type.
    auto* top = new object_t;
    top->set_parent(root);
    auto* middle = new object_t;
    middle->set_parent(top);
    middle->set_object_name("middle");
    auto* leaf = new special_t;
    leaf->set_parent(middle);
    leaf->set_object_name("leaf");
    auto* second = new special_t;
    second->set_parent(top);
    second->set_object_name("second");

    CHECK(top->find_child("leaf") == leaf);
    CHECK(top->find_child("middle") == middle);
    CHECK(top->find_child("nope") == nullptr);
    CHECK(top->find_child<special_t>() != nullptr);
    CHECK(top->find_child<receiver_t>() == nullptr);
    const auto specials = top->find_children<special_t>();
    CHECK(specials.size() == 2);
    CHECK(specials[0] == leaf);            // depth-first: the deeper one comes first
    CHECK(specials[1] == second);
    CHECK(top->find_children("nope").empty());
    CHECK(top->find_children<object_t>("middle").size() == 1);

    // (7) block_signals() suppresses emission and is reference counted.
    receiver_t blocked_receiver;
    blocked_receiver.set_parent(root);
    CHECK(sender.connect("ping(int) const", &blocked_receiver, "on_ping(int)").is_valid());
    CHECK(!sender.signals_blocked());
    CHECK(!sender.block_signals(true));          // returns the previous state
    CHECK(sender.signals_blocked());
    sender.ping(100);
    CHECK(blocked_receiver.hits == 0);
    CHECK(sender.block_signals(true));           // nested: still blocked, previous was true
    sender.block_signals(false);
    sender.ping(101);
    CHECK(blocked_receiver.hits == 0);           // one level still held
    sender.block_signals(false);
    CHECK(!sender.signals_blocked());
    sender.ping(102);
    CHECK(blocked_receiver.hits == 1);

    // (8) signal_blocker_t is the RAII form of the same thing.
    {
        signal_blocker_t blocked(&sender);
        CHECK(sender.signals_blocked());
        sender.ping(103);
        CHECK(blocked_receiver.hits == 1);
    }
    CHECK(!sender.signals_blocked());
    sender.ping(104);
    CHECK(blocked_receiver.hits == 2);
    {
        signal_blocker_t noop(nullptr);            // nullptr is a no-op
        CHECK(!sender.signals_blocked());
    }

    // (10) object_pointer_t turns null once the target is destroyed.
    auto* guarded = new object_t;
    guarded->set_parent(root);
    object_pointer_t<object_t> guard(guarded);
    CHECK(guard.data() == guarded);
    CHECK(static_cast<bool>(guard));
    CHECK(guard->object_name().empty());
    delete guarded;
    CHECK(guard.data() == nullptr);
    CHECK(!static_cast<bool>(guard));

    // Re-binding after a clear works, and a null target stays null.
    object_pointer_t<object_t> rebound;
    CHECK(rebound.data() == nullptr);
    auto* other_guard_target = new object_t;
    other_guard_target->set_parent(root);
    rebound = other_guard_target;
    CHECK(rebound.data() == other_guard_target);
    rebound.clear();
    CHECK(rebound.data() == nullptr);

    // (11) signal_spy_t records each emission together with its arguments.
    emitter_t spied;
    spied.set_parent(root);
    {
        signal_spy_t spy(&spied, "ping(int) const");
        CHECK(spy.is_valid());
        CHECK(spy.count() == 0);
        spied.ping(1);
        spied.ping(2);
        CHECK(spy.count() == 2);
        auto records = spy.take_all();
        CHECK(records.size() == 2);
        CHECK(records[0].size() == 1);
        CHECK(records[0][0].get<int>() == 1);
        CHECK(records[1][0].get<int>() == 2);
        CHECK(spy.count() == 0);          // take_all() emptied the list
    }
    // After the spy dies, nothing is recorded any more (and nothing crashes).
    spied.ping(3);

    // An unknown signal or a null sender yields an invalid spy that does nothing.
    signal_spy_t bad_signal(&spied, "no_such()");
    CHECK(!bad_signal.is_valid());
    signal_spy_t bad_object(nullptr, "ping(int) const");
    CHECK(!bad_object.is_valid());

    // (12) meta_object_t::connect_slots_by_name (QMetaObject::connectSlotsByName): the naming convention.
    convention_root_t convention;
    convention.set_parent(root);
    auto* button = new emitter_t;
    button->set_parent(&convention);
    button->set_object_name("button");
    auto* slider = new emitter_t;
    slider->set_parent(&convention);
    slider->set_object_name("slider");
    auto* unnamed = new emitter_t;          // no object_name: contributes no convention
    unnamed->set_parent(&convention);

    meta_object_t::connect_slots_by_name(&convention);
    button->ping(11);
    CHECK(convention.from_button == 11);
    slider->ping(0);                        // slider has no matching slot -> silently ignored
    CHECK(convention.from_slider == 0);     // (from_slider is only bumped by slider_pressed)
    unnamed->ping(99);
    CHECK(convention.from_button == 11);    // an unnamed child is not wired
    button->plain();
    CHECK(convention.from_button == 11);    // no slot named on_button_plain

    // (13) A CONSTRUCTOR is never a connection endpoint. Constructors are not in the method space:
    //      the class name doubles as a constructor signature ("collide_t(int)"), but method() and
    //      signal() do not resolve it, so a connection naming it is an ordinary unknown-signature
    //      miss. The constructor itself stays reachable through constructor() only.
    collide_t collide;
    collide.set_parent(root);
    CHECK(collide.value == 0);
    CHECK(collide.hits == 0);

    const index_t ctor_index = collide.meta_object()->index_of_constructor("collide_t(int)");
    CHECK(ctor_index != index_npos);
    const meta_constructor_t ctor_handle = collide.meta_object()->constructor(ctor_index);
    CHECK(ctor_handle.is_valid());

    // The structural fact the old runtime refusal used to prove: no method-space lookup can name a
    // constructor, so no meta_method_t endpoint can ever BE a constructor.
    CHECK(collide.meta_object()->index_of_method("collide_t(int)") == index_npos);
    CHECK(!collide.meta_object()->method("collide_t(int)").is_valid());
    CHECK(!collide.meta_object()->signal("collide_t(int)").is_valid());
    CHECK(collide.meta_object()->index_of_method("collide_t()") == index_npos);
    CHECK(!collide.meta_object()->method("collide_t()").is_valid());

    const std::size_t before = log_records().size();

    // (13a) Constructor spelled as the SLOT: the slot lookup misses, so the connection is refused
    //       silently, exactly like any unknown signature.
    const auto slot_hit = sender.connect("ping(int) const", &collide, "collide_t(int)");
    CHECK(!slot_hit.is_valid());
    CHECK(log_records().size() == before);

    // (13b) Constructor spelled as the SIGNAL: the signal lookup misses too.
    const auto signal_hit = collide.connect("collide_t(int)", &receiver, "on_ping(int)");
    CHECK(!signal_hit.is_valid());
    CHECK(log_records().size() == before);

    // (13c) The functor overload (signal side only) misses the same way.
    const auto functor_hit = collide.connect("collide_t(int)", []() { });
    CHECK(!functor_hit.is_valid());
    CHECK(log_records().size() == before);

    // (13d) The handle overload only accepts meta_method_t, and the lookup that used to hand it a
    //       constructor now hands it an invalid handle; connect() refuses an invalid endpoint.
    CHECK(!sender.connect(sender.meta_object()->method("ping(int) const"),
                          &collide,
                          collide.meta_object()->method("collide_t(int)")).is_valid());
    CHECK(log_records().size() == before);
    CHECK(!sender.connect(collide.meta_object()->signal("collide_t(int)"),
                          &receiver,
                          receiver.meta_object()->method("on_ping(int)")).is_valid());
    CHECK(log_records().size() == before);

    // (13e) A queued connect is refused identically: no constructor can reach the dispatch path.
    const auto queued_hit = sender.connect("ping(int) const", &collide, "collide_t(int)", connection_type_flag::QUEUED);
    CHECK(!queued_hit.is_valid());
    CHECK(log_records().size() == before);

    // (13f) No placement-new evidence: the rejected attempts never reached the receiver.
    sender.ping(3);
    CHECK(collide.hits == 0);
    CHECK(collide.value == 0);

    // (13g) Positive control: a normal connection on the same objects still establishes and dispatches,
    //       and logs nothing.
    const auto good = sender.connect("ping(int) const", &collide, "on_ping(int)");
    CHECK(good.is_valid());
    sender.ping(4);
    CHECK(collide.hits == 1);
    CHECK(collide.value == 4);
    CHECK(log_records().size() == before);
    CHECK(sender.disconnect(good));

    // (14) destroyed(): emitted from the destructor at Qt's point — exactly once, carrying the object
    //      pointer, before the children are deleted, with the connection set still live, and even when
    //      signals are blocked. Qt's QObject::~QObject clears blockSig first ("unblock signals so we
    //      always emit destroyed()", qobject.cpp:1010) and documents the signal as one that "cannot be
    //      blocked"; it is emitted after the QPointer notification and before the connections and the
    //      children are torn down.
    {
        destroyed_receiver_t observer;
        observer.set_parent(root);

        auto* parent = new object_t;
        parent->set_parent(root);
        auto* child = new object_t;
        child->set_parent(parent);
        child->set_object_name("doomed_child");
        object_pointer_t<object_t> child_guard(child);
        object_pointer_t<object_t> self_guard(parent);   // a QPointer is already null inside the slot
        object_t* raw_parent = parent;

        const meta_method_t destroyed_signal = meta_method_t::from_method(&object_t::destroyed);
        CHECK(destroyed_signal.is_valid());
        CHECK(destroyed_signal.is_signal());
        // Qt's signature is destroyed(QObject*); the port's key is the reflection-derived spelling.
        CHECK(std::string(destroyed_signal.signature()) == "destroyed(hwshqtb::metaobject::object_t*) const");
        CHECK(parent->connect(destroyed_signal, &observer,
                  meta_method_t::from_method(&destroyed_receiver_t::on_destroyed)).is_valid());
        // Another signal of the same object: the slot below emits it, which only reaches the observer
        // while the connection set is still live.
        CHECK(parent->connect(meta_method_t::from_method(&object_t::object_name_changed), &observer,
                  meta_method_t::from_method(&destroyed_receiver_t::on_name_changed)).is_valid());

        int functor_calls = 0;
        object_t* functor_arg = nullptr;
        bool child_alive_in_slot = false;
        bool children_intact = false;
        bool guard_null = false;
        bool sender_ok = false;
        const auto functor_handle = parent->connect(std::string(destroyed_signal.signature()),
            [&](object_t* object) {
                ++functor_calls;
                functor_arg = object;
                // Qt deletes the children AFTER the signal: the child and the child list are intact.
                children_intact = parent->children().size() == 1 && parent->children()[0] == child;
                child_alive_in_slot = static_cast<bool>(child_guard);
                // The death watch is already marked: object_pointer_t reads as null, like a QPointer.
                guard_null = self_guard.data() == nullptr;
                sender_ok = parent->sender() == parent;
                // The connection set is still live: emit another signal of this object.
                parent->set_object_name("dying");
                // A slot may even schedule a deferred delete of the dying object: the destructor
                // purges this object's event queue right after the emission (see below).
                parent->delete_later();
            });
        CHECK(functor_handle.is_valid());

        // Signals blocked: destroyed() is the one signal that is still emitted.
        parent->block_signals(true);
        CHECK(parent->signals_blocked());
        delete parent;

        // The deferred-delete event the slot posted was purged with the object: running the queue now
        // must not dispatch anything to the freed object (reaching the checks below at all is the
        // assertion for that; a stale event would be a use-after-free).
        core_application::process_events();

        CHECK(functor_calls == 1);                       // exactly once
        CHECK(functor_arg == raw_parent);                // carries the object pointer
        CHECK(observer.destroyed_calls == 1);
        CHECK(observer.destroyed_arg == raw_parent);
        CHECK(observer.sender_seen == raw_parent);       // sender() is the dying object
        CHECK(observer.name_calls == 1);                 // the connection set was still live
        CHECK(observer.last_name == "dying");
        CHECK(children_intact);                          // ran before the children were deleted
        CHECK(child_alive_in_slot);
        CHECK(guard_null);                               // the guarded pointer was already notified
        CHECK(sender_ok);
        CHECK(!child_guard);                             // the child went with the parent
        CHECK(self_guard.data() == nullptr);
    }

    // (15) A destroyed() slot may destroy other objects: a sibling of the child list the parent is
    //      about to walk, and a receiver that still has a connection in the table being dispatched.
    //      Qt's QObjectPrivate::deleteChildren() is written for exactly this ("the destructor of the
    //      child might delete siblings"); the port pops the live child list and dispatches over a
    //      snapshot of the connection table, so an entry that is already gone is skipped instead of
    //      being touched again.
    {
        int victim_calls = 0;
        auto* victim = new destroyed_receiver_t;
        victim->set_parent(root);
        victim->external_destroyed_calls = &victim_calls;

        auto* parent = new object_t;
        parent->set_parent(root);
        auto* first = new object_t;
        first->set_parent(parent);
        auto* second = new object_t;
        second->set_parent(parent);
        object_pointer_t<object_t> first_guard(first);
        object_pointer_t<object_t> second_guard(second);

        const meta_method_t destroyed_signal = meta_method_t::from_method(&object_t::destroyed);
        const std::string destroyed_signature(destroyed_signal.signature());
        // The deleting slot is connected FIRST, the victim's slot SECOND: the victim is gone by the
        // time the emission reaches its connection.
        CHECK(parent->connect(destroyed_signature, [&]() {
            delete second;
            delete victim;
        }).is_valid());
        CHECK(parent->connect(destroyed_signal, victim,
                  meta_method_t::from_method(&destroyed_receiver_t::on_destroyed)).is_valid());
        delete parent;

        CHECK(victim_calls == 0);            // destroyed before its turn: skipped, not crashed
        CHECK(!second_guard);                // the sibling the slot deleted
        CHECK(!first_guard);                 // the remaining child, deleted by the parent
    }

    // (16) An object destroyed while a dispatch is in progress. Deleting an object from a slot runs
    //      its destructor — and therefore its destroyed() emission — inside that dispatch; the
    //      emission pushes and pops its own dispatch frame, so the outer slot's sender() survives the
    //      nested destruction, and the destroyed() slot of the nested object sees itself as sender.
    {
        emitter_t outer;
        outer.set_parent(root);
        destroyed_receiver_t observer;
        observer.set_parent(root);
        auto* nested = new object_t;
        nested->set_parent(root);
        object_t* raw_nested = nested;

        const meta_method_t destroyed_signal = meta_method_t::from_method(&object_t::destroyed);
        CHECK(nested->connect(destroyed_signal, &observer,
                  meta_method_t::from_method(&destroyed_receiver_t::on_destroyed)).is_valid());
        bool nested_sender_ok = false;
        int nested_calls = 0;
        CHECK(nested->connect(std::string(destroyed_signal.signature()), [&]() {
            ++nested_calls;
            nested_sender_ok = nested->sender() == nested;
        }).is_valid());

        bool outer_sender_before = false;
        bool outer_sender_after = false;
        CHECK(outer.connect("ping(int) const", [&]() {
            outer_sender_before = outer.sender() == &outer;
            delete nested;                       // the nested destructor emits destroyed() right here
            outer_sender_after = outer.sender() == &outer;
        }).is_valid());

        outer.ping(1);

        CHECK(nested_calls == 1);
        CHECK(observer.destroyed_calls == 1);
        CHECK(observer.destroyed_arg == raw_nested);
        CHECK(observer.sender_seen == raw_nested);
        CHECK(nested_sender_ok);
        CHECK(outer_sender_before);
        CHECK(outer_sender_after);               // the nested emission restored the outer frame
    }

    // (17) connection_type_t on the typed invoke() entry points (M19). The low-level invoke() takes the
    //      caller's choice and honours it exactly like a signal connection does:
    //        Direct        -> call on the calling thread, synchronously;
    //        Auto          -> Direct when the target lives on the calling thread, Queued otherwise;
    //        Queued        -> post the call to the target's thread; the event carries the caller's
    //                         return slot and fills it when it runs (a value-returning row is fine);
    //        BlockingQueued-> post it AND wait, so the result is valid after the call.
    //      The refusals that remain are about the OBJECT, not about carrying a return value:
    //      BlockingQueued on the calling thread (would deadlock) and Queued on a value type (nowhere
    //      to post) each answer false with exactly one Warning and never run the method.
    {
        // ---- (17a) Direct and same-thread Auto: synchronous, and the result comes back ----
        invoke_target_t local;
        local.set_parent(root);
        const meta_object_t& local_meta = *local.meta_object();

        int direct_doubled = 0;
        CHECK(local_meta.invoke(local, "compute(int)", direct_doubled, connection_type_flag::DIRECT, 21));
        CHECK(direct_doubled == 42);
        CHECK(local.calls.load() == 1);                                // ran before invoke() returned
        CHECK(local.value.load() == 21);
        CHECK(local.last_thread == std::this_thread::get_id());

        int auto_doubled = 0;                                          // the Auto form (no type argument)
        CHECK(local_meta.invoke(local, "compute(int)", auto_doubled, 11));
        CHECK(auto_doubled == 22);
        CHECK(local.calls.load() == 2);
        CHECK(local.last_thread == std::this_thread::get_id());

        CHECK(local_meta.invoke(local, "set_value(int)", connection_type_flag::DIRECT, 3).is_valid());
        CHECK(local.value.load() == 3);
        CHECK(local.calls.load() == 3);

        // The handle form honours the type as well (and keeps the handle's own argument / return-type
        // checks: a wrong return type is still refused without calling, and the refusal reports itself
        // with ONE Critical naming the row, its actual return type and the expected spelling).
        const meta_method_t compute = local_meta.method("compute(int)");
        CHECK(compute.is_valid());
        int handle_doubled = 0;
        CHECK(compute.invoke(local, handle_doubled, connection_type_flag::DIRECT, 7));
        CHECK(handle_doubled == 14);
        CHECK(local.calls.load() == 4);
        const std::size_t before_wrong_ret = log_records().size();
        std::string wrong_type;
        CHECK(!compute.invoke(local, wrong_type, connection_type_flag::DIRECT, 7));
        CHECK(local.calls.load() == 4);                                // the check refused it, not the call
        CHECK(log_records().size() == before_wrong_ret + 1);
        if (log_records().size() == before_wrong_ret + 1) {
            const record_t& record = log_records().back();
            std::printf("  log: [%s] %s\n", record.category.c_str(), record.message.c_str());
            CHECK(record.category == "metaobject.connection");
            CHECK(record.level == log_level_enum::CRITICAL);
            CHECK(record.message.find("compute(int)") != std::string::npos);
            CHECK(record.message.find("'int'") != std::string::npos);          // the row's return type
            // The expected spelling is the library's own identity spelling for Ret (no hard-coded ABI
            // name): the message must quote exactly that.
            const std::string expected{ meta_info_t::original_name<std::string>() };
            CHECK(record.message.find("'" + expected + "'") != std::string::npos);
        }

        // The HANDLE's Auto out form (no tag) delegates to the tagged one with Auto, which resolves
        // to Direct on this thread: the value comes back synchronously.
        {
            int handle_auto_out = 0;
            CHECK(compute.invoke(local, handle_auto_out, 5));
            CHECK(handle_auto_out == 10);
        }

        // ---- (17a') the OBJECT-level Ret& form checks the return type, through the handle ----
        // This is the path that used to be UNCHECKED: its Direct branch wrote the row's own return
        // type through `&ret` whatever the caller's Ret was, so a wrong Ret silently wrote into a
        // foreign object and the call still answered true. The form now resolves the same own-first
        // winner and hands the call to method(signature).invoke(), which owns the ONE check:
        // false, the slot untouched, the method never run, and exactly one Critical naming the row,
        // its actual return type and the expected one.
        {
            const int calls_before_object = local.calls.load();
            const std::size_t before_object_wrong = log_records().size();
            // A `long` slot for an `int`-returning row: a genuinely different type, and a slot big
            // enough that the pre-fix unchecked write would land INSIDE it (so the mutation run shows
            // the wrong-type write instead of a stack corruption).
            long untouched = 123456789L;
            CHECK(!local_meta.invoke(local, "compute(int)", untouched, connection_type_flag::DIRECT, 7));
            CHECK(untouched == 123456789L);                            // nothing was written through it
            CHECK(local.calls.load() == calls_before_object);          // and the method never ran
            CHECK(log_records().size() == before_object_wrong + 1);    // exactly ONE record
            if (log_records().size() == before_object_wrong + 1) {
                const record_t& record = log_records().back();
                std::printf("  log: [%s] %s\n", record.category.c_str(), record.message.c_str());
                CHECK(record.category == "metaobject.connection");
                CHECK(record.level == log_level_enum::CRITICAL);
                CHECK(record.message.find("compute(int)") != std::string::npos);    // the signature
                CHECK(record.message.find("'int'") != std::string::npos);           // the actual return
                const std::string expected{ meta_info_t::original_name<long>() };
                CHECK(record.message.find("'" + expected + "'") != std::string::npos);  // the expected
                CHECK(record.message.find("returns 'int'") != std::string::npos);
                CHECK(record.message.find("not '") != std::string::npos);
            }
        }

        // ---- (17a'') a REFERENCE-returning row: the return type is compared BARE, at BOTH levels ----
        // `label() const` is declared `const std::string&`, so its return column keeps the cv/ref
        // affixes. The check peels them (detail::bare_spelling) and decays Ret (detail::arg_spelling),
        // so the row is capturable into a `const std::string&` AND into a `std::string` (a value
        // copy), while `int` is still refused with the one Critical. The TABLE-level half uses
        // `values() const` (`const std::vector<int>&`) for the vector shape and `label() const` for
        // the std::string shape: with the object-first order the out slot is the THIRD parameter, so a
        // std::string slot no longer collides with the std::string_view signature — before phase 9
        // that call was AMBIGUOUS, which is why this level could not spell it at all.
        {
            invoke_ref_target_t ref_target;
            ref_target.set_parent(root);
            const meta_object_t& ref_meta = *ref_target.meta_object();
            const std::string string_spelling{ meta_info_t::original_name<std::string>() };
            const std::string vector_spelling{ meta_info_t::original_name<std::vector<int>>() };

            // (a) the HANDLE level: `const std::string&` captured as a REFERENCE and as a VALUE.
            //     Both spellings narrow to the same bare type, which is exactly the fix.
            const meta_method_t label = ref_meta.method("label() const");
            CHECK(label.is_valid());
            // The row really carries the cv/ref spelling: that is the whole point of this case.
            CHECK(std::string(label.return_type_name()) == "const " + string_spelling + "&");
            std::string ref_storage{ "before" };
            const std::string& by_ref = ref_storage;
            CHECK(label.invoke(ref_target, by_ref, connection_type_flag::DIRECT));
            CHECK(by_ref == "referenced");
            CHECK(ref_storage == "referenced");                        // a real value copy landed
            std::string by_value{ "before" };
            CHECK(label.invoke(ref_target, by_value, connection_type_flag::DIRECT));
            CHECK(by_value == "referenced");

            // (b) the TABLE level: the delegated form hands Ret straight to the handle, so a
            //     reference-returning row behaves identically there — as a reference AND as a value.
            const meta_method_t values = ref_meta.method("values() const");
            CHECK(values.is_valid());
            CHECK(std::string(values.return_type_name()) == "const " + vector_spelling + "&");
            std::vector<int> vector_value{ 0 };
            CHECK(ref_meta.invoke(ref_target, "values() const", vector_value, connection_type_flag::DIRECT));
            CHECK(vector_value == ref_target.numbers);
            std::vector<int> vector_storage{ 0 };
            const std::vector<int>& vector_ref = vector_storage;
            CHECK(ref_meta.invoke(ref_target, "values() const", vector_ref, connection_type_flag::DIRECT));
            CHECK(vector_ref == ref_target.numbers);

            // (c) a genuinely different type is still refused AT THE OBJECT LEVEL: one Critical naming
            //     the row, its DECLARED return spelling and the expected one; the slot stays untouched.
            const std::size_t before_ref_wrong = log_records().size();
            int wrong_reference = -1;
            CHECK(!ref_meta.invoke(ref_target, "values() const", wrong_reference, connection_type_flag::DIRECT));
            CHECK(wrong_reference == -1);
            CHECK(log_records().size() == before_ref_wrong + 1);
            if (log_records().size() == before_ref_wrong + 1) {
                const record_t& record = log_records().back();
                std::printf("  log: [%s] %s\n", record.category.c_str(), record.message.c_str());
                CHECK(record.category == "metaobject.connection");
                CHECK(record.level == log_level_enum::CRITICAL);
                CHECK(record.message.find("values() const") != std::string::npos);
                CHECK(record.message.find("returns 'const " + vector_spelling + "&'")
                      != std::string::npos);
                CHECK(record.message.find("not 'int'") != std::string::npos);
            }

            // (d) the HANDLE refuses the std::string row the same way: ONE implementation, one message.
            const std::size_t before_handle_ref_wrong = log_records().size();
            int wrong_label = -1;
            CHECK(!label.invoke(ref_target, wrong_label, connection_type_flag::DIRECT));
            CHECK(wrong_label == -1);
            CHECK(log_records().size() == before_handle_ref_wrong + 1);
            if (log_records().size() == before_handle_ref_wrong + 1) {
                const record_t& record = log_records().back();
                std::printf("  log: [%s] %s\n", record.category.c_str(), record.message.c_str());
                CHECK(record.level == log_level_enum::CRITICAL);
                CHECK(record.message.find("label() const") != std::string::npos);
                CHECK(record.message.find("returns 'const " + string_spelling + "&'")
                      != std::string::npos);
                CHECK(record.message.find("not 'int'") != std::string::npos);
            }

            // (e) THE POINT OF THE OBJECT-FIRST ORDER: a `std::string` out slot at the OBJECT level.
            //     The out slot is the THIRD parameter now, so it no longer competes with the
            //     `std::string_view` signature: the typed form is the more specialized template and
            //     wins, so the call compiles. Under the old order both candidates were viable with one
            //     user-defined conversion each (std::string -> string_view on one side, const char[] ->
            //     string_view on the other), so this very call was AMBIGUOUS.
            {
                std::string label_out{ "before" };
                CHECK(ref_meta.invoke(ref_target, "label() const", label_out));    // (i) the Auto form
                CHECK(label_out == "referenced");
                // The overload that won is the bool-returning `Ret&` one, not the any_t form: this is
                // what proves the ambiguity is GONE rather than merely quiet.
                static_assert(std::is_same_v<
                    decltype(ref_meta.invoke(ref_target, "label() const", label_out)), bool>);

                std::string label_typed{ "before" };
                // (ii) the same std::string slot with a caller-chosen connection type.
                CHECK(ref_meta.invoke(ref_target, "label() const", label_typed, connection_type_flag::DIRECT));
                CHECK(label_typed == "referenced");
                static_assert(std::is_same_v<decltype(ref_meta.invoke(
                    ref_target, "label() const", label_typed, connection_type_flag::DIRECT)), bool>);
            }
        }

        // ---- cross-thread: an invoke_target_t owned by a worker thread ----
        auto* worker = new invoke_worker_t;
        worker->set_parent(root);
        worker->start();
        for (int i = 0; i < 20000000 && worker->created.load() == 0; ++i) {
            std::this_thread::yield();
        }
        CHECK(worker->created.load() == 1);
        invoke_target_t* remote = worker->target;
        CHECK(remote != nullptr);
        if (remote != nullptr) {
            const meta_object_t& remote_meta = *remote->meta_object();

            // ---- (17b) cross-thread Auto -> Queued: posted, deferred, and run on the worker ----
            worker->paused = 1;                                        // stop draining the queue
            for (int i = 0; i < 20000000 && worker->parked.load() == 0; ++i) {
                std::this_thread::yield();
            }
            CHECK(worker->parked.load() == 1);                         // nothing is in flight any more
            const int before_auto = remote->calls.load();
            CHECK(remote_meta.invoke(*remote, "set_value(int)", 7).is_valid());   // Auto, no connection type
            for (int i = 0; i < 2000; ++i) {
                std::this_thread::yield();
            }
            CHECK(remote->calls.load() == before_auto);                // deferred: the loop is parked
            worker->paused = 0;                                        // let the worker's loop run
            for (int i = 0; i < 20000000 && remote->calls.load() == before_auto; ++i) {
                std::this_thread::yield();
            }
            CHECK(remote->calls.load() == before_auto + 1);
            CHECK(remote->value.load() == 7);
            CHECK(remote->last_thread == worker->worker_thread);       // ran ON THE OBJECT'S THREAD
            CHECK(remote->last_thread != std::this_thread::get_id());

            // ---- (17c) BlockingQueued across threads: returns only after the call, result delivered ----
            int blocking_doubled = 0;
            CHECK(remote_meta.invoke(*remote, "compute(int)", blocking_doubled,
                      connection_type_flag::BLOCKING_QUEUED, 21));
            // Reaching this line proves the wait was released by the worker, and the value was written
            // into our own storage before the wait returned.
            CHECK(blocking_doubled == 42);
            CHECK(remote->last_thread == worker->worker_thread);
            CHECK(remote->value.load() == 21);

            // ---- (17c2) the UNTAGGED erased form is Direct, never Auto: on a target that lives on
            //      ANOTHER thread it still runs on the calling thread, synchronously — where the typed
            //      untagged form (17b) would have been posted. That is the erased channel's documented
            //      contract (the handle's invoke(Obj&&, std::span<const any_t>)), and the object level
            //      delegates to exactly that form, so the two stay one decision.
            {
                const any_t one_arg[] = { any_t(21) };
                const std::span<const any_t> erased_args = one_arg;
                const int before_erased_direct = remote->calls.load();
                const any_t erased_remote =
                    remote_meta.invoke(*remote, "compute(int)", erased_args);
                CHECK(erased_remote.is_valid());
                CHECK(erased_remote.is_initialized());       // it ran before the answer came back
                CHECK(erased_remote.get<int>() == 42);
                CHECK(remote->calls.load() == before_erased_direct + 1);
                CHECK(remote->last_thread == std::this_thread::get_id());   // OUR thread, not the worker's
            }

            // ---- (17d) Queued with a return value: posted, and the slot is filled when the worker
            //      runs it. The event holds the caller's ADDRESS, so the slot is untouched until then.
            worker->paused = 1;
            for (int i = 0; i < 20000000 && worker->parked.load() == 0; ++i) {
                std::this_thread::yield();
            }
            CHECK(worker->parked.load() == 1);
            const std::size_t before_queued_ret = log_records().size();
            const int before_posted = remote->calls.load();
            int posted_result = -1;                                    // the Ret& slot, handed over by address
            CHECK(remote_meta.invoke(*remote, "compute(int)", posted_result,
                      connection_type_flag::QUEUED, 9));
            CHECK(posted_result == -1);                                // nothing was delivered yet
            CHECK(remote->calls.load() == before_posted);              // ... and the body has not run
            CHECK(log_records().size() == before_queued_ret);          // no "cannot deliver" Warning any more
            worker->paused = 0;                                        // let the worker's loop run
            // FIFO barrier: a BlockingQueued call posted AFTER the queued one can only complete once
            // the queued one has been placed, and its sync gives this thread a happens-before edge to
            // the worker's write. That makes the slot read below race-free.
            int barrier = 0;
            CHECK(remote_meta.invoke(*remote, "compute(int)", barrier,
                      connection_type_flag::BLOCKING_QUEUED, 1));
            CHECK(barrier == 2);
            CHECK(posted_result == 18);                                // filled by the event, on the worker
            CHECK(remote->calls.load() == before_posted + 2);
            CHECK(remote->last_thread == worker->worker_thread);       // ran ON THE OBJECT'S THREAD
            CHECK(log_records().size() == before_queued_ret);          // still no record
        }

        // ---- (17e) BlockingQueued on the calling thread is refused (the deadlock Warning) ----
        const std::size_t before_deadlock = log_records().size();
        const int local_calls = local.calls.load();
        int deadlocked = -1;
        CHECK(!local_meta.invoke(local, "compute(int)", deadlocked,
                  connection_type_flag::BLOCKING_QUEUED, 5));
        CHECK(deadlocked == -1);
        CHECK(local.calls.load() == local_calls);                      // never ran
        CHECK(log_records().size() == before_deadlock + 1);
        CHECK(log_records().back().level == log_level_enum::WARNING);
        CHECK(log_records().back().message.find("deadlock") != std::string::npos);
        CHECK(!local_meta.invoke(local, "set_value(int)", connection_type_flag::BLOCKING_QUEUED, 99).is_valid());
        CHECK(local.value.load() != 99);
        CHECK(local.calls.load() == local_calls);
        CHECK(log_records().size() == before_deadlock + 2);
        CHECK(log_records().back().message.find("deadlock") != std::string::npos);

        // ---- (17f) Queued on a value type (no thread affinity) is refused ----
        const std::size_t before_value = log_records().size();
        invoke_value_t value_object;
        int ignored = 0;
        CHECK(!invoke_value_t::static_meta_object.invoke(value_object, "doubled(int) const", ignored,
                  connection_type_flag::QUEUED, 4));
        CHECK(ignored == 0);
        CHECK(log_records().size() == before_value + 1);
        CHECK(log_records().back().category == "metaobject.connection");
        CHECK(log_records().back().level == log_level_enum::WARNING);
        CHECK(log_records().back().message.find("thread affinity") != std::string::npos);
        CHECK(!invoke_value_t::static_meta_object.invoke(value_object, "doubled(int) const",
                  connection_type_flag::QUEUED, 4).is_valid());
        CHECK(log_records().size() == before_value + 2);

        // Positive control: the same value type still takes the Direct path (all a gadget can do).
        CHECK(invoke_value_t::static_meta_object.invoke(value_object, "doubled(int) const", ignored,
                  connection_type_flag::DIRECT, 4));
        CHECK(ignored == 8);
        int auto_value = 0;
        CHECK(invoke_value_t::static_meta_object.invoke(value_object, "doubled(int) const", auto_value, 6));
        CHECK(auto_value == 12);
        CHECK(log_records().size() == before_value + 2);               // Direct/Auto log nothing

        // ---- (17g) the non-Ret invoke() forms answer any_t: value / declared-only void / invalid ----
        // The three success and failure shapes of the new return convention, on both levels (the
        // meta-object call and the handle it delegates to). A value-returning entry now HANDS ITS
        // RESULT BACK instead of discarding it; a void entry answers a DECLARED-ONLY void any_t; a
        // miss and a refused connection type answer the INVALID any_t, the one "nothing ran" signal.
        {
            const int calls_before_any = local.calls.load();

            // (a) The value comes back — from the table level and from the handle.
            CHECK(local_meta.invoke(local, "compute(int)", 21).get<int>() == 42);
            CHECK(local.calls.load() == calls_before_any + 1);
            CHECK(local.value.load() == 21);
            CHECK(compute.invoke(local, 9).get<int>() == 18);
            CHECK(compute.invoke(local, connection_type_flag::DIRECT, 10).get<int>() == 20);

            // (b) A void entry: VALID, but declared only — the type is void and there is no storage,
            //     which is exactly what separates it from the invalid state.
            const any_t void_result = local_meta.invoke(local, "set_value(int)", 4);
            CHECK(void_result.is_valid());
            CHECK(!void_result.is_initialized());
            CHECK(void_result.type().is<void>());
            CHECK(local.value.load() == 4);

            // (c) A miss is the INVALID state (not the declared-only void one).
            CHECK(!local_meta.invoke(local, "no_such_method()").is_valid());
            CHECK(!local_meta.method("no_such_method()").invoke(local).is_valid());

            // (d) A QUEUED call hands its value back as well — just later. It is still a success when
            //     it is posted, with the return type DECLARED ONLY; the event then fills that very
            //     variable's storage, so the value is readable once the queue has been processed.
            //     The variable is NOT moved or copied between the call and the drain: the event holds
            //     its address (a declared-only move would take the storage away from it).
            any_t posted = local_meta.invoke(local, "compute(int)", connection_type_flag::QUEUED, 5);
            CHECK(posted.is_valid());
            CHECK(!posted.is_initialized());                           // nothing has run yet
            CHECK(posted.type().is<int>());
            event_dispatcher::instance().send_posted_events();
            CHECK(local.value.load() == 5);                            // it did run, on the queue
            CHECK(posted.is_initialized());                            // ... and the event marked it
            CHECK(posted.get<int>() == 10);                            // 5 * 2, produced by the event

            // (e) A refused queued call is INVALID: a value type has no thread affinity to post to.
            const std::size_t before_any_refusal = log_records().size();
            CHECK(!invoke_value_t::static_meta_object.invoke(value_object, "doubled(int) const",
                      connection_type_flag::QUEUED, 5).is_valid());
            CHECK(log_records().size() == before_any_refusal + 1);
            CHECK(log_records().back().message.find("thread affinity") != std::string::npos);
            CHECK(local.calls.load() == calls_before_any + 5);
        }

        // ---- (17g2) Queued with a return value: the event fills the slot when it RUNS ----------
        // The contract of the change: the call is posted and reports success; nothing is refused and
        // nothing is logged. The non-Ret form answers a DECLARED-ONLY any_t, the `Ret&` form leaves the
        // caller's variable untouched — in both cases the event holds an ADDRESS, and the value shows up
        // only after the queue has been processed.
        {
            const int calls_before_queued = local.calls.load();
            const std::size_t records_before_queued = log_records().size();

            // (i) Non-Ret: VALID but not INITIALISED on return, and the row's type is declared. The
            //     variable must NOT be moved or copied before the event runs: what the event points at
            //     is this variable's reserved storage.
            any_t queued_result = local_meta.invoke(local, "compute(int)", connection_type_flag::QUEUED, 21);
            CHECK(queued_result.is_valid());
            CHECK(!queued_result.is_initialized());
            CHECK(queued_result.type().is<int>());
            CHECK(local.calls.load() == calls_before_queued);          // posted, not run
            event_dispatcher::instance().send_posted_events();
            CHECK(queued_result.get<int>() == 42);                     // the value appears after processing
            CHECK(local.calls.load() == calls_before_queued + 1);      // the body ran exactly once
            CHECK(local.value.load() == 21);

            // (ii) Ret&: the caller's own variable is handed to the event by address. It stays at its
            //      initial value until the event runs, then holds the result.
            int out = -1;
            CHECK(local_meta.invoke(local, "compute(int)", out, connection_type_flag::QUEUED, 21));
            CHECK(out == -1);                                          // untouched until the event runs
            CHECK(local.calls.load() == calls_before_queued + 1);      // still only the posted one above
            event_dispatcher::instance().send_posted_events();
            CHECK(out == 42);
            CHECK(local.calls.load() == calls_before_queued + 2);

            // (iii) The old refusal is GONE: a queued value-returning call logs NOTHING (the handler is
            //       installed for the whole section, so any "cannot deliver the return value" record
            //       would be counted here).
            CHECK(log_records().size() == records_before_queued);
        }

        // ---- (17g3) the ERASED family also takes the connection type (the two new forms) ----------
        // The value channel (one any_t per parameter) now mirrors the typed channel exactly:
        //   invoke(obj, args)              -> Direct only (unchanged)
        //   invoke(obj, type, args)        -> NEW: caller-chosen connection type
        //   invoke(obj, ret, args)         -> Direct only (out slot before the args, unchanged position)
        //   invoke(obj, ret, type, args)   -> NEW
        // Both new forms go through invoke_erased(), the very dispatch the typed forms use, so the
        // refusal rules ("Queued on a value type", "BlockingQueued on the calling thread") stay one
        // implementation; the queued copies are made by the entry's declared parameter types.
        {
            const int calls_before_erased = local.calls.load();
            const any_t erased_args[] = { any_t(21) };
            const std::span<const any_t> erased_span = erased_args;

            // (i) Direct, no out slot: the value comes back and the row really ran.
            const any_t erased_direct = compute.invoke(local, connection_type_flag::DIRECT, erased_span);
            CHECK(erased_direct.is_valid());
            CHECK(erased_direct.is_initialized());
            CHECK(erased_direct.get<int>() == 42);
            CHECK(local.calls.load() == calls_before_erased + 1);

            // (ii) Direct with a caller slot: the erased return-type rule applies and the slot is
            //      marked by the caller (this call ran synchronously).
            any_t erased_slot(meta_type_t::from_type<int>());
            CHECK(compute.invoke(local, erased_slot, connection_type_flag::DIRECT, erased_span));
            CHECK(erased_slot.is_initialized());
            CHECK(erased_slot.get<int>() == 42);
            CHECK(local.calls.load() == calls_before_erased + 2);

            // (iii) Queued, no out slot: posted, so DECLARED ONLY on return; the event fills and marks
            //       that very variable once the queue is drained (the previous phase's contract).
            any_t erased_queued = compute.invoke(local, connection_type_flag::QUEUED, erased_span);
            CHECK(erased_queued.is_valid());
            CHECK(!erased_queued.is_initialized());
            CHECK(erased_queued.type().is<int>());
            CHECK(local.calls.load() == calls_before_erased + 2);      // posted, not run yet
            event_dispatcher::instance().send_posted_events();
            CHECK(erased_queued.is_initialized());
            CHECK(erased_queued.get<int>() == 42);
            CHECK(local.calls.load() == calls_before_erased + 3);

            // (iv) Queued with a caller slot: untouched until the event ran, then holding the result —
            //      marked by the EVENT, which is the only place that knows when it wrote.
            any_t erased_out_queued(meta_type_t::from_type<int>());
            CHECK(compute.invoke(local, erased_out_queued, connection_type_flag::QUEUED, erased_span));
            CHECK(!erased_out_queued.is_initialized());
            CHECK(local.calls.load() == calls_before_erased + 3);
            event_dispatcher::instance().send_posted_events();
            CHECK(erased_out_queued.is_initialized());
            CHECK(erased_out_queued.get<int>() == 42);
            CHECK(local.calls.load() == calls_before_erased + 4);

            // (v) A null pointer target answers the INVALID any_t / false without calling — the same
            //     "there is no object" rule the Direct erased forms follow.
            invoke_target_t* const null_target = nullptr;
            any_t null_slot(meta_type_t::from_type<int>());
            CHECK(!compute.invoke(null_target, connection_type_flag::DIRECT, erased_span).is_valid());
            CHECK(!compute.invoke(null_target, null_slot, connection_type_flag::DIRECT, erased_span));
            CHECK(!null_slot.is_initialized());
            CHECK(local.calls.load() == calls_before_erased + 4);

            // (vi) The erased return-type rule is UNCHANGED: a slot whose bare type is not the row's
            //      return type throws bad_any_cast_t (the erased channel's diagnostic, never the typed
            //      Critical) and the row does not run.
            any_t wrong_erased_slot(meta_type_t::from_type<double>());
            bool threw_wrong_erased_type = false;
            try {
                (void)compute.invoke(local, wrong_erased_slot, connection_type_flag::DIRECT, erased_span);
            }
            catch (const bad_any_cast_t&) {
                threw_wrong_erased_type = true;
            }
            CHECK(threw_wrong_erased_type);
            CHECK(!wrong_erased_slot.is_initialized());
            CHECK(local.calls.load() == calls_before_erased + 4);

            // (vii) A void row on the tagged erased no-out form: no slot is built (declare_return_slot
            //       answers the invalid any_t), so the success shape is the DECLARED-ONLY void one.
            const any_t void_args[] = { any_t(3) };
            const any_t erased_void = local_meta.method("set_value(int)")
                .invoke(local, connection_type_flag::DIRECT, std::span<const any_t>{ void_args });
            CHECK(erased_void.is_valid());
            CHECK(!erased_void.is_initialized());
            CHECK(erased_void.type().is<void>());
            CHECK(local.value.load() == 3);
            CHECK(local.calls.load() == calls_before_erased + 5);
        }

        // ---- (17g4) the whole handle-level family pinned at COMPILE time --------------------------
        // The order is obj, (out), connectiontype, args — at BOTH levels. These assertions name the
        // API, so a future reorder breaks THIS test instead of silently changing what a call means
        // (a swapped out slot and object still compiles when both are same-shaped lvalues).
        {
            int int_out = 0;
            any_t any_out(meta_type_t::from_type<int>());
            const any_t args_one[] = { any_t(1) };
            const std::span<const any_t> args_span = args_one;

            // typed, no out slot: Auto and tagged
            static_assert(std::is_same_v<decltype(compute.invoke(local)), any_t>);
            static_assert(std::is_same_v<decltype(compute.invoke(local, 9)), any_t>);
            static_assert(std::is_same_v<decltype(compute.invoke(local, connection_type_flag::DIRECT)), any_t>);
            // typed, out slot: Auto and tagged — the NON-variadic overload is the more specialised
            // one, so a plain lvalue in the out position selects it (never the any_t form).
            static_assert(std::is_same_v<decltype(compute.invoke(local, int_out)), bool>);
            static_assert(std::is_same_v<decltype(compute.invoke(
                local, int_out, connection_type_flag::DIRECT)), bool>);
            // erased, no out slot: Direct and tagged
            static_assert(std::is_same_v<decltype(compute.invoke(local, args_span)), any_t>);
            static_assert(std::is_same_v<decltype(compute.invoke(
                local, connection_type_flag::DIRECT, args_span)), any_t>);
            // erased, out slot: Direct and tagged
            static_assert(std::is_same_v<decltype(compute.invoke(local, any_out, args_span)), bool>);
            static_assert(std::is_same_v<decltype(compute.invoke(
                local, any_out, connection_type_flag::DIRECT, args_span)), bool>);
            // ... and the enum tag is accepted where a connection_type_t parameter is wanted.
            static_assert(std::is_same_v<decltype(compute.invoke(
                local, connection_type_flag::DIRECT, 21)), any_t>);
            // The positional-trap ESCAPE: naming the connection type keeps an LVALUE in the argument
            // pack (the tag form), instead of letting it bind to the out slot of the two-argument form.
            int lvalue_arg = 21;
            static_assert(std::is_same_v<decltype(compute.invoke(
                local, connection_type_flag::DIRECT, lvalue_arg)), any_t>);
        }

        // ---- (17g5) the OBJECT-level erased family: same order, same delegation -------------------
        // The table level now spells the whole boxed cross product the handle level pinned above:
        //   call  (obj, name,      [out], [type], args)  — the two tagged twins are new
        //   invoke(obj, signature, [out], [type], args)  — all four are new
        // Nothing is re-implemented: `call` resolves the name with resolve_method(); `invoke` looks the
        // row up with method(); both then hand the span to the handle's matching erased form. So the
        // connection decision, the return slot and the argument match are the SAME code (17g3) pinned,
        // and the object level answers exactly what the handle answers for the same call.
        {
            const any_t args_one[] = { any_t(21) };
            const std::span<const any_t> erased_args = args_one;
            const int calls_before_object_erased = local.calls.load();

            // (i) call by name, Direct: the value comes back.
            const any_t by_name = local_meta.call(local, "compute", connection_type_flag::DIRECT, erased_args);
            CHECK(by_name.is_valid());
            CHECK(by_name.is_initialized());
            CHECK(by_name.get<int>() == 42);
            CHECK(local.calls.load() == calls_before_object_erased + 1);

            // (ii) invoke by signature, UNTAGGED erased form: it is Direct (the erased contract, not
            //      the typed Auto one) and answers the same row and value — one implementation.
            const any_t by_signature = local_meta.invoke(local, "compute(int)", erased_args);
            CHECK(by_signature.is_valid());
            CHECK(by_signature.get<int>() == 42);
            CHECK(local.calls.load() == calls_before_object_erased + 2);

            // (iii) call by name, QUEUED: declared only on return, then filled by the event (which is
            //       the only place that knows when it wrote) once the queue is drained.
            any_t named_queued = local_meta.call(local, "compute", connection_type_flag::QUEUED, erased_args);
            CHECK(named_queued.is_valid());
            CHECK(!named_queued.is_initialized());
            CHECK(named_queued.type().is<int>());
            CHECK(local.calls.load() == calls_before_object_erased + 2);   // posted, not run
            event_dispatcher::instance().send_posted_events();
            CHECK(named_queued.is_initialized());
            CHECK(named_queued.get<int>() == 42);
            CHECK(local.calls.load() == calls_before_object_erased + 3);

            // (iv) invoke by signature, QUEUED with the caller's slot: untouched until the event ran.
            any_t named_slot(meta_type_t::from_type<int>());
            CHECK(local_meta.invoke(local, "compute(int)", named_slot, connection_type_flag::QUEUED,
                erased_args));
            CHECK(!named_slot.is_initialized());
            CHECK(local.calls.load() == calls_before_object_erased + 3);
            event_dispatcher::instance().send_posted_events();
            CHECK(named_slot.is_initialized());
            CHECK(named_slot.get<int>() == 42);
            CHECK(local.calls.load() == calls_before_object_erased + 4);

            // (v) the out-slot twins, both names, untagged and tagged: four forms, one value shape.
            any_t call_direct_slot(meta_type_t::from_type<int>());
            CHECK(local_meta.call(local, "compute", call_direct_slot, erased_args));
            CHECK(call_direct_slot.get<int>() == 42);
            any_t call_tagged_slot(meta_type_t::from_type<int>());
            CHECK(local_meta.call(local, "compute", call_tagged_slot, connection_type_flag::DIRECT,
                erased_args));
            CHECK(call_tagged_slot.get<int>() == 42);
            any_t invoke_direct_slot(meta_type_t::from_type<int>());
            CHECK(local_meta.invoke(local, "compute(int)", invoke_direct_slot, erased_args));
            CHECK(invoke_direct_slot.get<int>() == 42);
            any_t invoke_tagged_slot(meta_type_t::from_type<int>());
            CHECK(local_meta.invoke(local, "compute(int)", invoke_tagged_slot,
                connection_type_flag::DIRECT, erased_args));
            CHECK(invoke_tagged_slot.get<int>() == 42);
            CHECK(local.calls.load() == calls_before_object_erased + 8);

            // (vi) a VOID row through the tagged erased invoke: the DECLARED-ONLY void shape (valid,
            //      not initialised), never the invalid one.
            const any_t void_args[] = { any_t(9) };
            const any_t erased_void = local_meta.invoke(local, "set_value(int)",
                connection_type_flag::DIRECT, std::span<const any_t>{ void_args });
            CHECK(erased_void.is_valid());
            CHECK(!erased_void.is_initialized());
            CHECK(erased_void.type().is<void>());
            CHECK(local.value.load() == 9);
            CHECK(local.calls.load() == calls_before_object_erased + 9);

            // (vii) The erased MISS: `invoke` has no resolver to report it — method() is a silent
            //       lookup — so the TABLE level reports it itself: exactly ONE Warning with the
            //       check_invoke_args() wording (so the typed and the erased channel answer a typo with
            //       the same sentence), and the invalid any_t / false.
            const std::size_t before_erased_miss = log_records().size();
            CHECK(!local_meta.invoke(local, "no_such_method()", erased_args).is_valid());
            CHECK(log_records().size() == before_erased_miss + 1);
            if (log_records().size() == before_erased_miss + 1) {
                const record_t& record = log_records().back();
                std::printf("  log: [%s] %s\n", record.category.c_str(), record.message.c_str());
                CHECK(record.category == "metaobject.connection");
                CHECK(record.level == log_level_enum::WARNING);
                CHECK(record.message.find("no_such_method()") != std::string::npos);
            }
            any_t miss_slot(meta_type_t::from_type<int>());
            CHECK(!local_meta.invoke(local, "no_such_method()", miss_slot, erased_args));
            CHECK(!miss_slot.is_initialized());
            CHECK(log_records().size() == before_erased_miss + 2);

            // (viii) The erased ARGUMENT MISMATCH is a different channel: the row EXISTS and the boxed
            //        list does not fit it, so the handle throws bad_any_cast_t — that throw IS the erased
            //        argument check, and nothing is logged on top of it (no second report).
            const any_t wrong_args[] = { any_t(std::string("nope")) };
            const std::size_t before_wrong_erased_args = log_records().size();
            bool threw_wrong_erased_args = false;
            try {
                (void)local_meta.invoke(local, "compute(int)", std::span<const any_t>{ wrong_args });
            }
            catch (const bad_any_cast_t&) {
                threw_wrong_erased_args = true;
            }
            CHECK(threw_wrong_erased_args);
            CHECK(log_records().size() == before_wrong_erased_args);
            CHECK(local.calls.load() == calls_before_object_erased + 9);   // nothing ran
        }

        // ---- (17h) the receiver is destroyed while a BlockingQueued invoke is still queued ----
        // Qt releases the waiter from the EVENT DESTRUCTOR (QAbstractMetaCallEvent::~QAbstractMetaCallEvent,
        // qtbase/src/corelib/kernel/qobject.cpp:474-482), so every way an event can die unexecuted
        // releases the poster: the receiver's teardown purge (~object_t -> remove_object()), a
        // removePostedEvents()-style cleanup, or a delete after a failed dispatch. Here the worker
        // destroys the object WITHOUT draining its queue: the wait must return (not hang), the dropped
        // call must be reported as false, and the caller's result slot must be untouched.
        worker->paused = 1;
        CHECK(wait_until([&] {
            return worker->parked.load() != 0;
        }));
        invoke_target_t* const cancel_target = remote;                // destroyed below by the worker
        invoke_target_t* const control_target = worker->control;      // positive control
        CHECK(cancel_target != nullptr);
        CHECK(control_target != nullptr);
        if (cancel_target != nullptr && control_target != nullptr) {
            // object_t::thread() answers the public thread identity; the dispatcher is resolved from it
            // (a thread that has data already created its dispatcher, so the pointer is never null).
            using thread_ptr = decltype(std::declval<object_t&>().thread());
            thread_ptr cancel_thread = cancel_target->thread();
            event_dispatcher* cancel_dispatcher = event_dispatcher::instance(cancel_thread);
            const std::size_t pending_before = cancel_dispatcher->pending_event_count();
            const int compute_before = g_compute_calls.load();

            // The blocking call runs on a poster thread: the test thread has to stay free to command
            // the destruction, and watches the poster with a timeout instead of joining it blindly.
            invoke_poster_t poster;
            poster.meta = cancel_target->meta_object();
            poster.target = cancel_target;
            std::thread poster_thread([&poster] {
                poster.started = 1;
                const bool ok = poster.meta->invoke(*poster.target, "compute(int)", poster.result,
                    connection_type_flag::BLOCKING_QUEUED, 21);
                poster.returned = ok ? 1 : 0;
                poster.finished = 1;
            });
            CHECK(wait_until([&] {
                return poster.started.load() != 0;
            }));
            // Fact 1: the call is really queued in the receiver's (parked) thread before teardown.
            CHECK(wait_until([&] {
                return cancel_dispatcher->pending_event_count() > pending_before;
            }));

            // Fact 2: destroy the receiver without processing the queue.
            worker->destroy_requested = 1;
            CHECK(wait_until([&] {
                return worker->destroyed.load() != 0;
            }));
            CHECK(worker->parked.load() != 0);                        // the queue was never drained
            CHECK(g_compute_calls.load() == compute_before);          // the method never ran
            CHECK(wait_until([&] {                                    // ... and the event was purged
                return cancel_dispatcher->pending_event_count() == pending_before;
            }));

            // Fact 3: the waiter woke (watchdog: a regression fails instead of hanging the test) and
            // the cancelled call was reported as a failure.
            const bool woke = wait_until([&] {
                return poster.finished.load() != 0;
            }, 5000);
            if (!woke) {
                std::printf("  [FAIL] BlockingQueued waiter did not return after the receiver was "
                            "destroyed (HUNG)\n");
                std::fflush(stdout);
                std::_Exit(1);
            }
            poster_thread.join();
            CHECK(poster.finished.load() == 1);
            CHECK(poster.returned.load() == 0);                       // cancelled, not success
            CHECK(poster.result == 0);                                // result slot untouched

            // Fact 4: a normal BlockingQueued delivery still reports success and runs exactly once.
            worker->paused = 0;
            int control_doubled = 0;
            CHECK(control_target->meta_object()->invoke(*control_target, "compute(int)",
                      control_doubled, connection_type_flag::BLOCKING_QUEUED, 21));
            CHECK(control_doubled == 42);
            CHECK(control_target->calls.load() == 1);
            CHECK(control_target->last_thread == worker->worker_thread);
            CHECK(control_target->last_thread != std::this_thread::get_id());
            CHECK(g_compute_calls.load() == compute_before + 1);
        }

        worker->stop = 1;
        CHECK(worker->wait(5000));
    }

    // (18) The OBJECT's own cvref, through every entry point that can spell one. C++'s implicit-object
    //      parameter rule applies to a meta-invocation exactly as it does to a real call: a const
    //      object can only call const members, a volatile object only volatile members, `&` demands an
    //      lvalue and `&&` an rvalue. Before the consolidation the const-object case was checked in the
    //      table's matcher but the typed/erased object-level paths only compared ARGUMENTS, so
    //      `const obj` could run a non-const member. All four entry points now refuse it:
    //        (a) the typed by-name path  — meta_object_t::invoke -> check_invoke_args -> check_args;
    //        (b) the erased (boxed) path — meta_method_t's any_t span -> meta_method_t::method_matches_boxed;
    //        (c) the resolver            — meta_object_t::resolve_method -> meta_info_t::resolve_method;
    //        (d) a raw handle's invoke   — meta_method_t::invoke -> check_args.
    //      Volatile cannot be spelled at THIS level: every object-level form erases the address with
    //      static_cast<const void*>(&obj), which is ill-formed for a volatile pointer (a compile-time
    //      limit of the void* channel, not of the rule). It is pinned at the handle criterion here and
    //      at all three table-level entry points in M25 (12).
    std::printf("-- (18) object cvref: typed / erased / handle paths --\n");
    {
        cvref_target_t obj;
        obj.set_parent(root);
        const meta_object_t& meta = *obj.meta_object();
        const cvref_target_t& const_obj = obj;

        // ---- (a) the typed by-name path ----
        // Legal: non-const object -> non-const member, and -> const member.
        CHECK(meta.invoke(obj, "value(int)", connection_type_flag::DIRECT, 7).is_valid());
        CHECK(meta.invoke(obj, "get_const() const", connection_type_flag::DIRECT).is_valid());
        // Legal: const object -> const member.
        CHECK(meta.invoke(const_obj, "get_const() const", connection_type_flag::DIRECT).is_valid());
        // Refused: const object -> NON-const member. check_invoke_args reports it with ONE Warning and
        // nothing runs.
        {
            const std::size_t before = log_records().size();
            CHECK(!meta.invoke(const_obj, "value(int)", connection_type_flag::DIRECT, 7).is_valid());
            CHECK(log_records().size() == before + 1);
            if (log_records().size() == before + 1) {
                const record_t& record = log_records().back();
                std::printf("  log: [%s] %s\n", record.category.c_str(), record.message.c_str());
                CHECK(record.level == log_level_enum::WARNING);
                CHECK(record.message.find("accepts these arguments") != std::string::npos);
            }
        }
        // Refused: an rvalue object -> `&` member (and an lvalue object -> `&&` member, below).
        CHECK(!meta.invoke(std::move(obj), "get_lref() &", connection_type_flag::DIRECT).is_valid());

        // ---- (d) the raw handle's typed invoke ----
        const meta_method_t value_row = meta.method("value(int)");
        const meta_method_t const_row = meta.method("get_const() const");
        CHECK(value_row.is_valid());
        CHECK(const_row.is_valid());
        CHECK(value_row.invoke(obj, connection_type_flag::DIRECT, 7).is_valid());
        CHECK(!value_row.invoke(const_obj, connection_type_flag::DIRECT, 7).is_valid());
        CHECK(const_row.invoke(const_obj, connection_type_flag::DIRECT).is_valid());
        // The same criterion, stated directly: the handle's check_args carries the object qualifier.
        CHECK(value_row.check_args<int>(cvref_meta_type_t::from<cvref_target_t&>().qualifier));
        CHECK(!value_row.check_args<int>(cvref_meta_type_t::from<const cvref_target_t&>().qualifier));
        CHECK(!value_row.check_args<int>(
            cvref_meta_type_t::from<volatile cvref_target_t&>().qualifier));

        // ---- (b) the erased (boxed) path: the handle's any_t entry ----
        const any_t boxed_args[] = { any_t(7) };
        const std::span<const any_t> erased = boxed_args;
        CHECK(value_row.invoke(obj, connection_type_flag::DIRECT, erased).is_valid());
        // A const object is refused this non-const row; the erased channel's bad match THROWS.
        bool threw = false;
        try {
            (void)value_row.invoke(const_obj, connection_type_flag::DIRECT, erased);
        }
        catch (const bad_any_cast_t&) {
            threw = true;
        }
        CHECK(threw);
        // A const member is accepted from the const object through the same erased channel.
        CHECK(const_row.invoke(const_obj, connection_type_flag::DIRECT, std::span<const any_t>{})
                  .is_valid());

        // ---- (c) the resolver ----
        const std::array<cvref_meta_type_t, 1> int_args{ cvref_meta_type_t::from<int>() };
        CHECK(meta.resolve_method<cvref_target_t&>("value", int_args).is_valid());
        CHECK(!meta.resolve_method<const cvref_target_t&>("value", int_args).is_valid());
        // Volatile and const-volatile objects are refused by the same resolver (the table-level
        // entry points are exercised with the same qualifiers in M25 (12)).
        CHECK(!meta.resolve_method<volatile cvref_target_t&>("value", int_args).is_valid());

        // ---- no regression: the legal cvref shapes still run ----
        CHECK(meta.invoke(obj, "get_lref() &", connection_type_flag::DIRECT).is_valid());          // lvalue -> &
        CHECK(meta.invoke(std::move(obj), "get_rref() &&", connection_type_flag::DIRECT).is_valid()); // rvalue -> &&
        CHECK(meta.invoke(const_obj, "get_const() const", connection_type_flag::DIRECT).is_valid());   // const -> const
        CHECK(meta.resolve_method<cvref_target_t&>("get_lref").is_valid());
        CHECK(meta.resolve_method<cvref_target_t>("get_rref").is_valid());
        CHECK(meta.resolve_method<const cvref_target_t&>("get_const").is_valid());
        std::printf("  const / lvalue / rvalue shapes agree across all four entry points\n");
    }

    install_log_handler({ });

    std::printf(g_failures == 0 ? "  M13: all %d checks passed\n"
                                : "  M13: %d check(s) failed of %d\n",
                g_failures == 0 ? g_checks : g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}