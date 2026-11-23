// M25: overload resolution against ONE table.
//
// The table is keyed by SIGNATURE, so resolving a call is: take the rows whose bare NAME matches,
// then decide which of them the call's argument identities (cvref_meta_type_t) and the object's cvref
// select. This file pins the two pieces that do it:
//
//   * method_matches(slot, args, exact, object_qualifier) — the per-row matcher: arity,
//     bare spelling per argument, the reference-kind rule (the EXACT pass requires the two kinds to
//     agree, the ACCOMMODATING pass lets check_param_qualifier decide), and the member's own cvref
//     against the object's cvref (ONE qualifier_t: const / volatile bits plus the reference kind).
//     It is addressed by SLOT and is deliberately NOT constexpr: its inputs are run-time identities.
//   * meta_info_t::resolve_method(name, args, object_qualifier) — the exact pass, then
//     the accommodating pass; one hit -> slot, two or more at the same level -> ambiguous. Run-time
//     by design too; the compile-time twin is resolve_method_types<Ts...>(), and section (11) compares
//     the two answers at RUN TIME.
//   * meta_info_t::check_method_args<Ts...>(slot, exact, object_qualifier) — the compile-time matcher,
//     which is what the handle's typed entry points call with exact == false.
//
// The fixture forces every branch:
//   (1) f(int) vs f(double): the argument's TYPE picks the row;
//   (2) f(int,int): the ARITY picks the row;
//   (3) f(const std::string&) vs f(std::string&&): two rows whose BARE SPELLING is identical, so only
//       the reference kind separates them — an lvalue reaches the const& row, an rvalue the && one.
//       The expectations are built from meta_info_t::original_name<std::string>(), never a literal,
//       so no libstdc++ detail namespace is pinned here;
//   (4) the member's own cvref: a const object reaches f() const and never the non-const f(int); a
//       non-const object reaches BOTH f() and f() const;
//   (5) a missing name and a wrong arity answer index_npos with ambiguous == false;
//   (6) constructors are never reachable — the fixture declares them, they live in constructor_infos, and
//       resolve_method scans method_infos only;
//   (7) ref-qualified members: `&` takes an lvalue object, `&&` an rvalue object, neither takes the
//       other, and a member with NO ref qualifier takes both;
//   (8) the CV side, with ONE qualifier_t on the object: volatile selects only volatile members,
//       const volatile selects only const volatile, and a const rvalue object cannot call a
//       non-const `&&` member (it needs a `const &&` one).
//
// REACHABILITY of the `ambiguous` branch: it IS producible by a legal C++ class, and section (4) is
// exactly that class — `f()` + `f() const` called on a NON-const object. A real compiler prefers the
// non-const overload; this flat layer has no conversion ranking, so both rows match at the same level
// and the verdict is "ambiguous" rather than a silent preference. The branch is therefore TESTED
// here, not merely kept as a defensive branch that cannot fail.

#include <meta>

#include <array>
#include <cstdio>
#include <string>
#include <string_view>

#include <hwshqtb/metaobject/annotation.h>
#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/cvref_meta_type_t.h>
#include <hwshqtb/metaobject/meta_info_t.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>

using namespace hwshqtb::metaobject;

namespace m25_call_resolution_test {

int g_checks = 0;
int g_failures = 0;

/// @brief Record one check; a failure prints its line and the condition.
void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

/// @brief The slot of @p signature in THIS table, or index_npos when no such row exists.
/// @note The table is keyed by signature, so this is the one lookup that names an expected row
///       independently of the resolver: a miss is verified against the stored key, exactly like
///       meta_info_t::invoke does.
constexpr index_t slot_of(const meta_info_t& table, std::string_view signature) {
    const auto rows = table.method_infos.to_span();
    const index_t slot = table.method_hashtable.find(signature);
    if (slot >= rows.size()) {
        return index_npos;
    }
    return std::string_view(rows[slot].signature) == signature ? slot : index_npos;
}

/// @brief Render a slot as the quoted signature it holds ("<none>" for index_npos).
std::string shown(const meta_info_t& table, index_t slot) {
    const auto rows = table.method_infos.to_span();
    if (slot >= rows.size()) {
        return "<none>";
    }
    return "\"" + std::string(std::string_view(rows[slot].signature)) + "\"";
}

/// @brief A resolution that must be exactly the row @p expected, with no ambiguity.
void check_resolves_to(const meta_info_t& table, const method_resolution_t& got, std::string_view expected,
                       const char* what, int line) {
    ++g_checks;
    const index_t want = slot_of(table, expected);
    if (want != index_npos && got.slot == want && !got.ambiguous) {
        return;
    }
    ++g_failures;
    std::printf("  [FAIL] %s (line %d)\n    expected \"%s\"\n    actual   %s%s\n", what, line,
                std::string(expected).c_str(), shown(table, got.slot).c_str(),
                got.ambiguous ? " (ambiguous)" : "");
}

/// @brief A resolution that must be exactly "nothing matched".
void check_no_match(const meta_info_t& table, const method_resolution_t& got, const char* what, int line) {
    ++g_checks;
    if (got.slot == index_npos && got.other == index_npos && !got.ambiguous) {
        return;
    }
    ++g_failures;
    std::printf("  [FAIL] %s (line %d)\n    expected no match\n    actual   %s%s%s\n", what, line,
                shown(table, got.slot).c_str(), got.ambiguous ? " (ambiguous)" : "",
                got.other == index_npos ? "" : " + a second candidate");
}

/// @brief An ambiguous resolution: exactly @p first and @p second, in that order.
void check_ambiguous(const meta_info_t& table, const method_resolution_t& got,
                     std::string_view first, std::string_view second, const char* what, int line) {
    ++g_checks;
    if (got.ambiguous && got.slot == slot_of(table, first) && got.other == slot_of(table, second)) {
        return;
    }
    ++g_failures;
    std::printf("  [FAIL] %s (line %d)\n    expected ambiguity \"%s\" + \"%s\"\n"
                "    actual   %s + %s%s\n", what, line, std::string(first).c_str(),
                std::string(second).c_str(), shown(table, got.slot).c_str(),
                shown(table, got.other).c_str(), got.ambiguous ? "" : " (not flagged ambiguous)");
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_RESOLVES_TO(tbl, resolution, expected) \
    check_resolves_to((tbl), (resolution), (expected), #resolution, __LINE__)
#define CHECK_NO_MATCH(tbl, resolution) check_no_match((tbl), (resolution), #resolution, __LINE__)
#define CHECK_AMBIGUOUS(tbl, resolution, first, second) \
    check_ambiguous((tbl), (resolution), (first), (second), #resolution, __LINE__)

/// @brief The compile-time and runtime resolvers must answer the SAME method_resolution_t.
/// @tparam Ts The argument types, used for BOTH worlds: the compile-time resolver takes the TYPES,
///         the runtime one the identities built from them.
/// @param table The table BOTH resolvers answer from (one instance, so the slots are comparable).
/// @param name The BARE member name.
/// @param object_qualifier The object's cvref (a constant).
/// @note This is the anti-drift assertion the two matchers exist for: a rule changed in one matcher
///       only (the exact reference-kind test, the object-cvref block) makes the two answers differ.
/// @note The comparison is at RUN TIME on purpose. resolve_method() is not constexpr (its inputs are
///       run-time identities: a slot and type handles), so the old consteval wrapper could not exist
///       any more; resolve_method_types is still constexpr-callable — section (11a) static_asserts
///       that — and both answers are held side by side here, in one ordinary function body.
template <typename... Ts>
void check_two_worlds(const meta_info_t& table, std::string_view name, qualifier_t object_qualifier,
                      const char* what, int line) {
    ++g_checks;
    const std::array<cvref_meta_type_t, sizeof...(Ts)> args{ cvref_meta_type_t::from<Ts>()... };
    const method_resolution_t rt = table.resolve_method(name, args, object_qualifier);
    const method_resolution_t ct = table.resolve_method_types<Ts...>(name, object_qualifier);
    if (rt.slot == ct.slot && rt.other == ct.other && rt.ambiguous == ct.ambiguous) {
        return;
    }
    ++g_failures;
    std::printf("  [FAIL] %s (line %d)\n    runtime      %s%s\n    compile-time %s%s\n", what, line,
                shown(table, rt.slot).c_str(), rt.ambiguous ? " (ambiguous)" : "",
                shown(table, ct.slot).c_str(), ct.ambiguous ? " (ambiguous)" : "");
}

// The object side of every expectation below, as the values a forwarding parameter produces
// (get_param_qualifier<Obj&&>()): one qualifier_t per value category / cv combination. Naming them
// keeps the expectations readable and makes the rule they pin explicit: `const` and `volatile` are
// BITS, and the reference kind is a separate pair of bits — a bool pair cannot carry the third state.
constexpr qualifier_t lvalue_object{ qualifier_flag::LVALUE_REF };
constexpr qualifier_t const_lvalue_object = qualifier_t{ qualifier_flag::CONST } | qualifier_flag::LVALUE_REF;
constexpr qualifier_t rvalue_object{ qualifier_flag::RVALUE_REF };
constexpr qualifier_t const_rvalue_object = qualifier_t{ qualifier_flag::CONST } | qualifier_flag::RVALUE_REF;
constexpr qualifier_t volatile_lvalue_object =
    qualifier_t{ qualifier_flag::VOLATILE } | qualifier_flag::LVALUE_REF;
constexpr qualifier_t const_volatile_lvalue_object =
    qualifier_t{ qualifier_flag::CV } | qualifier_flag::LVALUE_REF;

// -------------------------------------------------------------------------------------------
// The fixture. Every overload is a real, callable member (the resolver never invokes them, but a
// row the compiler could not call would not be a fair test of the table).
// -------------------------------------------------------------------------------------------
struct call_target_t {
    // Constructors: they give the table a NON-EMPTY constructor_infos, which is what makes the
    // "a constructor row is never returned" check below meaningful.
    call_target_t() = default;
    explicit call_target_t(int) { }
    call_target_t(int, int) { }

    /// The two no-argument rows: the non-const one is declared FIRST, so a non-const object's
    /// ambiguity reports this one as the first candidate (the table keeps declaration order).
    int f() { return 0; }
    int f() const { return 1; }

    /// The argument-type pair.
    int f(int) { return 2; }
    int f(double) { return 3; }

    /// The arity row.
    int f(int, int) { return 4; }

    /// The reference-kind pair: the bare spellings are IDENTICAL, so only the reference kind can tell
    /// them apart.
    int f(const std::string&) { return 5; }
    int f(std::string&&) { return 6; }
};

/// A second fixture for the OBJECT side of the rule: a ref-qualified pair, where the member's own
/// cvref is the only thing that can select the row.
struct ref_qualified_t {
    int g() & { return 1; }
    int g() && { return 2; }
};

/// The full object-side fixture: every cv shape plus both ref qualifiers in ONE table. The rows are
/// declared in the order the ambiguous expectations name them (f() first, f() const second, ...).
/// @note This flat layer has no conversion ranking, so a non-cv object matches all four f() rows
///       (each implicit object parameter can bind it by adding cv) and reports the first two. A real
///       compiler would prefer the least-qualified one; the table primitive does not pretend to.
/// @note h() is the `const &&` member the rule needs for a CONST rvalue object: g() && is non-const,
///       so it stays unreachable from one, which is exactly the C++ rule (not a bug in the matcher).
struct cv_ref_target_t {
    int f() { return 0; }
    int f() const { return 1; }
    int f() volatile { return 2; }
    int f() const volatile { return 3; }

    int g() & { return 4; }
    int g() && { return 5; }

    int h() const && { return 6; }
};

/// A narrower object-side fixture: `f() volatile` is the ONLY volatile member, so a volatile object
/// must select it UNIQUELY and must not reach f() / f() const. This is the defect the old two-bool
/// object side had: it treated a volatile object as a plain non-const one.
struct volatile_choice_target_t {
    int f() { return 0; }
    int f() const { return 1; }
    int f() volatile { return 2; }
};

/// Single-row ref-qualified fixtures: with only ONE row in the table, a miss cannot be masked by a
/// sibling accepting the object, so "g() & refuses an rvalue object" is pinned directly.
struct ref_only_lref_t {
    int g() & { return 1; }
};

struct ref_only_rref_t {
    int g() && { return 1; }
};

/// A third fixture for the QUERY family: one row in every table the accessors read. The getter is
/// ALSO a method row (a property is an annotation, not a table of its own), which is what makes
/// method_count() a meaningful check that it counts method_infos and not properties.
/// @note The interface base carries the IID, the class carries two class-info pairs, and the enum is
///       declared inside the class: no annotation is needed for the enum table to pick it up.
struct [[= make_interface("com.example.IQuery/1.0")]] query_iface_t {
    virtual ~query_iface_t() = default;
};

struct [[= make_class_info("author", "m25")]] [[= make_class_info("version", "1")]]
query_target_t : query_iface_t {
    query_target_t() = default;
    explicit query_target_t(int) { }

    enum struct Mode { None = 0, Fast = 1 };

    [[= make_property("value")]] int value() const { return 0; }

    int ping(int) { return 0; }
    int ping() const { return 1; }
};

// The query family is constexpr end to end: a consteval caller must be able to read the names, the
// counts and the slots off a table built at compile time — the contract the later compile-time
// readers depend on. This pins one representative of every family; the run-time section (8) below
// covers the spans, every key and the collision guard.
consteval bool query_surface_is_a_constant_expression() {
    constexpr auto query_table = meta_info_t::build_meta<query_target_t>();
    return query_table.name() == "query_target_t"
        && query_table.qualified_name_of() != query_table.name()
        && query_table.method_count() == query_table.method_infos.to_span().size()
        && query_table.constructor_count() == query_table.constructor_infos.to_span().size()
        && query_table.property_count() == query_table.property_infos.to_span().size()
        && query_table.enum_count() == query_table.enum_infos.to_span().size()
        && query_table.class_info_count() == query_table.class_info_infos.to_span().size()
        && query_table.interface_count() == query_table.interface_infos.to_span().size()
        && query_table.find_method("ping(int)") != index_npos
        && query_table.find_method("ping( int)") == index_npos
        && query_table.find_constructor("query_target_t(int)") != index_npos
        && query_table.find_property("value") != index_npos
        && query_table.find_enum("Mode") != index_npos
        && query_table.find_class_info("author") != index_npos
        && query_table.find_interface("com.example.IQuery/1.0") != index_npos
        && std::string_view(query_table.method_at(query_table.find_method("ping(int)")).signature)
               == "ping(int)";
}
static_assert(query_surface_is_a_constant_expression(),
    "the query surface must be evaluable in a constant expression");

// The COMPILE-TIME world must stay usable in a constant expression: it is what meta_object_t's
// compile-time chain walk depends on. This pins resolve_method_types (and the check_method_args it calls)
// for the no-argument path; the ARGUMENT path needs a VALID meta_type_t, and the only constexpr
// producer of one is not constexpr, so that path can only be exercised at run time (sections 1, 3
// and 4 below). The RUNTIME resolver is deliberately NOT part of this assertion: it lost constexpr
// with the redesign (its inputs are run-time identities), and section (11) compares the two worlds
// at run time instead.
consteval bool resolve_method_types_is_a_constant_expression() {
    constexpr auto table = meta_info_t::build_meta<call_target_t>();
    const method_resolution_t both = table.resolve_method_types<>("f", lvalue_object);
    const method_resolution_t only_const = table.resolve_method_types<>("f", const_lvalue_object);
    return both.ambiguous && both.slot != index_npos
        && !only_const.ambiguous && only_const.slot == slot_of(table, "f() const");
}
static_assert(resolve_method_types_is_a_constant_expression(),
    "resolve_method_types / check_method_args must be evaluable in a constant expression");

// The TWO columns share ONE rule per world, over two DIFFERENT row types. This pins the
// constructor-column entry points (check_constructor_args at compile time) on the same table the
// sections below use: the shared rule must accept the int-taking constructor for an int and refuse it
// for a double, exactly as it would on a method row. No new check is printed (static_assert only).
consteval bool constructor_column_shares_the_rule() {
    constexpr auto table = meta_info_t::build_meta<call_target_t>();
    const index_t slot = table.find_constructor("call_target_t(int)");
    return slot != index_npos
        && table.check_constructor_args<int>(static_cast<std::size_t>(slot), /*exact=*/false,
            qualifier_flag::NONE)
        && !table.check_constructor_args<double>(static_cast<std::size_t>(slot), /*exact=*/false,
            qualifier_flag::NONE);
}
static_assert(constructor_column_shares_the_rule(),
    "check_constructor_args must run the one compile-time rule over constructor_info_t");

} // namespace m25_call_resolution_test

int main() {
    using namespace m25_call_resolution_test;

    std::printf("== M25: call resolution (argument types + cvref, one table) ==\n");

    constexpr auto table = meta_info_t::build_meta<call_target_t>();
    const auto rows = table.method_infos.to_span();
    const std::span<const cvref_meta_type_t> no_arguments{};

    // The argument identities the sections below hand to the resolver. from<T>() is the same rule a
    // caller applies to a live argument, so nothing here is a hand-built qualifier.
    const std::array<cvref_meta_type_t, 1> one_int{ cvref_meta_type_t::from<int>() };
    const std::array<cvref_meta_type_t, 1> one_double{ cvref_meta_type_t::from<double>() };
    const std::array<cvref_meta_type_t, 2> two_ints{ cvref_meta_type_t::from<int>(),
                                                      cvref_meta_type_t::from<int>() };
    const std::array<cvref_meta_type_t, 3> three_ints{ cvref_meta_type_t::from<int>(),
                                                       cvref_meta_type_t::from<int>(),
                                                       cvref_meta_type_t::from<int>() };

    // std::string's identity spelling comes from the namer, never from a literal: the tables store
    // the ORIGINAL spelling, which carries the implementation's inline namespace.
    constexpr std::string_view string_name = meta_info_t::original_name<std::string>();
    const std::string string_lref_signature = "f(const " + std::string(string_name) + "&)";
    const std::string string_rref_signature = "f(" + std::string(string_name) + "&&)";
    std::printf("  meta_info_t::original_name<std::string>() = %s\n", std::string(string_name).c_str());

    const std::array<cvref_meta_type_t, 1> string_lvalue{ cvref_meta_type_t::from<const std::string&>() };
    const std::array<cvref_meta_type_t, 1> string_mutable_lvalue{ cvref_meta_type_t::from<std::string&>() };
    const std::array<cvref_meta_type_t, 1> string_rvalue{ cvref_meta_type_t::from<std::string&&>() };

    // -----------------------------------------------------------------------------------------
    // (0) The table itself: seven method rows, no constructor among them, and every expected
    //     signature present. Without this the sections below could pass for the wrong reason.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (0) the table --\n");
    CHECK(rows.size() == 7);
    CHECK(table.constructor_infos.to_span().size() >= 3);   // the fixture declares three
    for (const auto& row : rows) {
        CHECK(std::string_view(row.name) == "f");
        // Every METHOD row is one of the two non-static kinds. A constructor cannot be here at all:
        // method_kind_enum has no Constructor value, and the constructor table's row type has no kind
        // column. The table-level pair below states the same fact as a lookup.
        CHECK(row.kind == method_kind_enum::PLAIN_METHOD || row.kind == method_kind_enum::SIGNAL);
    }
    // The stronger fact that replaced "no row's kind is Constructor": a constructor is NOT in the
    // method table and IS in the constructor table (the two index spaces are separate).
    CHECK(table.find_method("call_target_t()") == index_npos);
    CHECK(table.find_constructor("call_target_t()") != index_npos);
    CHECK(slot_of(table, "f()") != index_npos);
    CHECK(slot_of(table, "f() const") != index_npos);
    CHECK(slot_of(table, "f(int)") != index_npos);
    CHECK(slot_of(table, "f(double)") != index_npos);
    CHECK(slot_of(table, "f(int,int)") != index_npos);
    CHECK(slot_of(table, string_lref_signature) != index_npos);
    CHECK(slot_of(table, string_rref_signature) != index_npos);
    std::printf("  method_infos = %zu row(s), constructor_infos = %zu row(s)\n", rows.size(),
                table.constructor_infos.to_span().size());

    // -----------------------------------------------------------------------------------------
    // (1) The argument's TYPE picks the row: f(int) for an int, f(double) for a double. The bare
    //     spelling is what separates them — both parameters are by value, so the cvref is equal.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (1) int vs double --\n");
    CHECK_RESOLVES_TO(table, table.resolve_method("f", one_int, lvalue_object), "f(int)");
    CHECK_RESOLVES_TO(table, table.resolve_method("f", one_double, lvalue_object), "f(double)");

    // -----------------------------------------------------------------------------------------
    // (2) Arity picks f(int,int): one int has a different row, two ints have exactly one.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (2) arity --\n");
    CHECK_RESOLVES_TO(table, table.resolve_method("f", two_ints, lvalue_object), "f(int,int)");

    // -----------------------------------------------------------------------------------------
    // (3) The reference-kind pair. f(const std::string&) and f(std::string&&) spell the SAME bare
    //     type, so the exact pass's reference comparison is the ONLY thing that tells them apart.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (3) const std::string& vs std::string&& --\n");
    CHECK_RESOLVES_TO(table, table.resolve_method("f", string_lvalue, lvalue_object), string_lref_signature);
    CHECK_RESOLVES_TO(table, table.resolve_method("f", string_mutable_lvalue, lvalue_object), string_lref_signature);
    CHECK_RESOLVES_TO(table, table.resolve_method("f", string_rvalue, lvalue_object), string_rref_signature);

    // -----------------------------------------------------------------------------------------
    // (4) The member's own cvref against the object.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (4) member cvref vs the object --\n");
    // A const object can call the const row and only that one.
    CHECK_RESOLVES_TO(table, table.resolve_method("f", no_arguments, const_lvalue_object), "f() const");
    // The only int-taking f is non-const, so a const object must find NOTHING — not the non-const row.
    CHECK_NO_MATCH(table, table.resolve_method("f", one_int, const_lvalue_object));
    // A non-const object can call BOTH no-argument rows. A compiler would prefer the non-const one;
    // this layer has no ranking, so it reports the ambiguity, with the declaration order preserved
    // (f() first, f() const second). This is the legal-C++ class that makes the branch reachable.
    CHECK_AMBIGUOUS(table, table.resolve_method("f", no_arguments, lvalue_object), "f()", "f() const");
    // An rvalue object is not const: the same two rows still match, in the same order.
    CHECK_AMBIGUOUS(table, table.resolve_method("f", no_arguments, rvalue_object), "f()", "f() const");

    // -----------------------------------------------------------------------------------------
    // (5) Nothing matches: an unknown name, and a wrong arity (a THREE-argument f does not exist).
    //     Both answer index_npos, ambiguous == false, other == index_npos.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (5) no match --\n");
    CHECK_NO_MATCH(table, table.resolve_method("not_a_method", no_arguments, lvalue_object));
    CHECK_NO_MATCH(table, table.resolve_method("f", three_ints, lvalue_object));

    // -----------------------------------------------------------------------------------------
    // (6) A constructor is never returned: the rows above are methods only, and the constructor
    //     table's own keys (the class name) are not in method_infos at all.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (6) constructors are unreachable --\n");
    CHECK_NO_MATCH(table, table.resolve_method(table.class_name, no_arguments, lvalue_object));
    CHECK_NO_MATCH(table, table.resolve_method(table.class_name, one_int, lvalue_object));
    CHECK(rows[slot_of(table, "f(int)")].kind == method_kind_enum::PLAIN_METHOD);
    // The stronger fact that replaced "the constructor row's kind IS Constructor": there is no such
    // kind any more (the constructor row type has no kind column), so the split is stated where it is
    // decidable — the constructor answers in ITS table and misses in the METHOD table.
    CHECK(table.find_method("call_target_t()") == index_npos);
    CHECK(table.find_constructor("call_target_t()") != index_npos);

    // -----------------------------------------------------------------------------------------
    // (7) The object side of the rule, on ref-qualified members: an lvalue object reaches g() &, an
    //     rvalue object reaches g() &&, and neither reaches the other. A member with NO ref
    //     qualifier (f(int)) accepts either value category.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (7) ref-qualified member vs the object --\n");
    constexpr auto ref_table = meta_info_t::build_meta<ref_qualified_t>();
    CHECK(ref_table.method_infos.to_span().size() == 2);
    CHECK_RESOLVES_TO(ref_table, ref_table.resolve_method("g", no_arguments, lvalue_object), "g() &");
    CHECK_RESOLVES_TO(ref_table, ref_table.resolve_method("g", no_arguments, rvalue_object), "g() &&");
    // A member with NO ref qualifier (f(int)) accepts either value category; the lvalue case is
    // sections 1-3 above, this is the rvalue counterpart.
    CHECK_RESOLVES_TO(table, table.resolve_method("f", one_int, rvalue_object), "f(int)");

    // Single-row tables, so a MISS cannot be masked by the sibling row: `g() &` alone must refuse an
    // rvalue object, and `g() &&` alone must refuse an lvalue object.
    constexpr auto lref_only_table = meta_info_t::build_meta<ref_only_lref_t>();
    constexpr auto rref_only_table = meta_info_t::build_meta<ref_only_rref_t>();
    CHECK(lref_only_table.method_infos.to_span().size() == 1);
    CHECK(rref_only_table.method_infos.to_span().size() == 1);
    CHECK_RESOLVES_TO(lref_only_table,
        lref_only_table.resolve_method("g", no_arguments, lvalue_object), "g() &");
    CHECK_NO_MATCH(lref_only_table, lref_only_table.resolve_method("g", no_arguments, rvalue_object));
    CHECK_RESOLVES_TO(rref_only_table,
        rref_only_table.resolve_method("g", no_arguments, rvalue_object), "g() &&");
    CHECK_NO_MATCH(rref_only_table, rref_only_table.resolve_method("g", no_arguments, lvalue_object));

    // -----------------------------------------------------------------------------------------
    // (8) The CV side of the same rule, now carried by ONE qualifier_t. `volatile` is the state the
    //     old two-bool object side could not express at all: a volatile object was read as a plain
    //     non-const one, so f() and f() const were selectable from it. C++ forbids that: a `T&`
    //     implicit object parameter cannot bind a volatile T.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (8) cv-qualified members vs the object --\n");
    constexpr auto cv_table = meta_info_t::build_meta<cv_ref_target_t>();
    const index_t f_plain = slot_of(cv_table, "f()");
    const index_t f_const = slot_of(cv_table, "f() const");
    const index_t f_volatile = slot_of(cv_table, "f() volatile");
    const index_t f_cv = slot_of(cv_table, "f() const volatile");
    CHECK(cv_table.method_infos.to_span().size() == 7);
    CHECK(f_plain != index_npos);
    CHECK(f_const != index_npos);
    CHECK(f_volatile != index_npos);
    CHECK(f_cv != index_npos);

    // The defect, stated against the only two rows that used to answer: call_target_t holds f() and
    // f() const, and a volatile object must match NEITHER. The old rule matched both (it read the
    // object as non-const), which is exactly the bug this section pins.
    CHECK_NO_MATCH(table, table.resolve_method("f", no_arguments, volatile_lvalue_object));

    // Add f() volatile and it becomes the row a volatile object selects — uniquely, because this
    // narrow table has no const volatile row.
    constexpr auto volatile_table = meta_info_t::build_meta<volatile_choice_target_t>();
    CHECK_RESOLVES_TO(volatile_table,
        volatile_table.resolve_method("f", no_arguments, volatile_lvalue_object), "f() volatile");
    // A const volatile object demands BOTH bits, and nothing in the narrow table carries both.
    CHECK_NO_MATCH(volatile_table,
        volatile_table.resolve_method("f", no_arguments, const_volatile_lvalue_object));

    // const volatile selects f() const volatile uniquely in the full table as well.
    CHECK_RESOLVES_TO(cv_table, cv_table.resolve_method("f", no_arguments, const_volatile_lvalue_object),
        "f() const volatile");

    // A VOLATILE object on the full table: f() and f() const are gone; the two volatile rows remain,
    // in declaration order. A real compiler ranks f() volatile above f() const volatile; this flat
    // layer reports the ambiguity rather than inventing a ranking.
    CHECK_AMBIGUOUS(cv_table, cv_table.resolve_method("f", no_arguments, volatile_lvalue_object),
        "f() volatile", "f() const volatile");

    // A CONST object: f() const and f() const volatile both accept it (adding cv to the member's
    // implicit object parameter is a qualification conversion); declaration order names the first two.
    CHECK_AMBIGUOUS(cv_table, cv_table.resolve_method("f", no_arguments, const_lvalue_object),
        "f() const", "f() const volatile");

    // A plain f() is reachable from a non-const lvalue AND from a non-const rvalue: the four-row
    // table answers the same two-way ambiguity section (4) pins on the two-row one, f() first.
    CHECK_AMBIGUOUS(cv_table, cv_table.resolve_method("f", no_arguments, lvalue_object),
        "f()", "f() const");
    CHECK_AMBIGUOUS(cv_table, cv_table.resolve_method("f", no_arguments, rvalue_object),
        "f()", "f() const");

    // Ref qualifiers and cv combined. g() && is NON-const, so a const rvalue object cannot call it:
    // the const bit is unmet, and C++ says a `const &&` member is needed instead. h() is that member
    // and accepts the const rvalue object; it still must not accept a const LVALUE (ref mismatch).
    const index_t g_lref_cv = slot_of(cv_table, "g() &");
    const index_t g_rref_cv = slot_of(cv_table, "g() &&");
    const index_t h_const_rref = slot_of(cv_table, "h() const &&");
    CHECK(g_lref_cv != index_npos);
    CHECK(g_rref_cv != index_npos);
    CHECK(h_const_rref != index_npos);
    CHECK_RESOLVES_TO(cv_table, cv_table.resolve_method("g", no_arguments, lvalue_object), "g() &");
    CHECK_RESOLVES_TO(cv_table, cv_table.resolve_method("g", no_arguments, rvalue_object), "g() &&");
    CHECK_NO_MATCH(cv_table, cv_table.resolve_method("g", no_arguments, const_rvalue_object));
    CHECK_NO_MATCH(cv_table, cv_table.resolve_method("g", no_arguments, const_lvalue_object));
    CHECK_RESOLVES_TO(cv_table, cv_table.resolve_method("h", no_arguments, const_rvalue_object),
        "h() const &&");
    CHECK_NO_MATCH(cv_table, cv_table.resolve_method("h", no_arguments, const_lvalue_object));

    // -----------------------------------------------------------------------------------------
    // (9) The QUERY surface: one family per table (count / at / find) plus the two names. The
    //     counts are compared with the spans themselves, so a table the fixture stops filling
    //     cannot pass by answering a number that is merely constant.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (9) query surface --\n");
    constexpr auto query_table = meta_info_t::build_meta<query_target_t>();

    // The names are the two columns, not a spelling of their own.
    CHECK(query_table.name() == std::string_view(query_table.class_name));
    CHECK(query_table.qualified_name_of() == std::string_view(query_table.qualified_name));
    CHECK(query_table.name() == "query_target_t");
    CHECK(query_table.qualified_name_of() != query_table.name()); // the fixture lives in a namespace

    // Every family answers its OWN span, and every fixture table is non-empty: a 0 == 0 comparison
    // would pass for the wrong reason.
    CHECK(query_table.method_count() == query_table.method_infos.to_span().size());
    CHECK(query_table.constructor_count() == query_table.constructor_infos.to_span().size());
    CHECK(query_table.property_count() == query_table.property_infos.to_span().size());
    CHECK(query_table.enum_count() == query_table.enum_infos.to_span().size());
    CHECK(query_table.class_info_count() == query_table.class_info_infos.to_span().size());
    CHECK(query_table.interface_count() == query_table.interface_infos.to_span().size());
    CHECK(query_table.method_count() != 0);
    CHECK(query_table.constructor_count() != 0);
    CHECK(query_table.property_count() != 0);
    CHECK(query_table.enum_count() != 0);
    CHECK(query_table.class_info_count() != 0);
    CHECK(query_table.interface_count() != 0);

    // A known method: find -> at round-trips the stored signature, the very key the table holds.
    const std::size_t method_slot = query_table.find_method("ping(int)");
    CHECK(method_slot != index_npos);
    CHECK(std::string_view(query_table.method_at(method_slot).signature) == "ping(int)");

    // A near miss that is a HASH HIT. "ping( int)" lands on the "ping(int)" slot — the first check
    // pins that, so the ones after it are not vacuous. A hash hit alone is not a match (the chd
    // table can answer a wrong slot for a key it does not hold), so find_method compares the stored
    // signature column and answers index_npos. Remove that comparison and this section fails: the
    // extra-space key, and the different-parameter spelling next to it, would both report a hit.
    CHECK(query_table.method_hashtable.find("ping( int)") == method_slot);
    CHECK(query_table.find_method("ping( int)") == index_npos);
    CHECK(query_table.find_method("ping(double)") == index_npos);
    CHECK(query_table.find_method("not_a_method()") == index_npos);

    // The same find -> at round-trip for the other four tables, each with its own key column.
    const std::size_t ctor_slot = query_table.find_constructor("query_target_t(int)");
    CHECK(ctor_slot != index_npos);
    CHECK(std::string_view(query_table.constructor_at(ctor_slot).signature) == "query_target_t(int)");
    CHECK(query_table.find_constructor("query_target_t(long)") == index_npos);

    const std::size_t prop_slot = query_table.find_property("value");
    CHECK(prop_slot != index_npos);
    CHECK(std::string_view(query_table.property_at(prop_slot).name) == "value");
    CHECK(query_table.find_property("missing") == index_npos);

    const std::size_t enum_slot = query_table.find_enum("Mode");
    CHECK(enum_slot != index_npos);
    CHECK(std::string_view(query_table.enum_at(enum_slot).name) == "Mode");
    CHECK(query_table.find_enum("Missing") == index_npos);

    const std::size_t ci_slot = query_table.find_class_info("author");
    CHECK(ci_slot != index_npos);
    CHECK(std::string_view(query_table.class_info_at(ci_slot).name) == "author");
    CHECK(query_table.find_class_info("missing") == index_npos);

    // The fixture declares one interface base (with an IID), so this family is covered, not skipped.
    const std::size_t iid_slot = query_table.find_interface("com.example.IQuery/1.0");
    CHECK(iid_slot != index_npos);
    CHECK(std::string_view(query_table.interface_at(iid_slot).iid) == "com.example.IQuery/1.0");
    CHECK(query_table.find_interface("com.example.IQuery/2.0") == index_npos);

    // -----------------------------------------------------------------------------------------
    // (10) The FLOOR: meta_info_t::invoke(slot, obj, params) is the primitive the whole call layer
    //      stands on. No handle and no lookup take part: find_method() names the slot, the caller
    //      builds the generic_call array, and the row's thunk does the rest. f(int) returns 2, so
    //      params[0] (the return slot) must receive 2 — a primitive that does nothing leaves the -1.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (10) the table's own invoke primitive --\n");
    call_target_t primitive_target;
    const std::size_t primitive_slot = table.find_method("f(int)");
    CHECK(primitive_slot != index_npos);
    int primitive_argument = 7;
    int primitive_result = -1;
    void* primitive_params[] = { &primitive_result, &primitive_argument };
    table.invoke(primitive_slot, &primitive_target, primitive_params);
    CHECK(primitive_result == 2);

    // -----------------------------------------------------------------------------------------
    // (11) THE TWO WORLDS: resolve_method (runtime identities) and resolve_method_types<Ts...>
    //      (compile-time TYPES) must answer the SAME thing — slot, index_npos or ambiguous — for a
    //      hit, a reference-kind hit, a miss, an ambiguity, a const-object refusal and an
    //      `&&`-member refusal. Both answers are ALSO pinned against the expected row, so "agree on
    //      the wrong answer" cannot pass.
    //      The comparison is at RUN TIME, in one ordinary function body: the runtime resolver is not
    //      constexpr (its inputs are run-time identities), while the compile-time one still is — which
    //      is exactly what lets check_two_worlds() hold the two answers side by side.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (11) the two matchers agree (runtime vs compile time) --\n");
    {
        // A hit: f(int).
        const method_resolution_t ct_hit = table.resolve_method_types<int>("f", lvalue_object);
        CHECK_RESOLVES_TO(table, ct_hit, "f(int)");
        check_two_worlds<int>(table, "f", lvalue_object, "hit f(int)", __LINE__);

        // A hit on a reference-qualified parameter: f(const std::string&) from a const lvalue.
        const method_resolution_t ct_string =
            table.resolve_method_types<const std::string&>("f", lvalue_object);
        CHECK_RESOLVES_TO(table, ct_string, string_lref_signature);
        check_two_worlds<const std::string&>(table, "f", lvalue_object,
            "hit f(const std::string&)", __LINE__);

        // The SAME pair from the other side: an RVALUE std::string must reach f(std::string&&) in both
        // worlds. Both rows spell their parameter "std::string", so only the exact reference-kind rule
        // separates them — and check_param_qualifier() alone would accept BOTH (an rvalue binds to
        // const T& and to T&&), which is exactly why the agreement has to cover this case.
        const method_resolution_t ct_rvalue_string =
            table.resolve_method_types<std::string&&>("f", lvalue_object);
        CHECK_RESOLVES_TO(table, ct_rvalue_string, string_rref_signature);
        check_two_worlds<std::string&&>(table, "f", lvalue_object,
            "hit f(std::string&&)", __LINE__);

        // A miss: three ints have no row.
        const method_resolution_t ct_miss = table.resolve_method_types<int, int, int>("f", lvalue_object);
        CHECK_NO_MATCH(table, ct_miss);
        check_two_worlds<int, int, int>(table, "f", lvalue_object, "miss", __LINE__);

        // An ambiguity: f() + f() const from a non-const object.
        const method_resolution_t ct_ambiguous = table.resolve_method_types<>("f", lvalue_object);
        CHECK_AMBIGUOUS(table, ct_ambiguous, "f()", "f() const");
        check_two_worlds<>(table, "f", lvalue_object, "ambiguity", __LINE__);

        // A const-object refusal: f(int) is non-const, so a const object reaches nothing.
        const method_resolution_t ct_const = table.resolve_method_types<int>("f", const_lvalue_object);
        CHECK_NO_MATCH(table, ct_const);
        check_two_worlds<int>(table, "f", const_lvalue_object, "const object vs f(int)", __LINE__);

        // An `&&`-member refusal: an lvalue object cannot call `g() &&`.
        const method_resolution_t ct_rref_refused =
            rref_only_table.resolve_method_types<>("g", lvalue_object);
        CHECK_NO_MATCH(rref_only_table, ct_rref_refused);
        check_two_worlds<>(rref_only_table, "g", lvalue_object, "lvalue object vs g() &&", __LINE__);

        // The legal counterpart: the same `&&` member from an rvalue object.
        const method_resolution_t ct_rref_hit = rref_only_table.resolve_method_types<>("g", rvalue_object);
        CHECK_RESOLVES_TO(rref_only_table, ct_rref_hit, "g() &&");
        check_two_worlds<>(rref_only_table, "g", rvalue_object, "rvalue object vs g() &&", __LINE__);

        // A volatile-object refusal, in both worlds: no volatile member exists in this table.
        const method_resolution_t ct_volatile = table.resolve_method_types<>("f", volatile_lvalue_object);
        CHECK_NO_MATCH(table, ct_volatile);
        check_two_worlds<>(table, "f", volatile_lvalue_object,
            "volatile object vs non-volatile members", __LINE__);

        std::printf("  compile-time and runtime resolution agree on all six shapes\n");
    }

    // -----------------------------------------------------------------------------------------
    // (12) The table's THREE entry points all carry the object-cvref rule:
    //      (a) check_method_args<Ts...>     — the typed by-type path;
    //      (b) method_matches_boxed            — the boxed / erased-argument path, ON THE HANDLE;
    //      (c) resolve_method                 — the resolver (same answers as (11)).
    //      A const object must be refused a NON-CONST member by each of them, and the same three must
    //      still accept it from a plain lvalue. (The fourth entry point, a raw handle's invoke, needs
    //      a real meta_object_t and is asserted in M13.)
    //      The boxed bridge is PRIVATE on meta_method_t — the table must not know any_t — so it is
    //      reached the way production code reaches it: through the handle's erased invoke.
    //      Volatile cannot be spelled through that channel at all: every object-level erased form
    //      erases the address with static_cast<const void*>(&obj), which is ill-formed for a
    //      volatile pointer (M13's note); its refusal is pinned at the table level by (a) and (c).
    // -----------------------------------------------------------------------------------------
    std::printf("-- (12) check_method_args / method_matches_boxed / resolve_method refuse a const object --\n");
    {
        const std::size_t f_int_slot = slot_of(table, "f(int)");
        CHECK(f_int_slot != index_npos);

        // (a) the typed by-type path. (The accommodating pass — /*exact=*/false — which is what
        //     validates a selected row; the object-cvref rule is applied in full.)
        CHECK(table.check_method_args<int>(f_int_slot, /*exact=*/false, lvalue_object));
        CHECK(!table.check_method_args<int>(f_int_slot, /*exact=*/false, const_lvalue_object));
        CHECK(!table.check_method_args<int>(f_int_slot, /*exact=*/false, volatile_lvalue_object));

        // (b) the boxed path: one any_t per parameter, through a handle built on the same table.
        const meta_object_t object{ meta_info_t::build_meta<call_target_t>() };
        const meta_method_t f_int_handle = object.method("f(int)");
        CHECK(f_int_handle.is_valid());

        call_target_t call_target;
        const call_target_t& const_target = call_target;
        const any_t boxed_array[] = { any_t(7) };
        const std::span<const any_t> boxed = boxed_array;
        // The plain lvalue object reaches the row: the erased call runs f(int) and answers 2.
        const any_t boxed_result = f_int_handle.invoke(call_target, boxed);
        CHECK(boxed_result.is_initialized() && boxed_result.type().is<int>()
              && boxed_result.get<int>() == 2);

        // A const object is refused the same non-const row: method_matches_boxed answers no, and the
        // erased channel's mismatch is a bad_any_cast_t THROW (the contract M13 also pins).
        bool threw_const = false;
        try {
            (void)f_int_handle.invoke(const_target, boxed);
        }
        catch (const bad_any_cast_t&) {
            threw_const = true;
        }
        CHECK(threw_const);

        // A DECLARED-ONLY argument (never constructed) is refused on this channel too: there is no
        // value to hand the thunk, so the erased invoke throws before any call runs.
        bool threw_uninitialized = false;
        try {
            const any_t declared_only_array[] = { any_t{} };
            (void)f_int_handle.invoke(call_target, std::span<const any_t>(declared_only_array));
        }
        catch (const bad_any_uninitialized_t&) {
            threw_uninitialized = true;
        }
        CHECK(threw_uninitialized);

        // A const member is still reachable from a const object through all three entries.
        const std::size_t f_const_slot = slot_of(table, "f() const");
        CHECK(f_const_slot != index_npos);
        CHECK(table.check_method_args<>(f_const_slot, /*exact=*/false, const_lvalue_object));
        const meta_method_t f_const_handle = object.method("f() const");
        CHECK(f_const_handle.is_valid());
        const any_t const_result = f_const_handle.invoke(const_target, std::span<const any_t>{});
        CHECK(const_result.is_initialized() && const_result.get<int>() == 1);
        CHECK_RESOLVES_TO(table, table.resolve_method("f", no_arguments, const_lvalue_object),
            "f() const");

        // (c) the resolver, explicitly (the same answers as (11)).
        CHECK_NO_MATCH(table, table.resolve_method("f", one_int, const_lvalue_object));
        CHECK_NO_MATCH(table, table.resolve_method("f", one_int, volatile_lvalue_object));
        CHECK_RESOLVES_TO(table, table.resolve_method("f", one_int, lvalue_object), "f(int)");
    }

    // -----------------------------------------------------------------------------------------
    // (13) NO REGRESSION for the legal shapes: a non-const object may call a const member, a const
    //      object may call a const member, an rvalue may call `&&`, an lvalue may call `&`. These
    //      are exactly the cases the new cvref check must NOT refuse.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (13) legal shapes still resolve --\n");
    {
        constexpr auto ping_table = meta_info_t::build_meta<query_target_t>();
        const std::size_t ping_int_slot = slot_of(ping_table, "ping(int)");
        const std::size_t ping_const_slot = slot_of(ping_table, "ping() const");
        CHECK(ping_int_slot != index_npos);
        CHECK(ping_const_slot != index_npos);

        // non-const object -> const member.
        CHECK_RESOLVES_TO(ping_table, ping_table.resolve_method("ping", no_arguments, lvalue_object),
            "ping() const");
        CHECK(ping_table.check_method_args<>(ping_const_slot, /*exact=*/false, lvalue_object));
        // const object -> const member.
        CHECK_RESOLVES_TO(ping_table,
            ping_table.resolve_method("ping", no_arguments, const_lvalue_object), "ping() const");
        CHECK(ping_table.check_method_args<>(ping_const_slot, /*exact=*/false, const_lvalue_object));
        // non-const object -> non-const member.
        CHECK_RESOLVES_TO(ping_table, ping_table.resolve_method("ping", one_int, lvalue_object),
            "ping(int)");
        CHECK(ping_table.check_method_args<int>(ping_int_slot, /*exact=*/false, lvalue_object));

        // rvalue -> `&&` member, lvalue -> `&` member.
        CHECK_RESOLVES_TO(ref_table, ref_table.resolve_method("g", no_arguments, rvalue_object),
            "g() &&");
        CHECK_RESOLVES_TO(ref_table, ref_table.resolve_method("g", no_arguments, lvalue_object),
            "g() &");
        CHECK(ref_table.check_method_args<>(slot_of(ref_table, "g() &&"), /*exact=*/false, rvalue_object));
        CHECK(ref_table.check_method_args<>(slot_of(ref_table, "g() &"), /*exact=*/false, lvalue_object));

        std::printf("  const / non-const / lvalue / rvalue legal shapes all still match\n");
    }

    if (g_failures == 0) {
        std::printf("  M25 call resolution: all %d checks passed\n", g_checks);
    }
    else {
        std::printf("  M25 call resolution: %d check(s) failed\n", g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
