#ifndef HWSHQTB__METAOBJECT__META_ENUM_H
#define HWSHQTB__METAOBJECT__META_ENUM_H

/// @file meta_enum_t.h
/// @brief The enum metadata handle meta_enum_t (modelled on QMetaEnum).

#include "basic_def.h"
#include "export.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hwshqtb::metaobject {
    class object_t;
    class meta_object_t;
    class meta_info_t;

    namespace detail {
        /// @brief The compile-time enum row (defined in meta_info_t.h).
        struct enum_info_t;
    }

    /// @brief Enum metadata handle (QMetaEnum equivalent): bidirectional conversion between enum
    ///        names and values.
    ///
    /// @par Where handles come from
    /// From @c meta_object_t::enumeration(...). When the name does
    /// not exist an [invalid handle] (is_valid() == false) is returned and nothing is thrown - the
    /// same contract as meta_property_t.
    ///
    /// @par Coverage
    /// Only enums **declared inside a type** are covered (matching Qt's Q_ENUM written inside a
    /// class); namespace-scope enums are out of scope. Inheritance is covered by meta_object_t
    /// walking the super_class() chain (parents first).
    ///
    /// @par Division of labour with flag_t
    /// @c flag_t\<E\> is the **value layer** typed wrapper (compile-time bit operations); this view
    /// is the **metadata layer** type-erased view. The underlying truth is an integer in both cases,
    /// so this view does not return @c flag_t\<E\> (E is erased) and yields a plain @c std::int64_t
    /// instead; callers that want type safety wrap it back with
    /// @c flag_t\<E\>{static_cast\<E\>(bits)}.
    ///
    /// @par Layering contract
    /// Query interfaces are noexcept and defensive: an invalid handle yields an empty view / false
    /// / nullopt / 0, never an exception and never an out-of-bounds access. Only value_to_keys
    /// allocates (it returns a std::string).
    ///
    /// @note A handle only refers to the compile-time enum table and owns no object; the same handle
    ///       can be used with any instance of the class.
    /// @sa object_t::enumeration
    /// @sa flag_t
    class HWSHQTB_EXPORT meta_enum_t {
    public:
        /// @brief Default construction yields an invalid handle.
        meta_enum_t() noexcept = default;

        /// @name Identity and metadata
        /// @{
        /// @brief Whether the handle is valid.
        /// @return true when it holds an enum table and the slot is legal.
        /// @note noexcept.
        bool is_valid() const noexcept;

        /// @brief Get the enum name.
        /// @return For a valid handle the identifier declared inside the type; for an invalid handle
        ///         an empty view.
        /// @note noexcept, no allocation.
        std::string_view name() const noexcept;

        /// @brief Whether this is a scoped enum (enum class / enum struct).
        /// @return true if it is; false for an invalid handle.
        /// @note noexcept.
        bool is_scoped() const noexcept;

        /// @brief The name of the class declaring this enum (QMetaEnum::scope).
        std::string_view scope() const noexcept;

        /// @brief Get an enum declared inside T by name.
        /// @tparam T The class declaring the enum.
        /// @param name The enum name.
        /// @return The handle on a hit; an invalid handle otherwise.
        /// @note A type may declare several enums, so the name is required (QMetaEnum::fromType
        ///       cannot be reproduced verbatim).
        /// @note It reads the class's static facade, so the returned handle points into the DECLARING
        ///       class's table — the one that named the enum, which may be a base of T when T does not
        ///       declare it — valid for the program lifetime.
        template <class T>
        static meta_enum_t from_type(std::string_view name) {
            return enumeration_of(T::static_meta_object, name);
        }

        /// @brief Whether this is a flag enum.
        /// @return true if it is; false for an invalid handle.
        /// @note noexcept. Criterion: the enumerator values are pairwise disjoint under bitwise AND
        ///       (each value occupies its own bits). An enum containing a "combined value" (such as
        ///       All = A|B) is reported as false; an empty or single-entry enum is reported as true
        ///       (vacuous truth).
        bool is_flag() const noexcept;
        /// @}

        /// @name Enumerator traversal
        /// @{
        /// @brief Number of enumerators.
        /// @return 0 for an invalid handle.
        /// @note noexcept.
        std::size_t key_count() const noexcept;

        /// @brief Get the name of the @p index -th enumerator.
        /// @param index Zero-based.
        /// @return The name on a hit; an empty view when out of range or for an invalid handle.
        /// @note noexcept, no allocation.
        std::string_view key(std::size_t index) const noexcept;

        /// @brief Get the value of the @p index -th enumerator.
        /// @param index Zero-based.
        /// @return The value on a hit; 0 when out of range or for an invalid handle.
        /// @note noexcept.
        std::int64_t value(std::size_t index) const noexcept;
        /// @}

        /// @name Name <-> value (exact match)
        /// @{
        /// @brief Value -> name (exact match, QMetaEnum::valueToKey).
        /// @param value The value to look up.
        /// @return The enumerator name on a hit; an empty view when there is no match or the handle
        ///         is invalid.
        /// @note noexcept, no allocation.
        std::string_view value_to_key(std::int64_t value) const noexcept;

        /// @brief Name -> value (exact match, QMetaEnum::keyToValue).
        /// @param key The enumerator name.
        /// @return The value on a hit; nullopt when there is no match or the handle is invalid.
        /// @note noexcept.
        std::optional<std::int64_t> key_to_value(std::string_view key) const noexcept;
        /// @}

        /// @name Flag strings (bitwise interpretation, Qt's valueToKeys / keysToValue)
        /// @{
        /// @brief Value -> combined key string (QMetaEnum::valueToKeys).
        /// @param value The bit combination to expand.
        /// @return Qt's flag string: enumerators are visited in reverse declaration order; each
        ///         entry whose bits are all still set is emitted and its bits are cleared, so a
        ///         combined value such as Dialog = Window|0x2 does not let Window appear again. An
        ///         exact match against @p value wins even after earlier entries consumed bits; a
        ///         zero-valued enumerator is emitted for a zero @p value. Parts are joined in
        ///         declaration order with @c '|'. When nothing matches: an empty string. An invalid
        ///         handle yields an empty string.
        /// @note Comparison happens at the enum's underlying width (recorded when the table is
        ///       built), so an int-backed enumerator such as INT_MIN matches both 0x80000000 and
        ///       -2147483648.
        /// @note On a non-flag enum this is still interpreted bitwise and the result may be
        ///       meaningless.
        std::string value_to_keys(std::int64_t value) const;

        /// @brief Combined key string -> value (QMetaEnum::keysToValue).
        /// @param keys A combined key string such as @c "A|B".
        /// @return The bitwise OR of every segment's value when all segments match; nullopt for an
        ///         empty string, an empty segment (such as @c "A||B"), any unmatched segment, or an
        ///         invalid handle.
        /// @note noexcept (no allocation inside).
        std::optional<std::int64_t> keys_to_value(std::string_view keys) const noexcept;
        /// @}

    private:
        friend class object_t;
        friend class meta_object_t;

        /// @brief The non-template half of from_type(): resolve @p name through the meta-object's chain.
        /// @param meta The declaring class's meta-object (its own table outlives the handle).
        /// @param name The enum name.
        /// @return The handle on a hit; an invalid handle otherwise.
        static meta_enum_t enumeration_of(const meta_object_t& meta, std::string_view name);

        /// @brief Construct a valid handle from a declaring meta-object and a local slot.
        /// @param owner The meta-object that declares the enum.
        /// @param index The slot inside that meta-object's enum_infos.
        meta_enum_t(const meta_object_t* owner, index_t index) noexcept;

        /// @brief The enum row this handle points at, or nullptr.
        /// @return The row inside the declaring meta-object's enum_infos; nullptr for an invalid
        ///         handle or an out-of-range slot.
        /// @note noexcept. Reads the declaring meta-object's private table directly: meta_enum_t is a
        ///       friend of meta_object_t, so the row is one lookup away instead of a facade call.
        const detail::enum_info_t* aimed_info() const noexcept;

        /// @brief The meta-object that declares the enum; nullptr means an invalid handle.
        const meta_object_t* _owner = nullptr;

        /// @brief The slot inside enum_infos; index_npos means an invalid handle.
        index_t _index = index_npos;
    };
}

#endif
