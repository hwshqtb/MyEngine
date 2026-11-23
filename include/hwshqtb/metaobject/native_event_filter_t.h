#ifndef HWSHQTB__METAOBJECT__NATIVE_EVENT_FILTER_H
#define HWSHQTB__METAOBJECT__NATIVE_EVENT_FILTER_H

/// @file native_event_filter_t.h
/// @brief The native event filter interface (QAbstractNativeEventFilter equivalent).

#include "export.h"

namespace hwshqtb::metaobject {
    /// @brief Intercepts platform messages before the dispatcher interprets them.
    ///
    /// Mirrors QAbstractNativeEventFilter: a filter receives the raw platform message and decides
    /// whether it handled it. Returning true stops further processing of that message, so the
    /// dispatcher neither maps it to an event_t nor dispatches it.
    ///
    /// The message pointer is opaque on purpose: a platform implementation passes its own event
    /// structure through as @c void*, exactly like Qt passes a platform-specific @c MSG / @c XEvent.
    /// A filter that wants to interpret the content must know which platform implementation is
    /// installed.
    class HWSHQTB_EXPORT native_event_filter_t {
    public:
        /// @brief Destructor.
        virtual ~native_event_filter_t() = default;

        /// @brief Copy and move are disabled: filters are identified by their address.
        native_event_filter_t(const native_event_filter_t&) = delete;
        native_event_filter_t& operator=(const native_event_filter_t&) = delete;
        native_event_filter_t(native_event_filter_t&&) = delete;
        native_event_filter_t& operator=(native_event_filter_t&&) = delete;

        /// @brief Inspect one raw platform message.
        /// @param message The platform message; never nullptr.
        /// @param result Receives a platform-specific result code when the filter handles it.
        /// @return true when the message was handled and must not be processed further.
        virtual bool native_event_filter_t_proc(void* message, long* result) = 0;

    protected:
        /// @brief Constructor; only derived classes may be created.
        native_event_filter_t() = default;
    };
}

#endif
