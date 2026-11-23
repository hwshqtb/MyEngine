#ifndef HWSHQTB__UI__TAB_WIDGET_T_H
#define HWSHQTB__UI__TAB_WIDGET_T_H

/// @file tab_widget_t.h
/// @brief tab_widget_t: one page visible at a time behind a self-drawn tab bar (the QTabWidget
///        subset of the ui layer).

#include <string>
#include <string_view>
#include <vector>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/metaobject/object_pointer_t.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/widget_t.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    /// @brief A tab widget: a tab bar plus the pages it switches between.
    ///
    /// @par The pages
    /// Every page is a child item of the widget. add_tab() reparents it here and shows only the
    /// current page: the current page is laid out into the content area (the rectangle below the tab
    /// bar), every other page is set_visible(false) and therefore neither painted nor hit-tested. The
    /// widget controls page visibility and geometry — do not install a layout on the tab widget and
    /// do not place its pages yourself.
    ///
    /// @par The tab bar
    /// paint_event() draws the bar and one tab per page through default_style() (TAB_BAR and TAB; the
    /// tab's selected state is control_state_t::checked and its title is state.text). The SAME
    /// computation drives the hit test, so a click selects the tab it landed on. Tab widths follow
    /// the titles; a bar too narrow for its tabs simply clips them (no elision and no scrolling —
    /// deliberately not implemented).
    ///
    /// @par Keyboard and focus
    /// The widget is focusable (focus_policy_enum::STRONG_FOCUS). While IT holds the focus, Left and
    /// Right select the previous/next page and Home/End select the first/last one. While a control
    /// INSIDE the current page holds the focus the widget refuses every key: the page's controls are
    /// the more specific target (the same rule scroll_view_t follows).
    ///
    /// @par Ownership
    /// The widget owns its pages (the object tree deletes them). remove_tab() removes the page from
    /// the tab widget and DETACHES it (set_parent(nullptr)); the caller owns it afterwards, like Qt's
    /// QTabWidget::removeTab(). A page deleted elsewhere turns its entry's pointer null and is skipped.
    ///
    /// @par Signals
    /// current_changed() fires whenever current_index() really changes: adding the first page
    /// (-1 -> 0), selecting another page, removing the current page (the nearest one becomes current)
    /// and removing the last page (-1). It is a state notification; the widget declares no User
    /// property.
    class HWSHQTB_UI_EXPORT tab_widget_t: public widget_t {
        OBJECT

    public:
        /// @brief Construct an empty tab widget (no visual parent, the registry's creation path).
        /// @note current_index() is -1 until the first page is added; the focus policy is
        ///       focus_policy_enum::STRONG_FOCUS.
        tab_widget_t();

        /// @brief Construct a tab widget inside a retained widget tree.
        /// @param parent The visual parent; nullptr makes this a root widget.
        explicit tab_widget_t(widget_t* parent);

        /// @brief Append a page and reparent it here.
        /// @param page The page widget; nullptr and an already-added page are ignored.
        /// @param title The tab title; it is copied.
        /// @note Adding the FIRST page makes it current and emits current_changed(0); later pages
        ///       start hidden.
        void add_tab(widget_t* page, std::string_view title);

        /// @brief Remove the page at @p index and DETACH it (the caller owns it afterwards).
        /// @param index The zero-based page index; out of range is a no-op.
        /// @note When the current page is removed the nearest remaining page becomes current; removing
        ///       the last page leaves no current page (-1). current_changed() is emitted whenever the
        ///       index really changes.
        void remove_tab(int index);

        /// @brief The current page index, or -1 when there are no pages.
        int current_index() const;

        /// @brief Make @p index the current page (clamped into range).
        /// @param index The zero-based page index; with no pages the call is a no-op.
        /// @note Emits current_changed() only when the index really changes.
        void set_current_index(int index);

        /// @brief The number of pages.
        int count() const;

        /// @brief The page at @p index, or nullptr (out of range, or the page was deleted elsewhere).
        widget_t* page_at(int index) const;

        /// @brief The title of the page at @p index (empty for an out-of-range index).
        std::string_view tab_title(int index) const;

        /// @brief The area the current page occupies: the rectangle below the tab bar.
        ::hwshqtb::gui::rect_t content_rect() const;

        /// @brief The natural size: the largest page effective hint, plus the tab bar height.
        /// @note With no page the answer is {0, 0}: a page-less tab widget has no opinion.
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief The smallest size: the largest page effective_minimum_size(), plus the tab bar
        ///        height — the pages' aggregate floor plus the strip that is always there.
        /// @note With no page the answer is {0, 0}, like size_hint().
        ::hwshqtb::gui::extent_t minimum_size_hint() const override;

        /// @brief Drive the tab bar click and the keyboard.
        /// @param in The input snapshot, in this widget's local coordinates.
        /// @return true when the widget consumed the step (a click on the tab bar, or a handled key
        ///         while it holds the focus); false otherwise.
        bool interact(const ::hwshqtb::gui::interaction_t& in) override;

        /// @brief Emitted when the current page index really changes.
        /// @param index The new current index (-1 when no page is current).
        SIGNAL(void current_changed(int index) const)

    protected:
        /// @brief Draw the tab bar and the tabs through default_style().
        /// @param event The paint event; event.painter() is already translated to this widget and
        ///        clipped to rect().
        void paint_event(paint_event_t* event) override;

        /// @brief Resize the widget and re-place the current page.
        /// @param event The geometry event; the new rectangle is already stored on the item.
        /// @note The widget layer re-lays the pages out first; this then moves the current page onto
        ///       the new content rectangle and hides the others again.
        /// @note Hidden from the meta-info like the base hook.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void geometry_change_event(geometry_change_event_t* event) override;

    private:
        /// @brief One page and its tab title.
        struct tab_entry_t {
            /// @brief The page; guarded, so a page deleted elsewhere answers nullptr.
            ::hwshqtb::metaobject::object_pointer_t<widget_t> page;
            /// @brief The tab title.
            std::string title;
        };

        /// @brief The rectangle of the tab at @p index, or an empty rectangle out of range.
        ::hwshqtb::gui::rect_t tab_rect(int index) const;

        /// @brief The tab whose rectangle contains @p local, or -1.
        int tab_index_at(::hwshqtb::gui::point_t local) const;

        /// @brief The rectangle the tab bar occupies.
        ::hwshqtb::gui::rect_t tab_bar_rect() const;

        /// @brief Show and place the current page, hide every other one.
        void update_pages();

        /// @brief Whether a control inside the current page holds the keyboard focus.
        bool page_has_focus() const;

        /// @brief The pages, in tab order.
        std::vector<tab_entry_t> _tabs;
        /// @brief The current page index, or -1.
        int _current = -1;
        /// @brief Whether the pointer pressed down on the tab bar (the release then belongs here).
        bool _bar_pressed = false;
    };
}

#endif
