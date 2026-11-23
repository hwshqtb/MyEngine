#ifndef HWSHQTB__UI__GROUP_BOX_T_H
#define HWSHQTB__UI__GROUP_BOX_T_H

/// @file group_box_t.h
/// @brief group_box_t: a titled frame whose item children are laid out in an inset content area (the
///        QGroupBox subset of the ui layer).

#include <string>
#include <string_view>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/widget_t.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    /// @brief A titled frame around a group of child widgets.
    ///
    /// @par The container
    /// The group box is a CONTAINER: its item children are arranged by the layout installed on it
    /// (set_layout()), exactly like a plain widget_t, and the layout is applied to the CONTENT AREA
    /// (the rectangle inset by the border on the sides and by the title strip at the top), not to the
    /// whole rectangle. relayout() is overridden for that; it is called automatically on every resize
    /// and child change, so the children never spill into the title strip.
    ///
    /// @par Passive
    /// The group box itself is not focusable (its focus policy stays no_focus), consumes no
    /// interaction and declares no User property: it is decoration plus layout. Its children keep
    /// their own focus and events.
    ///
    /// @par Deliberately absent: checkable
    /// Qt's QGroupBox can be checkable (a check box in the title disables its children). That is
    /// intentionally NOT implemented: it would couple the container's enabled state to a child
    /// control and needs a second state model. Use a check_box_t plus a plain group_box_t when the
    /// caller wants that behaviour.
    ///
    /// @par Painting and size
    /// paint_event() draws the frame and the title through default_style() (GROUP_BOX_FRAME and
    /// GROUP_BOX_TITLE_TEXT). size_hint() is the larger of the layout's size_hint() plus the insets and
    /// the title's width plus two paddings; minimum_size_hint() applies the same recipe to
    /// layout_t::minimum_size(), the children's aggregate floor. Without a layout the content area
    /// contributes nothing and the title alone sets the box's extent.
    class HWSHQTB_UI_EXPORT group_box_t: public widget_t {
        OBJECT

    public:
        /// @brief Construct an empty untitled group box (no visual parent, the registry's path).
        group_box_t();

        /// @brief Construct a group box inside a retained widget tree.
        /// @param parent The visual parent; nullptr makes this a root widget.
        explicit group_box_t(widget_t* parent);

        /// @brief The title.
        /// @note The `title` property; the writer is set_title() and the notifier title_changed().
        [[= ::hwshqtb::metaobject::make_property("title", "set_title", "title_changed")]]
        std::string_view title() const;

        /// @brief Replace the title and emit title_changed() when it actually changes.
        /// @param title The new title; it is copied.
        void set_title(std::string_view title);

        /// @brief The area the children are laid out in: the rectangle inset by the border and the
        ///        title strip.
        ::hwshqtb::gui::rect_t content_rect() const;

        /// @brief The natural size: max(layout hint + insets, title width + 2 * TEXT_PADDING).
        /// @note Without a layout the content contribution is {0, 0}, so the title sets the extent.
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief The smallest size: the same recipe from layout_t::minimum_size(), the children's
        ///        aggregate floor (their effective_minimum_size() plus the layout's margins).
        ::hwshqtb::gui::extent_t minimum_size_hint() const override;

        /// @brief Apply the installed layout to the CONTENT AREA (widget_t::relayout applies it to the
        ///        whole rectangle, which would overlap the title strip).
        void relayout() override;

        /// @brief Emitted when set_title() actually changes the title.
        /// @param title The new title.
        SIGNAL(void title_changed(std::string_view title) const)

    protected:
        /// @brief Draw the frame and the title through default_style().
        /// @param event The paint event; event.painter() is already translated to this widget and
        ///        clipped to rect().
        void paint_event(paint_event_t* event) override;

    private:
        /// @brief The title.
        std::string _title;
    };
}

#endif
