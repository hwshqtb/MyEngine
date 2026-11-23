#ifndef HWSHQTB__METAOBJECT__REFLECTION_H
#define HWSHQTB__METAOBJECT__REFLECTION_H

#include "annotation.h"
#include "register_type.hpp"

#include <meta>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>
#include <map>

namespace hwshqtb::metaobject::detail {
    /// @brief Whether @p member is a member function that declares an EXPLICIT OBJECT parameter — a
    ///        "deducing-this" member (`void f(this T& self, ...)`).
    /// @param member The member reflection.
    /// @return true for a function whose FIRST parameter is an explicit object parameter; false for
    ///         every other member (an ordinary member, a static member, a data member, a type...).
    /// @note The meta-object does NOT describe such a member. An explicit object parameter is not an
    ///       implicit one — `f(this X const x)` and `f() const` are different declarations with
    ///       different semantics — so modelling one as the other would key a row that no call could
    ///       reach correctly. The exclusion is stated here, once, and the class-level rejection
    ///       (reject_explicit_object_members) makes it LOUD instead of silent.
    consteval bool has_explicit_object_parameter(std::meta::info member) {
        if (!std::meta::is_function(member)) {
            return false;
        }
        const std::vector<std::meta::info> params = std::meta::parameters_of(member);
        return !params.empty() && std::meta::is_explicit_object_parameter(params[0]);
    }

    /// @brief Whether the member is a member function that takes part in the meta-object.
    /// @param member The member reflection.
    /// @param want_static Whether a STATIC member function is collected too.
    /// @return true for a non-static member function that is not a constructor / destructor /
    ///         assignment, not annotated [[= make_hidden()]] and NOT a deducing-this member; with
    ///         @p want_static, a static member function passes the same test as well.
    /// @note The DEFAULT is false, deliberately: the METHOD table wants statics now
    ///       (build_method_infos passes true), but every other caller of get_functions() keeps the
    ///       pre-split meaning — above all build_property_infos()'s accessor-candidate scan, where a
    ///       static `set_value(int)` must never bind as an instance property writer.
    /// @note make_hidden() applies to a static member exactly as it does to a non-static one: the
    ///       annotation test is outside the static branch. That is what keeps object_t's hidden
    ///       static entry points (register_type / create / registered_type_names) out of its table.
    /// @note An explicit-object member is EXCLUDED here, in addition to being refused loudly by
    ///       reject_explicit_object_members(): the exclusion is what lets a [[= make_hidden()]]
    ///       deducing-this member live in a class without a diagnostic (the author opted out) and
    ///       what keeps such a member out of the PROPERTY scan as well.
    consteval bool is_aimed_function(std::meta::info member, bool want_static = false) {
        return std::meta::is_function(member)
            && !std::meta::is_constructor(member)
            && !std::meta::is_destructor(member)
            && !std::meta::is_assignment(member)
            && (want_static || !std::meta::is_static_member(member))
            && std::meta::annotations_of_with_type(member, ^^hidden_a).empty()
            && !has_explicit_object_parameter(member);
    }

    consteval bool is_aimed_property(std::meta::info member) {
        const bool is_plain_function = std::meta::is_function(member)
            && !std::meta::is_constructor(member)
            && !std::meta::is_destructor(member)
            && !std::meta::is_assignment(member)
            && !std::meta::is_static_member(member)
            && !has_explicit_object_parameter(member);
        if (!std::meta::is_nonstatic_data_member(member) && !is_plain_function) {
            return false;
        }
        return !std::meta::annotations_of_with_type(member, ^^property_a).empty();
    }

    /// @brief Refuse a class that declares a non-hidden member with an EXPLICIT OBJECT parameter.
    /// @tparam T The type whose table is being built (META_OBJECT's argument).
    /// @return Nothing. The function either returns (no such member) or throws.
    /// @note THE COMPILE-TIME REJECTION, and the reason the exclusion in is_aimed_function() is not
    ///       silent: without it, a deducing-this member would simply vanish from the table and the
    ///       author would have no way to know. It is called from build_meta<T>(), before anything is
    ///       collected, so the diagnostic fires for every META_OBJECT class that declares one.
    /// @note The rule is CONDITIONAL ON HIDING, and in this order:
    ///         1. the member carries [[= make_hidden()]] (hidden_a) — the author opted out of the
    ///            reflection, so it is simply not collected and there is NO diagnostic at all;
    ///         2. otherwise, an explicit object parameter is a hard error naming the member;
    ///         3. a member with an implicit object parameter is collected as always.
    ///       hidden_a is this library's "keep this out of the reflection" annotation, so honouring it
    ///       here is the same rule every other member follows — hiding is an opt-out, not a promise
    ///       that the declaration is supported.
    /// @note The message names the member and is reported at the META_OBJECT expansion.
    /// @note A CONSTRUCTOR with an explicit object parameter is refused too: it has no identifier of
    ///       its own, so the message names it "T::T".
    template <class T>
    consteval void reject_explicit_object_members() {
        constexpr auto no_check = std::meta::access_context::unchecked();
        for (const std::meta::info member : std::meta::members_of(^^T, no_check)) {
            if (!has_explicit_object_parameter(member)) {
                continue;
            }
            if (!std::meta::annotations_of_with_type(member, ^^hidden_a).empty()) {
                continue;
            }
            std::string name;
            if (std::meta::is_constructor(member)) {
                const std::string_view class_name = std::meta::identifier_of(std::meta::parent_of(member));
                name = std::string(class_name) + "::" + std::string(class_name);
            }
            else if (std::meta::has_identifier(member)) {
                name = std::meta::identifier_of(member);
            }
            else {
                name = std::string_view{ "<unnamed member>" };
            }
            std::string msg = "metaobject: '";
            msg += name;
            msg += "' declares an explicit object parameter (`this ...`); a deducing-this member is not "
                   "equivalent to an ordinary member function — an implicit object parameter is not an "
                   "explicit one (`f(this X const x)` is not `f() const`) — so the meta-object does not "
                   "describe it. Declare a plain member function instead, or keep this one out of the "
                   "reflection with [[= make_hidden()]].";
            throw std::meta::exception(std::define_static_string(msg), member);
        }
    }

    /// @brief Every member function of T that takes part in the meta-object.
    /// @tparam T The type to reflect.
    /// @param want_static Whether STATIC member functions are part of the answer.
    /// @return The reflections in members_of() order.
    /// @note The default (false) is the pre-split behaviour: non-static member functions only. The
    ///       method table asks for both (build_method_infos passes true); the property scan must NOT
    ///       — see is_aimed_function.
    template <class T>
    consteval std::vector<std::meta::info> get_functions(bool want_static = false) {
        constexpr auto no_check = std::meta::access_context::unchecked();
        std::vector<std::meta::info> funcs;
        for (auto member : std::meta::members_of(^^T, no_check)) {
            if (is_aimed_function(member, want_static)) {
                funcs.push_back(member);
            }
        }
        return funcs;
    }

    /// @brief Whether the member is a constructor that takes part in the meta-object.
    /// @param member The member reflection.
    /// @return true for a constructor that is not annotated [[= make_hidden()]].
    /// @note Constructors follow exactly the same rule as ordinary methods: reflected by default and
    ///       excluded by make_hidden() — the ONLY exclusion. Copy / move, deleted, non-public and
    ///       abstract-class constructors are all kept.
    /// @note The table describes what the class DECLARES; what cannot be called is a property of the
    ///       CALL. build_method_info() gives the entry a caller only when `new T(args...)` compiles in
    ///       meta_info_t's context (a requires-expression, NOT std::is_constructible_v, whose access
    ///       checking is context-free), and meta_object_t::new_instance() reports a null caller as a
    ///       Critical before it builds anything. Reflecting an inaccessible or deleted constructor
    ///       needs no access.
    consteval bool is_aimed_constructor(std::meta::info member) {
        return std::meta::is_constructor(member)
            && std::meta::annotations_of_with_type(member, ^^hidden_a).empty();
    }

    /// @brief Constructors of T that take part in the meta-object: every constructor that is not
    ///        hidden.
    /// @tparam T The type to reflect.
    /// @return The list of constructor reflections in members_of() order (GCC also yields the
    ///         implicitly declared copy / move constructors, in an unspecified position). Deleted
    ///         and non-public constructors and the constructors of an abstract class are NOT dropped
    ///         — see is_aimed_constructor.
    /// @note This list's order is the constructor table's order: build_constructor_infos() turns it into
    ///       constructor_infos alone, which is the class's own constructor index space (Qt's
    ///       constructorCount()). The method table (method_infos) never holds a constructor.
    template <class T>
    consteval std::vector<std::meta::info> get_constructors() {
        constexpr auto no_check = std::meta::access_context::unchecked();
        std::vector<std::meta::info> ctors;
        for (auto member : std::meta::members_of(^^T, no_check)) {
            if (is_aimed_constructor(member)) {
                ctors.push_back(member);
            }
        }
        return ctors;
    }

    /// @brief Every property member of T that takes part in the meta-object.
    /// @tparam T The type to reflect.
    /// @return The reflections in members_of() order (the declaring class only; a base class keeps its
    ///         own rows and the chain is a lookup fallback).
    /// @note A row is an annotated NON-STATIC DATA MEMBER or an annotated ordinary member function.
    ///       The annotation alone makes it a row: a plain accessor without [[= make_property()]] is
    ///       not one, because the writer / notifier / reseter are resolved by NAME from the property's
    ///       own annotation, not collected as separate entries.
    template <class T>
    consteval std::vector<std::meta::info> get_properties() {
        constexpr auto no_check = std::meta::access_context::unchecked();
        std::vector<std::meta::info> properties;
        for (auto member : std::meta::members_of(^^T, no_check)) {
            if (is_aimed_property(member)) {
                properties.push_back(member);
            }
        }
        return properties;
    }

    /// @brief Whether the member refers to an enum declared inside the type (M5).
    /// @param member The member reflection.
    /// @return true when it is a type and an enum.
    /// @note is_enum_type throws std::meta::exception for a non-type reflection, so is_type must be
    ///       checked first.
    consteval bool is_aimed_enum(std::meta::info member) {
        return std::meta::is_type(member) && std::meta::is_enum_type(member);
    }

    /// @brief Collect every enum declared inside the type T.
    /// @tparam T The type to reflect.
    /// @return The list of enum-type reflections (declaration order; namespace-scope enums are out
    ///         of scope).
    template <class T>
    consteval std::vector<std::meta::info> get_enums() {
        constexpr auto no_check = std::meta::access_context::unchecked();
        std::vector<std::meta::info> enums;
        for (auto member : std::meta::members_of(^^T, no_check)) {
            if (is_aimed_enum(member)) {
                enums.push_back(member);
            }
        }
        return enums;
    }

}

#endif
