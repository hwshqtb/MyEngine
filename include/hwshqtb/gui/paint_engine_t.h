#ifndef HWSHQTB__GUI__PAINT_ENGINE_H
#define HWSHQTB__GUI__PAINT_ENGINE_H

/// @file paint_engine_t.h
/// @brief The painter/backend contract of the GUI layer: engine features, the batched state
///        snapshot, and the paint_engine_t interface.

#include <cstdint>
#include <span>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/export.h"
#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/transform_t.h"
#include "hwshqtb/metaobject/flag_t.h"

namespace hwshqtb::gui {
    class paint_device_t;

    /// @brief One optional capability of a paint engine (a PaintEngineFeature subset).
    ///
    /// The flags describe what a backend can honour; they are queried, not negotiated. A painter
    /// adapts to a missing feature (for example by drawing an unsupported primitive itself).
    enum struct engine_feature_flag : std::uint32_t {
        /// @brief No feature at all (the empty set).
        NONE = 0,
        /// @brief The engine can transform primitives (the SDL backend: axis-aligned scaling).
        PRIMITIVE_TRANSFORM = 1u << 0,
        /// @brief The engine can translate primitives and glyph runs (NOT the SDL backend).
        /// @note Added in the W1 stage: SDL's renderer has no translation (only SDL_SetRenderScale),
        ///       so it does not declare the bit and painter_t applies the offset to every coordinate
        ///       itself. A backend that declares it receives the offset in engine_state_t::translate()
        ///       and applies it to primitives and glyph positions (never to the clip rectangle, which
        ///       is always in device coordinates).
        TRANSLATE = 1u << 1,
        /// @brief The engine can transform pixmaps (the SDL backend: SDL_RenderTexture scaling).
        PIXMAP_TRANSFORM = 1u << 2,
        /// @brief The engine can clip drawing to a rectangle.
        CLIP = 1u << 3,
        /// @brief The engine can antialias its primitives.
        ANTIALIASING = 1u << 4,
        /// @brief The engine can blend source colours with the destination (alpha).
        BLEND = 1u << 5,
        /// @brief The engine can clip drawing to a REGION (a set of rectangles), not only one rectangle.
        /// @note The companion of CLIP: declared only by an engine that can honour
        ///       engine_state_t::clip_region(). A painter that meets a region clip and an engine without
        ///       this bit clips to the region's bounding rectangle, so the caller has to split the
        ///       region into passes itself (window_t::draw_frame() does exactly that).
        CLIP_REGION = 1u << 6,
    };

    /// @brief A bit set of engine_feature_flag values (the project's typed flag wrapper, QFlags shape).
    using engine_feature_t = ::hwshqtb::metaobject::flag_t<engine_feature_flag>;

    /// @brief The paint state a painter pushes into its engine, with per-field dirty flags.
    ///
    /// This is the minimal form of QPaintEngineState: the painter owns exactly one snapshot, and on
    /// every state change it hands the whole snapshot to paint_engine_t::update_state() together
    /// with dirty_flags() saying which fields the engine has to re-read. Fields without their dirty
    /// bit are guaranteed unchanged since the last update, so an engine may keep its own copy and
    /// apply only the flagged fields.
    ///
    /// A fresh snapshot marks colour, clip and transform dirty: an engine that has never seen this
    /// painter knows nothing about the state, so the first update must establish all of it.
    /// translate() is deliberately NOT dirty in a fresh snapshot: its default is the identity
    /// offset, which is also the state every backend starts a pass with (paint_engine_t::begin()
    /// resets the backend, and a painter only ever writes a non-zero translate through
    /// set_translate()). A backend therefore never has to clear a stale translation it never
    /// received in this pass.
    ///
    /// @par Value type
    /// constexpr and header-only, so it is not exported (see export.h) and can be used in constant
    /// expressions. The setters are the builder used by painter_t: each one stores its field and
    /// accumulates the matching dirty flag.
    class engine_state_t {
    public:
        /// @brief The bits carried by dirty_flags().
        enum : std::uint32_t {
            dirty_color = 1u << 0,     ///< color() changed (or was never applied).
            dirty_clip = 1u << 1,      ///< clip() / clip_enabled() changed.
            dirty_transform = 1u << 2, ///< transform() changed.
            dirty_translate = 1u << 3, ///< translate() changed.
        };

        /// @brief Construct the default snapshot: opaque black, no clip, identity transform,
        ///        no translation.
        /// @note The colour, clip and transform dirty flags are set; translate is not (see the class
        ///       note).
        constexpr engine_state_t() noexcept = default;

        /// @brief The fields that changed since the last update.
        /// @return A bitwise OR of the dirty_* enumerators.
        constexpr std::uint32_t dirty_flags() const noexcept {
            return _dirty;
        }

        /// @brief Whether any field is dirty (dirty_flags() != 0).
        constexpr bool is_dirty() const noexcept {
            return _dirty != 0;
        }

        /// @brief The current drawing colour.
        constexpr color_t color() const noexcept {
            return _color;
        }

        /// @brief The current clip rectangle (only meaningful while clip_enabled() is true).
        constexpr rect_t clip() const noexcept {
            return _clip;
        }

        /// @brief Whether clipping is enabled.
        constexpr bool clip_enabled() const noexcept {
            return _clip_enabled;
        }

        /// @brief The current clip REGION, when the clip is not a single rectangle.
        /// @return The rectangles of the clip, in device pixels; empty when the clip is a rectangle
        ///         (read clip() then) or disabled.
        /// @note Valid only during the update_state() call that received this snapshot: the painter
        ///       owns the storage, exactly like glyph_run_t's spans.
        /// @note Only an engine that declares engine_feature_flag::CLIP_REGION receives a
        ///       non-empty region; every other engine is handed the bounding rectangle in clip().
        constexpr std::span<const rect_t> clip_region() const noexcept {
            return _clip_region;
        }

        /// @brief Whether a region clip (more than one rectangle) is in effect.
        constexpr bool clip_region_enabled() const noexcept {
            return !_clip_region.empty();
        }

        /// @brief The current transform.
        constexpr transform_t transform() const noexcept {
            return _transform;
        }

        /// @brief The current translation, in device pixels.
        ///
        /// Only meaningful for an engine that declares engine_feature_flag::TRANSLATE: painter_t then
        /// keeps the accumulated offset here and expects the backend to add it to every primitive
        /// and every glyph position. The translation never applies to clip(), which is always in
        /// device coordinates.
        constexpr point_t translate() const noexcept {
            return _translate;
        }

        /// @brief Set the drawing colour and mark it dirty.
        constexpr void set_color(color_t color) noexcept {
            _color = color;
            _dirty |= dirty_color;
        }

        /// @brief Set the clip rectangle and enable clipping; marks the clip dirty.
        /// @param clip The clip rectangle in device pixels. An empty rectangle clips everything away
        ///        (the engine must draw nothing), which is different from clear_clip().
        /// @note A rectangle clip replaces a region clip: the region is cleared.
        constexpr void set_clip(rect_t clip) noexcept {
            _clip = clip;
            _clip_enabled = true;
            _clip_region = {};
            _dirty |= dirty_clip;
        }

        /// @brief Set a REGION clip and its bounding rectangle; marks the clip dirty.
        /// @param region The clip rectangles, in device pixels; callers pass more than one.
        /// @param bounds The bounding rectangle of @p region, for engines that only clip to a
        ///        rectangle.
        /// @note The span points at the painter's storage and is valid for the duration of the
        ///       update_state() call only.
        /// @note The painter passes a region only to an engine that declares
        ///       engine_feature_flag::CLIP_REGION; the other engines get set_clip(bounds) instead.
        constexpr void set_clip_region(std::span<const rect_t> region, rect_t bounds) noexcept {
            _clip = bounds;
            _clip_enabled = true;
            _clip_region = region;
            _dirty |= dirty_clip;
        }

        /// @brief Disable clipping; marks the clip dirty.
        constexpr void clear_clip() noexcept {
            _clip_enabled = false;
            _clip_region = {};
            _dirty |= dirty_clip;
        }

        /// @brief Set the transform and mark it dirty.
        constexpr void set_transform(transform_t transform) noexcept {
            _transform = transform;
            _dirty |= dirty_transform;
        }

        /// @brief Set the translation and mark it dirty.
        /// @param translate The offset in device pixels; one already set marks it dirty again (a
        ///        repeated value is harmless, it only costs one redundant update_state()).
        constexpr void set_translate(point_t translate) noexcept {
            _translate = translate;
            _dirty |= dirty_translate;
        }

        /// @brief Forget the dirty flags (the engine has applied the snapshot).
        constexpr void clear_dirty() noexcept {
            _dirty = 0;
        }

    private:
        color_t _color{};
        rect_t _clip{};
        transform_t _transform{};
        point_t _translate{};
        /// @brief The region clip in effect; empty for a rectangle clip or no clip (see clip_region()).
        /// @note A span, not a container: the painter owns the storage and this snapshot only points
        ///       at it for the duration of one update_state() call.
        std::span<const rect_t> _clip_region{};
        std::uint32_t _dirty = dirty_color | dirty_clip | dirty_transform;
        bool _clip_enabled = false;
    };

    /// @brief A run of positioned glyphs; the engine decides how to draw them (the QGlyphRun role).
    ///
    /// This is the text seam, the analogue of QPaintEngine::drawGlyphRun: text_layout_t does the layout
    /// (decoding, engine resolution, advances, line breaks, baselines) and hands a backend a run of
    /// GLYPH INDICES plus the engine that owns them. A backend rasterises them however it likes — a
    /// texture cache in the SDL engine, a CPU rasteriser, a GPU text pipeline — without the layout
    /// knowing anything about rasterisation.
    ///
    /// @par Glyphs, not code points
    /// glyphs[i] is a glyph index of @c engine (its character-to-glyph mapping result), never a code
    /// point: one code point yields exactly one glyph, so a multi-byte character is one glyph and one
    /// advance. Index 0 is the engine's missing-glyph box, which is what an unmapped code point draws.
    ///
    /// @par Parallel arrays
    /// @c glyphs and @c positions always have @c count entries and are indexed in parallel;
    /// positions[i] is the pen position of glyphs[i]. The arrays are owned by the layout and are only
    /// valid for the duration of the draw_glyphs() call (an engine must copy what it wants to keep).
    ///
    /// @par Baseline
    /// positions[i].y() is the baseline of glyph i: the engine draws its mask so that the mask's top row
    /// lands at y - engine->ascent(). A run never mixes engines, so one ascent applies to all of it.
    ///
    /// @par Control characters
    /// The layout consumes '\n' itself (it only moves the pen), so a run never contains a line break.
    struct glyph_run_t {
        /// @brief The engine that rasterises this run's glyphs; never null.
        const font_engine_t* engine = nullptr;
        /// @brief Glyph indices (one per glyph), in text order.
        const std::uint32_t* glyphs = nullptr;
        /// @brief Per-glyph baseline pen positions, parallel to @c glyphs.
        const point_t* positions = nullptr;
        /// @brief The number of glyphs in the run.
        std::size_t count = 0;
        /// @brief The colour to draw the glyphs with.
        color_t color = color_t::black_color();
    };

    /// @brief One image's pixels as a drawing call sees them (the payload of draw_image()).
    ///
    /// @par Borrowed, never owned
    /// The pixels belong to whatever produced the view (an image_t, a decoded file) and are only valid
    /// for the duration of the draw_image() call; an engine that wants to keep them (a texture cache)
    /// must upload a copy.
    ///
    /// @par Layout
    /// Row-major ARGB, top row first: @c pixels is the top-left pixel and the pixel at (x, y) is
    /// <tt>pixels[y * pitch + x]</tt>. @c pitch is in PIXELS, not bytes, and is never smaller than
    /// @c width — an engine may therefore upload the whole pitch and only use @c width of it.
    struct image_view_t {
        /// @brief The top-left pixel; never null.
        const color_t* pixels = nullptr;
        /// @brief The image width in pixels.
        int width = 0;
        /// @brief The image height in pixels.
        int height = 0;
        /// @brief The distance from one row's first pixel to the next, in PIXELS.
        int pitch = 0;
    };

    /// @brief The contract between a painter and a drawing backend (QPaintEngine's minimal shape).
    ///
    /// The vocabulary is the GUI layer's own — point_t / rect_t / color_t / transform_t /
    /// engine_state_t / font_t / glyph_run_t — so no backend type appears in this header and
    /// painter_t works with any engine.
    ///
    /// @par Batched and state-snapshot based
    /// update_state() is called at most once per state change (never per primitive), and
    /// draw_rects() receives a whole batch at once, filled or outlined. A batch may have count == 1;
    /// a painter always passes an array, never a single-rectangle shortcut.
    ///
    /// @par Session
    /// begin() / end() bracket a paint pass on a device. begin() establishes the backend state
    /// (blend mode, clip, transform) and returns false when the device cannot be painted; end()
    /// flushes everything the backend queued, which is the point at which drawing becomes visible in
    /// the target. Drawing calls outside a session are ignored by the implementations.
    ///
    /// @par Deliberately absent
    /// draw_lines is not declared: it arrives with G2 (pen/brush), and no speculative stub is added
    /// here. Text is drawn through draw_glyphs() and images through draw_image(), both of which are
    /// rasterisation requests rather than layout ones — layout stays in painter_t.
    class HWSHQTB_GUI_EXPORT paint_engine_t {
    public:
        paint_engine_t() = default;
        virtual ~paint_engine_t() = default;

        paint_engine_t(const paint_engine_t&) = delete;
        paint_engine_t& operator=(const paint_engine_t&) = delete;

        /// @brief The features this backend can honour.
        virtual engine_feature_t features() const noexcept = 0;

        /// @brief Start a paint pass on @p device.
        /// @param device The target; it must outlive the pass.
        /// @return true when the backend is ready to draw, false otherwise (in which case the
        ///         painter stays inactive and ignores drawing).
        virtual bool begin(paint_device_t& device) = 0;

        /// @brief Finish the paint pass and flush the backend's queued work.
        /// @return true on success; ending a pass that was not begun returns true as well (no-op).
        virtual bool end() = 0;

        /// @brief Apply the fields whose dirty flags are set.
        /// @param state The snapshot; the engine must not keep a reference to it.
        virtual void update_state(const engine_state_t& state) = 0;

        /// @brief Draw a batch of rectangles with the current colour.
        /// @param rects The rectangles, in device pixels (rect_t's half-open convention).
        /// @param count The number of rectangles; count <= 0 draws nothing.
        /// @param fill true to fill each rectangle, false to outline it with a one-pixel border.
        virtual void draw_rects(const rect_t* rects, int count, bool fill) = 0;

        /// @brief Rasterise one positioned glyph run.
        ///
        /// The engine draws, for every i, the glyph of run.codepoints[i] with its cell's baseline at
        /// run.positions[i] (see glyph_run_t), in run.color. The painter has already resolved the
        /// layout, so an engine never measures, wraps or re-orders text.
        ///
        /// @param run The positioned run; the spans are only valid during the call.
        /// @note Called only inside a begin()/end() session, like draw_rects().
        virtual void draw_glyphs(const glyph_run_t& run) = 0;

        /// @brief Draw the whole of @p image into @p destination (QPaintEngine::drawImage equivalent).
        /// @param destination Where the image lands, in the painter's device coordinates. Its size is the
        ///        SCALED size: an engine that declares engine_feature_flag::PIXMAP_TRANSFORM scales, one
        ///        that does not may fall back to nearest-neighbour or refuse anything but 1:1.
        /// @param image The pixels to draw; see image_view_t for their layout and lifetime.
        /// @note Called only inside a begin()/end() session, like draw_rects() and draw_glyphs().
        /// @note The painter has already resolved the clip and the translation; an engine never lays out
        ///       an image, it only blits (and, with PIXMAP_TRANSFORM, scales) it.
        virtual void draw_image(const rect_t& destination, const image_view_t& image) = 0;
    };
}

#endif
