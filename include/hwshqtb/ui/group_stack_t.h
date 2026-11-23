#ifndef HWSHQTB__UI__GROUP_STACK_T_H
#define HWSHQTB__UI__GROUP_STACK_T_H

/// @file group_stack_t.h
/// @brief group_stack_t: the procedural layout cursor — the layout_t dual.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/ui/export.h"

namespace hwshqtb::ui {
    /// @brief The cursor and group stack a procedural layout drives by hand.
    ///
    /// This is the DUAL of layout_t: layout_t arranges a parent's item children by an algorithm and
    /// never sees a callback, while group_stack_t is the tool a frame handler uses to place things it
    /// creates one by one. It starts at a root layer, stacks rows from a cursor, and can push nested
    /// groups that add their own origin, cursor and object-name prefix.
    ///
    /// @par Zero dependencies
    /// The stack knows only geometry and strings: no item, widget, control, input or painter. Painting
    /// a group (its clip and translation) is the caller's job; this type only owns the arithmetic and
    /// the name prefix, so it stays testable and reusable outside the immediate host.
    ///
    /// @par Coordinates
    /// A layer's cursor is in that layer's own coordinates. begin_group() records the group's origin
    /// relative to the host and starts a fresh cursor at (0, 0), so a control placed inside a group has
    /// group-local geometry; a caller that maps a host-local point into a control must subtract the
    /// group origin first (layer_t::origin is exactly that accumulated origin).
    class HWSHQTB_UI_EXPORT group_stack_t {
    public:
        /// @brief One coordinate system of the stack.
        struct layer_t {
            /// @brief The layer origin, in host coordinates.
            ::hwshqtb::gui::point_t origin{};
            /// @brief The stacking cursor, in this layer's coordinates.
            ::hwshqtb::gui::point_t cursor{};
            /// @brief The row width of this layer.
            int width = 0;
            /// @brief The object-name prefix of this layer ("a/b/", empty at the root layer).
            std::string id_prefix;
        };

        /// @brief Construct the stack with one empty root layer.
        group_stack_t();
        /// @brief Destroy the stack; no object is referenced, so nothing is released.
        ~group_stack_t();

        group_stack_t(const group_stack_t&) = delete;
        group_stack_t& operator=(const group_stack_t&) = delete;
        group_stack_t(group_stack_t&&) = delete;
        group_stack_t& operator=(group_stack_t&&) = delete;

        /// @brief Discard every layer and start a new root layer of width @p width.
        /// @param width The row width of the root layer.
        void reset(int width);

        /// @brief The number of layers; 1 at the root, one more per open group.
        std::size_t depth() const noexcept;

        /// @brief The layer new rows and names are placed in.
        const layer_t& layer() const noexcept;

        /// @brief The current layer's stacking cursor.
        ::hwshqtb::gui::point_t cursor() const noexcept;

        /// @brief Move the current layer's cursor.
        /// @param cursor The new cursor, in the current layer's coordinates.
        void set_cursor(::hwshqtb::gui::point_t cursor);

        /// @brief Set the root layer's row width; ignored while a group is open.
        /// @param width The new root width.
        /// @note A group's width is fixed by the rectangle it was opened with, so a resize must not
        ///       rewrite it.
        void set_root_width(int width) noexcept;

        /// @brief Reserve one row in the current layer and advance its cursor.
        /// @param height The row height; a value <= 0 is treated as 1.
        /// @return The row rectangle, in the current layer's coordinates: every row is as wide as its
        ///         layer.
        ::hwshqtb::gui::rect_t alloc_row(int height);

        /// @brief Push a group layer opened at @p area.
        /// @param area The group rectangle in the CURRENT layer's coordinates.
        /// @param id_prefix The name segment of the group; the new prefix is
        ///        `current_prefix + id_prefix + "/"`.
        /// @note Pushing is unconditional: the caller decides whether a painter is available. The new
        ///       layer's cursor starts at (0, 0), its width is `max(0, area.width())`, and its origin is
        ///       the parent origin plus @p area's top-left.
        void begin_group(::hwshqtb::gui::rect_t area, std::string_view id_prefix);

        /// @brief Pop the innermost group.
        /// @return true when a group was popped; false at the root (the root cannot be popped).
        bool end_group() noexcept;

        /// @brief The object id of @p object_name: the current prefix followed by the name.
        /// @param object_name The name a caller asked for.
        /// @note The returned string is what identifies an object in the host's pool and state store,
        ///       so the group a name was created in is part of its identity.
        std::string key_of(std::string_view object_name) const;

    private:
        /// @brief The layer stack; the front element is the root layer.
        std::vector<layer_t> _layers;
    };
}

#endif
