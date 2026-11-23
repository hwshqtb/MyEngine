#ifndef HWSHQTB__METAOBJECT__THREAD_T_H
#define HWSHQTB__METAOBJECT__THREAD_T_H

/// @file thread_t.h
/// @brief A thread object with its own event loop (the thread_t equivalent).

#include "object_t.h"
#include "export.h"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace hwshqtb::metaobject {
    class event_loop_t;

    /// @brief A thread object with its own event loop (thread_t equivalent).
    ///
    /// Like Qt's thread_t, the object itself belongs to the thread that created it; only run()
    /// executes on the new thread. The new thread gets its own thread_data_t (and therefore its own
    /// event dispatcher) before run() is called.
    ///
    /// @note Named @c thread_t because @c thread clashes with platform headers.
    /// @note Every thread that has data owns an identity of this type: thread_data_t::current()
    ///       adopts one for threads not started through a thread_t object (including the main
    ///       thread). @sa object_t::thread
    /// @sa event_loop_t::loop_pump_enum
    class HWSHQTB_EXPORT thread_t: public object_t {
        OBJECT

        friend class object_t;
        /// @brief The adopted identity is created by thread_data_t::current().
        friend struct detail::thread_data_t;
        /// @brief event_dispatcher::instance(thread_t*) reads this object's data.
        friend class event_dispatcher;

    public:
        /// @brief Construct the thread object (does not start it).
        /// @param parent Optional parent, deleted with it.
        explicit thread_t(object_t* parent = nullptr);

        /// @brief Stop (if running) and join, so the OS thread never outlives the object.
        ~thread_t() override;

        /// @brief The thread body. The default implementation runs exec().
        virtual void run();

        /// @brief Create the OS thread and run run() on it.
        /// @note Idempotent while running.
        void start();

        /// @brief Ask this thread's event loop to return (equivalent to exit(0)).
        void quit();

        /// @brief Ask this thread's event loop to return with a code.
        /// @param return_code The value exec() will return.
        /// @note Callable from any thread. A running loop is asked directly (event_loop_t::exit());
        ///       while no loop is published yet the code is remembered and the next exec() returns
        ///       it immediately, without running a round.
        void exit(int return_code);

        /// @brief Wait for the thread to finish.
        /// @param milliseconds -1 waits forever; otherwise the timeout in milliseconds.
        /// @return true if the thread finished; false on timeout, or false immediately if start()
        ///         was never called (there is nothing to wait for and nothing to join).
        bool wait(int milliseconds = -1);

        /// @brief Run this thread's event loop until quit() / exit().
        /// @return The return code passed to exit(), or 0.
        /// @note Constructs an event_loop_t on the calling thread and runs it with
        ///       loop_pump_enum::POSTED_ONLY: this thread's posting queue only, because the native
        ///       pump belongs to the main thread.
        /// @note Nested calls are not supported: exit()/quit() target the innermost running loop.
        int exec();

        /// @brief Whether the thread is currently running.
        /// @note noexcept.
        bool is_running() const noexcept;

        /// @brief The thread object of the calling thread.
        /// @return The thread_t running on this thread, or nullptr for the main thread.
        /// @note noexcept.
        static thread_t* current_thread();

    private:
        /// @brief Adopt an existing thread data as the identity of the thread it belongs to.
        /// @param data The data to bind; it must outlive this object.
        /// @note Private: only thread_data_t::current() creates an adopted identity. It binds BOTH
        ///       the data this object runs (@c _data) and its own affinity (@c object_t::_thread) to
        ///       @p data, and run() must never be reached for it.
        /// @note An adopted identity can be superseded: thread_t::start() rebinds the data's owner to
        ///       the started object before publishing it as running, which leaves this object
        ///       unreferenced. That is expected — an adopted object is never destroyed (process
        ///       lifetime), and the data it points to lives as long as its thread.
        explicit thread_t(detail::thread_data_t* data);

        std::thread _thread;
        mutable std::mutex _mutex;
        std::condition_variable _cv;
        bool _running = false;
        bool _finished = false;
        /// @brief Whether start() was ever called (an OS thread was created); guarded by _mutex.
        bool _started = false;
        /// @brief The loop exec() currently runs on the worker; nullptr while none is published.
        /// @note Guarded by _mutex.
        event_loop_t* _loop = nullptr;
        /// @brief A quit()/exit() that arrived while no loop was published, consumed by the next
        ///        exec().
        /// @note Not cleared by start(): an exit() issued before run() reaches exec() still
        ///       applies to the next exec().
        bool _pending_exit = false;
        /// @brief The return code that belongs to _pending_exit.
        int _pending_exit_code = 0;
        detail::thread_data_t* _data = nullptr;
        /// @brief Whether this object is an adopted identity (created by thread_data_t::current()).
        /// @note An adopted identity is never started; run() reports the misuse instead of looping.
        bool _adopted = false;
    };
}

#endif
