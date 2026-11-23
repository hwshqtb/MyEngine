/// @file style_t.cpp
/// @brief style_t: the slate palette, the pixel metrics and the element painting (see style_t.h).

#include "hwshqtb/ui/style_t.h"

#include <algorithm>
#include <cstdlib>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/font_database_t.h"
#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/painter_t.h"

namespace hwshqtb::ui {
    int style_t::pixel_metric(pixel_metric_enum metric) const noexcept {
        // The default answer is the process-wide slate style: a style that changes one metric keeps
        // every other one without restating it.
        return default_style().pixel_metric(metric);
    }

    void style_t::draw_control(control_element_enum element, ::hwshqtb::gui::painter_t& painter,
                               ::hwshqtb::gui::rect_t rect, const control_state_t& state) const {
        // Same rule on the drawing side: an element the style does not customize is drawn exactly as
        // the process-wide style draws it.
        default_style().draw_control(element, painter, rect, state);
    }

    namespace {
        // ---- palette -------------------------------------------------------------------------
        // The button colours are the ones push_button_t painted before the style existed, so moving
        // the button onto the style changed no pixel.
        /// @brief The fill of an idle button.
        constexpr ::hwshqtb::gui::color_t button_background_color{ 58, 74, 92 };
        /// @brief The fill of a button the pointer is inside.
        constexpr ::hwshqtb::gui::color_t button_hovered_color{ 74, 96, 120 };
        /// @brief The fill of a held button.
        constexpr ::hwshqtb::gui::color_t button_pressed_color{ 40, 55, 70 };
        /// @brief The fill of a disabled button.
        constexpr ::hwshqtb::gui::color_t button_disabled_color{ 50, 53, 58 };
        /// @brief The border of an idle button.
        constexpr ::hwshqtb::gui::color_t button_border_color{ 150, 165, 180 };
        /// @brief The border of a held button.
        constexpr ::hwshqtb::gui::color_t button_pressed_border_color{ 220, 226, 232 };

        /// @brief The text of an enabled control.
        constexpr ::hwshqtb::gui::color_t text_color{ 245, 248, 250 };
        /// @brief The text of a disabled control.
        constexpr ::hwshqtb::gui::color_t disabled_text_color{ 120, 125, 132 };

        /// @brief The fill of an unchecked, idle check box indicator.
        constexpr ::hwshqtb::gui::color_t indicator_background_color{ 40, 55, 70 };
        /// @brief The fill of a hovered check box indicator.
        constexpr ::hwshqtb::gui::color_t indicator_hovered_color{ 74, 96, 120 };
        /// @brief The fill of a checked check box indicator.
        constexpr ::hwshqtb::gui::color_t indicator_checked_color{ 46, 109, 164 };
        /// @brief The fill of a disabled check box indicator.
        constexpr ::hwshqtb::gui::color_t indicator_disabled_color{ 50, 53, 58 };
        /// @brief The border of an enabled check box indicator.
        constexpr ::hwshqtb::gui::color_t indicator_border_color{ 150, 165, 180 };
        /// @brief The border of a disabled check box indicator.
        constexpr ::hwshqtb::gui::color_t indicator_disabled_border_color{ 96, 102, 110 };
        /// @brief The tick of a checked, enabled check box.
        constexpr ::hwshqtb::gui::color_t check_mark_color{ 245, 248, 250 };
        /// @brief The tick of a checked, disabled check box.
        constexpr ::hwshqtb::gui::color_t disabled_check_mark_color{ 120, 125, 132 };

        /// @brief The fill of an enabled progress bar groove.
        constexpr ::hwshqtb::gui::color_t progress_groove_color{ 24, 33, 44 };
        /// @brief The fill of a disabled progress bar groove.
        constexpr ::hwshqtb::gui::color_t progress_groove_disabled_color{ 40, 43, 47 };
        /// @brief The border of an enabled progress bar groove.
        constexpr ::hwshqtb::gui::color_t progress_border_color{ 70, 84, 98 };
        /// @brief The border of a disabled progress bar groove.
        constexpr ::hwshqtb::gui::color_t progress_disabled_border_color{ 60, 64, 70 };
        /// @brief The fill of an enabled progress bar chunk.
        constexpr ::hwshqtb::gui::color_t progress_chunk_color{ 46, 109, 164 };
        /// @brief The fill of a disabled progress bar chunk.
        constexpr ::hwshqtb::gui::color_t progress_chunk_disabled_color{ 90, 96, 104 };

        /// @brief The fill of a slider groove.
        constexpr ::hwshqtb::gui::color_t slider_groove_color{ 40, 55, 70 };
        /// @brief The fill of an enabled slider handle.
        constexpr ::hwshqtb::gui::color_t slider_handle_color{ 150, 165, 180 };
        /// @brief The fill of a disabled slider handle.
        constexpr ::hwshqtb::gui::color_t slider_handle_disabled_color{ 90, 96, 104 };
        /// @brief The border of an enabled slider handle.
        constexpr ::hwshqtb::gui::color_t slider_handle_border_color{ 220, 226, 232 };

        /// @brief The colour of the focus outline.
        constexpr ::hwshqtb::gui::color_t focus_rect_color{ 255, 214, 10 };

        /// @brief The fill of a scroll bar groove.
        constexpr ::hwshqtb::gui::color_t scroll_groove_color{ 24, 33, 44 };
        /// @brief The border of an enabled scroll bar groove.
        constexpr ::hwshqtb::gui::color_t scroll_groove_border_color{ 70, 84, 98 };
        /// @brief The fill of an idle scroll bar handle.
        constexpr ::hwshqtb::gui::color_t scroll_handle_color{ 150, 165, 180 };
        /// @brief The fill of a hovered scroll bar handle.
        constexpr ::hwshqtb::gui::color_t scroll_handle_hovered_color{ 180, 192, 204 };
        /// @brief The fill of a held scroll bar handle.
        constexpr ::hwshqtb::gui::color_t scroll_handle_pressed_color{ 220, 226, 232 };
        /// @brief The fill of a disabled scroll bar handle.
        constexpr ::hwshqtb::gui::color_t scroll_handle_disabled_color{ 90, 96, 104 };
        /// @brief The border of an enabled scroll bar handle.
        constexpr ::hwshqtb::gui::color_t scroll_handle_border_color{ 220, 226, 232 };
        /// @brief The fill of a scroll view's viewport.
        constexpr ::hwshqtb::gui::color_t scroll_view_viewport_color{ 30, 42, 56 };
        /// @brief The border of a scroll view's viewport.
        constexpr ::hwshqtb::gui::color_t scroll_view_border_color{ 70, 84, 98 };

        /// @brief The fill of an unselected list row.
        constexpr ::hwshqtb::gui::color_t list_row_color{ 30, 42, 56 };
        /// @brief The fill of an unselected list row the pointer is over.
        constexpr ::hwshqtb::gui::color_t list_row_hovered_color{ 40, 55, 70 };
        /// @brief The fill of a selected list row.
        constexpr ::hwshqtb::gui::color_t list_row_selected_color{ 46, 109, 164 };
        /// @brief The fill of a selected list row the pointer is over.
        constexpr ::hwshqtb::gui::color_t list_row_selected_hovered_color{ 58, 128, 186 };
        /// @brief The outline of the current list row.
        constexpr ::hwshqtb::gui::color_t list_row_current_color{ 255, 214, 10 };

        /// @brief The fill of a table's header strip.
        constexpr ::hwshqtb::gui::color_t table_header_color{ 40, 55, 70 };
        /// @brief The bottom border of a table's header strip.
        constexpr ::hwshqtb::gui::color_t table_header_border_color{ 70, 84, 98 };
        /// @brief The grid line between two table columns.
        constexpr ::hwshqtb::gui::color_t table_grid_color{ 70, 84, 98 };
        /// @brief The outline around the current table cell.
        constexpr ::hwshqtb::gui::color_t table_cell_current_color{ 255, 214, 10 };

        /// @brief The fill of a group box.
        constexpr ::hwshqtb::gui::color_t group_box_background_color{ 30, 42, 56 };
        /// @brief The border of a group box.
        constexpr ::hwshqtb::gui::color_t group_box_border_color{ 70, 84, 98 };

        /// @brief The fill of a tab widget's tab bar.
        constexpr ::hwshqtb::gui::color_t tab_bar_color{ 24, 33, 44 };
        /// @brief The border of a tab bar / of a tab.
        constexpr ::hwshqtb::gui::color_t tab_border_color{ 70, 84, 98 };
        /// @brief The fill of a tab that is not the current one.
        constexpr ::hwshqtb::gui::color_t tab_color{ 40, 55, 70 };
        /// @brief The fill of the current tab.
        constexpr ::hwshqtb::gui::color_t tab_selected_color{ 58, 74, 92 };

        /// @brief The fill of a line edit's frame.
        constexpr ::hwshqtb::gui::color_t line_edit_background_color{ 24, 33, 44 };
        /// @brief The border of an unfocused line edit.
        constexpr ::hwshqtb::gui::color_t line_edit_border_color{ 70, 84, 98 };
        /// @brief The border of a focused line edit.
        constexpr ::hwshqtb::gui::color_t line_edit_focused_border_color{ 150, 165, 180 };
        /// @brief The selected-text highlight.
        constexpr ::hwshqtb::gui::color_t selection_color{ 46, 109, 164 };
        /// @brief The caret of a line edit.
        constexpr ::hwshqtb::gui::color_t caret_color{ 245, 248, 250 };

        // ---- drawing helpers -----------------------------------------------------------------
        /// @brief Fill a @p thickness-wide line from @p from to @p to, clipped to @p bounds.
        /// @param painter The painter to draw with.
        /// @param from The first endpoint.
        /// @param to The second endpoint.
        /// @param thickness The side of the square brush, in pixels.
        /// @param bounds The clip rectangle of the element.
        /// @note Uses only fill_rect (the painter has no line primitive); the steps are integer
        ///       Bresenham, so the same endpoints always produce the same squares.
        void draw_thick_line(::hwshqtb::gui::painter_t& painter, ::hwshqtb::gui::point_t from,
                             ::hwshqtb::gui::point_t to, int thickness,
                             ::hwshqtb::gui::rect_t bounds) {
            const int dx = to.x() - from.x();
            const int dy = to.y() - from.y();
            const int steps = std::max(std::abs(dx), std::abs(dy));
            const int offset = thickness / 2;
            if (steps == 0) {
                painter.fill_rect(::hwshqtb::gui::rect_t{ from.x() - offset, from.y() - offset,
                                                          thickness, thickness }
                                      .intersected(bounds));
                return;
            }
            for (int step = 0; step <= steps; ++step) {
                const int x = from.x() + dx * step / steps;
                const int y = from.y() + dy * step / steps;
                painter.fill_rect(::hwshqtb::gui::rect_t{ x - offset, y - offset, thickness, thickness }
                                      .intersected(bounds));
            }
        }

        /// @brief Draw the tick of a checked check box indicator.
        /// @param painter The painter to draw with.
        /// @param bounds The indicator rectangle; the tick is clipped to it.
        /// @note Two strokes: down-right to the joint, then up-right. A very small indicator cannot
        ///       hold a tick, so it is filled solid instead.
        void draw_check_mark(::hwshqtb::gui::painter_t& painter, ::hwshqtb::gui::rect_t bounds) {
            const int size = std::min(bounds.width(), bounds.height());
            if (size <= 0) {
                return;
            }
            if (size < 7) {
                painter.fill_rect(bounds);
                return;
            }
            const int thickness = std::max(1, size / 7);
            const ::hwshqtb::gui::point_t start{ bounds.x() + size * 2 / 10, bounds.y() + size * 55 / 100 };
            const ::hwshqtb::gui::point_t joint{ bounds.x() + size * 45 / 100,
                                                 bounds.y() + size * 80 / 100 };
            const ::hwshqtb::gui::point_t end{ bounds.x() + size * 85 / 100, bounds.y() + size * 22 / 100 };
            draw_thick_line(painter, start, joint, thickness, bounds);
            draw_thick_line(painter, joint, end, thickness, bounds);
        }

        /// @brief Draw @p text centred inside @p rect, with the one-line glyph cell centred vertically.
        /// @param painter The painter to draw with.
        /// @param rect The text rectangle.
        /// @param text The text; empty draws nothing.
        /// @param color The text colour.
        /// @note The baseline formula is load-bearing: push_button_t's label must land on exactly the
        ///       pixel rows it did before the style existed.
        void draw_centered_text(::hwshqtb::gui::painter_t& painter, ::hwshqtb::gui::rect_t rect,
                                std::string_view text, ::hwshqtb::gui::color_t color) {
            if (text.empty()) {
                return;
            }
            const ::hwshqtb::gui::font_metrics_t metrics{};
            const int text_width = metrics.text_extent(text).width();
            // Centre the LINE BOX vertically: its top edge plus ascent() is the baseline origin.
            // @note The box height is the resolved engine's ascent + descent, NOT font_t::glyph_height():
            //       that constant describes the built-in 5x7 face, and using it for a system face (whose
            //       line box is e.g. 24 px) pushed the baseline too low and clipped the glyph bottoms
            //       against the widget. For the built-in face the two values are equal (6 + 1 = 7), so
            //       every existing pixel stays where it was.
            const int cell_height = metrics.ascent() + metrics.descent();
            const int cell_top = rect.y() + (rect.height() - cell_height) / 2;
            painter.set_color(color);
            painter.draw_text(::hwshqtb::gui::point_t{ rect.x() + (rect.width() - text_width) / 2,
                                                       cell_top + metrics.ascent() },
                              text);
        }

        /// @brief Draw @p text at the left edge of @p rect, with the one-line glyph cell centred.
        /// @param painter The painter to draw with.
        /// @param rect The text rectangle.
        /// @param text The text; empty draws nothing.
        /// @param color The text colour.
        /// @note Unlike draw_centered_text() the text starts at rect.x(): a list row aligns its
        ///       content to the leading edge, and the caller clips it to the row.
        void draw_left_text(::hwshqtb::gui::painter_t& painter, ::hwshqtb::gui::rect_t rect,
                            std::string_view text, ::hwshqtb::gui::color_t color) {
            if (text.empty()) {
                return;
            }
            const ::hwshqtb::gui::font_metrics_t metrics{};
            // The resolved engine's line box, not the built-in face's 7 px cell (see draw_centered_text).
            const int cell_height = metrics.ascent() + metrics.descent();
            const int cell_top = rect.y() + (rect.height() - cell_height) / 2;
            painter.set_color(color);
            painter.draw_text(::hwshqtb::gui::point_t{ rect.x(), cell_top + metrics.ascent() },
                              text);
        }
    }

    namespace {
        /// @name The look margins of our text metrics
        /// Our counterpart of Qt's PM_ButtonMargin / PM_HeaderMargin / PM_TabBarTabVSpace: Qt's model is
        /// "font line height + a style margin", and the margin belongs to the STYLE, not to the font
        /// (QCommonStyle uses 2, 4 and 6 in different places). Ours are larger than Qt's because the
        /// built-in face is only 8 px tall: a 12 px button (8 + 4) would look cramped, and these values
        /// are chosen so that the built-in face keeps exactly the heights it always had (8 + 18 = 26).
        /// @{
        /// @brief Extra height of a list/tree/table row band: LINE_HEIGHT = 8 + 18 = 26 (unchanged).
        constexpr int text_line_margin = 18;
        /// @brief Extra height of a group box title band: TITLE_HEIGHT = 8 + 10 = 18 (unchanged).
        constexpr int title_band_margin = 10;
        /// @brief Extra height of a tab bar strip: TAB_HEIGHT = 8 + 16 = 24 (unchanged).
        constexpr int tab_margin = 16;
        /// @brief Extra height of a button: BUTTON_HEIGHT = 8 + 18 = 26 (unchanged).
        constexpr int button_margin = 18;
        /// @}
    }

    const ::hwshqtb::gui::font_metrics_t& default_style_t::font_metrics() const noexcept {
        // One database lookup per generation change, not one per pixel_metric() call.
        const unsigned generation = ::hwshqtb::gui::font_database_t::instance().generation();
        if (!_font_cached || _font_generation != generation) {
            _font_metrics = ::hwshqtb::gui::font_metrics_t{};
            _font_generation = generation;
            _font_cached = true;
        }
        return _font_metrics;
    }

    int default_style_t::pixel_metric(pixel_metric_enum metric) const noexcept {
        // The TEXT metrics are "the resolved face's line height + our margin" (the Qt model, with our
        // margins). The non-text metrics below keep their values: indicators, grooves, scroll bars,
        // columns and the progress bar do not depend on glyphs, so a font change must not move them.
        switch (metric) {
        case pixel_metric_enum::LINE_HEIGHT:
            return font_metrics().line_height() + text_line_margin;
        case pixel_metric_enum::TITLE_HEIGHT:
            return font_metrics().line_height() + title_band_margin;
        case pixel_metric_enum::TABLE_HEADER_HEIGHT:
            return font_metrics().line_height() + title_band_margin;
        case pixel_metric_enum::TAB_HEIGHT:
            return font_metrics().line_height() + tab_margin;
        case pixel_metric_enum::BUTTON_HEIGHT:
            return font_metrics().line_height() + button_margin;
        default:
            break;
        }
        switch (metric) {
        case pixel_metric_enum::TEXT_PADDING:
            return 8;
        case pixel_metric_enum::TEXT_VERTICAL_PADDING:
            return 2;
        case pixel_metric_enum::BORDER_WIDTH:
            return 1;
        case pixel_metric_enum::INDICATOR_SIZE:
            return 14;
        case pixel_metric_enum::INDICATOR_SPACING:
            return 6;
        case pixel_metric_enum::FOCUS_RECT_WIDTH:
            return 1;
        case pixel_metric_enum::PROGRESS_GROOVE_HEIGHT:
            return 16;
        case pixel_metric_enum::PROGRESS_BAR_WIDTH:
            return 120;
        case pixel_metric_enum::SLIDER_GROOVE_THICKNESS:
            return 4;
        case pixel_metric_enum::SLIDER_HANDLE_SIZE:
            return 12;
        case pixel_metric_enum::SLIDER_LENGTH:
            return 120;
        case pixel_metric_enum::SCROLL_BAR_THICKNESS:
            return 14;
        case pixel_metric_enum::SCROLL_BAR_MIN_HANDLE:
            return 16;
        case pixel_metric_enum::SCROLL_BAR_LENGTH:
            return 120;
        case pixel_metric_enum::SCROLL_WHEEL_STEP:
            return 16;
        case pixel_metric_enum::LINE_EDIT_WIDTH:
            return 80;
        case pixel_metric_enum::LIST_VIEW_MIN_WIDTH:
            return 60;
        case pixel_metric_enum::LIST_VIEW_WIDTH:
            return 160;
        case pixel_metric_enum::LIST_VIEW_HEIGHT:
            return 160;
        case pixel_metric_enum::TABLE_COLUMN_WIDTH:
            return 80;
        case pixel_metric_enum::TABLE_MAX_WIDTH:
            return 480;
        case pixel_metric_enum::TABLE_GRID_WIDTH:
            return 1;
        case pixel_metric_enum::TREE_INDENTATION:
            return 16;
        case pixel_metric_enum::TREE_EXPANDER_SIZE:
            return 12;
        }
        return 0; // every enumerator is covered; this is the defined answer for a forged value
    }

    void default_style_t::draw_control(control_element_enum element, ::hwshqtb::gui::painter_t& painter,
                                       ::hwshqtb::gui::rect_t rect, const control_state_t& state) const {
        switch (element) {
        case control_element_enum::BUTTON_FRAME: {
            ::hwshqtb::gui::color_t fill = button_background_color;
            if (!state.enabled) {
                fill = button_disabled_color;
            }
            else if (state.pressed) {
                fill = button_pressed_color;
            }
            else if (state.hovered) {
                fill = button_hovered_color;
            }
            painter.set_color(fill);
            painter.fill_rect(rect);
            painter.set_color(state.pressed ? button_pressed_border_color : button_border_color);
            painter.draw_rect(rect);
            break;
        }
        case control_element_enum::CHECK_INDICATOR: {
            ::hwshqtb::gui::color_t fill = indicator_background_color;
            if (!state.enabled) {
                fill = indicator_disabled_color;
            }
            else if (state.checked) {
                fill = indicator_checked_color;
            }
            else if (state.pressed) {
                fill = button_pressed_color;
            }
            else if (state.hovered) {
                fill = indicator_hovered_color;
            }
            painter.set_color(fill);
            painter.fill_rect(rect);
            painter.set_color(state.enabled ? indicator_border_color : indicator_disabled_border_color);
            painter.draw_rect(rect);
            if (state.checked) {
                painter.set_color(state.enabled ? check_mark_color : disabled_check_mark_color);
                draw_check_mark(painter, rect);
            }
            break;
        }
        case control_element_enum::RADIO_INDICATOR: {
            ::hwshqtb::gui::color_t fill = indicator_background_color;
            if (!state.enabled) {
                fill = indicator_disabled_color;
            }
            else if (state.checked) {
                fill = indicator_checked_color;
            }
            else if (state.pressed) {
                fill = button_pressed_color;
            }
            else if (state.hovered) {
                fill = indicator_hovered_color;
            }
            painter.set_color(fill);
            painter.fill_rect(rect);
            painter.set_color(state.enabled ? indicator_border_color : indicator_disabled_border_color);
            painter.draw_rect(rect);
            if (state.checked) {
                // The dot of a selected radio: a smaller square, because the painter has no circle
                // primitive (the square reads as the selected mark at this size).
                const int size = std::min(rect.width(), rect.height());
                const int inset = std::max(3, size / 3);
                const ::hwshqtb::gui::rect_t dot{ rect.x() + inset, rect.y() + inset,
                                                  size - 2 * inset, size - 2 * inset };
                if (!dot.isEmpty()) {
                    painter.set_color(state.enabled ? check_mark_color : disabled_check_mark_color);
                    painter.fill_rect(dot);
                }
            }
            break;
        }
        case control_element_enum::PROGRESS_GROOVE:
            painter.set_color(state.enabled ? progress_groove_color : progress_groove_disabled_color);
            painter.fill_rect(rect);
            painter.set_color(state.enabled ? progress_border_color : progress_disabled_border_color);
            painter.draw_rect(rect);
            break;
        case control_element_enum::PROGRESS_CHUNK:
            painter.set_color(state.enabled ? progress_chunk_color : progress_chunk_disabled_color);
            painter.fill_rect(rect);
            break;
        case control_element_enum::SLIDER_GROOVE:
            painter.set_color(slider_groove_color);
            painter.fill_rect(rect);
            break;
        case control_element_enum::SLIDER_HANDLE:
            painter.set_color(state.enabled ? slider_handle_color : slider_handle_disabled_color);
            painter.fill_rect(rect);
            if (state.enabled) {
                painter.set_color(slider_handle_border_color);
                painter.draw_rect(rect);
            }
            break;
        case control_element_enum::SCROLL_BAR_GROOVE:
            painter.set_color(scroll_groove_color);
            painter.fill_rect(rect);
            if (state.enabled) {
                painter.set_color(scroll_groove_border_color);
                painter.draw_rect(rect);
            }
            break;
        case control_element_enum::SCROLL_BAR_HANDLE: {
            ::hwshqtb::gui::color_t fill = scroll_handle_color;
            if (!state.enabled) {
                fill = scroll_handle_disabled_color;
            }
            else if (state.pressed) {
                fill = scroll_handle_pressed_color;
            }
            else if (state.hovered) {
                fill = scroll_handle_hovered_color;
            }
            painter.set_color(fill);
            painter.fill_rect(rect);
            if (state.enabled) {
                painter.set_color(scroll_handle_border_color);
                painter.draw_rect(rect);
            }
            break;
        }
        case control_element_enum::SCROLL_VIEW_VIEWPORT:
            painter.set_color(state.enabled ? scroll_view_viewport_color : progress_groove_disabled_color);
            painter.fill_rect(rect);
            painter.set_color(scroll_view_border_color);
            painter.draw_rect(rect);
            break;
        case control_element_enum::LIST_ROW: {
            ::hwshqtb::gui::color_t fill = list_row_color;
            if (!state.enabled) {
                fill = progress_groove_disabled_color;
            }
            else if (state.hovered) {
                fill = list_row_hovered_color;
            }
            painter.set_color(fill);
            painter.fill_rect(rect);
            break;
        }
        case control_element_enum::LIST_ROW_SELECTED: {
            ::hwshqtb::gui::color_t fill = list_row_selected_color;
            if (!state.enabled) {
                fill = progress_groove_disabled_color;
            }
            else if (state.hovered) {
                fill = list_row_selected_hovered_color;
            }
            painter.set_color(fill);
            painter.fill_rect(rect);
            break;
        }
        case control_element_enum::LIST_ROW_CURRENT:
            painter.set_color(list_row_current_color);
            painter.draw_rect(rect);
            break;
        case control_element_enum::LIST_TEXT:
            draw_left_text(painter, rect, state.text,
                           state.enabled ? text_color : disabled_text_color);
            break;
        case control_element_enum::TABLE_HEADER: {
            painter.set_color(state.enabled ? table_header_color : progress_groove_disabled_color);
            painter.fill_rect(rect);
            // The strip's bottom border separates the titles from the first row. The header cells of
            // one strip are drawn one per column, so the border is redrawn at the same device row by
            // each of them: the last call still leaves exactly one border.
            const int border = std::max(1, pixel_metric(pixel_metric_enum::BORDER_WIDTH));
            if (rect.height() > 0) {
                painter.set_color(table_header_border_color);
                painter.fill_rect(::hwshqtb::gui::rect_t{ rect.x(), rect.bottom() - border,
                                                          rect.width(), border });
            }
            const int padding = pixel_metric(pixel_metric_enum::TEXT_PADDING);
            draw_left_text(painter,
                           ::hwshqtb::gui::rect_t{ rect.x() + padding, rect.y(),
                                                   std::max(0, rect.width() - 2 * padding),
                                                   rect.height() },
                           state.text, state.enabled ? text_color : disabled_text_color);
            break;
        }
        case control_element_enum::TABLE_CELL: {
            const int padding = pixel_metric(pixel_metric_enum::TEXT_PADDING);
            draw_left_text(painter,
                           ::hwshqtb::gui::rect_t{ rect.x() + padding, rect.y(),
                                                   std::max(0, rect.width() - 2 * padding),
                                                   rect.height() },
                           state.text, state.enabled ? text_color : disabled_text_color);
            if (state.focused && state.enabled) {
                painter.set_color(table_cell_current_color);
                painter.draw_rect(rect);
            }
            break;
        }
        case control_element_enum::TABLE_GRID:
            painter.set_color(table_grid_color);
            painter.fill_rect(rect);
            break;
        case control_element_enum::TREE_EXPANDER: {
            ::hwshqtb::gui::color_t fill = indicator_background_color;
            if (!state.enabled) {
                fill = indicator_disabled_color;
            }
            else if (state.hovered) {
                fill = indicator_hovered_color;
            }
            painter.set_color(fill);
            painter.fill_rect(rect);
            painter.set_color(state.enabled ? indicator_border_color : indicator_disabled_border_color);
            painter.draw_rect(rect);
            const int size = std::min(rect.width(), rect.height());
            if (size < 5) {
                break; // too small for a marker: the box alone carries the state
            }
            // The marker is two bars centred in the box: the horizontal one always, the vertical one
            // only while the node is collapsed. The two forms therefore differ by exactly that bar,
            // and neither writes a colour of its own.
            const int thickness = std::max(1, size / 6);
            const int length = std::max(1, size / 2);
            painter.set_color(state.enabled ? indicator_border_color : indicator_disabled_border_color);
            painter.fill_rect(::hwshqtb::gui::rect_t{ rect.x() + (size - length) / 2,
                                                     rect.y() + (size - thickness) / 2, length,
                                                     thickness });
            if (!state.checked) {
                painter.fill_rect(::hwshqtb::gui::rect_t{ rect.x() + (size - thickness) / 2,
                                                         rect.y() + (size - length) / 2, thickness,
                                                         length });
            }
            break;
        }
        case control_element_enum::FOCUS_RECT:
            painter.set_color(focus_rect_color);
            painter.draw_rect(rect);
            break;
        case control_element_enum::GROUP_BOX_FRAME:
            painter.set_color(state.enabled ? group_box_background_color : progress_groove_disabled_color);
            painter.fill_rect(rect);
            painter.set_color(group_box_border_color);
            painter.draw_rect(rect);
            break;
        case control_element_enum::GROUP_BOX_TITLE_TEXT:
            draw_centered_text(painter, rect, state.text,
                               state.enabled ? text_color : disabled_text_color);
            break;
        case control_element_enum::TAB_BAR:
            painter.set_color(tab_bar_color);
            painter.fill_rect(rect);
            painter.set_color(tab_border_color);
            painter.draw_rect(rect);
            break;
        case control_element_enum::TAB: {
            ::hwshqtb::gui::color_t fill = state.checked ? tab_selected_color : tab_color;
            if (!state.enabled) {
                fill = progress_groove_disabled_color;
            }
            painter.set_color(fill);
            painter.fill_rect(rect);
            painter.set_color(tab_border_color);
            painter.draw_rect(rect);
            draw_centered_text(painter, rect, state.text,
                               state.enabled ? text_color : disabled_text_color);
            break;
        }
        case control_element_enum::LINE_EDIT_FRAME:
            painter.set_color(state.enabled ? line_edit_background_color : progress_groove_disabled_color);
            painter.fill_rect(rect);
            painter.set_color(state.focused ? line_edit_focused_border_color
                                            : (state.enabled ? line_edit_border_color
                                                             : progress_disabled_border_color));
            painter.draw_rect(rect);
            break;
        case control_element_enum::SELECTION:
            painter.set_color(selection_color);
            painter.fill_rect(rect);
            break;
        case control_element_enum::LINE_EDIT_CURSOR:
            painter.set_color(caret_color);
            painter.fill_rect(rect);
            break;
        case control_element_enum::TEXT:
            draw_centered_text(painter, rect, state.text,
                               state.enabled ? text_color : disabled_text_color);
            break;
        }
    }

    const style_t& default_style() noexcept {
        static const default_style_t style;
        return style;
    }
}
