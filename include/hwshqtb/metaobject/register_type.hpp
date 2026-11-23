#ifndef HWSHQTB__METAOBJECT__REGISTER_TYPE_H
#define HWSHQTB__METAOBJECT__REGISTER_TYPE_H

#include "basic_def.h"
#include "canonical_name.hpp"
#include "export.h"

#include <meta>
#include <ranges>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace hwshqtb::metaobject {
    namespace detail {
        constexpr bool is_space(char c) noexcept {
            return c == ' ' || c == '\t' || c == '\n'
                || c == '\r' || c == '\f' || c == '\v';
        }

        constexpr bool is_ident_continue(char c) noexcept {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '_';
        }

        constexpr bool need_space_between(std::string_view prev,
            std::string_view curr) noexcept {
            if (prev.empty() || curr.empty()) return false;
            return is_ident_continue(prev.back()) && is_ident_continue(curr.front());
        }

        constexpr auto normalize_type(std::string_view sv) {
            auto pieces = sv
                | std::views::chunk_by([](char a, char b) noexcept {
                      return (a == ':' || b != ':')
                          && (is_space(a) == is_space(b));
                  })
                | std::views::filter([](auto&& c) {
                      return !c.empty()
                          && !is_space(*std::ranges::begin(c))
                          && !std::string_view{ std::ranges::data(c), std::ranges::size(c) }
                                  .starts_with("::__");
                  })
                | std::views::transform([](auto&& c) noexcept {
                      return std::string_view{ std::ranges::data(c), std::ranges::size(c) };
                  });

            return std::views::concat(std::views::single(std::string_view{}), pieces)
                | std::views::pairwise
                | std::views::transform([](auto&& pr) {
                      auto&& [prev, curr] = pr;
                      const bool need_space = need_space_between(prev, curr);
                      auto prefix = std::views::repeat(' ') | std::views::take(need_space ? 1u : 0u);
                      return std::views::concat(prefix, curr);
                  })
                | std::views::join;
        }

        struct split_type_t {
            std::string type;
            qualifier_t modifiers;
        };

        constexpr split_type_t split_qt_arg(std::string_view type_sv) {
            std::string t = std::ranges::to<std::string>(normalize_type(type_sv));

            auto rskip_ws = [](const std::string& s, std::size_t i) noexcept {
                while (i > 0 && is_space(s[i - 1]))
                    --i;
                return i;
            };

            std::size_t end = rskip_ws(t, t.size());
            qualifier_t modifiers = qualifier_flag::NONE;

            // 1) Trailing references
            if (end >= 2 && t[end - 1] == '&' && t[end - 2] == '&') {
                modifiers = qualifier_flag::RVALUE_REF;
                end = rskip_ws(t, end - 2);
            }
            else if (end >= 1 && t[end - 1] == '&') {
                modifiers = qualifier_flag::LVALUE_REF;
                end = rskip_ws(t, end - 1);
            }

            // 2) Trailing cv: peel one token at a time from right to left
            while (end > 0) {
                std::size_t s = end;
                while (s > 0 && is_ident_continue(t[s - 1]))
                    --s;
                if (s == end) break;

                std::string_view word(t.data() + s, end - s);
                if (word == "const") {
                    modifiers = modifiers | qualifier_flag::CONST;
                    end = rskip_ws(t, s);
                }
                else if (word == "volatile") {
                    modifiers = modifiers | qualifier_flag::VOLATILE;
                    end = rskip_ws(t, s);
                }
                else {
                    break;
                }
            }

            std::string_view remaining(t.data(), end);
            const bool is_pointer = !remaining.empty() && remaining.back() == '*';

            // 3) Leading cv: top-level only when this is not a pointer
            if (!is_pointer) {
                if (remaining.starts_with("const ")) {
                    modifiers = modifiers | qualifier_flag::CONST;
                    remaining.remove_prefix(6);
                }
                else if (remaining.starts_with("volatile ")) {
                    modifiers = modifiers | qualifier_flag::VOLATILE;
                    remaining.remove_prefix(9);
                }
            }

            return { std::string(remaining), modifiers };
        }

        /// @brief The bare spelling of an already-normalised parameter type name.
        /// @param stored A normalised spelling (detail::normalize_type / normalized_type_name
        ///        output), e.g. "const std::__cxx11::basic_string<char>&".
        /// @return A VIEW of @p stored with the top-level reference and cv removed — the spelling
        ///         `std::remove_cvref_t` names, which is what a type interface's identity and the
        ///         type registry use.
        /// @note The peeling rules are split_qt_arg's (references first, then a top-level cv word, with
        ///       a pointer to const keeping its leading `const`), but WITHOUT re-normalising: the input
        ///       is already normalised.
        /// @note Returns a view, so this allocates nothing and is usable on the invoke path
        ///       (meta_info_t::check_args) as well as in the consteval table builder.
        constexpr std::string_view bare_spelling(std::string_view stored) noexcept {
            if (stored.ends_with("&&")) {
                stored.remove_suffix(2);
            }
            else if (stored.ends_with("&")) {
                stored.remove_suffix(1);
            }
            // A pointer's top-level cv sits at the END ("object_t* const").
            for (;;) {
                if (stored.ends_with(" const")) {
                    stored.remove_suffix(6);
                }
                else if (stored.ends_with(" volatile")) {
                    stored.remove_suffix(9);
                }
                else {
                    break;
                }
            }
            // A non-pointer's top-level cv sits at the FRONT ("const int"); a pointer TO const
            // ("const object_t*") must keep it, which is why the test is "still a pointer".
            if (!stored.ends_with("*")) {
                if (stored.starts_with("const ")) {
                    stored.remove_prefix(6);
                }
                else if (stored.starts_with("volatile ")) {
                    stored.remove_prefix(9);
                }
            }
            return stored;
        }

        /// @brief Strip a parameter piece's TOP-LEVEL cv, leaving a referent's / pointee's cv alone.
        /// @param piece One parameter type piece, already trimmed and normalised (detail::normalize_type
        ///        output, optionally passed through meta_info_t::normalized_type): "const int",
        ///        "int*const", "const int&", "std::string"...
        /// @return The SAME piece with its top-level cv removed, in the three shapes C++ distinguishes:
        ///         - a REFERENCE (the piece ends with '&', covering "&&") has no top-level cv of its
        ///           own: the cv it shows belongs to the REFERENT, so the piece is returned as written
        ///           ("const int&" stays "const int&");
        ///         - a POINTER spells its top-level cv AFTER the declarator ("int* const"), so the text
        ///           following the LAST '*' is dropped when it is cv-only, and the result is the bare
        ///           declarator ("int*const" and "int* const" both answer "int*"). A POINTEE's cv is
        ///           written BEFORE the '*' ("const int*") and is therefore untouched;
        ///         - a VALUE spells its top-level cv BEFORE the type, so the leading "const " /
        ///           "volatile " words are removed ("const int" answers "int", "const volatile T"
        ///           answers "T").
        /// @note This mirrors exactly what C++ does to a FUNCTION PARAMETER's type: a parameter's
        ///       top-level cv is dropped ([dcl.fct]), while a reference's referent and a pointer's
        ///       pointee keep theirs. That is why the COMPILE-TIME spelling (normalized_type_name over
        ///       the reflected parameter) already carries no top-level cv, and the run-time user-text
        ///       side has to agree with it — the two spellings are the same key.
        /// @note The cv words are only stripped when they are a WHOLE word and are where the
        ///       declarator puts them: a '*' whose tail is not cv text (a function pointer's
        ///       "(*)(int)", an array's "*[4]") is left alone, and "constint" is not a cv prefix.
        /// @note Returns a VIEW into @p piece (a prefix or a suffix of it), so the caller must keep the
        ///       piece alive; normalized_signature() calls it inside one full expression.
        constexpr std::string_view strip_parameter_top_level_cv(std::string_view piece) noexcept {
            if (piece.ends_with('&')) {
                // '&' and "&&" alike: a reference has no top-level cv of its own.
                return piece;
            }
            // The pointer case: whatever follows the LAST '*' is the pointer's own cv, and only the
            // cv-words shape is stripped.
            if (const std::size_t star = piece.rfind('*'); star != std::string_view::npos) {
                std::string_view tail = piece.substr(star + 1);
                bool only_cv = true;
                std::size_t i = 0;
                while (i < tail.size()) {
                    while (i < tail.size() && is_space(tail[i])) {
                        ++i;
                    }
                    if (i >= tail.size()) {
                        break;
                    }
                    const std::size_t begin = i;
                    while (i < tail.size() && is_ident_continue(tail[i])) {
                        ++i;
                    }
                    if (begin == i) {
                        only_cv = false;
                        break;
                    }
                    const std::string_view word = tail.substr(begin, i - begin);
                    if (word != std::string_view("const") && word != std::string_view("volatile")) {
                        only_cv = false;
                        break;
                    }
                }
                if (only_cv) {
                    return piece.substr(0, star + 1);
                }
            }
            // The by-value case: peel the leading cv words (one token at a time, so "const volatile T"
            // loses both).
            for (;;) {
                if (piece.starts_with("const ")) {
                    piece.remove_prefix(6);
                }
                else if (piece.starts_with("volatile ")) {
                    piece.remove_prefix(9);
                }
                else {
                    return piece;
                }
            }
        }

        /// @brief Resolve a parameter piece's BASE spelling to the IDENTITY spelling the table uses.
        /// @param type One parameter type piece, already trimmed and normalised (detail::normalize_type
        ///        / meta_info_t::normalized_type output). It may carry the cv/ref/pointer affixes the
        ///        caller wrote, e.g. "const std::string&".
        /// @return The IDENTITY spelling with the affixes re-attached exactly as they were, e.g.
        ///         "const std::__cxx11::basic_string<char>&"; @p type itself when its base is not a
        ///         name the type registry knows.
        /// @note The TYPE REGISTRY is the single source of truth, and this function has no private
        ///       spelling table of its own. The base is looked up with meta_type_t::resolve_global(),
        ///       which tries the primary (identity) name and then every name bound as an alias to a
        ///       type: the canonical spelling register_type<T>() / from_type<T>() binds automatically,
        ///       the standard-library typedefs the library registers when it is loaded ("std::string",
        ///       "std::string_view", "std::wstring", detail::register_default_types) and any alias a
        ///       user bound with meta_type_t::register_type<T>(alias). A valid handle's name() is the
        ///       identity spelling, so the alias spelling, the canonical spelling and the identity
        ///       spelling of one type all converge on the same key. A spelling the registry does not
        ///       know is returned untouched — never guessed.
        /// @note RUNTIME, deliberately not constexpr: the identity spelling lives in the registry's
        ///       storage, which no constant expression can read. It runs on the USER-TEXT path only
        ///       (normalized_signature / canonical_base_type), never on invoke() and never in the
        ///       consteval table builder.
        /// @note The base is found by peeling the leading const/volatile words and the trailing
        ///       reference/pointer/pointer-cv affixes; those slices are re-attached BYTE FOR BYTE, so
        ///       this function never reorders or normalises the qualifiers (the surrounding layers
        ///       own that). Only the base spelling changes.
        /// @note Returns std::string, not a view: a rewritten piece joins the registry's spelling
        ///       with the caller's affixes, and those two do not share storage.
        HWSHQTB_EXPORT std::string original_parameter_alias(std::string_view type);

        /// @brief The ORIGINAL spelling of T, as the namer produces it (static-storage caller).
        /// @tparam T The type to name.
        /// @return canonical_name.hpp's ORIGINAL spelling (detail::original_name_of): the canonical
        ///         walk with implementation-detail namespaces KEPT, e.g.
        ///         "std::__cxx11::basic_string<char>", not a vendor rendering.
        /// @note This is the IDENTITY spelling: `type_interface_t::name` and every table key use it, so
        ///       a reflected column and the interface it refers to agree byte-for-byte by construction
        ///       (both run this overload). The canonical spelling stays the user-facing one
        ///       (meta_info_t::canonical_name<T>()) and is bound automatically as an alias by
        ///       meta_type_t::register_type<T>() / from_type<T>(), so by-name lookup accepts both.
        /// @note Uses the namer for every type, so `meta_info_t::registered_type_name<T>()`, the
        ///       synthesized value interface and the reflection-built tables agree on the primitive
        ///       short forms, the cv placement rule and template-argument elision.
        /// @note A type the namer cannot spell fails the static_assert with a readable message (the
        ///       reflection overload below throws the equivalent std::meta::exception instead, because
        ///       its argument is a constant-evaluation-only reflection parameter, which a
        ///       static_assert condition may not depend on). There is no display_string_of
        ///       fallback in either overload.
        template <typename T>
        constexpr std::string normalized_type_name() {
            static_assert(canonical_name_exists(^^T),
                "detail::normalized_type_name<T>: the type has no canonical name. "
                "canonical_name.hpp has no spelling rule for it (the known gaps are an enum-value "
                "template argument and a template-template argument); spell the type canonically or "
                "extend the namer -- a display_string_of fallback would store a registry key nothing "
                "can resolve.");
            return original_name_of(^^T);
        }

        /// @brief Normalise the spelling of a type reflection (the table-building path).
        /// @param type The type reflection.
        /// @return The ORIGINAL spelling of the type the reflection ALIASES: the namer's output with
        ///         implementation-detail namespaces KEPT, e.g. "std::__cxx11::basic_string<char>".
        /// @note This is the IDENTITY spelling `type_interface_t::name` uses, which is what makes a
        ///       property type name match its registry entry and the writer signature built from it.
        /// @note The namer dealiases first, so `^^std::string` is spelled
        ///       `std::__cxx11::basic_string<char>` -- the same key the registry uses
        ///       (`meta_info_t::registered_type_name<T>()` reflects ^^T directly, which is already
        ///       original).
        /// @note NO display_string_of fallback and no normalize_type post-pass: the namer already
        ///       produces the original spelling (cv placement, no space after a template-argument
        ///       comma; an anonymous namespace is an ERROR, not a scope to elide), and normalize_type
        ///       would undo the space in a pointer's trailing cv (`int* const` -> `int*const`).
        /// @note A type the namer cannot spell (the known gaps are an enum-value template argument
        ///       and a template-template argument) throws a std::meta::exception naming the contract,
        ///       so the build fails loudly instead of silently storing a vendor string under a name
        ///       the registry will never resolve.
        consteval std::string normalized_type_name(std::meta::info type) {
            if (!canonical_name_exists(type)) {
                throw std::meta::exception(
                    "detail::normalized_type_name: the type has no canonical name. "
                    "canonical_name.hpp has no spelling rule for it (the known gaps are an "
                    "enum-value template argument and a template-template argument); spell the "
                    "reflected type canonically or extend the namer -- a display_string_of fallback "
                    "would store a registry key nothing can resolve.",
                    type);
            }
            return original_name_of(type);
        }
    }
}

#endif
