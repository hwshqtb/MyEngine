#include <hwshqtb/metaobject/core_application.h>

#include <hwshqtb/metaobject/event_t.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/event_loop_t.h>
#include <hwshqtb/metaobject/thread_data_t.h>
#include <hwshqtb/metaobject/type_interface_t.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>

namespace hwshqtb::metaobject {
    std::string core_application::_application_name{};

    std::string core_application::_application_version{};

    std::string core_application::_organization_name{};

    std::string core_application::_organization_domain{};

    namespace {
        /// @brief The process-wide application instance (QCoreApplication::self).
        std::atomic<core_application*> g_instance{ nullptr };

        /// @brief Re-assert the builtin type NAMES in the one process-wide registry.
        ///
        /// The LIST and its fixed order moved to the registry's own translation unit:
        /// detail::register_default_types() (src/type_registry.cpp) owns it now and runs it from a
        /// static initialiser, so a by-name lookup answers before any application object exists
        /// (QMetaType::fromName works without a QCoreApplication for the same reason).
        ///
        /// @note Kept as the constructor's first step, and forwarding on purpose: construction must
        ///       still leave the same set registered, and if a link ever dropped the registry's
        ///       static initialiser this call restores the pre-existing guarantee that the builtin
        ///       ids keep the 1..N prefix (they are handed out before anything else the application
        ///       does). Idempotent: after the load-time run every call is an atomic load, so a second
        ///       application instance costs nothing and never renumbers anything.
        void register_builtin_types() {
            detail::register_default_types();
        }
    }

    core_application::core_application(int& argc, char** argv):
        _argc(argc),
        _argv(argv),
        // Remember the constructing thread BEFORE anything else (QCoreApplicationPrivate::threadData):
        // exit()/quit() are callable from any thread and broadcast to THIS thread's loops.
        _application_thread(&detail::thread_data_t::current()) {
        // Idempotent re-assertion of the builtin type names, before anything else the application
        // does. The library already registered them when it was loaded (see register_builtin_types
        // below), so this is normally a no-op; it is kept first so the constructor still guarantees
        // the builtin ids keep the 1..N prefix even if that load-time run did not happen.
        register_builtin_types();
        // Construction registers this as the global instance (QCoreApplication::self = this).
        core_application* expected = nullptr;
        g_instance.compare_exchange_strong(expected, this,
            std::memory_order_acq_rel, std::memory_order_acquire);
        // Start the platform side of whatever dispatcher is installed (the built-in software one
        // does nothing).
        event_dispatcher::instance().start_platform();
    }

    core_application::~core_application() {
        // Tear down in reverse order: the dispatcher first, then unregister the global instance.
        // Every exec() runs its own loop on the stack.
        event_dispatcher::instance().stop_platform();
        core_application* expected = this;
        g_instance.compare_exchange_strong(expected, nullptr,
            std::memory_order_acq_rel, std::memory_order_acquire);
    }

    core_application* core_application::instance() noexcept {
        return g_instance.load(std::memory_order_acquire);
    }

    std::vector<std::string_view> core_application::arguments() {
        std::vector<std::string_view> result;
        core_application* app = instance();
        if (app == nullptr || app->_argv == nullptr || app->_argc <= 0) {
            return result;
        }
        result.reserve(static_cast<std::size_t>(app->_argc));
        for (int i = 0; i < app->_argc; ++i) {
            result.emplace_back(app->_argv[i]);
        }
        return result;
    }

    std::string core_application::application_dir_path() {
        core_application* app = instance();
        if (app == nullptr || app->_argv == nullptr || app->_argc <= 0 || app->_argv[0] == nullptr) {
            return { };
        }
        std::error_code ec;
        // absolute() resolves the path against the current directory; parent_path() strips the
        // executable name. Both are noexcept-by-error_code.
        const auto exe = std::filesystem::absolute(app->_argv[0], ec);
        if (ec) {
            return { };
        }
        return exe.parent_path().string();
    }

    void core_application::set_event_dispatcher(event_dispatcher* dispatcher) noexcept {
        event_dispatcher::set_instance(dispatcher);
    }

    event_dispatcher* core_application::event_dispatcher_instance() noexcept {
        return event_dispatcher::replacement();
    }

    int core_application::exec() {
        // No application object means no loop to run: exec() returns 0 even if this thread happens to
        // have loops of its own.
        core_application* app = instance();
        if (app == nullptr) {
            return 0;
        }
        // about_to_quit is the OUTERMOST exec()'s event (QCoreApplication::aboutToQuit): an exec()
        // started from a slot is a nested loop and must not emit it again. loop_level() is 0 only
        // when this thread is not already inside any loop, and the flag is captured before this
        // call's loop registers itself. The guard emits on every exit path, including exceptions.
        struct about_to_quit_guard {
            core_application* app = nullptr;
            bool outermost = false;

            ~about_to_quit_guard() {
                if (outermost && app != nullptr) {
                    app->about_to_quit();
                }
            }
        } guard{ app, event_loop_t::loop_level() == 0 };
        // One loop per exec() call, owned by this call and living on the stack (QEventLoop
        // semantics): a nested exec() cannot reset or share the outer loop's quit state, and
        // exit()/quit() reach every loop of the APPLICATION thread through that thread's
        // active-loop stack. The phase policy is snapshotted HERE: a running loop keeps the policy it
        // started with, and a set_event_phase() made from a slot applies to the next exec().
        const std::optional<event_phase_t> phase = app->_event_phase;
        event_loop_t loop;
        loop.set_phase(phase);
        return loop.exec(process_events_type_flag::APPLICATION_EXEC);
    }

    void core_application::set_event_phase(std::optional<event_phase_t> phase) {
        _event_phase = phase;
    }

    std::optional<event_phase_t> core_application::phase() const noexcept {
        return _event_phase;
    }

    void core_application::quit() {
        // The single rule lives in event_loop_t::quit(): exit(0).
        exit(0);
    }

    void core_application::exit(int return_code) {
        // QCoreApplication::exit() does nothing without an application object.
        core_application* app = instance();
        if (app == nullptr || app->_application_thread == nullptr) {
            return;
        }
        // Broadcast to EVERY loop running on the APPLICATION thread — the thread that constructed
        // this instance, not the caller's (QCoreApplicationPrivate::threadData) — outermost first:
        // the order the loops were entered. Because the target is the application thread, a worker's
        // quit() stops the application instead of walking the worker's own stack, and a request from
        // a nested loop still stops the outer loops of the application thread.
        //
        // The application thread pushes/pops these nodes while this thread walks them, so the walk
        // takes the same mutex. No lock inversion is possible: nothing called while holding it takes
        // the dispatcher's queue locks, and push_loop()/pop_loop() take no other lock.
        detail::thread_data_t& data = *app->_application_thread;
        const std::lock_guard<std::mutex> lock(data.loops_mutex);
        for (detail::event_loop_stack_node_t* node = data.active_loops; node != nullptr; node = node->next) {
            if (node->loop != nullptr) {
                node->loop->exit(return_code);
            }
        }
    }

    // ---- event posting and dispatching (QCoreApplication-style API) ----

    bool core_application::send_event(object_t* receiver, event_t* event) {
        if (receiver == nullptr || event == nullptr) {
            return false;
        }
        event->set_spontaneous(); // the sendEvent path marks the event spontaneous (QEvent::spontaneous)
        // notify is a virtual member: dispatch through the global instance, or deliver directly
        // when no instance exists.
        core_application* app = instance();
        return (app != nullptr) ? app->notify(receiver, event) : receiver->event(event);
    }

    void core_application::post_event(object_t* receiver, event_t* event, event_priority_enum priority) {
        event_dispatcher::instance().post_event(receiver, event, priority);
    }

    void core_application::send_posted_events(object_t* receiver, event_type_enum type) {
        event_dispatcher::instance().send_posted_events(receiver, type);
    }

    void core_application::remove_posted_events(object_t* receiver, event_type_enum type) {
        event_dispatcher::instance().remove_posted_events(receiver, type);
    }

    void core_application::process_events(process_events_type_t flags) {
        event_dispatcher::instance().process_events(flags);
    }

    void core_application::process_events(process_events_type_t flags, int maximum_time) {
        // A call made from inside a running loop belongs to that loop object: going through it makes
        // the loop's phase policy (snapshotted at exec() start) apply to this nested processing too.
        // Outside any loop the historical dispatcher rounds are kept.
        detail::thread_data_t& data = detail::thread_data_t::current();
        event_loop_t* active_loop = nullptr;
        {
            const std::lock_guard<std::mutex> lock(data.loops_mutex);
            if (data.innermost_loop != nullptr) {
                active_loop = data.innermost_loop->loop;
            }
        }
        if (active_loop != nullptr) {
            active_loop->process_events(flags, maximum_time);
            return;
        }
        // Runs dispatcher rounds until the deadline, with or without a running loop.
        auto& dispatcher = event_dispatcher::instance();
        const auto span = std::chrono::milliseconds(maximum_time > 0 ? maximum_time : 0);
        const auto deadline = std::chrono::steady_clock::now() + span;
        do {
            dispatcher.process_events(flags);
        }
        while (std::chrono::steady_clock::now() < deadline);
    }

    bool core_application::notify(object_t* receiver, event_t* event) {
        if (receiver == nullptr || event == nullptr) {
            return false;
        }
        // (1) event filters: called in installation order; the first one returning true consumes
        //     the event.
        if (receiver->apply_event_filters(event)) {
            event->accept();
            return true;
        }
        // (2) the target object's own event handling.
        return receiver->event(event);
    }

    int core_application::argc() const noexcept {
        return _argc;
    }

    char** core_application::argv() const noexcept {
        return _argv;
    }

    std::string_view core_application::application_name() noexcept {
        return _application_name;
    }

    void core_application::set_application_name(std::string_view name) {
        _application_name.assign(name);
    }

    std::string_view core_application::application_version() noexcept {
        return _application_version;
    }

    void core_application::set_application_version(std::string_view version) {
        _application_version.assign(version);
    }

    std::string_view core_application::organization_name() noexcept {
        return _organization_name;
    }

    void core_application::set_organization_name(std::string_view name) {
        _organization_name.assign(name);
    }

    std::string_view core_application::organization_domain() noexcept {
        return _organization_domain;
    }

    void core_application::set_organization_domain(std::string_view domain) {
        _organization_domain.assign(domain);
    }
}

META_OBJECT(hwshqtb::metaobject::core_application);