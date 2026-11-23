#ifndef HWSHQTB__METAOBJECT__META_PROPERTY_H
#define HWSHQTB__METAOBJECT__META_PROPERTY_H

/// @file meta_property_t.h
/// @brief The reflection handle for properties, meta_property_t.

#include "basic_def.h"
#include "meta_type_t.h"
#include "meta_method_t.h"
#include "any_t.h"
#include "export.h"

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace hwshqtb::metaobject {
    class object_t;
    class meta_object_t;
    class meta_info_t;
    class meta_enum_t;

    /// @brief Thrown when an invalid property handle (name not found / not pointing at a legal slot)
    ///        is used anyway.
    /// @note Derives from std::logic_error; the semantics are "the caller skipped the is_valid()
    ///       check".
    class bad_meta_property_t: public std::logic_error {
    public:
        bad_meta_property_t():
            std::logic_error("bad meta property: invalid meta_property_t") { }
    };

    /// @brief The reflection handle of one property: its metadata plus the read/write entry points
    ///        for a concrete object.
    ///
    /// @par Where handles come from
    /// From meta_object_t::property(name) — an object reaches it as meta_object()->property(name),
    /// since object_t has no facade of its own. When the name does not exist an [invalid handle]
    /// (is_valid() == false) is returned and nothing is thrown - consistent with
    /// meta_type_t::resolve_global() answering an invalid handle when the name is not found.
    ///
    /// @par The two sources of accessors
    /// property_a may be attached to a function (a getter) or to a data member (the Qt MEMBER style).
    /// The latter has no method_infos slot, so READING is synthesised by _reader_helper (the member is
    /// evaluated through the object). Writing, resetting and notifying are NOT synthesised: a
    /// data-member property is writable only when its annotation names a writer, and has a reseter /
    /// notifier only when it names one. Therefore write / reset / notifier are expressed with
    /// meta_method_t: named entries expose a name, while a caller without a slot has an empty
    /// signature() (the "no table entry" test).
    ///
    /// @par Accessor inheritance and the run-time fallback
    /// The annotation names an accessor by BARE NAME ("set_value", "value_changed", "reset_value"),
    /// and `build_meta<T>()` resolves that name at compile time by REFLECTING the
    /// declared member functions of the class itself (parameter shape plus reflection type comparison
    /// — see property_a). The scan is limited to the DECLARING class, so two cases stay
    /// unresolved there:
    ///  - an accessor declared by a BASE class (the common case: an inherited NOTIFY),
    ///  - a name that matches nothing at all (a typo, or a function excluded from the table).
    /// For those the entry keeps the bare name plus the property's type name, and writer() /
    /// reseter() / notifier() resolve it LAZILY at run time: the key
    /// `name(type_name)` / `name()` is looked up along the meta-object chain (parents included) with
    /// the stored-signature verification every table query uses, and the handle points at the table
    /// that DECLARES the accessor. A named accessor that still resolves to nothing is reported once
    /// per handle at Critical through the log facade, and the query itself stays noexcept and answers
    /// with an invalid handle (never throws) — see resolve_named_accessor.
    ///
    /// @note A CONSTRUCTOR is never an accessor, and it cannot even be a candidate: constructors
    ///       live in their own table (meta_info_t::constructor_infos), so the method table this fallback
    ///       scans holds methods and signals only. An annotation whose bare name equals the class name
    ///       therefore simply matches nothing and is resolved (and reported) like any other
    ///       unresolved name.
    ///
    /// @par Intrinsic capability
    /// A property is **always readable**, hence there is no readable(); on success read() always
    /// returns an initialised any_t.
    ///
    /// @par Layering contract
    ///  - Query interfaces are noexcept and defensive: an invalid handle yields an empty view /
    ///    false / an invalid handle / an invalid meta_type_t;
    ///  - Operation interfaces throw in situations "the caller could have prevented", rather than
    ///    returning a half-built result:
    ///     - invalid handle                                  -> bad_meta_property_t
    ///     - property type cannot be resolved (a run-time
    ///       built column with no known name)              -> bad_meta_type_t
    ///     - the value to write is uninitialised              -> bad_any_uninitialized_t
    ///     - the value type mismatches and there is no
    ///       usable converter                                 -> bad_any_cast_t
    ///     - the underlying type does not support the copy
    ///       construction / assignment needed for reading     -> bad_any_unsupported_t
    ///     - write on a read-only property / reset without a
    ///       reseter                                          -> returns false (capability missing,
    ///                                                            checkable in advance via the handle)
    ///
    /// @note A handle only refers to the compile-time property table and owns no object; the
    ///       collaborating object is passed by reference, so there is no "null object" branch in the
    ///       interface. The same handle can be used with any instance of the same class.
    /// @sa meta_object_t::property
    /// @sa meta_method_t
    class HWSHQTB_EXPORT meta_property_t {
    public:
        /// @brief Default construction yields an invalid handle.
        meta_property_t() noexcept = default;

        /// @name Identity and metadata
        /// @{
        /// @brief Whether the handle is valid.
        /// @return true when it holds a property table and the slot is legal.
        /// @note noexcept.
        bool is_valid() const noexcept;

        /// @brief Get the property name.
        /// @return The property name for a valid handle; an empty view otherwise.
        /// @note noexcept, no allocation.
        std::string_view name() const noexcept;

        /// @brief Get the normalised name of the property type.
        /// @return The type name for a valid handle; an empty view otherwise.
        /// @note noexcept, no allocation.
        std::string_view type_name() const noexcept;

        /// @brief Resolve the property's type.
        /// @param obj Ignored (the registry is process-wide); kept for source compatibility.
        /// @return The corresponding type; an invalid meta_type_t when the handle is invalid or a
        ///         run-time built column names a type nothing has registered.
        /// @note The table stores the property type's NAME (there is no embedded interface column: a
        ///       reflected table carries no type_interface_t*), so resolving is a registry lookup of
        ///       that spelling. The name is the identity spelling every reflected key uses, so the
        ///       lookup hits on first use for a reflected property; a run-time built column that names
        ///       an unregistered type answers an invalid meta_type_t. Like meta_type_t::from_type\<T\>(),
        ///       it is a defensive query; read() throws bad_meta_type_t in the same situation, because
        ///       that is already an operation meant to produce a value.
        /// @sa meta_type_t::is_valid
        meta_type_t type(const object_t& obj) const;
        /// @}

        /// @name Capabilities and associated methods
        /// @{
        /// @brief Whether the metadata marks the property as designable (for editors / tools).
        /// @note noexcept; false for an invalid handle.
        bool designable() const noexcept;

        /// @brief Whether the property has a writer.
        bool is_writable() const noexcept;

        /// @brief Whether the property has a reset function.
        bool is_resettable() const noexcept;

        /// @brief Whether the property has a change-notification signal.
        /// @note This query also answers Qt's QMetaProperty::isBindable(): bindability is value-derived
        ///       — a property can carry a binding exactly when it has a notifier — so there is no
        ///       separate is_bindable().
        bool is_notifiable() const noexcept;

        /// @brief Whether the property is observably constant (no writer and no notifier).
        bool is_constant() const noexcept;

        /// @brief Whether the property's type names an enumeration (QMetaProperty::isEnumType).
        /// @return true when the type is an enum, or a flag_t\<E\> wrapper around one; false for an
        ///         invalid handle or any other type.
        /// @note noexcept. The two facts stay apart: is_flag_type() tells a plain enum from a wrapper,
        ///       exactly as Qt reports QFlags\<E\> as an enum type AND a flag type.
        bool is_enum_type() const noexcept;

        /// @brief Whether the property's type is a flag_t\<E\> wrapper (QMetaProperty::isFlagType).
        /// @return true for a flag wrapper; false for a plain enum or any other type.
        /// @note noexcept. A wrapper fact only: whether the enum's VALUES are a usable bit set is
        ///       meta_enum_t::is_flag() (the value judgment stays on the enum).
        bool is_flag_type() const noexcept;

        /// @brief The enum this property's type names, if any.
        /// @return The enum handle when the property type is an enum, or a flag_t\<E\> wrapper around
        ///         one; an invalid handle otherwise.
        /// @note Uses the enum name recorded at build time from the real C++ type. A run-time built
        ///       property (meta_object_builder_t) has no such column and falls back to the type-name
        ///       tail parse.
        meta_enum_t enumerator() const;

        /// @brief The traits declared by the property annotation.
        property_trait_t traits() const noexcept;

        /// @brief Whether the property is the class's "user" property.
        bool is_user() const noexcept;

        /// @brief Whether the property must be set before a component is instantiated.
        bool is_required() const noexcept;

        /// @brief Whether the property is code-only (not shown by the front end).
        bool is_hidden() const noexcept;

        /// @brief Whether a derived class may not redeclare this property.
        bool is_final() const noexcept;

        /// @brief Get the writer.
        /// @return An invalid meta_method_t when the property is read-only, or when the annotation's
        ///         writer name resolves to nothing in the meta-object chain; for a data-member
        ///         property the writer is a synthesised accessor (is_valid() is true, signature() is
        ///         empty).
        /// @note noexcept. An inherited writer is resolved lazily here (and the handle then points at
        ///       the base's table). A named-but-unresolved writer emits one Critical per handle and
        ///       role; the query itself still answers with the invalid handle.
        /// @sa write
        meta_method_t writer() const noexcept;

        /// @brief Get the reseter.
        /// @return An invalid meta_method_t when the property cannot be reset, or when the
        ///         annotation's reseter name resolves to nothing.
        /// @note noexcept; inherited reseters are resolved lazily, a named-but-unresolved one logs
        ///       one Critical per handle.
        /// @sa reset
        meta_method_t reseter() const noexcept;

        /// @brief Get the notifier (a signal).
        /// @return An invalid meta_method_t when the property declares no notifier, or when the
        ///         annotation's notifier name resolves to nothing.
        /// @note noexcept. This layer only reports it; invoking goes through the method / signal layer.
        /// @note An inherited notifier is the canonical lazy case (the property's class declares the
        ///       property, a base class declares the signal): it is resolved here, at run time, and
        ///       the handle points at the declaring base's table.
        meta_method_t notifier() const noexcept;
        /// @}

        /// @name Operations
        /// @{
        /// @brief Read the property value (the intrinsic capability of a property).
        /// @param obj Target object.
        /// @return Always an initialised any_t that owns the value.
        /// @throws bad_meta_property_t If the handle is invalid.
        /// @throws bad_meta_type_t If the property type cannot be resolved (a run-time built column
        ///         whose name nothing has registered).
        /// @throws bad_any_unsupported_t If the underlying type does not support the copy
        ///         construction / copy assignment needed for reading.
        any_t read(const object_t& obj) const;

        /// @brief Write the property value.
        /// @param obj Target object.
        /// @param value The value to write.
        /// @return true on success; false when the property is read-only (writer() is invalid).
        /// @throws bad_meta_property_t If the handle is invalid.
        /// @throws bad_meta_type_t If the property type cannot be resolved.
        /// @throws bad_any_uninitialized_t If @p value is uninitialised.
        /// @throws bad_any_cast_t If @p value has the wrong type and no usable converter exists.
        bool write(object_t& obj, const any_t& value) const;

        /// @brief Restore the property to its default value.
        /// @param obj Target object.
        /// @return true on success; false when the property has no reseter.
        /// @throws bad_meta_property_t If the handle is invalid.
        /// @throws bad_meta_type_t If the property type cannot be resolved.
        bool reset(object_t& obj) const;
        /// @}

        /// @name Operations on a value-type object (GADGET)
        /// @{
        // A GADGET is not an object_t: it has no tree, so the object_t overloads above cannot accept
        // it. These overloads resolve the property type through the ONE process-wide registry
        // (resolved_type(): the identity name column, looked up by name) and run the very same
        // reader / writer / reseter thunk, which is a plain function pointer and has no object_t
        // dependency. An object_t argument still selects the object_t overloads above, so both entry
        // points share one resolution rule.

        /// @brief Read the property of a value-type object (GADGET).
        /// @tparam T The value type; must not derive from object_t.
        /// @param obj Target object.
        /// @return Always an initialised any_t that owns the value.
        /// @throws bad_meta_property_t If the handle is invalid.
        /// @throws bad_meta_type_t If the property type cannot be resolved (undeclared and unknown).
        /// @throws bad_any_unsupported_t If the underlying type does not support the construction /
        ///         copy operations needed for reading.
        template <typename T>
            requires(!std::is_base_of_v<object_t, T> && !std::is_pointer_v<std::remove_cvref_t<T>>)
        any_t read(const T& obj) const {
            return read_value(std::addressof(obj), resolved_type());
        }

        /// @brief Write the property of a value-type object (GADGET).
        /// @tparam T The value type; must not derive from object_t.
        /// @param obj Target object.
        /// @param value The value to write.
        /// @return true on success; false when the property is read-only (writer() is invalid).
        /// @throws bad_meta_property_t If the handle is invalid.
        /// @throws bad_meta_type_t If the property type cannot be resolved.
        /// @throws bad_any_uninitialized_t If @p value is uninitialised.
        /// @throws bad_any_cast_t If @p value has the wrong type and no usable converter exists.
        template <typename T>
            requires(!std::is_base_of_v<object_t, T> && !std::is_pointer_v<std::remove_cvref_t<T>>)
        bool write(T& obj, const any_t& value) const {
            return write_value(std::addressof(obj), resolved_type(), value);
        }

        /// @brief Restore the property of a value-type object (GADGET) to its default value.
        /// @tparam T The value type; must not derive from object_t.
        /// @param obj Target object.
        /// @return true on success; false when the property has no reseter.
        /// @throws bad_meta_property_t If the handle is invalid.
        /// @throws bad_meta_type_t If the property type cannot be resolved.
        template <typename T>
            requires(!std::is_base_of_v<object_t, T> && !std::is_pointer_v<std::remove_cvref_t<T>>)
        bool reset(T& obj) const {
            return reset_value(std::addressof(obj), resolved_type());
        }
        /// @}

    private:
        friend class object_t;
        friend class meta_object_t;

        /// @brief The property row of the declaring meta-object, or nullptr for an invalid handle.
        /// @return The row this handle points at; every metadata query reads one of its columns.
        /// @note Everything table-related goes through meta_object_t::property_entry(): this handle
        ///       never touches a meta_info_t directly.
        const detail::property_info_t* entry() const noexcept;

        /// @brief The resolved handle of the property's type.
        /// @return The handle of the table's embedded interface (produced on demand from the
        ///         property type's reflection; its first use registers the identity name), or of the
        ///         stored name when the type has no embedded interface (a run-time built table); an
        ///         invalid handle for a property whose type cannot be resolved.
        /// @note noexcept. Shared by type() and the operations, so every entry point resolves a
        ///       property type the same way.
        meta_type_t resolved_type() const noexcept;

        /// @brief Read through the property's reader into freshly allocated storage.
        /// @param obj The target object, type-erased.
        /// @param type The property type resolved in the caller's registry (a tree's registry for
        ///        the object_t overload, the global one for a GADGET).
        /// @return Always an initialised any_t that owns the value.
        /// @note The shared body of both read() overloads: only the way the type is resolved differs.
        any_t read_value(const void* obj, const meta_type_t& type) const;

        /// @brief Write through the property's writer.
        /// @param obj The target object, type-erased.
        /// @param type The property type resolved in the caller's registry.
        /// @param value The value to write.
        /// @return true on success; false when the property is read-only.
        /// @note The shared body of both write() overloads.
        bool write_value(void* obj, const meta_type_t& type, const any_t& value) const;

        /// @brief Restore the property through its reseter.
        /// @param obj The target object, type-erased.
        /// @param type The property type resolved in the caller's registry.
        /// @return true on success; false when the property has no reseter.
        /// @note The shared body of both reset() overloads.
        bool reset_value(void* obj, const meta_type_t& type) const;

        /// @brief Construct a valid handle from the declaring meta-object and a local slot.
        /// @param owner The meta-object that declares the property.
        /// @param index The slot inside that meta-object's property_infos.
        meta_property_t(const meta_object_t* owner, index_t index) noexcept;

        /// @brief Resolve a bare accessor name at run time (the fallback for inherited or unbound names).
        /// @param name The bare accessor name recorded by the annotation.
        /// @param arities The shapes to try, in order — the compile-time sequence of this role
        ///        (pick_overload in meta_info_t.h): the writer tries `{ 1 }` (the property-type form),
        ///        the reseter `{ 0 }` (the no-argument form is its only shape), the notifier `{ 1, 0 }`
        ///        (one-argument form first, no-argument form as the fallback).
        /// @param role_bit The miss-memo bit of this role (see _miss_logged).
        /// @param role The role name used in the diagnostic ("writer", "reseter", "notifier").
        /// @return The handle of the resolved accessor; an invalid handle when the name resolves to
        ///         nothing in the chain, or when the level that would answer holds several candidates
        ///         (an ambiguous bare name — see below).
        /// @note noexcept. On a miss OR an ambiguity this logs ONE Critical per handle and role: the
        ///       handle memoises the outcome in _miss_logged, so asking is_writable() / writer() /
        ///       write() through the same handle reports a single diagnostic instead of flooding the
        ///       log. Constructing a fresh handle (property(name)) starts a fresh attempt.
        /// @note A bare name is not a signature, so no signature text is fabricated here: the table
        ///       key ends in the member's cvref suffix ("base_signal(int) const"), so a hand-built
        ///       "name(type)" key would miss every const accessor. The candidates are matched by
        ///       NAME + SHAPE instead — arity, plus the property's type for the one-argument roles —
        ///       walking the chain from the declaring class to the root. Signature lookups stay
        ///       exact; this is a name lookup.
        /// @note Ambiguity has the same rule as the compile-time pass: within the scope that would
        ///       answer, several candidates at the same level (exact type first, bare type second)
        ///       cannot be distinguished by a bare name, so no arbitrary entry is picked. The first
        ///       scope with a unique candidate still wins; an ambiguity in a scope is only reported
        ///       when no more-derived scope answered.
        meta_method_t resolve_named_accessor(const detail::compiling_string_t& name,
                                              std::initializer_list<std::uint8_t> arities,
                                              std::uint8_t role_bit, const char* role) const noexcept;

        /// @brief The meta-object that declares the property; nullptr means an invalid handle.
        /// @note The DECLARING one, not the most derived: a handle for an inherited property points at
        ///       the base class's meta-object, which is what makes the metadata columns exact and lets
        ///       the accessor walk start at the class that actually wrote the annotation.
        const meta_object_t* _owner = nullptr;

        /// @brief The slot inside property_infos; index_npos means an invalid handle.
        index_t _index = index_npos;

        /// @brief Which accessor roles have already emitted their "named but unresolved or ambiguous"
        ///        Critical.
        /// @note mutable: writer() / reseter() / notifier() are const and noexcept by contract, and
        ///       the memo is exactly what keeps a repeated query from flooding the log. A copied
        ///       handle carries the memo along; a handle built from scratch starts clean.
        mutable std::uint8_t _miss_logged = 0;
    };
}

#endif
