/// @file thread_pool_t.cpp
/// @brief A pool of worker threads running runnable_t tasks (QThreadPool equivalent).

#include "hwshqtb/metaobject/thread_pool_t.h"

#include "hwshqtb/metaobject/log.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>
#include <utility>

namespace hwshqtb::metaobject {
    runnable_t::~runnable_t() = default;

    thread_pool_t::thread_pool_t(object_t* parent) {
        if (parent != nullptr) {
            set_parent(parent);
        }
        const unsigned int hardware = std::thread::hardware_concurrency();
        _max = (hardware > 0) ? static_cast<int>(hardware) : 1;
    }

    thread_pool_t::~thread_pool_t() {
        // Qt parity: the destructor waits, so a task that was already queued still runs.
        wait_for_done();
        {
            std::scoped_lock lock(_mutex);
            _shutdown = true;
        }
        _cv.notify_all();
        // Take the worker list out and join every thread. Workers that already exited are joinable
        // as well, so this joins both the idle ones and the retired ones.
        std::vector<worker_slot_t> workers;
        {
            std::scoped_lock lock(_mutex);
            workers.swap(_workers);
        }
        for (worker_slot_t& slot : workers) {
            if (slot.thread.joinable()) {
                slot.thread.join();
            }
        }
    }

    void thread_pool_t::start(runnable_t* task) {
        if (task == nullptr) {
            return;
        }
        std::scoped_lock lock(_mutex);
        _queue.push_back(task);
        maybe_spawn_locked();
        _cv.notify_one();
    }

    bool thread_pool_t::try_start(runnable_t* task) {
        if (task == nullptr) {
            return false;
        }
        std::scoped_lock lock(_mutex);
        const bool idle_worker = _idle > 0;
        _queue.push_back(task);
        if (idle_worker) {
            _cv.notify_one();
            return true; // an idle worker picks it up right away
        }
        // No idle worker: queue it (a worker will still run it later) and report that it did not
        // start immediately, exactly like QThreadPool::tryStart().
        maybe_spawn_locked();
        _cv.notify_one();
        return false;
    }

    void thread_pool_t::clear() {
        std::deque<runnable_t*> dropped;
        {
            std::scoped_lock lock(_mutex);
            dropped.swap(_queue);
        }
        // Delete outside the lock: a task's destructor is arbitrary user code.
        for (runnable_t* task : dropped) {
            discard(task);
        }
    }

    int thread_pool_t::max_thread_count() const noexcept {
        std::scoped_lock lock(_mutex);
        return _max;
    }

    void thread_pool_t::set_max_thread_count(int count) {
        std::scoped_lock lock(_mutex);
        _max = (count > 0) ? count : 1;
        // Wake idle workers so that shrinking takes effect as they go idle.
        _cv.notify_all();
    }

    int thread_pool_t::active_thread_count() const noexcept {
        std::scoped_lock lock(_mutex);
        return static_cast<int>(_live);
    }

    void thread_pool_t::wait_for_done() {
        std::unique_lock lock(_mutex);
        for (;;) {
            if (_queue.empty() && _active == 0) {
                return;
            }
            // A drained queue with no worker left (all expired) would never finish: spawn one.
            if (!_queue.empty() && _live == 0) {
                maybe_spawn_locked();
            }
            _cv.notify_all();
            // Timed wait: a safety net against a lost wake-up, never the normal path.
            _cv.wait_for(lock, std::chrono::milliseconds(50), [this] {
                return _queue.empty() && _active == 0;
            });
        }
    }

    void thread_pool_t::set_expiry_timeout(int milliseconds) {
        std::scoped_lock lock(_mutex);
        _expiry_ms = (milliseconds >= 0) ? milliseconds : 0;
        // Wake the idle workers so their wait re-reads the new timeout.
        ++_expiry_generation;
        _cv.notify_all();
    }

    thread_pool_t& thread_pool_t::global_instance() {
        // Deliberately never destroyed (safe during static teardown); the process reclaims it.
        static thread_pool_t* instance = new thread_pool_t();
        return *instance;
    }

    void thread_pool_t::worker_loop(const std::shared_ptr<std::atomic<bool>>& done) {
        std::unique_lock lock(_mutex);
        for (;;) {
            if (_queue.empty()) {
                if (_shutdown || _live > static_cast<std::size_t>(_max)) {
                    break;
                }
                const unsigned int generation = _expiry_generation;
                ++_idle;
                const bool ready = _cv.wait_for(lock, std::chrono::milliseconds(_expiry_ms), [this, generation] {
                    return _shutdown || !_queue.empty()
                        || _live > static_cast<std::size_t>(_max)
                        || _expiry_generation != generation;
                });
                --_idle;
                if (!ready && _queue.empty()) {
                    break; // idle for the whole expiry window: retire
                }
                continue;
            }
            runnable_t* task = _queue.front();
            _queue.pop_front();
            ++_active;
            lock.unlock();
            run_task(task);
            lock.lock();
            --_active;
            // Wake wait_for_done(): the queue may now be empty and nothing may be running.
            _cv.notify_all();
        }
        --_live;
        done->store(true, std::memory_order_release);
        _cv.notify_all();
    }

    void thread_pool_t::maybe_spawn_locked() {
        reap_finished_locked();
        if (_queue.size() <= _idle || _live >= static_cast<std::size_t>(_max)) {
            return;
        }
        worker_slot_t slot;
        slot.done = std::make_shared<std::atomic<bool>>(false);
        const std::shared_ptr<std::atomic<bool>> done = slot.done;
        // The new worker blocks on _mutex until the spawner releases it, so it cannot observe the
        // half-initialised vectors.
        slot.thread = std::thread([this, done] {
            worker_loop(done);
        });
        _workers.push_back(std::move(slot));
        ++_live;
    }

    void thread_pool_t::reap_finished_locked() {
        for (auto it = _workers.begin(); it != _workers.end();) {
            if (it->done != nullptr && it->done->load(std::memory_order_acquire)) {
                if (it->thread.joinable()) {
                    it->thread.join();
                }
                it = _workers.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void thread_pool_t::run_task(runnable_t* task) {
        try {
            if (task != nullptr) {
                task->run();
            }
        }
        catch (const std::exception& error) {
            HWSHQTB_LOG_RAW(log_events(), log_level_enum::CRITICAL,
                std::string("thread pool task threw: ") + error.what());
        }
        catch (...) {
            HWSHQTB_LOG_RAW(log_events(), log_level_enum::CRITICAL, "thread pool task threw an unknown exception");
        }
        if (task != nullptr && task->auto_delete()) {
            delete task;
        }
    }

    void thread_pool_t::discard(runnable_t* task) noexcept {
        if (task != nullptr && task->auto_delete()) {
            delete task;
        }
    }

    bool runnable_t::auto_delete() const {
        return true;
    }
}

META_OBJECT(hwshqtb::metaobject::thread_pool_t);
