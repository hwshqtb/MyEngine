// M16 Phase A: interface conversion (Q_INTERFACES / qobject_cast).
//
// An interface here is an ordinary abstract class - NOT an object_t. An interface TYPE carries
// [[= make_interface("...")]] on its OWN definition, and a class IMPLEMENTS it by inheriting it
// PUBLICLY: build_meta<T>() reflects T's direct bases, so the base list IS the interface list (there
// is no INTERFACES(...) macro to keep in sync any more).
//
// What this test pins down:
//  1. one interface: the IID row resolves and a method can be called through the result;
//  2. several interfaces: every IID resolves, each to its own sub-object;
//  3. an unrelated class: the cast is null, implements is false, interface_count is 0;
//  4. inheritance: a derived class inherits its base's interface entry (parents-first chain walk);
//  5. the namespace-level cast<T>(U*) recovers an interface from its make_interface annotation and
//     returns exactly the same sub-object pointer as meta_object_t::cast(void*, iid);
//  6. interface_count() adds up the whole chain;
//  7. a GADGET (not an object_t) can implement an interface too: build_meta<T>() serves both;
//  8. cast<T>(U*) for a CLASS: an OBJECT hit/miss and a GADGET hit/miss go through the meta-object
//     chain, nullptr in answers nullptr out, and the const overload returns a pointer to const;
//  9. meta_object_t::cast<U>(U*) is the table-level class cast: runtime table for an object_t,
//     static table for a gadget, and a derived object hits its base's table;
// 10. a PRIVATE interface base is not reported: the base scan uses access_context::unprivileged(),
//     so implements() is false and the cast is null — the consumer could not static_cast to it;
// 11. the interface row order is the base DECLARATION order (pinned on the compile-time table).

#include <meta>
#include <cstdio>
#include <string_view>

#include <hwshqtb/metaobject/annotation.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_info_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::metaobject;

namespace m16_interface_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

}  // namespace m16_interface_test

using namespace m16_interface_test;  // the file-local test namespace stays the file's own vocabulary

#define CHECK(cond) check((cond), #cond, __LINE__)

// ---------------------------------------------------------------------------
// The interfaces. Each is a plain abstract class that binds its IID on its own definition with
// [[= make_interface("...")]]; that annotation is the ONE source of an IID (there is no trait and
// no separate declaration macro any more).
// ---------------------------------------------------------------------------
struct [[= make_interface("com.example.IFoo/1.0")]] IFoo {
    virtual ~IFoo() = default;
    virtual int foo_value() const = 0;
};

struct [[= make_interface("com.example.IBar/1.0")]] IBar {
    virtual ~IBar() = default;
    virtual std::string_view bar_name() const = 0;
};

struct [[= make_interface("com.example.IBaz/1.0")]] IBaz {
    virtual ~IBaz() = default;
    virtual int baz_value() const = 0;
};

// ---------------------------------------------------------------------------
// (1) A class that implements exactly one interface. The row comes from the base list: IFoo is
// public and carries make_interface, so build_meta<foo_widget_t>() records it — nothing else.
// ---------------------------------------------------------------------------
struct foo_widget_t : object_t, IFoo {
    OBJECT
public:
    int foo_value() const override { return 42; }
};
META_OBJECT(foo_widget_t);

// ---------------------------------------------------------------------------
// (2) A class that implements several interfaces at once, in this declaration order. object_t stays
// FIRST: single-inheritance assumption — it is the one meta-object base, the rest are the
// interfaces. The row order is the base order (checked at compile time below).
// ---------------------------------------------------------------------------
struct multi_widget_t : object_t, IFoo, IBar, IBaz {
    OBJECT
public:
    int foo_value() const override { return 7; }
    std::string_view bar_name() const override { return "multi-bar"; }
    int baz_value() const override { return 9; }
};
META_OBJECT(multi_widget_t);

// The interface rows of multi_widget_t are its interface BASES in declaration order: IFoo, IBar,
// IBaz. No public query exposes interfaces by index, so the row order is pinned here, on the
// compile-time table build_meta builds.
consteval bool multi_rows_are_in_base_order() {
    constexpr auto table = meta_info_t::build_meta<multi_widget_t>();
    constexpr auto rows = table.interface_infos.to_span();
    if (rows.size() != 3) {
        return false;
    }
    return std::string_view(rows[0].iid) == "com.example.IFoo/1.0"
        && std::string_view(rows[1].iid) == "com.example.IBar/1.0"
        && std::string_view(rows[2].iid) == "com.example.IBaz/1.0";
}
static_assert(multi_rows_are_in_base_order(),
    "the interface table must hold one row per interface base, in base declaration order");

// A class with exactly TWO interfaces, declared in NON-alphabetical order (IBaz before IBar): the
// rows follow the base declaration order, not the IID text or the hash layout.
struct dual_widget_t : object_t, IBaz, IBar {
    OBJECT
public:
    std::string_view bar_name() const override { return "dual-bar"; }
    int baz_value() const override { return 15; }
};
META_OBJECT(dual_widget_t);

consteval bool dual_rows_are_in_base_order() {
    constexpr auto table = meta_info_t::build_meta<dual_widget_t>();
    constexpr auto rows = table.interface_infos.to_span();
    if (rows.size() != 2) {
        return false;
    }
    return std::string_view(rows[0].iid) == "com.example.IBaz/1.0"
        && std::string_view(rows[1].iid) == "com.example.IBar/1.0";
}
static_assert(dual_rows_are_in_base_order(),
    "two interfaces must be recorded in base declaration order (IBaz, then IBar)");

// ---------------------------------------------------------------------------
// (3) A class with no interface at all (the negative case).
// ---------------------------------------------------------------------------
struct plain_widget_t : object_t {
    OBJECT
public:
    int nothing() const { return 0; }
};
META_OBJECT(plain_widget_t);

// ---------------------------------------------------------------------------
// (4) Inheritance: the base declares the interface (by inheriting it), the derived class declares
// none of its own and must still answer through the chain. derived_bar_widget_t adds a second one
// on top.
// ---------------------------------------------------------------------------
struct base_widget_t : object_t, IFoo {
    OBJECT
public:
    int foo_value() const override { return 11; }
};
META_OBJECT(base_widget_t);

struct derived_widget_t : base_widget_t {
    OBJECT
public:
    int extra() const { return 1; }
};
META_OBJECT(derived_widget_t);

struct derived_bar_widget_t : base_widget_t, IBar {
    OBJECT
public:
    std::string_view bar_name() const override { return "derived-bar"; }
    int foo_value() const override { return 21; }
};
META_OBJECT(derived_bar_widget_t);

// ---------------------------------------------------------------------------
// (7) A GADGET can implement an interface too. A gadget is not an object_t, so the old object_t-bound
// cast thunk could not be formed for it at all — build_meta<T>() would not compile. The widened
// void* thunk has one shape for OBJECT and GADGET alike. IFoo is a base and the interface it
// implements, so the row comes from that base, exactly as for an OBJECT class. The gadget base above
// is a value type with no polymorphic accessor, so it does NOT join a chain: a GADGET is always a
// root, and cast<gadget_base_t> on the derived value answers nullptr.
// ---------------------------------------------------------------------------
struct gadget_base_t {
    GADGET
public:
    int base_value() const { return 3; }
};
META_OBJECT(gadget_base_t);

struct gadget_widget_t : gadget_base_t, IFoo {
    GADGET
public:
    int foo_value() const override { return 5; }
};
META_OBJECT(gadget_widget_t);

// ---------------------------------------------------------------------------
// (10) ACCESS: a PRIVATE interface base is implemented but not exposed.
//
// The class really does implement IFoo (foo_value is overridden), but the inheritance is private:
// at any ordinary (non-member, non-friend) site there is no conversion from hidden_widget_t* to
// IFoo*. The table must not report an interface its consumer could not static_cast to, so the base
// scan enumerates with std::meta::access_context::unprivileged() and this base is dropped.
// ---------------------------------------------------------------------------
struct hidden_widget_t : object_t, private IFoo {
    OBJECT
public:
    int foo_value() const override { return 13; }
};
META_OBJECT(hidden_widget_t);

// The consumer-side fact that makes the access rule necessary: the conversion does not exist.
static_assert(!std::is_convertible_v<hidden_widget_t*, IFoo*>,
    "a private interface base must not be convertible at a consumer's site");
// The base is really there, just inaccessible: unprivileged() drops exactly the inaccessible ones,
// and the class reports that it has one (so the empty row list below is not an empty base list).
consteval bool hidden_ifoo_is_visible_unprivileged() {
    for (auto base : std::meta::bases_of(^^hidden_widget_t, std::meta::access_context::unprivileged())) {
        if (std::meta::type_of(base) == ^^IFoo) {
            return true;
        }
    }
    return false;
}
static_assert(!hidden_ifoo_is_visible_unprivileged(),
    "access_context::unprivileged() must not yield the private interface base");
static_assert(std::meta::has_inaccessible_bases(^^hidden_widget_t, std::meta::access_context::unprivileged()),
    "hidden_widget_t must have an inaccessible base, or the probe above proves nothing");

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    std::printf("== M16-A: interface conversion ==\n");

    // The interfaces a class declares ITSELF: interface_count() counts the whole chain, so the own
    // table's size is the difference from the base chain's count (0 for a class with no base).
    // This is the public-API form of the old own-table size / empty checks.
    const auto own_interface_count = [](const meta_object_t* meta) noexcept {
        return meta->interface_count()
            - ((meta->super_class() != nullptr) ? meta->super_class()->interface_count() : 0);
    };

    // (1) One interface: the string IID resolves, and the returned pointer is usable. The row is
    // INFERRED from the public IFoo base — found by implements() here and by cast<T> in section (5).
    foo_widget_t foo;
    foo.set_parent(root);
    void* raw_foo = foo.meta_object()->cast(&foo, "com.example.IFoo/1.0");
    CHECK(raw_foo != nullptr);
    // The thunk is the ordinary base-class pointer adjustment, so it yields exactly what an implicit
    // conversion yields.
    CHECK(raw_foo == static_cast<IFoo*>(&foo));
    CHECK(static_cast<IFoo*>(raw_foo)->foo_value() == 42);
    CHECK(foo.meta_object()->implements("com.example.IFoo/1.0"));
    CHECK(!foo.meta_object()->implements("com.example.IBar/1.0"));
    CHECK(foo.meta_object()->interface_count() == 1);
    // The class's own table holds exactly that one entry, keyed by the IID text: the own count is
    // the chain count minus the base's, and the entry is keyed by the exact IID string (implements()
    // is the exact-key lookup; the base does not answer it, so the hit is in foo's own table).
    CHECK(own_interface_count(foo.meta_object()) == 1);
    CHECK(foo.meta_object()->implements("com.example.IFoo/1.0")
        && !foo.meta_object()->super_class()->implements("com.example.IFoo/1.0"));

    // (3) An unrelated class: nothing resolves, nothing is counted.
    plain_widget_t plain;
    plain.set_parent(root);
    CHECK(plain.meta_object()->cast(&plain, "com.example.IFoo/1.0") == nullptr);
    CHECK(plain.meta_object()->cast(&plain, "com.example.IBar/1.0") == nullptr);
    CHECK(plain.meta_object()->cast(&plain, "") == nullptr);
    CHECK(!plain.meta_object()->implements("com.example.IFoo/1.0"));
    CHECK(!plain.meta_object()->implements(""));
    CHECK(plain.meta_object()->interface_count() == 0);
    CHECK(own_interface_count(plain.meta_object()) == 0);

    // (2) Several interfaces: every IID resolves to its own sub-object.
    multi_widget_t multi;
    multi.set_parent(root);
    void* raw_multi_foo = multi.meta_object()->cast(&multi, "com.example.IFoo/1.0");
    void* raw_multi_bar = multi.meta_object()->cast(&multi, "com.example.IBar/1.0");
    void* raw_multi_baz = multi.meta_object()->cast(&multi, "com.example.IBaz/1.0");
    CHECK(raw_multi_foo != nullptr);
    CHECK(raw_multi_bar != nullptr);
    CHECK(raw_multi_baz != nullptr);
    CHECK(raw_multi_foo == static_cast<IFoo*>(&multi));
    CHECK(raw_multi_bar == static_cast<IBar*>(&multi));
    CHECK(raw_multi_baz == static_cast<IBaz*>(&multi));
    // A virtual call through each pointer proves the conversion landed on the right sub-object.
    CHECK(static_cast<IFoo*>(raw_multi_foo)->foo_value() == 7);
    CHECK(static_cast<IBar*>(raw_multi_bar)->bar_name() == "multi-bar");
    CHECK(static_cast<IBaz*>(raw_multi_baz)->baz_value() == 9);
    CHECK(multi.meta_object()->implements("com.example.IFoo/1.0"));
    CHECK(multi.meta_object()->implements("com.example.IBar/1.0"));
    CHECK(multi.meta_object()->implements("com.example.IBaz/1.0"));
    CHECK(multi.meta_object()->interface_count() == 3);
    CHECK(own_interface_count(multi.meta_object()) == 3);
    // An IID nobody declares is still a miss.
    CHECK(multi.meta_object()->cast(&multi, "com.example.IMissing/1.0") == nullptr);

    // (2b) Two interfaces, declared IBaz then IBar: both rows exist in that order (the compile-time
    // static_assert above pins the row order; these checks pin that both are reachable at run time).
    dual_widget_t dual;
    dual.set_parent(root);
    CHECK(dual.meta_object()->interface_count() == 2);
    CHECK(dual.meta_object()->implements("com.example.IBaz/1.0"));
    CHECK(dual.meta_object()->implements("com.example.IBar/1.0"));
    CHECK(dual.meta_object()->cast(&dual, "com.example.IBaz/1.0") == static_cast<IBaz*>(&dual));
    CHECK(dual.meta_object()->cast(&dual, "com.example.IBar/1.0") == static_cast<IBar*>(&dual));
    CHECK(cast<IBaz>(&dual) == static_cast<IBaz*>(&dual));
    CHECK(cast<IBar>(&dual) == static_cast<IBar*>(&dual));

    // (4) Inheritance, first level: the derived class declares no interface of its own.
    derived_widget_t derived;
    derived.set_parent(root);
    void* derived_foo = derived.meta_object()->cast(&derived, "com.example.IFoo/1.0");
    CHECK(derived_foo != nullptr);
    CHECK(derived_foo == static_cast<IFoo*>(&derived));
    CHECK(static_cast<IFoo*>(derived_foo)->foo_value() == 11);
    CHECK(derived.meta_object()->implements("com.example.IFoo/1.0"));
    CHECK(derived.meta_object()->interface_count() == 1);
    // Own table empty: the hit really came from the base's meta-object along super_class().
    CHECK(own_interface_count(derived.meta_object()) == 0);
    CHECK(derived.meta_object()->super_class() == &base_widget_t::static_meta_object);

    // Inheritance, second level: the base's IFoo plus the derived's own IBar.
    derived_bar_widget_t derived_bar;
    derived_bar.set_parent(root);
    CHECK(derived_bar.meta_object()->interface_count() == 2);
    CHECK(derived_bar.meta_object()->implements("com.example.IFoo/1.0"));
    CHECK(derived_bar.meta_object()->implements("com.example.IBar/1.0"));
    void* derived_bar_foo = derived_bar.meta_object()->cast(&derived_bar, "com.example.IFoo/1.0");
    void* derived_bar_bar = derived_bar.meta_object()->cast(&derived_bar, "com.example.IBar/1.0");
    CHECK(derived_bar_foo != nullptr);
    CHECK(derived_bar_bar != nullptr);
    // The inherited entry down-casts through the DECLARING class (base_widget_t), which is an
    // ordinary upcast here and must still land on the right sub-object.
    CHECK(derived_bar_foo == static_cast<IFoo*>(&derived_bar));
    CHECK(derived_bar_bar == static_cast<IBar*>(&derived_bar));
    CHECK(static_cast<IFoo*>(derived_bar_foo)->foo_value() == 21);
    CHECK(static_cast<IBar*>(derived_bar_bar)->bar_name() == "derived-bar");

    // (5) The namespace-level cast<T> reads T's make_interface annotation and returns the same
    // pointer the table's IID lookup returns.
    CHECK(cast<IFoo>(&foo) == static_cast<IFoo*>(foo.meta_object()->cast(&foo, "com.example.IFoo/1.0")));
    CHECK(cast<IFoo>(&multi) == static_cast<IFoo*>(multi.meta_object()->cast(&multi, "com.example.IFoo/1.0")));
    CHECK(cast<IBar>(&multi) == static_cast<IBar*>(multi.meta_object()->cast(&multi, "com.example.IBar/1.0")));
    CHECK(cast<IBaz>(&multi) == static_cast<IBaz*>(multi.meta_object()->cast(&multi, "com.example.IBaz/1.0")));
    CHECK(cast<IFoo>(&derived) != nullptr);
    CHECK(cast<IFoo>(&derived)->foo_value() == 11);
    CHECK(cast<IBar>(&derived_bar) != nullptr);
    CHECK(cast<IBar>(&derived_bar)->bar_name() == "derived-bar");
    // The negative case through the template form too (hit and miss).
    CHECK(cast<IFoo>(&plain) == nullptr);
    CHECK(cast<IBar>(&foo) == nullptr);

    // The virtual accessor carries the conversion: calling through an object_t* pointer reaches the
    // object's own (derived) table.
    object_t* as_object = &derived_bar;
    CHECK(as_object->meta_object()->cast(as_object, "com.example.IFoo/1.0") == static_cast<IFoo*>(&derived_bar));
    CHECK(cast<IBar>(as_object) == static_cast<IBar*>(&derived_bar));

    // Defensive: a null object and an unknown IID are misses, not crashes.
    CHECK(derived_bar.meta_object()->cast(nullptr, "com.example.IFoo/1.0") == nullptr);
    CHECK(derived_bar.meta_object()->cast(&derived_bar, "com.example.IMissing/1.0") == nullptr);

    // (6) interface_count follows the chain: base 1, derived (no own entry) 1, derived + own IBar 2.
    CHECK(base_widget_t::static_meta_object.interface_count() == 1);
    CHECK(derived_widget_t::static_meta_object.interface_count() == 1);
    CHECK(derived_bar_widget_t::static_meta_object.interface_count() == 2);
    CHECK(foo_widget_t::static_meta_object.interface_count() == 1);
    CHECK(multi_widget_t::static_meta_object.interface_count() == 3);
    CHECK(plain_widget_t::static_meta_object.interface_count() == 0);

    // (7) A GADGET records its interface exactly like an OBJECT class: the row is built and answers
    // through the same queries. The namespace-level cast<T> recovers it through the gadget's STATIC
    // table (a gadget has no virtual accessor to reach a runtime one).
    CHECK(gadget_widget_t::static_meta_object.implements("com.example.IFoo/1.0"));
    CHECK(!gadget_widget_t::static_meta_object.implements("com.example.IBar/1.0"));
    CHECK(gadget_widget_t::static_meta_object.interface_count() == 1);
    CHECK(own_interface_count(&gadget_widget_t::static_meta_object) == 1);
    // A GADGET is a value type with no polymorphic accessor, so it is a ROOT: `GADGET : GADGET` is
    // allowed but does not chain, and the base's rows stay behind gadget_base_t::static_meta_object.
    CHECK(gadget_widget_t::static_meta_object.super_class() == nullptr);
    gadget_widget_t gadget_widget;
    CHECK(cast<IFoo>(&gadget_widget) == static_cast<IFoo*>(&gadget_widget));
    CHECK(cast<IFoo>(&gadget_widget)->foo_value() == 5);
    CHECK(cast<IBar>(&gadget_widget) == nullptr);   // interface miss on a gadget

    // (10) A private interface base is implemented but NOT reported: unprivileged() drops it from the
    // base scan, so there is no row to find — implements() is false, the table cast is null, and the
    // namespace-level cast<IFoo> (which asks that same table) is null too. Silently exposing it
    // would be wrong: the consumer's static_cast<IFoo*> would not compile for this class.
    hidden_widget_t hidden;
    hidden.set_parent(root);
    CHECK(hidden.foo_value() == 13);   // the interface IS implemented...
    CHECK(!hidden.meta_object()->implements("com.example.IFoo/1.0"));
    CHECK(hidden.meta_object()->interface_count() == 0);
    CHECK(own_interface_count(hidden.meta_object()) == 0);
    CHECK(hidden.meta_object()->cast(&hidden, "com.example.IFoo/1.0") == nullptr);
    CHECK(cast<IFoo>(&hidden) == nullptr);

    // nullptr in -> nullptr out, for an interface target, an OBJECT target and a GADGET target,
    // through both entry points.
    CHECK(cast<IFoo>(static_cast<foo_widget_t*>(nullptr)) == nullptr);
    CHECK(cast<IFoo>(static_cast<gadget_widget_t*>(nullptr)) == nullptr);
    CHECK(cast<plain_widget_t>(static_cast<object_t*>(nullptr)) == nullptr);
    CHECK(cast<gadget_base_t>(static_cast<gadget_widget_t*>(nullptr)) == nullptr);
    CHECK(foo_widget_t::static_meta_object.cast(static_cast<foo_widget_t*>(nullptr)) == nullptr);
    CHECK(foo_widget_t::static_meta_object.cast(static_cast<object_t*>(nullptr)) == nullptr);

    // (8) cast<T> for a CLASS of the OBJECT kind: a hit for the exact class and for a base class
    // (the meta-object chain is what is checked), a miss for an unrelated sibling.
    CHECK(cast<foo_widget_t>(&foo) == &foo);
    CHECK(cast<foo_widget_t>(static_cast<object_t*>(&foo)) == &foo);   // through a base pointer
    CHECK(cast<multi_widget_t>(&multi) == &multi);
    CHECK(cast<foo_widget_t>(&multi) == nullptr);                      // sibling: no relation
    CHECK(cast<plain_widget_t>(&foo) == nullptr);                      // derived of the same root only
    CHECK(cast<derived_widget_t>(static_cast<object_t*>(&derived)) == &derived);
    CHECK(cast<base_widget_t>(static_cast<object_t*>(&derived_bar))
          == static_cast<base_widget_t*>(&derived_bar));
    CHECK(cast<derived_widget_t>(static_cast<object_t*>(&derived_bar)) == nullptr);  // base is not derived

    // cast<T> for a CLASS of the GADGET kind. A gadget has no chain, but a gadget is NOT polymorphic:
    // its static type IS its real type, so the compiler's own relation answers the up-cast — here
    // `gadget_widget_t*` is convertible to `gadget_base_t*`. A down-cast (the base pointer back to the
    // derived type) has no runtime information to consult, so it answers nullptr.
    gadget_base_t gadget_base;
    CHECK(cast<gadget_widget_t>(&gadget_widget) == &gadget_widget);
    CHECK(cast<gadget_base_t>(&gadget_widget) == static_cast<gadget_base_t*>(&gadget_widget));
    CHECK(cast<gadget_base_t>(&gadget_base) == &gadget_base);
    CHECK(cast<gadget_widget_t>(&gadget_base) == nullptr);             // the base is not a derived
    CHECK(cast<gadget_base_t>(&foo) == nullptr);                       // cross-kind miss
    CHECK(cast<foo_widget_t>(&gadget_widget) == nullptr);              // cross-kind miss, other way

    // (9) The table-level cast<U>(U*): it answers "is this object an instance of my class, or of a
    // class derived from it", using the object's RUNTIME table through object_t's accessor and the
    // static table for a gadget.
    CHECK(foo_widget_t::static_meta_object.cast(&foo) == static_cast<void*>(&foo));
    CHECK(foo_widget_t::static_meta_object.cast(&multi) == nullptr);   // sibling
    CHECK(base_widget_t::static_meta_object.cast(&derived) == static_cast<void*>(&derived));
    CHECK(derived_widget_t::static_meta_object.cast(static_cast<object_t*>(&derived))
          == static_cast<void*>(&derived));
    CHECK(plain_widget_t::static_meta_object.cast(static_cast<object_t*>(&foo)) == nullptr);
    CHECK(gadget_base_t::static_meta_object.cast(&gadget_widget) == nullptr);  // no chain between gadgets
    CHECK(gadget_widget_t::static_meta_object.cast(&gadget_base) == nullptr);

    // The const form: the same dispatch, but the result is a pointer to const (the const overload
    // never hands out a mutable pointer for a const source).
    const foo_widget_t& const_foo = foo;
    const IFoo* const_ifoo = cast<const IFoo>(&const_foo);
    CHECK(const_ifoo == static_cast<const IFoo*>(&foo));
    CHECK(const_ifoo->foo_value() == 42);
    CHECK(cast<const foo_widget_t>(&const_foo) == &foo);
    CHECK(cast<const plain_widget_t>(&const_foo) == nullptr);          // const class miss
    CHECK(cast<const IFoo>(static_cast<const foo_widget_t*>(nullptr)) == nullptr);
    const object_t* const_object = &foo;
    CHECK(cast<const foo_widget_t>(const_object) == &foo);             // const base pointer
    const gadget_widget_t& const_gadget = gadget_widget;
    CHECK(cast<const IFoo>(&const_gadget) == static_cast<const IFoo*>(&gadget_widget));

    if (g_failures == 0) {
        std::printf("  [ok] all interface checks passed\n");
        return 0;
    }
    std::printf("  [FAIL] %d check(s) failed\n", g_failures);
    return 1;
}
