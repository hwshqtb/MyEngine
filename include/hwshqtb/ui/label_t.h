#ifndef HWSHQTB__UI__LABEL_T_H
#define HWSHQTB__UI__LABEL_T_H

/// @file label_t.h
/// @brief label_t: one line of static text (the QLabel subset of the ui layer).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/widget_t.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    /// @brief Where a label's TEXT sits horizontally inside the label's own rectangle.
    /// @note This is not the item_t layout alignment: it arranges glyphs inside the label, while
    ///       item_t::alignment() places the label itself inside a layout cell. The two axes and the two
    ///       decisions are independent, which is why the accessors are spelled differently
    ///       (text_alignment() / set_text_alignment() vs alignment() / set_alignment()).
    enum struct text_alignment_enum : std::uint8_t {
        /// @brief Start at the left padding.
        LEFT,
        /// @brief Centre between the two paddings.
        CENTER,
        /// @brief End at the right padding.
        RIGHT,
    };

    /// @brief A static text label.
    ///
    /// @par Not interactive
    /// A label is a passive widget: it is not focusable (its focus policy stays no_focus), it
    /// consumes no interaction (it has no interact() override, so the item default returns false)
    /// and it declares no User property — there is no per-frame result to report.
    ///
    /// @par Text
    /// set_text() replaces the text and emits text_changed() when the value really changes; text() is
    /// the reflected `text` property. The text is drawn as ONE line: the style centres a single glyph
    /// cell vertically, and an embedded '\n' is laid out by the painter but not measured by the
    /// label's size hint (multi-line labels are out of scope).
    ///
    /// @par Size
    /// size_hint() is the advance-based text extent plus the style's padding on all four sides; the
    /// text alignment only decides where the text is drawn inside the rectangle the layout gives it.
    ///
    /// @par Text alignment versus layout alignment
    /// Two different questions that must not be confused: text_alignment() / set_text_alignment()
    /// place the text INSIDE this label, while the inherited item_t::alignment() /
    /// set_alignment(alignment_t) place the whole label inside its layout cell. Both are reachable
    /// because the two accessors no longer share a name.
    ///
    /// @par Painting
    /// paint_event() draws the text through default_style() (the TEXT element), so the label names no
    /// colour and no pixel value.
    ///
    /// @par Image (QLabel::setPixmap)
    /// A label shows ONE thing: text OR an image. set_image() takes an image and clears the text;
    /// set_text() takes text back and drops the image — the same exclusivity Qt's QLabel has, and the
    /// reason there is no separate "image control" in this layer (a picture is a label's content, not a
    /// row view).
    ///
    /// @par Scaling (QLabel::setScaledContents)
    /// With scaled_contents() false (the default) the image is drawn 1:1 at the label's top-left, aligned
    /// horizontally by text_alignment(). With it true the image fills the label's rectangle, which
    /// DISTORTS the aspect ratio when the two shapes differ — exactly Qt's behaviour, kept because a
    /// caller that wants aspect-preserving fit can size the label to the image's own shape.
    class HWSHQTB_UI_EXPORT label_t: public widget_t {
        OBJECT

    public:
        /// @brief Construct a label with no visual parent (the class registry's creation path).
        /// @note The text starts empty.
        label_t();

        /// @brief Construct a label inside a retained widget tree.
        /// @param parent The visual parent; nullptr makes this a root widget.
        explicit label_t(widget_t* parent);

        /// @brief The text.
        /// @note The `text` property; the writer is set_text() and the notifier text_changed().
        [[= ::hwshqtb::metaobject::make_property("text", "set_text", "text_changed")]]
        std::string_view text() const;

        /// @brief Replace the text and emit text_changed() when it actually changes.
        /// @param text The new text; it is copied.
        /// @note Drops any image: a label shows text OR an image, never both.
        void set_text(std::string_view text);

        /// @brief Show @p image instead of text (QLabel::setPixmap).
        /// @param image The decoded image; it is OWNED by the label from here on.
        /// @note Clears the text, re-applies the layout (the hints change) and requests a repaint.
        /// @note Hidden from the meta-info: the argument is a move-only image, which the reflected method
        ///       table cannot copy, and a picture is not a scriptable property here.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void set_image(::hwshqtb::gui::image_t image);

        /// @brief Drop the image; the label goes back to showing its (empty) text.
        /// @note Requests a repaint and re-applies the layout.
        void clear_image();

        /// @brief The displayed image, or nullptr when the label shows text.
        /// @return A borrowed pointer to the owned image; valid until the next set_image()/clear_image().
        /// @note noexcept.
        const ::hwshqtb::gui::image_t* image() const noexcept;

        /// @brief Whether the label shows an image.
        /// @note noexcept.
        bool has_image() const noexcept;

        /// @brief Whether the image fills the label's rectangle (QLabel::scaledContents).
        /// @note noexcept.
        bool scaled_contents() const noexcept;

        /// @brief Scale the image to fill the rectangle, or draw it 1:1.
        /// @param scaled true fills the rectangle (distorting the aspect ratio when the shapes differ).
        /// @note Requests a repaint when it really changes.
        void set_scaled_contents(bool scaled);

        /// @brief The horizontal placement of the text inside the label (left by default).
        /// @note Not the layout alignment; that one is the inherited item_t::alignment().
        text_alignment_enum text_alignment() const;

        /// @brief Set the horizontal placement of the TEXT inside the label's own rectangle.
        /// @param alignment The new text alignment; requests a repaint when it really changes.
        /// @note This is not the layout alignment: the inherited item_t::set_alignment(alignment_t)
        ///       decides where the label sits inside its layout cell.
        void set_text_alignment(text_alignment_enum alignment);

        /// @brief The natural size: the text extent plus the style's padding on every side.
        /// @return {text width + 2 * TEXT_PADDING, text height + 2 * TEXT_VERTICAL_PADDING}.
        /// @note This is the label's OPINION. A layout asks item_t::effective_size_hint(), which
        ///       applies any fixed constraint and clamps to the minimum.
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief The smallest size the label accepts: its size_hint().
        /// @note Font derived, like the hint: the text is the content, so a layout that compresses the
        ///       label below its text clips or overlaps it instead of showing it.
        ::hwshqtb::gui::extent_t minimum_size_hint() const override;

        /// @brief Emitted when set_text() actually changes the text.
        /// @param text The new text.
        SIGNAL(void text_changed(std::string_view text) const)

    protected:
        /// @brief Draw the text through default_style().
        /// @param event The paint event; event.painter() is already translated to this widget and
        ///        clipped to rect().
        void paint_event(paint_event_t* event) override;

    private:
        /// @brief The text.
        std::string _text;
        /// @brief The horizontal placement of the text inside the rectangle.
        /// @note Distinct from the inherited item_t::_alignment, which places the label in a cell.
        text_alignment_enum _text_alignment = text_alignment_enum::LEFT;
        /// @brief The displayed image; empty when the label shows text.
        std::optional<::hwshqtb::gui::image_t> _image;
        /// @brief Whether the image fills the rectangle (QLabel::scaledContents).
        bool _scaled_contents = false;
    };
}

#endif
