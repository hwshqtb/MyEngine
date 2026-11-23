#ifndef HWSHQTB__METAOBJECT__SIGNAL_BLOCKER_H
#define HWSHQTB__METAOBJECT__SIGNAL_BLOCKER_H

/// @file signal_blocker_t.h
/// @brief RAII scope that suppresses signal emission from an object.

#include "object_t.h"
#include "export.h"

namespace hwshqtb::metaobject {
    /// @brief Suppresses signal delivery from one object for the lifetime of the blocker.
    ///
    /// A thin RAII wrapper over object_t::block_signals(). Blocking is reference counted, so nested
    /// blockers are safe: signals resume only after the outermost one is destroyed.
    ///
    /// The blocker is movable but not copyable: moving transfers ownership of one blocking level, so
    /// the source goes inert and neither side touches the counter.
    ///
    /// unblock(), reblock() and dismiss() mirror QSignalBlocker: unblock()/reblock() release and
    /// re-acquire this blocker's level; dismiss() drops the ownership without releasing the level,
    /// leaving the object blocked after the blocker is gone.
    ///
    /// @code
    /// {
    ///     signal_blocker_t blocked(&obj);   // obj emits nothing here
    ///     obj.set_value(1.0);             // no slot is called
    /// }                                   // emission is restored here
    /// @endcode
    class HWSHQTB_EXPORT signal_blocker_t {
    public:
        /// @brief Block signals on @p object.
        /// @param object The object; nullptr makes the blocker a no-op.
        /// @note noexcept.
        explicit signal_blocker_t(object_t* object) noexcept;

        /// @brief Release this blocker's level, unless it was dismissed.
        ~signal_blocker_t();

        /// @brief Copy is disabled: the blocker owns one blocking level.
        signal_blocker_t(const signal_blocker_t&) = delete;
        signal_blocker_t& operator=(const signal_blocker_t&) = delete;

        /// @brief Move the blocker; the source becomes inert without touching the counter.
        /// @param other The blocker to move from.
        /// @note noexcept.
        signal_blocker_t(signal_blocker_t&& other) noexcept;

        /// @brief Release this blocker's level, then adopt @p other's state.
        /// @param other The blocker to move from.
        /// @return *this.
        /// @note noexcept.
        /// @note The release happens before the adoption, so the counter stays equal to the number
        ///       of live blocker levels.
        /// @note Self-assignment is a no-op.
        signal_blocker_t& operator=(signal_blocker_t&& other) noexcept;

        /// @brief The object this blocker observes.
        /// @return The blocked object, or nullptr when the blocker is inert.
        /// @note noexcept.
        object_t* blocker_object() const noexcept;

        /// @brief Release this blocker's level now (idempotent).
        /// @note noexcept.
        /// @note A later reblock() can acquire a new level; the destructor then becomes a no-op.
        void unblock() noexcept;

        /// @brief Re-acquire a level if this blocker currently holds none (idempotent).
        /// @note noexcept.
        /// @note An inert blocker (nullptr object), or one whose level was released or dismissed,
        ///       can take a new level on the object it still points at.
        void reblock() noexcept;

        /// @brief Give up ownership WITHOUT unblocking: the object stays blocked after destruction.
        /// @note noexcept.
        /// @note The object pointer is kept, so reblock() can still acquire a level afterwards.
        void dismiss() noexcept;

    private:
        /// @brief The object whose signals are blocked (nullptr = no-op blocker).
        object_t* _object = nullptr;

        /// @brief Whether this blocker currently owns one level of the object's block counter.
        bool _blocked = false;
    };
}

#endif
