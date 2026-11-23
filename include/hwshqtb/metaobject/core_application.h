#ifndef HWSHQTB__METAOBJECT__CORE_APPLICATION_H
#define HWSHQTB__METAOBJECT__CORE_APPLICATION_H

#include "object_t.h"
#include "event_t.h"
#include "event_dispatcher.h"
#include "macros.h"
#include "export.h"

#include <optional>
#include <string>
#include <string_view>

namespace hwshqtb::metaobject {
    class event_loop_t;

    /// @brief Application object (QCoreApplication equivalent): global instance + event-loop entry
    ///        points + application metadata.
    ///
    /// Construction registers the object as the global instance (QCoreApplication::self) and
    /// destruction unregisters it.
    ///
    /// @note Only one instance should exist at a time: the registration performed by a second
    ///       construction fails and is ignored (the first one stays global).
    /// @note The application metadata (name / version / organisation) is static storage and may be
    ///       set before the instance exists.
    /// @note Construction starts the platform side of the dispatcher installed on this thread (the
    ///       built-in software dispatcher does nothing); destruction stops it. A platform
    ///       implementation installed after this object was constructed (the GUI layer installs one)
    ///       is started by its installer.
    /// @note The TYPE registry is process-wide and independent of the instance, so @c meta_type_t /
    ///       @c any_t work without an application object. The builtin type names are registered when
    ///       the library is loaded (detail::register_default_types), so resolve_global() answers the
    ///       primitives and the std::string / std::wstring aliases before an instance exists; the
    ///       constructor re-asserts them idempotently.
    /// @par Phases
    /// set_event_phase() installs an event_phase_t on the application; exec() hands it to the
    /// event_loop_t it creates. Set it before exec(): a running loop snapshots its phase when it
    /// starts, so a change made while the loop runs only affects a later exec().
    /// @warning Emitting a signal on a BlockingQueued connection from the same thread always
    ///          deadlocks (same as Qt).
    /// @sa object_t
    /// @sa event_loop_t
    class HWSHQTB_EXPORT core_application: public object_t {
        OBJECT

    public:
        /// @brief Construct and register as the global instance.
        /// @param argc Argument count (a non-const reference, matching Qt).
        /// @param argv Argument vector.
        explicit core_application(int& argc, char** argv);

        /// @brief Destroy and unregister the global instance.
        ~core_application() override;

        /// @brief Copy and move are disabled: the application instance is unique and identified by
        ///        its address.
        core_application(const core_application&) = delete;
        core_application& operator=(const core_application&) = delete;
        core_application(core_application&&) = delete;
        core_application& operator=(core_application&&) = delete;

        /// @brief Get the global instance (QCoreApplication::instance).
        /// @return The constructed instance; nullptr if none exists.
        /// @note noexcept.
        static core_application* instance() noexcept;

        /// @brief The command-line arguments (QCoreApplication::arguments).
        /// @return The arguments including argv[0]; empty when there is no application instance.
        /// @note The views point into the argv storage owned by the constructor.
        static std::vector<std::string_view> arguments();

        /// @brief The directory that contains the executable (QCoreApplication::applicationDirPath).
        /// @return An absolute directory path; empty when it cannot be determined.
        static std::string application_dir_path();

        /// @brief Install a replacement event dispatcher (QCoreApplication::setEventDispatcher).
        /// @param dispatcher The dispatcher to use on this thread; nullptr restores the default.
        /// @note The dispatcher is not owned. Install it from the thread that will run the loop and
        ///       before exec() starts. Forwards to event_dispatcher::set_instance().
        /// @note noexcept.
        static void set_event_dispatcher(event_dispatcher* dispatcher) noexcept;

        /// @brief The dispatcher currently in use (QCoreApplication::eventDispatcher).
        /// @return The installed replacement, or the thread's default dispatcher.
        /// @note noexcept.
        static event_dispatcher* event_dispatcher_instance() noexcept;

        /// @brief Enter the event loop (QCoreApplication::exec).
        /// @return The exit code (0 for quit(), rc for exit(rc)).
        /// @note Each call runs its OWN event_loop_t on the calling thread's stack (QEventLoop
        ///       semantics): an exec() started from a slot is a nested loop and does not reset or
        ///       share the outer loop's quit state. quit()/exit() broadcast to every loop running on
        ///       the APPLICATION thread (the thread that constructed this instance), so a request
        ///       from a nested loop stops the whole application, and a worker can stop it too.
        /// @note A quit request from the platform implementation also makes it return.
        /// @note Returns 0 without running a loop when there is no application instance.
        static int exec();

        /// @brief Run the application's event loop in phased mode; std::nullopt restores the
        ///        event-by-event behaviour.
        /// @param phase The phase policy, or std::nullopt.
        /// @note Must be called before exec(): a running loop snapshots its phase when it starts, so
        ///       a change made while the loop runs only affects a later exec().
        /// @note The setting lives on the application object; exec() hands it to the event_loop_t it
        ///       creates. process_events(flags, maximum_time) called from inside that loop goes
        ///       through the same loop object and therefore inherits the policy.
        /// @note Not synchronized: set it from the application thread before exec().
        void set_event_phase(std::optional<event_phase_t> phase);

        /// @brief The phase policy installed on this application.
        /// @return The policy set by set_event_phase(), or std::nullopt for event-by-event mode.
        /// @note noexcept.
        std::optional<event_phase_t> phase() const noexcept;

        /// @brief Emitted once, when the OUTERMOST exec() on the application thread returns
        ///        (QCoreApplication::aboutToQuit); a nested exec() return does not emit it again.
        SIGNAL(void about_to_quit() const)

        /// @brief Request every event loop running on the application thread to stop, with code 0
        ///        (QCoreApplication::quit = exit(0)).
        static void quit();

        /// @brief Leave every event loop running on the application thread with the given return code
        ///        (QCoreApplication::exit).
        /// @param return_code Exit code.
        /// @note Broadcasts outermost-first to the loops of the thread that CONSTRUCTED this
        ///       instance (QCoreApplicationPrivate::threadData), like Qt, so it works from any
        ///       thread: a worker's quit() stops the application. Loops running on any other thread
        ///       — a worker's own thread_t::exec(), for example — are not application loops and are
        ///       not reached; stop those with thread_t::exit() / event_loop_t::exit().
        static void exit(int return_code = 0);

        /// @name Event posting and dispatching (the QCoreApplication-style API)
        /// @{
        /// @brief Dispatch an event synchronously (QCoreApplication::sendEvent).
        /// @param receiver The receiver; nullptr -> returns false.
        /// @param event The event; ownership is **not** taken (it may live on the stack); the event
        ///        is marked spontaneous before dispatching.
        /// @return true if the event was accepted.
        static bool send_event(object_t* receiver, event_t* event);

        /// @brief Post an event asynchronously (QCoreApplication::postEvent).
        /// @param receiver The receiver; nullptr -> the event is destroyed immediately.
        /// @param event The event; ownership **is** taken and it is deleted after dispatching.
        /// @param priority Priority; higher priorities are dispatched first.
        static void post_event(object_t* receiver, event_t* event, event_priority_enum priority = event_priority_enum::NORMAL);

        /// @brief Dispatch posted events immediately (QCoreApplication::sendPostedEvents).
        /// @param receiver Dispatch only to this receiver; nullptr -> all.
        /// @param type Dispatch only this type; None -> all types.
        static void send_posted_events(object_t* receiver = nullptr,
            event_type_enum type = event_type_enum::NONE);

        /// @brief Remove and destroy posted events (QCoreApplication::removePostedEvents).
        /// @param receiver Target receiver; nullptr -> all.
        /// @param type Target type; None -> all types.
        static void remove_posted_events(object_t* receiver, event_type_enum type = event_type_enum::NONE);

        /// @brief Process one round of events (QCoreApplication::processEvents).
        /// @param flags Event-processing flags.
        static void process_events(process_events_type_t flags = process_events_type_flag::ALL_EVENTS);

        /// @brief Keep processing events for up to @p maximum_time milliseconds.
        /// @param flags Event-processing flags.
        /// @param maximum_time Maximum processing time in milliseconds.
        /// @note When called from inside a running loop it goes through that loop object, so the
        ///       phase policy that loop snapshotted at exec() start is inherited. Outside any loop it
        ///       keeps the historical dispatcher-round behaviour.
        static void process_events(process_events_type_t flags, int maximum_time);

        /// @brief Dispatching hub (QCoreApplication::notify): run the event filters first, then hand
        ///        the event to receiver->event().
        /// @param receiver The receiver; nullptr -> returns false.
        /// @param event The event; nullptr -> returns false.
        /// @return true if the event was accepted.
        /// @note Virtual: override it to intercept events globally.
        virtual bool notify(object_t* receiver, event_t* event);
        /// @}

        /// @brief Argument count.
        /// @return The @c argc passed to the constructor.
        /// @note noexcept.
        int argc() const noexcept;

        /// @brief Argument vector.
        /// @return The @c argv passed to the constructor.
        /// @note noexcept.
        char** argv() const noexcept;

        /// @name Application metadata (static; may be set before the instance exists)
        /// @{
        /// @brief Get the application name.
        /// @return The current name; empty when unset.
        /// @note noexcept.
        static std::string_view application_name() noexcept;

        /// @brief Set the application name.
        /// @param name The new name.
        static void set_application_name(std::string_view name);

        /// @brief Get the application version.
        /// @return The current version string; empty when unset.
        /// @note noexcept.
        static std::string_view application_version() noexcept;

        /// @brief Set the application version.
        /// @param version The new version string.
        static void set_application_version(std::string_view version);

        /// @brief Get the organisation name.
        /// @return The current organisation name; empty when unset.
        /// @note noexcept.
        static std::string_view organization_name() noexcept;

        /// @brief Set the organisation name.
        /// @param name The new organisation name.
        static void set_organization_name(std::string_view name);

        /// @brief Get the organisation domain.
        /// @return The current organisation domain; empty when unset.
        /// @note noexcept.
        static std::string_view organization_domain() noexcept;

        /// @brief Set the organisation domain.
        /// @param domain The new organisation domain.
        static void set_organization_domain(std::string_view domain);
        /// @}

    private:
        /// @brief Argument count.
        int _argc = 0;

        /// @brief Argument vector.
        char** _argv = nullptr;

        /// @brief The thread_data_t of the thread that CONSTRUCTED this application
        ///        (QCoreApplicationPrivate::threadData).
        ///
        /// exit()/quit() broadcast to this thread's active-loop stack, so the request works from any
        /// thread (a worker's quit() stops the application) and a worker's own loops are never
        /// targets. Captured by the constructor, while the thread is necessarily alive; the
        /// thread_data_t lives until the thread exits, so for an application constructed and destroyed
        /// on its own thread (the Qt rule) the pointer is valid for the application's whole lifetime.
        detail::thread_data_t* _application_thread = nullptr;

        /// @brief The phase policy exec() hands to the event_loop_t it creates.
        /// @note std::nullopt keeps the event-by-event behaviour. Set it before exec(); a running
        ///       loop snapshots the value when it starts.
        std::optional<event_phase_t> _event_phase;

        /// @brief Storage for the application name.
        static std::string _application_name;

        /// @brief Storage for the application version.
        static std::string _application_version;

        /// @brief Storage for the organisation name.
        static std::string _organization_name;

        /// @brief Storage for the organisation domain.
        static std::string _organization_domain;
    };
}

#endif
