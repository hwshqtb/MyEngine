#ifndef HWSHQTB__METAOBJECT__RUNNABLE_H
#define HWSHQTB__METAOBJECT__RUNNABLE_H

/// @file runnable_t.h
/// @brief A task a thread_pool_t can run (QRunnable equivalent).

#include "export.h"

namespace hwshqtb::metaobject {
    /// @brief A task runnable by a thread_pool_t (QRunnable equivalent).
    ///
    /// Derive and implement run(); hand the object to thread_pool_t::start() / try_start().
    /// Ownership follows auto_delete(): true (the default, like Qt) means the pool deletes the task
    /// after run() — on the worker thread that ran it; false means the caller keeps ownership and
    /// must delete it once the pool reports the task finished (wait_for_done()).
    ///
    /// @note A runnable that is also an object_t is deleted on the worker thread; ~object_t lazily
    ///       creates that thread's dispatcher, which needs no platform initialisation.
    /// @sa thread_pool_t
    class HWSHQTB_EXPORT runnable_t {
    public:
        /// @brief Virtual destructor: the pool deletes tasks through this type.
        virtual ~runnable_t();

        /// @brief The task body; runs on a pool worker thread.
        /// @note An exception escaping run() is caught by the pool, logged as Critical and does not
        ///       stop the worker (unlike Qt, where the worker thread dies; see thread_pool_t).
        virtual void run() = 0;

        /// @brief Whether the pool must delete this task after run().
        /// @return true by default, like QRunnable::autoDelete().
        /// @note noexcept.
        virtual bool auto_delete() const;
    };
}

#endif
