#ifndef HWSHQTB__METAOBJECT__META_ENUM_BUILDER_T_H
#define HWSHQTB__METAOBJECT__META_ENUM_BUILDER_T_H

/// @file meta_enum_builder_t.h
/// @brief The mutable handle of one run-time built enumeration (QMetaEnumBuilder equivalent).

#include "basic_def.h"
#include "export.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hwshqtb::metaobject {
    class meta_object_builder_t;

    /// @brief The mutable handle of one enumeration row of a meta_object_builder_t
    ///        (QMetaEnumBuilder equivalent).
    ///
    /// @par Shape
    /// `{owner*, index}`, exactly like meta_enum_t: the handle owns nothing and every accessor
    /// forwards to the row its builder currently holds. A copy names the same row.
    ///
    /// @par Enumerators
    /// add_key() appends one name/value pair, remove_key() erases one and shifts the rest down. The
    /// enumeration itself is created by meta_object_builder_t::add_enumerator() and removed with
    /// meta_object_builder_t::remove_enumerator() — the handle can edit a row, not create or destroy
    /// one.
    ///
    /// @par is_flag is recorded, not inferred
    /// The compile-time path DERIVES is_flag from the enumerator values (pairwise disjoint under
    /// bitwise AND) because it can read the real declaration. A run-time builder has no declaration
    /// to read, so — like Qt's QMetaEnumBuilder — it records the fact explicitly through
    /// set_is_flag(); the default is false.
    ///
    /// @note Never default-constructed: only the owning builder creates one.
    /// @sa meta_object_builder_t
    /// @sa meta_enum_t
    class HWSHQTB_EXPORT meta_enum_builder_t {
    public:
        /// @name Identity
        /// @{
        /// @brief The row's index inside its owner (the coordinate remove_enumerator() takes).
        /// @note noexcept. Rows shift down after a removal, so re-take a handle you kept.
        index_t index() const noexcept;

        /// @brief The enum name (the table key).
        /// @return The name; empty for a stale handle.
        /// @note noexcept, no allocation.
        std::string_view name() const noexcept;
        /// @}

        /// @name Flag classification
        /// @{
        /// @brief Whether the enumeration is recorded as a flag enum.
        /// @return The recorded fact; false for a stale handle.
        /// @note noexcept. meta_enum_t::is_flag() reads this column; nothing derives it here.
        bool is_flag() const noexcept;

        /// @brief Record whether this is a flag enum.
        /// @param is_flag The fact meta_enum_t::is_flag() will report.
        /// @note A no-op on a stale handle.
        void set_is_flag(bool is_flag);
        /// @}

        /// @name Enumerators
        /// @{
        /// @brief Append one enumerator.
        /// @param name The enumerator identifier.
        /// @param value Its value, widened to std::int64_t (the width the table stores).
        /// @note Declaration order is the table order, and value_to_key() / key_to_value() keep
        ///       working on it exactly as they do on a reflected enum.
        /// @note A no-op on a stale handle.
        void add_key(std::string_view name, std::int64_t value);

        /// @brief Number of enumerators.
        /// @return The count; 0 for a stale handle.
        /// @note noexcept.
        std::size_t key_count() const noexcept;

        /// @brief The name of the @p index -th enumerator.
        /// @param index Zero-based position in declaration order.
        /// @return The name on a hit; an empty view when out of range or for a stale handle.
        /// @note noexcept, no allocation.
        std::string_view key(std::size_t index) const noexcept;

        /// @brief The value of the @p index -th enumerator.
        /// @param index Zero-based position in declaration order.
        /// @return The value on a hit; 0 when out of range or for a stale handle.
        /// @note noexcept.
        std::int64_t value(std::size_t index) const noexcept;

        /// @brief Erase the @p index -th enumerator, shifting the later ones down.
        /// @param index Zero-based position in declaration order; out of range is a no-op.
        /// @note A no-op on a stale handle.
        void remove_key(std::size_t index);
        /// @}

    private:
        friend class meta_object_builder_t;

        /// @brief Construct the handle of one row of @p owner.
        /// @param owner The builder that owns the row; it must outlive the handle.
        /// @param index The row index inside that builder.
        meta_enum_builder_t(meta_object_builder_t* owner, index_t index) noexcept;

        /// @brief The builder that owns the row; never null: only the owning builder creates a handle.
        meta_object_builder_t* _owner = nullptr;

        /// @brief The row index inside @p _owner.
        index_t _index = index_npos;
    };
}

#endif
