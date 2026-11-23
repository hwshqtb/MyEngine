#ifndef HWSHQTB__UI__IM_PASS_T_H
#define HWSHQTB__UI__IM_PASS_T_H

/// @file im_pass_t.h
/// @brief im_pass_t: the state of ONE content-production pass of the immediate host.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/metaobject/object_pointer_t.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/group_stack_t.h"
#include "hwshqtb/ui/widget_t.h"

namespace hwshqtb::ui {
    /// @brief Everything the immediate host owns for the duration of one content-production pass.
    ///
    /// @par Why this name
    /// The object exists for one PASS, not for one widget and not for one frame: a frame may paint
    /// several items, and each immediate host contributes exactly one content-production pass to it.
    /// The `im_` prefix says which host it belongs to; the `_t` follows the project's type naming.
    ///
    /// @par Its lifetime
    /// One instance is a member of a host and is REUSED: begin() opens a pass and end() closes it, so
    /// the state written between the two calls belongs to exactly one content production. What is
    /// collected before begin() (interact() delivers while no pass is running) is the input of that
    /// upcoming pass; begin() only prepares the claim bookkeeping, it never discards it. end() consumes
    /// both the interaction batch and the visit list, and turns the visit list into the previous-pass
    /// order the Tab ring reads.
    ///
    /// The pointer snapshot is the one exception that outlives end(): it is the LAST LEVEL delivered
    /// to the host, so a later pass can settle a control without a new pointer step (see
    /// im_host_t::settle_input()).
    class HWSHQTB_UI_EXPORT im_pass_t {
    public:
        /// @brief One collected interaction and the claim that travels with it.
        /// @note The claim lives in the same entry as the interaction on purpose: two parallel arrays
        ///       drift apart as soon as an entry is erased from the middle (the Tab step is erased
        ///       before the batch is replayed), and the drift is invisible until a control claims the
        ///       wrong event.
        struct step_t {
            /// @brief The interaction exactly as interact() collected it (host-local coordinates).
            ::hwshqtb::gui::interaction_t input{};
            /// @brief Whether a control already claimed this step this pass.
            bool claimed = false;
        };

        /// @brief One control acquired in the open pass, with the id it was acquired under.
        /// @note This is the write-back log: the host turns it into im_state_store_t snapshots after
        ///       the frame handler returns, so a widget()-only configuration and a write made after
        ///       the last control() call are captured as well.
        struct access_t {
            /// @brief The group-prefixed object id the control was acquired under.
            std::string id;
            /// @brief The acquired control, guarded: a control deleted during the pass reads null.
            ::hwshqtb::metaobject::object_pointer_t<widget_t> control;
        };

        /// @brief Construct the pass state with an empty root group layer.
        im_pass_t();
        /// @brief Destroy the pass state; it owns no object, so nothing else is released.
        ~im_pass_t();

        im_pass_t(const im_pass_t&) = delete;
        im_pass_t& operator=(const im_pass_t&) = delete;
        im_pass_t(im_pass_t&&) = delete;
        im_pass_t& operator=(im_pass_t&&) = delete;

        /// @brief Open a content-production pass.
        /// @param root_width The row width of the host's own layer (a resize between passes changes
        ///        it).
        /// @note Resets the group stack to that root layer, clears the visit list, and clears the
        ///       claims of the steps that are about to be replayed. The collected interactions are
        ///       kept: they are the input of this pass.
        void begin(int root_width);

        /// @brief Close the pass.
        /// @note The visit list becomes previous(); the acquisition log is cleared (the host has
        ///       already written the state back); the interaction batch is consumed. The pointer
        ///       snapshot is NOT reset (it is the last delivered level, not a per-pass value).
        void end();

        /// @brief Whether begin() has run without the matching end().
        bool active() const noexcept;

        /// @brief Append one interaction to the batch.
        /// @param in The step, in host-local coordinates.
        void push(const ::hwshqtb::gui::interaction_t& in);

        /// @brief The number of collected steps.
        std::size_t input_count() const noexcept;

        /// @brief The step at @p index.
        /// @param index The position; the caller guarantees it is < input_count().
        step_t& input(std::size_t index);
        /// @brief The step at @p index.
        /// @param index The position; the caller guarantees it is < input_count().
        const step_t& input(std::size_t index) const;

        /// @brief Erase the step at @p index together with its claim.
        /// @param index The position; the caller guarantees it is < input_count().
        void erase_input(std::size_t index);

        /// @brief Discard every collected step (a hidden or disabled host has no pass to give them to).
        /// @note The claims go with the entries; there is nothing else to reset.
        void clear_input() noexcept;

        /// @brief Record the pointer LEVEL of a delivered mouse step.
        /// @param in The step; only mouse, mouse_down and modifiers are read.
        /// @note A key step must not call this: its mouse fields are context, and advancing the
        ///       snapshot with a key would move the last pointer position for no event.
        void record_pointer(const ::hwshqtb::gui::interaction_t& in) noexcept;

        /// @brief The last pointer position delivered to the host (host-local).
        ::hwshqtb::gui::point_t last_mouse() const noexcept;
        /// @brief The last mouse-button LEVEL delivered to the host.
        bool last_mouse_down() const noexcept;
        /// @brief The last modifier mask delivered to the host.
        std::uint32_t last_modifiers() const noexcept;

        /// @brief Record one control visited during the open pass, in call order.
        /// @param control The control the frame handler drove or configured.
        /// @note The visitor order is the input of the next pass's Tab ring; it is replaced wholesale
        ///       by end(), so a pass that visits nothing also empties the ring.
        void visit(widget_t& control);

        /// @brief Record that @p control was acquired under @p id in the open pass.
        /// @param id The group-prefixed object id.
        /// @param control The acquired control.
        /// @return true when this is the first acquisition of @p id in this pass; false when the id
        ///         was already recorded (the log keeps one entry per id).
        /// @note begin() clears the log; end() clears it after the host has written the state back.
        bool note_access(std::string_view id, widget_t& control);

        /// @brief The controls acquired in the open pass, in call order, each with its id.
        const std::vector<access_t>& accesses() const noexcept;

        /// @brief The controls visited in the PREVIOUS pass, in call order.
        /// @note Guards, not raw pointers: a control that died in between reads as null and the Tab
        ///       ring skips it.
        const std::vector<::hwshqtb::metaobject::object_pointer_t<widget_t>>& previous() const noexcept;

        /// @brief The group stack this pass draws with.
        group_stack_t& groups() noexcept;
        /// @brief The group stack this pass draws with.
        const group_stack_t& groups() const noexcept;

    private:
        /// @brief The interactions of the pass, in arrival order, each with its claim.
        std::vector<step_t> _input;
        /// @brief The last pointer position delivered to the host (host-local).
        ::hwshqtb::gui::point_t _last_mouse{};
        /// @brief The last mouse-button level delivered to the host.
        bool _last_mouse_down = false;
        /// @brief The last modifier mask delivered to the host.
        std::uint32_t _last_modifiers = 0;

        /// @brief The controls visited in the open pass, in call order.
        std::vector<::hwshqtb::metaobject::object_pointer_t<widget_t>> _visited;
        /// @brief The controls visited in the previous pass, in call order (the Tab ring).
        std::vector<::hwshqtb::metaobject::object_pointer_t<widget_t>> _previous;
        /// @brief The controls acquired in the open pass, once each, with their ids (the write-back log).
        std::vector<access_t> _accesses;

        /// @brief The cursor/prefix stack of the open pass.
        group_stack_t _groups;
        /// @brief Whether a pass is open (begin() without end()).
        bool _active = false;
    };
}

#endif
