#ifndef HWSHQTB__METAOBJECT__THREAD_POOL_H
#define HWSHQTB__METAOBJECT__THREAD_POOL_H

/// @file thread_pool_t.h
/// @brief A pool of worker threads running runnable_t tasks (QThreadPool equivalent).

#include "object_t.h"
#include "runnable_t.h"
#include "export.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace hwshqtb::metaobject {
    /// @brief A pool of worker threads that run runnable_t tasks (QThreadPool equivalent).
    ///
    /// @par Model
    /// Workers are plain std::threads with a mutex/condition_variable queue; they do **not** run an
    /// event loop (Qt parity: QThreadPool workers do not either). A worker is spawned lazily when a
    /// task is queued, no idle worker exists and fewer than max_thread_count() threads live; an idle
    /// worker exits after set_expiry_timeout() milliseconds (Qt's 30 s default).
    ///
    /// @par Lifecycle
    ///  - `start()` queues a task and returns; `try_start()` reports whether an idle worker took it
    ///    immediately (false means it was queued for later, exactly like QThreadPool::tryStart);
    ///  - `wait_for_done()` waits until the queue is empty and no task is running;
    ///  - the destructor drains first — every task still queued when the pool is destroyed still
    ///    runs — then shuts the workers down and joins them (QThreadPool's destructor waits, too);
    ///  - `clear()` drops queued tasks without running them, deleting the auto_delete ones.
    ///
    /// @par Exceptions
    /// An exception leaving run() is caught, reported as Critical through the log facade, and the
    /// worker keeps running (Qt instead lets the exception kill the worker thread).
    ///
    /// @par Threads and objects
    /// A task may be an object_t (multiple inheritance, like Qt's QObject+QRunnable); deleting it on
    /// the worker thread is safe. An object_t task can be moved to another thread with
    /// object_t::move_to_thread() and then delete_later() there.
    ///
    /// @warning The pool object belongs to the thread that creates it; its methods are safe to call
    ///          from any thread, but it must outlive every task it runs.
    /// @sa runnable_t
    class HWSHQTB_EXPORT thread_pool_t: public object_t {
        OBJECT

    public:
        /// @brief Construct an empty pool (no worker is started until the first task).
        /// @param parent Optional parent, deleted with it.
        explicit thread_pool_t(object_t* parent = nullptr);

        /// @brief Drain every queued task, then stop and join the workers.
        /// @note Like QThreadPool's destructor this waits: tasks already queued still run.
        ~thread_pool_t() override;

        /// @brief Queue a task (QThreadPool::start).
        /// @param task The task; nullptr is ignored. Ownership follows runnable_t::auto_delete().
        void start(runnable_t* task);

        /// @brief Run a task now if an idle worker exists, otherwise queue it (QThreadPool::tryStart).
        /// @param task The task; nullptr returns false.
        /// @return true when an idle worker took the task immediately; false when it was queued.
        /// @note A queued task is still guaranteed to run (unless clear() drops it).
        bool try_start(runnable_t* task);

        /// @brief Drop the queued tasks without running them (QThreadPool::clear).
        /// @note The auto_delete tasks among them are deleted; running tasks are unaffected.
        void clear();

        /// @brief The maximum number of worker threads (QThreadPool::maxThreadCount).
        /// @note noexcept.
        int max_thread_count() const noexcept;

        /// @brief Set the maximum number of worker threads.
        /// @param count The new maximum; values below 1 are clamped to 1.
        /// @note Shrinking takes effect as workers go idle: excess idle workers exit.
        void set_max_thread_count(int count);

        /// @brief The number of worker threads that currently exist (QThreadPool::activeThreadCount).
        /// @note noexcept; idle-but-alive workers are counted, workers that exited are not.
        int active_thread_count() const noexcept;

        /// @brief Wait until the queue is empty and no task is running.
        void wait_for_done();

        /// @brief How long an idle worker waits for work before exiting.
        /// @param milliseconds The timeout; negative values are clamped to 0.
        /// @note Qt's default is 30000 (30 s); this pool uses the same default.
        void set_expiry_timeout(int milliseconds);

        /// @brief The process-wide pool (QThreadPool::globalInstance).
        /// @return A reference to the one instance of the process.
        /// @note Defined out of line, deliberately never destroyed: safe during static teardown.
        static thread_pool_t& global_instance();

    private:
        /// @brief One worker thread and the flag it sets when its loop returns.
        struct worker_slot_t {
            std::thread thread;
            std::shared_ptr<std::atomic<bool>> done;
        };

        /// @brief The worker body: pop, run, repeat; exit on shutdown, expiry or shrink.
        void worker_loop(const std::shared_ptr<std::atomic<bool>>& done);

        /// @brief Spawn a worker when work is queued, no idle worker exists and the cap allows it.
        /// @note The caller must hold @c _mutex.
        void maybe_spawn_locked();

        /// @brief Join and drop the workers whose loop already returned.
        /// @note The caller must hold @c _mutex; joining a finished thread does not block.
        void reap_finished_locked();

        /// @brief Run one task and apply the auto-delete rule; never lets an exception escape.
        void run_task(runnable_t* task);

        /// @brief Delete @p task when it is auto_delete (used by clear() on dropped tasks).
        static void discard(runnable_t* task) noexcept;

        mutable std::mutex _mutex;
        std::condition_variable _cv;
        std::deque<runnable_t*> _queue;
        std::vector<worker_slot_t> _workers;
        std::size_t _active = 0;
        std::size_t _idle = 0;
        std::size_t _live = 0;
        int _max = 1;
        int _expiry_ms = 30000;
        /// @brief Bumped by set_expiry_timeout() so waiting workers re-read the timeout at once.
        unsigned int _expiry_generation = 0;
        bool _shutdown = false;
    };
}

#endif
