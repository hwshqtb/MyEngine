#ifndef HWSHQTB__METAOBJECT__META_CONSTRUCTOR_T_H
#define HWSHQTB__METAOBJECT__META_CONSTRUCTOR_T_H

/// @file meta_constructor_t.h
/// @brief The reflection handle for constructors, meta_constructor_t.

#include "basic_def.h"
#include "meta_info_t.h"
#include "any_t.h"
#include "export.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <utility>

namespace hwshqtb::metaobject {
    class object_t;
    class meta_object_t;

    /// @brief The reflection handle of ONE constructor of a class (Qt answers constructors through
    ///        QMetaMethod with MethodType::ConstructorMethod; here a constructor is a handle type of
    ///        its own, because it is not a method).
    ///
    /// @par A constructor is not a method
    /// Nothing in this class is reached through meta_method_t: a constructor has no existing object to
    /// run on, no return type and no signal/slot classification, so it has its own handle, its own
    /// table (meta_info_t::constructor_infos) and its own index space. The two spaces never mix:
    /// meta_object_t::method_count() / method(index) count METHODS only, while
    /// constructor_count() / constructor(index) / index_of_constructor() cover this class's OWN
    /// constructors (constructors are not inherited, so there is no chain walk here).
    ///
    /// @par Ownership
    /// A handle stores `const meta_object_t* _owner` (the DECLARING meta-object) plus its slot inside
    /// that meta-object's constructor_infos. It is a pure handle: every question — signature, name, access,
    /// qualifier, parameters, tag — is answered from the row it points at. The table is owned by
    /// value by the meta-object, so a handle is valid while that meta-object lives (the class's
    /// `static_meta_object`, or an arena entry from meta_object_builder_t).
    ///
    /// @par Calling convention
    /// construct() runs the row's thunk with meta_info_t's generic_call convention: @c params[0] is
    /// the return slot — here the `void*` storage that receives the new instance's ADDRESS — and
    /// @c params[1...] hold the argument addresses.
    ///
    /// @sa meta_object_t::constructor
    /// @sa meta_object_t::index_of_constructor
    class HWSHQTB_EXPORT meta_constructor_t {
    public:
        /// @brief Default construction yields an invalid handle.
        meta_constructor_t() noexcept = default;

        /// @brief Construct the handle of the @p index -th constructor of @p owner.
        /// @param owner The declaring meta-object.
        /// @param index The slot inside that meta-object's constructor_infos (the CONSTRUCTOR coordinate).
        /// @note Public because a caller that already holds the meta-object names the row itself; an
        ///       out-of-range slot yields the invalid handle rather than undefined behaviour.
        meta_constructor_t(const meta_object_t* owner, index_t index) noexcept;

        /// @name Identity
        /// @{
        /// @brief Whether the handle names a constructor that can be CALLED.
        /// @return true when the row exists and carries a construction thunk; false for an invalid
        ///         handle and for a constructor no thunk can be generated for (a deleted one, or any
        ///         constructor of an abstract type — both keep a null caller).
        /// @note noexcept. "Is there a row?" is a different question and is answered by
        ///       signature().empty(): the two differ exactly for a deleted / abstract constructor,
        ///       which is in the table but not callable.
        bool is_valid() const noexcept;

        /// @brief Get the normalised signature, of the form @c class_t(type,type).
        /// @return The signature when the row exists; an empty view for an invalid handle.
        /// @note noexcept, no allocation. Constructors have no cvref suffix: the key stays
        ///       "class_t(int,int)".
        std::string_view signature() const noexcept;

        /// @brief Get the class name the constructor belongs to.
        /// @return The bare class identifier (what precedes '(' in the signature); an empty view for
        ///         an invalid handle.
        /// @note noexcept.
        std::string_view name() const noexcept;

        /// @brief The declared access of the constructor, as a hint only: a meta-object construction
        ///        does not enforce it (every constructor in the table can be called).
        /// @note none for an invalid handle.
        /// @note A run-time built entry (meta_object_builder_t) reports PUBLIC_SCOPE: the builder
        ///       carries no access information.
        /// @note noexcept.
        method_access_enum access() const noexcept;

        /// @brief The constructor's own cvref qualifiers.
        /// @return Always None: a constructor has no cvref suffix to report, and asking is_const()
        ///         about one is not a valid question.
        /// @note noexcept. Present so the handle reads like meta_method_t::qualifier().
        /// @note Structural, not read from the row: the constructor row type
        ///       (detail::constructor_info_t) has no cvref column at all, because the column could only
        ///       ever hold None for a constructor. The accessor stays for shape, and answers None.
        qualifier_t qualifier() const noexcept;

        /// @brief The constructor's global coordinate in the CONSTRUCTOR space of its class.
        /// @return The slot inside the owner's constructor_infos — the coordinate
        ///         meta_object_t::constructor(index) accepts; index_npos for an invalid handle.
        /// @note noexcept. This is NOT a method index: meta_object_t::method(index) does not accept
        ///       it, and the two spaces are counted independently.
        index_t index() const noexcept;

        /// @brief The declaring meta-object; nullptr for an invalid handle.
        /// @note noexcept.
        const meta_object_t* owner() const noexcept;
        /// @}

        /// @name Parameter metadata
        /// @{
        /// @brief Get the number of parameters.
        /// @return The parameter count when the row exists; 0 for an invalid handle.
        /// @note noexcept.
        index_t param_count() const noexcept;

        /// @brief The name of parameter @p index (empty when it is unnamed).
        /// @return The declared parameter name; an empty view when out of range or invalid.
        /// @note noexcept.
        std::string_view param_name(index_t index) const noexcept;

        /// @brief Get the normalised type name of the @p index -th parameter.
        /// @param index Parameter index, zero-based.
        /// @return The type name on a hit; an empty view when out of range or invalid.
        /// @note noexcept, no allocation.
        std::string_view param_type_name(index_t index) const noexcept;

        /// @brief Get the qualifiers of the @p index -th parameter.
        /// @param index Parameter index, zero-based.
        /// @return The parameter's cvref qualifiers on a hit; None when out of range or invalid.
        /// @note noexcept.
        qualifier_t param_qualifier(index_t index) const noexcept;
        /// @}

        /// @name Annotations
        /// @{
        /// @brief Query an annotation's staticised value pointer by its type name.
        /// @param type_name The normalised type name of the carried value ("int",
        ///        "hwshqtb::metaobject::revision_t", "const char*", ...).
        /// @return The value pointer (the staticised object), or nullptr when this constructor has no
        ///         annotation of that type.
        /// @note The annotations are the [[= make_tag(v)]] / [[= make_str_tag(text)]] attributes of
        ///       the declaration. The pointer refers to a static object of the carried type, so read it
        ///       back through that type:
        ///         * [[= make_tag(revision_t{42, 0})]] -> *(const revision_t*)tag("hwshqtb::metaobject::revision_t")
        ///         * [[= make_str_tag("port_tag")]]  -> *(const char* const*)tag("const char*")  (the
        ///           stored object IS the const char*, hence the double indirection)
        /// @note The shape is exactly meta_method_t::tag(): the two handles read the same way.
        /// @note noexcept. An invalid handle and a miss both answer nullptr; nothing is logged.
        const void* tag(std::string_view type_name) const noexcept;
        /// @}

        /// @name Construction
        /// @{
        /// @brief Run this constructor's thunk, writing the new instance's address through
        ///        @p out_slot.
        /// @param args One any_t per parameter, in declaration order.
        /// @param out_slot The address of the caller's `void*` variable; the thunk writes the new
        ///        instance's address through it (params[0] of the generic_call convention).
        /// @return true when the instance was built and stored through @p out_slot; false for an
        ///         invalid handle, a null @p out_slot, a deleted / abstract constructor (no thunk),
        ///         an argument count that does not match the row, or a thunk that threw (logged at
        ///         Critical by the construction path).
        /// @note This is the row-level entry into the ONE construction path
        ///       (meta_object_t::construct_into), so the diagnostics and the try/catch are the same
        ///       ones new_instance() reports. The handle has already chosen the constructor, so the
        ///       argument list is checked against THIS row's parameter count before the thunk runs;
        ///       the argument TYPES are the caller's responsibility, exactly as they are for the
        ///       signature form of meta_object_t::construct_into.
        /// @note The caller owns the instance: destroy it with an ordinary `delete` of the class it
        ///       named. The table cannot check which class the caller casts the result to.
        /// @note Throws bad_any_uninitialized_t when an argument is only declared (no value was
        ///       constructed) — the same contract as new_instance().
        bool construct(std::span<const any_t> args, void* out_slot) const;
        /// @}

        /// @name Equality comparison
        /// @{
        /// @brief Whether two handles name the same constructor row.
        /// @param lhs Left operand.
        /// @param rhs Right operand.
        /// @return true when the declaring meta-object and the slot match.
        /// @note noexcept. Constructors are never inherited, so two equal handles always name the
        ///       same row of the same table.
        friend bool operator==(const meta_constructor_t& lhs, const meta_constructor_t& rhs) noexcept;
        /// @}

    private:
        /// @brief The constructor_infos row this handle names, or nullptr when it has none.
        /// @return The row inside the declaring meta-object's table; nullptr for an invalid handle
        ///         and for a stale out-of-range slot.
        /// @note noexcept. The one table read every accessor goes through: meta_constructor_t is a
        ///       friend of meta_object_t and reads the private table directly instead of forwarding
        ///       one query per column.
        const detail::constructor_info_t* entry() const noexcept;

        /// @brief The declaring meta-object; nullptr means an invalid handle.
        const meta_object_t* _owner = nullptr;

        /// @brief The constructor_infos slot inside _owner's table; index_npos for an invalid handle.
        index_t _index = index_npos;
    };
}

#endif
