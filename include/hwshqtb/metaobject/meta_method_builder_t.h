#ifndef HWSHQTB__METAOBJECT__META_METHOD_BUILDER_T_H
#define HWSHQTB__METAOBJECT__META_METHOD_BUILDER_T_H

/// @file meta_method_builder_t.h
/// @brief The mutable handle of one run-time built method (QMetaMethodBuilder equivalent).

#include "meta_info_t.h"
#include "export.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace hwshqtb::metaobject {
    class meta_object_builder_t;

    /// @brief The mutable handle of one method / signal / constructor row of a
    ///        meta_object_builder_t (QMetaMethodBuilder equivalent).
    ///
    /// @par Shape
    /// `{owner*, index}` plus which of the builder's two row spaces the index belongs to — the same
    /// shape as meta_method_t and meta_property_t. The handle owns nothing: every accessor forwards to
    /// the row the builder currently holds at @c index, so a handle is valid exactly while its builder
    /// is alive and the row is still there. remove_method() / remove_constructor() erase a row and
    /// shift every later index of THAT space down, so handles taken before a removal must be re-taken
    /// afterwards.
    ///
    /// @note The METHOD space holds methods and signals; the CONSTRUCTOR space holds constructors.
    ///       add_method() / add_signal() create the first, add_constructor() the second, and a handle
    ///       remembers which one it came from — the row lookup follows it. A constructor row has NO
    ///       method kind (method_kind_enum has no Constructor value; the built row type has no kind column),
    ///       so method_kind() is only meaningful for a row of the METHOD space.
    /// @note The two spaces become two UNRELATED built row types (detail::method_info_t /
    ///       detail::constructor_info_t); this one handle class is the builder staging facade over
    ///       both, which is why it carries a space flag and not two subclasses.
    /// @note The class is copyable (a handle is a pointer plus an index, like meta_method_t); the
    ///       copy names the same row.
    /// @note A handle is never default-constructed: only the owning builder creates one.
    /// @sa meta_object_builder_t
    /// @sa meta_method_t
    /// @sa meta_constructor_t
    class HWSHQTB_EXPORT meta_method_builder_t {
    public:
        /// @name Identity
        /// @{
        /// @brief The row's index inside its owner (the coordinate remove_method() /
        ///        remove_constructor() takes).
        /// @note noexcept. Equal to the order add_*() appended the row in ITS OWN space; after a
        ///       removal the same index names the NEXT row of that space, because the rows shift down.
        index_t index() const noexcept;

        /// @brief The method name, as the signature parser extracted it.
        /// @return The identifier before '('; empty for a stale handle. A constructor row carries the
        ///         class name, which is what "class_t(int)" spells before '('.
        /// @note noexcept, no allocation.
        std::string_view name() const noexcept;

        /// @brief The normalised signature — the table key (QMetaMethod::signature's counterpart).
        /// @return `name(type,type)` plus the canonical cvref suffix ("combine(int,double) const");
        ///         empty for a stale handle.
        /// @note noexcept, no allocation. The text is composed from the parsed name, the SAME
        ///       identity parameter spellings param_type_name(i) reports, and the member cvref, so
        ///       the key and the parameter column never disagree on an alias spelling: add_*()
        ///       with "const std::string&" stores "const std::__cxx11::basic_string<char>&" in both.
        /// @note meta_object_t::method() takes this exact key (its lookup is strict). The BUILDER's
        ///       index_of_method() runs the same registry lookup on its argument, so the alias and
        ///       canonical spellings find the row there.
        std::string_view signature() const noexcept;
        /// @}

        /// @name Return type
        /// @{
        /// @brief The declared return type.
        /// @return The identity spelling ("std::__cxx11::basic_string<char>", "const int&"); empty
        ///         when none was set (a constructor row keeps it empty).
        /// @note noexcept, no allocation.
        std::string_view return_type() const noexcept;

        /// @brief Set the return type from its spelling.
        /// @param type The type text, e.g. "void" or "const std::string&".
        /// @note The text is resolved the way the parser resolves a parameter — the type registry
        ///       lookup puts the base on the IDENTITY spelling and the top-level cvref is kept in the
        ///       spelling — so a run-time built row spells its type exactly as a reflected one does.
        ///       The row has no return-qualifier column, so the cvref lives in the spelling only.
        /// @note A no-op on a stale handle.
        void set_return_type(std::string_view type);
        /// @}

        /// @name Parameters
        /// @{
        /// @brief The number of parameters.
        /// @return The parameter count; 0 for a stale handle.
        /// @note noexcept. Fixed at add_*() time: the parameter list comes from the signature text,
        ///       which is the table key and therefore cannot be edited afterwards.
        std::size_t parameter_count() const noexcept;

        /// @brief Record the parameter names.
        /// @param names One name per parameter, in declaration order; an empty entry means "unnamed"
        ///        (which is what a signature text always yields on its own).
        /// @note The count must EQUAL parameter_count(): the names are a column of the same row, so a
        ///       different length would leave the column misaligned. A mismatch is refused with
        ///       std::invalid_argument instead of silently truncating. A no-op on a stale handle.
        void set_parameter_names(std::span<const std::string_view> names);

        /// @brief Override the per-parameter cvref qualifiers recorded from the signature text.
        /// @param qualifiers One qualifier per parameter, in declaration order.
        /// @note The text parser infers this column ("const std::string&" is Const|LVALUE_REF) and
        ///       this setter may then override it. The stored parameter TYPE SPELLING is re-assembled
        ///       from the base name and this qualifier when build() runs, so the two columns can
        ///       never disagree — "double" with Const|LVALUE_REF becomes the spelling "const double&".
        /// @note The SIGNATURE KEY is composed at add_*() time from the text's identity spellings and
        ///       is not rewritten by this override (same rule as set_qualifier()): the built row's
        ///       signature() keeps the key while param_type_name(i) reports the override. Pass the
        ///       qualifier in the signature text when the key itself should spell it.
        /// @note The count must EQUAL parameter_count(), for the same reason as the names; a
        ///       mismatch throws std::invalid_argument. A no-op on a stale handle.
        void set_parameter_qualifiers(std::span<const qualifier_t> qualifiers);
        /// @}

        /// @name Classification and function qualifiers
        /// @{
        /// @brief What kind of function this is (PLAIN_METHOD / SIGNAL / STATIC_METHOD).
        /// @note noexcept. add_method() starts a PLAIN_METHOD row and add_signal() a SIGNAL row; the
        ///       setter exists so a row can be reclassified before the snapshot.
        /// @note ONLY meaningful for a METHOD row. A constructor row has no kind at all (the built
        ///       detail::constructor_info_t has no kind column), so this accessor answers PLAIN_METHOD
        ///       for it — the default its staging row carries — and no caller should ask.
        method_kind_enum method_kind() const noexcept;

        /// @brief Reclassify the row.
        /// @param kind The new kind: PLAIN_METHOD, SIGNAL or STATIC_METHOD. There is no Constructor value
        ///        to reject — a constructor row is created by meta_object_builder_t::add_constructor()
        ///        and lives in the constructor table, whose row type has no kind column, so the
        ///        forbidden state (a Constructor-kind row in method_infos) is unrepresentable.
        /// @note STATIC_METHOD is ALLOWED on a run-time built row: the caller takes on the contract the
        ///       kind states — the row thunk must IGNORE its obj argument, exactly as a static member
        ///       function does, because a receiver-less row may be invoked with a null object. No check
        ///       is performed here: the thunk is the caller code and the table records what the caller
        ///       declared (declaration metadata, never a gate).
        /// @note A no-op on a stale handle.
        void set_method_kind(method_kind_enum kind);

        /// @brief The declared access (a hint only, exactly as in the compile-time table).
        /// @note noexcept. A run-time built row starts at method_access_enum::PUBLIC_SCOPE: no
        ///       declaration exists, and public is the honest default for a signature the caller
        ///       wrote.
        method_access_enum access() const noexcept;

        /// @brief Set the declared access.
        /// @param access The new specifier (method_access_enum::NONE means "no declared member").
        /// @note A no-op on a stale handle.
        void set_access(method_access_enum access);

        /// @brief The member function's own cvref qualifiers.
        /// @return The combination the signature's trailing suffix spelled, or the override set with
        ///         set_qualifier(); None for a stale handle.
        /// @note noexcept. This is the FUNCTION's cvref; the per-parameter cvref lives in
        ///       set_parameter_qualifiers().
        qualifier_t qualifier() const noexcept;

        /// @brief Override the member function's cvref qualifiers.
        /// @param qualifier The new combination.
        /// @note The signature key carries the suffix the TEXT spelled and is not rewritten, because
        ///       it is also what a caller looks the entry up by. Overriding the column without
        ///       spelling the suffix in add_*() therefore makes the row report a qualifier its key
        ///       does not spell; the setter exists for a caller that keys by the bare text and uses
        ///       the column as metadata.
        /// @note A no-op on a stale handle.
        void set_qualifier(qualifier_t qualifier);
        /// @}

        /// @name Annotations
        /// @{
        /// @brief Attach a text tag (the run-time counterpart of [[= make_str_tag("...")]]).
        /// @param text The text; an empty text clears the tag.
        /// @note The table has no reflection to staticise a value from, so the only shape a run-time
        ///       tag can carry is the one make_str_tag() produces: the carried type name is
        ///       "const char*" and the stored pointer is a staticised `const char*`. Read it back the
        ///       same way as the compile-time form:
        ///       `*(const char* const*)method.tag("const char*")`.
        /// @note The text is copied into process-lifetime storage, so it outlives both this handle and
        ///       the builder (a built table is never invalidated by a later edit).
        /// @note A no-op on a stale handle.
        void set_tag(std::string_view text);
        /// @}

        /// @name Invoker
        /// @{
        /// @brief Set the invoker thunk (method_info_t::caller for a method row,
        ///        constructor_info_t::caller for a constructor row).
        /// @param caller The thunk, or nullptr to clear it.
        /// @note The convention is uniform across the library: `void(void* object, void** params)`
        ///       with params[0] reserved for the return value and the arguments starting at params[1]
        ///       — the same convention the SIGNAL macro and the static meta-objects use. A method or
        ///       signal is called on an existing object; a CONSTRUCTOR is called with a null object
        ///       and receives the new instance through params[0] as a `void**`.
        /// @note A row with a null caller is present in the table but not callable: meta_method_t::
        ///       is_valid() reports false, and no thunk the library could synthesise exists for a
        ///       member it cannot see.
        /// @note A no-op on a stale handle.
        void set_caller(detail::generic_call caller);
        /// @}

    private:
        friend class meta_object_builder_t;

        /// @brief Construct the handle of one row of @p owner.
        /// @param owner The builder that owns the row; it must outlive the handle.
        /// @param index The row index inside that builder, in the space @p constructor_row selects.
        /// @param constructor_row true for a constructor row (the builder's `_constructors`),
        ///        false for a method row (its `_methods`).
        /// @note Private: a handle is created by the builder's add_*() / method() / constructor()
        ///       entry points, so it always names a row that exists at the moment it is handed out.
        meta_method_builder_t(meta_object_builder_t* owner, index_t index,
                              bool constructor_row = false) noexcept;

        /// @brief The builder that owns the row; never null: only the owning builder creates a handle.
        meta_object_builder_t* _owner = nullptr;

        /// @brief The row index inside @p _owner.
        index_t _index = index_npos;

        /// @brief Which row space @p _index belongs to: false = methods, true = constructors.
        bool _constructor_row = false;
    };
}

#endif
