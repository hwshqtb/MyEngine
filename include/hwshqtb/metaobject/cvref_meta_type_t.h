#ifndef HWSHQTB__METAOBJECT__CVREF_META_TYPE_T_H
#define HWSHQTB__METAOBJECT__CVREF_META_TYPE_T_H

/// @file cvref_meta_type_t.h
/// @brief One argument's identity: a bare type handle plus the value's cvref.

#include "basic_def.h"
#include "meta_type_t.h"
#include "export.h"

#include <string>
#include <type_traits>

namespace hwshqtb::metaobject {
    class any_t;

    /// @brief A type handle plus the value's cvref — one argument as overload resolution sees it.
    ///
    /// A parameter of a reflected table is TWO facts: the declared type (one meta_type_t handle) and
    /// the value's top-level cv and reference kind (one qualifier_t). This value type keeps them
    /// together, so "the argument T", "the argument T&" and "the argument T&&" are three distinct
    /// values even though all three carry the same bare type handle.
    ///
    /// @par Spelling
    /// spelled() renders the pair as the ONE string a table stores in its parameter column, so two
    /// arguments match when their spelled() strings are equal — the reference kind is part of the
    /// parameter's identity, and the table does not compare the two facts separately.
    ///
    /// @note This is the identity carrier of overload resolution: meta_object_t::resolve_method() takes
    ///       one per argument (the typed forms build them from `Ts...`, the erased form takes them
    ///       as given), and the table-level matcher compares the bare type plus the cvref.
    /// @note The two spellings a table stores are compared as ONE string (see spelled()), because the
    ///       reference kind is part of a parameter's identity: `T`, `T&` and `T&&` are different keys.
    /// @sa meta_type_t
    /// @sa qualifier_flag
    /// @sa get_param_qualifier
    struct HWSHQTB_EXPORT cvref_meta_type_t {
        /// The bare type (may be an INVALID meta_type_t: that is a legitimate value).
        meta_type_t type;

        /// The value's top-level cv and reference kind, as get_param_qualifier<T>() produces them.
        qualifier_t qualifier = qualifier_flag::NONE;

        /// @brief Default construction: NO type (the invalid handle) and no qualifier.
        /// @note This mirrors meta_type_t's default — "no type at all", which is NOT the void type. A
        ///       void argument identity is explicit: cvref_meta_type_t::from\<void\>(), which spells
        ///       "void" and is valid.
        /// @note An invalid argument identity is a legitimate value: it is exactly what a failed
        ///       by-name lookup answers (meta_type_t::resolve_global), and spelled() renders it as the
        ///       empty string.
        cvref_meta_type_t() = default;

        /// @brief Construct from a type handle and its cvref.
        /// @param type The bare type; an invalid handle is allowed and is a legitimate value.
        /// @param qualifier The value's top-level cv and reference kind.
        cvref_meta_type_t(const meta_type_t& type, qualifier_t qualifier = qualifier_flag::NONE);

        /// @brief The whole declared spelling, e.g. "const std::__cxx11::basic_string<char>&".
        /// @return
        ///  - the empty string for an invalid type (an invalid handle, e.g. a failed by-name lookup);
        ///  - otherwise the type's identity spelling (meta_type_t::name(), the ORIGINAL spelling the
        ///    tables use) with the cv and the reference kind placed by the rule below.
        /// @note Composed the way canonical_name.hpp spells a type: a NON-pointer's top-level cv goes
        ///       in FRONT ("const int"), a pointer's goes at the END ("int* const"), and the reference
        ///       is a suffix ("&" / "&&"). This is exactly what the table's parameter column stores, so
        ///       equality of two spelled() strings is the matching rule.
        /// @note The handle always carries the BARE type (remove_cvref_t), so no cv ever arrives twice:
        ///       the pointee's cv is part of the name ("const int*"), the top-level cv is part of
        ///       @c qualifier.
        std::string spelled() const;

        /// @brief The argument identity of a declared type, written with its cvref.
        /// @tparam T The argument type as it appears in the signature, e.g. `const std::string&`.
        /// @return The bare type (remove_cvref_t\<T\>) with get_param_qualifier\<T\>() as the cvref.
        /// @note The type handle is produced on demand (meta_type_t::from_type), so no declaration is
        ///       needed. Arrays and functions follow std::decay_t, exactly like from_type does.
        template <typename T>
        static cvref_meta_type_t from() {
            return cvref_meta_type_t{ meta_type_t::from_type<std::remove_cvref_t<T>>(),
                                      get_param_qualifier<T>() };
        }

        /// @brief The argument identity a recorded value carries.
        /// @param value The value to read; it answers the bare type and the cvref it was bound with.
        /// @return type = value.type(), qualifier = value.qualifier().
        /// @note Agrees with from\<T\>() for a value captured as T: any_t records the argument's cvref
        ///       on the way in (see any_t::capture), so the same argument spells the same either way.
        static cvref_meta_type_t from(const any_t& value);

        /// @brief Whether the handle names a type.
        /// @return type.is_valid().
        /// @note noexcept; safe on an invalid handle.
        bool is_valid() const noexcept {
            return type.is_valid();
        }

        /// @brief Whether two argument identities are the same.
        /// @return true when both the bare type and the cvref agree.
        /// @note noexcept. Since the handle always carries the bare type, this agrees with
        ///       `lhs.spelled() == rhs.spelled()` — and stays allocation-free, which the comparison
        ///       inside overload resolution needs.
        /// @note Two INVALID identities agree when their qualifiers do: neither names a type, and the
        ///       pair itself is the identity ("unknown type, by value" is one key). That keeps the
        ///       equivalence with spelled() — both spell the empty string — whereas meta_type_t's own
        ///       operator== answers false for two invalid handles, because "no type" never names the
        ///       same type. The two rules differ exactly where the pair has no type at all.
        friend bool operator==(const cvref_meta_type_t& lhs, const cvref_meta_type_t& rhs) noexcept {
            if (lhs.qualifier != rhs.qualifier) {
                return false;
            }
            if (!lhs.type.is_valid() || !rhs.type.is_valid()) {
                return !lhs.type.is_valid() && !rhs.type.is_valid();
            }
            return lhs.type == rhs.type;
        }

        /// @brief Whether two argument identities differ.
        friend bool operator!=(const cvref_meta_type_t& lhs, const cvref_meta_type_t& rhs) noexcept {
            return !(lhs == rhs);
        }
    };
}

#endif
