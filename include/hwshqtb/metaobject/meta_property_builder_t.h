#ifndef HWSHQTB__METAOBJECT__META_PROPERTY_BUILDER_T_H
#define HWSHQTB__METAOBJECT__META_PROPERTY_BUILDER_T_H

/// @file meta_property_builder_t.h
/// @brief The mutable handle of one run-time built property (QMetaPropertyBuilder equivalent).

#include "meta_method_builder_t.h"
#include "export.h"

#include <string_view>

namespace hwshqtb::metaobject {
    class meta_object_builder_t;

    /// @brief The mutable handle of one property row of a meta_object_builder_t
    ///        (QMetaPropertyBuilder equivalent).
    ///
    /// @par Shape
    /// `{owner*, index}`, exactly like meta_property_t: the handle owns nothing and every accessor
    /// forwards to the row its builder currently holds. A copy names the same row.
    ///
    /// @par Capabilities are value-derived, exactly as in a built table
    /// meta_property_t derives every capability from what the row can actually do (a writer, a
    /// reseter, a notifier; constant is "no writer and no notifier"), and the property row has no
    /// capability flag columns. So:
    ///  - a capability is RECORDED by its thunk (set_reader / set_writer / set_reseter) or by
    ///    set_notifier();
    ///  - the matching `set_*` bool setter therefore means "confirm" (a no-op when the thunk is
    ///    there) or "clear" (drops the thunk); switching a capability ON without the thunk behind it
    ///    is refused with std::invalid_argument rather than producing a table that reports a
    ///    capability it cannot honour.
    /// The traits have flags of their own: set_user(), set_required(), set_hidden() and set_final()
    /// toggle them, and the matching is_*() queries read them back. Their COMBINATIONS are validated
    /// by build() — Required+Hidden, two User properties and a Final override of a base property are
    /// refused there — so a setter never rejects a value it can store.
    ///
    /// @note Never default-constructed: only the owning builder creates one.
    /// @sa meta_object_builder_t
    /// @sa meta_property_t
    class HWSHQTB_EXPORT meta_property_builder_t {
    public:
        /// @name Identity and metadata
        /// @{
        /// @brief The row's index inside its owner (the coordinate remove_property() takes).
        /// @note noexcept. Rows shift down after a removal, so re-take a handle you kept.
        index_t index() const noexcept;

        /// @brief The property name (the table key).
        /// @return The name; empty for a stale handle.
        /// @note noexcept, no allocation.
        std::string_view name() const noexcept;

        /// @brief The property type.
        /// @return The IDENTITY spelling of the type text add_property() was given; empty for a stale
        ///         handle.
        /// @note noexcept, no allocation. Resolved the way the parser resolves a parameter (the type
        ///       registry lookup puts the base on the identity spelling and keeps the cvref affixes),
        ///       so a run-time built column spells a type exactly as a reflected one does.
        std::string_view type() const noexcept;
        /// @}

        /// @name Capabilities
        /// @{
        /// @brief Whether the pending row has a reader thunk (the handle's CONFIGURATION state).
        /// @note noexcept. This reports whether set_reader() has recorded a thunk yet — it is not a
        ///       capability of a built row: a built property is ALWAYS readable, because build()
        ///       refuses a row whose reader is missing (Qt's Q_PROPERTY requires a READ accessor),
        ///       which is also why meta_property_t has no readable() query at all.
        bool is_readable() const noexcept;

        /// @brief Whether the row can write (it has a writer thunk).
        /// @note noexcept.
        bool is_writable() const noexcept;

        /// @brief Whether the row can reset (it has a reseter thunk).
        /// @note noexcept.
        bool is_resettable() const noexcept;

        /// @brief Whether the row is constant: no writer and no notifier.
        /// @note noexcept. The same rule meta_property_t::is_constant() applies; there is no flag to
        ///       read.
        bool is_constant() const noexcept;
        /// @}

        /// @name Traits
        /// @{
        // The four bits of the row's trait column, in the order property_trait_flag declares them.
        // Each query is noexcept and answers false on a stale row; each setter stores the bit as
        // given. build() is where the combinations are judged, never the setter.

        /// @brief Whether the row carries the User trait.
        /// @note noexcept. At most one property per class may carry it; build() enforces that rule,
        ///       like the compile-time path does.
        bool is_user() const noexcept;

        /// @brief Whether the row carries the Required trait.
        /// @note noexcept. A property cannot be both Required and Hidden; build() enforces it.
        bool is_required() const noexcept;

        /// @brief Whether the row carries the Hidden trait.
        /// @note noexcept. A property cannot be both Required and Hidden; build() enforces it.
        bool is_hidden() const noexcept;

        /// @brief Whether the row carries the Final trait.
        /// @note noexcept. A property of a derived class must not redeclare a same-named Final
        ///       property of any base class; build() walks the chain and enforces it.
        bool is_final() const noexcept;
        /// @}

        /// @name Capability setters
        /// @{
        /// @brief Confirm or clear the read capability.
        /// @param enabled true confirms (refused with std::invalid_argument when no reader thunk is
        ///        recorded — record one with set_reader()); false clears the reader thunk.
        /// @note A no-op on a stale handle.
        void set_readable(bool enabled);

        /// @brief Confirm or clear the write capability.
        /// @param enabled true confirms (refused when no writer thunk is recorded); false clears the
        ///        writer thunk.
        /// @note A no-op on a stale handle.
        void set_writable(bool enabled);

        /// @brief Confirm or clear the reset capability.
        /// @param enabled true confirms (refused when no reseter thunk is recorded); false clears the
        ///        reseter thunk.
        /// @note A no-op on a stale handle.
        void set_resettable(bool enabled);

        /// @brief Make the row constant (true) or restore the default state (false).
        /// @param enabled true clears the writer thunk and the notifier, which is exactly what makes
        ///        a row constant; false is accepted only when the row already has a writer or a
        ///        notifier (a setter cannot invent an accessor), and is refused with
        ///        std::invalid_argument otherwise.
        /// @note A no-op on a stale handle.
        void set_constant(bool enabled);
        /// @}

        /// @name Trait setters
        /// @{
        // A trait is always representable, so these never refuse a combination: the row records what
        // the caller says and build() reports the rules (Required+Hidden, a second User, a Final
        // override) with std::invalid_argument, exactly like the compile-time pass.
        // All four throw std::invalid_argument when the row they name is gone (the stale-handle rule
        // every mutating entry point of this family follows).

        /// @brief Set or clear the User trait.
        /// @param enabled The trait's new state.
        /// @note Always representable, so this one never refuses a VALUE.
        /// @throws std::invalid_argument when the handle is stale.
        void set_user(bool enabled);

        /// @brief Set or clear the Required trait.
        /// @param enabled The trait's new state.
        /// @throws std::invalid_argument when the handle is stale.
        void set_required(bool enabled);

        /// @brief Set or clear the Hidden trait.
        /// @param enabled The trait's new state.
        /// @throws std::invalid_argument when the handle is stale.
        void set_hidden(bool enabled);

        /// @brief Set or clear the Final trait.
        /// @param enabled The trait's new state.
        /// @throws std::invalid_argument when the handle is stale.
        void set_final(bool enabled);
        /// @}

        /// @name Accessor thunks
        /// @{
        /// @brief Set the reader: the thunk that writes the value into params[0].
        /// @param caller The thunk, or nullptr to clear it (which clears the read capability too).
        /// @note generic_call convention: params[0] is the destination.
        /// @note A no-op on a stale handle.
        void set_reader(detail::generic_call caller);

        /// @brief Set the writer: the thunk that writes params[1] into the object.
        /// @param caller The thunk, or nullptr to clear it (which clears the write capability too).
        /// @note A no-op on a stale handle.
        void set_writer(detail::generic_call caller);

        /// @brief Set the reseter: the thunk that restores the default value.
        /// @param caller The thunk, or nullptr to clear it (which clears the reset capability too).
        /// @note A no-op on a stale handle.
        void set_reseter(detail::generic_call caller);
        /// @}

        /// @name Notifier and enum
        /// @{
        /// @brief Name the signal emitted when the value changes (QMetaPropertyBuilder::
        ///        setNotifySignal).
        /// @param method The signal row of the SAME builder; its final slot is resolved at build()
        ///        time, so removing an earlier method cannot leave this pointing at the wrong entry.
        /// @note The signal becomes the row's notifier, which is what makes the property
        ///       notifiable — and, because bindability is value-derived, bindable.
        /// @note A no-op on a stale handle.
        void set_notifier(meta_method_builder_t method);

        /// @brief Record the enum this property's type names.
        /// @param enum_name The simple name of the enum declared by the same class.
        /// @param is_flags Whether the type is a flag_t<E> wrapper (the enum name still names E).
        /// @note Feeds meta_property_t::is_enum_type() / is_flag_type() / enumerator(); a run-time
        ///       table has no reflection to read that fact from, so it is recorded explicitly.
        /// @note A no-op on a stale handle.
        void set_enum(std::string_view enum_name, bool is_flags);
        /// @}

    private:
        friend class meta_object_builder_t;

        /// @brief Construct the handle of one row of @p owner.
        /// @param owner The builder that owns the row; it must outlive the handle.
        /// @param index The row index inside that builder.
        meta_property_builder_t(meta_object_builder_t* owner, index_t index) noexcept;

        /// @brief Read one trait bit of this handle's row.
        /// @param trait The bit to test.
        /// @return true when the row carries @p trait; false for a stale handle.
        /// @note noexcept. The shared body of the four is_*() trait queries.
        bool trait(property_trait_flag trait) const noexcept;

        /// @brief Store one trait bit of this handle's row.
        /// @param trait The bit to change.
        /// @param enabled The bit's new state.
        /// @note The shared body of the four set_*() trait setters.
        /// @throws std::invalid_argument on a stale handle.
        void set_trait(property_trait_flag trait, bool enabled);

        /// @brief The builder that owns the row; never null: only the owning builder creates a handle.
        meta_object_builder_t* _owner = nullptr;

        /// @brief The row index inside @p _owner.
        index_t _index = index_npos;
    };
}

#endif
