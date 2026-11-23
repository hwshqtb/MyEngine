#ifndef HWSHQTB__METAOBJECT__ANNOTATION_H
#define HWSHQTB__METAOBJECT__ANNOTATION_H

#include "basic_def.h"
#include "flag_t.h"

#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <meta>

namespace hwshqtb::metaobject {
    struct hidden_a { };

    struct signal_a { };

    /// @brief The accessor NAMES of one property (writer / notifier / reseter) and its traits.
    ///
    /// Each accessor field holds a BARE C++ FUNCTION NAME — "set_value", "value_changed",
    /// "reset_value" — or "" for "no such accessor"; the name is not a signature and carries no
    /// parameter spelling, the parameter list is what compile-time resolution determines.
    ///
    /// The role fixes the parameter shape: writer takes exactly one parameter matching the property
    /// type, reseter none, notifier one matching parameter first and then none. An exact type match
    /// wins; with none, a SINGLE cv/ref-stripped ("bare") match is accepted. Two candidates at the
    /// same level are a compile-time error — a bare name cannot choose, and never a guess.
    ///
    /// @note The compile-time scan covers the DECLARING class only: an accessor declared by a base
    ///       class keeps its bare name and is resolved at run time by
    ///       meta_property_t::writer() / reseter() / notifier() against the object's meta-object
    ///       chain. A name that resolves nowhere is logged at Critical once per handle and role, and
    ///       the query answers with an invalid handle — the API stays noexcept.
    struct property_a {
        const char* name = nullptr;
        const char* writer = nullptr;
        const char* notifier = nullptr;
        const char* reseter = nullptr;
        property_trait_t traits = property_trait_flag::NONE;
    };

    /// @brief Build a property annotation.
    /// @param name The property name (the hash key); required.
    /// @param writer The writer's BARE NAME ("set_value"); empty means read-only.
    /// @param notifier The change-notification signal's BARE NAME ("value_changed"); empty means none.
    /// @param reseter The reset function's BARE NAME ("reset_value"); empty means none.
    /// @param traits The property traits.
    /// @return The annotation value.
    /// @note A bare name is the ONLY form: the parameter list is discovered by reflection (see
    ///       property_a above), so a signature such as "set_value(double)" names nothing and stays
    ///       unresolved.
    /// @note An accessor declared by a base class is fine — it resolves at run time (inheritance is
    ///       not visible at compile time by design).
    consteval property_a make_property(std::string_view name, std::string_view writer = "", std::string_view notifier = "", std::string_view reseter = "", property_trait_t traits = property_trait_flag::NONE) {
        return {
            std::define_static_string(name),
            std::define_static_string(writer),
            std::define_static_string(notifier),
            std::define_static_string(reseter),
            traits
        };
    }

    // for type-alias like `using str [[=rewrite{}]] = std::string_view;`
    // so that `str` will be transfered to `std::basic_string_view<char>`
    struct rewrite_a { };

    /// @brief Class-level key/value metadata (Q_CLASSINFO equivalent).
    struct class_info_a {
        /// @brief The key.
        const char* name = nullptr;
        /// @brief The value.
        const char* value = nullptr;
    };

    /// @brief Binds an interface type to its IID (replaces the old DECLARE_INTERFACE macro).
    /// @note Written on the interface's own definition:
    ///       `struct [[= make_interface("com.example.IFoo/1.0")]] IFoo { ... };`
    /// @note The IID is a `const char*`, not a std::string_view, because an annotation value must
    ///       have a STRUCTURAL type and std::string_view is not one (its members are private), so
    ///       GCC rejects the annotation with "annotation does not have structural type". The text
    ///       itself lives in static storage: make_interface copies it there.
    struct interface_a {
        /// @brief The IID, e.g. "com.example.IFoo/1.0".
        const char* iid = nullptr;
    };

    // ---- factories: the marker structs above are deliberately NOT exported, so that users can only
    // ---- spell an annotation through these functions (same pattern as make_property).

    /// @brief Mark a member as not observed (no func_info / no entry in the method list).
    consteval hidden_a make_hidden() {
        return {};
    }

    /// @brief Mark a member function as a signal.
    consteval signal_a make_signal() {
        return {};
    }

    /// @brief Mark a type alias for rewriting.
    consteval rewrite_a make_rewrite() {
        return {};
    }

    /// @brief Attach a key/value pair to the class (Q_CLASSINFO equivalent).
    /// @param name The key.
    /// @param value The value.
    /// @return The annotation value.
    consteval class_info_a make_class_info(std::string_view name, std::string_view value) {
        return { std::define_static_string(name), std::define_static_string(value) };
    }

    /// @brief Bind an interface type to its IID: THIS annotation is what makes a type an interface.
    /// @param iid The IID, e.g. "com.example.IFoo/1.0"; its characters are copied into static
    ///        storage, so the annotation stays valid for the whole program.
    /// @return The annotation value.
    /// @note Written on the interface's own definition, never on the class that implements it:
    ///       `struct [[= make_interface("com.example.IFoo/1.0")]] IFoo { ... };`
    /// @note An interface type carries make_interface on its OWN definition, and a class implements
    ///       it by inheriting it PUBLICLY. build_meta<T>() reflects T's direct bases and records one
    ///       interface row per accessible interface base — there is no INTERFACES(...) list to keep
    ///       in sync, and a private / protected interface base is not reported (a consumer's
    ///       static_cast<I*>(T*) would not compile for it).
    /// @note build_interface_infos reads the annotation with annotations_of_with_type through
    ///       interface_iid_of() below; an interface base that carries none is a compile error.
    consteval interface_a make_interface(std::string_view iid) {
        return { std::define_static_string(iid) };
    }

    /// @brief The IID an interface type carries (its make_interface annotation).
    /// @tparam T The interface type.
    /// @return The annotated IID text, e.g. "com.example.IFoo/1.0".
    /// @note This is the ONE way an IID is found: build_interface_infos copies it into the interface
    ///       table, and cast<T>() passes it to that table's IID lookup.
    /// @note A missing annotation throws a std::meta::exception naming the type and the factory: a
    ///       table entry with no IID would be a null key, so the build must fail instead.
    template <typename T>
    consteval std::string_view interface_iid_of() {
        constexpr auto annos = std::define_static_array(std::meta::annotations_of_with_type(^^T, ^^interface_a));
        if (annos.empty()) {
            const std::string msg = std::string("interface type '")
                + std::string(std::meta::display_string_of(^^T))
                + "' carries no [[= make_interface(\"...\")]] annotation: an interface type must "
                  "bind its IID on its own definition";
            throw std::meta::exception(std::define_static_string(msg), ^^T);
        }
        return std::string_view(std::meta::extract<interface_a>(annos[0]).iid);
    }

    /// @brief A {minor, major} revision (QTypeRevision counterpart); structural by design.
    /// @note Q_REVISION(42) is the minor revision, so `revision_t{42, 0}` is its counterpart here.
    struct revision_t {
        int minor = 0;
        int major = 0;
    };

    /// @brief A generic value annotation: the value's type reflection + a pointer to its staticised
    ///        storage.
    /// @note The reflection is what makes the KIND of the value recoverable at build time (an `int`
    ///       revision, a text tag, a user struct); it is flattened to its type name when the
    ///       meta-object table is built, because a std::meta::info is consteval-only and cannot live
    ///       in a runtime table.
    /// @sa make_tag
    /// @sa make_str_tag
    struct tag_a {
        /// @brief The IDENTITY of the carried value's type (a type reflection).
        std::meta::info type;
        /// @brief The staticised value: a pointer to a static object of that type.
        /// @note For make_str_tag the carried type is `const char*` and the static object IS that
        ///       pointer (so reading it back is a double indirection: `*(const char* const*)value`).
        const void* value = nullptr;
    };

    /// @brief Attach a staticised value of any structural type (int, revision_t{...}, double, ...).
    /// @tparam T The value's type, deduced from the argument.
    /// @param v The value; it is copied into static storage.
    /// @return The annotation, carrying ^^T and the pointer to that static object.
    /// @note The value must be of a structural type: define_static_object() rejects anything else
    ///       (that diagnostic is the guardrail — there is no constraint here on purpose).
    /// @note Text is NOT carried by this form: std::string_view is not structural, and a pointer to a
    ///       string literal is not reflectable. Use make_str_tag for text.
    template <typename T>
    consteval tag_a make_tag(T v) {
        return { ^^T, std::define_static_object(v) };
    }

    /// @brief Attach a staticised text tag (stored as a static const char*; the carried type is
    ///        ^^const char*).
    /// @param v The text; its characters are copied into static storage by define_static_string.
    /// @return The annotation, carrying ^^const char* and the pointer to that static `const char*`.
    /// @note The double indirection is deliberate: the tag value is the static `const char*` OBJECT,
    ///       so a reader casts to `const char* const*` and dereferences once.
    consteval tag_a make_str_tag(std::string_view v) {
        return { ^^const char*, std::define_static_object(std::define_static_string(v)) };
    }
}

#endif
