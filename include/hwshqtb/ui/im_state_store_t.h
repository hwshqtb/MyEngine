#ifndef HWSHQTB__UI__IM_STATE_STORE_T_H
#define HWSHQTB__UI__IM_STATE_STORE_T_H

/// @file im_state_store_t.h
/// @brief im_state_store_t: the cross-pass property state of immediate controls, keyed by object id.

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "hwshqtb/metaobject/any_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/ui/export.h"

namespace hwshqtb::ui {
    /// @brief Stores the reflected property values of immediate controls across passes, keyed by id.
    ///
    /// @par What counts as state
    /// The trait is read from the metadata, not guessed: a class's USER property is its event, and
    /// the state is the WRITABLE property whose notifier resolves to a signal with the same name as
    /// that USER property. check_box/radio link `checked` to `toggled`, slider/scroll_bar link
    /// `value` to `value_changed`, line_edit links `text` to `text_changed`. A class with no USER
    /// property, or with a USER command event that no property notifies for (push_button's
    /// `clicked`), has no state and is not snapshotted at all.
    ///
    /// A class that (against the normal model) declares several writable properties notifying the
    /// same USER event has exactly one state: the FIRST one in property order. This is deterministic
    /// and documented rather than silently picking an arbitrary entry; normal modeling never reaches
    /// it.
    ///
    /// @par Ownership
    /// The store owns copies of the values and never points at a control. That is what makes the
    /// object pool discardable: deleting a control loses no state, and a control created later under
    /// the same id is restored to the values the last pass wrote back.
    ///
    /// @par Type changes
    /// An entry is tied to the control's class. When restore() meets an object of another class, the
    /// entry is discarded and the new object keeps its defaults.
    ///
    /// @par Values
    /// A string_view value is copied into owned text, because the view a getter returns points into
    /// the control's own storage and would dangle once the control dies. Every other value is stored
    /// as the copy the property reader produced.
    ///
    /// @par Range-clamped values
    /// A value property is restored through its setter, so a setter that clamps does so against the
    /// range the control has AT RESTORE TIME. The host restores a newly created control before the
    /// caller can install a non-default range, so a stored value outside the control's DEFAULT range
    /// is clamped to it. set_range() afterwards only clamps the value it already sees; it does not
    /// bring the out-of-range value back. A caller with a non-default range must therefore set the
    /// range and write the value every pass (the external-value model). This is the accepted
    /// contract: range bounds are layout/content inputs the producing pass supplies, not state.
    class HWSHQTB_UI_EXPORT im_state_store_t {
    public:
        /// @brief Construct an empty store.
        im_state_store_t() = default;
        /// @brief Destroy the store; the snapshots are released, no control is touched.
        ~im_state_store_t() = default;

        im_state_store_t(const im_state_store_t&) = delete;
        im_state_store_t& operator=(const im_state_store_t&) = delete;
        im_state_store_t(im_state_store_t&&) = delete;
        im_state_store_t& operator=(im_state_store_t&&) = delete;

        /// @brief Record the state of @p object under @p id, replacing an earlier snapshot of that id.
        /// @param object The control whose state property is read.
        /// @param id The identity the state belongs to (the group-prefixed object name).
        /// @note A control with no linked state (no USER property, or no writable property notifying
        ///       it) records nothing and erases any earlier entry for @p id.
        /// @note A snapshot of a control of a different class replaces the old one: the entry follows
        ///       the object that was written back last, so a type change starts from the new defaults.
        /// @note A state value that cannot be read or copied leaves the earlier entry untouched (the
        ///       failure is a property of this read, not evidence that the state is gone).
        void snapshot(const ::hwshqtb::metaobject::object_t& object, std::string_view id);

        /// @brief Write the state stored under @p id back into @p object.
        /// @param object The control whose state property is written.
        /// @param id The identity the state belongs to.
        /// @return true when an entry existed and the object's class matched it; false when there was
        ///         no entry or the class differed (the entry is erased in the latter case).
        /// @note Signals are blocked for the duration: restoring a value is not a change the caller
        ///       made, and a freshly created control must not emit the same state signal a reused one
        ///       stays silent about.
        /// @note The write is skipped when the linked property no longer exists or no longer accepts
        ///       the stored value.
        bool restore(::hwshqtb::metaobject::object_t& object, std::string_view id);

        /// @brief Whether a snapshot is stored under @p id.
        /// @note noexcept; no allocation.
        bool contains(std::string_view id) const noexcept;

        /// @brief Forget the snapshot stored under @p id.
        /// @param id The identity to forget.
        /// @return true when an entry was removed; false when there was none.
        bool erase(std::string_view id);

        /// @brief Forget every snapshot.
        void clear();

        /// @brief The number of stored ids.
        /// @note noexcept.
        std::size_t size() const noexcept;

    private:
        /// @brief One stored property value.
        struct value_t {
            /// @brief The property name, as the meta-object reports it.
            std::string name;
            /// @brief The copied value of a property that is not string_view-valued.
            ::hwshqtb::metaobject::any_t value;
            /// @brief The owned text of a string_view-valued property.
            std::string text;
            /// @brief Whether @c text is the value (the property's type is std::string_view).
            bool text_value = false;
        };

        /// @brief One id's state value plus the class it was read from.
        struct snapshot_t {
            /// @brief The qualified class name of the control the value was read from.
            std::string type_name;
            /// @brief The one linked state property and its copied value.
            value_t value;
        };

        /// @brief Transparent hash, so string_view lookups allocate no key.
        struct key_hash_t {
            using is_transparent = void;

            /// @brief Hash one spelling of a key.
            std::size_t operator()(std::string_view text) const noexcept {
                return std::hash<std::string_view>{}(text);
            }
        };

        /// @brief Transparent equality, so string_view lookups allocate no key.
        struct key_equal_t {
            using is_transparent = void;

            /// @brief Compare two spellings of a key.
            bool operator()(std::string_view left, std::string_view right) const noexcept {
                return left == right;
            }
        };

        /// @brief id -> snapshot.
        std::unordered_map<std::string, snapshot_t, key_hash_t, key_equal_t> _entries;
    };
}

#endif
