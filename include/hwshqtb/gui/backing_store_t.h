#ifndef HWSHQTB__GUI__BACKING_STORE_T_H
#define HWSHQTB__GUI__BACKING_STORE_T_H

/// @file backing_store_t.h
/// @brief backing_store_t: the window's persistent drawing surface.

#include <memory>

#include "hwshqtb/gui/export.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/region_t.h"

namespace hwshqtb::gui {
    /// @brief The window's persistent drawing surface (the Qt QBackingStore equivalent).
    ///
    /// The pixels live in an out-of-line platform surface of the bound kind: it owns one buffer and
    /// presents it to the drawing target this store was created with.
    ///
    /// @par Persistence
    /// The surface survives frames: the pixels outside a flushed region keep their previous
    /// contents. That is what makes partial repainting possible at all — a swapchain back buffer is
    /// undefined after present, so it cannot play this role.
    ///
    /// @par Format
    /// The surface is 32-bit ARGB, the format the window renderer's own back buffer has on Windows
    /// (DXGI_FORMAT_B8G8R8A8_UNORM). Drawing into a surface of that format therefore produces the
    /// same pixels the direct-to-window path produced, which is what keeps a captured frame
    /// byte-identical across the two paths.
    ///
    /// @par Target switching and failure
    /// begin_paint() makes the surface the drawing target of its renderer, and end_paint() restores
    /// the default (window) target. A driver that cannot render into an offscreen buffer refuses the
    /// switch: the surface is then dropped, is_valid() answers false and the window paints straight
    /// into its window target instead (the pre-backing-store behaviour). The fallback is not silent
    /// — is_valid() is the query a caller uses to tell the two modes apart.
    ///
    /// @par Lifetime
    /// The drawing target must outlive the store; the store owns only its own surface. Use it from
    /// the thread that owns the target (window_t's painting thread).
    class HWSHQTB_GUI_EXPORT backing_store_t {
    public:
        /// @brief Create a store that draws through @p target.
        /// @param target The platform drawing target (today the window's renderer), type-erased to
        ///        void* so that no platform type appears in this header. It is not owned and must
        ///        stay alive for the whole lifetime of the store; nullptr creates an invalid store.
        /// @note No surface is allocated yet: resize() creates it for a pixel size.
        explicit backing_store_t(void* target);

        /// @brief Destroy the surface (the target is not owned).
        ~backing_store_t();

        backing_store_t(const backing_store_t&) = delete;
        backing_store_t& operator=(const backing_store_t&) = delete;
        backing_store_t(backing_store_t&&) = delete;
        backing_store_t& operator=(backing_store_t&&) = delete;

        /// @brief Make the surface match @p pixel_size, recreating it when the size changes.
        /// @param pixel_size The window's client area in pixels.
        /// @return true when a valid surface of that size exists afterwards; false when no surface
        ///         could be created (an empty size destroys the current one).
        /// @note A recreated surface has undefined contents until the next frame is painted.
        /// @note After a driver refused the render target once, resize() keeps answering false: the
        ///       window stays on the direct path instead of retrying a switch that cannot work.
        bool resize(::hwshqtb::gui::extent_t pixel_size);

        /// @brief Whether a surface exists and the renderer accepted it as a render target.
        /// @return true when begin_paint() can draw into the surface.
        /// @note noexcept.
        bool is_valid() const noexcept;

        /// @brief The surface size in pixels.
        /// @return The size passed to the last successful resize(), or an empty extent when there is
        ///         no surface.
        /// @note noexcept.
        ::hwshqtb::gui::extent_t size() const noexcept;

        /// @brief Make the renderer draw into the backing surface.
        /// @return true when the surface is the current render target; false when there is no valid
        ///         surface or the driver refused the switch (the renderer keeps its previous target
        ///         in that case, so the caller can paint straight into the window).
        /// @note Begin/end pairs are not nestable: one surface, one pass.
        bool begin_paint();

        /// @brief Restore the renderer's default target.
        /// @note A no-op when begin_paint() did not succeed.
        void end_paint();

        /// @brief Copy @p region from the backing surface to the window target and present.
        /// @param region The area to copy, in pixels of the surface, in window coordinates; an empty
        ///        region copies nothing but still presents.
        /// @note The copy is a 1:1 texture blit with blending disabled, and the window target's
        ///       clip/scale/blend state is re-established first, so nothing a painter left behind can
        ///       alter the pixels.
        /// @note A partial region is only safe when the target is known to hold the rest of the frame.
        ///       A window target is not a persistent surface — the Direct3D 11 renderer gives a
        ///       normal window a two-buffer flip-model swap chain, so the buffer a frame draws into
        ///       holds the pixels of the frame two presents ago — therefore every pixel the surface
        ///       changed must be inside @p region. window_t passes the whole client area for exactly
        ///       that reason, because a scroll moves pixels far outside the strip it repaints.
        /// @note A no-op when is_valid() is false (the direct path has already presented).
        void flush(const region_t& region);

        /// @brief Move the surface's own pixels inside @p rect by (@p dx, @p dy).
        ///
        /// The fast path of a container scroll (QBackingStore::scroll): the content that is already
        /// painted moves instead of being drawn again, and only the strip the shift uncovers has to be
        /// repainted. The pixels outside @p rect are left untouched.
        ///
        /// @param rect The scrolled area, in pixels of the surface, in window coordinates.
        /// @param dx The horizontal shift: a pixel at x() ends up at x() + @p dx.
        /// @param dy The vertical shift: a pixel at y() ends up at y() + @p dy.
        /// @return true when the pixels moved; false when the caller must repaint @p rect instead: no
        ///         surface, a paint pass is open, the driver refused the copy, or the shift is at least
        ///         the extent (nothing of the old content would remain inside @p rect).
        /// @note The pixels this call moves are NOT visible on screen by themselves: flush() has to
        ///       cover @p rect (or more), not only the strip the caller repaints afterwards, or the
        ///       window keeps showing the pre-scroll content.
        /// @note A failed shift leaves the surface's pixels unchanged in every case that matters.
        /// @note The renderer is left on its default (window) target, exactly as end_paint() leaves it.
        bool scroll(::hwshqtb::gui::rect_t rect, int dx, int dy);

    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };
}

#endif
