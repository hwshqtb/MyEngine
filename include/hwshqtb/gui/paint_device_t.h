#ifndef HWSHQTB__GUI__PAINT_DEVICE_H
#define HWSHQTB__GUI__PAINT_DEVICE_H

/// @file paint_device_t.h
/// @brief paint_device_t: a drawing target of the GUI layer (QPaintDevice's minimal form).

#include "hwshqtb/gui/export.h"
#include "hwshqtb/gui/geometry.h"

namespace hwshqtb::gui {
    class paint_engine_t;

    /// @brief A drawing target: something with a size and, when it can be painted, a paint engine.
    ///
    /// The device OWNS its engine and creates it while it is being set up, so paint_engine() is a pure
    /// query: it returns the engine the device already holds, or nullptr while the device has none
    /// (a moved-from image, a device whose target could not be created). It never creates anything and
    /// never throws — "this device cannot be painted right now" is a STATE, and nullptr is how it is
    /// reported.
    ///
    /// A painter keeps a reference to its device, so the device must outlive every painter that
    /// draws into it.
    ///
    /// @par Deliberately absent (H2)
    /// No DPI/metrics accessors yet: high-DPI support is deferred to a later stage, and the
    /// interface can grow without breaking existing devices.
    class HWSHQTB_GUI_EXPORT paint_device_t {
    public:
        paint_device_t() = default;
        virtual ~paint_device_t() = default;

        paint_device_t(const paint_device_t&) = delete;
        paint_device_t& operator=(const paint_device_t&) = delete;

        /// @brief The device extent in device pixels.
        virtual extent_t size() const noexcept = 0;

        /// @brief The device's paint engine, or nullptr when the device has none.
        /// @return The engine the device created while it was set up, or nullptr when it has none
        ///         (never painted, moved from, or its drawing target could not be created).
        /// @note A PURE QUERY: it never creates anything, and it never throws.
        /// @note The pointer stays valid for as long as the device holds the engine (i.e. until it is
        ///       moved from or destroyed); a painter must not keep it across a pass.
        virtual paint_engine_t* paint_engine() = 0;
    };
}

#endif
