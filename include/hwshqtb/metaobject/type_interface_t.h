#ifndef HWSHQTB__METAOBJECT__TYPE_INTERFACE_H
#define HWSHQTB__METAOBJECT__TYPE_INTERFACE_H

/// @file type_interface_t.h
/// @brief The per-type capability interface (the QMetaTypeInterface analogue).
///
/// @par The model
/// A type's `type_interface_t` is produced ON DEMAND, at compile time, wherever the type is
/// statically known: `detail::interface_of<T>` is one constant-initialised object per module holding
/// the type's identity name, size / alignment, flags and the captureless thunks (default / copy /
/// move constructor, destructor, assignments, comparisons, serialisation, debug rendering). The
/// builder is `consteval`, so there is no static-initialisation order risk and no registration
/// ceremony.
///
/// @par Identity
/// The public name is the namer's ORIGINAL spelling (canonical_name.hpp's original_name_of: the same
/// walk as the canonical spelling, with implementation-detail namespaces KEPT, e.g.
/// `std::__cxx11::basic_string<char>`), copied into static storage by `std::define_static_string` —
/// never `display_string_of()` output and never a stringized declaration spelling. That is the identity
/// spelling, and it is what makes the interface and the reflection-built meta tables agree BY
/// CONSTRUCTION: both run the same namer over the same type. The CANONICAL spelling
/// (meta_info_t::canonical_name<T>(), detail namespaces elided) stays the user-facing one and is bound
/// automatically as an ALIAS by meta_type_t::register_type<T>() and meta_type_t::from_type<T>(), so
/// by-name lookup accepts either spelling. Ids are assigned at RUN TIME by the one process-wide
/// registry (src/type_registry.cpp, inside the library): 0 means "not assigned yet", ids start at 1,
/// and a second module's copy of the same interface receives the id already assigned to that name.
/// Handles are therefore NEVER compared by pointer — two PE modules hold two copies of the interface
/// — but by id, with the name as the tie-break while either id is still 0 (see detail::same_type).
///
/// @par Using a type
/// Any type can be used as a meta_type_t (meta_type_t::from_type<T>()), stored in an
/// any_t, and used as a conversion endpoint (register_conversion<From, To>()). There is no
/// declaration and no registration call: producing the interface IS the registration, and the one
/// registry assigns the id (and records the identity name) on the first handle-creating use.
///
/// @par The two lookups
///  - by type: meta_type_t::from_type<T>() — compile-time interface, no name round-trip;
///  - by name: meta_type_t::resolve_global(name) asks the registry. A name becomes
///    findable when an interface carrying it is first used (from_type / from_interface /
///    type_interface_t::registered), which is what the reflected tables do when they resolve a type
///    column through their embedded interface. A type nothing has touched yet is not in the name table.
///
/// The one exception is the builtin set: detail::register_default_types() puts the primitives and the
/// standard-library typedef aliases ("std::string", "std::string_view", "std::wstring") in the table
/// when the module is loaded, so they resolve before any core_application exists and stay registered
/// for the whole process.
///
/// @par Module lifetime
/// The interface OBJECT lives in the module that instantiated it: the variable template is
/// unexported (exporting it would force dllimport and break the constant expression), so every
/// module keeps its own constant-initialised copy. A meta_type_t handle points at one of those
/// copies, and an any_t stores such a handle. Consequence: a value whose type's interface lives in a
/// plugin DLL must not outlive that DLL — the same constraint Qt's QMetaType has. The registry
/// keeps the identity NAME and the id by value, so the id space itself survives an unload; only
/// handles that still point into the unloaded image become invalid.
///
/// @par Name convergence
/// The name is the namer's spelling — `short` rather than `short int`, no space after a
/// template-argument comma, implementation-detail scopes KEPT (`std::__cxx11::basic_string<char>`
/// rather than `std::basic_string<char>`) — and it is shared by every path that names a type (the
/// tables' columns, meta_info_t::registered_type_name<T>(), the interface itself). The canonical
/// (detail-namespace-free) spelling is bound as a registry alias on template registration, so both
/// spellings resolve by name.

#include "basic_def.h"
#include "canonical_name.hpp"
#include "export.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace hwshqtb::metaobject {
    /// @brief The ABI revision of type_interface_t (a consumer may compare it against its own).
    inline constexpr std::uint16_t type_interface_revision = 1;

    /// @brief The conversion caller: construct a To from a From into unconstructed storage.
    /// @param dst Unconstructed storage of the target type.
    /// @param src A constructed object of the source type.
    /// @return true on success (dst constructed); false leaves dst unconstructed.
    using metatype_conversion = bool (*)(void* dst, const void* src);

    /// @brief One type's capability table: identity, layout, flags and thunks.
    ///
    /// @par Calling convention
    /// Every thunk takes the interface as its FIRST argument and is a plain capture-less function
    /// pointer, so the same thunk works for every module's copy of the interface and no per-object
    /// state is needed.
    ///
    /// @par Mutability
    /// The object itself is declared `const` in every module (it lives in read-only data), but `id`
    /// is `mutable` and atomic: the registry writes the runtime-assigned id into it, exactly like
    /// Qt's QMTI_MUTABLE typeId. Reads use relaxed ordering; the registry owns the synchronisation.
    ///
    /// @note There is deliberately NO allocator: `allocate()` derives from size / alignment
    ///       (`operator new(size, align_val_t)` when the alignment exceeds the default one), and
    ///       `deallocate()` mirrors it — Qt's QMetaType does the same.
    struct type_interface_t {
        /// @brief Default-construct on unconstructed storage.
        using default_ctor_caller = void (*)(const type_interface_t*, void*);
        /// @brief Copy-construct on unconstructed storage.
        using copy_ctor_caller = void (*)(const type_interface_t*, void*, const void*);
        /// @brief Move-construct on unconstructed storage.
        using move_ctor_caller = void (*)(const type_interface_t*, void*, void*);
        /// @brief Destroy a constructed object.
        using dtor_caller = void (*)(const type_interface_t*, void*);
        /// @brief Copy-assign onto a constructed object.
        using copy_assign_caller = void (*)(const type_interface_t*, void*, const void*);
        /// @brief Move-assign onto a constructed object.
        using move_assign_caller = void (*)(const type_interface_t*, void*, void*);
        /// @brief Equality comparison.
        using equals_caller = bool (*)(const type_interface_t*, const void*, const void*);
        /// @brief Less-than comparison.
        using less_caller = bool (*)(const type_interface_t*, const void*, const void*);
        /// @brief Serialisation: constructed object -> text; nullopt means "unsupported".
        using serialize_caller = std::optional<std::string> (*)(const type_interface_t*, const void*);
        /// @brief Deserialisation: text -> unconstructed storage; returns success.
        using deserialize_caller = bool (*)(const type_interface_t*, void*, std::string_view);
        /// @brief Debug rendering: constructed object -> readable text.
        using debug_string_caller = std::string (*)(const type_interface_t*, const void*);

        /// @brief The ABI revision this interface was built with.
        std::uint16_t revision = type_interface_revision;
        /// @brief alignof(T) (1 for void).
        std::uint16_t alignment = 1;
        /// @brief sizeof(T) (0 for void).
        std::uint32_t size = 0;
        /// @brief The runtime-assigned type id; 0 until the registry assigns one, never 0 afterwards.
        mutable std::atomic<index_t> id{ 0 };
        /// @brief Static properties of the type (type_tag_flag combination).
        type_tag_t flags = type_tag_flag::NONE;
        /// @brief The identity name: the declared spelling `#T` (never display_string_of output).
        const char* name = nullptr;

        /// @brief Default construction; nullptr means "not default-constructible".
        default_ctor_caller default_ctor = nullptr;
        /// @brief Copy construction; nullptr means "not copy-constructible".
        copy_ctor_caller copy_ctor = nullptr;
        /// @brief Move construction; nullptr means "not move-constructible".
        move_ctor_caller move_ctor = nullptr;
        /// @brief Destruction; nullptr means "not destructible".
        dtor_caller dtor = nullptr;
        /// @brief Copy assignment; nullptr means "not copy-assignable".
        copy_assign_caller copy_assign = nullptr;
        /// @brief Move assignment; nullptr means "not move-assignable".
        move_assign_caller move_assign = nullptr;
        /// @brief Equality comparison; nullptr means "unsupported".
        equals_caller equals = nullptr;
        /// @brief Less-than comparison; nullptr means "unsupported".
        less_caller less = nullptr;
        /// @brief Serialisation; nullptr means "unsupported".
        serialize_caller serialize = nullptr;
        /// @brief Deserialisation; nullptr means "unsupported".
        deserialize_caller deserialize = nullptr;
        /// @brief Debug rendering; nullptr means "use <name>".
        debug_string_caller debug_string = nullptr;

        /// @name On-demand interface production
        /// @{
        /// @brief The interface of @p T (no registration; usable in constant expressions).
        /// @tparam T The type; cv, references and arrays are removed (std::decay_t).
        /// @return A pointer to the module's constant-initialised interface object. Never nullptr.
        /// @note This is the one production point of the type system: there is no declaration to make
        ///       and no registration call to write.
        /// @note Declared here and defined after detail::interface_of: the interface object is a
        ///       variable template, so an in-class body cannot name it yet.
        template <typename T>
        static constexpr const type_interface_t* declared() noexcept;

        /// @brief The interface of @p T, registered (name + id) on first use.
        /// @tparam T The type; cv, references and arrays are removed (std::decay_t).
        /// @return The interface with its runtime id assigned (or 0 while the registry is unavailable).
        /// @note The registration is a one-time, lock-protected registry operation; every later call is
        ///       a relaxed atomic load. Unexported templates mean each module keeps its own interface
        ///       object; the registry makes them share one id by name.
        template <typename T>
        static inline const type_interface_t* registered() noexcept;
        /// @}
    };

    namespace detail {
        /// @brief Whether the scalar type T is signed.
        /// @note For an enum the answer comes from reflection over its underlying type
        ///       (`std::meta::is_signed_type`), which reproduces
        ///       `std::is_signed_v<std::underlying_type_t<T>>` without `\<type_traits\>`; a flag_t
        ///       asks the same about the enum it wraps. Non-scalars are unsigned in the sense of the
        ///       structural conversion (the flag is never set for them).
        template <typename T>
        consteval bool is_signed_scalar() {
            if constexpr (std::is_enum_v<T>) {
                return std::meta::is_signed_type(std::meta::underlying_type(^^T));
            }
            else if constexpr (is_flag_v<T>) {
                return std::meta::is_signed_type(std::meta::underlying_type(^^enum_of_flag_t<T>));
            }
            else if constexpr (std::is_integral_v<T>) {
                return std::meta::is_signed_type(^^T);
            }
            else {
                return false;
            }
        }

        /// @brief The type_tag_flag combination of T.
        template <typename T>
        consteval type_tag_t type_flags_of() {
            type_tag_t flags = type_tag_flag::NONE;
            if constexpr (std::is_pointer_v<T>) {
                flags = flags | type_tag_flag::IS_POINTER;
            }
            if constexpr (std::is_enum_v<T>) {
                flags = flags | type_tag_flag::IS_ENUM;
            }
            else if constexpr (is_flag_v<T>) {
                flags = flags | type_tag_flag::IS_FLAGS;
            }
            else if constexpr (std::is_integral_v<T>) {
                flags = flags | type_tag_flag::IS_INTEGER;
            }
            if constexpr (std::is_trivially_copyable_v<T>) {
                flags = flags | type_tag_flag::IS_TRIVIALLY_COPYABLE;
            }
            if constexpr (is_signed_scalar<T>()) {
                flags = flags | type_tag_flag::IS_SIGNED;
            }
            if constexpr (std::is_nothrow_move_constructible_v<T>) {
                flags = flags | type_tag_flag::IS_NOEXCEPT_MOVE_CONSTRUCTIBLE;
            }
            if constexpr (std::is_nothrow_move_assignable_v<T>) {
                flags = flags | type_tag_flag::IS_NOEXCEPT_MOVE_ASSIGNABLE;
            }
            return flags;
        }

        // The thunk builders: each answers nullptr when the operation is unavailable, so the
        // interface doubles as its own capability table.

        template <typename T>
        consteval auto default_ctor_of() -> typename type_interface_t::default_ctor_caller {
            if constexpr (std::is_default_constructible_v<T>) {
                return +[](const type_interface_t*, void* mem) {
                    ::new (mem) T();
                };
            }
            else {
                return nullptr;
            }
        }

        template <typename T>
        consteval auto copy_ctor_of() -> typename type_interface_t::copy_ctor_caller {
            if constexpr (std::is_copy_constructible_v<T>) {
                return +[](const type_interface_t*, void* mem, const void* other) {
                    ::new (mem) T(*static_cast<const T*>(other));
                };
            }
            else {
                return nullptr;
            }
        }

        template <typename T>
        consteval auto move_ctor_of() -> typename type_interface_t::move_ctor_caller {
            if constexpr (std::is_move_constructible_v<T>) {
                return +[](const type_interface_t*, void* mem, void* other) {
                    ::new (mem) T(std::move(*static_cast<T*>(other)));
                };
            }
            else {
                return nullptr;
            }
        }

        template <typename T>
        consteval auto dtor_of() -> typename type_interface_t::dtor_caller {
            if constexpr (std::is_destructible_v<T>) {
                return +[](const type_interface_t*, void* mem) {
                    static_cast<T*>(mem)->~T();
                };
            }
            else {
                return nullptr;
            }
        }

        template <typename T>
        consteval auto copy_assign_of() -> typename type_interface_t::copy_assign_caller {
            if constexpr (std::is_copy_assignable_v<T>) {
                return +[](const type_interface_t*, void* dst, const void* src) {
                    *static_cast<T*>(dst) = *static_cast<const T*>(src);
                };
            }
            else {
                return nullptr;
            }
        }

        template <typename T>
        consteval auto move_assign_of() -> typename type_interface_t::move_assign_caller {
            if constexpr (std::is_move_assignable_v<T>) {
                return +[](const type_interface_t*, void* dst, void* src) {
                    *static_cast<T*>(dst) = std::move(*static_cast<T*>(src));
                };
            }
            else {
                return nullptr;
            }
        }

        template <typename T>
        consteval auto equals_of() -> typename type_interface_t::equals_caller {
            if constexpr (requires(const T& a, const T& b) { { a == b } -> std::convertible_to<bool>; }) {
                return +[](const type_interface_t*, const void* lhs, const void* rhs) -> bool {
                    return *static_cast<const T*>(lhs) == *static_cast<const T*>(rhs);
                };
            }
            else {
                return nullptr;
            }
        }

        template <typename T>
        consteval auto less_of() -> typename type_interface_t::less_caller {
            if constexpr (requires(const T& a, const T& b) { { a < b } -> std::convertible_to<bool>; }) {
                return +[](const type_interface_t*, const void* lhs, const void* rhs) -> bool {
                    return *static_cast<const T*>(lhs) < *static_cast<const T*>(rhs);
                };
            }
            else {
                return nullptr;
            }
        }

        template <typename T>
        consteval auto debug_string_of() -> typename type_interface_t::debug_string_caller {
            if constexpr (std::is_same_v<T, bool>) {
                return +[](const type_interface_t*, const void* value) -> std::string {
                    return *static_cast<const bool*>(value) ? "true" : "false";
                };
            }
            else if constexpr (std::is_arithmetic_v<T>) {
                return +[](const type_interface_t*, const void* value) -> std::string {
                    return std::to_string(*static_cast<const T*>(value));
                };
            }
            else if constexpr (std::is_same_v<T, std::string>) {
                return +[](const type_interface_t*, const void* value) -> std::string {
                    return *static_cast<const std::string*>(value);
                };
            }
            else if constexpr (std::is_same_v<T, std::string_view>) {
                return +[](const type_interface_t*, const void* value) -> std::string {
                    return std::string(*static_cast<const std::string_view*>(value));
                };
            }
            else {
                return nullptr;
            }
        }

        /// @brief Build the interface of T with the identity name @p name.
        /// @param name The `#T` spelling (constant static storage).
        /// @return A fully initialised constant-expression interface value.
        /// @note `void` is special-cased (its own overload below): `sizeof(void)` is ill-formed and
        ///       `std::meta::size_of(^^void)` throws. It carries no thunks and size 0 — the handle's
        ///       "declared only" type.
        template <typename T>
        consteval type_interface_t make_type_interface(const char* name) {
            return type_interface_t{
                .revision = type_interface_revision,
                .alignment = static_cast<std::uint16_t>(alignof(T)),
                .size = static_cast<std::uint32_t>(sizeof(T)),
                .id = 0,
                .flags = type_flags_of<T>(),
                .name = name,
                .default_ctor = default_ctor_of<T>(),
                .copy_ctor = copy_ctor_of<T>(),
                .move_ctor = move_ctor_of<T>(),
                .dtor = dtor_of<T>(),
                .copy_assign = copy_assign_of<T>(),
                .move_assign = move_assign_of<T>(),
                .equals = equals_of<T>(),
                .less = less_of<T>(),
                .serialize = nullptr,
                .deserialize = nullptr,
                .debug_string = debug_string_of<T>(),
            };
        }

        /// @brief The void interface: no size, no thunks, no flags.
        template <>
        consteval type_interface_t make_type_interface<void>(const char* name) {
            return type_interface_t{
                .revision = type_interface_revision,
                .alignment = 1,
                .size = 0,
                .id = 0,
                .flags = type_tag_flag::NONE,
                .name = name,
                .default_ctor = nullptr,
                .copy_ctor = nullptr,
                .move_ctor = nullptr,
                .dtor = nullptr,
                .copy_assign = nullptr,
                .move_assign = nullptr,
                .equals = nullptr,
                .less = nullptr,
                .serialize = nullptr,
                .deserialize = nullptr,
                .debug_string = nullptr,
            };
        }

        /// @brief The identity name of an interface ("" for nullptr / a null name).
        /// @note constexpr: the reflected table builders compare it while building at compile time.
        constexpr std::string_view interface_name(const type_interface_t* iface) noexcept {
            return (iface != nullptr && iface->name != nullptr) ? std::string_view(iface->name) : std::string_view{};
        }

        /// @brief The id of an interface (index_npos for nullptr; 0 while unassigned).
        constexpr index_t interface_id(const type_interface_t* iface) noexcept {
            return (iface != nullptr) ? iface->id.load(std::memory_order_relaxed) : index_npos;
        }

        /// @brief Whether two interfaces identify the same type.
        /// @param lhs One interface; nullptr is "no type".
        /// @param rhs The other interface.
        /// @return true when they are the same object, or carry the same non-zero id, or (while an id
        ///         is still 0) the same identity name.
        /// @note Handles are NEVER compared by pointer alone: each PE module holds its own copy of an
        ///       interface, so the identity is the id — with the name as the fallback while the
        ///       registry has not assigned one yet.
        HWSHQTB_EXPORT bool same_type(const type_interface_t* lhs, const type_interface_t* rhs) noexcept;

        // ---- the process-wide registry (src/type_registry.cpp) ----

        /// @brief Register @p iface with the one global registry and return its id.
        /// @param iface The interface to register (must outlive the process).
        /// @return The runtime id (>= 1); 0 only if the registry could not allocate.
        /// @note noexcept. First use assigns the id; a later module's copy of the same NAME receives
        ///       the id already assigned (cross-module dedup, Qt's registerCustomType).
        HWSHQTB_EXPORT index_t ensure_registered(const type_interface_t* iface) noexcept;

        /// @brief Bind @p alias to the primary name of @p iface.
        /// @param iface The interface whose primary name the alias should answer to.
        /// @param alias The extra name; empty binds nothing.
        /// @return false when the alias already names a DIFFERENT type (one Warning then), or when the
        ///         inputs are empty/null; true otherwise (including a repeated binding of the same pair
        ///         and an alias that equals the type's own primary name).
        /// @note The alias table stores the PRIMARY NAME, not the interface pointer, so the entry
        ///       survives the module that registered the type — name_entry_t's reasoning.
        /// @note A primary name always wins: an alias may not collide with one, and interface_by_name
        ///       checks the primary table first.
        HWSHQTB_EXPORT bool bind_alias(const type_interface_t* iface, std::string_view alias) noexcept;

        /// @brief The CANONICAL spelling of @p T, in static storage: the name bound as an alias.
        /// @tparam T The type to name; cv, references and arrays are removed (std::decay_t), exactly
        ///         like the interface's own name.
        /// @return A view of detail::canonical_name_of's output (implementation-detail namespaces
        ///         elided), copied into static storage by `std::define_static_string` so it outlives
        ///         the call.
        /// @note INTERNAL: paired with bind_canonical_alias<T>(); the canonical counterpart of the
        ///       interface name, produced by the SAME namer, so the two can never disagree.
        template <typename T>
        consteval std::string_view canonical_alias_name() {
            return std::define_static_string(
                canonical_name_of(std::meta::dealias(^^std::decay_t<T>)));
        }

        /// @brief Bind the canonical spelling of @p T as a second name of @p iface (no-op when equal).
        /// @tparam T The type whose interface is being registered.
        /// @param iface The interface that was just registered; null is ignored.
        /// @return true when the alias is bound (or already bound to this primary name); false when
        ///         another type owns the name (one Warning, logged by bind_alias).
        /// @note The interface's identity name is the ORIGINAL spelling, so the user-facing canonical
        ///       spelling has to be bound for resolve_global("std::basic_string<char>") to keep
        ///       working. bind_alias treats "alias equals the primary name" as a no-op success, which
        ///       is exactly what happens for the types whose two spellings agree (the common case).
        /// @note INTERNAL and factored on purpose: meta_type_t::register_type<T>() and
        ///       meta_type_t::from_type<T>() are the two places where both spellings of the same @p T
        ///       are available, and they must bind the alias identically. No public method is added.
        template <typename T>
        inline bool bind_canonical_alias(const type_interface_t* iface) noexcept {
            return bind_alias(iface, canonical_alias_name<T>());
        }

        /// @brief Look an interface up by its identity name or a registered alias.
        /// @param name The identity (ORIGINAL) name, or an alias bound by bind_alias() — including the
        ///        canonical spelling registration binds automatically.
        /// @return The interface (id assigned); nullptr when no such name is known.
        /// @note There is no eager registration for a USER type: a name is in the table once an
        ///       interface carrying it has been used (ensure_registered / from_interface / from_type).
        ///       The builtin set is the exception — register_default_types() puts it there at load
        ///       time, before any core_application exists.
        /// @note The primary table is consulted first, so a primary name always shadows an alias.
        HWSHQTB_EXPORT const type_interface_t* interface_by_name(std::string_view name) noexcept;

        /// @brief Register a conversion caller for (@p from, @p to).
        /// @return true when it was added; false when it already existed (or an interface is null).
        HWSHQTB_EXPORT bool add_conversion(const type_interface_t* from, const type_interface_t* to,
                                           metatype_conversion caller) noexcept;

        /// @brief Find the conversion caller registered for (@p from, @p to).
        /// @return The caller; nullptr when none is registered.
        HWSHQTB_EXPORT metatype_conversion find_conversion(const type_interface_t* from,
                                                           const type_interface_t* to) noexcept;

        /// @brief Register the library's builtin type NAMES — the 24 primitives and the
        ///        standard-library typedef aliases — in the one process-wide registry.
        /// @note The registry's own start-up hook, not a public registration API: type_registry.cpp
        ///       holds a namespace-scope object whose constructor calls it, so the names are in the
        ///       table once the module is loaded. That is why resolve_global("std::string") /
        ///       resolve_global("int") answer WITHOUT a core_application — the same property
        ///       QMetaType::fromName("QString") has without a QCoreApplication.
        /// @note core_application's constructor calls it again; harmless, because every step is
        ///       idempotent (an already-assigned id is an atomic load, a repeated alias binding is a
        ///       no-op success).
        /// @note The ORDER is fixed (void first, the standard-library aliases last): ids are handed
        ///       out on first registration, so keeping the order keeps each builtin's id stable.
        /// @note noexcept: it runs from a static initialiser, where a throw would terminate.
        HWSHQTB_EXPORT void register_default_types() noexcept;
    }

    namespace detail {
        /// @brief The interface of @p T, in static storage: the on-demand production of one type's
        ///        capability table.
        /// @tparam T The type whose interface is wanted (cv-qualifiers / references / arrays are
        ///         removed by `std::decay_t` first, so `const std::string&` and `std::string` share
        ///         ONE interface — and one id — exactly like Qt's QMetaType).
        /// @note The identity name is the ORIGINAL namer's output (`detail::original_name_of`,
        ///       canonical_name.hpp: implementation-detail namespaces KEPT, e.g.
        ///       `std::__cxx11::basic_string<char>`), copied into static storage by
        ///       `std::define_static_string`. The reflection-built meta tables spell their type columns
        ///       with the SAME namer, so an interface and the column that refers to it agree
        ///       byte-for-byte by construction.
        /// @note The canonical (detail-namespace-free) spelling is bound as an alias by
        ///       meta_type_t::register_type<T>() / from_type<T>() — this variable template itself
        ///       registers nothing, so it must stay usable in a constant expression.
        /// @note A VARIABLE TEMPLATE, not a function-local static: `type_interface_t::declared<T>()`
        ///       must be usable in a CONSTANT EXPRESSION — `meta_type_t::from_type<T>()` and
        ///       `meta_type_t::object_pointer_type()` are constexpr, and the tables are built at
        ///       compile time — and the address of a function-local static is not a constant
        ///       expression. An inline variable template has a stable address per linked image (vague
        ///       linkage, hidden with everything else in the library), which is the "stable address
        ///       per module" the handles rely on.
        /// @note Constant-initialised (the builder is `consteval`): usable during static
        ///       initialisation. Only its mutable atomic id is ever written, by the registry, on
        ///       first use.
        template <typename T>
        inline constinit const type_interface_t interface_of =
            make_type_interface<std::decay_t<T>>(
                std::define_static_string(original_name_of(std::meta::dealias(^^std::decay_t<T>))));
    }

    template <typename T>
    constexpr const type_interface_t* type_interface_t::declared() noexcept {
        return &detail::interface_of<std::decay_t<T>>;
    }

    template <typename T>
    inline const type_interface_t* type_interface_t::registered() noexcept {
        const type_interface_t* iface = declared<T>();
        (void)detail::ensure_registered(iface);
        return iface;
    }

    /// @brief Register a From -> To converter in the one process-wide conversion map.
    /// @tparam From Source type; no declaration is needed.
    /// @tparam To Target type; must be constructible from @c const @c From&.
    /// @return true on success; false if the conversion already exists or the target is not
    ///         constructible.
    /// @note A free function, not a member of object_t: no object instance takes part — the only
    ///       thing it touches is the process-wide conversion table.
    /// @note A type itself needs no registration (its interface is produced on demand); only
    ///       CONVERSIONS are registered, because they are user-supplied behaviour rather than a
    ///       fact about the type.
    /// @note The converter constructs the target object on uninitialised storage; semantics
    ///       match meta_type_t::convert().
    /// @sa meta_type_t::has_conversion_to
    template <typename From, typename To>
    bool register_conversion() {
        if constexpr (std::is_same_v<std::decay_t<From>, std::decay_t<To>>) {
            return true;  // the identity conversion needs no registration
        }
        else if constexpr (std::is_constructible_v<To, const From&>) {
            return detail::add_conversion(type_interface_t::registered<From>(),
                type_interface_t::registered<To>(),
                [](void* dst, const void* src) -> bool {
                    new (dst) To(*static_cast<const From*>(src));
                    return true;
                });
        }
        else {
            // To cannot be constructed from const From& by the language itself, so there is
            // nothing to register: the branch is discarded instead of failing the build.
            // Integer <-> enum / flags does not go through this entry point at all: it is the
            // structural identity of meta_type_t::convert(), which needs no registration.
            return false;
        }
    }

    /// @brief Same as above, but with a custom conversion function.
    /// @tparam From Source type.
    /// @tparam To Target type.
    /// @param converter A conversion function of the form @c To(const @c From&).
    /// @return true on success; false if a type is not declared, the converter is null, or the
    ///         conversion already exists.
    /// @note A free function for the same reason as the overload above: it registers into the
    ///       process-wide table and reads no object state.
    /// @note The converter pointer lives in a static slot dedicated to this @c (From, To) pair
    ///       and is read by a capture-less thunk, so it can decay into metatype_conversion without
    ///       capturing a run-time function pointer.
    /// @sa meta_type_t::has_conversion_to
    template <typename From, typename To>
    bool register_conversion(To (*converter)(const From&)) {
        if (converter == nullptr) {
            return false;
        }
        // Static slot dedicated to this (From, To) pair: no capture, so the thunk decays into
        // metatype_conversion (a plain function pointer).
        static To (*slot)(const From&) = nullptr;
        metatype_conversion thunk = [](void* dst, const void* src) -> bool {
            new (dst) To(slot(*static_cast<const From*>(src)));
            return true;
        };
        if (!detail::add_conversion(type_interface_t::registered<From>(),
                type_interface_t::registered<To>(), thunk)) {
            return false;  // already exists: leave the slot alone so live thunks keep working
        }
        slot = converter;
        return true;
    }
}

#endif
