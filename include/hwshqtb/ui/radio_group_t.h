#ifndef HWSHQTB__UI__RADIO_GROUP_T_H
#define HWSHQTB__UI__RADIO_GROUP_T_H

/// @file radio_group_t.h
/// @brief radio_group_t: the explicit button group of the ui layer (the QButtonGroup subset) — it ties
///        radio buttons that may live under different parents into one exclusive set.

#include <cstddef>
#include <vector>

#include "hwshqtb/ui/export.h"

namespace hwshqtb::ui {
    class radio_button_t;

    /// @brief An explicit exclusive group of radio buttons (Qt's QButtonGroup, minimal form).
    ///
    /// @par Why a plain class
    /// A group is not a visual element and owns nothing in the object tree: it is a plain class the
    /// caller keeps alive, exactly like a layout or a router. It does NOT own its buttons (Qt's
    /// QButtonGroup does not either).
    ///
    /// @par Lifetime
    /// The group and its buttons detach from each other in both directions: a destroyed button leaves
    /// the group, and a destroyed group clears the group pointer of every remaining button (which then
    /// falls back to its same-parent automatic exclusivity). A button belongs to at most ONE group:
    /// adding it to another group removes it from the previous one.
    ///
    /// @par Exclusivity
    /// While the group is exclusive (the default), checking one member clears the other members, each
    /// of which emits toggled(false). A member of a group takes no part in the same-parent automatic
    /// exclusivity of radio_button_t: the explicit group always wins over the automatic one.
    class HWSHQTB_UI_EXPORT radio_group_t final {
    public:
        /// @brief Construct an empty group.
        /// @param exclusive Whether the group enforces exclusivity (true by default).
        explicit radio_group_t(bool exclusive = true);

        /// @brief Detach every remaining button from this group (the buttons themselves survive).
        ~radio_group_t();

        radio_group_t(const radio_group_t&) = delete;
        radio_group_t& operator=(const radio_group_t&) = delete;
        radio_group_t(radio_group_t&&) = delete;
        radio_group_t& operator=(radio_group_t&&) = delete;

        /// @brief Add @p button to the group, taking it away from any previous group.
        /// @param button The button to add; nullptr is ignored.
        /// @note When the group is exclusive and @p button is already checked, the other checked
        ///       members are cleared (each emits toggled(false)). Adding an unchecked button changes
        ///       no state.
        void add_button(radio_button_t* button);

        /// @brief Remove @p button from the group.
        /// @param button The button to remove; a button of another group (or nullptr) is ignored.
        /// @note The button keeps its checked state; it is only no longer managed by this group.
        void remove_button(radio_button_t* button);

        /// @brief The first button of the group that is checked, or nullptr.
        /// @note With exclusivity off several buttons may be checked; this answers the FIRST one in
        ///       membership order.
        radio_button_t* checked_button() const;

        /// @brief Whether the group enforces exclusivity.
        bool is_exclusive() const;

        /// @brief Enable or disable the group's exclusivity.
        /// @param exclusive The new flag.
        /// @note Turning exclusivity off keeps the current checked states (two members may then be
        ///       checked at once); turning it on does not clear them either, the next selection does.
        void set_exclusive(bool exclusive);

        /// @brief The number of buttons in the group.
        std::size_t button_count() const;

    private:
        /// @brief The member buttons, in insertion order; the group does NOT own them.
        std::vector<radio_button_t*> _buttons;
        /// @brief Whether selecting one member clears the others.
        bool _exclusive = true;

        /// @brief The radio buttons read the member list to clear their group peers.
        friend class radio_button_t;
    };
}

#endif
