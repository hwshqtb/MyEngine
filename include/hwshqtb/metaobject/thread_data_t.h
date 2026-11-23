#ifndef HWSHQTB__METAOBJECT__THREAD_DATA_H
#define HWSHQTB__METAOBJECT__THREAD_DATA_H

/// @file thread_data_t.h
/// @brief Per-thread bookkeeping (the thread_tData equivalent).

#include <atomic>
#include <mutex>
#include <thread>

namespace hwshqtb::metaobject {
    class event_dispatcher;
    class binding_scheduler_t;
    class event_loop_t;
    class thread_t;

    namespace detail {
        class binding_context_t;

        /// @brief One entry of the per-thread stack of running event loops.
        ///
        /// The node is a local of the event_loop_t::run_exec() call that runs the loop, so
        /// registration allocates nothing.
        struct event_loop_stack_node_t {
            /// @brief The loop this node registers.
            event_loop_t* loop = nullptr;
            /// @brief The next-outer loop; nullptr for the outermost.
            event_loop_stack_node_t* previous = nullptr;
            /// @brief The next-inner loop; nullptr for the innermost.
            event_loop_stack_node_t* next = nullptr;
        };

        /// @brief Per-thread bookkeeping (thread_tData equivalent).
        ///
        /// One instance per thread, created on first use by thread_data_t::current() and living until
        /// the thread exits. It holds the thread identity, the lazily created event dispatcher, the
        /// binding scheduler and the installed binding-evaluation context, and the stack of event
        /// loops currently running on the thread (outermost, innermost and nesting depth).
        ///
        /// @note Reach it through thread_data_t::current(). The dispatcher, scheduler and context are
        ///       non-owning pointers; the active-loop stack and the depth are guarded by
        ///       @c loops_mutex (the depth is atomic so it can also be read without the lock).
        struct thread_data_t {
            /// @brief This thread's identity (captured when the instance is created).
            std::thread::id id = std::this_thread::get_id();

            /// @brief The thread object that carries this thread's identity to the public API.
            /// @note Created by current() on first use, so it is non-null for every thread that has
            ///       data. thread_t::start() replaces it with the started object and clears it again
            ///       when that thread finishes. The adopted object is never destroyed.
            thread_t* owner = nullptr;

            /// @brief The thread's event dispatcher; created lazily by get_dispatcher().
            event_dispatcher* dispatcher = nullptr;

            /// @brief The thread's binding scheduler; created lazily by
            ///        binding_scheduler_t::current().
            /// @note A raw pointer: the scheduler is never destroyed.
            binding_scheduler_t* bindings = nullptr;

            /// @brief The binding-evaluation context installed on this thread; nullptr when no
            ///        binding is being evaluated.
            /// @note The context is a caller-owned stack object; context_scope_t installs and
            ///       restores the slot.
            binding_context_t* context = nullptr;

            /// @brief The outermost event loop running on this thread; nullptr when none runs.
            /// @note Guarded by @c loops_mutex; @c next walks from the outermost towards the
            ///       innermost loop.
            event_loop_stack_node_t* active_loops = nullptr;

            /// @brief The innermost event loop running on this thread; nullptr when none runs.
            /// @note Guarded by @c loops_mutex.
            event_loop_stack_node_t* innermost_loop = nullptr;

            /// @brief Event-loop nesting depth: the number of loops in @c active_loops.
            /// @note std::atomic so event_loop_t::loop_level() can read it without @c loops_mutex.
            std::atomic<int> loop_level{ 0 };

            /// @brief Guards @c active_loops, @c innermost_loop and the @c loop_level updates.
            /// @note Held only for the stack operation or the broadcast walk, never across an
            ///       event_loop_t::exec() or an event dispatcher round.
            std::mutex loops_mutex;

            /// @brief Push @p node as the innermost running loop of this thread.
            /// @param node The node to push; it must outlive the pop_loop() call.
            /// @note Locks @c loops_mutex; the push itself allocates nothing.
            void push_loop(event_loop_stack_node_t& node) noexcept {
                const std::lock_guard<std::mutex> lock(loops_mutex);
                node.previous = innermost_loop;
                node.next = nullptr;
                if (node.previous != nullptr) {
                    node.previous->next = &node;
                }
                else {
                    active_loops = &node;
                }
                innermost_loop = &node;
                ++loop_level;
            }

            /// @brief Remove @p node, which must be the innermost running loop of this thread.
            /// @param node The node returned by the matching push_loop() call.
            /// @note Locks @c loops_mutex; the pop itself allocates nothing.
            void pop_loop(event_loop_stack_node_t& node) noexcept {
                const std::lock_guard<std::mutex> lock(loops_mutex);
                if (node.previous != nullptr) {
                    node.previous->next = nullptr;
                }
                else {
                    active_loops = nullptr;
                }
                innermost_loop = node.previous;
                node.previous = nullptr;
                node.next = nullptr;
                --loop_level;
            }

            /// @brief Get the calling thread's data.
            /// @return A reference to the thread-local instance.
            /// @note noexcept; creates the instance on first use.
            /// @note The first call on a thread also adopts that thread's identity: a heap-allocated
            ///       thread_t bound to this data is stored in @c owner. The thread-local data is
            ///       registered BEFORE that object is constructed, because constructing a thread_t
            ///       constructs an object_t, whose constructor calls current() again — with the slot
            ///       still empty that call would recurse forever. The adopted object is never
            ///       destroyed, exactly like the thread's dispatcher.
            static thread_data_t& current() noexcept;

            /// @brief Get the calling thread's dispatcher.
            /// @return A reference to the thread's dispatcher, created on first use.
            /// @note Defined inline so that public header code can call it.
            event_dispatcher& get_dispatcher() {
                // Only reading here: initialization belongs to current().
                return (dispatcher != nullptr) ? *dispatcher : current().get_dispatcher();
            }
        };
    }
}

#endif
