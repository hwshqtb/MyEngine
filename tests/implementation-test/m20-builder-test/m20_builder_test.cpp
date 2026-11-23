// P1: the run-time builder family — meta_object_builder_t plus the three member handles
// (meta_method_builder_t / meta_property_builder_t / meta_enum_builder_t).
//
// Covered here, in this order:
//  1. add -> mutate through the handle -> build() -> the table reports the mutated values;
//  2. the defect this phase fixes: a runtime-built method with a `const std::string&` parameter
//     answers check_args the way a reflected class with the same declaration does (parity assert);
//  3. index_of_* finds what add_* created, and remove_* shifts the rows correctly;
//  4. snapshot semantics: mutating a handle after build() leaves the built table untouched;
//  5. a property added with a notifier resolves that notifier — including after a method removal
//     shifted the signal's row down.
//
// Two extras: (6) build() does NOT register the class with the class registry (registration stays
// the caller's job), and (7) the parser's output for representative declarations (the parameter
// list, the parameter cvref and the member's own cvref), including a parameter type that contains a
// comma. (8) covers the property traits the handle exposes (User / Required / Hidden / Final), that
// the built table answers them, and that build() enforces the two trait rules — Required+Hidden and
// at most one User — while Final is carried for introspection and enforces nothing. (10) closes the
// residual hole in the method/constructor split: set_method_kind() cannot turn a method row into a
// constructor; add_constructor() is the only way into the constructor table.

#include <meta>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/metaobject/meta_class_info_t.h"
#include "hwshqtb/metaobject/meta_enum_t.h"
#include "hwshqtb/metaobject/meta_method_t.h"
#include "hwshqtb/metaobject/meta_object_builder_t.h"
#include "hwshqtb/metaobject/meta_object_t.h"
#include "hwshqtb/metaobject/meta_property_t.h"
#include "hwshqtb/metaobject/object_t.h"

using namespace hwshqtb::metaobject;

namespace m20_builder_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// The cvref combination `CONST|LVALUE_REF` in one expression: a scoped enum has no operator|, so the
/// flag wrapper is what combines two enumerators.
constexpr qualifier_t qualifier_const_lref = qualifier_t{ qualifier_flag::CONST } | qualifier_flag::LVALUE_REF;

/// Thunks. Only their presence is asserted here: a run-time built row is a row like any other, and
/// this test never invokes one (that is the dynamic tests' business).
void stub_method(void*, void**) { }
void stub_reader(void*, void**) { }
void stub_writer(void*, void**) { }
void stub_reseter(void*, void**) { }

/// A reflected class whose declaration the run-time built row must agree with (criterion 2).
struct builder_parity_t: object_t {
    OBJECT
public:
    void ping(const std::string& text) {
        last = text;
    }
    std::string last;
};
META_OBJECT(builder_parity_t);

/// Resolve a method row by its NAME among the table's OWN rows — used on both tables so the assertion
/// never depends on the canonical spelling of the parameter type.
/// @note Own rows only: method(i) is a chain-global index whose first `super_class()->method_count()`
///       entries belong to the ancestors. Scanning from 0 would return the BASE's row when base and
///       derived declare the same name, which is exactly the case this parity check builds — the
///       comparison would then be the base's row against itself.
meta_method_t method_named(const meta_object_t& table, std::string_view name) {
    const index_t first_own = (table.super_class() != nullptr) ? table.super_class()->method_count() : 0;
    for (index_t i = first_own; i < table.method_count(); ++i) {
        const meta_method_t row = table.method(i);
        if (row.name() == name) {
            return row;
        }
    }
    return {};
}

}  // namespace m20_builder_test

using namespace m20_builder_test;

int main() {
    std::printf("== P1: run-time builder family ==\n");

    // No manual registration: the normaliser asks the TYPE REGISTRY for a type's identity spelling,
    // and the standard-library typedefs the fixtures spell (`std::string`, `std::wstring`) are
    // registered when the library is LOADED (detail::register_default_types, a static initialiser in
    // type_registry.cpp). This test constructs no core_application on purpose — the builder family
    // has to work without one — so the checks below also prove that load-time registration ran in
    // this binary.

    // (1) add -> mutate through the handle -> build() -> the table reports the mutated values.
    {
        std::printf("-- (1) mutate through the handle --\n");
        meta_object_builder_t builder("BuilderMutate");
        CHECK(builder.class_name() == "BuilderMutate");
        CHECK(builder.super_class() == nullptr);
        CHECK(builder.method_count() == 0);

        meta_method_builder_t method = builder.add_method("combine(int, double) const");
        CHECK(method.index() == 0);
        CHECK(method.name() == "combine");
        CHECK(method.signature() == "combine(int,double) const");
        CHECK(method.parameter_count() == 2);
        CHECK(method.method_kind() == method_kind_enum::PLAIN_METHOD);
        CHECK(method.access() == method_access_enum::PUBLIC_SCOPE);
        CHECK(method.qualifier() == qualifier_flag::CONST);   // parsed from the trailing suffix
        // Instantiate flag_t::operator!=(Enum) too: its operands are an enum and the underlying
        // integer, so the comparison is ill-formed unless the enum is cast (regression guard).
        CHECK(qualifier_const_lref != qualifier_flag::NONE);
        CHECK(qualifier_t{ qualifier_flag::CONST } != qualifier_const_lref);
        CHECK(method.return_type().empty());

        method.set_return_type("std::string");
        const std::string_view parameter_names[] = { "count", "scale" };
        method.set_parameter_names(parameter_names);
        const qualifier_t parameter_qualifiers[] = { qualifier_flag::NONE, qualifier_const_lref };
        method.set_parameter_qualifiers(parameter_qualifiers);
        method.set_access(method_access_enum::PROTECTED_SCOPE);
        method.set_tag("ported");
        method.set_caller(&stub_method);

        CHECK(method.return_type() == "std::__cxx11::basic_string<char>");   // identity, like a parameter
        CHECK(method.access() == method_access_enum::PROTECTED_SCOPE);

        // The two classification setters: a row can be reclassified and its structured cvref column
        // overridden before the snapshot.
        meta_method_builder_t reclassified = builder.add_method("probe()");
        reclassified.set_method_kind(method_kind_enum::SIGNAL);
        reclassified.set_qualifier(qualifier_const_lref);
        reclassified.set_caller(&stub_method);
        CHECK(builder.method_count() == 2);
        CHECK(reclassified.method_kind() == method_kind_enum::SIGNAL);
        CHECK(reclassified.qualifier() == qualifier_const_lref);

        // The column guards: a span of the wrong length would misalign the row, so it is refused.
        bool threw_wrong_parameter_count = false;
        try {
            const std::string_view too_many[] = { "a", "b", "c" };
            method.set_parameter_names(too_many);
        }
        catch (const std::invalid_argument&) {
            threw_wrong_parameter_count = true;
        }
        CHECK(threw_wrong_parameter_count);

        const meta_object_t* built = builder.build();
        CHECK(built != nullptr);
        CHECK(built->class_name() == "BuilderMutate");

        const meta_method_t row = built->method("combine(int,double) const");
        CHECK(row.is_valid());
        CHECK(row.name() == "combine");
        CHECK(row.signature() == "combine(int,double) const");
        CHECK(row.param_count() == 2);
        CHECK(row.param_name(0) == "count");
        CHECK(row.param_name(1) == "scale");
        CHECK(row.param_qualifier(0) == qualifier_flag::NONE);
        CHECK(row.param_qualifier(1) == qualifier_const_lref);
        // The spelling is re-assembled from base + qualifier, so the override shapes the column too.
        CHECK(row.param_type_name(0) == "int");
        CHECK(row.param_type_name(1) == "const double&");
        CHECK(row.return_type_name() == "std::__cxx11::basic_string<char>");
        CHECK(row.access() == method_access_enum::PROTECTED_SCOPE);
        CHECK(row.qualifier() == qualifier_flag::CONST);
        CHECK(row.method_kind() == method_kind_enum::PLAIN_METHOD);

        // The tag round-trips exactly like make_str_tag's: a staticised `const char*`, read through
        // the documented double indirection.
        const void* tag = row.tag("const char*");
        CHECK(tag != nullptr);
        CHECK(tag != nullptr && std::string_view(*(const char* const*)tag) == "ported");
        CHECK(row.tag("int") == nullptr);

        // The reclassified row: the kind is what the signal lookups read, and the qualifier column
        // reports the override (the KEY keeps the suffix its text spelled, as documented).
        const meta_method_t signal_row = built->method("probe()");
        CHECK(signal_row.is_valid());
        CHECK(signal_row.is_signal());
        CHECK(signal_row.method_kind() == method_kind_enum::SIGNAL);
        CHECK(signal_row.qualifier() == qualifier_const_lref);
        CHECK(built->index_of_signal("probe()") != index_npos);
        CHECK(built->signal("probe()").is_valid());
    }

    // (2) The defect: per-parameter cvref was never recorded, so check_args refused every
    //     parameterised runtime-built row. A reflected class with the same declaration is the oracle.
    {
        std::printf("-- (2) check_args parity with a reflected class --\n");
        meta_object_builder_t builder("BuilderParity", &builder_parity_t::static_meta_object);
        CHECK(builder.super_class() == &builder_parity_t::static_meta_object);
        meta_method_builder_t method = builder.add_method("ping(const std::string&)");
        method.set_return_type("void");
        method.set_caller(&stub_method);

        const meta_object_t* built = builder.build();
        CHECK(built->class_name() == "BuilderParity");
        CHECK(built->super_class() == &builder_parity_t::static_meta_object);
        CHECK(built->inherits("builder_parity_t"));
        // L7: the builder key is built from the IDENTITY parameter spelling the row stores, so the
        // built table's key spells `std::__cxx11::basic_string<char>`. meta_object_t::index_of_method()
        // is a strict exact lookup, while the builder's own lookup applies the registry lookup.
        CHECK(builder.index_of_method("ping(const std::string&)") != index_npos);
        CHECK(built->index_of_method("ping(const std::__cxx11::basic_string<char>&)") != index_npos);
        CHECK(built->index_of_method("ping(const std::string&)") == index_npos);

        const meta_method_t runtime_row = method_named(*built, "ping");
        const meta_method_t reflected_row = method_named(builder_parity_t::static_meta_object, "ping");
        CHECK(runtime_row.is_valid());
        CHECK(reflected_row.is_valid());
        // The runtime row must be the BUILT table's own, not the reflected base's: its chain-global
        // coordinate lies at or after the end of the base's rows.
        CHECK(runtime_row.index() >= builder_parity_t::static_meta_object.method_count());
        CHECK(runtime_row.signature() == reflected_row.signature());  // the key agrees with the column

        const bool runtime_const_ref = runtime_row.check_args<const std::string&>();
        const bool reflected_const_ref = reflected_row.check_args<const std::string&>();
        const bool runtime_value = runtime_row.check_args<std::string>();
        const bool reflected_value = reflected_row.check_args<std::string>();
        const bool runtime_wrong = runtime_row.check_args<int>();
        const bool reflected_wrong = reflected_row.check_args<int>();

        CHECK(runtime_const_ref);                                     // the declared argument type
        CHECK(reflected_const_ref);
        CHECK(runtime_const_ref == reflected_const_ref);              // ... and the parity
        CHECK(runtime_value == reflected_value);
        CHECK(!runtime_wrong);
        CHECK(runtime_wrong == reflected_wrong);

        // The columns behind the answer, compared one by one.
        CHECK(runtime_row.param_count() == reflected_row.param_count());
        CHECK(runtime_row.param_type_name(0) == reflected_row.param_type_name(0));
        CHECK(runtime_row.param_qualifier(0) == reflected_row.param_qualifier(0));
        CHECK(runtime_row.return_type_name() == reflected_row.return_type_name());
        // The run-time spelling is the identity one, not the alias the caller wrote.
        CHECK(runtime_row.param_type_name(0).find("basic_string<char>") != std::string_view::npos);

        std::printf("  runtime  check_args<const std::string&>=%d <std::string>=%d <int>=%d  param='%.*s'\n",
            runtime_const_ref ? 1 : 0, runtime_value ? 1 : 0, runtime_wrong ? 1 : 0,
            static_cast<int>(runtime_row.param_type_name(0).size()),
            runtime_row.param_type_name(0).data());
        std::printf("  reflected check_args<const std::string&>=%d <std::string>=%d <int>=%d  param='%.*s'\n",
            reflected_const_ref ? 1 : 0, reflected_value ? 1 : 0, reflected_wrong ? 1 : 0,
            static_cast<int>(reflected_row.param_type_name(0).size()),
            reflected_row.param_type_name(0).data());
    }

    // (3) index_of_* finds what add_* created; remove_* shifts the rows correctly.
    {
        std::printf("-- (3) index_of_* and remove_* --\n");
        meta_object_builder_t builder("BuilderIndex");
        CHECK(builder.index_of_method("alpha(int)") == index_npos);
        CHECK(builder.index_of_property("first") == index_npos);
        CHECK(builder.index_of_enumerator("Mode") == index_npos);

        const meta_method_builder_t alpha = builder.add_method("alpha(int)");
        const meta_method_builder_t beta = builder.add_signal("beta(double)");
        const meta_method_builder_t gamma = builder.add_slot("gamma()");
        const meta_method_builder_t ctor = builder.add_constructor("BuilderIndex()");
        CHECK(builder.method_count() == 3);   // constructors are their own space, not method rows
        CHECK(alpha.index() == 0);
        CHECK(beta.index() == 1);
        CHECK(gamma.index() == 2);
        CHECK(ctor.index() == 0);             // the constructor coordinate is its own space
        CHECK(beta.method_kind() == method_kind_enum::SIGNAL);
        CHECK(gamma.method_kind() == method_kind_enum::PLAIN_METHOD);
        // A constructor row is NOT a method row and has NO method kind: method_kind_enum has no Constructor
        // value any more, and the built row type (detail::constructor_info_t) carries no kind column.
        // The stronger fact is the SPACE the row lives in, stated as the two lookups.
        CHECK(builder.index_of_method("BuilderIndex()") == index_npos);
        CHECK(builder.index_of_constructor("BuilderIndex()") == 0);

        CHECK(builder.index_of_method("alpha(int)") == 0);
        CHECK(builder.index_of_method("alpha( int )") == 0);      // any spelling of the same text
        CHECK(builder.index_of_signal("beta(double)") == 1);
        CHECK(builder.index_of_signal("alpha(int)") == index_npos);   // a method is not a signal
        CHECK(builder.index_of_constructor("BuilderIndex()") == 0);
        CHECK(builder.index_of_constructor("alpha(int)") == index_npos);
        CHECK(builder.method(2).name() == "gamma");
        CHECK(builder.method(99).name().empty());                 // out of range stays defensible

        const meta_property_builder_t first = builder.add_property("first", "int");
        meta_property_builder_t second = builder.add_property("second", "double");
        CHECK(first.index() == 0);
        CHECK(second.index() == 1);
        CHECK(first.name() == "first");
        CHECK(first.type() == "int");
        CHECK(second.type() == "double");
        second.set_reader(&stub_reader);      // build() refuses a property that cannot be read
        CHECK(builder.index_of_property("first") == 0);
        CHECK(builder.index_of_property("second") == 1);
        CHECK(builder.index_of_property("third") == index_npos);

        meta_enum_builder_t mode = builder.add_enumerator("Mode");
        CHECK(mode.index() == 0);
        CHECK(mode.name() == "Mode");
        CHECK(!mode.is_flag());                                   // recorded, not inferred: default false
        mode.set_is_flag(true);
        CHECK(mode.is_flag());
        mode.add_key("None", 0);
        mode.add_key("Fast", 1);
        mode.add_key("Safe", 2);
        CHECK(mode.key_count() == 3);
        CHECK(mode.key(1) == "Fast");
        CHECK(mode.value(2) == 2);
        mode.remove_key(1);
        CHECK(mode.key_count() == 2);
        CHECK(mode.key(1) == "Safe");
        CHECK(mode.value(1) == 2);
        CHECK(builder.index_of_enumerator("Mode") == 0);
        CHECK(builder.index_of_enumerator("Other") == index_npos);

        CHECK(builder.add_class_info("Author", "hwshqtb") == 0);
        CHECK(builder.add_class_info("URL", "example.com") == 1);
        CHECK(builder.class_info_count() == 2);

        // remove_* shifts everything after the erased row down.
        builder.remove_method(beta.index());                      // erase the signal at 1
        CHECK(builder.method_count() == 2);   // the constructor row never counted as a method
        CHECK(builder.index_of_method("beta(double)") == index_npos);
        CHECK(builder.index_of_method("gamma()") == 1);
        CHECK(builder.index_of_constructor("BuilderIndex()") == 0);
        CHECK(builder.method(1).name() == "gamma");
        CHECK(builder.constructor(0).name() == "BuilderIndex");

        builder.remove_property(first.index());
        CHECK(builder.property_count() == 1);
        CHECK(builder.index_of_property("second") == 0);
        CHECK(builder.property(0).name() == "second");

        builder.remove_enumerator(0);
        CHECK(builder.enumerator_count() == 0);
        builder.remove_class_info(1);                             // the LAST pair
        CHECK(builder.class_info_count() == 1);
        CHECK(builder.index_of_enumerator("Mode") == index_npos);

        // Out-of-range removals are no-ops, not corruption.
        builder.remove_method(99);
        builder.remove_property(99);
        builder.remove_enumerator(99);
        builder.remove_class_info(99);
        CHECK(builder.method_count() == 2);

        // The surviving rows still build into a usable table.
        const meta_object_t* built = builder.build();
        CHECK(built->method_count() == 2);
        CHECK(built->property_count() == 1);
        CHECK(built->enumeration_count() == 0);
        CHECK(built->class_info_count() == 1);
        CHECK(built->class_info("Author").value() == "hwshqtb");
        // Both spaces answer the same signature, each in its own coordinate system: the builder's
        // constructor index is a constructor-row index (what remove_constructor() takes), and the
        // built meta-object's is the same constructor coordinate — constructor(i) is its accessor.
        CHECK(builder.index_of_constructor("BuilderIndex()") == 0);
        CHECK(built->constructor_count() == 1);
        CHECK(built->index_of_constructor("BuilderIndex()") == 0);
        CHECK(built->constructor(0).signature() == "BuilderIndex()");
        // The split: the built table's method space does not contain the constructor at all.
        CHECK(built->index_of_method("BuilderIndex()") == index_npos);
        CHECK(!built->method("BuilderIndex()").is_valid());
        CHECK(built->method(1).name() == "gamma");
    }

    // (4) Snapshot semantics: the table is assembled from a copy, so a later edit cannot reach it
    //     and build() stays idempotent.
    {
        std::printf("-- (4) one-shot snapshot --\n");
        meta_object_builder_t builder("BuilderSnapshot");
        meta_method_builder_t ping = builder.add_method("ping(int)");
        ping.set_return_type("int");
        ping.set_caller(&stub_method);

        const meta_object_t* first = builder.build();
        CHECK(first != nullptr);
        CHECK(builder.build() == first);                          // idempotent, not a second table

        ping.set_return_type("double");
        ping.set_tag("late");
        ping.set_access(method_access_enum::PRIVATE_SCOPE);

        const meta_method_t row = first->method("ping(int)");
        CHECK(row.is_valid());
        CHECK(row.return_type_name() == "int");                   // unchanged by the later edit
        CHECK(row.access() == method_access_enum::PUBLIC_SCOPE);
        CHECK(row.tag("const char*") == nullptr);
        CHECK(builder.method(0).return_type() == "double");       // the builder itself did change
    }

    // (5) A property with a notifier resolves it — and the notifier stays the right signal after a
    //     method removal shifted its row down.
    {
        std::printf("-- (5) property notifier --\n");
        meta_object_builder_t builder("BuilderProperty");
        const meta_method_builder_t early = builder.add_method("early()");
        meta_method_builder_t changed = builder.add_signal("valueChanged(int)");
        changed.set_caller(&stub_method);

        meta_property_builder_t value = builder.add_property("value", "int");
        CHECK(value.name() == "value");
        CHECK(value.type() == "int");
        // The handle's is_readable() is its CONFIGURATION state (does the pending row have a reader
        // thunk yet), not a capability of a built row: build() refuses the row while it is false.
        CHECK(!value.is_readable());
        CHECK(!value.is_writable());
        CHECK(!value.is_resettable());
        CHECK(value.is_constant());                               // no writer, no notifier

        bool threw_missing_reader = false;
        try {
            (void) builder.build();
        }
        catch (const std::invalid_argument&) {
            threw_missing_reader = true;
        }
        CHECK(threw_missing_reader);                              // a property must be readable

        value.set_reader(&stub_reader);
        value.set_writer(&stub_writer);
        value.set_reseter(&stub_reseter);
        value.set_notifier(changed);
        CHECK(value.is_readable());
        CHECK(value.is_writable());
        CHECK(value.is_resettable());
        CHECK(!value.is_constant());
        CHECK(!value.is_user());
        value.set_user(true);
        CHECK(value.is_user());
        CHECK(builder.index_of_method("early()") == 0);

        // Erase the method BEFORE the signal: the property's notifier row index must follow it down.
        builder.remove_method(early.index());
        CHECK(builder.index_of_method("valueChanged(int)") == 0);

        const meta_object_t* built = builder.build();             // now the reader is there
        const meta_property_t property = built->property("value");
        CHECK(property.is_valid());
        CHECK(property.type_name() == "int");
        CHECK(property.is_valid());                               // readable: a built row has a reader
        CHECK(property.is_writable());
        CHECK(property.is_resettable());
        CHECK(property.is_notifiable());
        CHECK(property.is_notifiable());                          // the notifier is the bindable fact
        CHECK(!property.is_constant());

        const meta_method_t notifier = property.notifier();
        CHECK(notifier.is_valid());
        CHECK(notifier.signature() == "valueChanged(int)");        // still the signal, not "early()"
        CHECK(notifier.is_signal());
        CHECK(notifier.param_type_name(0) == "int");
        CHECK(built->method(0).name() == "valueChanged");
    }

    // build() does NOT register the class: registration is the caller's job, exactly as it was for
    // the old builder. A built table must not appear in registered_type_names().
    {
        std::printf("-- (6) no implicit class-registry registration --\n");
        const std::vector<std::string> names = object_t::registered_type_names();
        auto registered = [&names](std::string_view name) {
            for (const std::string& entry : names) {
                if (entry == name) {
                    return true;
                }
            }
            return false;
        };
        CHECK(!registered("BuilderMutate"));
        CHECK(!registered("BuilderParity"));
        CHECK(!registered("BuilderProperty"));
        CHECK(!registered("BuilderIndex"));
        CHECK(!registered("BuilderSnapshot"));
        CHECK(!registered("BuilderParse"));
        // And with no registration, create() cannot resolve the name at all.
        CHECK(object_t::create("BuilderMutate") == nullptr);
    }

    // (7) The parser's output for representative declarations: the parameter list, the parameter
    //     cvref and the member's own cvref all come from the library's single signature parser
    //     (meta_info_t::split_signature / normalized_signature), and a parameter type may contain a
    //     comma as long as it is bracketed.
    {
        std::printf("-- (7) signature parsing --\n");
        // The user-text normaliser is the other half of the alias rule: it resolves a cv/ref-qualified
        // alias spelling to the IDENTITY spelling the tables store, so a key a caller writes by hand
        // agrees with a reflected row byte for byte.
        CHECK(meta_info_t{}.normalized_signature("ping(int, const std::string&) const")
            == "ping(int,const std::__cxx11::basic_string<char>&) const");
        CHECK(meta_info_t{}.normalized_signature("ping(std::string)")
            == "ping(std::__cxx11::basic_string<char>)");
        meta_object_builder_t builder("BuilderParse");
        meta_method_builder_t ping = builder.add_method("ping(int, const std::string&) const");
        meta_method_builder_t configure =
            builder.add_method("configure(std::map<int,int>, const std::string&)");
        meta_method_builder_t changed = builder.add_signal("valueChanged(double)");

        CHECK(ping.name() == "ping");
        // L7: the key is composed from the same identity spellings the parameter column stores, so
        // the alias `const std::string&` appears as `const std::__cxx11::basic_string<char>&` too.
        CHECK(ping.signature() == "ping(int,const std::__cxx11::basic_string<char>&) const");
        CHECK(builder.index_of_method("ping(int, const std::string&) const") == ping.index());
        CHECK(ping.parameter_count() == 2);
        CHECK(ping.qualifier() == qualifier_flag::CONST);
        CHECK(configure.name() == "configure");
        CHECK(configure.parameter_count() == 2);      // "std::map<int,int>" is ONE parameter
        CHECK(configure.qualifier() == qualifier_flag::NONE);
        CHECK(changed.name() == "valueChanged");
        CHECK(changed.signature() == "valueChanged(double)");
        CHECK(changed.method_kind() == method_kind_enum::SIGNAL);

        ping.set_caller(&stub_method);
        configure.set_caller(&stub_method);
        changed.set_caller(&stub_method);
        const meta_object_t* built = builder.build();

        const meta_method_t ping_row = method_named(*built, "ping");
        CHECK(ping_row.is_valid());
        CHECK(ping_row.param_type_name(0) == "int");
        CHECK(ping_row.param_type_name(1) == "const std::__cxx11::basic_string<char>&");
        CHECK(ping_row.param_qualifier(0) == qualifier_flag::NONE);
        CHECK(ping_row.param_qualifier(1) == qualifier_const_lref);
        CHECK(ping_row.param_name(0).empty());        // a signature text carries no parameter names
        CHECK(ping_row.qualifier() == qualifier_flag::CONST);

        const meta_method_t configure_row = method_named(*built, "configure");
        CHECK(configure_row.is_valid());
        CHECK(configure_row.param_type_name(0) == "std::map<int,int>");
        CHECK(configure_row.param_type_name(1) == "const std::__cxx11::basic_string<char>&");
        CHECK(configure_row.param_qualifier(0) == qualifier_flag::NONE);
        CHECK(configure_row.param_qualifier(1) == qualifier_const_lref);
        CHECK(configure_row.qualifier() == qualifier_flag::NONE);

        std::printf("  ping      key='%.*s' p0='%.*s' p1='%.*s'\n",
            static_cast<int>(ping_row.signature().size()), ping_row.signature().data(),
            static_cast<int>(ping_row.param_type_name(0).size()), ping_row.param_type_name(0).data(),
            static_cast<int>(ping_row.param_type_name(1).size()), ping_row.param_type_name(1).data());
        std::printf("  configure key='%.*s' p0='%.*s' p1='%.*s'\n",
            static_cast<int>(configure_row.signature().size()), configure_row.signature().data(),
            static_cast<int>(configure_row.param_type_name(0).size()), configure_row.param_type_name(0).data(),
            static_cast<int>(configure_row.param_type_name(1).size()), configure_row.param_type_name(1).data());
        std::printf("  valueChanged key='%.*s'\n",
            static_cast<int>(changed.signature().size()), changed.signature().data());
    }

    // (7b) The registry is the single source of names. A type the library knows only through the
    //      registry must reach the IDENTITY spelling the tables store — the case the old private
    //      alias list could not answer, because it listed std::string / std::string_view only.
    //      `std::wstring` is registered by the library at load time (register_default_types) and the
    //      canonical spelling is bound automatically, so all three spellings of
    //      std::basic_string<wchar_t> must converge.
    {
        std::printf("-- (7b) registry-backed name convergence --\n");
        const std::string identity_base{ meta_info_t::original_name<std::basic_string<wchar_t>>() };
        const std::string canonical_base{ meta_info_t::canonical_name<std::basic_string<wchar_t>>() };
        const std::string identity_piece = "const " + identity_base + "&";
        const std::string canonical_piece = "const " + canonical_base + "&";

        meta_object_builder_t builder("BuilderWideText");
        meta_method_builder_t method = builder.add_method("set_value(const std::wstring&)");
        method.set_caller(&stub_method);
        // The stored key carries the IDENTITY spelling, not the caller's text.
        CHECK(std::string(method.signature()) == "set_value(" + identity_piece + ")");

        // The alias, the canonical spelling and the identity spelling of ONE type produce the SAME key.
        const std::string key_identity =
            meta_info_t{}.normalized_signature("set_value(" + identity_piece + ")");
        const std::string key_canonical =
            meta_info_t{}.normalized_signature("set_value(" + canonical_piece + ")");
        const std::string key_alias =
            meta_info_t{}.normalized_signature("set_value(const std::wstring&)");
        CHECK(key_identity == key_canonical);
        CHECK(key_alias == key_identity);
        // The builder's own lookup runs the same registry lookup, so the canonical text finds the row.
        CHECK(builder.index_of_method("set_value(" + canonical_piece + ")") == method.index());

        // The built table stores the identity spelling in the parameter column and in its key.
        const meta_object_t* built = builder.build();
        const meta_method_t wide_row = method_named(*built, "set_value");
        CHECK(wide_row.is_valid());
        CHECK(std::string(wide_row.param_type_name(0)) == identity_piece);
        CHECK(std::string(wide_row.signature()) == "set_value(" + identity_piece + ")");

        // A spelling the registry does not know is passed through verbatim: never guessed.
        CHECK(meta_info_t{}.normalized_signature("set_value(const no::such::type&)")
            == "set_value(const no::such::type&)");
    }

    // (8) Property traits: the handle exposes all four bits of the row's trait column, the built
    //     table answers them through meta_property_t, and build() enforces the three rules the
    //     compile-time pass enforces (Required+Hidden, at most one User); Final enforces nothing.
    {
        std::printf("-- (8) property traits --\n");
        meta_object_builder_t builder("BuilderTraits");
        meta_property_builder_t flag = builder.add_property("flag", "int");
        flag.set_reader(&stub_reader);      // build() refuses a property without a reader
        CHECK(!flag.is_user());
        CHECK(!flag.is_required());
        CHECK(!flag.is_hidden());
        CHECK(!flag.is_final());

        // The setters are bit setters, not validators: the contradiction is build()'s call, and a
        // bit can be cleared again without disturbing the others.
        flag.set_required(true);
        flag.set_hidden(true);
        CHECK(flag.is_required());
        CHECK(flag.is_hidden());
        flag.set_hidden(false);
        CHECK(flag.is_required());
        CHECK(!flag.is_hidden());
        flag.set_final(true);
        flag.set_user(true);
        CHECK(flag.is_final());
        CHECK(flag.is_user());

        const meta_object_t* built = builder.build();
        const meta_property_t trait_row = built->property("flag");
        CHECK(trait_row.is_valid());
        CHECK(trait_row.is_required());
        CHECK(!trait_row.is_hidden());
        CHECK(trait_row.is_final());
        CHECK(trait_row.is_user());
        CHECK(built->user_property().name() == "flag");

        // The columns are snapshotted: clearing the builder row afterwards cannot reach the table.
        flag.set_final(false);
        CHECK(!flag.is_final());
        CHECK(trait_row.is_final());
    }

    // (8a) Required + Hidden is refused by build().
    {
        meta_object_builder_t builder("BuilderBadTraits");
        meta_property_builder_t bad = builder.add_property("x", "int");
        bad.set_reader(&stub_reader);       // keep the reader rule out of this case's way
        bad.set_required(true);
        bad.set_hidden(true);
        bool threw = false;
        try {
            builder.build();
        }
        catch (const std::invalid_argument&) {
            threw = true;
        }
        CHECK(threw);
    }

    // (8b) At most one User property per table.
    {
        meta_object_builder_t one("BuilderOneUser");
        meta_property_builder_t only = one.add_property("only", "int");
        only.set_reader(&stub_reader);
        only.set_user(true);
        const meta_object_t* built = one.build();
        CHECK(built->property("only").is_user());
        CHECK(built->user_property().name() == "only");
    }
    {
        meta_object_builder_t two("BuilderTwoUser");
        meta_property_builder_t a = two.add_property("a", "int");
        a.set_reader(&stub_reader);
        a.set_user(true);
        meta_property_builder_t b = two.add_property("b", "int");
        b.set_reader(&stub_reader);
        b.set_user(true);
        bool threw = false;
        try {
            two.build();
        }
        catch (const std::invalid_argument&) {
            threw = true;
        }
        CHECK(threw);
    }

    // (8c) A derived property MAY redeclare a property its base marked Final: Final is reported by
    //      is_final() and nothing enforces it. The derived row shadows the base's for by-name lookup,
    //      while the base table still answers its own row.
    {
        meta_object_builder_t base_builder("BuilderFinalBase");
        meta_property_builder_t sealed = base_builder.add_property("sealed", "int");
        sealed.set_reader(&stub_reader);
        sealed.set_final(true);
        const meta_object_t* base = base_builder.build();
        CHECK(base->property("sealed").is_final());

        meta_object_builder_t derived("BuilderFinalDerived", base);
        meta_property_builder_t derived_sealed = derived.add_property("sealed", "int");
        derived_sealed.set_reader(&stub_reader);
        const meta_object_t* built = derived.build();
        CHECK(built->super_class() == base);
        CHECK(built->property("sealed").is_valid());
        CHECK(!built->property("sealed").is_final());   // the shadowing row carries no Final bit
        CHECK(base->property("sealed").is_final());     // the base still reports its own bit
    }

    // (8d) The rule is gone at every depth: a grandchild may redeclare a property the root marked
    //      Final, and its own row wins by name on its own table.
    {
        meta_object_builder_t root_builder("BuilderFinalRoot");
        meta_property_builder_t root_sealed = root_builder.add_property("sealed", "int");
        root_sealed.set_reader(&stub_reader);
        root_sealed.set_final(true);
        const meta_object_t* root = root_builder.build();

        meta_object_builder_t middle_builder("BuilderFinalMiddle", root);
        meta_property_builder_t middle_only = middle_builder.add_property("middle_only", "int");
        middle_only.set_reader(&stub_reader);
        const meta_object_t* middle = middle_builder.build();
        CHECK(middle->property("sealed").is_valid());        // inherited from the root
        CHECK(middle->property("sealed").is_final());
        CHECK(middle->property("middle_only").is_valid());

        meta_object_builder_t grandchild("BuilderFinalGrand", middle);
        meta_property_builder_t grandchild_sealed = grandchild.add_property("sealed", "int");
        grandchild_sealed.set_reader(&stub_reader);
        const meta_object_t* built = grandchild.build();
        CHECK(built->property("sealed").is_valid());
        CHECK(!built->property("sealed").is_final());
        CHECK(root->property("sealed").is_final());
    }

    // (9) A notifier added with add_method() is not a signal: build() refuses it and names the cure,
    //     instead of recording a notifier whose later connect() could never succeed.
    {
        std::printf("-- (9) notifier must be a signal --\n");
        meta_object_builder_t builder("BuilderNotifierKind");
        meta_method_builder_t plain = builder.add_method("valueChanged(int)");
        plain.set_caller(&stub_method);

        meta_property_builder_t value = builder.add_property("value", "int");
        value.set_reader(&stub_reader);
        value.set_notifier(plain);

        bool threw = false;
        try {
            (void) builder.build();
        }
        catch (const std::invalid_argument&) {
            threw = true;
        }
        CHECK(threw);
    }

    // (10) set_method_kind() can no longer name a constructor — there is no method_kind_enum::Constructor value,
    //      so "reclassify a method row as a constructor" is unrepresentable and the old refusal (and
    //      its message) is gone with it. What remains true, and is stated here, is the SPACE rule: a
    //      method row stays in the method space, and NOTHING here can create a constructor row. The
    //      one runtime reclassification that matters is STATIC_METHOD: it is allowed, and its contract
    //      is on set_method_kind() — the thunk must ignore its obj argument. The suite never invokes it, so
    //      it also exercises has_receiver() as a pure query.
    {
        std::printf("-- (10) set_method_kind() has no Constructor value; STATIC_METHOD is a method row --\n");
        meta_object_builder_t builder("BuilderSetKindCtor");
        meta_method_builder_t method = builder.add_method("probe(int)");
        method.set_caller(&stub_method);
        method.set_method_kind(method_kind_enum::STATIC_METHOD);
        CHECK(method.method_kind() == method_kind_enum::STATIC_METHOD);
        CHECK(builder.method_count() == 1);
        CHECK(builder.constructor_count() == 0);

        const meta_object_t* built = builder.build();
        CHECK(built != nullptr);
        CHECK(built->method("probe(int)").is_valid());
        CHECK(built->method("probe(int)").method_kind() == method_kind_enum::STATIC_METHOD);
        CHECK(!built->method("probe(int)").has_receiver());
        CHECK(built->constructor_count() == 0);
        CHECK(built->index_of_constructor("probe(int)") == index_npos);
    }

    std::printf(g_failures == 0 ? "  run-time builder family: all checks passed\n"
                                : "  run-time builder family: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
