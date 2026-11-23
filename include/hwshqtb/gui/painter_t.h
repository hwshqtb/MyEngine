#ifndef HWSHQTB__GUI__PAINTER_H
#define HWSHQTB__GUI__PAINTER_H

/// @file painter_t.h
/// @brief painter_t: the immediate-mode drawing API of the GUI layer (QPainter's minimal shape).

#include <string_view>
#include <vector>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/export.h"
#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/paint_device_t.h"
#include "hwshqtb/gui/paint_engine_t.h"
#include "hwshqtb/gui/region_t.h"

namespace hwshqtb::gui {
    /// @brief The pixel buffer an image draws from; declared here so draw_image() needs no include.
    class image_t;
    /// @brief Draws into a paint_device_t through that device's paint engine.
    ///
    /// @par Session
    /// begin() / end() bracket a paint pass. end() flushes the engine, which is when the queued
    /// drawing becomes visible in the device (for image_t the pixels are readable from then on). The
    /// destructor ends a session that is still open, so an early return cannot lose buffered drawing.
    /// begin() returns false when the device cannot be painted — it has no engine at all (a moved-from
    /// image, a device whose target could not be created), or the engine refused to start. Drawing
    /// while inactive — before begin(), after end(), or after a failed begin() — is ignored, exactly
    /// like a QPainter that failed to start.
    ///
    /// @par The engine belongs to the device
    /// The painter holds NO engine of its own: every call asks the device through
    /// paint_device_t::paint_engine(), which is a pure query. The device creates its engine while it is
    /// being set up, so the answer is stable within a pass; a painter never creates, replaces or
    /// destroys one.
    ///
    /// @par State handling
    /// The painter owns one engine_state_t and calls paint_engine_t::update_state() only when that
    /// snapshot is dirty, immediately before the next drawing call; drawing itself always goes
    /// through paint_engine_t::draw_rects() with an array, also for a single rectangle.
    ///
    /// @par Text
    /// draw_text() is a LAYOUT operation: the painter computes codepoints, advances and baselines and
    /// hands the engine one glyph_run_t. It never rasterises a glyph itself, so the same painter
    /// draws text through the SDL backend, through a test recording engine, or through a future CPU
    /// or GPU backend without a change (the engine seam is the point of H3).
    ///
    /// @par Geometry
    /// Coordinates are device pixels and rect_t's half-open rule applies: a fill covers
    /// [left(), right()) x [top(), bottom()), and an outline draws the one-pixel border of that pixel
    /// block (never the pixel at right() or bottom()). Empty rectangles (width() <= 0 or
    /// height() <= 0) draw nothing, and rectangles partially outside the device are clipped by the
    /// backend.
    ///
    /// @par Translation, clipping and the state stack (W1)
    /// translate() accumulates an offset that is applied to every following primitive (fill_rect,
    /// draw_rect, draw_text) until restore() — nested widgets rely on it. A backend that declares
    /// engine_feature_flag::TRANSLATE receives the accumulated offset in engine_state_t::translate(); a
    /// backend that does not (the SDL engine: SDL's renderer cannot translate) gets the offset added
    /// to the coordinates by the painter itself, which is exactly Qt's PaintEngineFeatures mechanism.
    /// set_clip() takes a rectangle in the painter's CURRENT user coordinates (i.e. after the
    /// accumulated translate), intersects it with the clip already in effect (Qt's clip stack
    /// behaviour) and hands the engine the resulting DEVICE-space rectangle; clear_clip() removes the
    /// clip. save() pushes colour + clip + translate onto an internal stack and restore() pops it.
    /// clear() is the one device-wide operation: it ignores translation and clipping (see below).
    ///
    /// @par clear() and the current colour
    /// clear() paints the whole device with the given colour but does NOT change the current colour:
    /// @code
    /// p.set_color(color_t::red_color());
    /// p.clear(color_t::white_color());   // white background
    /// p.fill_rect(r);                    // still red
    /// @endcode
    /// @note clear() deliberately ignores the accumulated translate and any clip in effect: it is a
    ///       background operation on the device, not a primitive. The painter state is restored
    ///       before the call returns, so drawing after it uses the same colour/clip/translate.
    ///
    /// @par Lifetime
    /// The device must outlive the painter. A painter is not copyable or movable; create one per
    /// paint pass instead.
    class HWSHQTB_GUI_EXPORT painter_t {
    public:
        /// @brief Create a painter for @p device.
        /// @param device The target; it must outlive this painter and be paintable.
        /// @note Asks the device for its engine, which may create it (and cache it in the device).
        explicit painter_t(paint_device_t& device);

        /// @brief End the paint pass if it is still open (see the class note on the session).
        ~painter_t();

        painter_t(const painter_t&) = delete;
        painter_t& operator=(const painter_t&) = delete;
        painter_t(painter_t&&) = delete;
        painter_t& operator=(painter_t&&) = delete;

        /// @brief Start a paint pass on the device.
        /// @return true when the engine started; false when the device cannot be painted. Calling
        ///         begin() on an already active painter returns true and keeps the session.
        bool begin();

        /// @brief Flush the engine and finish the paint pass.
        /// @return true on success; ending an inactive painter is a no-op that returns true.
        bool end();

        /// @brief Whether a paint pass is currently open.
        bool is_active() const noexcept;

        /// @brief Set the colour used by the following drawing calls.
        /// @param color The new drawing colour (alpha is honoured when the engine blends).
        void set_color(color_t color);

        /// @brief Fill the whole device with @p color without changing the current colour.
        /// @param color The background colour.
        void clear(color_t color);

        /// @brief Fill @p rect with @p color without changing the current colour (the regional form
        ///        of clear()).
        /// @param rect The area to fill, in DEVICE pixels.
        /// @param color The background colour.
        /// @note Like clear(), it ignores the accumulated translate and any clip in effect, and it
        ///       restores the painter state before returning: a partial repaint clears exactly the
        ///       damaged rectangle and leaves every other pixel of a persistent surface untouched.
        /// @note An empty rectangle fills nothing.
        void clear(rect_t rect, color_t color);

        /// @brief Fill @p rect with the current colour.
        /// @param rect The area in device pixels; an empty rectangle draws nothing.
        void fill_rect(rect_t rect);

        /// @brief Outline @p rect with a one-pixel border in the current colour.
        /// @param rect The area in device pixels; the border lies inside it and an empty rectangle
        ///        draws nothing.
        void draw_rect(rect_t rect);

        /// @brief Draw @p image into @p destination (QPainter::drawImage equivalent).
        /// @param destination Where the image lands, in device pixels. Its size is the drawn size, so a
        ///        destination larger or smaller than the image scales it (a backend that declares
        ///        engine_feature_flag::PIXMAP_TRANSFORM samples; the SDL one uses nearest sampling).
        /// @param image The image to draw; it is read during this call and never stored.
        /// @note The painter resolves the clip and the translation, so the engine receives device
        ///       coordinates and blits — it never lays an image out.
        /// @note An empty destination, a moved-from image or a null engine draws nothing.
        void draw_image(rect_t destination, const image_t& image);

        /// @brief Draw one or more lines of text in the current colour.
        ///
        /// The painter does the layout only: it decodes @p text byte by byte into codepoints,
        /// positions each one by font_metrics_t::advance(), and emits exactly ONE glyph_run_t to the
        /// engine per call (an empty string emits nothing). The engine owns the rasterisation.
        ///
        /// @param baseline_origin The baseline origin of the first line: the first glyph cell's row 0
        ///        lands at baseline_origin.y() - font_metrics_t::ascent(), row 6 (descenders) at
        ///        baseline_origin.y().
        /// @param text The text; '\n' starts a new line line_height() lower and begins again at
        ///        baseline_origin.x(). Every other byte is passed through as a codepoint: only
        ///        printable ASCII 32..126 has glyphs, anything else (controls, DEL, non-ASCII bytes)
        ///        is drawn as the replacement box. H3 is deliberately byte-oriented, not Unicode.
        /// @param font The font to lay the text out with; defaults to the only H3 face.
        /// @note The run's origin is the first GLYPH's baseline origin, which differs from
        ///       @p baseline_origin when the text starts with '\n'. Interior newlines produce gaps in
        ///       the run's positions, not separate runs.
        void draw_text(point_t baseline_origin, std::string_view text,
                       const font_t& font = font_t::default_font());

        /// @brief Translate every following primitive by @p offset.
        ///
        /// The offsets ACCUMULATE until restore(): translate(a) followed by translate(b) moves the
        /// next primitive by a + b, which is what nested widgets need (each nesting level adds its
        /// own geometry origin).
        ///
        /// @param offset The offset in device pixels.
        /// @note Whether the offset goes to the backend or is applied to the coordinates here is
        ///       decided by engine_feature_flag::TRANSLATE (see the class note). Text is laid out first
        ///       and its glyph positions are then shifted, so a translated run stays one run.
        /// @note The clip rectangle set by set_clip() is translated at the moment it is SET, not when
        ///       it is used: moving the painter afterwards does not move an existing clip (Qt's rule).
        void translate(point_t offset);

        /// @brief Intersect the current clip with @p rect and enable clipping.
        ///
        /// @param rect The clip rectangle in the painter's current (translated) coordinates; an empty
        ///        rectangle clips everything away, which is different from clear_clip().
        /// @note Nested calls INTERSECT, like Qt's clip stack: a widget that sets its own rectangle
        ///       inside a parent's clip can only ever narrow the visible area.
        /// @note The effective rectangle is stored and handed to the engine in DEVICE coordinates;
        ///       engine_state_t::translate() never applies to a clip.
        void set_clip(rect_t rect);

        /// @brief Intersect the current clip with @p region and enable clipping.
        ///
        /// @param region The clip region in the painter's current (translated) coordinates; an empty
        ///        region clips everything away.
        /// @note This is the region form of set_clip(); the same stack rules apply. The result is
        ///       reduced to the cheap rectangle form whenever it is one rectangle, so a single-rect
        ///       region costs exactly what set_clip() costs.
        /// @note An engine that declares engine_feature_flag::CLIP_REGION receives the region and
        ///       clips in one pass. An engine that does not (the only backend today) cannot clip to a
        ///       region at all, so the painter clips it to the region's BOUNDING rectangle — a
        ///       superset: the caller must split a multi-rectangle region into passes itself when it
        ///       needs pixel-exact clipping. window_t::draw_frame() is that caller.
        void set_clip_region(const region_t& region);

        /// @brief Disable clipping (a later set_clip() starts from the full device again).
        void clear_clip();

        /// @brief The clip currently in effect, in the painter's CURRENT user coordinates.
        /// @return The device-space clip with the accumulated translation subtracted again, or the
        ///         whole device when no clip is in effect. An empty rectangle means nothing can be
        ///         drawn at the current translation.
        /// @note This is the query a container uses to decide what part of a child is still visible:
        ///       it is the same rectangle the backend clips to, so a caller that paints only inside it
        ///       paints exactly what the frame can show.
        /// @note For a region clip this answers the region's bounding rectangle; ask clip_region()
        ///       for the exact area.
        /// @note noexcept.
        rect_t clip_rect() const noexcept;

        /// @brief The clip currently in effect as a region, in the current user coordinates.
        /// @return The device clip with the accumulated translation subtracted again, or the whole
        ///         device as one rectangle when no clip is in effect. Empty means nothing can be
        ///         drawn at the current translation.
        /// @note The exact form of clip_rect(): a rectangle clip answers one rectangle.
        /// @note The span of rects() in the answer is owned by the returned value.
        region_t clip_region() const;

        /// @brief Whether the device's engine declares @p feature.
        /// @param feature The capability to ask about.
        /// @return The answer the engine gave at begin(); false outside a pass.
        /// @note Queried once per pass, like engine_feature_flag::TRANSLATE, so a caller may use it
        ///       for free while painting.
        /// @note noexcept.
        bool supports(engine_feature_flag feature) const noexcept;

        /// @brief Push the current colour, clip and translation onto the state stack.
        /// @note save()/restore() must be balanced. Every save() costs one entry of a std::vector on
        ///       the painter, so a paint pass with deep widget nesting allocates only as deep as the
        ///       tree it paints.
        void save();

        /// @brief Pop the last save() and restore its colour, clip and translation.
        /// @note restore() on an empty stack is a DOCUMENTED NO-OP: it leaves the painter state
        ///       untouched and does not throw. This is the deliberate choice for the unbalanced case
        ///       (the alternative would be a debug-only assert, which hides the bug in release builds
        ///       or aborts a release paint pass); a widget tree is balanced by construction, so a
        ///       mismatched restore() cannot corrupt anything — it only fails to restore.
        void restore();

    private:
        /// @brief One saved state of the save()/restore() stack.
        /// @note Colour + clip + translation only: those are the three things a widget paint pass
        ///       changes. The engine's transform is not part of the painter API (no scale/rotate
        ///       yet), so it needs no slot here.
        /// @note The clip is one rectangle OR a region: @c clip is the rectangle (for a region clip it
        ///       is the bounding rectangle), @c clip_region is the exact area and is empty for a
        ///       rectangle clip.
        struct saved_state_t {
            color_t color;                 ///< The colour that was current.
            rect_t clip;                   ///< The effective DEVICE-space clip.
            bool clip_enabled = false;     ///< Whether the clip was enabled at all.
            point_t translate;             ///< The accumulated translation.
            region_t clip_region;          ///< The exact DEVICE-space clip when it is a region.
        };

        /// @brief Push the dirty state snapshot into the engine.
        void flush_state();

        /// @brief The device's engine, or nullptr when it has none.
        /// @return What paint_device_t::paint_engine() answers; a pure query, never creates anything.
        /// @note The painter holds no engine of its own: the engine belongs to the device, and the
        ///       painter only borrows it. Inside an active pass the answer is never nullptr (the pass
        ///       would not have started).
        /// @note noexcept.
        paint_engine_t* engine() const noexcept;

        /// @brief Re-publish the current clip (rectangle or region) into the engine snapshot.
        void publish_clip();

        /// @brief Flush the state, then draw one rectangle through the batched entry point.
        void draw_one(rect_t rect, bool fill);

        /// @brief Restore the ENGINE state (colour, clip, translation) from @p saved.
        /// @note Used by restore(); it exists because the engine snapshot and the painter's own
        ///       fields (_clip, _clip_enabled, _translate) both have to be updated, and doing that in
        ///       two places would be a silent divergence waiting to happen.
        void apply_saved_state(const saved_state_t& saved);

        paint_device_t* _device = nullptr;
        engine_state_t _state;
        /// @brief The save()/restore() stack (only save() pushes, restore() pops).
        std::vector<saved_state_t> _saved;
        /// @brief The accumulated translation, in device pixels.
        point_t _translate{};
        /// @brief The effective clip in DEVICE coordinates (meaningful while _clip_enabled).
        rect_t _clip{};
        /// @brief The exact DEVICE-space clip when it is a region; empty for a rectangle clip.
        region_t _clip_region;
        /// @brief Whether a clip is in effect.
        bool _clip_enabled = false;
        /// @brief Whether the backend declares engine_feature_flag::TRANSLATE (cached at begin()).
        /// @note false means the painter applies the offset to the coordinates itself.
        bool _engine_translate = false;
        /// @brief The capabilities the engine declared at begin().
        engine_feature_t _engine_features{};
        bool _active = false;
    };
}

#endif
