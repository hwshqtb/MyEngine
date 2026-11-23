// T4: regression test for the canonical namer, which had no CTest target before this file.
//
// canonical_name.hpp is entirely new code -- the primitive table, cv placement, arrays, function
// pointers, template-argument elision by substitution, reserved-scope skipping and the
// anonymous-namespace error. Every entry point is `consteval`, so the contract is pinned with
// `static_assert`s: this file failing to compile IS the test failing. The executable exists only so
// CTest has something to run (it prints a one-line summary and exits 0).
//
// The spelling is a table key, not a display string, so the assertions below pin exact bytes:
// primitive short forms, prefix cv on non-declarator types and postfix cv on declarator types,
// declaration-order dimensions, the C declarator grammar for function pointers, elided defaulted
// template arguments, no implementation-detail namespace scope, and an anonymous namespace being
// an error rather than a scope that is silently dropped.
//
// The probe that checked this header was a standalone program, not a test target; nothing in tests/
// included it before this file.

#include <meta>

#include <hwshqtb/metaobject/meta_info_t.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <map>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

using hwshqtb::metaobject::meta_info_t;

// The namespace-scope enum and the function-pointer parameter type used by the declarator cases.
// They live at global scope so the pinned spellings read exactly like the declarator they describe.
enum class E { first };
struct H { };

// A type in a plain user namespace: no reserved scope, so the two spellings must agree.
namespace owned {
    struct widget { };
}

namespace {
    // Rule 4: a type declared in an anonymous namespace has no stable global name.
    struct anonymous_namespace_type {
        int value = 0;
    };

    /// @brief Whether meta_info_t::canonical_name<T>() is exactly @p expected.
    template <typename T>
    consteval bool spells_as(std::string_view expected) {
        return meta_info_t::canonical_name<T>() == expected;
    }

    /// @brief Whether the rule-4 walk throws a std::meta::exception that names the reason.
    /// @note The catch is inside a constant expression; GCC 16.2.0 accepts that (the namer's own
    ///       spelled_argument_count() relies on the same property), so "the call throws" is
    ///       assertable at compile time instead of being left as a comment.
    consteval bool anonymous_namespace_type_throws() {
        try {
            (void)meta_info_t::canonical_name<anonymous_namespace_type>();
        }
        catch (const std::meta::exception& error) {
            return std::string_view(error.what()).find("anonymous namespace")
                != std::string_view::npos;
        }
        return false;
    }
}

/// The spelling assertion. The message names the type and the exact expected bytes.
#define M21_SPELLS(TYPE, SPELLING)                                                                 \
    static_assert(spells_as<TYPE>(SPELLING),                                                       \
                  #TYPE " must have the canonical spelling " SPELLING)

// ---------------------------------------------------------------------------------------------
// Primitive table (rule 1): all 21 fundamentals, exact short spellings. identifier_of refuses
// every fundamental, so these are the table's bytes and nothing else.
// ---------------------------------------------------------------------------------------------
M21_SPELLS(void, "void");
M21_SPELLS(bool, "bool");
M21_SPELLS(char, "char");
M21_SPELLS(signed char, "signed char");
M21_SPELLS(unsigned char, "unsigned char");
M21_SPELLS(short, "short");
M21_SPELLS(unsigned short, "unsigned short");
M21_SPELLS(int, "int");
M21_SPELLS(unsigned int, "unsigned int");
M21_SPELLS(long, "long");
M21_SPELLS(unsigned long, "unsigned long");
M21_SPELLS(long long, "long long");
M21_SPELLS(unsigned long long, "unsigned long long");
M21_SPELLS(float, "float");
M21_SPELLS(double, "double");
M21_SPELLS(long double, "long double");
M21_SPELLS(wchar_t, "wchar_t");
M21_SPELLS(char8_t, "char8_t");
M21_SPELLS(char16_t, "char16_t");
M21_SPELLS(char32_t, "char32_t");
M21_SPELLS(std::nullptr_t, "std::nullptr_t");

// ---------------------------------------------------------------------------------------------
// cv placement: prefix on a non-declarator base, postfix on a declarator type. `const int*` is a
// pointer TO const, `int* const` is a CONST pointer -- an always-prefix namer would spell the
// second as the first, which is a different type.
// ---------------------------------------------------------------------------------------------
M21_SPELLS(const int, "const int");
M21_SPELLS(const volatile int, "const volatile int");
M21_SPELLS(const int*, "const int*");
M21_SPELLS(int* const, "int* const");
M21_SPELLS(const int* const, "const int* const");
M21_SPELLS(int&, "int&");
M21_SPELLS(int&&, "int&&");
M21_SPELLS(int* const*, "int* const*");

// ---------------------------------------------------------------------------------------------
// Arrays: dimensions in declaration order, not suffix order (`int[2][3]`, not `int[3][2]`).
// ---------------------------------------------------------------------------------------------
M21_SPELLS(int[3], "int[3]");
M21_SPELLS(int[2][3], "int[2][3]");
M21_SPELLS(int[2][3][4], "int[2][3][4]");

// ---------------------------------------------------------------------------------------------
// Function pointers: the declarator is parenthesised when the target is a function/array, so the
// spelling follows C's binding rules and composes for a function returning a function pointer.
// ---------------------------------------------------------------------------------------------
M21_SPELLS(void (*)(int), "void(*)(int)");
M21_SPELLS(E (*)(H), "E(*)(H)");
M21_SPELLS(E (&)(H), "E(&)(H)");
M21_SPELLS(E (*(*)(H))(H), "E(*(*)(H))(H)");
M21_SPELLS(E (*)[2][3], "E(*)[2][3]");

// ---------------------------------------------------------------------------------------------
// Rule 2, template-argument elision: only the arguments a template's own defaults do NOT reproduce
// are spelled. The last case is the negative: a non-default allocator is part of the type's
// identity and both arguments MUST stay.
// ---------------------------------------------------------------------------------------------
M21_SPELLS(std::string, "std::basic_string<char>");
M21_SPELLS(std::vector<int>, "std::vector<int>");
M21_SPELLS(std::vector<std::string>, "std::vector<std::basic_string<char>>");

// The macro can split on a comma, so the two comma-carrying specialisations get local aliases.
using int_int_map = std::map<int, int>;
using int3_array = std::array<int, 3>;
M21_SPELLS(int_int_map, "std::map<int,int>");
M21_SPELLS(int3_array, "std::array<int,3>");

using pmr_int_vector = std::vector<int, std::pmr::polymorphic_allocator<int>>;
M21_SPELLS(pmr_int_vector, "std::vector<int,std::pmr::polymorphic_allocator<int>>");

// ---------------------------------------------------------------------------------------------
// Rule 3, reserved scopes: std::basic_string lives in the inline ABI namespace std::__cxx11 on
// this target, and that scope must not reach the key.
// ---------------------------------------------------------------------------------------------
static_assert(meta_info_t::canonical_name<std::string>().find("__cxx11") == std::string_view::npos,
              "std::string must not carry the implementation-detail scope std::__cxx11");

// ---------------------------------------------------------------------------------------------
// The SECOND spelling: original_name<T>() keeps the reserved scope rule 3 drops. Both spellings
// come from ONE walk with a flag, so a type with no reserved scope answers the same string for
// both, and a nested template argument obeys the same flag.
// ---------------------------------------------------------------------------------------------
static_assert(meta_info_t::canonical_name<std::string>() == "std::basic_string<char>",
              "canonical_name<std::string>() must stay std::basic_string<char>"); // UNCHANGED
static_assert(meta_info_t::original_name<std::string>() == "std::__cxx11::basic_string<char>",
              "original_name<std::string>() must keep std::__cxx11");            // NEW

// No reserved scope: the two spellings are equal.
static_assert(meta_info_t::original_name<std::vector<int>>() == "std::vector<int>",
              "original_name<std::vector<int>>() has no reserved scope to keep");
static_assert(meta_info_t::original_name<std::vector<int>>()
                  == meta_info_t::canonical_name<std::vector<int>>(),
              "the two spellings must agree on std::vector<int>");
static_assert(meta_info_t::original_name<owned::widget>() == "owned::widget",
              "a plain user namespace is spelled the same way by both");
static_assert(meta_info_t::original_name<owned::widget>()
                  == meta_info_t::canonical_name<owned::widget>(),
              "the two spellings must agree on a type in a plain user namespace");

// A nested template argument follows the SAME flag: the inner std::string keeps/elides __cxx11
// while the outer std::vector (no reserved scope) is unchanged.
static_assert(meta_info_t::canonical_name<std::vector<std::string>>()
                  == "std::vector<std::basic_string<char>>",
              "the canonical nested argument must elide __cxx11");
static_assert(meta_info_t::original_name<std::vector<std::string>>()
                  == "std::vector<std::__cxx11::basic_string<char>>",
              "the original nested argument must keep __cxx11");

// ---------------------------------------------------------------------------------------------
// Rule 4, anonymous namespace: the type is NOT given a spelling with the scope silently dropped.
// meta_info_t::has_canonical_name() answers false, and meta_info_t::canonical_name() itself throws
// the diagnostic.
// ---------------------------------------------------------------------------------------------
static_assert(!meta_info_t::has_canonical_name<anonymous_namespace_type>(),
              "a type declared in an anonymous namespace has no canonical name");
static_assert(anonymous_namespace_type_throws(),
              "meta_info_t::canonical_name<T>() on an anonymous-namespace type must throw std::meta::exception");

int main() {
    std::printf("== T4: canonical_name (compile-time contract) ==\n");
    constexpr std::string_view string_name = meta_info_t::canonical_name<std::string>();
    constexpr std::string_view pmr_name = meta_info_t::canonical_name<pmr_int_vector>();
    constexpr std::string_view original_string_name = meta_info_t::original_name<std::string>();
    constexpr std::string_view nested_original_name =
        meta_info_t::original_name<std::vector<std::string>>();
    std::printf("  std::string              -> %.*s\n", static_cast<int>(string_name.size()),
                string_name.data());
    std::printf("  std::string (original)   -> %.*s\n",
                static_cast<int>(original_string_name.size()), original_string_name.data());
    std::printf("  vector<string> (original)-> %.*s\n",
                static_cast<int>(nested_original_name.size()), nested_original_name.data());
    std::printf("  pmr::vector<int>         -> %.*s\n", static_cast<int>(pmr_name.size()),
                pmr_name.data());
    std::printf("  anonymous-namespace type -> meta_info_t::has_canonical_name() == false, "
                "meta_info_t::canonical_name() throws\n");
    std::puts("  all compile-time assertions passed");
    return 0;
}
