#ifndef HWSHQTB__GUI__IMAGE_H
#define HWSHQTB__GUI__IMAGE_H

/// @file image_t.h
/// @brief image_t: an offscreen ARGB8888 paint device that can be read back pixel by pixel.

#include <memory>
#include <string_view>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/export.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/paint_device_t.h"
#include "hwshqtb/gui/paint_engine_t.h"

namespace hwshqtb::gui {
    /// @brief An offscreen ARGB8888 surface that is painted into like a window and read back per pixel.
    ///
    /// @par Backing
    /// 32-bit ARGB pixels owned by an out-of-line implementation, with a software renderer over them.
    /// Painting goes through the device's paint_engine() exactly as it will for a window-backed
    /// device, so the pixel assertions in tests/gui-test cover the rendering path the demo itself
    /// uses — with no window, no display, and no video subsystem initialised.
    ///
    /// @par Platform stays private
    /// The pixels, the renderer and the engine live in an out-of-line `impl`, so no platform type or
    /// header appears in this header. The device is a paint_device_t like any window: an image is not
    /// special to a painter, and a surface type is nobody's business but this device's.
    ///
    /// @par Lifetime
    /// Non-copyable, movable. A painter_t must not outlive the image it paints into. A moved-from
    /// image is valid but empty: size() is 0 x 0, pixel() returns transparent, and paint_engine()
    /// returns nullptr (the device has no engine any more, and asking is not an error).
    ///
    /// @par Engine
    /// The engine is created together with the pixels, in the constructor, and lives exactly as long
    /// as they do. paint_engine() only hands it out.
    ///
    /// @par Thread safety
    /// Not thread safe: one image must be used from one thread at a time.
    class HWSHQTB_GUI_EXPORT image_t final: public paint_device_t {
    public:
        /// @brief Create an ARGB8888 image of @p width x @p height transparent pixels.
        /// @param width The image width in pixels; must be positive.
        /// @param height The image height in pixels; must be positive.
        /// @throws std::invalid_argument when either side is not positive.
        /// @throws std::runtime_error when the platform cannot allocate the pixels or the renderer
        ///         they are painted through (the message carries the platform error).
        image_t(int width, int height);

        /// @brief Create an ARGB8888 image of @p size transparent pixels.
        /// @param size The image extent; both sides must be positive.
        /// @throws std::invalid_argument when either side is not positive.
        /// @throws std::runtime_error when the platform cannot allocate the pixels or its renderer.
        explicit image_t(extent_t size);

        ~image_t() override;

        image_t(const image_t&) = delete;
        image_t& operator=(const image_t&) = delete;

        /// @brief Take over @p other's surface; @p other becomes empty (see the class note).
        image_t(image_t&& other) noexcept;

        /// @brief Replace the contents with @p other's surface; @p other becomes empty.
        image_t& operator=(image_t&& other) noexcept;

        /// @brief The image extent in device pixels (0 x 0 after being moved from).
        extent_t size() const noexcept override;

        /// @brief The image's paint engine, or nullptr when the image has none.
        /// @return The engine the constructor created with the pixels; nullptr after the image was
        ///         moved from (an empty image has no engine).
        /// @note A pure query: it never creates anything and never throws.
        /// @note noexcept.
        paint_engine_t* paint_engine() noexcept override;

        /// @brief The colour of one pixel, for tests and inspection.
        ///
        /// This is an O(1) memory read of the pixels (a locked pixel-word read), not a rendering API;
        /// do not use it from a paint pass. The device's engine is flushed first, so queued drawing is
        /// applied even when the painter session has not been ended yet.
        ///
        /// @param x The pixel column.
        /// @param y The pixel row.
        /// @return The pixel colour. Out-of-range coordinates — negative, or at/beyond size() under
        ///         the half-open convention — return color_t::transparent_color() instead of reading
        ///         out of bounds. That defined value is part of the contract: row and column scans may
        ///         cross the edge safely.
        color_t pixel(int x, int y) const;

        /// @brief The pixels as a drawing call sees them, for painter_t::draw_image().
        /// @return A borrowed view (pixels, width, height, pixel pitch) of this image's buffer.
        /// @note Valid until the next drawing call on this image: anything queued is flushed before the
        ///       view is taken, and the next call may reallocate. A consumer that keeps the pointer past
        ///       its own call is reading memory it no longer owns.
        /// @note A moved-from image answers a view with a null pointer and zero extent.
        image_view_t view() const;

        /// @brief The writable pixel buffer (QImage::bits()): for a loader or an image being built.
        /// @param pitch Receives the row pitch in PIXELS.
        /// @return The top-left pixel, or nullptr for a moved-from image.
        /// @note Writing through this pointer bypasses every drawing call, so the image must not be
        ///       read through view() while the pointer is in use.
        color_t* writable_pixels(int& pitch);

    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };

}

#endif
