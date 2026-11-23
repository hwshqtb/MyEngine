// M10 test: property traits, derived property queries, method kind, parameter names, enum scope,
// and the bare-name / reflection accessor contract (writer, reseter, notifier).

#include <meta>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <hwshqtb/metaobject/annotation.h>
#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_constructor_t.h>
#include <hwshqtb/metaobject/meta_enum_t.h>
#include <hwshqtb/metaobject/meta_info_t.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_property_t.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/property_t.h>

using namespace hwshqtb::metaobject;

namespace m10_metadata_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// ---------------------------------------------------------------------------
// Log capture: a named-but-unresolved accessor reports a Critical, and the test asserts on the
// record instead of letting the default sink print it.
// ---------------------------------------------------------------------------
struct record_t {
    std::string category;
    log_level_enum level = log_level_enum::DEBUG;
    std::string message;
};

std::vector<record_t>& records() {
    static std::vector<record_t> captured;
    return captured;
}

// ---------------------------------------------------------------------------
// A base class carrying the accessors a derived property names. The compile-time accessor scan
// covers the DECLARING class only, so every one of them has to be resolved at run time.
// ---------------------------------------------------------------------------
struct meta_base_t : object_t {
    OBJECT
public:
    void set_inherited(int v) { _inherited = v; }
    void reset_inherited() { _inherited = 0; }
    int inherited_value() const { return _inherited; }

    SIGNAL(void inherited_changed(int v) const)

private:
    int _inherited = 0;
};
META_OBJECT(meta_base_t);

struct meta_test_t : object_t {
    OBJECT
public:
    /// The `bound` property's data member is wired to its own notifier by the class: the meta layer no
    /// longer installs that subscription (M17 stage 2), and property_t::from() needs a notifier.
    meta_test_t() {
        _bound_notifier =
            _bound_data.on_value_changed([this] { bound_changed(_bound_data.value().get<int>()); });
    }

    enum struct Mode { None = 0, Fast = 1, Safe = 2 };

    // Bare names, resolved by reflection. This one matches at the EXACT level: writer and notifier
    // take a `double` (the property type itself), the reseter takes no parameter.
    [[= make_property("value", "set_value", "value_changed", "", property_trait_flag::USER)]]
    double value() const { return _value; }

    void set_value(double v) {
        _value = v;
        value_changed(v);
    }

    SIGNAL(void value_changed(double v) const)

    [[= make_property("ro")]]
    int ro() const { return 7; }

    // M17: a writable property whose backend is a bindable. Nothing is declared: bindability is
    // value-derived, so naming a notifier (plus a writer, which installing a binding needs) IS the
    // declaration.
    [[= make_property("bound", "set_bound", "bound_changed")]]
    int bound() const { return _bound_data.value().get<int>(); }

    void set_bound(int v) { _bound_data.set_value(any_t(v)); }

    SIGNAL(void bound_changed(int new_value) const)

    [[= make_property("code_only", "", "", "", property_trait_flag::HIDDEN)]]
    int code_only() const { return 1; }

    [[= make_property("mode")]]
    Mode mode() const { return Mode::Fast; }

    int add(int a, int b) const { return a + b; }

    // Reflection-matched writer: set_sig_writer has exactly one parameter, of the property type.
    [[= make_property("sig_writer", "set_sig_writer")]]
    double sig_writer() const { return _sig_value; }

    void set_sig_writer(double v) { _sig_value = v; }

    // Reflection-matched reseter: reset_sig_value takes no parameter (the role's only shape).
    [[= make_property("sig_resetter", "", "", "reset_sig_value")]]
    double sig_resetter() const { return _sig_value; }

    void reset_sig_value() { _sig_value = 0.0; }

    // Reflection-matched notifier, one-parameter form (exact: double is the property type).
    [[= make_property("sig_notifier_arg", "", "sig_value_changed")]]
    double sig_notifier_arg() const { return _sig_value; }

    SIGNAL(void sig_value_changed(double v) const)

    // Reflection-matched notifier, no-parameter form — the second and last shape tried.
    [[= make_property("sig_notifier_zero", "", "sig_ticked")]]
    double sig_notifier_zero() const { return _sig_value; }

    SIGNAL(void sig_ticked() const)

    // std::string property + std::string parameter: the compile-time layer compares the REFLECTED
    // types, so the alias spelling std::string never appears (the parameter is the identity spelling
    // of std::basic_string<char>, see meta_info_t::original_name).
    [[= make_property("sig_alias", "set_sig_alias")]]
    std::string sig_alias() const { return _sig_text; }

    void set_sig_alias(std::string v) { _sig_text = std::move(v); }

    // Exact cv/ref match: the getter's type is const std::string& and so is the writer's parameter.
    [[= make_property("sig_cref", "set_sig_cref")]]
    const std::string& sig_cref() const { return _sig_text; }

    void set_sig_cref(const std::string& v) { _sig_text = v; }

    // The documented qualifier rule at the BARE-TYPE level: the property type is `double`, the
    // parameter is `const double&` — not an exact match, but the only bare-type candidate, so it is
    // accepted.
    [[= make_property("cref_writer", "set_cref_writer")]]
    double cref_writer() const { return _cref_value; }

    void set_cref_writer(const double& v) { _cref_value = v; }

    // The same rule for a notifier: `std::string` property, `const std::string&` signal parameter.
    [[= make_property("cref_notified", "", "cref_changed")]]
    std::string cref_notified() const { return _sig_text; }

    SIGNAL(void cref_changed(const std::string& value) const)

    // A name that matches nothing at all: the compile-time pass leaves it unbound and the run-time
    // pass reports a Critical (once per handle and role) instead of throwing.
    [[= make_property("unresolved", "no_such_writer", "no_such_notifier")]]
    double unresolved() const { return 0.0; }

    double _value = 0.0;
    property_t _bound_data{ any_t(2) };
    double _sig_value = 0.0;
    double _cref_value = 0.0;
    std::string _sig_text;

private:
    /// What _bound_data.on_value_changed() returned; destroying it unregisters the subscription.
    property_change_handler_t _bound_notifier;
};
META_OBJECT(meta_test_t);

// The compile-time scan of meta_derived_t sees only its own members, so all three bare names miss
// there; the property still resolves them, through the run-time chain walk.
struct meta_derived_t : meta_base_t {
    OBJECT
public:
    [[= make_property("inherited", "set_inherited", "inherited_changed", "reset_inherited")]]
    int inherited() const { return inherited_value(); }
};
META_OBJECT(meta_derived_t);

// B1 regression: a derived property naming the base class's ZERO-argument signal and a zero-argument
// reseter. The compile-time scan sees only the declaring class, so both names take the run-time chain
// walk — which must reproduce the compile-time sequence (notifier: one-argument exact, one-argument
// bare, then no-argument), or the property comes out constant and logs a misleading Critical.
struct zero_base_t : object_t {
    OBJECT
public:
    SIGNAL(void changed() const)

    void reset_value() { _zero = 0; }

    int base_value() const { return _zero; }

private:
    int _zero = 0;
};
META_OBJECT(zero_base_t);

struct zero_derived_t : zero_base_t {
    OBJECT
public:
    [[= make_property("value", "", "changed", "reset_value")]]
    int value() const { return base_value(); }
};
META_OBJECT(zero_derived_t);

// The qualifier rule's tie-break, with alias sugar in play. The getter returns std::string BY VALUE,
// so GCC reflects its return type with the alias spelling while the parameter keeps its own reflected
// spelling. The exact level compares the types with the alias spelling
// removed, so set_text(std::string) IS the exact match and wins over set_text(const std::string&) —
// the pair must not be reported as ambiguous.
struct overload_probe {
    GADGET

public:
    [[= make_property("text", "set_text")]]
    std::string text() const { return _text; }

    void set_text(std::string v) { _text = std::move(v); }
    void set_text(const std::string& v) { _text = v; }

private:
    std::string _text;
};
META_OBJECT(overload_probe);

// ---------------------------------------------------------------------------
// Constructor collision: a bare accessor name that equals the CLASS name.
//
// Constructors have their own table (constructor_infos) and are NOT in the function table any more, so the
// run-time accessor fallback builds a key like "collide_t(int)" that no method_infos row owns. The
// fallback must report an ordinary MISS (it did hit the constructor row before the split and had to
// reject it). The three properties below exercise writer, notifier and reseter against BOTH
// constructor shapes (the one-argument form spells `collide_t(int)`; the no-argument reseter form
// spells `collide_t()`), and the constructor itself stays reachable only through constructor().
// ---------------------------------------------------------------------------
struct collide_t : object_t {
    OBJECT
public:
    collide_t() = default;

    explicit collide_t(int seed):
        _value(seed) { }

    /// Writer name == class name, and the property type int matches the constructor's parameter.
    [[= make_property("p", "collide_t")]]
    int p() const { return _value; }

    /// Notifier name == class name, one-parameter form (the shape a notifier tries first).
    [[= make_property("q", "", "collide_t")]]
    int q() const { return _value; }

    /// Reseter name == class name, no-argument form (matches the default constructor).
    [[= make_property("r", "", "", "collide_t")]]
    int r() const { return _value; }

private:
    int _value = 0;
};
META_OBJECT(collide_t);

// ---------------------------------------------------------------------------
// L13: run-time accessor ambiguity. The compile-time scan covers the DECLARING class only, so a
// derived property that names a BASE class's accessor reaches the run-time chain walk. When that
// name matches two overloads at the same level — here `set_amb(int)` and `set_amb(int) const`,
// both exactly the property type `int` — a bare annotation name cannot choose, so the run-time
// pass must report the ambiguity and hand out NO writer (never the first table entry). This is the
// run-time twin of the negative compile-time test m10_negative_ambiguous_accessor.
// ---------------------------------------------------------------------------
struct ambiguous_base_t : object_t {
    OBJECT
public:
    void set_amb(int) { }
    void set_amb(int) const { }

    int base_value() const { return 0; }
};
META_OBJECT(ambiguous_base_t);

struct ambiguous_derived_t : ambiguous_base_t {
    OBJECT
public:
    [[= make_property("amb", "set_amb")]]
    int amb() const { return base_value(); }
};
META_OBJECT(ambiguous_derived_t);

// The L13 control: the same inherited-accessor shape, but with exactly ONE candidate, still
// resolves and writes. (The m10 section (10) inherited property is the other control.)
struct unique_base_t : object_t {
    OBJECT
public:
    void set_uni(int v) { _uni = v; }

    int uni_value() const { return _uni; }

private:
    int _uni = 0;
};
META_OBJECT(unique_base_t);

struct unique_derived_t : unique_base_t {
    OBJECT
public:
    [[= make_property("uni", "set_uni")]]
    int uni() const { return uni_value(); }
};
META_OBJECT(unique_derived_t);

}  // namespace m10_metadata_test

using namespace m10_metadata_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    install_log_handler([](log_level_enum level, const log_context_t& context, std::string_view message) {
        records().push_back(record_t{ std::string(context.category), level, std::string(message) });
        return true; // consumed: the default sink stays silent
    });

    std::printf("== M10: metadata ==\n");
    auto* p = new meta_test_t;
    p->set_parent(root);

    // (1) traits and trait queries
    const meta_property_t value = p->meta_object()->property("value");
    CHECK(value.is_valid());
    CHECK(value.is_user());
    CHECK(!value.is_required());
    CHECK(!value.is_hidden());
    CHECK(!value.is_final());
    CHECK(bool(value.traits() & property_trait_flag::USER));

    // (1b) M17: bindability is value-derived — a property is bindable iff it has a notifier, which is
    // is_notifiable() itself (the old is_bindable() had no other body). This fixture's `value` is
    // notifiable, so it answers that way as well; `ro` (no notifier) does not. The old declared-trait
    // bit is gone: there is nothing in traits() to assert.
    const meta_property_t bound = p->meta_object()->property("bound");
    CHECK(bound.is_valid());
    CHECK(bound.is_notifiable());               // the bindable fact: it has a notifier
    CHECK(bound.is_notifiable());
    CHECK(bound.is_writable());
    CHECK(value.is_notifiable());
    CHECK(property_t::from(*p, value).is_valid());   // bindable exactly because it is notifiable

    // (1c) M17: property_t::from() is the one way to reach a property as a bindable. The handle is a
    // REFERENCE view of the object's property (it owns no value) and carries the property's value
    // type; the binding itself is installed with property_t::set_binding().
    const property_t bound_view = property_t::from(*p, bound);
    CHECK(bound_view.is_valid());
    CHECK(!bound_view.is_owning());
    CHECK(!bound_view.has_binding());
    CHECK(bound_view.type().is<int>());
    // The trait is not the admission ticket: any NOTIFIABLE property can be viewed, so the non-Bindable
    // `value` property is reachable too.
    const property_t value_view = property_t::from(*p, value);
    CHECK(value_view.is_valid());
    CHECK(value_view.type().is<double>());

    // (2) derived queries
    CHECK(value.is_valid());                 // readable: every valid property row has a reader
    CHECK(value.is_writable());
    CHECK(value.is_notifiable());
    CHECK(!value.is_constant());

    const meta_property_t ro = p->meta_object()->property("ro");
    CHECK(ro.is_valid());
    CHECK(ro.is_valid());                    // readable: every valid property row has a reader
    CHECK(!ro.is_writable());
    CHECK(!ro.is_notifiable());
    CHECK(ro.is_constant());
    CHECK(!ro.is_resettable());
    CHECK(!ro.is_notifiable());         // no notifier, so not bindable (value-derived rule)
    // ... and a property that cannot notify cannot be viewed at all: from() throws instead of handing
    // out an invalid handle (the delete half of M17 stage 2).
    bool ro_threw = false;
    try {
        (void)property_t::from(*p, ro);
    }
    catch (const bad_property_t&) {
        ro_threw = true;
    }
    CHECK(ro_threw);

    // (3) Hidden is the inverse of designable
    const meta_property_t code_only = p->meta_object()->property("code_only");
    CHECK(code_only.is_hidden());
    CHECK(!code_only.designable());

    // (4) enumerator() associates an enum-typed property with its enum
    const meta_property_t mode = p->meta_object()->property("mode");
    CHECK(mode.is_valid());
    const meta_enum_t mode_enum = mode.enumerator();
    CHECK(mode_enum.is_valid());
    CHECK(mode_enum.name() == "Mode");
    CHECK(mode_enum.scope() == "meta_test_t");
    CHECK(mode_enum.key_count() == 3);
    CHECK(mode_enum.is_flag());

    // (5) the type-based entry point agrees
    const meta_enum_t by_type = meta_enum_t::from_type<meta_test_t>("Mode");
    CHECK(by_type.is_valid());
    CHECK(by_type.name() == mode_enum.name());
    CHECK(by_type.scope() == mode_enum.scope());

    // (6) a non-enum property has no enumerator
    CHECK(!ro.enumerator().is_valid());

    // (7) method_kind_enum
    const meta_method_t add = p->meta_object()->method("add(int,int) const");
    CHECK(add.is_valid());
    CHECK(add.method_kind() == method_kind_enum::PLAIN_METHOD);
    CHECK(!add.is_signal());

    const meta_method_t sig = p->meta_object()->signal("value_changed(double) const");
    CHECK(sig.is_valid());
    CHECK(sig.method_kind() == method_kind_enum::SIGNAL);
    CHECK(sig.is_signal());

    // (8) parameter names
    CHECK(add.param_count() == 2);
    CHECK(add.param_name(0) == "a");
    CHECK(add.param_name(1) == "b");
    CHECK(add.param_name(99).empty());
    CHECK(sig.param_name(0) == "v");

    // (9) Bare-name accessors resolved by REFLECTION. writer: one parameter of the property type.
    const meta_property_t sig_writer = p->meta_object()->property("sig_writer");
    CHECK(sig_writer.is_valid());
    CHECK(sig_writer.is_writable());
    const meta_method_t sig_writer_method = sig_writer.writer();
    CHECK(sig_writer_method.is_valid());
    CHECK(!sig_writer_method.signature().empty());
    CHECK(sig_writer_method.signature() == "set_sig_writer(double)");
    CHECK(sig_writer_method.invoke(*p, 4.5).is_valid());
    CHECK(p->sig_writer() == 4.5);

    // reseter: the no-argument shape.
    const meta_property_t sig_resetter = p->meta_object()->property("sig_resetter");
    CHECK(sig_resetter.is_resettable());
    CHECK(sig_resetter.reseter().signature() == "reset_sig_value()");
    CHECK(sig_resetter.reseter().invoke(*p).is_valid());
    CHECK(p->sig_writer() == 0.0);

    // notifier: the one-parameter form is tried first, the no-argument form second.
    const meta_property_t sig_notifier_arg = p->meta_object()->property("sig_notifier_arg");
    CHECK(sig_notifier_arg.is_notifiable());
    CHECK(sig_notifier_arg.notifier().signature() == "sig_value_changed(double) const");
    CHECK(sig_notifier_arg.notifier().is_signal());
    const meta_property_t sig_notifier_zero = p->meta_object()->property("sig_notifier_zero");
    CHECK(sig_notifier_zero.is_notifiable());
    CHECK(sig_notifier_zero.notifier().signature() == "sig_ticked() const");

    // Every stored column and signature key keeps the ORIGINAL spelling of a type; the canonical
    // `std::basic_string<char>` is only the user-facing alias. Build the expectations from the library,
    // never from a literal, so they follow a standard library without the `std::__cxx11` namespace.
    const std::string std_string_name(meta_info_t::original_name<std::string>());

    // The compile-time layer compares REAL types: the writer is matched with the reflected
    // std::string parameter, not with an annotation's alias spelling.
    const meta_property_t sig_alias = p->meta_object()->property("sig_alias");
    CHECK(sig_alias.is_writable());
    CHECK(sig_alias.writer().signature() == "set_sig_alias(" + std_string_name + ")");

    // EXACT level: the getter's const std::string& and the writer's parameter are the same type,
    // qualifiers included.
    const meta_property_t sig_cref = p->meta_object()->property("sig_cref");
    CHECK(sig_cref.type_name() == "const " + std_string_name + "&");
    CHECK(sig_cref.is_writable());
    CHECK(sig_cref.writer().signature() == "set_sig_cref(const " + std_string_name + "&)");

    // B2: type() / read() resolve the SAME decayed key the registry holds. The property column keeps
    // the cvref spelling ("const <original>&") while the registry key is the bare name
    // ("<original>"), so resolved_type() must strip it exactly like the method path.
    CHECK(sig_cref.type(*p).is_valid());
    CHECK(sig_cref.type(*p).is<std::string>());
    CHECK(sig_cref.read(*p).is_initialized());
    CHECK(sig_cref.read(*p).get<std::string>().empty());

    // BARE level: neither parameter equals the property type exactly (`double` vs `const double&`,
    // `<original>` vs `const <original>&`), and each is the only candidate
    // at that level, so the documented qualifier rule accepts it.
    const meta_property_t cref_writer = p->meta_object()->property("cref_writer");
    CHECK(cref_writer.type_name() == "double");
    CHECK(cref_writer.is_writable());
    CHECK(cref_writer.writer().signature() == "set_cref_writer(const double&)");
    CHECK(bool(cref_writer.writer().param_qualifier(0) & qualifier_flag::CONST));
    CHECK(bool(cref_writer.writer().param_qualifier(0) & qualifier_flag::LVALUE_REF));

    const meta_property_t cref_notified = p->meta_object()->property("cref_notified");
    CHECK(cref_notified.type_name() == std_string_name);
    CHECK(cref_notified.is_notifiable());
    CHECK(cref_notified.notifier().signature() == "cref_changed(const " + std_string_name + "&) const");
    CHECK(cref_notified.notifier().is_signal());
    CHECK(bool(cref_notified.notifier().param_qualifier(0) & qualifier_flag::CONST));
    CHECK(bool(cref_notified.notifier().param_qualifier(0) & qualifier_flag::LVALUE_REF));

    // The rule's tie-break with alias sugar: set_text(std::string) is the EXACT match for a
    // std::string-returning getter (the alias spelling is removed before comparing, cv/ref is not),
    // so the by-value overload wins and the pair is not ambiguous.
    const meta_property_t overload_text = overload_probe::static_meta_object.property("text");
    CHECK(overload_text.is_valid());
    CHECK(overload_text.is_writable());
    CHECK(overload_text.writer().signature() == "set_text(" + std_string_name + ")");

    // (10) Inherited accessors: the names belong to meta_base_t, so the compile-time pass (own
    // members only) does not bind them; writer() / reseter() / notifier() resolve them at run time
    // along the meta-object chain, and the handles point at the base's table.
    auto* d = new meta_derived_t;
    d->set_parent(root);
    const meta_property_t inherited = d->meta_object()->property("inherited");
    CHECK(inherited.is_valid());
    CHECK(inherited.type_name() == "int");
    CHECK(inherited.is_writable());
    CHECK(inherited.writer().is_valid());
    CHECK(inherited.writer().name() == "set_inherited");
    CHECK(inherited.writer().signature() == "set_inherited(int)");
    CHECK(inherited.writer().invoke(*d, 41).is_valid());
    CHECK(d->inherited_value() == 41);

    CHECK(inherited.is_resettable());
    CHECK(inherited.reseter().signature() == "reset_inherited()");
    CHECK(inherited.reseter().invoke(*d).is_valid());
    CHECK(d->inherited_value() == 0);

    CHECK(inherited.is_notifiable());
    CHECK(inherited.notifier().is_valid());
    CHECK(inherited.notifier().is_signal());
    CHECK(inherited.notifier().signature() == "inherited_changed(int) const");

    // The operations (not just the handles) go through the run-time-resolved accessor.
    CHECK(inherited.write(*d, any_t(7)));
    CHECK(d->inherited_value() == 7);
    CHECK(inherited.read(*d).get<int>() == 7);
    CHECK(inherited.reset(*d));
    CHECK(d->inherited_value() == 0);

    // (10b) B1 regression: the property names the BASE class's zero-argument signal and a base-class
    // reseter. The compile-time pass binds neither (own members only), so the run-time chain walk has
    // to reproduce the compile-time notifier sequence — one-argument exact, one-argument bare, then
    // no-argument — or the property comes out constant and reports the misleading Critical.
    auto* z = new zero_derived_t;
    z->set_parent(root);
    const meta_property_t zero_notified = z->meta_object()->property("value");
    const std::size_t logged_before_zero = records().size();
    CHECK(zero_notified.is_valid());
    CHECK(zero_notified.is_notifiable());
    CHECK(zero_notified.notifier().is_valid());
    CHECK(zero_notified.notifier().is_signal());
    CHECK(zero_notified.notifier().signature() == "changed() const");
    CHECK(!zero_notified.is_constant());
    CHECK(zero_notified.is_resettable());
    CHECK(zero_notified.reseter().is_valid());
    CHECK(zero_notified.reseter().signature() == "reset_value()");
    CHECK(zero_notified.reseter().invoke(*z).is_valid());
    CHECK(z->base_value() == 0);
    CHECK(records().size() == logged_before_zero);   // no "which no class ... declares" Critical

    // No diagnostic so far: every named accessor resolved.
    CHECK(records().empty());

    // (11) A named accessor that resolves nowhere: the query stays noexcept and answers with an
    // invalid handle, and the failure is reported ONCE per handle and role at Critical.
    const meta_property_t unresolved = p->meta_object()->property("unresolved");
    CHECK(unresolved.is_valid());
    CHECK(!unresolved.is_writable());
    CHECK(!unresolved.writer().is_valid());
    CHECK(records().size() == 1u);                 // is_writable() + writer(): one report
    CHECK(records().back().category == "metaobject.connection");
    CHECK(records().back().level == log_level_enum::CRITICAL);
    CHECK(records().back().message.find("unresolved") != std::string::npos);
    CHECK(records().back().message.find("no_such_writer") != std::string::npos);

    CHECK(!unresolved.is_notifiable());
    CHECK(!unresolved.notifier().is_valid());
    CHECK(records().size() == 2u);                 // a different role reports its own miss
    CHECK(records().back().message.find("no_such_notifier") != std::string::npos);

    // Flood control is per HANDLE (the documented choice): a fresh handle is a fresh resolution
    // attempt and reports again, so a repeated property(name) loop cannot be mistaken for silence.
    const meta_property_t unresolved_again = p->meta_object()->property("unresolved");
    CHECK(!unresolved_again.is_writable());
    CHECK(records().size() == 3u);

    // (12) CONSTRUCTOR COLLISION. Constructors are NOT in method_infos any more, so a bare accessor name
    //      equal to the class name ("collide_t(int)" for the writer / notifier form, "collide_t()"
    //      for the reseter form) matches no function row at all: the fallback reports an ordinary
    //      miss, exactly like a typo, and can never hand out a handle that would placement-new over
    //      the live object.
    auto* c = new collide_t(5);
    c->set_parent(root);
    CHECK(c->p() == 5);

    // Positive control 1 — the constructor lives in the CONSTRUCTOR space only: the method lookup by
    // its signature misses, and index_of_constructor / constructor find it.
    const index_t ctor_index = c->meta_object()->index_of_constructor("collide_t(int)");
    CHECK(ctor_index != index_npos);
    const meta_constructor_t ctor = c->meta_object()->constructor(ctor_index);
    CHECK(ctor.is_valid());
    CHECK(!ctor.signature().empty());
    CHECK(ctor.signature() == "collide_t(int)");
    CHECK(c->meta_object()->index_of_method("collide_t(int)") == index_npos);
    CHECK(!c->meta_object()->method("collide_t(int)").is_valid());
    CHECK(!c->meta_object()->signal("collide_t(int)").is_valid());

    // Positive control 2 — the split is structural, not a runtime refusal: a constructor handle is a
    // meta_constructor_t, and invoke() only accepts meta_method_t. Assert the absence in
    // the method space instead of calling invoke() on a handle that can no longer exist.
    CHECK(c->meta_object()->index_of_method("collide_t()") == index_npos);
    CHECK(!c->meta_object()->method("collide_t()").is_valid());
    CHECK(c->meta_object()->index_of_constructor("collide_t()") != index_npos);
    CHECK(c->meta_object()->constructor(
        c->meta_object()->index_of_constructor("collide_t()")).is_valid());
    CHECK(c->p() == 5);

    // Positive control 3 — construction still goes through new_instance(), which allocates first and
    // writes the new instance's pointer into the caller's own pointer variable (the out alias).
    const any_t ctor_args[] = { any_t(7) };
    collide_t* made = nullptr;
    any_t made_slot(std::ref(made));
    CHECK(c->meta_object()->new_instance(ctor_args, made_slot));
    CHECK(made != nullptr);
    if (made != nullptr) {
        CHECK(made != c);
        delete made;
    }
    CHECK(c->p() == 5);

    // Writer named like the class: rejected, and the miss is reported once for this handle.
    const meta_property_t collide_writer = c->meta_object()->property("p");
    CHECK(collide_writer.is_valid());
    CHECK(!collide_writer.is_writable());
    CHECK(!collide_writer.writer().is_valid());
    CHECK(records().size() == 4u);
    CHECK(records().back().category == "metaobject.connection");
    CHECK(records().back().level == log_level_enum::CRITICAL);
    CHECK(records().back().message.find("collide_t") != std::string::npos);
    CHECK(records().back().message.find("\"p\"") != std::string::npos);

    // Notifier named like the class (one-parameter form): rejected, its own Critical.
    const meta_property_t collide_notifier = c->meta_object()->property("q");
    CHECK(collide_notifier.is_valid());
    CHECK(!collide_notifier.is_notifiable());
    CHECK(!collide_notifier.notifier().is_valid());
    CHECK(records().size() == 5u);
    CHECK(records().back().message.find("\"q\"") != std::string::npos);

    // Reseter named like the class (no-argument form, i.e. the default constructor's key): rejected.
    const meta_property_t collide_reseter = c->meta_object()->property("r");
    CHECK(collide_reseter.is_valid());
    CHECK(!collide_reseter.is_resettable());
    CHECK(!collide_reseter.reseter().is_valid());
    CHECK(records().size() == 6u);
    CHECK(records().back().message.find("\"r\"") != std::string::npos);

    // The operations follow the same resolution (false, not a constructor call); the memos keep the
    // log at six records, and the object is still the original one.
    CHECK(!collide_writer.write(*c, any_t(9)));
    CHECK(!collide_reseter.reset(*c));
    CHECK(records().size() == 6u);
    CHECK(c->p() == 5);

    // Positive control 4 — a normal reflection-matched writer still binds and works.
    const meta_property_t normal_writer = p->meta_object()->property("sig_writer");
    CHECK(normal_writer.is_writable());
    CHECK(normal_writer.writer().signature() == "set_sig_writer(double)");
    CHECK(normal_writer.write(*p, any_t(2.5)));
    CHECK(p->sig_writer() == 2.5);

    // (13) L13: run-time accessor AMBIGUITY. The property names a BASE class accessor, so the
    //      compile-time pass binds nothing and writer() walks the chain. That base has both
    //      set_amb(int) and set_amb(int) const — two candidates at the same level, both exactly
    //      the property type. The run-time pass must refuse to pick one: no writer handle, no
    //      write, and one Critical naming the property, the accessor and the ambiguity.
    const std::size_t logged_before_ambiguous = records().size();
    auto* amb = new ambiguous_derived_t;
    amb->set_parent(root);
    const meta_property_t ambiguous = amb->meta_object()->property("amb");
    CHECK(ambiguous.is_valid());
    CHECK(!ambiguous.is_writable());              // no silent pick
    CHECK(!ambiguous.writer().is_valid());
    // is_writable() + writer() are one report: the handle memoises the ambiguity per role.
    CHECK(records().size() == logged_before_ambiguous + 1u);
    CHECK(records().back().category == "metaobject.connection");
    CHECK(records().back().level == log_level_enum::CRITICAL);
    CHECK(records().back().message.find("\"amb\"") != std::string::npos);
    CHECK(records().back().message.find("set_amb") != std::string::npos);
    CHECK(records().back().message.find("several overloads") != std::string::npos);
    // The operations follow the same resolution: false, not a call into an arbitrary overload.
    CHECK(!ambiguous.write(*amb, any_t(5)));
    CHECK(records().size() == logged_before_ambiguous + 1u);   // memo: no second Critical

    // Control: a UNIQUE inherited accessor still resolves and writes, so the ambiguity rule did
    // not turn every run-time name into a miss.
    auto* uni = new unique_derived_t;
    uni->set_parent(root);
    const meta_property_t unique = uni->meta_object()->property("uni");
    CHECK(unique.is_valid());
    CHECK(unique.is_writable());
    CHECK(unique.writer().is_valid());
    CHECK(unique.writer().signature() == "set_uni(int)");
    CHECK(unique.write(*uni, any_t(42)));
    CHECK(uni->uni_value() == 42);
    CHECK(records().size() == logged_before_ambiguous + 1u);   // a resolved accessor logs nothing

    install_log_handler({ });

    std::printf(g_failures == 0 ? "  M10: all checks passed\n" : "  M10: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
