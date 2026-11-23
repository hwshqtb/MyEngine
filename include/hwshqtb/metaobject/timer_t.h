#ifndef HWSHQTB__METAOBJECT__TIMER_T_H
#define HWSHQTB__METAOBJECT__TIMER_T_H

/// @file timer_t.h
/// @brief A repeating or one-shot timer object (the QTimer equivalent).

#include "object_t.h"
#include "macros.h"
#include "export.h"

namespace hwshqtb::metaobject {
    /// @brief A timer that emits timeout() on each tick (QTimer equivalent).
    ///
    /// Thin wrapper over object_t::start_timer / kill_timer: the actual timing lives in the thread's
    /// event dispatcher, so a timer ticks on the thread that owns this object.
    class HWSHQTB_EXPORT timer_t : public object_t {
        OBJECT

    public:
        /// @brief Construct an inactive timer.
        /// @param parent Optional parent, deleted with it.
        explicit timer_t(object_t* parent = nullptr);

        /// @brief Stops the timer before it is destroyed.
        ~timer_t() override;

        /// @brief The current interval in milliseconds.
        /// @note noexcept.
        int interval() const noexcept;

        /// @brief Set the interval; restarting an active timer applies it immediately.
        /// @param ms The interval; a value <= 0 is treated as 1.
        void set_interval(int ms);

        /// @brief Whether the timer fires only once.
        /// @note noexcept.
        bool is_single_shot() const noexcept;

        /// @brief Set the one-shot flag; restarting an active timer applies it immediately.
        /// @param on true for a one-shot timer.
        void set_single_shot(bool on);

        /// @brief Whether the timer is currently running.
        /// @note noexcept.
        bool is_active() const noexcept;

        /// @brief Start the timer with the current interval.
        void start();

        /// @brief Start the timer with a new interval.
        /// @param ms The interval; a value <= 0 is treated as 1.
        void start(int ms);

        /// @brief Stop the timer.
        void stop();

        /// @brief Emitted on each tick.
        SIGNAL(void timeout() const)

    protected:
        /// @brief Receives the ticks and re-emits them as timeout().
        /// @param e The timer event.
        void timer_event(timer_event_t* e) override;

    private:
        int _interval_ms = 0;
        bool _single_shot = false;
        int _timer_id = 0;
    };
}

#endif
