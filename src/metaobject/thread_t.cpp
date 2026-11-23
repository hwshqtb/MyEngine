/// @file thread_t.cpp
/// @brief A thread object with its own event loop (the thread_t equivalent).

#include "hwshqtb/metaobject/thread_t.h"

#include "hwshqtb/metaobject/event_loop_t.h"
#include "hwshqtb/metaobject/log.h"
#include "hwshqtb/metaobject/thread_data_t.h"

#include <chrono>
#include <utility>

namespace hwshqtb::metaobject {
    namespace {
        /// The thread_t running on the calling thread (nullptr on the main thread).
        thread_local thread_t* g_current = nullptr;
    }

    thread_t::thread_t(object_t* parent) {
        if (parent != nullptr) {
            set_parent(parent);
        }
    }

    thread_t::thread_t(detail::thread_data_t* data) {
        // The adopted identity runs where the data already lives, so the data is also the object's
        // own affinity; _adopted keeps run() from ever looping on it. The affinity member is spelled
        // out because this class has a member of its own named _thread.
        _data = data;
        object_t::_thread = data;
        _adopted = true;
    }

    thread_t::~thread_t() {
        quit();
        wait(-1);
        // Defensive: a joinable std::thread must never be destroyed (std::terminate).
        if (_thread.joinable()) {
            _thread.join();
        }
    }

    void thread_t::run() {
        if (_adopted) {
            // The analogue of QAdoptedThread::run()'s qFatal: an adopted identity belongs to a thread
            // that is already running, so reaching this body means start() was called on it.
            HWSHQTB_LOG_RAW(log_events(), log_level_enum::FATAL,
                "thread_t::run(): an adopted thread identity must never be started");
            return;
        }
        exec();
    }

    void thread_t::start() {
        std::thread previous;
        {
            std::scoped_lock lock(_mutex);
            if (_running) {
                return;
            }
            if (_started && !_finished) {
                // A start is already in flight: the worker exists but has not published _running
                // yet. Idempotent, exactly like the running case — never join a live thread here.
                return;
            }
            if (_thread.joinable()) {
                // The worker finished but nobody called wait(): reap it before replacing it,
                // otherwise assigning a new std::thread would destroy a joinable one.
                previous = std::move(_thread);
            }
            _finished = false;
            // Set under the lock before the OS thread exists, so wait() can tell "not started yet"
            // apart from "starting".
            _started = true;
            _thread = std::thread([this] {
                // Establish this thread's data and dispatcher ON THIS THREAD before running anything.
                detail::thread_data_t& data = detail::thread_data_t::current();
                // The started object supersedes the identity current() just adopted for this thread
                // (Qt: QThreadPrivate::start stores `this` in the thread data). Before _running is
                // published, so no observer can read the superseded identity through a running thread.
                data.owner = this;
                {
                    std::scoped_lock lock(_mutex);
                    _data = &data;
                    _running = true; // only now is the thread's context usable
                }
                g_current = this;

                run();

                g_current = nullptr;
                // Release the identity before the thread-local data dies (Qt: stores nullptr when the
                // thread is finishing). After this point the thread has no identity to hand out.
                data.owner = nullptr;
                std::scoped_lock lock(_mutex);
                _running = false;
                _finished = true;
                _cv.notify_all();
            });
        }
        if (previous.joinable()) {
            // Joined outside the lock: the old worker may still need _mutex on its way out.
            previous.join();
        }
    }

    void thread_t::quit() {
        exit(0);
    }

    void thread_t::exit(int return_code) {
        {
            std::scoped_lock lock(_mutex);
            if (_loop != nullptr) {
                // Hand the code to the published loop under the lock, so the pointer cannot be
                // cleared (and the loop destroyed) while we are calling into it.
                _loop->exit(return_code);
            }
            else {
                // No loop yet: remember the code, the next exec() returns it without looping.
                _pending_exit = true;
                _pending_exit_code = return_code;
            }
        }
        _cv.notify_all();
    }

    bool thread_t::wait(int milliseconds) {
        std::unique_lock lock(_mutex);
        // A never-started thread is done immediately. A started thread is done only once the worker
        // published _finished: _running is still false during the startup window, so it must not be
        // used here.
        const auto done = [this] {
            return _finished || !_started;
        };
        if (milliseconds < 0) {
            _cv.wait(lock, done);
        }
        else if (!_cv.wait_for(lock, std::chrono::milliseconds(milliseconds), done)) {
            return false;
        }
        if (!_finished) {
            // Never started: nothing to wait for and nothing to join.
            return false;
        }
        lock.unlock();
        if (_thread.joinable()) {
            _thread.join();
        }
        return true;
    }

    int thread_t::exec() {
        // Constructed on THIS thread: its affinity, dispatcher and posting queue are this thread's,
        // and posted_only never pumps native messages (that pump belongs to the main thread).
        event_loop_t loop;
        // Reset the loop's exit state before publishing it below: a reset after the publication
        // could clear an exit() that had already reached the loop.
        loop.prepare_exec();
        event_loop_t* previous = nullptr;
        {
            std::scoped_lock lock(_mutex);
            if (_pending_exit) {
                // A quit()/exit() arrived while no loop was published: return its code right away
                // and consume it, so a later exec() runs normally.
                _pending_exit = false;
                const int code = _pending_exit_code;
                _pending_exit_code = 0;
                return code;
            }
            // From here on exit() hands the code straight to the loop instead of the pending slot.
            previous = _loop;
            _loop = &loop;
        }
        // Restores the pointer that was published before this call on every exit path, including an
        // exception from a slot: a finished loop is never reachable.
        struct loop_guard {
            thread_t* self;
            event_loop_t* previous;
            ~loop_guard() {
                std::scoped_lock lock(self->_mutex);
                self->_loop = previous;
            }
        } guard{ this, previous };

        return loop.run_exec(process_events_type_flag::ALL_EVENTS, loop_pump_enum::POSTED_ONLY);
    }

    bool thread_t::is_running() const noexcept {
        std::scoped_lock lock(_mutex);
        // Deliberately _running (published only once the worker's thread_data_t exists), NOT
        // _started && !_finished: callers poll this to know the worker context is usable.
        return _running;
    }

    thread_t* thread_t::current_thread() {
        return g_current;
    }
}

META_OBJECT(hwshqtb::metaobject::thread_t);
