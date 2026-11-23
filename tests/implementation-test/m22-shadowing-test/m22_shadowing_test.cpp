// M22: own-first meta-object lookup and shadowing of a base-class member.
//
// The lookup direction changed to own-first: meta_object_t::global_index() and enumeration_in()
// consult the most-derived table before walking super_class(). No existing test declared the same
// signature in a base and in its derived class, so nothing pinned the case where the two rows are
// different declarations with the same normalised key:
//
//   * method("f()") must name the DERIVED row, not the base row that happens to come first in the
//     global coordinate space. The two declarations are told apart by their [[= make_tag(...)]]
//     payloads (1 in the base, 2 in the derived), which works for a reflected table where the text
//     of the two declarations is identical;
//   * index_of_method() must return the derived row's coordinate and method(that) must round-trip;
//   * Direct and Queued invocation must both run the derived body (the counters are disjoint);
//   * meta_method_t::from_method(&shadow_derived_t::f) must name the same derived row;
//   * a signal declared in both classes must still fire when it is emitted on the derived object
//     (the connection and the emitter both resolve their signature through the derived table);
//   * enumeration("Mode") must report the derived enum, and meta_enum_t::from_type must agree.
//
// This is a TEST-ONLY addition: the fixtures live in this file and no library header is modified.

#include <meta>

#include <cstdio>
#include <string_view>

#include <hwshqtb/metaobject/annotation.h>
#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_class_info_t.h>
#include <hwshqtb/metaobject/meta_constructor_t.h>
#include <hwshqtb/metaobject/meta_enum_t.h>
#include <hwshqtb/metaobject/meta_info_t.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/meta_property_t.h>
#include <hwshqtb/metaobject/meta_type_t.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::metaobject;

namespace m22_shadowing_test {

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

/// The base: f() and pinged(int) const are its own reflected rows. The tag payload 1 identifies
/// this declaration when the normalised signature "f()" is shared with the derived class.
struct shadow_base_t : object_t {
    OBJECT
public:
    int base_calls = 0;

    [[= make_tag(1)]] int f() {
        ++base_calls;
        return 1;
    }

    SIGNAL(void pinged(int) const)

    enum Mode { base_a, base_b };
};
META_OBJECT(shadow_base_t);

/// The derived class: f() has exactly the same normalised signature as shadow_base_t::f(), and
/// pinged(int) const is the same signal signature as the base's. Both hide their base counterpart;
/// the meta-object must prefer these rows because the lookup starts at the most-derived table.
struct shadow_derived_t : shadow_base_t {
    OBJECT
public:
    int derived_calls = 0;

    /// Same normalised signature as the base's ("f()"); tag 2 is what distinguishes the row.
    [[= make_tag(2)]] int f() {
        ++derived_calls;
        return 2;
    }

    SIGNAL(void pinged(int) const)

    enum Mode { derived_a, derived_b };
};
META_OBJECT(shadow_derived_t);

/// The receiver of the shadowed signal.
struct shadow_receiver_t : object_t {
    OBJECT
public:
    int hits = 0;
    int last = 0;
    int zero_hits = 0;
    int two_hits = 0;
    int text_hits = 0;

    void on_pinged(int value) {
        ++hits;
        last = value;
    }

    /// Fewer parameters than the signal: legal, the extra signal argument is simply not passed.
    void on_pinged0() { ++zero_hits; }

    /// More parameters than the signal: refused by connect(), the dispatcher has no value for them.
    void on_pinged2(int a, int b) {
        ++two_hits;
        last = a + b;
    }

    /// A parameter type the signal does not carry: refused as well.
    void on_number(double) { ++text_hits; }
};
META_OBJECT(shadow_receiver_t);

/// A base property marked Final, and a derived property of the SAME name. `Final` is introspection
/// only: nothing enforces it, so this pair must compile, and the derived row must shadow the base's
/// for by-name lookup while the base table keeps reporting its own bit.
struct sealed_base_t : object_t {
    OBJECT
public:
    [[= make_property("sealed", "", "", "", property_trait_flag::FINAL)]]
    int sealed = 1;
};
META_OBJECT(sealed_base_t);

struct sealed_derived_t : sealed_base_t {
    OBJECT
public:
    [[= make_property("sealed")]]
    int sealed = 2;
};
META_OBJECT(sealed_derived_t);

/// A property whose NOTIFY name resolves to a plain slot: the notifier must stay unbound, because
/// only a signal can be a change-notification channel (a plain slot would satisfy is_notifiable()
/// and then fail at connect()).
struct notify_slot_t : object_t {
    OBJECT
public:
    [[= make_property("value", "", "value_changed")]]
    int value = 0;

    void value_changed(int) { }
};
META_OBJECT(notify_slot_t);

/// The same property with a real SIGNAL of that name: this one must bind.
struct notify_signal_t : object_t {
    OBJECT
public:
    [[= make_property("value", "", "value_changed")]]
    int value = 0;

    SIGNAL(void value_changed(int))
};
META_OBJECT(notify_signal_t);

/// Three levels of class info, so a handle's index() can be told apart from a local slot: the
/// middle class's row is local slot 0 but chain-global index 1 (the root's row comes first).
struct [[= make_class_info("Level", "root")]] info_root_t : object_t {
    OBJECT
};
META_OBJECT(info_root_t);

struct [[= make_class_info("Level", "mid")]] info_mid_t : info_root_t {
    OBJECT
};
META_OBJECT(info_mid_t);

struct [[= make_class_info("Level", "leaf")]] info_leaf_t : info_mid_t {
    OBJECT
};
META_OBJECT(info_leaf_t);

/// Constructors are not inherited, so the constructor VIEW must be own-only too: the derived class
/// counts and addresses only its own two, and the base's signature resolves to nothing there.
struct ctor_base_t : object_t {
    OBJECT
public:
    ctor_base_t() = default;
    ctor_base_t(int value) : seed(value) { }

    int seed = 0;
};
META_OBJECT(ctor_base_t);

struct ctor_derived_t : ctor_base_t {
    OBJECT
public:
    ctor_derived_t() = default;
    ctor_derived_t(int value) : ctor_base_t(value) { }
};
META_OBJECT(ctor_derived_t);

/// A derived interface type: a class implementing IFoo2 also implements IFoo, so both must be
/// recorded (and both must be castable), not just the direct base.
struct [[= make_interface("m22.IFoo/1.0")]] ifoo_t {
    virtual ~ifoo_t() = default;
    virtual int foo() = 0;
};

struct [[= make_interface("m22.IFoo2/1.0")]] ifoo2_t : ifoo_t {
    virtual int foo2() = 0;
};

struct iface_holder_t : object_t, ifoo2_t {
    OBJECT
public:
    int foo() override { return 1; }
    int foo2() override { return 2; }
};
META_OBJECT(iface_holder_t);

/// Accessor scope rule: the property is declared in the LEAF, the approximate setter (int&) in the
/// MIDDLE class, and an exact setter (int) in the ROOT. Scope-major resolution starts at the declaring
/// class and hides by class, so the MIDDLE's setter wins; the old level-major rule ran an exact pass
/// over the whole chain first and picked the ROOT's.
struct acc_root_t : object_t {
    OBJECT
public:
    int exact_hits = 0;

    void set_value(int v) {
        ++exact_hits;
        value = v;
    }

    int value = 0;
};
META_OBJECT(acc_root_t);

struct acc_middle_t : acc_root_t {
    OBJECT
public:
    int approx_hits = 0;

    /// Approximate for a property of type "int": the bare spelling matches, the exact one does not.
    void set_value(int& v) {
        ++approx_hits;
        value = v;
    }
};
META_OBJECT(acc_middle_t);

struct acc_leaf_t : acc_middle_t {
    OBJECT
public:
    /// No setter here, so the compile-time pass binds nothing and the run-time walk decides.
    [[= make_property("value", "set_value")]]
    int value = 0;
};
META_OBJECT(acc_leaf_t);

/// GADGET declares only a static table — no meta_object() — so an OBJECT class may inherit it and
/// still be a root of its own chain.
struct gadget_base_t {
    GADGET
public:
    void gadget_and_object() { }
};
META_OBJECT(gadget_base_t);

struct object_over_gadget_t : gadget_base_t {
    OBJECT
};
META_OBJECT(object_over_gadget_t);

/// The typed invoke() validation target: a `long` parameter the caller can deliberately mismatch.
struct arg_check_t : object_t {
    OBJECT
public:
    void take(long v) { last = v; }

    long last = 0;
};
META_OBJECT(arg_check_t);

/// A payload type the caller NEVER registers: the Queued path must register it for them, because it
/// copies the argument by its declared type name.
struct queued_payload_t { int v = 0; };

struct queued_sink_t : object_t {
    OBJECT
public:
    void take(queued_payload_t p) { got = p.v; }

    int got = -1;
};
META_OBJECT(queued_sink_t);

}  // namespace m22_shadowing_test

/// Two classes with the SAME simple name in different namespaces, declared at global scope so their
/// qualified names carry exactly one namespace: the registry keys on the qualified name, so both are
/// creatable and neither can shadow the other.
namespace twin_a {
    struct twin_t : object_t {
        OBJECT
    public:
        int tag = 1;
    };
    META_OBJECT(twin_t);
}

namespace twin_b {
    struct twin_t : object_t {
        OBJECT
    public:
        int tag = 2;
    };
    META_OBJECT(twin_t);
}

using namespace m22_shadowing_test;

int main(int argc, char** argv) {
    core_application app(argc, argv);

    std::printf("== M22: own-first lookup and derived shadowing ==\n");

    shadow_derived_t derived;
    const meta_object_t* const meta = derived.meta_object();

    // (1) method("f()") names the DERIVED row: the tag payload, the coordinate and the round-trip.
    {
        std::printf("-- (1) method(\"f()\") selects the derived row --\n");
        const meta_method_t row = meta->method("f()");
        CHECK(row.is_valid());
        CHECK(row.name() == "f");
        CHECK(row.signature() == "f()");

        // The two declarations are textually identical, so the annotation is the discriminator.
        const void* const raw_tag = row.tag("int");
        CHECK(raw_tag != nullptr);
        CHECK(raw_tag != nullptr && *static_cast<const int*>(raw_tag) == 2);

        const index_t index = meta->index_of_method("f()");
        CHECK(index != index_npos);
        CHECK(index == row.index());
        // The derived f() is one of the derived class's own rows, so its global coordinate lies at
        // or beyond the end of the whole base chain (an own-first lookup must not answer a
        // coordinate that belongs to a base row).
        CHECK(index >= shadow_base_t::static_meta_object.method_count());
        CHECK(meta->method(index) == row);
        CHECK(meta->method(index).tag("int") != nullptr);
        CHECK(meta->method(index).tag("int") != nullptr
            && *static_cast<const int*>(meta->method(index).tag("int")) == 2);

        // Sanity: the base row really is a different row with a different tag.
        const meta_method_t base_row = shadow_base_t::static_meta_object.method("f()");
        CHECK(base_row.is_valid());
        CHECK(base_row.tag("int") != nullptr);
        CHECK(base_row.tag("int") != nullptr && *static_cast<const int*>(base_row.tag("int")) == 1);
        CHECK(base_row != row);
    }

    // (2) The Direct invocation runs the derived body, not the base one.
    {
        std::printf("-- (2) Direct invoke(\"f()\") --\n");
        CHECK(derived.derived_calls == 0);
        CHECK(derived.base_calls == 0);
        CHECK(meta->invoke(derived, "f()").is_valid());
        CHECK(derived.derived_calls == 1);
        CHECK(derived.base_calls == 0);
    }

    // (3) The Queued invocation resolves the same row: it is posted, then dispatched to the
    //     derived body. The object lives on the calling thread; Queued still goes through the queue.
    //     The value row hands its result back THROUGH this variable: the event holds its storage, so
    //     it must not be destroyed (or moved) before the queue has been drained.
    {
        std::printf("-- (3) Queued invoke(\"f()\") --\n");
        any_t queued_f = meta->invoke(derived, "f()", connection_type_flag::QUEUED);
        CHECK(queued_f.is_valid());
        CHECK(!queued_f.is_initialized());          // posted, not run: value not here yet
        CHECK(derived.derived_calls == 1);
        CHECK(derived.base_calls == 0);
        event_dispatcher::instance().send_posted_events();
        CHECK(derived.derived_calls == 2);
        CHECK(derived.base_calls == 0);
        CHECK(queued_f.get<int>() == 2);            // the derived body's answer, filled by the event
    }

    // (4) from_method(&shadow_derived_t::f) names the same derived row, and the base pointer still
    //     names the base row (the two handles are distinct).
    {
        std::printf("-- (4) from_method(&shadow_derived_t::f) --\n");
        const meta_method_t from_derived = meta_method_t::from_method(&shadow_derived_t::f);
        CHECK(from_derived.is_valid());
        CHECK(from_derived == meta->method("f()"));
        CHECK(from_derived == meta->method(meta->index_of_method("f()")));

        const meta_method_t from_base = meta_method_t::from_method(&shadow_base_t::f);
        CHECK(from_base.is_valid());
        CHECK(from_base == shadow_base_t::static_meta_object.method("f()"));
        CHECK(from_base != from_derived);
    }

    // (5) A signal declared in both classes: connecting and emitting on the derived object must
    //     reach the receiver. Every lookup along the path starts at the derived table, so the
    //     connection and the emitter agree on the derived signal row.
    {
        std::printf("-- (5) signal declared in both classes --\n");
        const meta_method_t signal_row = meta->signal("pinged(int) const");
        CHECK(signal_row.is_valid());
        CHECK(signal_row.is_signal());
        CHECK(signal_row.index() >= shadow_base_t::static_meta_object.method_count());

        shadow_receiver_t receiver;
        const connection_handle_t connection =
            derived.connect("pinged(int) const", &receiver, "on_pinged(int)");
        CHECK(connection.is_valid());

        derived.pinged(41);
        CHECK(receiver.hits == 1);
        CHECK(receiver.last == 41);

        derived.pinged(42);
        CHECK(receiver.hits == 2);
        CHECK(receiver.last == 42);
    }

    // (6) enumeration("Mode") reports the derived enum; from_type agrees with the by-name query.
    {
        std::printf("-- (6) enumeration(\"Mode\") --\n");
        const meta_enum_t mode = meta->enumeration("Mode");
        CHECK(mode.is_valid());
        CHECK(mode.name() == "Mode");
        CHECK(mode.scope() == "shadow_derived_t");
        CHECK(mode.key_count() == 2);
        CHECK(mode.key(0) == "derived_a");
        CHECK(mode.key(1) == "derived_b");

        const meta_enum_t from_type = meta_enum_t::from_type<shadow_derived_t>("Mode");
        CHECK(from_type.is_valid());
        CHECK(from_type.scope() == mode.scope());
        CHECK(from_type.name() == mode.name());
        CHECK(from_type.key(0) == mode.key(0));
        CHECK(from_type.key(1) == mode.key(1));

        // The base's own Mode still reports itself, so the two rows are genuinely different.
        const meta_enum_t base_mode = shadow_base_t::static_meta_object.enumeration("Mode");
        CHECK(base_mode.is_valid());
        CHECK(base_mode.scope() == "shadow_base_t");
        CHECK(base_mode.key(0) == "base_a");
    }

    // (7) Final is introspection only: a derived property may redeclare a name its base marked
    //     Final (this translation unit compiling at all is half the proof), the derived row shadows
    //     the base's for by-name lookup, and the base still reports its own bit.
    {
        std::printf("-- (7) Final is introspection only --\n");
        const meta_property_t base_sealed = sealed_base_t::static_meta_object.property("sealed");
        CHECK(base_sealed.is_valid());
        CHECK(base_sealed.is_final());

        const meta_property_t derived_sealed = sealed_derived_t::static_meta_object.property("sealed");
        CHECK(derived_sealed.is_valid());
        CHECK(!derived_sealed.is_final());   // own-first lookup reaches the shadowing row
        CHECK(derived_sealed.type_name() == "int");

        // The chain-global property space holds both rows (one more than the base's chain), the
        // by-name query picks the derived one, and its coordinate is the base chain's row count.
        CHECK(sealed_derived_t::static_meta_object.property_count()
              == sealed_base_t::static_meta_object.property_count() + 1);
        CHECK(sealed_derived_t::static_meta_object.index_of_property("sealed")
              == sealed_base_t::static_meta_object.property_count());
    }

    // (8) connect() validates the argument lists: a slot may drop trailing signal arguments, never
    //     add its own, and a parameter type the signal does not carry is refused too. Before this
    //     check existed, an over-long slot read past the dispatcher's argument array.
    {
        std::printf("-- (8) connect argument validation --\n");
        shadow_receiver_t receiver;

        const connection_handle_t shorter = derived.connect("pinged(int) const", &receiver, "on_pinged0()");
        CHECK(shorter.is_valid());

        const connection_handle_t longer =
            derived.connect("pinged(int) const", &receiver, "on_pinged2(int,int)");
        CHECK(!longer.is_valid());

        const connection_handle_t mismatched =
            derived.connect("pinged(int) const", &receiver, "on_number(double)");
        CHECK(!mismatched.is_valid());

        derived.pinged(7);
        CHECK(receiver.zero_hits == 1);   // the legal short slot ran
        CHECK(receiver.two_hits == 0);    // the refused ones never ran
        CHECK(receiver.text_hits == 0);
    }

    // (9) A notifier must be a SIGNAL. A plain slot of that name leaves the property non-notifiable;
    //     the same property with a real signal binds.
    {
        std::printf("-- (9) notifier must be a signal --\n");
        const meta_property_t plain = notify_slot_t::static_meta_object.property("value");
        CHECK(plain.is_valid());
        CHECK(!plain.is_notifiable());
        CHECK(!plain.notifier().is_valid());

        const meta_property_t signaled = notify_signal_t::static_meta_object.property("value");
        CHECK(signaled.is_valid());
        CHECK(signaled.is_notifiable());
        CHECK(signaled.notifier().is_valid());
        CHECK(signaled.notifier().is_signal());
        CHECK(signaled.notifier().name() == "value_changed");
    }

    // (10) class_info handles answer in the CHAIN-GLOBAL index space, like method handles: the
    //      middle class's row is local slot 0 but global index 1, and feeding that index back to a
    //      descendant's table must return the same row (a local slot would land on the root's row).
    {
        std::printf("-- (10) class_info index space --\n");
        const meta_class_info_t mid = info_mid_t::static_meta_object.class_info("Level");
        CHECK(mid.is_valid());
        CHECK(mid.value() == "mid");
        CHECK(mid.index() == 1);                      // root's row is global 0

        const meta_class_info_t from_leaf = info_leaf_t::static_meta_object.class_info(mid.index());
        CHECK(from_leaf.is_valid());
        CHECK(from_leaf.name() == mid.name());
        CHECK(from_leaf.value() == mid.value());      // "mid", not the root's "root"

        const meta_class_info_t root_row = info_leaf_t::static_meta_object.class_info(0);
        CHECK(root_row.is_valid());
        CHECK(root_row.value() == "root");

        const meta_class_info_t leaf_row = info_leaf_t::static_meta_object.class_info("Level");
        CHECK(leaf_row.is_valid());
        CHECK(leaf_row.value() == "leaf");
        CHECK(leaf_row.index() == 2);
    }

    // (11) Constructors are not inherited, and the constructor VIEW now says so too: the derived
    //      table counts and addresses only its own two, and the base's exact signature is not
    //      addressable from it (before the fix, the view walked the chain and could hand back a
    //      base constructor that new_instance() would then refuse to run).
    {
        std::printf("-- (11) constructors are own-only --\n");
        const meta_object_t& base_meta = ctor_base_t::static_meta_object;
        const meta_object_t& derived_meta = ctor_derived_t::static_meta_object;

        // Four reflected constructors each (default, int, copy, move). Inherited counting would make
        // the derived answer 8; own-only makes the two equal.
        CHECK(base_meta.constructor_count() == 4);
        CHECK(derived_meta.constructor_count() == 4);
        CHECK(derived_meta.constructor_count() == base_meta.constructor_count());

        const meta_constructor_t base_ctor = base_meta.constructor(0);
        CHECK(base_ctor.is_valid());
        CHECK(base_ctor.index() == 0);
        CHECK(derived_meta.index_of_constructor(base_ctor.signature()) == index_npos);

        CHECK(derived_meta.constructor(0).is_valid());
        CHECK(derived_meta.constructor(0).index() == 0);
        CHECK(!derived_meta.constructor(4).is_valid());   // no fifth constructor of its own
    }

    // (12) An OBJECT class may inherit a GADGET: a gadget declares only a static table, no
    //      meta_object(), so it is not part of the chain — the OBJECT is a root of its own chain and
    //      does NOT see the gadget base's rows (those stay reachable as gadget_base_t::static_...).
    {
        std::printf("-- (12) OBJECT inheriting a GADGET --\n");
        const meta_object_t& table = object_over_gadget_t::static_meta_object;
        CHECK(table.super_class() == nullptr);
        CHECK(!table.has_super_class());
        CHECK(table.class_name() == "object_over_gadget_t");
        CHECK(gadget_base_t::static_meta_object.class_name() == "gadget_base_t");

        object_over_gadget_t value;
        CHECK(value.meta_object() == &table);
        // The gadget base is NOT in the chain, so its rows are not reachable through it.
        CHECK(!table.method("gadget_and_object()").is_valid());
        CHECK(gadget_base_t::static_meta_object.method("gadget_and_object()").is_valid());

        // cast<T> still reaches the gadget base: a gadget is not polymorphic, so the compiler's own
        // relation is the exact answer (no chain needed).
        CHECK(cast<gadget_base_t>(&value) == static_cast<gadget_base_t*>(&value));
        CHECK(cast<object_over_gadget_t>(&value) == &value);
        const object_over_gadget_t& const_value = value;
        CHECK(cast<const gadget_base_t>(&const_value) == static_cast<const gadget_base_t*>(&const_value));
        CHECK(cast<object_over_gadget_t>(static_cast<gadget_base_t*>(&value)) == nullptr);  // no down-cast
    }

    // (13) Interfaces are transitive: implementing IFoo2 (which derives from IFoo) implements IFoo,
    //      so both rows exist and both casts succeed.
    {
        std::printf("-- (13) transitive interfaces --\n");
        iface_holder_t holder;
        const meta_object_t& table = iface_holder_t::static_meta_object;
        CHECK(table.interface_count() == 2);
        CHECK(table.implements("m22.IFoo2/1.0"));
        CHECK(table.implements("m22.IFoo/1.0"));
        CHECK(cast<ifoo2_t>(&holder) != nullptr);
        CHECK(cast<ifoo_t>(&holder) != nullptr);
        CHECK(cast<ifoo_t>(&holder) == static_cast<ifoo_t*>(&holder));

        // The interface index space is usable end to end: count bounds it, the by-name lookup returns
        // a coordinate that interface_iid() decodes, and the order is the base-scan order (the direct
        // interface first, then the interface it derives from).
        CHECK(table.interface_count() == 2);
        const index_t first = table.index_of_interface("m22.IFoo2/1.0");
        const index_t second = table.index_of_interface("m22.IFoo/1.0");
        CHECK(first != index_npos);
        CHECK(second != index_npos);
        CHECK(first != second);
        CHECK(table.interface_iid(first) == "m22.IFoo2/1.0");
        CHECK(table.interface_iid(second) == "m22.IFoo/1.0");
        CHECK(table.interface_iid(table.interface_count()) == std::string_view{ });   // out of range
        CHECK(table.interface_iid(index_npos) == std::string_view{ });

        // The thunks and cast() recover T* from the object's address, which is only valid when the
        // meta-object base sits at offset 0. A portable static_assert cannot express that for a
        // non-standard-layout class (offsetof is conditionally supported there), so the invariant is
        // pinned here: the object_t sub-object IS the complete object for a multiply-inherited class.
        CHECK(static_cast<void*>(static_cast<object_t*>(&holder)) == static_cast<void*>(&holder));
    }

    // (14) Accessor resolution is scope-major: the nearest class that can answer wins, so the middle
    //      class's approximate setter hides the root's exact one (level-major would have taken the
    //      root's exact match; see acc_root_t / acc_middle_t / acc_leaf_t).
    {
        std::printf("-- (14) accessor scope order --\n");
        acc_leaf_t leaf;
        const meta_property_t value = acc_leaf_t::static_meta_object.property("value");
        CHECK(value.is_valid());
        CHECK(value.is_writable());

        const meta_method_t writer = value.writer();
        CHECK(writer.is_valid());
        CHECK(writer.name() == "set_value");
        CHECK(writer.param_count() == 1);
        CHECK(writer.param_type_name(0) == "int&");   // the middle class's, not the root's "int"
    }

    // (15) The qualified name is the key: two classes with the same simple name in different
    //      namespaces keep that name, are distinguishable by qualified_class_name(), and both can be
    //      registered and created (a bare-name key would let one shadow the other).
    {
        std::printf("-- (15) qualified class names --\n");
        const meta_object_t& a_meta = twin_a::twin_t::static_meta_object;
        const meta_object_t& b_meta = twin_b::twin_t::static_meta_object;

        CHECK(a_meta.class_name() == "twin_t");
        CHECK(b_meta.class_name() == "twin_t");
        CHECK(a_meta.qualified_class_name() == "twin_a::twin_t");
        CHECK(b_meta.qualified_class_name() == "twin_b::twin_t");

        CHECK(a_meta.inherits("twin_a::twin_t"));
        CHECK(!a_meta.inherits("twin_b::twin_t"));
        CHECK(b_meta.inherits("twin_b::twin_t"));
        CHECK(a_meta.inherits("twin_t"));    // the bare identifier still answers

        CHECK(object_t::register_type(&a_meta));
        CHECK(object_t::register_type(&b_meta));
        object_t* a = object_t::create("twin_a::twin_t");
        object_t* b = object_t::create("twin_b::twin_t");
        CHECK(a != nullptr);
        CHECK(b != nullptr);
        CHECK(a != nullptr && a->meta_object() == &a_meta);
        CHECK(b != nullptr && b->meta_object() == &b_meta);
        delete a;
        delete b;
    }

    // (16) The typed invoke() VALIDATES its arguments before dispatching. The thunk reinterprets
    //      each argument address as the DECLARED parameter type, so a mismatched argument is not a
    //      conversion but a wrong-type read — it used to run and report success. It must now be
    //      refused (false, one Warning), and the target must not run at all.
    {
        std::printf("-- (16) invoke argument validation --\n");
        arg_check_t target;
        const meta_object_t& meta_ref = *target.meta_object();

        CHECK(meta_ref.invoke(target, "take(long)", 7L).is_valid());
        CHECK(target.last == 7);

        // A double where a long is declared: refused, and the target's state is untouched.
        target.last = -1;
        CHECK(!meta_ref.invoke(target, "take(long)", 3.5).is_valid());
        CHECK(target.last == -1);

        // The capturing form refuses the same call.
        long out = -2;
        CHECK(!meta_ref.invoke(target, "take(long)", out, 3.5));
        CHECK(out == -2);

        // A queued call refuses it before copying anything.
        CHECK(!meta_ref.invoke(target, "take(long)", connection_type_flag::QUEUED, 3.5).is_valid());
        CHECK(target.last == -1);

        // The correct type still works, so the refusal above is about the mismatch only.
        CHECK(meta_ref.invoke(target, "take(long)", 9L).is_valid());
        CHECK(target.last == 9);
    }

    // (17) A Queued call registers its argument types for the caller. The deferred path copies each
    //      argument by its DECLARED parameter type, resolved through the registry by NAME, so a type
    //      the caller never registered used to make the copy fail and the call be dropped with a
    //      Warning. It must now be delivered — and the registration must happen on the Queued branch
    //      only (the Direct branch passes addresses and never resolves a name).
    {
        std::printf("-- (17) queued call registers its argument types --\n");
        constexpr std::string_view spelled = meta_info_t::canonical_name<queued_payload_t>();
        CHECK(!meta_type_t::resolve_global(spelled).is_valid());   // not registered yet

        queued_sink_t sink;
        queued_payload_t payload;
        payload.v = 5;

        // The fixture lives in this test's namespace, so the stored signature carries the qualified
        // spelling of the parameter type; build it from the same namer the table used.
        const std::string signature = "take(" + std::string(spelled) + ")";
        CHECK(sink.meta_object()->index_of_method(signature) != index_npos);
        CHECK(sink.meta_object()->invoke(sink, signature, connection_type_flag::QUEUED, payload).is_valid());
        core_application::process_events();
        CHECK(sink.got == 5);
        CHECK(meta_type_t::resolve_global(spelled).is_valid());    // registered by the call
    }

    if (g_failures == 0) {
        std::printf("  M22 shadowing: all %d checks passed\n", g_checks);
    }
    else {
        std::printf("  M22 shadowing: %d check(s) failed\n", g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
