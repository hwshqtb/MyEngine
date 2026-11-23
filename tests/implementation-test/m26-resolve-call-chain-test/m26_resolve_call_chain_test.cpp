// M26: the CHAIN-level resolver — meta_object_t::resolve_method(), the runtime-name face of overload
// resolution.
//
// meta_info_t::resolve_method() answers from ONE table and is silent. This file pins the layer above it,
// the one that owns the chain and the diagnostics:
//
//   * own-first walking: `this` is asked before super_class(), exactly like method(), and the FIRST
//     table that answers wins — a derived f(int) hides the base's f(int) instead of being merged with
//     it (the two rows share the normalised signature, so only the DECLARING table tells them apart);
//   * fall-through: a member only the base declares (g()) is answered by the base's table, and a
//     derived class that declares no f at all reaches the base's f(int);
//   * the answer is the DECLARING table's handle, verified through operator== (declaring meta-object +
//     slot), not merely through the signature text;
//   * the const object rule: a `const Obj&` refuses a non-const-only member and selects h() const when
//     both h() and h() const exist;
//   * the erased form and the typed form answer the SAME row for the same call;
//   * a miss answers an invalid handle and logs exactly ONE Warning carrying the name, the argument
//     spellings and the object's class name;
//   * an ambiguity is TERMINAL: the invalid handle is returned and exactly ONE Critical is logged,
//     naming both candidate signatures. The base deliberately declares an unambiguous h() as well, so
//     a walk that kept looking after the derived's ambiguity would find it — the invalid handle proves
//     the walk stopped.
//   * the COMPILE-TIME name form (resolve_method<"f", Obj, Ts...>()) answers the SAME rows as the
//     runtime-name form, so the two front ends cannot disagree; a miss / an ambiguity there is a
//     COMPILE-time error, which is why the const-object refusal it pins is proved with a separate
//     -fsyntax-only probe, not a check in this file.
//   * a receiver that is not Obj's own static table is a Critical and an invalid handle.
//
// The fixture is a three-class chain: chain_base_t (f(int), g(), h()), chain_derived_t (its own f(int)
// hiding the base's, f(double), h() and h() const), and chain_other_t (no f at all).

#include <meta>

#include <array>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/cvref_meta_type_t.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::metaobject;

namespace m26_resolve_call_chain_test {

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

/// @brief One captured log record (category name, level, text).
struct log_record_t {
    std::string category;
    log_level_enum level;
    std::string message;
};

/// @brief Every record the installed handler received during the current section.
std::vector<log_record_t> g_records;

/// @brief Whether @p haystack contains @p needle.
bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

/// @brief A handle that must be exactly the row @p expected (declaring table + slot).
/// @note The comparison is meta_method_t::operator==, which compares the DECLARING meta-object, the
///       slot and the invoker — so this is the "compare the handle's owner" assertion, not a text
///       comparison that would pass for an identically spelled row of another table.
void check_row(const meta_method_t& actual, const meta_method_t& expected, const char* what, int line) {
    ++g_checks;
    if (actual.is_valid() && actual == expected) {
        return;
    }
    ++g_failures;
    std::printf("  [FAIL] %s (line %d)\n    expected \"%s\"\n    actual   \"%s\"%s\n", what, line,
                std::string(expected.signature()).c_str(), std::string(actual.signature()).c_str(),
                actual.is_valid() ? "" : " (INVALID)");
}

/// @brief An invalid handle, with the section's log checked separately.
void check_invalid(const meta_method_t& actual, const char* what, int line) {
    ++g_checks;
    if (!actual.is_valid()) {
        return;
    }
    ++g_failures;
    std::printf("  [FAIL] %s (line %d)\n    expected an INVALID handle\n    actual   \"%s\"\n", what,
                line, std::string(actual.signature()).c_str());
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_ROW(actual, expected) check_row((actual), (expected), #actual, __LINE__)
#define CHECK_INVALID(actual) check_invalid((actual), #actual, __LINE__)

// -------------------------------------------------------------------------------------------
// The fixture: one base, one derived that hides and extends, one derived that declares no f.
// -------------------------------------------------------------------------------------------
struct chain_base_t : object_t {
    OBJECT
public:
    /// The row the derived hides with its own f(int).
    int f(int) { return 1; }

    /// A base-only row: the fall-through case.
    int g() { return 2; }

    /// An UNAMBIGUOUS h(), deliberately present in the base: the derived's h()/h() const pair is
    /// ambiguous for a non-const object, and the resolver must stop there instead of finding this one.
    int h() { return 8; }
};
META_OBJECT(chain_base_t);

struct chain_derived_t : chain_base_t {
    OBJECT
public:
    /// Same normalised signature as the base's f(int): own-first selects THIS row.
    int f(int) { return 3; }

    /// A derived-only overload.
    int f(double) { return 4; }

    /// The ambiguity pair: both match a non-const object at the same level, only h() const matches a
    /// const object.
    int h() { return 5; }
    int h() const { return 6; }
};
META_OBJECT(chain_derived_t);

/// A second derived class that declares no f at all: f(int) must fall through to the base's row.
struct chain_other_t : chain_base_t {
    OBJECT
public:
    int k() { return 7; }
};
META_OBJECT(chain_other_t);

} // namespace m26_resolve_call_chain_test

int main() {
    using namespace m26_resolve_call_chain_test;

    install_log_handler([](log_level_enum level, const log_context_t& context, std::string_view message) {
        g_records.push_back(log_record_t{ std::string(context.category), level, std::string(message) });
        return true;   // consumed: the default sink stays silent
    });

    std::printf("== M26: chain-level resolve_method (own-first walk, runtime name) ==\n");

    const meta_object_t& derived = chain_derived_t::static_meta_object;
    const meta_object_t& base = chain_base_t::static_meta_object;
    const meta_object_t& other = chain_other_t::static_meta_object;

    // -----------------------------------------------------------------------------------------
    // (0) The chain the sections below walk: two direct links to the same base table.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (0) chain shape --\n");
    CHECK(derived.super_class() == &base);
    CHECK(other.super_class() == &base);
    CHECK(derived.class_name() == "chain_derived_t");
    CHECK(base.class_name() == "chain_base_t");
    // The derived adds five own rows to the whole base chain: its four declared members (f(int),
    // f(double), h(), h() const) plus the OBJECT accessor's own meta_object() const override.
    std::printf("  method_count: derived=%zu base=%zu\n", derived.method_count(), base.method_count());
    CHECK(derived.method_count() == base.method_count() + 5);

    // -----------------------------------------------------------------------------------------
    // (1) Own-first: the derived's f(int) wins over the base's, and the handle names the DERIVED
    //     table. The two rows share the signature "f(int)", so comparing against both tables'
    //     handles is what proves WHICH declaration was answered.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (1) own-first: the derived f(int) wins --\n");
    g_records.clear();
    const meta_method_t derived_f_int = derived.resolve_method<chain_derived_t&, int>("f");
    CHECK(derived_f_int.is_valid());
    CHECK(derived_f_int.signature() == "f(int)");
    CHECK_ROW(derived_f_int, derived.method("f(int)"));
    CHECK(!(derived_f_int == base.method("f(int)")));
    CHECK(g_records.empty());
    std::printf("  f(int) -> \"%s\" (derived table)\n", std::string(derived_f_int.signature()).c_str());

    // -----------------------------------------------------------------------------------------
    // (2) The derived's own overload f(double) resolves on the derived; the base-only g() falls
    //     through and names the BASE table's row.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (2) f(double) on the derived, g() from the base --\n");
    g_records.clear();
    const meta_method_t derived_f_double = derived.resolve_method<chain_derived_t&, double>("f");
    CHECK(derived_f_double.is_valid());
    CHECK(derived_f_double.signature() == "f(double)");
    CHECK_ROW(derived_f_double, derived.method("f(double)"));

    const meta_method_t derived_g = derived.resolve_method<chain_derived_t&>("g");
    CHECK(derived_g.is_valid());
    CHECK(derived_g.signature() == "g()");
    CHECK_ROW(derived_g, base.method("g()"));
    // The by-signature lookup agrees: the derived has no g() of its own.
    CHECK(derived.method("g()") == base.method("g()"));
    CHECK(g_records.empty());

    // -----------------------------------------------------------------------------------------
    // (3) Fall-through on a derived class that declares no f: the BASE's f(int) is the answer, and it
    //     is not the shadowing row of the other derived class.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (3) f(int) falls through to the base on the other derived --\n");
    g_records.clear();
    const meta_method_t other_f_int = other.resolve_method<chain_other_t&, int>("f");
    CHECK(other_f_int.is_valid());
    CHECK(other_f_int.signature() == "f(int)");
    CHECK_ROW(other_f_int, base.method("f(int)"));
    CHECK(!(other_f_int == derived.method("f(int)")));

    // The other derived's own row still resolves on its own table.
    const meta_method_t other_k = other.resolve_method<chain_other_t&>("k");
    CHECK(other_k.is_valid());
    CHECK_ROW(other_k, other.method("k()"));
    CHECK(g_records.empty());

    // -----------------------------------------------------------------------------------------
    // (4) A miss: an invalid handle and exactly ONE Warning naming the name, the argument spelling
    //     and the class. The warning is the chain layer's, so its text must reach the caller.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (4) a miss logs exactly one Warning --\n");
    g_records.clear();
    const meta_method_t missing = derived.resolve_method<chain_derived_t&, int>("nope");
    CHECK_INVALID(missing);
    CHECK(g_records.size() == 1);
    if (g_records.size() == 1) {
        const log_record_t& record = g_records[0];
        std::printf("  log: [%s] %s\n", record.category.c_str(), record.message.c_str());
        CHECK(record.level == log_level_enum::WARNING);
        CHECK(record.category == "metaobject.connection");
        CHECK(contains(record.message, "nope"));
        CHECK(contains(record.message, "chain_derived_t"));
        CHECK(contains(record.message, "int"));    // the argument spelling
    }

    // -----------------------------------------------------------------------------------------
    // (5) The const object rule. h() and h() const both exist on the derived: a const Obj& selects
    //     the const one. A non-const-only member (f(int)) is refused entirely — it does not fall back
    //     to a base row that is non-const too.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (5) const Obj& picks h() const, refuses a non-const-only f --\n");
    g_records.clear();
    const meta_method_t const_h = derived.resolve_method<const chain_derived_t&>("h");
    CHECK(const_h.is_valid());
    CHECK(const_h.signature() == "h() const");
    CHECK_ROW(const_h, derived.method("h() const"));
    CHECK(!(const_h == derived.method("h()")));
    CHECK(g_records.empty());

    g_records.clear();
    const meta_method_t const_f = derived.resolve_method<const chain_derived_t&, int>("f");
    CHECK_INVALID(const_f);
    CHECK(g_records.size() == 1);
    if (g_records.size() == 1) {
        CHECK(g_records[0].level == log_level_enum::WARNING);
        CHECK(contains(g_records[0].message, "f"));
        CHECK(contains(g_records[0].message, "chain_derived_t"));
    }

    // -----------------------------------------------------------------------------------------
    // (6) The erased form and the typed form answer the SAME row for the same arguments: the typed
    //     front end builds exactly the identities the erased one takes.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (6) erased form == typed form --\n");
    g_records.clear();
    const std::array<cvref_meta_type_t, 1> one_int{ cvref_meta_type_t::from<int>() };
    const meta_method_t erased_f_int = derived.resolve_method<chain_derived_t&>("f", one_int);
    CHECK_ROW(erased_f_int, derived_f_int);
    CHECK_ROW(erased_f_int, derived.method("f(int)"));

    const std::span<const cvref_meta_type_t> no_args{};
    const meta_method_t erased_g = derived.resolve_method<chain_derived_t&>("g", no_args);
    CHECK_ROW(erased_g, derived_g);

    const meta_method_t erased_const_h = derived.resolve_method<const chain_derived_t&>("h", no_args);
    CHECK_ROW(erased_const_h, const_h);
    CHECK(g_records.empty());
    std::printf("  erased f(int) -> \"%s\" (same row as typed)\n",
                std::string(erased_f_int.signature()).c_str());

    // -----------------------------------------------------------------------------------------
    // (7) Ambiguity is terminal. The derived's h() and h() const both match a non-const object; the
    //     base declares an unambiguous h(), so a walk that kept looking would answer the base's row.
    //     The resolver must stop, answer an invalid handle and log exactly ONE Critical naming both
    //     candidate signatures.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (7) ambiguity stops the walk and logs one Critical --\n");
    g_records.clear();
    const meta_method_t ambiguous_h = derived.resolve_method<chain_derived_t&>("h");
    CHECK_INVALID(ambiguous_h);
    CHECK(g_records.size() == 1);
    if (g_records.size() == 1) {
        const log_record_t& record = g_records[0];
        std::printf("  log: [%s] %s\n", record.category.c_str(), record.message.c_str());
        CHECK(record.level == log_level_enum::CRITICAL);
        CHECK(record.category == "metaobject.connection");
        CHECK(contains(record.message, "h() const"));
        CHECK(contains(record.message, "h()"));
        CHECK(contains(record.message, "chain_derived_t"));
        CHECK(contains(record.message, "ambiguous"));
    }
    // The base's unambiguous h() is still reachable through the base's own table — the resolver simply
    // must not use it to break the derived's tie.
    CHECK(base.resolve_method<chain_base_t&>("h").is_valid());

    // -----------------------------------------------------------------------------------------
    // (8) The COMPILE-TIME name form. Same chain, same fixture, same rows — the name and the
    //     argument types are template arguments, and the answer is a constant. Each assertion below
    //     is duplicated against the runtime-name form of the same call, which is the point: the two
    //     front ends must agree row for row.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (8) compile-time name form: same rows as the runtime-name form --\n");
    g_records.clear();

    const meta_method_t ct_f_int = derived.resolve_method<"f", chain_derived_t&, int>();
    CHECK(ct_f_int.is_valid());
    CHECK(ct_f_int.signature() == "f(int)");
    // Own-first at compile time too: the handle names the DERIVED table's f(int), not the base's.
    CHECK_ROW(ct_f_int, derived.method("f(int)"));
    CHECK(!(ct_f_int == base.method("f(int)")));
    CHECK_ROW(ct_f_int, derived_f_int);
    std::printf("  \"f\" with (int) -> \"%s\" (derived table)\n",
                std::string(ct_f_int.signature()).c_str());

    // g() exists only on the base: the compile-time walk crosses one link and names the base row.
    const meta_method_t ct_g = derived.resolve_method<"g", chain_derived_t&>();
    CHECK(ct_g.is_valid());
    CHECK(ct_g.signature() == "g()");
    CHECK_ROW(ct_g, base.method("g()"));
    CHECK_ROW(ct_g, derived_g);

    // The same name with a different argument list: the compile-time matcher picks the overload, not
    // merely the name.
    const meta_method_t ct_f_double = derived.resolve_method<"f", chain_derived_t&, double>();
    CHECK(ct_f_double.is_valid());
    CHECK_ROW(ct_f_double, derived.method("f(double)"));
    CHECK_ROW(ct_f_double, derived_f_double);

    // The const object rule, positive side: a const Obj& selects h() const. The refusal of a
    // non-const-only member is a COMPILE error now, so it cannot be asserted here — a
    // -fsyntax-only probe is what pins it.
    const meta_method_t ct_const_h = derived.resolve_method<"h", const chain_derived_t&>();
    CHECK(ct_const_h.is_valid());
    CHECK(ct_const_h.signature() == "h() const");
    CHECK_ROW(ct_const_h, derived.method("h() const"));
    CHECK(!(ct_const_h == derived.method("h()")));
    CHECK_ROW(ct_const_h, const_h);

    // The same name and arguments asked of the BASE's own table name the base's row: the form
    // resolves against the class it is told about, not against the most derived class it can see.
    const meta_method_t ct_base_f = base.resolve_method<"f", chain_base_t&, int>();
    CHECK(ct_base_f.is_valid());
    CHECK_ROW(ct_base_f, base.method("f(int)"));
    CHECK(!(ct_base_f == derived.method("f(int)")));
    CHECK(g_records.empty());
    std::printf("  \"g\" -> \"%s\" (base table), \"h\" const -> \"%s\"\n",
                std::string(ct_g.signature()).c_str(), std::string(ct_const_h.signature()).c_str());

    // -----------------------------------------------------------------------------------------
    // (9) The receiver rule. The compile-time form answers a table->slot pair, so `*this` must BE
    //     Obj's static table: asking the BASE's table about a DERIVED object would hand back a slot
    //     that indexes the base's row order while the caller compares it against the derived's. That
    //     is a Critical and an invalid handle, never a guess.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (9) a receiver that is not Obj's table is a Critical --\n");
    g_records.clear();
    const meta_method_t wrong_receiver = base.resolve_method<"f", chain_derived_t&, int>();
    CHECK_INVALID(wrong_receiver);
    CHECK(g_records.size() == 1);
    if (g_records.size() == 1) {
        const log_record_t& record = g_records[0];
        std::printf("  log: [%s] %s\n", record.category.c_str(), record.message.c_str());
        CHECK(record.level == log_level_enum::CRITICAL);
        CHECK(record.category == "metaobject.connection");
        CHECK(contains(record.message, "chain_base_t"));
        CHECK(contains(record.message, "chain_derived_t"));
        CHECK(contains(record.message, "compile-time"));
    }

    install_log_handler(log_handler{ });

    if (g_failures == 0) {
        std::printf("  M26 chain resolve_method: all %d checks passed\n", g_checks);
    }
    else {
        std::printf("  M26 chain resolve_method: %d check(s) failed\n", g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
