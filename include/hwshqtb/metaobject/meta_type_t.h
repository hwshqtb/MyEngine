#ifndef HWSHQTB__METAOBJECT__META_TYPE_H
#define HWSHQTB__METAOBJECT__META_TYPE_H

/// @file meta_type_t.h
/// @brief The run-time type descriptor meta_type_t and its failure channel bad_meta_type_t.

#include "basic_def.h"
#include "register_type.hpp"
#include "type_interface_t.h"
#include "export.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace hwshqtb::metaobject {
    class object_t;

    /// @brief Thrown when an invalid meta_type_t is passed to an operation that needs one, or across
    ///        an any_t boundary.
    ///
    /// It counts as "invalid" when the handle holds no interface: DEFAULT CONSTRUCTION produces such a
    /// handle, and so does a failed name lookup. A handle that names a type is always valid, whether or
    /// not its id was assigned yet.
    ///
    /// @note Derives from std::runtime_error and can be caught with
    ///       @c catch @c (const @c std::runtime_error&); it is independent of any_t's failure channel
    ///       (the bad_any_t family).
    /// @sa meta_type_t::is_valid
    /// @sa meta_type_t::from_type
    class bad_meta_type_t: public std::runtime_error {
    public:
        /// @brief Construct without a type name.
        bad_meta_type_t():
            std::runtime_error("bad meta type: invalid meta_type_t") { }

        /// @brief Construct with a type name.
        /// @param name The name of the type involved, for diagnostics only.
        explicit bad_meta_type_t(const std::string& name):
            std::runtime_error("bad meta type: invalid meta_type_t (" + name + ")") { }
    };

    /// @brief a class like QMetaType, but more powerful and flexible, used for any named type.
    ///
    /// A value-semantic run-time type handle: internally it is ONE pointer to the type's static
    /// type_interface_t, so it can be freely copied, moved and default-constructed.
    ///
    /// @par State model
    ///  - @b invalid: the handle holds no interface. DEFAULT CONSTRUCTION produces one — this is the
    ///    analogue of a default std::type_index, meaning "no type at all" — and so does a failed name
    ///    lookup;
    ///  - @b void: the void interface, which is a REAL type. It is reached only through
    ///    from_type\<void\>() (or a successful lookup of "void"), and it is "declared only", never
    ///    initialised — Qt's QMetaType() convention applied to the void type itself;
    ///  - @b valid: some declared type. A valid handle may still have id() == 0 until the registry
    ///    assigns one; that is an implementation detail, never a different type.
    ///
    /// @par Identity
    /// Two handles identify the same type when their interfaces carry the same runtime id, or — while
    /// either id is still 0 — the same identity name. They are NEVER compared by interface pointer:
    /// each module holds its own copy of the interface (the type_interface template is deliberately
    /// unexported so it stays usable in constant expressions), and the addresses differ across a PE
    /// module boundary.
    ///
    /// @par Layering contract
    ///  - Query interfaces are all @c noexcept and defensive: when invalid they return index_npos /
    ///    an empty string / false;
    ///  - Operations that need a type throw bad_meta_type_t on an invalid handle;
    ///  - "The type does not support this operation" is expressed by @c false or @c std::nullopt;
    ///    this layer never throws for it, and any_t decides whether to upgrade "unsupported" into an
    ///    exception.
    ///
    /// @par Module lifetime
    /// A handle points at the type's `type_interface_t`, which lives in the module that declared the
    /// type (each module keeps its own constant-initialised copy, and the registry gives them one
    /// id). A handle — and an any_t holding one — of a type declared in a plugin DLL must therefore
    /// not outlive that DLL. Builtins and types declared in the executable or in the library are
    /// unaffected. The registry keeps the identity NAME and the id by value, so the id space itself
    /// survives an unload.
    ///
    /// @par Storage convention
    ///  - Every construction-like interface taking @c void* receives a pointer to [unconstructed]
    ///    storage, which is constructed on success;
    ///  - On failure the storage is left unconstructed / clean, so the caller only needs to call
    ///    deallocate();
    ///  - Every @c void* / @c const @c void* operand must have been produced by this type itself;
    ///    otherwise the behaviour is undefined.
    ///
    /// @sa any_t
    /// @sa type_interface_t.h
    class HWSHQTB_EXPORT meta_type_t {
    public:
        /// @brief Default construction yields the INVALID handle: the std::type_index analogue, that
        ///        is, "no type at all" — never the void type.
        /// @note noexcept and constexpr. The handle holds no interface, so is_valid() answers false,
        ///       is_void() answers false (void is a different, REAL type), and every query answers its
        ///       defensive value.
        /// @note The void type is reachable only through from_type\<void\>() (or a successful lookup
        ///       of "void"); it is a valid, declared-only type and is NOT this state.
        /// @sa is_valid
        /// @sa from_type
        constexpr meta_type_t() noexcept:
            _iface(nullptr) { }

        /// @name Special member functions
        /// @{
        /// @brief Copy construction.
        meta_type_t(const meta_type_t&) = default;

        /// @brief Move construction.
        meta_type_t(meta_type_t&&) = default;

        /// @brief Copy assignment.
        meta_type_t& operator=(const meta_type_t&) = default;

        /// @brief Move assignment.
        meta_type_t& operator=(meta_type_t&&) = default;

        /// @brief Destruction.
        ~meta_type_t() = default;
        /// @}

        /// @name Identity queries
        /// @{
        /// @brief Whether the handle names a type.
        /// @note noexcept; safe on an invalid handle.
        constexpr bool is_valid() const noexcept {
            return _iface != nullptr;
        }

        /// @brief Whether this is the void type.
        /// @note noexcept; safe on an invalid handle (returns false).
        /// @note Compares the identity NAME, not the interface pointer: the void interface has one
        ///       copy per module and the handle may carry another module's.
        bool is_void() const noexcept;

        /// @brief Get the runtime-assigned type id.
        /// @return The id for a valid handle (0 while the registry has not assigned one yet);
        ///         index_npos for an invalid one.
        /// @note noexcept. 0 is the "unassigned" sentinel: equality tests must fall back to the name
        ///       while either side is 0 (meta_type_t's operator== does exactly that).
        index_t id() const noexcept;

        /// @brief Get the type's identity name: the ORIGINAL spelling (meta_info_t::original_name<T>()),
        ///        which is what `type_interface_t::name` and every table key carry.
        /// @return The name for a valid handle; an empty string for an invalid one.
        /// @note Never the canonical user-facing spelling (meta_info_t::canonical_name<T>()), which is
        ///       only a registry alias; the two differ when a reserved namespace is involved.
        std::string name() const;

        /// @brief The identity name as a VIEW: the same text name() answers, without the copy.
        /// @return A view of the interface's name, which lives in static storage (it outlives every
        ///         handle); the empty view for an invalid handle.
        /// @note constexpr, noexcept and ALLOCATION-FREE: this is the accessor a matcher uses.
        ///       name() builds a std::string, which overload resolution must never do — it compares
        ///       once per argument, and a `consteval` resolver has to evaluate the comparison at
        ///       compile time, where an allocation would be a hard error.
        /// @sa name
        constexpr std::string_view name_view() const noexcept {
            return detail::interface_name(_iface);
        }

        /// @brief The static properties of the type.
        /// @return The flag set; type_tag_flag::NONE for an invalid handle.
        /// @note noexcept.
        type_tag_t tag() const noexcept;

        /// @brief Render a stored value for debugging.
        /// @param value Pointer to a constructed object of this type.
        /// @return The type's own rendering when it has one, otherwise @c "<name>"; an empty string
        ///         for an invalid handle or a null pointer.
        /// @note Separate from serialisation: a serialised form may be binary-encoded.
        std::string debug_string(const void* value) const;

        /// @brief Whether this handle names @c std::decay_t\<T\>.
        /// @tparam T The type to compare against; cv, references and arrays are removed.
        /// @return true if it is; false for an invalid handle or a different type.
        /// @note Pure query: registers nothing and never throws. The interface of T is produced on
        ///       demand at compile time, so no declaration is needed.
        template <typename T>
        bool is() const noexcept {
            return detail::same_type(_iface, type_interface_t::declared<T>());
        }

        /// @brief The handle of @c std::decay_t\<T\>, registered on first use.
        /// @tparam T The type to look up; no declaration is needed.
        /// @return The corresponding handle (always valid).
        /// @note The interface is produced on demand at compile time, so the handle needs no lookup;
        ///       the id is assigned by the one global registry on first use (Qt's QMetaType::fromType).
        /// @note Binds the CANONICAL spelling (e.g. "std::basic_string<char>") as an alias of the
        ///       identity name (e.g. "std::__cxx11::basic_string<char>"), so resolve_global() answers
        ///       both spellings. That is why this is not a constant expression.
        template <typename T>
        static meta_type_t from_type() noexcept {
            const type_interface_t* iface = type_interface_t::registered<T>();
            (void)detail::bind_canonical_alias<T>(iface);
            return meta_type_t(iface);
        }

        /// @brief Register a type so a by-name lookup finds it, and optionally bind one extra name.
        /// @tparam T The type to register; cv, references and arrays are removed (std::decay_t).
        /// @param alias An extra name the type answers to, e.g. "std::string" for
        ///        std::basic_string<char>. Empty binds nothing extra.
        /// @return The type's id (unchanged by a repeated call).
        /// @note Idempotent: after the first call this is an atomic load of the already-assigned id, so
        ///       it is safe to call from every start-up path.
        /// @note Repeat the call to bind more than one extra name. A name already bound to a DIFFERENT
        ///       type is refused with one Warning and the first binding is kept — the same policy the
        ///       class registry uses.
        /// @note The registered name is `meta_info_t::registered_type_name<T>()` — the very spelling
        ///       detail::normalized_type_name gives the reflected tables, so a table column that refers
        ///       to @p T resolves after this call.
        /// @note The CANONICAL spelling (meta_info_t::canonical_name<T>(), detail namespaces elided) is
        ///       bound as an additional alias automatically: the identity name is the ORIGINAL
        ///       spelling, so resolve_global("std::basic_string<char>") would otherwise stop working.
        ///       bind_alias treats the two spellings being equal as a no-op success, which is the
        ///       common case.
        /// @note This is the EXPLICIT entry point. Producing a type's interface through
        ///       meta_type_t::from_type<T>() / type_interface_t::registered<T>() registers it too (that
        ///       is how a handle's name becomes findable), and the library itself registers the builtin
        ///       set plus the standard-library typedef aliases ("std::string", "std::string_view",
        ///       "std::wstring") when it is loaded — detail::register_default_types() runs from a static
        ///       initialiser, and core_application's constructor re-asserts it; a type that must be found
        ///       by name without either needs one call here.
        /// @note The type itself must be nameable (canonical_name.hpp): a type declared in an anonymous
        ///       namespace is a compile-time error, by design.
        template <typename T>
        static index_t register_type(std::string_view alias = { }) {
            const type_interface_t* iface = type_interface_t::declared<T>();
            const index_t id = detail::ensure_registered(iface);
            (void)detail::bind_canonical_alias<T>(iface);
            if (!alias.empty()) {
                (void)detail::bind_alias(iface, alias);
            }
            return id;
        }

        /// @brief The handle of an interface, assigning its id on first use.
        /// @param iface A type's interface (from type_interface_t::declared\<T\>() or a table column).
        /// @return The handle; an invalid one when @p iface is nullptr.
        /// @note Entry point for an interface obtained without going through from_type\<T\>(): it
        ///       ensures the handle carries the runtime id, so the integer identity fast path is
        ///       available for every handle in existence. This is also what puts the identity NAME
        ///       into the registry, which is what a later resolve_global(name) finds.
        /// @note noexcept; registering is idempotent.
        static meta_type_t from_interface(const type_interface_t* iface) noexcept;

        /// @brief Resolve a type by name in the process-wide registry.
        /// @param name The type's identity name (the ORIGINAL spelling), or the canonical spelling the
        ///        registry binds as an alias on registration.
        /// @return The corresponding type when the name is known; an invalid meta_type_t otherwise.
        /// @note Defensive query; never throws. The returned handle carries the runtime id (assigned
        ///       here on first use): a found name always yields a handle whose id() is non-zero.
        static meta_type_t resolve_global(std::string_view name) noexcept;

        /// @brief The one type entry every object pointer shares (qPointerFromVariant's storage type).
        /// @return The object_pointer_t&lt;object_t&gt; interface handle.
        /// @note constexpr: the interface is produced on demand at compile time, so no registration
        ///       and no builtin bootstrap are needed (the registry assigns its id on first use).
        /// @note Defined in object_pointer_t.h (the type has to be complete there).
        /// @sa any_t
        /// @sa pointer_from_any
        static constexpr meta_type_t object_pointer_t_type() noexcept;

        /// @brief Whether this handle is the object-pointer entry.
        /// @return true when this handle names object_pointer_t&lt;object_t&gt;.
        /// @note noexcept; safe on an invalid handle (returns false).
        bool is_object_pointer_trait() const noexcept;
        /// @}

        /// @name Type conversion (*this is the source type)
        /// @{
        // Two families: a registered (From, To) converter keyed by runtime ids, and the structural
        // integer <-> enum / flags identity (IS_INTEGER on one side, IS_ENUM / IS_FLAGS on the other),
        // which needs no registration.

        /// @brief Whether a converter from @c *this to @p target exists.
        /// @param target The target type.
        /// @return true when @p target is the same type, a registered converter exists, or the two
        ///         form the structural integer <-> enum / flags pair; false otherwise.
        /// @note noexcept; always true when @p target is the same type (the identity conversion,
        ///       equivalent to copy_construct).
        /// @sa register_conversion
        bool has_conversion_to(const meta_type_t& target) const noexcept;

        /// @brief Convert @p src to the @p target type and construct it into @p dst.
        /// @param dst [Unconstructed] storage of the @p target type.
        /// @param target The target type.
        /// @param src A constructed object of type @c *this.
        /// @return true on success, in which case @p dst is constructed; false on failure, in which
        ///         case @p dst stays unconstructed / clean.
        /// @note The structural integer <-> enum / flags case is a WIDTH contract: the source is read
        ///       as a 64-bit value (sign- or zero-extended from its own storage width) and is placed
        ///       only when it is representable in the TARGET's storage width — a target enum's or flag
        ///       wrapper's underlying width, or the target integer's width. Otherwise the conversion
        ///       fails without truncating (widening an enum / flags value into a wider integer always
        ///       fits).
        /// @pre @p dst came from @c target.allocate() and is not constructed yet.
        /// @throws bad_meta_type_t If a handle is invalid.
        bool convert(void* dst, const meta_type_t& target, const void* src) const;
        /// @}

        /// @name Low-level lifetime operations
        /// @{
        /// @brief Whether the type is default-constructible.
        bool has_default_constructor() const noexcept;

        /// @brief Default-construct on unconstructed storage.
        /// @param mem Unconstructed storage.
        /// @return true on success; false when the type is not default-constructible.
        bool default_construct(void* mem) const;

        /// @brief Whether the type is copy-constructible.
        bool has_copy_constructor() const noexcept;

        /// @brief Copy-construct from @p src into @p dst.
        /// @param dst Unconstructed storage.
        /// @param src A constructed object of type @c *this.
        /// @return true on success; false when the type is not copy-constructible.
        bool copy_construct(void* dst, const void* src) const;

        /// @brief Whether the type is move-constructible.
        bool has_move_constructor() const noexcept;

        /// @brief Move-construct from @p src into @p dst.
        /// @param dst Unconstructed storage.
        /// @param src A constructed object of type @c *this; on success it is left in a "valid but
        ///        unspecified" state.
        /// @return true on success; false when the type is not move-constructible.
        bool move_construct(void* dst, void* src) const;

        /// @brief Whether the type's move constructor is declared noexcept.
        /// @note Lets any_t pick its move semantics: when true it can call
        ///       noexcept_move_constructor() to actually move the value, and when false it moves the
        ///       storage pointer instead, which keeps any_t's move constructor noexcept.
        /// @sa noexcept_move_constructor
        bool has_noexcept_move_constructor() const noexcept;

        /// @brief Move construction that promises not to throw.
        /// @param dst Unconstructed storage.
        /// @param src A constructed object of type @c *this; on success it is left in a "valid but
        ///        unspecified" state.
        /// @warning Only callable when has_noexcept_move_constructor() @c == @c true; if that
        ///          precondition is violated (the underlying move constructor throws) this calls
        ///          std::terminate directly.
        void noexcept_move_constructor(void* dst, void* src) const noexcept;

        /// @brief Whether the type is destructible.
        bool has_destructor() const noexcept;

        /// @brief Destroy the object at @p mem.
        /// @param mem A constructed object of type @c *this.
        /// @note noexcept and never fails: any_t's destructor path relies on this.
        void destroy(void* mem) const noexcept;

        /// @brief Whether the type is copy-assignable.
        bool has_copy_assignment() const noexcept;

        /// @brief Copy-assign @p src into @p dst.
        /// @param dst A constructed object of type @c *this.
        /// @param src A constructed object of type @c *this.
        /// @return true on success; false when the type is not copy-assignable.
        bool copy_assign(void* dst, const void* src) const;

        /// @brief Whether the type is move-assignable.
        bool has_move_assignment() const noexcept;

        /// @brief Move-assign @p src into @p dst.
        /// @param dst A constructed object of type @c *this.
        /// @param src A constructed object of type @c *this; on success it is left in a "valid but
        ///        unspecified" state.
        /// @return true on success; false when the type is not move-assignable.
        bool move_assign(void* dst, void* src) const;

        /// @brief Whether the type's move assignment is declared noexcept.
        /// @note Lets any_t pick its move semantics: when true it can call noexcept_move_assign() to
        ///       move-assign on the existing storage, and when false it swaps the storage pointers
        ///       instead, which keeps any_t's move assignment noexcept.
        /// @sa noexcept_move_assign
        bool has_noexcept_move_assignment() const noexcept;

        /// @brief Move assignment that promises not to throw.
        /// @param dst A constructed object of type @c *this.
        /// @param src A constructed object of type @c *this; on success it is left in a "valid but
        ///        unspecified" state.
        /// @warning Only callable when has_noexcept_move_assignment() @c == @c true; if that
        ///          precondition is violated (the underlying move assignment throws) this calls
        ///          std::terminate directly.
        void noexcept_move_assign(void* dst, void* src) const noexcept;

        /// @brief Allocate raw storage for the type (derived from size / alignment).
        /// @return Unconstructed storage; it must be filled by a construction-like interface and
        ///         eventually handed to deallocate().
        /// @throws bad_meta_type_t If the handle is invalid.
        /// @throws std::bad_alloc If memory is exhausted.
        void* allocate() const;

        /// @brief Release storage obtained from allocate() or convert().
        /// @param mem The storage to release; @c nullptr is allowed.
        /// @note noexcept, never throws.
        void deallocate(void* mem) const noexcept;
        /// @}

        /// @name Comparison (two primitives plus four inline derivations)
        /// @{
        /// @brief Whether the type provides equality comparison.
        bool has_equal_to() const noexcept;

        /// @brief Equality comparison.
        /// @param lhs A constructed object of type @c *this.
        /// @param rhs A constructed object of type @c *this.
        /// @return The comparison result; false when the type provides no equality comparison.
        /// @pre The result is only meaningful when has_equal_to() @c == @c true.
        bool equal_to(const void* lhs, const void* rhs) const;

        /// @brief Whether the type provides less-than comparison.
        bool has_less() const noexcept;

        /// @brief Less-than comparison.
        /// @param lhs A constructed object of type @c *this.
        /// @param rhs A constructed object of type @c *this.
        /// @return The comparison result; false when the type provides no less-than comparison.
        /// @pre The result is only meaningful when has_less() @c == @c true.
        bool less(const void* lhs, const void* rhs) const;

        /// @brief Derived predicate: not equal, equivalent to @c !equal_to(lhs, @c rhs).
        /// @param lhs A constructed object of type @c *this.
        /// @param rhs A constructed object of type @c *this.
        /// @return true when they are not equal.
        /// @pre has_equal_to()
        bool not_equal_to(const void* lhs, const void* rhs) const;

        /// @brief Derived predicate: greater, equivalent to @c less(rhs, @c lhs).
        /// @param lhs A constructed object of type @c *this.
        /// @param rhs A constructed object of type @c *this.
        /// @return true when @p lhs is greater than @p rhs.
        /// @pre has_less()
        bool greater(const void* lhs, const void* rhs) const;

        /// @brief Derived predicate: less or equal, equivalent to @c !less(rhs, @c lhs).
        /// @param lhs A constructed object of type @c *this.
        /// @param rhs A constructed object of type @c *this.
        /// @return true when @p lhs is less than or equal to @p rhs.
        /// @pre has_less()
        bool less_equal(const void* lhs, const void* rhs) const;

        /// @brief Derived predicate: greater or equal, equivalent to @c !less(lhs, @c rhs).
        /// @param lhs A constructed object of type @c *this.
        /// @param rhs A constructed object of type @c *this.
        /// @return true when @p lhs is greater than or equal to @p rhs.
        /// @pre has_less()
        bool greater_equal(const void* lhs, const void* rhs) const;

        /// @}

        /// @name Serialisation
        /// @{
        /// @brief Whether the type is serialisable.
        bool serializable() const noexcept;

        /// @brief Serialise the object at @p mem.
        /// @param mem A constructed object of type @c *this.
        /// @return The serialised result on success; @c std::nullopt when the type does not support
        ///         serialisation.
        /// @note nullopt is used instead of an empty string for "unsupported" so that it stays
        ///       distinguishable from "the serialised result is itself empty".
        std::optional<std::string> serialize(const void* mem) const;

        /// @brief Whether the type is deserialisable.
        bool deserializable() const noexcept;

        /// @brief Deserialise from @p data and construct into @p mem.
        /// @param mem Unconstructed storage.
        /// @param data The serialised data.
        /// @return true on success, in which case @p mem is constructed; false when the data itself
        ///         is invalid, in which case @p mem stays unconstructed.
        /// @pre @p mem is unconstructed (if an object already lives there, destroy() it first).
        bool deserialize(void* mem, std::string_view data) const;
        /// @}

        /// @name Type identity
        /// @{
        /// @brief Whether two handles name the same type.
        /// @return true when both are valid and their interfaces agree (same id, or the same identity
        ///         name while either id is still 0).
        /// @note Never compares interface pointers: each module owns a copy of the interface.
        friend bool operator==(const meta_type_t& lhs, const meta_type_t& rhs) noexcept;
        /// @}

    private:
        friend class any_t;

        /// @brief Construct a handle from an interface; nullptr means an invalid handle.
        /// @param iface The type's interface (may be nullptr).
        /// @note noexcept, constexpr: the object_pointer_t_type() handle is built this way.
        explicit constexpr meta_type_t(const type_interface_t* iface) noexcept:
            _iface(iface) { }

        /// @brief The type's interface; nullptr means an invalid handle.
        const type_interface_t* _iface = nullptr;
    };
}

#endif
