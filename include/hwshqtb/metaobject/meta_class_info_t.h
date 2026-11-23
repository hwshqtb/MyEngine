#ifndef HWSHQTB__METAOBJECT__META_CLASS_INFO_T_H
#define HWSHQTB__METAOBJECT__META_CLASS_INFO_T_H

/// @file meta_class_info_t.h
/// @brief A class-level key/value pair (the QMetaClassInfo equivalent).

#include "basic_def.h"
#include "export.h"

#include <string_view>

namespace hwshqtb::metaobject {
    class meta_object_t;

    namespace detail {
        /// @brief The compile-time class-info row (defined in meta_info_t.h).
        struct class_info_t;
    }

    /// @brief A class-level key/value pair read from a meta-object (QMetaClassInfo equivalent).
    ///
    /// A lightweight view: it stores the declaring meta-object and the entry's local index inside
    /// that declaration, exactly like QMetaClassInfo. Use meta_object_t::class_info() to obtain one.
    /// The pair itself lives in the meta_info_t the declaring meta-object owns by value, so the
    /// handle stays valid as long as that meta-object does.
    class HWSHQTB_EXPORT meta_class_info_t {
    public:
        /// @brief Construct an invalid instance.
        /// @note noexcept.
        meta_class_info_t() noexcept = default;

        /// @brief Whether this refers to a real entry.
        /// @return true when the owner is set and the index is in range.
        /// @note noexcept.
        bool is_valid() const noexcept;

        /// @brief The key (Q_CLASSINFO's first argument).
        /// @return The key, or an empty view when invalid.
        std::string_view name() const;

        /// @brief The value (Q_CLASSINFO's second argument).
        /// @return The value, or an empty view when invalid.
        std::string_view value() const;

        /// @brief The index inside the declaring meta-object's table (QMetaClassInfo::index).
        /// @note noexcept.
        index_t index() const noexcept;

        /// @brief The meta-object that declares the entry.
        /// @note noexcept. The DECLARING one, never the chain head: `index()` is local to it.
        const meta_object_t* owner() const noexcept;

    private:
        friend class meta_object_t;

        /// @brief Construct from the declaring meta-object and a local index.
        /// @param owner The meta-object that declares the entry (not the chain root).
        /// @param index The index inside @p owner's own table.
        /// @note noexcept. Private: meta_object_t::class_info() is the only entry point.
        meta_class_info_t(const meta_object_t* owner, index_t index) noexcept;

        /// @brief The class-info row this handle points at, or nullptr.
        /// @return The row inside the declaring meta-object's class_info_infos; nullptr for an
        ///         invalid handle or an out-of-range slot.
        /// @note noexcept. Reads the declaring meta-object's private table directly: meta_class_info_t
        ///       is a friend of meta_object_t.
        const detail::class_info_t* aimed_info() const noexcept;

        /// @brief The declaring meta-object (nullptr when invalid).
        const meta_object_t* _owner = nullptr;
        /// @brief The index inside that declaration (index_npos when invalid).
        index_t _index = index_npos;
    };
}

#endif
