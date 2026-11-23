// M24: cvref_meta_type_t — one argument as overload resolution sees it (a bare type handle plus the
// value's cvref), and the ONE string the table's parameter column stores for it.
//
// Nothing consumes the type yet, so this file is the whole contract for now. It pins:
//   (1) the bare spelling of a fundamental ("int"), the default-constructed (invalid, "") value and
//       the explicit void identity (from<void>(), "void") — the two are NOT the same value;
//   (2) prefix cv on a NON-pointer ("const int&");
//   (3) the rvalue-reference suffix ("int&&");
//   (4) volatile on a non-pointer ("volatile int"), plus the order canonical_name.hpp uses when both
//       bits are set ("const volatile ...");
//   (5) std::string, whose identity spelling carries __cxx11 — the expectation is built from
//       meta_info_t::original_name<std::string>(), NEVER written as a literal, so the test does not
//       pin a libstdc++ version's namespace;
//   (6) the pointer rule: a pointer's top-level cv TRAILS its declarator ("const int* const") while
//       the pointee's cv stays in front ("const int*") — the two are different spellings of two
//       different types, and conflating them is the bug this section exists to catch;
//   (7) from<T>() and from(any_t) agree for a captured const std::string&;
//   (8) equality is structural (type + cvref): T, T& and T&& never compare equal, a repeated T does,
//       and an invalid handle is !is_valid() and spells the empty string.
//
#include <meta>

#include <cstdio>
#include <string>
#include <string_view>

#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/cvref_meta_type_t.h>
#include <hwshqtb/metaobject/meta_info_t.h>
#include <hwshqtb/metaobject/meta_type_t.h>

using namespace hwshqtb::metaobject;

namespace m24_cvref_meta_type_test {

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

/// @brief Record one spelling check; both strings are printed on a mismatch.
/// @note The spelling is a table key, so a failure must show the exact bytes the two sides produced.
void check_spelled(const cvref_meta_type_t& value,
                   const std::string& expected,
                   const char* what,
                   int line) {
    ++g_checks;
    const std::string actual = value.spelled();
    if (actual != expected) {
        std::printf("  [FAIL] %s (line %d)\n    expected \"%s\"\n    actual   \"%s\"\n", what, line,
                    expected.c_str(), actual.c_str());
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_SPELLED(value, expected) check_spelled((value), (expected), #value, __LINE__)

} // namespace m24_cvref_meta_type_test

int main() {
    using namespace m24_cvref_meta_type_test;

    std::printf("== M24: cvref_meta_type_t (argument identity) ==\n");

    // -----------------------------------------------------------------------------------------
    // (1) A bare fundamental, and the state a default-constructed value is in.
    //     Default construction is the INVALID identity: meta_type_t's default is "no type" (the
    //     std::type_index analogue), so the defaulted cvref_meta_type_t is invalid and spells "".
    //     The VOID argument identity is a different, explicit value (from<void>()). This section used
    //     to assert the opposite ("void" / is_valid()), which is the behaviour the phase corrected.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (1) bare type and default construction --\n");
    CHECK_SPELLED(cvref_meta_type_t::from<int>(), "int");
    CHECK_SPELLED(cvref_meta_type_t{}, "");
    CHECK(!cvref_meta_type_t{}.is_valid());
    CHECK_SPELLED(cvref_meta_type_t::from<void>(), "void");
    CHECK(cvref_meta_type_t::from<void>().is_valid());
    CHECK(cvref_meta_type_t{} != cvref_meta_type_t::from<void>());   // "no type" is not void

    // -----------------------------------------------------------------------------------------
    // (2) Lvalue reference: the cv goes in FRONT (a non-pointer's rule) and "&" is the suffix.
    //     This is the case a "drop the reference" mutation breaks.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (2) lvalue reference keeps const in front --\n");
    CHECK_SPELLED(cvref_meta_type_t::from<const int&>(), "const int&");
    CHECK_SPELLED(cvref_meta_type_t::from<int&>(), "int&");

    // -----------------------------------------------------------------------------------------
    // (3) Rvalue reference: the SECOND "&" is what distinguishes T&& from T&, so the suffix must be
    //     the whole "&&" and not a single "&".
    // -----------------------------------------------------------------------------------------
    std::printf("-- (3) rvalue reference suffix --\n");
    CHECK_SPELLED(cvref_meta_type_t::from<int&&>(), "int&&");

    // -----------------------------------------------------------------------------------------
    // (4) Volatile, and the const-then-volatile order the namer uses. Both bits sit in one
    //     qualifier_t; the spelling must join them with a space, in that order.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (4) volatile and the cv order --\n");
    CHECK_SPELLED(cvref_meta_type_t::from<volatile int>(), "volatile int");
    CHECK_SPELLED(cvref_meta_type_t::from<const volatile int>(), "const volatile int");
    CHECK_SPELLED(cvref_meta_type_t::from<const volatile int&>(), "const volatile int&");

    // -----------------------------------------------------------------------------------------
    // (5) std::string: the identity spelling is the ORIGINAL one (std::__cxx11::basic_string<char>),
    //     which is what the tables store. The expectation is derived from the namer, so this test
    //     does not hard-code a libstdc++ detail namespace.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (5) std::string via the namer --\n");
    constexpr std::string_view string_name = meta_info_t::original_name<std::string>();
    const std::string string_spelling{string_name};
    std::printf("  meta_info_t::original_name<std::string>() = %s\n", string_spelling.c_str());
    CHECK_SPELLED(cvref_meta_type_t::from<std::string>(), string_spelling);
    CHECK_SPELLED(cvref_meta_type_t::from<std::string&>(), string_spelling + "&");
    CHECK_SPELLED(cvref_meta_type_t::from<const std::string&>(), "const " + string_spelling + "&");
    CHECK_SPELLED(cvref_meta_type_t::from<std::string&&>(), string_spelling + "&&");

    // -----------------------------------------------------------------------------------------
    // (6) Pointer rule: a NON-pointer's cv is a prefix, a POINTER's top-level cv is a suffix after
    //     the declarator. "const int*" (pointee const) and "const int* const" (also top-level const)
    //     are different keys, and the second one must not be spelled "const const int*".
    // -----------------------------------------------------------------------------------------
    std::printf("-- (6) pointer: top-level const trails the declarator --\n");
    CHECK_SPELLED(cvref_meta_type_t::from<const int*>(), "const int*");
    CHECK_SPELLED(cvref_meta_type_t::from<const int* const>(), "const int* const");
    CHECK_SPELLED(cvref_meta_type_t::from<int* const>(), "int* const");
    CHECK_SPELLED(cvref_meta_type_t::from<int* const&>(), "int* const&");
    CHECK_SPELLED(cvref_meta_type_t::from<const volatile int* const>(), "const volatile int* const");
    CHECK_SPELLED(cvref_meta_type_t::from<int** const>(), "int** const");
    // The two pointer spellings above are different identities, not decorative variants.
    CHECK(cvref_meta_type_t::from<const int*>() != cvref_meta_type_t::from<const int* const>());

    // -----------------------------------------------------------------------------------------
    // (7) from<T>() and from(any_t) must agree: any_t records the argument's cvref on the way in, so
    //     the same argument read back spells the same string as the compile-time spelling.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (7) from<T>() agrees with from(any_t) --\n");
    {
        const std::string text = "captured";
        any_t boxed = any_t::capture(text);   // T deduced as const std::string&

        const cvref_meta_type_t from_type_entry = cvref_meta_type_t::from<const std::string&>();
        const cvref_meta_type_t from_value = cvref_meta_type_t::from(boxed);

        std::printf("  from<const std::string&>() = %s\n", from_type_entry.spelled().c_str());
        std::printf("  from(any_t)                = %s\n", from_value.spelled().c_str());
        CHECK(from_type_entry.spelled() == from_value.spelled());
        CHECK(from_type_entry == from_value);
        CHECK(from_value.is_valid());
        CHECK(from_value.type == meta_type_t::from_type<std::string>());
        CHECK(from_value.qualifier == (qualifier_t(qualifier_flag::CONST) | qualifier_flag::LVALUE_REF));
        CHECK_SPELLED(from_value, "const " + string_spelling + "&");
    }

    // -----------------------------------------------------------------------------------------
    // (8) Equality is the pair (type, cvref): a repeated T is equal, T / T& / T&& are three different
    //     keys, and the invalid state is !is_valid() with an empty spelling.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (8) equality and the invalid state --\n");
    CHECK(cvref_meta_type_t::from<int>() == cvref_meta_type_t::from<int>());
    CHECK(cvref_meta_type_t::from<const int&>() == cvref_meta_type_t::from<const int&>());
    CHECK(cvref_meta_type_t::from<int>() != cvref_meta_type_t::from<const int&>());
    CHECK(cvref_meta_type_t::from<int>() != cvref_meta_type_t::from<int&>());
    CHECK(cvref_meta_type_t::from<int>() != cvref_meta_type_t::from<int&&>());
    CHECK(cvref_meta_type_t::from<int&>() != cvref_meta_type_t::from<int&&>());
    CHECK(cvref_meta_type_t::from<std::string&>() != cvref_meta_type_t::from<std::string>());

    const cvref_meta_type_t invalid{ meta_type_t::resolve_global("m24::no::such::type") };
    CHECK(!invalid.is_valid());
    CHECK(invalid.spelled().empty());
    CHECK(invalid != cvref_meta_type_t::from<int>());
    // A failed lookup and a default-constructed identity are the SAME key now (both "no type, no
    // qualifier"), which is what keeps operator== in step with spelled(). Before the phase the
    // default was void, so this had to be `!=`.
    CHECK(invalid == cvref_meta_type_t{});
    CHECK(invalid != cvref_meta_type_t::from<void>());
    std::printf("  invalid.spelled() = \"%s\"\n", invalid.spelled().c_str());

    if (g_failures == 0) {
        std::printf("  M24 cvref_meta_type_t: all %d checks passed\n", g_checks);
    }
    else {
        std::printf("  M24 cvref_meta_type_t: %d check(s) failed\n", g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
