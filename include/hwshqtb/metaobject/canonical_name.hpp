#ifndef HWSHQTB__METAOBJECT__CANONICAL_NAME_H
#define HWSHQTB__METAOBJECT__CANONICAL_NAME_H

#include <meta>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/// @file canonical_name.hpp
/// @brief The type-naming algorithm: a recursive walk over a type reflection that composes a C++
///        spelling out of `identifier_of`, a primitive-type table and the scope chain.
///
/// One walk, two spellings. The CANONICAL spelling (meta_info_t::canonical_name\<T\>(),
/// meta_info_t::canonical_name\<T\>()) is the USER-FACING one: implementation-detail namespaces are
/// elided (rule 3) and the registry binds it as an ALIAS. The ORIGINAL spelling (meta_info_t::original_name\<T\>(),
/// meta_info_t::original_name\<T\>()) is the IDENTITY spelling: the same walk with those namespaces
/// KEPT, e.g. `std::__cxx11::basic_string<char>`, and it is what `type_interface_t::name` and every
/// table key use. Neither is a vendor rendering: the walk never calls `display_string_of`, so both are
/// built only from identifiers and type-reflection comparisons. What a caller may rely on:
///   - cv-qualifiers written the way a declaration writes them: BEFORE the base type when the
///     unqualified type is not a declarator type (`const int`), AFTER the declarator when it is a
///     pointer / reference / array / function type (`int* const`), so `const int*` (pointer to const)
///     and `int* const` (const pointer) stay distinct;
///   - C's binding rules preserved by the declarator walk: `int[2][3]` (dimensions in declaration
///     order), `void(*)(int)`, `E(&)(H)`; a declarator whose target is a function or array type is
///     parenthesised (`E(*)[3]`);
///   - fundamentals spelled from a primitive table (see rule 1's table), because
///     `identifier_of` refuses every fundamental type; `std::nullptr_t` is keyed on
///     `^^decltype(nullptr)`;
///   - template specialisations spelled through `template_of` + `template_arguments_of`, with a
///     trailing run of arguments the template's own defaults reproduce elided (rule 2);
///   - a declaration's scope chain built by recursive `parent_of` + `identifier_of`, with
///     implementation-detail namespaces elided for the canonical spelling (rule 3) and kept for the
///     original one;
///   - no space after ',' inside template arguments and no leading "::".
///
/// RULE 2 -- template-argument elision. Defaulted template arguments are recovered by SUBSTITUTION,
/// not by a query: the smallest `k` for which `can_substitute(template_of(t), a[0..k))` holds and
/// `substitute(template_of(t), a[0..k)) == t` is the number of arguments that must be written, and
/// `a[k..n)` is dropped (see rule 2). A TYPE argument is named recursively; an
/// integral VALUE argument is spelled in decimal (`true`/`false` for bool). Any other value type and
/// template-template arguments have no spelling rule and make the type unnamable.
///
/// RULE 3 -- reserved scopes. An ABI namespace such as `std::__cxx11` is not part of the USER-FACING
/// spelling, so the canonical one elides it: any NAMESPACE scope whose identifier begins with `__`, or
/// with `_` followed by an uppercase letter, is dropped from the parent chain and `std::__cxx11::X` is
/// spelled `std::X`. The original spelling keeps those scopes — it is the IDENTITY spelling that
/// `type_interface_t::name` and every table key use, so elision never reaches a key. Only namespaces
/// are tested, so the declaration being named always keeps its identifier (a class called `_Impl` is
/// spelled as such).
///
/// RULE 4 -- a type declared in an anonymous namespace is an ERROR. Such a scope has no identifier,
/// so the type has no stable global name (the same declaration in another translation unit is a
/// DIFFERENT type) and cannot become a table key: the walk throws `std::meta::exception` naming the
/// offending type as soon as it finds such a scope in the parent chain, and
/// `canonical_name_exists()` answers false for the same type. Move the type to a NAMED namespace (a
/// test-local one is fine) to make it a table key.
///
/// Two deliberate limits: `noexcept` is NOT part of the name (`void()` and `void() noexcept`
/// legitimately share one key, since noexcept does not participate in overload resolution), and a
/// member-pointer type such as `int C::*` has no spelling rule and is reported as unnamable via
/// meta_info_t::has_canonical_name rather than guessed.

namespace hwshqtb::metaobject {
    namespace detail {
        /// @brief Decimal digits of @p value (constexpr; std::to_string is not usable here).
        consteval std::string decimal_unsigned(std::size_t value) {
            char digits[32] = {};
            std::size_t count = 0;
            do {
                digits[count++] = static_cast<char>('0' + value % 10);
                value /= 10;
            } while (value != 0);
            std::string out;
            out.reserve(count);
            while (count != 0) {
                out += digits[--count];
            }
            return out;
        }

        /// @brief Decimal digits of @p value, with a leading '-' for negative values.
        /// @note The magnitude is formed in unsigned arithmetic so that the most negative value
        ///       does not overflow.
        consteval std::string decimal_signed(long long value) {
            if (value < 0) {
                return "-" + decimal_unsigned(0ULL - static_cast<unsigned long long>(value));
            }
            return decimal_unsigned(static_cast<unsigned long long>(value));
        }

        /// @brief Whether a scope identifier is reserved to the implementation (rule 3).
        /// @param id The identifier of a scope.
        /// @return true when @p id begins with `__`, or with `_` followed by an uppercase letter --
        ///         the C++ reserved-identifier forms (`__cxx11`, `_GLIBCXX...`, `_V2`, ...).
        /// @note The stand-in for "is this an implementation-detail / inline ABI namespace", which
        ///       the compiler cannot be asked directly.
        consteval bool is_reserved_identifier(std::string_view id) {
            if (id.size() < 2 || id.front() != '_') {
                return false;
            }
            const char second = id[1];
            return second == '_' || (second >= 'A' && second <= 'Z');
        }

        /// @brief Whether @p e is a NAMESPACE scope that rule 3 drops from the parent chain.
        /// @param e A reflection from a parent chain.
        /// @param elide_reserved The one flag the two spellings differ by: when true (canonical)
        ///        a reserved-identifier NAMESPACE is dropped; when false (original) it is kept.
        /// @return true when @p e is a NAMESPACE with a reserved identifier (`__cxx11`, `_V2`, ...)
        ///         and @p elide_reserved is true.
        /// @note Only namespaces: dropping a class/template scope would remove part of the type's
        ///       identity, and the named declaration itself always keeps a reserved identifier.
        /// @note A namespace with NO identifier (an anonymous namespace) is NOT an elided scope: it
        ///       is the error of rule 4, not a scope to drop.
        consteval bool is_elided_scope(std::meta::info e, bool elide_reserved = true) {
            return elide_reserved
                && std::meta::is_namespace(e)
                && std::meta::has_identifier(e)
                && is_reserved_identifier(std::meta::identifier_of(e));
        }

        /// @brief A readable label for a reflection, for diagnostics.
        /// @param e A reflection that may lack an identifier.
        /// @return The identifier of @p e, or a stand-in when it has none.
        /// @note Diagnostics only: the rule-4 throw must be able to name the offending type even when
        ///       the reflection it starts from has no identifier of its own.
        consteval std::string reflection_label(std::meta::info e) {
            return std::meta::has_identifier(e) ? std::string(std::meta::identifier_of(e))
                                                : std::string("<a type without an identifier>");
        }

        /// @brief The scope-qualified identifier of a declaration: `qualified()` of the algorithm.
        /// @param e A reflection of a declaration (type, class template, namespace, ...).
        /// @param elided An optional diagnostic sink (nullptr in the namer): every scope rule 3
        ///        drops while spelling @p e is appended to it, fully qualified as reached from the
        ///        global scope (`std::__cxx11`), without duplicates.
        /// @param origin The declaration the walk started from, used ONLY by the rule-4 diagnostic so
        ///        that the exception names the offending type rather than the scope it was reached
        ///        through.
        /// @param elide_reserved The one flag the two spellings differ by: true (canonical) drops
        ///        every reserved-identifier NAMESPACE scope, false (original) keeps it.
        /// @return The identifier of @p e, prefixed by its enclosing scopes down to (but not
        ///         including) the global namespace, with every reserved-identifier NAMESPACE scope
        ///         left out when @p elide_reserved is true (rule 3).
        /// @throws std::meta::exception When the parent chain contains an anonymous namespace (rule 4):
        ///         that namespace has no identifier, the type has no stable global name, and it
        ///         cannot become a table key.
        /// @note The global scope is spelled `^^::` and compared by reflection identity; it has no
        ///       identifier of its own (`has_identifier(^^::)` is false), which is why the recursion
        ///       must stop BEFORE it rather than spell it as an empty prefix.
        /// @note No leading "::": a declaration directly in the global namespace yields its bare
        ///       identifier.
        /// @note A skipped scope is spelled as its parent prefix alone, so `std::__cxx11::X`
        ///       becomes `std::X` and `__gnu_cxx::X` becomes `X` (the scope link is dropped, not
        ///       replaced). The spelling never depends on @p elided.
        consteval std::string qualified_name(std::meta::info e, std::vector<std::string>* elided,
                                             std::meta::info origin, bool elide_reserved = true) {
            std::string prefix;
            if (std::meta::has_parent(e)) {
                const std::meta::info parent = std::meta::parent_of(e);
                if (!(parent == ^^::)) {
                    if (std::meta::is_namespace(parent) && !std::meta::has_identifier(parent)) {
                        throw std::meta::exception(
                            "canonical_name: the type '" + reflection_label(origin)
                                + "' is declared inside an anonymous namespace. An anonymous namespace "
                                  "has no identifier and therefore no stable global name, so the type "
                                  "cannot become a table key; move it to a named namespace.",
                            origin);
                    }
                    prefix = qualified_name(parent, elided, origin, elide_reserved);
                }
            }
            if (is_elided_scope(e, elide_reserved)) {
                if (elided != nullptr) {
                    const std::string own = std::string(std::meta::identifier_of(e));
                    const std::string reached = prefix.empty() ? own : prefix + "::" + own;
                    bool seen = false;
                    for (const std::string& previous : *elided) {
                        seen = seen || previous == reached;
                    }
                    if (!seen) {
                        elided->push_back(reached);
                    }
                }
                return prefix; // drop this link, keep the outer scopes
            }
            if (!prefix.empty()) {
                prefix += "::";
            }
            prefix += std::meta::identifier_of(e);
            return prefix;
        }

        /// @brief The scope walk for the namer: no diagnostic sink, @p e is its own origin.
        consteval std::string qualified_name(std::meta::info e) {
            return qualified_name(e, nullptr, e);
        }

        /// @brief Whether the scope walk can spell @p e (the same walk, checked).
        /// @note See meta_info_t::has_canonical_name : a diagnostic aid, not a fallback spelling.
        /// @note Rule 3 does not change the answer for a reserved scope: it still has an identifier
        ///       (it is what the reserved-identifier test reads), so the check is the untouched
        ///       parent walk. An ANONYMOUS namespace is the one scope that makes the answer false
        ///       (rule 4): it has no identifier, so no spelling of the type can name its scope.
        consteval bool qualified_name_exists(std::meta::info e) {
            if (!std::meta::has_identifier(e) && !std::meta::is_namespace(e)) {
                return false;
            }
            if (std::meta::is_namespace(e) && !std::meta::has_identifier(e)) {
                return false; // rule 4: an anonymous namespace cannot be spelled
            }
            if (std::meta::has_parent(e)) {
                const std::meta::info parent = std::meta::parent_of(e);
                if (!(parent == ^^::)) {
                    return qualified_name_exists(parent);
                }
            }
            return true;
        }

        /// @brief How many template arguments of @p t must be spelled (rule 2).
        /// @param t A DEALIASED type reflection with `has_template_arguments(t) == true`.
        /// @return The smallest `k` such that
        ///         `can_substitute(template_of(t), a[0..k))` and
        ///         `substitute(template_of(t), a[0..k)) == t`,
        ///         i.e. the length of the shortest prefix of @p t's arguments that already
        ///         reproduces @p t through the template's own defaults; `template_arguments_of(t)
        ///         .size()` when no shorter prefix does (the full list is then spelled).
        /// @note Defaulted arguments are recovered by substitution, not by a query: the compiler has
        ///       neither `template_parameters_of` nor `default_argument_of`.
        /// @note `substitute` is reached only after `can_substitute` returned true (a constrained
        ///       template whose constraint is not satisfied makes `can_substitute` return false), and
        ///       is still wrapped in a `try`/`catch` because it may throw `std::meta::exception`; on a
        ///       throw the answer is "no elision" rather than an error escaping the walk.
        consteval std::size_t spelled_argument_count(std::meta::info t) {
            const std::meta::info tmpl = std::meta::template_of(t);
            const std::vector<std::meta::info> args = std::meta::template_arguments_of(t);
            for (std::size_t k = 0; k != args.size(); ++k) {
                const auto stop = args.begin() + static_cast<std::ptrdiff_t>(k);
                const std::vector<std::meta::info> prefix(args.begin(), stop);
                if (!std::meta::can_substitute(tmpl, prefix)) {
                    continue;
                }
                try {
                    if (std::meta::substitute(tmpl, prefix) == t) {
                        return k;
                    }
                }
                catch (...) {
                    // can_substitute() said the substitution is well-formed but it still refused;
                    // do not elide (the full argument list is the safe spelling).
                }
            }
            return args.size();
        }

        /// @brief The table spelling of a fundamental type: @p t == `^^T` for one primitive T.
        /// @param t A DEALIASED type reflection. The caller (the canonical walk) has already
        ///          removed cv-qualifiers, so the comparison is against the unqualified `^^T`.
        /// @return The spelling from the primitive table, or an empty view when @p t is not one of
        ///         the primitives in the table (i.e. not a standard fundamental type).
        /// @note `identifier_of` refuses every fundamental type, so the ONE spelling source here is
        ///       reflection equality. `std::nullptr_t` is keyed on `^^decltype(nullptr)` because
        ///       `dealias` maps the alias to `decltype(nullptr)` first.
        consteval std::string_view primitive_spelling(std::meta::info t) {
            if (t == ^^void) {
                return "void";
            }
            if (t == ^^bool) {
                return "bool";
            }
            if (t == ^^char) {
                return "char";
            }
            if (t == ^^signed char) {
                return "signed char";
            }
            if (t == ^^unsigned char) {
                return "unsigned char";
            }
            if (t == ^^short) {
                return "short";
            }
            if (t == ^^unsigned short) {
                return "unsigned short";
            }
            if (t == ^^int) {
                return "int";
            }
            if (t == ^^unsigned int) {
                return "unsigned int";
            }
            if (t == ^^long) {
                return "long";
            }
            if (t == ^^unsigned long) {
                return "unsigned long";
            }
            if (t == ^^long long) {
                return "long long";
            }
            if (t == ^^unsigned long long) {
                return "unsigned long long";
            }
            if (t == ^^float) {
                return "float";
            }
            if (t == ^^double) {
                return "double";
            }
            if (t == ^^long double) {
                return "long double";
            }
            if (t == ^^wchar_t) {
                return "wchar_t";
            }
            if (t == ^^char8_t) {
                return "char8_t";
            }
            if (t == ^^char16_t) {
                return "char16_t";
            }
            if (t == ^^char32_t) {
                return "char32_t";
            }
            if (t == ^^decltype(nullptr)) {
                return "std::nullptr_t";
            }
            return {};
        }

        /// @brief The spelling of an integral value template argument.
        /// @param value A reflection with `std::meta::is_value(value) == true`.
        /// @return The decimal spelling of the value (bool is spelled `true`/`false`), or an empty
        ///         string when the value's type has no rule (not an integral fundamental type).
        /// @note `std::meta::extract<T>` requires T to be the argument's exact type: the `3` of
        ///       `std::array<int,3>` is a `size_t`, so it takes `extract<unsigned long long>`, while
        ///       `extract<int>` refuses with `value cannot be extracted`.
        /// @note Character types are spelled numerically (`std::array<char,97>`, not a character
        ///       literal), and a `bool` value is spelled `true`/`false`.
        consteval std::string value_spelling(std::meta::info value) {
            const std::meta::info t = std::meta::type_of(value);
            if (t == ^^bool) {
                return std::meta::extract<bool>(value) ? "true" : "false";
            }
            if (t == ^^char) {
                return decimal_signed(static_cast<long long>(std::meta::extract<char>(value)));
            }
            if (t == ^^signed char) {
                return decimal_signed(static_cast<long long>(std::meta::extract<signed char>(value)));
            }
            if (t == ^^unsigned char) {
                return decimal_unsigned(
                    static_cast<unsigned long long>(std::meta::extract<unsigned char>(value)));
            }
            if (t == ^^short) {
                return decimal_signed(static_cast<long long>(std::meta::extract<short>(value)));
            }
            if (t == ^^unsigned short) {
                return decimal_unsigned(
                    static_cast<unsigned long long>(std::meta::extract<unsigned short>(value)));
            }
            if (t == ^^int) {
                return decimal_signed(static_cast<long long>(std::meta::extract<int>(value)));
            }
            if (t == ^^unsigned int) {
                return decimal_unsigned(
                    static_cast<unsigned long long>(std::meta::extract<unsigned int>(value)));
            }
            if (t == ^^long) {
                return decimal_signed(static_cast<long long>(std::meta::extract<long>(value)));
            }
            if (t == ^^unsigned long) {
                return decimal_unsigned(
                    static_cast<unsigned long long>(std::meta::extract<unsigned long>(value)));
            }
            if (t == ^^long long) {
                return decimal_signed(static_cast<long long>(std::meta::extract<long long>(value)));
            }
            if (t == ^^unsigned long long) {
                return decimal_unsigned(static_cast<unsigned long long>(
                    std::meta::extract<unsigned long long>(value)));
            }
            if (t == ^^wchar_t) {
                return decimal_signed(static_cast<long long>(std::meta::extract<wchar_t>(value)));
            }
            if (t == ^^char8_t) {
                return decimal_unsigned(
                    static_cast<unsigned long long>(std::meta::extract<char8_t>(value)));
            }
            if (t == ^^char16_t) {
                return decimal_unsigned(
                    static_cast<unsigned long long>(std::meta::extract<char16_t>(value)));
            }
            if (t == ^^char32_t) {
                return decimal_unsigned(
                    static_cast<unsigned long long>(std::meta::extract<char32_t>(value)));
            }
            return {};
        }

        /// @brief The canonical spelling of a type reflection: `name()` of the algorithm.
        /// @param t A reflection of a TYPE.
        /// @param inner The declarator accumulated by the outer layers, inserted at the point where
        ///        the identifier would go: "" for the whole type, "*" for a pointer's target,
        ///        "(*)" for a pointer to function/array, "[2]" for an array's element, "(int)" for
        ///        a function's return type.
        /// @param elided An optional diagnostic sink (nullptr in the namer): every namespace scope
        ///        rule 3 drops, and nothing else, is appended to it. The spelling never depends on
        ///        it.
        /// @param elide_reserved The one flag the two spellings differ by: true (canonical) drops
        ///        every reserved-identifier NAMESPACE scope, false (original) keeps it. Threaded
        ///        through every recursive arm, so a nested template argument obeys the same flag.
        /// @return The canonical spelling built from identifiers, the primitive table and
        ///         reflection comparisons only.
        /// @note Template-argument elision lives in rule 2: a specialisation
        ///       spells only the argument prefix that does not follow from the template's own defaults
        ///       (`std::vector<int>`, not `std::vector<int,std::allocator<int>>`).
        /// @note Preconditions are the algorithm's own: every entity the walk reaches must be
        ///       nameable (GCC reports `reflection with has_identifier false` otherwise), and an
        ///       anonymous namespace in the scope chain throws a std::meta::exception naming the
        ///       type (rule 4).
        /// @note The top-level cv is peeled first and the UNQUALIFIED type decides where it goes:
        ///       after the declarator for a pointer / reference / array / function type
        ///       (`int* const`), before the base spelling for everything else (`const int`).
        /// @note Arrays accumulate their extent at the FRONT of the element's declarator, which puts
        ///       the dimensions in declaration order: `E[2][3]`. An unknown bound is spelled `[]`
        ///       (queried with `is_unbounded_array_type`, because `extent` answers 0 for it too).
        /// @note A pointer/reference to a function or array parenthesises its own declarator
        ///       (`(*)`, `(&)`, `(&&)`) before recursing, which produces `E(*)(H)` instead of
        ///       `E(H)*`; it composes to `E(*(*)(H))(H)`.
        consteval std::string canonical_name_of(std::meta::info t,
                                                std::string_view inner,
                                                std::vector<std::string>* elided,
                                                bool elide_reserved = true) {
            t = std::meta::dealias(t);

            if (std::meta::is_const(t) || std::meta::is_volatile(t)) {
                // Peel the top-level cv first, both qualifiers at once, so the placement decision
                // sees the UNQUALIFIED type.
                const bool qualified_const = std::meta::is_const(t);
                const bool qualified_volatile = std::meta::is_volatile(t);
                std::meta::info unqualified = t;
                if (qualified_const) {
                    unqualified = std::meta::remove_const(unqualified);
                }
                if (qualified_volatile) {
                    unqualified = std::meta::remove_volatile(unqualified);
                }
                std::string qualifiers;
                if (qualified_const) {
                    qualifiers = "const";
                }
                if (qualified_volatile) {
                    qualifiers += qualifiers.empty() ? "volatile" : " volatile";
                }
                // A declarator type's own cv belongs AFTER its declarator (`int* const`):
                // prepending to `inner` puts it between the inner declarator this branch contributes
                // and any OUTER declarator already inside `inner`.
                if (std::meta::is_pointer_type(unqualified)
                    || std::meta::is_lvalue_reference_type(unqualified)
                    || std::meta::is_rvalue_reference_type(unqualified)
                    || std::meta::is_array_type(unqualified)
                    || std::meta::is_function_type(unqualified)) {
                    return canonical_name_of(unqualified, " " + qualifiers + std::string(inner),
                                             elided, elide_reserved);
                }
                // Every other type is qualified by prefixing the base spelling (`const int`).
                return qualifiers + " " + canonical_name_of(unqualified, inner, elided,
                                                            elide_reserved);
            }
            if (std::meta::is_pointer_type(t)) {
                const std::meta::info target = std::meta::remove_pointer(t);
                if (std::meta::is_function_type(target) || std::meta::is_array_type(target)) {
                    return canonical_name_of(target, "(*" + std::string(inner) + ")", elided,
                                             elide_reserved);
                }
                return canonical_name_of(target, "*" + std::string(inner), elided, elide_reserved);
            }
            if (std::meta::is_lvalue_reference_type(t)) {
                const std::meta::info target = std::meta::remove_reference(t);
                if (std::meta::is_function_type(target) || std::meta::is_array_type(target)) {
                    return canonical_name_of(target, "(&" + std::string(inner) + ")", elided,
                                             elide_reserved);
                }
                return canonical_name_of(target, "&" + std::string(inner), elided, elide_reserved);
            }
            if (std::meta::is_rvalue_reference_type(t)) {
                const std::meta::info target = std::meta::remove_reference(t);
                if (std::meta::is_function_type(target) || std::meta::is_array_type(target)) {
                    return canonical_name_of(target, "(&&" + std::string(inner) + ")", elided,
                                             elide_reserved);
                }
                return canonical_name_of(target, "&&" + std::string(inner), elided, elide_reserved);
            }
            if (std::meta::is_array_type(t)) {
                // Extent 0 is ambiguous between "unknown bound" and a zero-length array (which
                // C++ does not have), so ask the dedicated query instead of spelling `[0]`.
                const std::string extent = std::meta::is_unbounded_array_type(t)
                                               ? std::string("[]")
                                               : "[" + decimal_unsigned(std::meta::extent(t)) + "]";
                return canonical_name_of(std::meta::remove_extent(t), std::string(inner) + extent,
                                         elided, elide_reserved);
            }
            if (std::meta::is_function_type(t)) {
                std::string parameters;
                const std::vector<std::meta::info> params = std::meta::parameters_of(t);
                for (std::size_t i = 0; i != params.size(); ++i) {
                    if (i != 0) {
                        parameters += ",";
                    }
                    // For a function TYPE the elements are TYPE reflections, so they are named
                    // directly (a function ENTITY would yield parameter entities; this namer only
                    // walks type reflections).
                    parameters += canonical_name_of(params[i], "", elided, elide_reserved);
                }
                if (std::meta::is_vararg_function(t)) {
                    if (!params.empty()) {
                        parameters += ",";
                    }
                    parameters += "...";
                }
                return canonical_name_of(std::meta::return_type_of(t),
                                         std::string(inner) + "(" + parameters + ")", elided,
                                         elide_reserved);
            }
            if (std::meta::is_fundamental_type(t)) {
                const std::string_view spelling = primitive_spelling(t);
                if (!spelling.empty()) {
                    return std::string(spelling) + std::string(inner);
                }
                // No primitive in the chain matched: fall through so that qualified_name() reports
                // the missing identifier instead of this namer inventing a spelling.
            }
            if (std::meta::has_template_arguments(t)) {
                // Only the arguments that do not follow from the template's own defaults:
                // spelled_argument_count() decides the loop bound, so `std::vector<int>` comes out
                // short.
                const std::size_t spelled = spelled_argument_count(t);
                std::string out = qualified_name(std::meta::template_of(t), elided,
                                                 std::meta::template_of(t), elide_reserved)
                    + "<";
                const std::vector<std::meta::info> args = std::meta::template_arguments_of(t);
                for (std::size_t i = 0; i != spelled; ++i) {
                    if (i != 0) {
                        out += ",";
                    }
                    const std::meta::info arg = args[i];
                    if (std::meta::is_type(arg)) {
                        out += canonical_name_of(arg, "", elided, elide_reserved);
                    }
                    else if (std::meta::is_value(arg)) {
                        std::string spelling = value_spelling(arg);
                        if (spelling.empty()) {
                            // No rule for this value type: fail rather than invent a spelling (a
                            // non-type reflection is not a type, so dealias() rejects it with
                            // "reflection does not represent a type").
                            out += canonical_name_of(std::meta::dealias(arg), "", elided,
                                                     elide_reserved);
                        }
                        else {
                            out += spelling;
                        }
                    }
                    else {
                        // Template-template (or any other) argument: no rule.
                        out += canonical_name_of(std::meta::dealias(arg), "", elided,
                                                 elide_reserved);
                    }
                }
                out += ">";
                out += inner;
                return out;
            }
            return qualified_name(t, elided, t, elide_reserved) + std::string(inner);
        }

        /// @brief One-argument front end: the whole type, no accumulated declarator.
        consteval std::string canonical_name_of(std::meta::info t) {
            return canonical_name_of(t, std::string_view{}, nullptr);
        }

        /// @brief The ORIGINAL spelling: like canonical_name_of, but implementation-detail namespaces
        ///        (std::__cxx11, __gnu_cxx, ...) are KEPT.
        /// @return e.g. "std::__cxx11::basic_string<char>"; identical to canonical_name_of when the type
        ///         has no reserved scope (std::vector<int> answers the same string for both).
        /// @note This is the IDENTITY spelling: `type_interface_t::name` and every table key use it. The
        ///       canonical spelling is the user-facing one and is only bound as a registry alias.
        /// @note Same walk, one flag: canonical_name_of(t, std::string_view{}, nullptr, false). The
        ///       elision rule cannot change whether a name exists, so has_canonical_name() is shared.
        consteval std::string original_name_of(std::meta::info t) {
            return canonical_name_of(t, std::string_view{}, nullptr, false);
        }

        /// @brief The scopes rule 3 elides when naming @p t (diagnostic; empty when none).
        /// @param t A reflection of a TYPE.
        /// @return The fully-qualified names of the namespace scopes the walk dropped, in the order
        ///         it reached them, without duplicates.
        /// @note This runs the real walk (the canonical walk) with its diagnostic sink
        ///       attached, so it can never disagree with the spelling; it is meaningful only for a
        ///       type whose name exists (see meta_info_t::has_canonical_name).
        consteval std::vector<std::string> elided_scopes_of(std::meta::info t) {
            std::vector<std::string> elided;
            (void)canonical_name_of(t, std::string_view{}, &elided);
            return elided;
        }

        /// @brief Whether the canonical walk can produce a name, without calling identifier_of
        ///        where it would refuse.
        /// @param t A reflection of a TYPE.
        /// @return true exactly when the recursive walk reaches only types that can be spelled:
        ///         primitives in the table, nameable declarations, type template arguments that
        ///         recurse, and integral value template arguments.
        /// @note This mirrors the canonical walk branch for branch and produces NO spelling of
        ///       its own: it lets a diagnostic report the gap instead of failing to compile. It is
        ///       not a fallback for the namer. meta_info_t::has_canonical_name is its public face.
        /// @note The elision rule applies here too, with the same rule 2: an
        ///       elided defaulted argument is never spelled, so it cannot make the type unnamable.
        consteval bool canonical_name_exists(std::meta::info t) {
            t = std::meta::dealias(t);

            if (std::meta::is_const(t)) {
                return canonical_name_exists(std::meta::remove_const(t));
            }
            if (std::meta::is_volatile(t)) {
                return canonical_name_exists(std::meta::remove_volatile(t));
            }
            if (std::meta::is_pointer_type(t)) {
                return canonical_name_exists(std::meta::remove_pointer(t));
            }
            if (std::meta::is_lvalue_reference_type(t) || std::meta::is_rvalue_reference_type(t)) {
                return canonical_name_exists(std::meta::remove_reference(t));
            }
            if (std::meta::is_array_type(t)) {
                return canonical_name_exists(std::meta::remove_extent(t));
            }
            if (std::meta::is_function_type(t)) {
                if (!canonical_name_exists(std::meta::return_type_of(t))) {
                    return false;
                }
                for (const std::meta::info param : std::meta::parameters_of(t)) {
                    if (!canonical_name_exists(param)) {
                        return false;
                    }
                }
                return true;
            }
            if (std::meta::is_fundamental_type(t)) {
                return !primitive_spelling(t).empty();
            }
            if (std::meta::has_template_arguments(t)) {
                if (!qualified_name_exists(std::meta::template_of(t))) {
                    return false;
                }
                // Rule 2: only the arguments the spelling would keep have to be nameable; a
                // defaulted tail that rule 2 elides is never spelled, so it cannot make the type
                // unnamable.
                const std::size_t spelled = spelled_argument_count(t);
                const std::vector<std::meta::info> args = std::meta::template_arguments_of(t);
                for (std::size_t i = 0; i != spelled; ++i) {
                    const std::meta::info arg = args[i];
                    if (std::meta::is_type(arg)) {
                        if (!canonical_name_exists(arg)) {
                            return false;
                        }
                    }
                    else if (std::meta::is_value(arg)) {
                        if (value_spelling(arg).empty()) {
                            return false;
                        }
                    }
                    else {
                        return false; // no rule for template-template / other arguments
                    }
                }
                return true;
            }
            // A declaration is spellable only when its whole SCOPE CHAIN is: its own identifier AND
            // every parent up to the global scope. has_identifier() alone would accept a class in an
            // anonymous namespace (the class has an identifier, its parent scope does not);
            // qualified_name_exists() answers false for that scope (rule 4) instead of letting the
            // spelling throw.
            return qualified_name_exists(t);
        }
    }
}

#endif
