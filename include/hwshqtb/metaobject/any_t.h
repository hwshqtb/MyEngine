#ifndef HWSHQTB__METAOBJECT__ANY_H
#define HWSHQTB__METAOBJECT__ANY_H

/// @file any_t.h
/// @brief The type-erased "variable" abstraction any_t and its failure channel, the bad_any_t family.

#include "basic_def.h"
#include "meta_type_t.h"
#include "export.h"

#include <concepts>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace hwshqtb::metaobject {
    class any_t;
    class object_t;
    class meta_call_event_t;
    template <typename T>
    class object_pointer_t;

    namespace detail {
        /// @brief Whether T is any_t (after removing cv and references), used to exclude overload
        ///        candidates.
        template <typename T>
        inline constexpr bool is_any_value_v = std::is_same_v<std::decay_t<T>, any_t>;

        /// @brief Whether T is meta_type_t (after removing cv and references), used to exclude
        ///        overload candidates.
        template <typename T>
        inline constexpr bool is_meta_type_v = std::is_same_v<std::decay_t<T>, meta_type_t>;

        /// @brief Whether T (after removing cv and references) is an object_pointer_t instantiation.
        /// @note Keeps the generic value constructor from taking over object_pointer_t arguments.
        template <typename T>
        struct is_object_pointer_trait: std::false_type { };

        template <typename U>
        struct is_object_pointer_trait<object_pointer_t<U>>: std::true_type { };

        template <typename T>
        inline constexpr bool is_object_pointer_v = is_object_pointer_trait<std::decay_t<T>>::value;

        /// @brief Whether T is a std::reference_wrapper (the alias entry point test).
        template <typename T>
        struct is_reference_wrapper_trait: std::false_type { };

        template <typename U>
        struct is_reference_wrapper_trait<std::reference_wrapper<U>>: std::true_type { };

        /// @brief The "base type" of an argument: `std::reference_wrapper<U>` -> U; otherwise -> decay_t\<T\>.
        template <typename T>
        struct unwrap_base_trait {
            using type = T;
        };

        template <typename U>
        struct unwrap_base_trait<std::reference_wrapper<U>> {
            using type = U;
        };

        template <typename T>
        using unwrap_base_t = typename unwrap_base_trait<std::decay_t<T>>::type;
    }

    /// @brief Common base class of every failure on any_t, so a single
    ///        @c catch @c (const @c bad_any_t&) can handle them all.
    /// @note Derives from std::logic_error; the semantics are "the caller broke the contract".
    class bad_any_t: public std::logic_error {
    public:
        /// @brief Construct with the given description.
        /// @param what Exception description.
        explicit bad_any_t(const std::string& what):
            std::logic_error(what) { }
    };

    /// @brief Type mismatch on access, qualifier over-reach, failed conversion, or the underlying
    ///        type is not copyable.
    class bad_any_cast_t: public bad_any_t {
    public:
        /// @brief Construct.
        bad_any_cast_t():
            bad_any_t("bad any cast: type mismatch or not convertible") { }
    };

    /// @brief A value or reference was taken from a variable that is only declared (uninitialised).
    class bad_any_uninitialized_t: public bad_any_t {
    public:
        /// @brief Construct.
        bad_any_uninitialized_t():
            bad_any_t("bad any access: variable is not initialized") { }
    };

    /// @brief An operation the type does not support was requested.
    ///
    /// Typical cases: initialising void, an explicit type that disagrees with the value's type,
    /// emplace with a type that differs from the declared one, and calling equal / less / serialize
    /// / a conversion on a type that does not support it.
    class bad_any_unsupported_t: public bad_any_t {
    public:
        /// @brief Construct (without naming the concrete operation).
        bad_any_unsupported_t():
            bad_any_t("bad any operation: unsupported by the type") { }

        /// @brief Construct and include the concrete operation name.
        /// @param operation Operation name, for diagnostics only.
        explicit bad_any_unsupported_t(const std::string& operation):
            bad_any_t("bad any operation: unsupported " + operation) { }
    };

    /// @brief A [variable]: it has three states — invalid, "only declared (uninitialised)" and
    ///        "defined (initialised)" — and, unless invalid, it carries its own type.
    ///
    /// @par States (checked in this order: invalid BEFORE uninitialised)
    ///  - @b invalid (@c !is_valid()): no declared type, no storage — what default construction
    ///    produces and what a failed call reports later. type() answers the invalid handle (never
    ///    throws); every operation needing the value (get(), operator&(), as(), serialize(),
    ///    deserialize()) throws bad_meta_type_t.
    ///  - @b declared only (@c is_valid() && @c !is_initialized()): the type is known and the storage
    ///    is reserved, but the object is NOT constructed — the slot a caller hands to a thunk that
    ///    builds in place (meta_method_t::invoke(obj, args, ret),
    ///    meta_object_t::new_instance(args, out)). get()/as() throw bad_any_uninitialized_t. A void
    ///    declaration is this state with NO storage: operator&() throws bad_any_unsupported_t.
    ///  - @b defined (@c is_valid() && @c is_initialized()): the object is constructed.
    /// @note Invalid is checked BEFORE uninitialised: a default-constructed variable reports
    ///       bad_meta_type_t ("there is no type"), never bad_any_uninitialized_t; a void declaration
    ///       is VALID and reports bad_any_uninitialized_t from get().
    ///
    /// @par One address, three kinds of answer
    /// operator&() is the single storage accessor: a constructed variable answers the value's
    /// address, a declared-only one answers the reserved storage, and an alias answers the aliased
    /// object's address. An INVALID variable throws bad_meta_type_t (there is no type to take an
    /// address of), while a variable that declares a type but reserves no storage (a void
    /// declaration, the reset() state of a non-void one) throws bad_any_unsupported_t — so
    /// "invalid", "declared only" and "nothing reserved" can never be confused.
    ///
    /// @par Composition of the type
    /// The type of a variable @c = meta_type_t (base type) @c × qualifier_t (cvref):
    ///  - @b owning a value: produced by construction, emplace or assignment; qualifier records the
    ///    argument's cvref, and in-place construction yields None;
    ///  - @b alias: produced by @c std::ref / @c std::cref (capture follows the same dispatch);
    ///    it does not own anything.
    ///
    /// @par Failure channel
    ///  - the variable is invalid (it declares no type)      -> bad_meta_type_t
    ///    (checked BEFORE every other failure)
    ///  - an explicit type handle that is invalid            -> bad_meta_type_t
    ///  - taking a value / reference while uninitialised    -> bad_any_uninitialized_t
    ///  - type mismatch, qualifier over-reach, failed
    ///    conversion, not copyable                          -> bad_any_cast_t
    ///  - the type does not support the operation, or the
    ///    requested state does not exist (operator&() on a
    ///    variable with no storage, on an alias to a const
    ///    object)                                           -> bad_any_unsupported_t
    ///
    /// @warning An alias does not own anything: the referenced object must outlive the any_t, and
    ///          copying an any_t keeps it an alias (no deep copy).
    /// @note No registration, no declaration and no application instance are needed: every type's
    ///       interface is produced on demand at compile time, and the process-wide registry assigns
    ///       ids lazily on first use.
    /// @warning Module lifetime: a value whose type was declared in a plugin DLL must not outlive that
    ///          DLL (the handle points into the module's image).
    /// @note Invariant: an any_t is EITHER invalid (it declares no type at all) OR holds a valid
    ///       type. Default construction yields the INVALID state; an explicit void type
    ///       (meta_type_t::from_type\<void\>()) is a valid, declared-only type and is NOT the invalid
    ///       state.
    /// @sa meta_type_t
    class HWSHQTB_EXPORT any_t {
    public:
        /// @brief Default construction: the INVALID state — no declared type and no storage.
        /// @note noexcept and allocation-free; type() answers the invalid handle (never throws).
        /// @note Use @c any_t(meta_type_t::from_type\<void\>()) for a declared-only void variable: an
        ///       explicit void type is VALID, the shape a successful void call reports later.
        any_t() noexcept;

        /// @brief Declare a variable of the given type: reserve its storage, do NOT construct the
        ///        object.
        /// @param type The variable's base type.
        /// @throws bad_meta_type_t If @p type is invalid.
        /// @note The variable ends up "declared only": storage is allocated and owned, so a caller can
        ///       hand operator&() to a thunk that constructs in place — exactly what
        ///       meta_method_t::invoke(obj, ret, args) / meta_object_t::new_instance(args, out) need.
        /// @note A reference-qualified type never reaches this constructor: meta_type_t's identity is
        ///       the bare type, so it has no reference branch.
        /// @note void reserves nothing and operator&() refuses it, but the variable is still VALID and
        ///       declared only (type() answers the void type) — unlike a default-constructed one.
        explicit any_t(const meta_type_t& type);

        /// @name Special member functions
        /// @{
        /// @brief Copy construction.
        /// @param other The variable being copied.
        /// @note Owning a value -> deep copy; an alias -> becomes an alias again (the referenced
        ///       object is not copied); a declared-only variable -> the destination reserves its own
        ///       storage and copy-constructs NOTHING (it stays declared only as well); an invalid
        ///       variable -> the destination is invalid too (there is no type to copy).
        /// @throws bad_any_unsupported_t If the underlying type is not copyable.
        any_t(const any_t& other);

        /// @brief Move construction.
        /// @param other The variable being moved.
        /// @note noexcept. The source keeps its type; its value state becomes unspecified.
        /// @note A declared-only source reserves its own storage on the destination and moves no
        ///       object, because there is no object to move. An invalid source yields an invalid
        ///       destination.
        any_t(any_t&& other) noexcept;

        /// @brief Destruction.
        /// @note noexcept; only an owned, CONSTRUCTED value is destroyed, and an alias does not
        ///       release the referenced object. A declared-only variable destroys nothing but still
        ///       releases its reserved storage. An invalid variable owns nothing at all.
        ~any_t();

        /// @}

        /// @name Defining the variable (owning a value or becoming an alias)
        /// @{
        /// @brief Define the variable from a value (owning): copy from an lvalue, move from an rvalue.
        /// @tparam T Value type; if the argument is a @c std::reference_wrapper the alias semantics
        ///         apply instead.
        /// @param value The initial value.
        /// @throws bad_meta_type_t Only if the type's interface is somehow invalid (every type has
        ///         one).
        /// @throws bad_any_unsupported_t If the underlying type is not copyable / movable.
        /// @note Object pointers are excluded: they are stored under the shared
        ///       object_pointer_t&lt;object_t&gt; entry instead (see the constructor below).
        /// @note No declaration is needed: the interface is produced from T itself, so the global
        ///       registry assigns its id on first use and no registration call is involved.
        template <typename T>
            requires(!detail::is_any_value_v<T> && !detail::is_meta_type_v<T>
                     && !detail::is_object_pointer_v<T>)
        any_t(T&& value) {
            *this = from(std::forward<T>(value), meta_type_t::from_type<detail::unwrap_base_t<T>>());
        }

        /// @brief Store an object pointer (QPointer-in-QVariant); the derived type needs NO
        ///        registration at all.
        /// @tparam T An object_t-derived type.
        /// @param pointer The object to guard; nullptr stores a null guard.
        /// @note The object_pointer_t&lt;object_t&gt; interface is a compile-time constant, so this needs
        ///       no registration and no bootstrap call; it never throws bad_meta_type_t.
        /// @note The variable owns a real guarded pointer, so it turns null when the object dies —
        ///       exactly like an object_pointer_t<T> built from the same address.
        /// @note This is the storage side of qPointerFromVariant(); recovery is pointer_from_any()
        ///       (object_pointer_t.h).
        template <typename T>
            requires std::derived_from<T, object_t> && (!std::is_const_v<T>)
        any_t(T* pointer) {
            *this = adopt_object_pointer_t(static_cast<object_t*>(pointer));
        }

        /// @brief Store an object pointer from an existing guard; the derived type needs NO
        ///        registration.
        /// @tparam T An object_t-derived type.
        /// @param pointer The guard to store; the stored guard shares its death token, so the two
        ///        turn null together.
        /// @note A const-qualified T is rejected: the shared entry is object_pointer_t&lt;object_t&gt;.
        template <typename T>
            requires std::derived_from<T, object_t> && (!std::is_const_v<T>)
        any_t(const object_pointer_t<T>& pointer) {
            *this = adopt_object_pointer_t(pointer.data());
        }

        /// @brief Define the variable from a value, with an explicit type entry.
        /// @tparam T Value type; if the argument is a @c std::reference_wrapper the alias semantics
        ///         apply instead.
        /// @param type The variable's base type entry.
        /// @param value The initial value.
        /// @pre @p type must match the value's base type (the explicit type only selects the context
        ///      / entry; no Base-to-Derived slicing is performed).
        /// @throws bad_meta_type_t If @p type is invalid.
        /// @throws bad_any_unsupported_t If the type disagrees with the value, or the underlying type
        ///         is not copyable / movable.
        template <typename T>
            requires(!detail::is_any_value_v<T> && !detail::is_meta_type_v<T>)
        any_t(const meta_type_t& type, T&& value) {
            if (!type.is_valid()) throw bad_meta_type_t{};
            if (!type.is<detail::unwrap_base_t<T>>()) throw bad_any_unsupported_t{};
            *this = from(std::forward<T>(value), type);
        }

        /// @brief Define the variable by constructing in place (interface produced from T itself).
        /// @tparam T Value type; must be a non-void type without cv / reference.
        /// @tparam Args Constructor argument types.
        /// @param args Arguments forwarded to T's constructor.
        template <typename T, typename... Args>
            requires(!std::is_void_v<T> && std::same_as<T, std::decay_t<T>>)
        any_t(std::in_place_type_t<T>, Args&&... args) {
            construct_in_place<T>(meta_type_t::from_type<T>(), std::forward<Args>(args)...);
        }

        /// @brief Define the variable by constructing in place, with an explicit type entry.
        /// @tparam T Value type; must be a non-void type without cv / reference.
        /// @tparam Args Constructor argument types.
        /// @param type The variable's base type entry.
        /// @param args Arguments forwarded to T's constructor.
        /// @pre @p type must match T.
        /// @throws bad_meta_type_t If @p type is invalid.
        /// @throws bad_any_unsupported_t If @p type disagrees with T.
        template <typename T, typename... Args>
            requires(!std::is_void_v<T> && std::same_as<T, std::decay_t<T>>)
        any_t(const meta_type_t& type, std::in_place_type_t<T>, Args&&... args) {
            if (!type.is_valid()) throw bad_meta_type_t{};
            if (!type.is<T>()) throw bad_any_unsupported_t{};
            construct_in_place<T>(type, std::forward<Args>(args)...);
        }

        /// @}

        /// @name Parameter <- argument binding
        /// @{
        /// @brief Bind one argument: own it in place (copy from an lvalue, move from an rvalue) and
        ///        record the argument's cvref (including RVALUE_REF).
        /// @tparam T Argument type; no declaration is needed.
        /// @param arg The argument.
        /// @return The bound any_t.
        /// @throws bad_any_unsupported_t If the underlying type is not copyable / movable.
        /// @note A @c std::reference_wrapper argument becomes an alias instead; therefore a prvalue
        ///       (such as the literal @c 1) is owned in place and cannot create a dangling alias.
        /// @note The recorded cvref feeds the parameter-compatibility check of
        ///       check_param_qualifier().
        template <typename T>
            requires(!detail::is_any_value_v<T> && !detail::is_meta_type_v<T>)
        static any_t capture(T&& arg) {
            return from(std::forward<T>(arg), meta_type_t::from_type<detail::unwrap_base_t<T>>());
        }

        /// @brief Bind one argument with an explicit base type entry for the parameter.
        /// @tparam T Argument type.
        /// @param type The parameter's base type entry.
        /// @param arg The argument.
        /// @return The bound any_t.
        /// @pre @p type must match the argument's base type.
        /// @throws bad_meta_type_t If @p type is invalid.
        /// @throws bad_any_unsupported_t If @p type disagrees with the argument type.
        template <typename T>
            requires(!detail::is_any_value_v<T> && !detail::is_meta_type_v<T>)
        static any_t capture(const meta_type_t& type, T&& arg) {
            if (!type.is_valid()) throw bad_meta_type_t{};
            if (!type.is<detail::unwrap_base_t<T>>()) throw bad_any_unsupported_t{};
            return from(std::forward<T>(arg), type);
        }

        /// @brief Adopt an already constructed storage block (allocated and filled by the caller) and
        ///        take over its ownership.
        /// @param type The value's type entry.
        /// @param mem The constructed storage; nullptr means the caller filled nothing in, and the
        ///        variable then holds no storage at all (a not-addressable state).
        /// @param qualifiers The storage-side cvref.
        /// @return The adopting any_t; a non-null @p mem is recorded as a CONSTRUCTED object.
        /// @note noexcept; used by the reflective read path to wrap the result filled in by a reader.
        static any_t adopt(const meta_type_t& type, void* mem, qualifier_t qualifiers = qualifier_flag::NONE) noexcept;

        /// @}

        /// @name Assignment and redefinition
        /// @{
        /// @brief Copy assignment.
        /// @param other The variable being copied.
        /// @return @c *this.
        /// @note Owning a value -> deep copy; an alias -> becomes an alias again.
        /// @throws bad_any_unsupported_t If the underlying type is not copyable.
        /// @note The self-assignment guard uses std::addressof: any_t overloads operator& (it is
        ///       the storage accessor), so `&other` would answer the variable's storage address
        ///       instead of the object's own.
        any_t& operator=(const any_t& other);

        /// @brief Move assignment.
        /// @param other The variable being moved.
        /// @return @c *this.
        /// @note noexcept. The source keeps its type; its value state becomes unspecified.
        any_t& operator=(any_t&& other) noexcept;

        /// @brief Redefine the variable from a value (owning).
        /// @tparam T Value type; if the argument is a @c std::reference_wrapper the alias semantics
        ///         apply instead.
        /// @param value The new value.
        /// @return @c *this.
        /// @throws bad_any_unsupported_t If the underlying type is not copyable / movable.
        /// @note Object pointers are excluded: they are stored under the shared
        ///       object_pointer_t&lt;object_t&gt; entry instead (see the assignment below).
        template <typename T>
            requires(!detail::is_any_value_v<T> && !detail::is_meta_type_v<T>
                     && !detail::is_object_pointer_v<T>)
        any_t& operator=(T&& value) {
            *this = from(std::forward<T>(value), meta_type_t::from_type<detail::unwrap_base_t<T>>());
            return *this;
        }

        /// @brief Redefine the variable as an object pointer (QPointer-in-QVariant); the derived type
        ///        needs NO registration.
        /// @tparam T An object_t-derived type.
        /// @param pointer The object to guard; nullptr stores a null guard.
        /// @return @c *this.
        /// @note Same storage as the pointer constructor: the variable owns a real guarded pointer
        ///       and turns null when the object dies. No builtin bootstrap is required.
        template <typename T>
            requires std::derived_from<T, object_t> && (!std::is_const_v<T>)
        any_t& operator=(T* pointer) {
            *this = adopt_object_pointer_t(static_cast<object_t*>(pointer));
            return *this;
        }

        /// @brief Redefine the variable from an existing guard; the derived type needs NO
        ///        registration.
        /// @tparam T An object_t-derived type.
        /// @param pointer The guard to store; the stored guard shares its death token, so the two
        ///        turn null together.
        /// @return @c *this.
        /// @note A const-qualified T is rejected: the shared entry is object_pointer_t&lt;object_t&gt;.
        template <typename T>
            requires std::derived_from<T, object_t> && (!std::is_const_v<T>)
        any_t& operator=(const object_pointer_t<T>& pointer) {
            *this = adopt_object_pointer_t(pointer.data());
            return *this;
        }

        /// @brief Define the variable by constructing T in place (interface produced from T itself).
        /// @tparam T Value type; must be a non-void type without cv / reference.
        /// @tparam Args Constructor argument types.
        /// @param args Arguments forwarded to T's constructor.
        /// @return A reference to the underlying object (valid until the next reset / redefinition).
        /// @pre The declared type matches T (or nothing is declared yet: the invalid state, or the
        ///      void type).
        /// @throws bad_meta_type_t If the type entry of T is invalid.
        /// @throws bad_any_unsupported_t If the declared type disagrees with T.
        /// @note Arguments follow T's constructor semantics; @c std::ref is not unwrapped here.
        /// @note The type entry is always derived from T; any current value is destroyed before the
        ///       new one is constructed. An invalid variable accepts the definition (it declared no
        ///       type, so there is nothing to disagree with).
        template <typename T, typename... Args>
            requires(!std::is_void_v<T> && std::same_as<T, std::decay_t<T>>)
        T& emplace(Args&&... args) {
            if (is_valid() && !(_type.is_void() || _type.is<T>())) throw bad_any_unsupported_t{};
            reset();
            construct_in_place<T>(meta_type_t::from_type<T>(), std::forward<Args>(args)...);
            return *static_cast<T*>(_mem);
        }

        /// @brief Return to the "declared only" state: destroy the underlying object but keep type.
        /// @note noexcept.
        void reset() noexcept;

        /// @brief Swap two variables.
        /// @param other The other variable.
        /// @note noexcept.
        void swap(any_t& other) noexcept;

        /// @brief Swap two variables (the usual std form, ADL-friendly).
        /// @param lhs Left operand.
        /// @param rhs Right operand.
        /// @note noexcept.
        friend void swap(any_t& lhs, any_t& rhs) noexcept;

        /// @}

        /// @name State queries
        /// @{
        /// @brief Whether the variable declares a type at all (the opposite of the invalid state).
        /// @return false only in the invalid state (default construction, a failed call); true for
        ///         both the declared-only and the defined states.
        /// @note noexcept: it is the precondition test for every value access, so it must be usable
        ///       from anywhere without a try block, exactly like is_initialized().
        bool is_valid() const noexcept;

        /// @brief Whether the variable is initialised (the "defined" state).
        /// @return true when the underlying object is constructed (an alias is always constructed:
        ///         it names an existing object); false in the "declared only" state, whose storage
        ///         is reserved but empty, and false in the invalid state.
        /// @note noexcept. This answers the constructed flag, NOT "is a storage address available":
        ///       a declared-only variable owns its storage (operator&() answers it) and still is not
        ///       initialised.
        bool is_initialized() const noexcept;

        /// @brief Get the variable's base type.
        /// @return The declared base type; the INVALID handle when the variable is invalid (it
        ///         declares no type). Never the void type unless void was explicitly declared.
        /// @note noexcept; this is without cvref; see qualifier() for the cvref part.
        /// @note Unlike the value accessors, this does NOT throw for the invalid state: it is a
        ///       query, and "no type" is answerable. Test it with is_valid() before using it as a
        ///       type.
        meta_type_t type() const noexcept;

        /// @brief Get the variable's qualifiers (the cvref part of the declared type).
        /// @return Declared only and in-place construction -> None; bound from an argument -> the
        ///         recorded argument cvref.
        /// @note noexcept.
        qualifier_t qualifier() const noexcept;

        /// @brief The address of the underlying storage.
        /// @return
        ///  - constructed: the value's address;
        ///  - declared, not constructed: the reserved storage (for in-place construction);
        ///  - alias (@c std::ref / @c std::cref): the aliased object's address.
        /// @throws bad_meta_type_t When the variable is INVALID (it declares no type, so there is no
        ///         storage to point at even in principle).
        /// @throws bad_any_unsupported_t When the variable declares a type but holds no storage at
        ///         all (a void declaration, the reset / moved-from state of a non-void one), and from
        ///         the non-const overload for an alias to a CONST object — there is no mutable
        ///         address to hand out there; use the const overload.
        /// @note The single storage accessor: "declared only" answers its reserved storage, so it
        ///       cannot be mistaken for "no storage".
        /// @note The const overload answers for every state that has storage, so a const any_t (or
        ///       any const-qualified view of one) always has an address; the non-const overload is
        ///       the one that has to refuse a const alias.
        void* operator&();
        const void* operator&() const;

        /// @brief Whether the base type is @c std::remove_cvref_t\<T\>.
        /// @tparam T The type to compare against.
        /// @return true when they match.
        /// @note Pure query: it never throws and can be used as a precondition check before get().
        template <typename T>
        bool is() const noexcept {
            return _type.is<T>();
        }

        /// @}

        /// @name Access
        /// @{
        /// @brief Access the underlying object as T; T may carry cvref and the return type is T.
        /// @tparam T The type used for access; may carry cv, an lvalue reference or an rvalue reference.
        /// @return A reference when T is a reference type, a copy when T is a value type.
        /// @pre is_initialized() @c == @c true.
        /// @pre @c std::remove_cvref_t\<T\> equals the base type, and T is no looser than qualifier().
        /// @throws bad_any_cast_t On a type mismatch or qualifier over-reach; the invalid and
        ///         uninitialised failures are the state table's (bad_meta_type_t, then
        ///         bad_any_uninitialized_t).
        template <typename T>
        T get() {
            using base = std::remove_cvref_t<T>;
            if (!is_valid()) throw bad_meta_type_t{};
            if (!is_initialized()) throw bad_any_uninitialized_t{};
            if (!_type.is<base>()) throw bad_any_cast_t{};
            if constexpr (std::is_reference_v<T>) {
                using ref = std::remove_reference_t<T>;
                if constexpr (!std::is_const_v<ref>) {
                    if (_qualifiers & qualifier_flag::CONST) throw bad_any_cast_t{};
                }
                auto* p = static_cast<std::conditional_t<std::is_const_v<ref>, const base, base>*>(_mem);
                if constexpr (std::is_rvalue_reference_v<T>) {
                    return std::move(*p);
                }
                else {
                    return *p;
                }
            }
            else {
                return *static_cast<const base*>(_mem);
            }
        }

        /// @brief Same as get(), but for const variables: a non-const reference cannot be taken out.
        /// @tparam T The type used for access; must not be a non-const lvalue / rvalue reference.
        /// @return A reference when T is a (const) reference type, a copy when T is a value type.
        /// @pre is_initialized() @c == @c true.
        /// @throws bad_any_cast_t On a type mismatch or qualifier over-reach (the state-table
        ///         failures are as in the non-const form).
        template <typename T>
            requires(!std::is_reference_v<T> || std::is_const_v<std::remove_reference_t<T>>)
        T get() const {
            using base = std::remove_cvref_t<T>;
            if (!is_valid()) throw bad_meta_type_t{};
            if (!is_initialized()) throw bad_any_uninitialized_t{};
            if (!_type.is<base>()) throw bad_any_cast_t{};
            if constexpr (std::is_reference_v<T>) {
                const base* p = static_cast<const base*>(_mem);
                if constexpr (std::is_rvalue_reference_v<T>) {
                    return std::move(*p);
                }
                else {
                    return *p;
                }
            }
            else {
                return *static_cast<const base*>(_mem);
            }
        }

        /// @brief Convert to the value type T.
        /// @tparam T Target value type; must satisfy @c T @c == @c std::decay_t\<T\>.
        /// @return The converted result; for the same type this equals a copied value.
        /// @pre is_initialized() @c == @c true.
        /// @throws bad_meta_type_t If the variable is invalid (checked first: there is no type to
        ///         convert FROM).
        /// @throws bad_any_uninitialized_t If uninitialised.
        /// @throws bad_any_unsupported_t If there is no converter from @c type() to T.
        /// @throws bad_any_cast_t If the converter itself fails.
        template <typename T>
            requires std::same_as<T, std::decay_t<T>>
        T as() const {
            if (!is_valid()) throw bad_meta_type_t{};
            if (!is_initialized()) throw bad_any_uninitialized_t{};
            if (_type.is<T>()) {
                return *static_cast<const T*>(_mem);
            }
            meta_type_t target = meta_type_t::from_type<T>();
            if (!target.is_valid()) throw bad_meta_type_t{};
            if (!_type.has_conversion_to(target)) throw bad_any_unsupported_t{};
            void* dst = target.allocate();
            if (!_type.convert(dst, target, _mem)) {
                target.deallocate(dst);
                throw bad_any_cast_t{};
            }
            T result = *static_cast<const T*>(dst);
            target.destroy(dst);
            target.deallocate(dst);
            return result;
        }

        /// @brief Serialise the current value.
        /// @return The serialised result.
        /// @pre is_initialized() @c == @c true.
        /// @throws bad_any_unsupported_t If the type does not support serialisation; the invalid and
        ///         uninitialised failures are the state table's.
        std::string serialize() const;

        /// @brief Define / redefine the variable from @p data.
        /// @param data Serialised data.
        /// @return true on success; false if the data itself is invalid.
        /// @throws bad_any_unsupported_t If the type does not support deserialisation; an invalid
        ///         variable has no type to deserialise INTO (bad_meta_type_t).
        /// @note If a value is currently present, it is destroyed before the new one is constructed.
        bool deserialize(std::string_view data);
        /// @}

    private:
        /// @brief Store a guarded pointer to @p pointer under the single object_pointer_t<object_t>
        ///        entry.
        /// @param pointer The object to guard; nullptr stores a null guard.
        /// @return The variable owning the guard.
        /// @note Never throws: the object-pointer interface is a compile-time constant, so no
        ///       registration is involved.
        /// @note Defined in any_t.cpp: object_pointer_t is only forward-declared here to keep the
        ///       object_t.h / object_pointer_t.h include cycle closed.
        static any_t adopt_object_pointer_t(object_t* pointer);

        /// @brief Construct T in place: allocate, build with the type's storage convention, record the
        ///        new state.
        /// @param type The value's type entry (already validated by the caller: a constructor cannot
        ///        receive a mismatched one, emplace checked it against the declared type).
        /// @param args Arguments forwarded to T's constructor.
        /// @throws Whatever T's constructor throws; the storage is released first, and the variable
        ///         is left in the "no storage" state (not a half-built one).
        template <typename T, typename... Args>
        void construct_in_place(const meta_type_t& type, Args&&... args) {
            _type = type;
            _qualifiers = qualifier_flag::NONE;
            void* mem = type.allocate();
            try {
                new (mem) T(std::forward<Args>(args)...);
            }
            catch (...) {
                type.deallocate(mem);
                _mem = nullptr;
                _owns = false;
                _constructed = false;
                throw;
            }
            _mem = mem;
            _owns = true;
            _constructed = true;
        }

        /// @brief Dispatch: std::reference_wrapper -> alias; anything else -> owning.
        template <typename T>
        static any_t from(T&& value, const meta_type_t& type) {
            if constexpr (detail::is_reference_wrapper_trait<std::decay_t<T>>::value) {
                return from_ref(value, type);
            }
            else {
                return from_value(std::forward<T>(value), type);
            }
        }

        /// @brief Owning a value: copy from an lvalue, move from an rvalue, and record the argument cvref.
        template <typename T>
        static any_t from_value(T&& value, const meta_type_t& type) {
            if (!type.is_valid()) throw bad_meta_type_t{};
            using U = std::decay_t<T>;
            any_t var;
            var._type = type;
            var._qualifiers = get_param_qualifier<T&&>();
            var._owns = true;
            var._mem = type.allocate();
            // Decay to U first (array -> pointer, rvalue -> move, lvalue -> copy) and only then take
            // the address. Otherwise an array argument (such as a string literal) would have the
            // address of its first element taken by addressof, while the move/copy thunk expects a
            // pointer to a U object, which would read out of bounds.
            U decayed = std::forward<T>(value);
            void* src = const_cast<void*>(static_cast<const void*>(std::addressof(decayed)));
            const bool ok = std::is_lvalue_reference_v<T>
                ? type.copy_construct(var._mem, src)
                : type.move_construct(var._mem, src);
            if (!ok) {
                type.deallocate(var._mem);
                var._mem = nullptr;
                var._owns = false;
                throw bad_any_unsupported_t{};
            }
            var._constructed = true;
            return var;
        }

        /// @brief Alias: unwrap the @c std::reference_wrapper and record LVALUE_REF (plus CONST for
        ///        @c std::cref).
        template <typename T>
        static any_t from_ref(std::reference_wrapper<T> ref, const meta_type_t& type) {
            if (!type.is_valid()) throw bad_meta_type_t{};
            any_t var;
            var._type = type;
            var._mem = const_cast<void*>(static_cast<const void*>(std::addressof(ref.get())));
            var._owns = false;
            // The alias names an object that already exists, so it is initialised even though it
            // owns nothing: is_initialized() answers the constructed flag, not "do we own storage".
            var._constructed = true;
            var._qualifiers = qualifier_flag::LVALUE_REF;
            if constexpr (std::is_const_v<T>) {
                var._qualifiers = var._qualifiers | qualifier_flag::CONST;
            }
            return var;
        }

        /// @brief Mark the variable constructed AFTER a thunk has built the object in its storage.        /// @note Called by the erased call paths (meta_method_t::invoke(obj, ret, args),
        ///       meta_object_t::new_instance(args, out)) once a thunk filled a declared-only slot; it
        ///       is private so a caller cannot pretend a slot is constructed.
        /// @note A non-blocking Queued invoke() cannot make this call on the caller's thread: the
        ///       thunk runs later, on the receiver's thread. That path's meta_call_event_t is therefore
        ///       a friend too and calls it once the event placed the call — which is why the returned
        ///       variable must still be the one the event points at at that moment.
        void mark_constructed() noexcept {
            _constructed = true;
        }

        friend class meta_method_t;
        friend class meta_object_t;
        friend class meta_call_event_t;

        /// @brief Base type; the INVALID handle when the variable is invalid (it declares no type).
        meta_type_t _type;

        /// @brief Underlying storage; nullptr means no storage is reserved at all (see operator&()).
        /// @note Non-null in BOTH the "constructed" and the "declared but not constructed" state:
        ///       the pointer never answers "is the object built?" — _constructed does.
        void* _mem = nullptr;

        /// @brief Storage-side cvref.
        qualifier_t _qualifiers = qualifier_flag::NONE;

        /// @brief Whether the storage pointed at by @c _mem is owned (owned -> destroy + deallocate;
        ///        alias -> do not deallocate).
        bool _owns = false;

        /// @brief Whether the underlying object is constructed (the "defined" state).
        /// @note Distinct from `_mem != nullptr`, which is only "storage is reserved".
        bool _constructed = false;
    };
}

#endif
