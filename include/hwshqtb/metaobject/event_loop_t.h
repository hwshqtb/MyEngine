#ifndef HWSHQTB__METAOBJECT__EVENT_LOOP_T_H
#define HWSHQTB__METAOBJECT__EVENT_LOOP_T_H

/// @file event_loop_t.h
/// @brief The event loop event_loop_t (QEventLoop equivalent).

#include "object_t.h"
#include "event_t.h"
#include "event_dispatcher.h"
#include "export.h"

#include <atomic>
#include <cstdint>
#include <optional>

namespace hwshqtb::metaobject {

    /// @brief Event loop (QEventLoop equivalent).
    ///
    /// Like Qt, it derives from the object base class (@c QEventLoop @c : @c QObject), so it can be
    /// attached to the object tree and act as an event receiver.
    ///
    /// @par Pump policies
    /// exec() takes a loop_pump_enum: @c system_and_posted (the default, used by core_application::exec())
    /// pumps native platform messages through @c event_dispatcher::process_events() and also honours a
    /// system quit request; @c posted_only (used by thread_t::exec()) drains the calling thread's
    /// posting queue and sleeps while it is empty, because the native pump belongs to the main thread.
    /// A cross-thread post wakes that idle sleep at once; the 1 ms timeout is only the backstop. Both
    /// policies share the loop level, the exit code and exit()/quit().
    ///
    /// @par Phases
    /// set_phase() installs an event_phase_t: exec() then calls event_dispatcher::process_phase()
    /// once per iteration instead of process_events(). std::nullopt restores the event-by-event
    /// behaviour. A @c posted_only loop ignores the policy, because a phase pumps native messages
    /// and that pump belongs to the main thread.
    ///
    /// @par Quit requests and nesting
    /// Every running loop is registered on its thread's active-loop stack. core_application::exit()
    /// broadcasts to every loop on the APPLICATION thread — the thread that constructed the
    /// core_application instance, not necessarily the caller's — so a worker's quit() stops the
    /// application and a quit() issued from a nested loop stops the whole application on that thread
    /// instead of only the innermost loop. Only the OUTERMOST @c system_and_posted loop
    /// (loop_level() == 1) clears a pending dispatcher quit request when it starts, so a nested exec()
    /// started from a slot cannot swallow the outer loop's quit request.
    ///
    /// @par Typical usage
    /// @code
    /// event_loop_t loop;                 // or event_loop_t loop(&app);
    /// ...;                             // post a Quit event / call loop.quit()
    /// const int rc = loop.exec();
    /// @endcode
    ///
    /// @warning Emitting a signal on a BlockingQueued connection from the same thread always
    ///          deadlocks (same as Qt).
    /// @sa event_dispatcher
    /// @sa loop_pump_enum
    class HWSHQTB_EXPORT event_loop_t: public object_t {
        OBJECT

    public:
        /// @brief Construct an event loop.
        /// @param parent Parent object; when non-null the loop joins the object tree (the parent
        ///        deletes it when destroyed).
        explicit event_loop_t(object_t* parent = nullptr);

        /// @brief Destructor.
        ~event_loop_t() override;

        /// @brief Process one round of events (QEventLoop::processEvents).
        /// @param flags Event-processing flags.
        /// @return true if at least one event was dispatched in this round.
        bool process_events(process_events_type_t flags = process_events_type_flag::ALL_EVENTS);

        /// @brief Keep processing events for up to @p maximum_time milliseconds.
        /// @param flags Event-processing flags.
        /// @param maximum_time Maximum processing time in milliseconds.
        /// @note When a phase is installed, the loop calls event_dispatcher::process_phase() per
        ///       iteration instead of process_events(); a nested call inherits the same policy.
        void process_events(process_events_type_t flags, int maximum_time);

        /// @brief Run exec() in phased mode; std::nullopt restores the event-by-event behaviour.
        /// @param phase The phase policy, or std::nullopt.
        /// @note Applies to the @c system_and_posted pump only: a @c posted_only loop (thread_t) must
        ///       not pump native messages, so it keeps draining its posting queue directly.
        /// @note Set it before exec(); exec() snapshots the policy when it starts, so a change made
        ///       while the loop runs applies to the next exec() (or process_events(flags, max_time)).
        void set_phase(std::optional<event_phase_t> phase);

        /// @brief The installed phase policy.
        /// @return The policy set by set_phase(), or std::nullopt for event-by-event mode.
        /// @note noexcept.
        std::optional<event_phase_t> phase() const noexcept;

        /// @brief Enter the loop until quit()/exit() or (main-thread policy) a system quit request.
        /// @param flags Event-processing flags.
        /// @param pump Which event sources to drain; the default keeps the main-thread behaviour.
        /// @return The exit code set by exit() (0 for quit()).
        int exec(process_events_type_t flags = {}, loop_pump_enum pump = loop_pump_enum::SYSTEM_AND_POSTED);

        /// @brief Whether the loop is running.
        /// @return true while running.
        /// @note noexcept.
        bool is_running() const noexcept;

        /// @brief The nesting depth of the event loops running on the calling thread.
        /// @return 0 when no loop is running, 1 inside the outermost exec(), and so on.
        /// @note The depth of the calling thread's active-loop stack (QThreadData::eventLoops).
        ///       Counts both pump policies, so a loop running on a thread_t is level 1 there too.
        static int loop_level() noexcept;

        /// @brief Wake a blocked wait up (QEventLoop::wakeUp).
        void wake_up();

        /// @brief Leave the loop with the given return code (QEventLoop::exit).
        /// @param return_code Exit code.
        /// @note Callable from another thread while the loop runs: the code and the quit request are
        ///       atomic, and a @c posted_only loop is woken out of its idle sleep.
        void exit(int return_code = 0);

        /// @brief Leave the loop (equivalent to exit(0)).
        /// @note thread_t::quit() and core_application::quit() both forward here.
        void quit();

        /// @brief Event handling: a Quit event requests the loop to stop.
        /// @param event The event.
        /// @return true if handled; otherwise the base class handles it.
        bool event(event_t* event) override;

    private:
        friend class thread_t;

        /// @brief Reset the exit state (the first half of exec()).
        /// @note Split out for thread_t::exec(), which must reset the state **before** it publishes
        ///       the loop to exit() callers on other threads: a reset after the publication could
        ///       clear a request that had already arrived, leaving that loop running forever.
        /// @note noexcept.
        void prepare_exec() noexcept;

        /// @brief Run the loop until it is asked to return (the second half of exec()).
        /// @param flags Event-processing flags.
        /// @param pump Which event sources to drain.
        /// @return The exit code.
        /// @note Requires prepare_exec() first; thread_t::exec() calls the pair around the
        ///       publication of the loop pointer.
        int run_exec(process_events_type_t flags, loop_pump_enum pump);

        /// @brief Whether the loop is running.
        std::atomic<bool> _running{ false };

        /// @brief Whether a quit has been requested (atomic: exit() is callable from any thread).
        std::atomic<bool> _quit{ false };

        /// @brief Exit code; stored before _quit so a thread that sees the request also sees it.
        std::atomic<int> _exit_code{ 0 };

        /// @brief The dispatcher of the thread running the loop, set by run_exec().
        ///
        /// exit() is callable from another thread, so it cannot use event_dispatcher::instance()
        /// (that would answer the CALLER's dispatcher). Dispatchers are never destroyed, so the
        /// pointer stays valid for the object's lifetime.
        std::atomic<event_dispatcher*> _dispatcher{ nullptr };

        /// @brief The phase policy; std::nullopt means event-by-event mode.
        /// @note Set before exec() from the loop's own thread; exec() snapshots it when it starts.
        std::optional<event_phase_t> _phase;
    };
}

#endif
