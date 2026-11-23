#include <meta>
#include <iostream>
#include <functional>
#include <atomic>
#include <thread>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>

#include <hwshqtb/metaobject/annotation.h>
#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/dynamic_base_t.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/event_t.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_enum_builder_t.h>
#include <hwshqtb/metaobject/meta_enum_t.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_object_builder_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/meta_property_builder_t.h>
#include <hwshqtb/metaobject/meta_type_t.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/property_t.h>
#include <hwshqtb/metaobject/signal_blocker_t.h>
#include <hwshqtb/metaobject/signal_spy_t.h>
#include <hwshqtb/metaobject/thread_t.h>
#include <hwshqtb/metaobject/timer_t.h>
#include <hwshqtb/metaobject/type_interface_t.h>

using namespace hwshqtb::metaobject;

// ---------------------------------------------------------------------------
// A custom conversion function: int -> std::string (hexadecimal).
// std::string cannot be constructed from int, so the automatic overload necessarily fails; only the
// custom overload can register this conversion.
// ---------------------------------------------------------------------------
std::string to_hex(const int& v) {
    static constexpr char digits[] = "0123456789abcdef";
    const bool neg = v < 0;
    unsigned int u = neg ? 0u - static_cast<unsigned int>(v) : static_cast<unsigned int>(v);
    std::string rev;
    if (u == 0) {
        rev += '0';
    }
    while (u != 0) {
        rev += digits[u % 16u];
        u /= 16u;
    }
    std::string out = neg ? "-0x" : "0x";
    out.append(rev.rbegin(), rev.rend());
    return out;
}

// ---------------------------------------------------------------------------
// Person: a property (read / write / notify / reset), a signal and a plain method.
// ---------------------------------------------------------------------------
struct [[= make_class_info("Author", "hwshqtb")]] Person : object_t {
    OBJECT
public:
    // The value property: reader=value(), writer=setValue(), notifier=valueChanged(),
    // reseter=resetValue() — accessors are named by BARE NAME and matched by reflection.
    [[=make_property("value", "setValue", "valueChanged", "resetValue")]]
    double value() const { return _value; }

    void setValue(double v) {
        _value = v;
        valueChanged(v);   // emit: the signal body injected by SIGNAL dispatches via activate_signal
    }

    void resetValue() {
        _value = 0.0;
        valueChanged(_value);
    }

    SIGNAL(void valueChanged(double value) const)   // signal: the macro body resolves itself at compile time and dispatches

    int add(int a, int b) const { return a + b; }

    // [[= make_hidden()]]: declares "do not observe this" - no func_info is generated, so reflection
    // cannot find it by signature either (M6 demonstration).
    [[= make_hidden()]]
    int secret_scratch() { return 7; }

    // M5: enums declared inside the type. Mode gives each value its own bit (a flag enum), while
    // Color has overlapping values (not a flag enum).
    enum struct Mode { None = 0, Fast = 1, Safe = 2, Deep = 4 };
    enum Color { Red = 1, Green = 2, Blue = 3 };

    double _value = 0.0;
};
META_OBJECT(Person);

// ---------------------------------------------------------------------------
// Employee: inherits Person, used to exercise the inheritance chain (looking up methods / properties
// along the chain).
// ---------------------------------------------------------------------------
struct Employee : Person {
    OBJECT
public:
    [[=make_property("salary")]]
    double salary() const { return _salary; }

    void promote() { std::cout << "  [slot-ish] promote()\n"; }

    double _salary = 1000.0;
};
META_OBJECT(Employee);

// ---------------------------------------------------------------------------
// Receiver: the target of a connection slot.
// ---------------------------------------------------------------------------
struct Receiver : object_t {
    OBJECT
public:
    double last = 0.0;
    int calls = 0;
    void onChanged(double v) { last = v; ++calls; }
};
META_OBJECT(Receiver);

// ---------------------------------------------------------------------------
// For M7: a custom event type (>= event_type_enum::USER) plus the matching receiver / filter /
// deferred-delete objects.
// ---------------------------------------------------------------------------
// Register a user event type at run time (QEvent::registerEventType).
const event_type_enum tick_event_type = event_t::register_event_type();

// Receiver of the custom event: counts how often custom_event is reached.
struct Probe : object_t {
    OBJECT
public:
    int custom_seen = 0;
    void custom_event(event_t* e) override {
        if (e->type() == tick_event_type) {
            ++custom_seen;
            e->accept();
            return;
        }
        object_t::custom_event(e);
    }
};
META_OBJECT(Probe);

// Event filter: when intercept is true it consumes the custom event, so Probe::custom_event is no
// longer reached.
struct EventFilter : object_t {
    OBJECT
public:
    bool intercept = false;
    bool event_filter(object_t* watched, event_t* e) override {
        (void)watched;
        if (intercept && e->type() == tick_event_type) {
            e->accept();
            return true;
        }
        return false;
    }
};
META_OBJECT(EventFilter);

// Deferred-delete counter: proves that delete_later() really destroyed the object.
int deletable_destroyed = 0;

struct Deletable : object_t {
    OBJECT
public:
    ~Deletable() override { ++deletable_destroyed; }
};
META_OBJECT(Deletable);

// Queued quit slot: requests a quit once inside the event loop (verifies the exec()/quit() cycle).
struct Quitter : object_t {
    OBJECT
public:
    int calls = 0;
    void requestQuit(double) {
        ++calls;
        core_application::quit();
    }
};
META_OBJECT(Quitter);

// ---------------------------------------------------------------------------
// M8: a worker thread with its own event loop and its own receiver.
// ---------------------------------------------------------------------------
struct Worker : thread_t {
    OBJECT
public:
    Receiver* receiver = nullptr;
    std::atomic<int> ready{0};
    std::atomic<int> stop{0};

    void run() override {
        // Created on THIS thread: its affinity is the worker's, and it must share the root's type
        // registry (set_parent does not change the affinity).
        receiver = new Receiver;
        receiver->set_parent(core_application::instance());
        ready = 1;
        while (stop.load() == 0) {
            // Only the posting queue: the native pump belongs to the main thread.
            event_dispatcher::instance().send_posted_events();
            std::this_thread::yield();
        }
    }
};
META_OBJECT(Worker);

/// M9: counts timer ticks.
struct TickCounter : object_t {
    OBJECT
public:
    int ticks = 0;
    void on_timeout() { ++ticks; }
};
META_OBJECT(TickCounter);

/// The dynamic type used by the M15 section: object_t + the mixin.
class demo_dynamic_t : public object_t, public dynamic_base_t {
public:
    explicit demo_dynamic_t(const meta_object_t* meta, object_t* parent = nullptr)
        : dynamic_base_t(meta) {
        if (parent != nullptr) {
            set_parent(parent);
        }
    }
    DYNAMIC_BASE
};
// --- M15: thunks for the dynamically built type (plain function pointers, no library machinery) ---

void dyn_read_value(void* obj, void** params) {
    const any_t stored = static_cast<demo_dynamic_t*>(obj)->dynamic_value("value");
    *static_cast<double*>(params[0]) = stored.is_initialized() ? stored.get<double>() : 0.0;
}

void dyn_write_value(void* obj, void** params) {
    static_cast<demo_dynamic_t*>(obj)->set_dynamic_value(
        "value", any_t(*static_cast<const double*>(params[1])));
}

int main(int argc, char** argv) {
    // core_application: construction registers the global instance (QCoreApplication style); every
    // demo object below is set_parent'ed to it, so they share one type registry (context climbs to
    // _root).
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    // Application metadata (QCoreApplication static metadata)
    core_application::set_application_name("metaobject-demo");
    core_application::set_application_version("1.0.0");
    core_application::set_organization_name("hwshqtb");
    core_application::set_organization_domain("example.com");

    // Start-up registration: CONVERSIONS only (two automatic + one custom). Types need no
    // registration at all — each type's interface is produced on demand from its own reflection and
    // the one process-wide registry assigns ids lazily.
    register_conversion<int, double>();
    register_conversion<double, int>();
    register_conversion<const char*, std::string>();
    const bool custom_ok = register_conversion<int, std::string>(&to_hex);

    std::cout << "== core_application (QCoreApplication style) ==\n";
    std::cout << "instance()==&app = " << (core_application::instance() == &app)
              << "  argc=" << app.argc()
              << "  argv[0]=" << (app.argc() > 0 && app.argv() ? app.argv()[0] : "") << "\n";
    std::cout << "app: name=" << core_application::application_name()
              << " version=" << core_application::application_version()
              << " org=" << core_application::organization_name()
              << " domain=" << core_application::organization_domain() << "\n";

    Person* p = new Person;
    p->set_parent(root);          // shares root's registry; root deletes it automatically
    Employee* e = new Employee;
    e->set_parent(root);
    Receiver* recv = new Receiver;
    recv->set_parent(root);

    // =========================================================================
    // M0: any_t - value / type / conversion (automatic + custom) / alias / exceptions
    // =========================================================================
    std::cout << "\n== M0: any_t ==\n";

    any_t a = 42;                                   // implicit construction (the global context resolves int)
    std::cout << "any<int>: is<int>=" << a.is<int>()
              << " get<int>=" << a.get<int>()
              << " type.name=" << a.type().name() << "\n";

    any_t s = std::string("hello");                 // a std::string value
    std::cout << "any<string>: get<string>=" << s.get<std::string>() << "\n";

    // Automatic conversions: double -> int / int -> double / const char* -> std::string
    any_t d = 3.14;
    std::cout << "any<double>(3.14).as<int>() = " << d.as<int>() << "\n";
    std::cout << "any<int>(42).as<double>() = " << any_t(42).as<double>() << "\n";
    any_t cs = "hi";                                 // a const char* value (an array argument decays to a pointer)
    std::cout << "any<const char*>.as<string>() = " << cs.as<std::string>() << "\n";

    // Custom conversion: int -> std::string (hexadecimal)
    std::cout << "register_conversion<int,std::string>(&to_hex) = " << custom_ok << "\n";
    std::cout << "custom convert: any<int>(255).as<string>() = "
              << any_t(255).as<std::string>() << "\n";

    // Alias (non-owning): std::ref / std::cref
    int original = 100;
    any_t ref = std::ref(original);                 // alias
    ref.get<int&>() = 200;                          // write back to the original through the alias
    std::cout << "std::ref write-through: original=" << original
              << " has LValueRef=" << bool(ref.qualifier() & qualifier_flag::LVALUE_REF) << "\n";

    const int cval = 7;
    any_t cref = std::cref(cval);
    std::cout << "std::cref get<const int&>=" << cref.get<const int&>()
              << " has Const=" << bool(cref.qualifier() & qualifier_flag::CONST) << "\n";

    // Exceptions. The variable is an explicit VOID declaration: a default-constructed any_t is the
    // INVALID state (no type at all) and reports bad_meta_type_t, while a declared-only void variable
    // is valid and reports "not built yet".
    try { any_t empty(meta_type_t::from_type<void>()); (void)empty.get<int>(); }
    catch (const bad_any_uninitialized_t&) { std::cout << "  [ok] reading an uninitialised value throws bad_any_uninitialized_t\n"; }

    try { any_t x = 42; (void)x.get<double>(); }
    catch (const bad_any_cast_t&) { std::cout << "  [ok] a type mismatch throws bad_any_cast_t\n"; }

    // =========================================================================
    // M2: meta_method - reflective invocation and introspection
    // =========================================================================
    std::cout << "\n== M2: meta_method ==\n";

    int r = 0;
    p->meta_object()->invoke(*p, "add(int,int) const", r, 3, 4);   // object-level invocation (walks the chain)
    std::cout << "invoke_method add(3,4) = " << r << "\n";

    auto add = p->meta_object()->method("add(int,int) const");
    std::cout << "method: name=" << add.name() << " sig=" << add.signature()
              << " params=" << add.param_count()
              << " ret=" << add.return_type_name()
              << " check_args<int,int>=" << add.check_args<int, int>()
              << " check_args<double>=" << add.check_args<double>() << "\n";

    std::cout << "method_count(Person) = " << p->meta_object()->method_count() << "\n";

    // make_hidden(): a marked member is not observed (no func_info is generated) and cannot be found by
    // signature either; add, by contrast, is found.
    std::cout << "hidden marker: method(\"secret_scratch()\").is_valid()="
              << p->meta_object()->method("secret_scratch()").is_valid()
              << "  method(\"add(int,int)\").is_valid()="
              << p->meta_object()->method("add(int,int) const").is_valid() << " (expected 0 / 1)\n";

    // =========================================================================
    // M1: meta_property - read / write / reset / metadata
    // =========================================================================
    std::cout << "\n== M1: meta_property ==\n";

    p->setValue(10.0);
    auto prop = p->meta_object()->property("value");        // handle lookup lives on the meta-object
    std::cout << "property: name=" << prop.name() << " type_name=" << prop.type_name()
              << " designable=" << prop.designable()
              << " writer.valid=" << prop.writer().is_valid()
              << " reseter.valid=" << prop.reseter().is_valid()
              << " notifier.valid=" << prop.notifier().is_valid() << "\n";

    // The three accessors are resolved by REFLECTION from the bare annotation names
    // ("setValue" / "valueChanged" / "resetValue"). If one stops resolving — a renamed function, a
    // parameter whose type no longer matches the property's type — say so loudly instead of letting
    // the demo quietly report a read-only / non-notifiable property.
    if (!prop.is_writable() || !prop.is_resettable() || !prop.is_notifiable()) {
        std::cout << "  [REGRESSION] property \"value\" did not resolve every accessor:"
                  << " writer=" << prop.is_writable()
                  << " reseter=" << prop.is_resettable()
                  << " notifier=" << prop.is_notifiable()
                  << " (expected 1 1 1; check that the bare names match the declarations and that"
                  << " the notifier's parameter type equals the property type double)\n";
    }
    // The matching rule requires the notifier's parameter to BE the property type. Print both so a
    // mismatch (for example a const-qualified parameter added to the signal) shows up as agree=0
    // instead of hiding behind notifier.valid=1.
    const std::string_view notifier_parameter = prop.notifier().param_type_name(0);
    std::cout << "  notifier: sig=" << prop.notifier().signature()
              << " param0=" << notifier_parameter
              << " property_type=" << prop.type_name()
              << " agree=" << (notifier_parameter == prop.type_name()) << "\n";

    any_t read_back = prop.read(*p);
    std::cout << "read() = " << read_back.get<double>() << "\n";
    std::cout << "object_t::property() reads the value directly = "
              << p->property("value").get<double>() << "\n";

    prop.write(*p, any_t(20.0));                    // via meta_property_t::write (writer=setValue, which emits)
    std::cout << "after write: value()=" << p->value() << "\n";

    p->set_property("value", any_t(30.0));          // convenience set_property
    std::cout << "after set_property: value()=" << p->value() << "\n";

    prop.reset(*p);                                 // reset (reseter=resetValue, which emits)
    std::cout << "after reset: value()=" << p->value() << "\n";

    // =========================================================================
    // M3: inheritance chain - class name / parent chain / inherits / lookup along the chain
    // =========================================================================
    std::cout << "\n== M3: inheritance ==\n";

    std::cout << "Employee class_name = " << e->meta_object()->class_name() << "\n";
    std::cout << "super_meta name = "
              << (e->meta_object()->has_super_class() ? e->meta_object()->super_class()->class_name() : std::string_view("null"))
              << "\n";
    std::cout << "e inherits Person = " << e->inherits("Person")
              << "  e inherits object_t = " << e->inherits("object_t") << "\n";

    auto inherited_add = e->meta_object()->method("add(int,int) const");   // look up a base-class method along the chain
    std::cout << "lookup along the chain Person::add = " << (inherited_add.is_valid() ? "found" : "miss") << "\n";

    std::cout << "Employee method_count(with inherited) = " << e->meta_object()->method_count()
              << "  property_count(with inherited) = " << e->meta_object()->property_count() << "\n";
    std::cout << "lookup along the chain property object_name = "
              << (e->meta_object()->property("object_name").is_valid() ? "found" : "miss") << "\n";

    std::cout << "method list (parents first):\n";
    for (index_t i = 0; i < e->meta_object()->method_count(); ++i) {
        auto mm = e->meta_object()->method(i);
        if (!mm.signature().empty()) std::cout << "  [" << i << "] " << mm.name() << "\n";
    }

    // =========================================================================
    // M5: enum metadata (meta_enum)
    // =========================================================================
    std::cout << "\n== M5: meta_enum ==\n";
    std::cout << "Person enum count (with inherited) = " << p->meta_object()->enumeration_count() << "\n";

    const meta_enum_t mode = p->meta_object()->enumeration("Mode");
    // Same lookup through the type-based entry point (QMetaEnum::fromType equivalent).
    const meta_enum_t mode_by_type = meta_enum_t::from_type<Person>("Mode");
    std::cout << "from_type<Person>(\"Mode\"): valid=" << mode_by_type.is_valid()
              << " scope=" << mode_by_type.scope()
              << " is_flag=" << mode_by_type.is_flag() << "\n";
    std::cout << "Mode: valid=" << mode.is_valid()
              << " scoped=" << mode.is_scoped()
              << " is_flag=" << mode.is_flag()
              << " key_count=" << mode.key_count() << "\n";
    for (std::size_t i = 0; i < mode.key_count(); ++i) {
        std::cout << "  [" << i << "] " << mode.key(i) << " = " << mode.value(i) << "\n";
    }
    std::cout << "value_to_key(2) = " << mode.value_to_key(2) << " (expected Safe)"
              << "  value_to_key(3) = '" << mode.value_to_key(3) << "' (no match -> empty)\n";
    const std::optional<std::int64_t> deep = mode.key_to_value("Deep");
    std::cout << "key_to_value(\"Deep\") = " << (deep.has_value() ? *deep : -1)
              << "  key_to_value(\"Nope\").has_value() = " << mode.key_to_value("Nope").has_value()
              << " (expected 4 / 0)\n";
    std::cout << "value_to_keys(3) = " << mode.value_to_keys(3) << " (expected Fast|Safe)"
              << "  value_to_keys(0) = " << mode.value_to_keys(0) << " (expected None)\n";
    const std::optional<std::int64_t> combo = mode.keys_to_value("Safe|Deep");
    std::cout << "keys_to_value(\"Safe|Deep\") = " << (combo.has_value() ? *combo : -1)
              << " (expected 6)"
              << "  keys_to_value(\"Safe|Nope\").has_value() = "
              << mode.keys_to_value("Safe|Nope").has_value() << " (expected 0)\n";

    const meta_enum_t color = p->meta_object()->enumeration("Color");
    std::cout << "Color: valid=" << color.is_valid()
              << " scoped=" << color.is_scoped()
              << " is_flag=" << color.is_flag() << " (expected 1 0 0)\n";
    std::cout << "enumeration(\"Nope\").is_valid() = "
              << p->meta_object()->enumeration("Nope").is_valid() << " (expected 0)\n";

    // =========================================================================
    // M4: the object tree / object_name / signal-slot connection and emission
    // =========================================================================
    std::cout << "\n== M4: object & connection ==\n";

    std::cout << "p.parent==root = " << (p->parent() == root)
              << "  p.root()==root = " << (p->root() == root)
              << "  root->children().size = " << root->children().size() << "\n";

    // object_name (a property inherited from object_t; set_object_name emits object_name_changed, and
    // since there is no connection right now only the name is updated)
    p->set_object_name("Alice");
    std::cout << "object_name() = " << p->object_name() << "\n";

    auto h = p->connect("valueChanged(double) const", recv, "onChanged(double)");
    std::cout << "connect valid = " << h.is_valid() << "\n";

    p->setValue(42.0);                               // valueChanged(...) inside setValue -> Direct dispatch to recv::onChanged
    std::cout << "after signal: recv.last=" << recv->last << " recv.calls=" << recv->calls << "\n";

    bool dis = h.disconnect();
    std::cout << "disconnect = " << dis << "  handle still valid = " << h.is_valid() << "\n";

    p->setValue(99.0);                               // already disconnected, so nothing is dispatched
    std::cout << "after disconnect signal: recv.calls=" << recv->calls << " (unchanged)\n";

    // Unique: connecting again returns the existing handle (no new connection is added)
    auto h1 = p->connect("valueChanged(double) const", recv, "onChanged(double)",
                         connection_type_t{connection_type_flag::DIRECT} | connection_type_flag::UNIQUE);
    auto h2 = p->connect("valueChanged(double) const", recv, "onChanged(double)",
                         connection_type_t{connection_type_flag::DIRECT} | connection_type_flag::UNIQUE);
    p->setValue(7.0);
    std::cout << "Unique connection de-duplication: calls=" << recv->calls << " (only +1)\n";

    // =========================================================================
    // M7: the event system - custom events / event filters / queued posting / delete_later / exec
    // =========================================================================
    std::cout << "\n== M7: event ==\n";

    // (1) Custom event: post_event defers delivery -> reaches custom_event through notify
    auto* probe = new Probe;
    probe->set_parent(root);
    core_application::post_event(probe, new event_t(tick_event_type));
    std::cout << "after post_event (not dispatched yet): probe.custom_seen=" << probe->custom_seen;
    core_application::process_events();
    std::cout << "  after process_events=" << probe->custom_seen << "\n";

    // (2) Event filter: once it intercepts and consumes the event, Probe::custom_event is skipped
    auto* filt = new EventFilter;
    filt->set_parent(root);
    filt->intercept = true;
    probe->install_event_filter(filt);
    core_application::post_event(probe, new event_t(tick_event_type));
    core_application::process_events();
    std::cout << "filter interception: probe.custom_seen=" << probe->custom_seen << " (unchanged)\n";

    // (3) send_event (synchronous): takes no ownership, the event lives on the stack, and spontaneous is true
    probe->remove_event_filter(filt);
    event_t sync_event(tick_event_type);
    const bool accepted = core_application::send_event(probe, &sync_event);
    std::cout << "send_event after removing the filter: accepted=" << accepted
              << "  spontaneous=" << sync_event.spontaneous()
              << "  custom_seen=" << probe->custom_seen << "\n";

    // (4) Queued connection: the slot does not run on emission; it runs after process_events, with the
    //     arguments copied into the event by type
    auto* qrecv = new Receiver;
    qrecv->set_parent(root);
    p->connect("valueChanged(double) const", qrecv, "onChanged(double)",
               connection_type_t{connection_type_flag::QUEUED});
    p->setValue(5.0);
    std::cout << "after a Queued emission: qrecv.calls=" << qrecv->calls;
    core_application::process_events();
    std::cout << "  after process_events: calls=" << qrecv->calls << " last=" << qrecv->last << "\n";

    // (5) delete_later: posts a DeferredDelete, and the next process_events really destroys the object
    auto* victim = new Deletable;
    victim->set_parent(root);
    victim->delete_later();
    std::cout << "after delete_later (not dispatched yet): destroyed=" << deletable_destroyed;
    core_application::process_events();
    std::cout << "  after process_events: destroyed=" << deletable_destroyed << "\n";

    // (6) The exec / quit cycle: queue a slot that calls quit(), then enter the event loop (see return)
    auto* quitter = new Quitter;
    quitter->set_parent(root);
    p->connect("valueChanged(double) const", quitter, "requestQuit(double)",
               connection_type_t{connection_type_flag::QUEUED});
    p->setValue(1.0);
    std::cout << "about to enter exec() (the queue already holds a queued call that will quit)\n";

    // =========================================================================
    // M8: thread affinity, thread_t and Auto resolution
    // =========================================================================
    std::cout << "\n== M8: threads ==\n";

    auto* worker = new Worker;
    worker->set_parent(root);
    std::cout << "main thread() == root->thread(): " << (p->thread() == root->thread()) << "\n";
    worker->start();
    while (worker->ready.load() == 0) {
        std::this_thread::yield();
    }
    std::cout << "worker->receiver->thread() != p->thread(): "
              << (worker->receiver->thread() != p->thread()) << " (expected 1)\n";

    // Default connection type: Auto resolves to Queued because the receiver lives on the worker.
    auto worker_conn = p->connect("valueChanged(double) const", worker->receiver, "onChanged(double)");
    std::cout << "Auto connection valid = " << worker_conn.is_valid() << "\n";

    const int before = worker->receiver->calls;
    p->setValue(123.0);

    // Let the worker pick it up.
    for (int i = 0; i < 20000000 && worker->receiver->calls == before; ++i) {
        std::this_thread::yield();
    }
    std::cout << "Queued delivery: worker receiver calls=" << worker->receiver->calls
              << " (expected " << (before + 1) << ")\n";
    std::cout << "after the worker ran: calls=" << worker->receiver->calls
              << " last=" << worker->receiver->last
              << " (expected " << (before + 1) << " / 123)\n";

    worker_conn.disconnect();
    worker->stop = 1;
    std::cout << "worker finished cleanly = " << worker->wait(5000) << "\n";

    // =========================================================================
    // M9: timers
    // =========================================================================
    std::cout << "\n== M9: timers ==\n";

    auto* tick_counter = new TickCounter;
    tick_counter->set_parent(root);
    auto* ticker = new timer_t;
    ticker->set_parent(root);
    ticker->set_interval(20);
    std::cout << "connect timeout() -> on_timeout(): valid="
              << ticker->connect("timeout() const", tick_counter, "on_timeout()").is_valid() << "\n";
    std::cout << "before start: is_active=" << ticker->is_active() << " (expected 0)\n";
    ticker->start();
    std::cout << "after start:  is_active=" << ticker->is_active() << " (expected 1)\n";

    for (int i = 0; i < 100; ++i) {
        core_application::process_events();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // The tick COUNT is not printed: a round sleeps at least ~1 ms, so how many 20 ms periods fit
    // into 100 rounds depends on the machine's sleep granularity and load, not on the timer. What
    // the demo proves is qualitative: a repeating timer fires more than once.
    std::cout << "20ms timer over 100 rounds: fired repeatedly=" << (tick_counter->ticks >= 2)
              << " (the exact count depends on the OS sleep granularity)\n";

    ticker->stop();
    const int after_stop = tick_counter->ticks;
    for (int i = 0; i < 40; ++i) {
        core_application::process_events();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // ... and that stopping it freezes the count, whatever it was.
    std::cout << "after stop:   is_active=" << ticker->is_active()
              << " ticks frozen=" << (tick_counter->ticks == after_stop) << "\n";

    // The pump above drained the queued call that requests quit(); queue it again for exec().
    p->setValue(2.0);

    // =========================================================================
    // M11: dynamic properties and class info
    // =========================================================================
    std::cout << "\n== M11: dynamic properties ==\n";
    std::cout << "dynamic properties before: " << p->dynamic_property_names().size() << "\n";
    p->set_property("editor_hint", any_t(7));
    const auto dyn_names = p->dynamic_property_names();
    std::cout << "after set_property(\"editor_hint\", 7): count=" << dyn_names.size()
              << " name=" << (dyn_names.empty() ? "" : dyn_names[0])
              << " value=" << p->property("editor_hint").get<int>() << "\n";
    p->set_property("value", any_t(1.5));
    std::cout << "a declared property does not join the dynamic table: count still "
              << p->dynamic_property_names().size()
              << ", value()=" << p->value() << "\n";
    std::cout << "class_info_count(Person) = " << p->meta_object()->class_info_count() << "\n";
    const auto author_info = p->meta_object()->class_info("Author");
    std::cout << "class_info(\"Author\") = " << author_info.value()
              << "  class_info(\"Missing\") count is index_npos: "
              << (p->meta_object()->index_of_class_info("Missing") == index_npos) << "\n";

    // =========================================================================
    // M13: connections and lookups
    // =========================================================================
    std::cout << "\n== M13: connections ==\n";

    int lambda_hits = 0;
    const auto fn_conn = p->connect("valueChanged(double) const", [&](double value) {
        ++lambda_hits;
        std::cout << "  functor received value=" << value << "\n";
    });
    std::cout << "functor connect valid = " << fn_conn.is_valid() << "\n";
    p->setValue(42.0);
    std::cout << "functor calls after one emit = " << lambda_hits << "\n";

    signal_spy_t spy(p, "valueChanged(double) const");
    p->setValue(43.0);
    std::cout << "signal_spy count = " << spy.count() << "\n";
    const auto records = spy.take_all();
    std::cout << "recorded value = "
              << ((records.empty() || records[0].empty()) ? -1.0 : records[0][0].get<double>())
              << "\n";

    object_t named_child;
    named_child.set_object_name("probe");
    named_child.set_parent(p);
    std::cout << "find_child(\"probe\") matches = "
              << (p->find_child("probe") == &named_child) << "\n";

    {
        signal_blocker_t blocked(p);
        const int before = lambda_hits;
        p->setValue(44.0);
        std::cout << "while blocked, functor not called = " << (lambda_hits == before) << "\n";
    }
    p->setValue(45.0);
    std::cout << "after unblock, functor calls = " << lambda_hits << "\n";

    // =========================================================================
    // M14: diagnostics
    // =========================================================================
    std::cout << "\n== M14: diagnostics ==\n";

    std::vector<std::string> captured;
    install_log_handler([&](log_level_enum level, const log_context_t& context, std::string_view msg) {
        captured.push_back(std::string(context.category) + "|" + log_level_name(level) + "|" + std::string(msg));
        return true;                       // consumed: nothing goes to stderr
    });
    HWSHQTB_LOG_RAW(log_events(), log_level_enum::WARNING, "a warning from the demo");
    HWSHQTB_LOG_RAW(log_events(), log_level_enum::INFO, "below the category threshold, dropped");
    std::cout << "captured log lines = " << captured.size()
              << "  first = " << (captured.empty() ? "" : captured[0]) << "\n";
    install_log_handler({ });

    const meta_type_t int_type = meta_type_t::resolve_global("int");
    std::cout << "meta_type_t(int): valid=" << int_type.is_valid()
              << " name=" << int_type.name()
              << " id=" << int_type.id()
              << " trivially_copyable=" << bool(int_type.tag() & type_tag_flag::IS_TRIVIALLY_COPYABLE)
              << "\n";
    const int sample = 7;
    std::cout << "debug_string(7) = " << int_type.debug_string(&sample) << "\n";

    object_t tree_root;
    tree_root.set_object_name("tree_root");
    object_t child_a;
    child_a.set_object_name("child_a");
    child_a.set_parent(&tree_root);
    object_t grandchild;
    grandchild.set_object_name("grandchild");
    grandchild.set_parent(&child_a);
    std::cout << "dump_object_tree():\n";
    tree_root.dump_object_tree(std::cout);
    // =========================================================================
    // M15: dynamic meta-object
    // =========================================================================
    std::cout << "\n== M15: dynamic meta-object ==\n";

    meta_object_builder_t dyn_builder("DynamicWidget", &object_t::static_meta_object);
    dyn_builder.add_method("setValue(double)").set_caller(&dyn_write_value);
    dyn_builder.add_signal("valueChanged(double) const").set_caller(&dyn_write_value);
    meta_property_builder_t dyn_value = dyn_builder.add_property("value", "double");
    dyn_value.set_reader(&dyn_read_value);
    dyn_value.set_writer(&dyn_write_value);
    meta_enum_builder_t dyn_mode_builder = dyn_builder.add_enumerator("Mode");
    dyn_mode_builder.add_key("None", 0);
    dyn_mode_builder.add_key("Fast", 1);
    dyn_mode_builder.set_is_flag(true);   // { 0, 1 } is pairwise disjoint, which is how the old builder derived it
    dyn_builder.add_class_info("Author", "hwshqtb");
    const meta_object_t* dyn_meta_object = dyn_builder.build();

    demo_dynamic_t dyn_object(dyn_meta_object, root);
    std::cout << "class=" << dyn_meta_object->class_name()
              << " methods=" << dyn_meta_object->method_count()
              << " properties=" << dyn_meta_object->property_count()
              << " enums=" << dyn_meta_object->enumeration_count()
              << " class_info=" << dyn_meta_object->class_info_count() << "\n";

    // The argument channel is one any_t per parameter; setValue(double) matches it by name.
    const any_t dyn_args[] = { any_t(3.5) };
    dyn_meta_object->method("setValue(double)").invoke(dyn_object, dyn_args);
    std::cout << "after setValue(3.5): property(\"value\") = "
              << dyn_object.property("value").get<double>() << "\n";
    std::cout << "property meta: name=" << dyn_meta_object->property("value").name()
              << " type=" << dyn_meta_object->property("value").type_name() << "\n";
    const meta_enum_t dyn_mode = dyn_meta_object->enumeration("Mode");
    const auto fast_value = dyn_mode.key_to_value("Fast");
    std::cout << "enum Mode: count=" << dyn_mode.key_count()
              << " Fast=" << (fast_value ? *fast_value : -1) << "\n";
    std::cout << "class_info(Author)=" << dyn_meta_object->class_info("Author").value()
              << "  inherits(object_t)=" << dyn_meta_object->inherits("object_t") << "\n";

    // =========================================================================
    // M17: property binding
    // =========================================================================
    std::cout << "\n== M17: property binding ==\n";
    {
        property_t a(any_t(2.0));
        property_t b(any_t(0.0));
        b.set_binding([&]() -> any_t { return any_t(a.value().get<double>() * 2.0); });
        std::cout << "initial: a=" << a.value().get<double>()
                  << "  b (bound to a*2)=" << b.value().get<double>() << "\n";

        a.set_value(any_t(3.0));
        core_application::process_events();          // the end-of-round flush recomputes b
        std::cout << "after set_value(3.0) + one process_events: b=" << b.value().get<double>()
                  << " (expect 6)\n";
    }

    // The round above drained the queued quit() call the M7 section armed for exec() - exactly what
    // the M9 pump did to the first one. Post the Quitter's slot again (not p->setValue, which would
    // print the M13 functor line again) so exec() still returns.
    core_application::post_event(quitter, new meta_call_event_t(nullptr, meta_method_t{},
        quitter->meta_object()->method("requestQuit(double)"), { any_t(0.0) }));

    std::vector<std::string> binding_logs;
    install_log_handler([&](log_level_enum level, const log_context_t& context, std::string_view message) {
        binding_logs.push_back(std::string(context.category) + "|" + log_level_name(level) + "|" + std::string(message));
        return true;                                 // consumed: nothing goes to stderr
    });
    property_t throwing(any_t(5.0));
    throwing.set_binding([]() -> any_t { throw std::runtime_error("closure failed"); });
    std::cout << "a throwing binding keeps the old value: " << throwing.value().get<double>()
              << "  has_binding=" << throwing.has_binding() << "\n";
    std::cout << "logged: " << (binding_logs.empty() ? "" : binding_logs.back()) << "\n";
    install_log_handler({ });
    std::cout << "\n[all tests completed]\n";
    return core_application::exec();
}