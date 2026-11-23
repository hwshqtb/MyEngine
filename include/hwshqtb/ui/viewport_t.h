#ifndef HWSHQTB__UI__VIEWPORT_T_H
#define HWSHQTB__UI__VIEWPORT_T_H

/// @file viewport_t.h
/// @brief viewport_t: shows a region of one content item at a place inside itself.

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/metaobject/object_pointer_t.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/widget_t.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    /// @brief A widget that renders a REGION OF A CONTENT ITEM into a rectangle of its own.
    ///
    /// @par What a viewport is
    /// A viewport holds no scroll state and creates no scroll bars: it maps one content rectangle
    /// (source_rect(), in the content's coordinates) onto one rectangle of its own
    /// (destination_rect(), in this widget's coordinates). Everything else — scrolling, ranges,
    /// bars — belongs to a subclass.
    ///
    /// @par Ownership
    /// The content is NOT owned and is NOT reparented. A caller that wants the content in the item
    /// tree (painting order, hit testing, focus) attaches it itself; a viewport loaded from elsewhere
    /// may paint an unparented item too. The pointer is guarded, so destroying the content clears it.
    ///
    /// @par Painting
    /// paint_event() draws the SCROLL_VIEW_VIEWPORT frame and then paint_content(). The default
    /// paint_content() clips to destination_rect(), translates so that the content's source origin
    /// lands on the destination's origin, and calls content()->paint(). A subclass that draws
    /// something else overrides paint_content(). Item children (scroll bars, a header) are painted
    /// after the content, in the ordinary widget child order.
    class HWSHQTB_UI_EXPORT viewport_t: public widget_t {
        OBJECT

    public:
        /// @brief Construct an empty viewport.
        /// @param parent The visual parent; nullptr makes it a root widget.
        explicit viewport_t(widget_t* parent = nullptr);

        viewport_t(const viewport_t&) = delete;
        viewport_t& operator=(const viewport_t&) = delete;
        viewport_t(viewport_t&&) = delete;
        viewport_t& operator=(viewport_t&&) = delete;

        /// @brief The displayed item, or nullptr.
        item_t* content() const noexcept;

        /// @brief Install the item to display.
        /// @param content The content; nullptr clears it. The viewport does not own it.
        /// @note The content is not reparented: a caller that needs tree participation attaches it.
        virtual void set_content(item_t* content);

        /// @brief The shown content region.
        ::hwshqtb::gui::rect_t source_rect() const noexcept;

        /// @brief The region OF THE CONTENT that is shown, in content coordinates.
        /// @param source The shown region. The rectangle is not clipped to the content's extent.
        /// @note The rectangle's extent does not scale: only its origin selects the part of the
        ///       content that lands on the destination's origin; the destination clips the rest.
        void set_source_rect(::hwshqtb::gui::rect_t source);

        /// @brief The target rectangle (body_rect() until set_destination_rect() was called).
        ::hwshqtb::gui::rect_t destination_rect() const noexcept;

        /// @brief Where the source region lands, in this widget's coordinates.
        /// @param destination The target rectangle; it is also the content's clip.
        /// @note Until this is called, destination_rect() answers body_rect().
        void set_destination_rect(::hwshqtb::gui::rect_t destination);

        /// @brief The area of this widget that is not taken by the frame it paints and by the item
        ///        children that overlay it.
        /// @return rect() inset by the frame's border width.
        /// @note The base has no child overlays; scroll_view_t overrides this to reserve the strips
        ///       of its visible scroll bars. The frame inset is the base's own decoration: a subclass
        ///       whose content paints OVER the frame answers its own rectangle.
        virtual ::hwshqtb::gui::rect_t body_rect() const;

        /// @brief The rectangle in which @p child is shown: destination_rect() for the content,
        ///        widget_t::items_rect() for every other item child.
        /// @param child An item child of this widget.
        /// @note One rule for painting and hit testing: paint_item_children() and hit() both ask
        ///       this, so the content's pixels and the points it owns agree by construction.
        ::hwshqtb::gui::rect_t items_rect(const item_t& child) const override;

    protected:
        /// @brief Draw the frame, then paint_content() — the viewport's OWN pixels.
        /// @param event The pass; its painter is already translated to this widget and clipped to
        ///        rect(), and its damage is in this widget's coordinates.
        /// @note The item children are traversed by the framework after this returns, through item_t
        ///       children; the content is skipped because paint_content() just painted it.
        void paint_event(paint_event_t* event) override;

        /// @brief The content, which paint_event() paints through the source->destination map.
        /// @return content(), so the framework does not paint it a second time.
        const item_t* paint_skip() const noexcept override;

        /// @brief Draw the content into destination_rect().
        /// @param painter The painter, translated to this widget and clipped to rect().
        /// @note The default clips to destination_rect(), translates so that source_rect()'s origin
        ///       lands on destination_rect()'s origin, and calls content()->paint(). Subclasses that
        ///       draw something else override this.
        /// @note This is an EXTENSION POINT: it is the one seam through which a subclass replaces the
        ///       source-to-destination mapping without re-implementing the frame and the child pass.
        virtual void paint_content(::hwshqtb::gui::painter_t& painter);

    private:
        /// @brief The displayed item (guarded; not owned).
        ::hwshqtb::metaobject::object_pointer_t<item_t> _content;
        /// @brief The shown content region, in content coordinates.
        ::hwshqtb::gui::rect_t _source{};
        /// @brief The explicit destination rectangle.
        ::hwshqtb::gui::rect_t _destination{};
        /// @brief Whether _destination was installed; false means "use body_rect()".
        bool _has_destination = false;
    };
}

#endif
