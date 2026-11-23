#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/meta_enum_t.h>
#include <hwshqtb/metaobject/meta_property_t.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/event_t.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/event_loop_t.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/object_pointer_t.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <hwshqtb/metaobject/thread_t.h>

namespace hwshqtb::metaobject {
    namespace detail {
        /// @brief Internal node of one connection: reference counted, shared by the sender's and
        ///        the receiver's connection tables.
        struct connection {
            /// @brief Connection type (Unique is a bit).
            connection_type_t type = connection_type_flag::AUTO;
            /// @brief Signal handle on the sender side.
            meta_method_t signal;
            /// @brief Sender.
            object_t* sender = nullptr;
            /// @brief Receiver; nullptr for a functor connection.
            object_t* receiver = nullptr;
            /// @brief Method handle on the receiver side; invalid for a functor connection.
            meta_method_t slot;
            /// @brief Set to false when either side is destroyed or the connection is cut.
            bool alive = true;
            /// @brief Optional context object: destroying it cuts the connection.
            object_t* context = nullptr;
            /// @brief When set, the connection is a functor call instead of a slot call.
            std::function<void(std::span<any_t>)> functor;
            /// @brief Functor connections only: the thread that CREATED the connection.
            /// @note A functor has no receiver object, so the creating thread takes that role: Auto
            ///       compares it with the emitting thread, and a Queued / BlockingQueued call runs on
            ///       its dispatcher. The callable and its captures were created on that thread.
            /// @note The thread_data_t always has its dispatcher created when it is stored here
            ///       (thread_data_t::current() creates it on the owning thread), so posting to it never
            ///       touches a foreign thread's TLS.
            detail::thread_data_t* subscriber_thread = nullptr;
        };

        /// @brief Internal node of one event-filter relation: both sides hold a reference, and
        ///        destroying either side marks it dead.
        struct event_filter_node {
            /// @brief The observed object.
            object_t* target = nullptr;
            /// @brief The filter object.
            object_t* filter = nullptr;
            /// @brief Set to false when either side is destroyed or the filter is removed.
            bool alive = true;
        };

        /// @brief Copy a signal's arguments for a queued connection.
        /// @param signal Signal handle (provides the parameter count and types).
        /// @param receiver Target object (kept for the handle's API; type resolution is process-wide).
        /// @param params generic_call convention: params[0] is the return-value slot, arguments
        ///        start at params[1].
        /// @return On success the argument vector; nullopt when a parameter is not copyable or its
        ///         type cannot be resolved (undeclared and unknown to the registry).
        /// @note dispatch_signal's failure channel: a connection whose arguments cannot be copied is
        ///       SKIPPED (no throw, no log) — the receiver simply does not see this emission. The
        ///       copy rule itself is detail::copy_args(), shared with the signal_spy_t record and the
        ///       queued invoke() path; this adapter only turns its failing index into the nullopt
        ///       channel the two dispatch_signal call sites use.
        inline std::optional<std::vector<any_t>> copy_signal_args(const meta_method_t& signal,
            const object_t& receiver,
            void** params) {
            const std::size_t count = signal.param_count();
            std::vector<any_t> args;
            if (detail::copy_args(signal, receiver, params, count, args) != count) {
                return std::nullopt;
            }
            return args;
        }
    }

    // ---- object_t: lazy state block ----

    namespace detail {
        namespace {
            /// @brief Live detail::object_data_t blocks (diagnostic).
            /// @note Relaxed atomics: the counters are a test-facing diagnostic, not synchronisation.
            ///       They are process-wide in the shared build because only the library touches them
            ///       (an inline function-local counter would be duplicated into the executable and
            ///       the DLL).
            std::atomic<std::size_t> g_object_data_t_live{ 0 };
            /// @brief Cumulative detail::object_data_t allocations (diagnostic).
            std::atomic<std::size_t> g_object_data_t_total{ 0 };
        }

        HWSHQTB_EXPORT std::size_t object_data_live_count() noexcept {
            return g_object_data_t_live.load(std::memory_order_relaxed);
        }

        HWSHQTB_EXPORT std::size_t object_data_total_allocations() noexcept {
            return g_object_data_t_total.load(std::memory_order_relaxed);
        }
    }

    object_t::object_t():
        _thread(&detail::thread_data_t::current()) { }

    detail::object_data_t* object_t::ensure_data() const {
        if (_data == nullptr) {
            _data = new detail::object_data_t();
            detail::g_object_data_t_live.fetch_add(1, std::memory_order_relaxed);
            detail::g_object_data_t_total.fetch_add(1, std::memory_order_relaxed);
        }
        return _data;
    }

    const std::vector<object_t*>& object_t::child_list() const noexcept {
        // Function-local static: immutable, thread-safely initialised, and the one answer for every
        // bare object (children() then hands out an empty span over it).
        static const std::vector<object_t*> empty_children;
        return (_data != nullptr) ? _data->children : empty_children;
    }

    const std::string& object_t::name_ref() const noexcept {
        // Same technique as child_list(): one shared immutable empty string for every bare object.
        static const std::string empty_name;
        return (_data != nullptr) ? _data->name : empty_name;
    }

    thread_t* object_t::thread() const noexcept {
        // The identity lives in the per-thread data (QObject::thread reads d->threadData->thread the
        // same way); every thread that has data adopted one in thread_data_t::current().
        return _thread->owner;
    }

    detail::thread_data_t* object_t::thread_data() const noexcept {
        return _thread;
    }

    int object_t::start_timer(int milliseconds, bool repeating) {
        return event_dispatcher::instance().register_timer(this, milliseconds, repeating);
    }

    void object_t::kill_timer(int timer_id) {
        event_dispatcher::instance().unregister_timer(timer_id);
    }

    void object_t::timer_event(timer_event_t* e) {
        // The default implementation ignores the tick (like QObject::timerEvent).
        if (e != nullptr) {
            e->ignore();
        }
    }

    std::vector<std::string> object_t::dynamic_property_names() const {
        std::vector<std::string> names;
        if (_data == nullptr) {
            return names; // no state block: no dynamic property was ever written
        }
        names.reserve(_data->dynamic_properties.size());
        for (const auto& kv : _data->dynamic_properties) {
            names.push_back(kv.first);
        }
        return names;
    }

    void object_t::dump_object_info(std::ostream& os) const {
        const meta_object_t& info = *meta_object();
        os << info.class_name() << "(\"" << object_name() << "\")"
           << "  [meta: " << info.class_name() << "]"
           << "  conn=" << ((_data != nullptr) ? _data->connections.size() : 0)
           << "  props=" << info.property_count() << "\n";
    }

    void object_t::dump_object_tree(std::ostream& os) const {
        // Depth-first, two spaces per level; the root is at depth 0 (no connector, like Qt).
        const auto dump = [&](auto&& self, const object_t* node, int depth) -> void {
            for (int i = 0; i < depth; ++i) {
                os << "  ";
            }
            if (depth > 0) {
                os << "\u2514\u2500 ";
            }
            const meta_object_t& info = *node->meta_object();
            os << info.class_name() << "(\"" << node->object_name() << "\")"
               << "  [meta: " << info.class_name() << "]"
               << "  conn=" << ((node->_data != nullptr) ? node->_data->connections.size() : 0)
               << "  props=" << info.property_count() << "\n";
            for (const object_t* child : node->child_list()) {
                if (child != nullptr) {
                    self(self, child, depth + 1);
                }
            }
        };
        dump(dump, this, 0);
    }

    void object_t::move_to_thread(thread_t* target) {
        // The thread object only supplies its thread data; the affinity is that data pointer, so both
        // overloads share one implementation.
        move_to_thread((target != nullptr) ? target->_data : nullptr);
    }

    void object_t::move_to_thread(detail::thread_data_t* target_data) {
        if (target_data == nullptr || target_data == _thread) {
            return;
        }
        // Qt forbids moving an object that still has a parent: its affinity is inherited from the
        // parent, so the caller has to detach it first.
        if (parent() != nullptr) {
            return;
        }
        auto migrate = [&](auto&& self, object_t* node) -> void {
            node->_thread = target_data;
            for (object_t* child : node->child_list()) {
                self(self, child);
            }
        };
        migrate(migrate, this);
        target_data->get_dispatcher().wake_up();
    }

    object_t::~object_t() {
        // The state block is deleted at the very end of this body: every list below lives in it, and
        // a child's destructor reaches back into the parent's block to unlink itself. A bare object
        // (no block) skips all of it; the dispatcher cleanup is unconditional because a bare object
        // can still own timers (start_timer() needs no state block).
        detail::object_data_t* const data = _data;

        // Tell every object_pointer_t that this object is gone, before anything else is torn down.
        if (data != nullptr && data->watch) {
            data->watch->dead = true;
        }

        // ---- destroyed() (QObject::~QObject order) ----
        //
        // Qt's destructor order is: mark deleted, clear the signal-block counter, notify the guarded
        // pointers, emit destroyed(), then delete the children, cut the connections and detach from
        // the parent. The same order here means the slot still sees a live object with a live
        // connection set, and an event it posts to this object (delete_later() included) is purged
        // right below instead of being dispatched to a dead object.
        //
        // The emission goes through dispatch_signal() directly, never through activate_signal()'s
        // block check, so destroyed() is non-blockable by construction: neither a caller's
        // block_signals(true) nor a block installed by an earlier slot can suppress it.
        _signal_block_count = 0;
        if (data != nullptr) {
            // Resolved from object_t's own table through the same compile-time key the SIGNAL
            // emitter uses; the cached handle points into the constant-initialised static table, so
            // it is valid even when a static object is destroyed during static destruction.
            static constexpr std::string_view destroyed_key =
                meta_info_t::signature_of(^^object_t::destroyed);
            static const meta_method_t destroyed_signal = static_meta_object.signal(destroyed_key);
            if (destroyed_signal.is_valid()) {
                // Qt's isSignalConnected(0): with nobody listening there is nothing to do — and no
                // copy of the table below.
                bool wanted = false;
                for (const auto& node : data->connections) {
                    if (node && node->alive && node->signal == destroyed_signal) {
                        wanted = true;
                        break;
                    }
                }
                if (wanted) {
                    object_t* self = this;
                    void* argv[2] = { nullptr, &self };
                    // Snapshot: a destroyed() slot may destroy other objects (their destructors cut
                    // their connections, which erases entries of this very table), or connect /
                    // disconnect. The copy keeps every node alive and the loop valid; a node cut
                    // during the emission is skipped through its alive flag, and a connection added
                    // during the emission does not take part in it (Qt's activate() does not promise
                    // that either).
                    const std::vector<std::shared_ptr<detail::connection>> snapshot = data->connections;
                    // destroyed(object_t*) carries ONE argument, so the erased view needs ONE handle:
                    // an empty span would make a typed sink index past its end. The alias is built the
                    // same way the SIGNAL macro builds them — capture() over std::ref — so this path
                    // needs no factory the public API does not already have.
                    any_t destroyed_args[1]{
                        any_t::capture(meta_type_t::from_type<object_t*>(), ::std::ref(self)) };
                    dispatch_signal(destroyed_signal, argv, destroyed_args, snapshot);
                }
            }
        }

        // Event loop: drop every pending event posted to this object and every timer it owns. The
        // dispatcher is per thread and never destroyed, so this is safe during static destruction as
        // well. Unconditional: a bare object (no state block) can still own timers.
        event_dispatcher::instance().remove_object(this);
        event_dispatcher::instance().remove_timers_of(this);

        if (data != nullptr) {
            // Event filters: unlink both directions (this object as target and as filter).
            for (auto& node : data->event_filters) {
                if (!node) continue;
                node->alive = false;
                if (node->filter != nullptr) {
                    std::erase_if(node->filter->ensure_data()->installed_filters, [&](const auto& p) {
                        return p == node;
                    });
                    node->filter = nullptr;
                }
            }
            data->event_filters.clear();
            for (auto& node : data->installed_filters) {
                if (!node) continue;
                node->alive = false;
                if (node->target != nullptr) {
                    std::erase_if(node->target->ensure_data()->event_filters, [&](const auto& p) {
                        return p == node;
                    });
                    node->target = nullptr;
                }
            }
            data->installed_filters.clear();

            // Remove ourselves from the parent's child list: otherwise a direct delete (as done by
            // delete_later) would make the parent delete this object a second time.
            if (data->parent != nullptr) {
                object_t* const old_parent = data->parent;
                std::erase_if(old_parent->ensure_data()->children, [&](object_t* c) {
                    return c == this;
                });
                data->parent = nullptr;
                // Notify only AFTER the list no longer contains this: a widget parent re-lays out
                // its remaining children here, and must not touch the half-destroyed object.
                old_parent->child_removed(this);
            }

            // As sender: cut every outgoing connection.
            for (auto& node : data->connections) {
                if (!node) continue;
                node->alive = false;
                if (node->receiver != nullptr) {
                    std::erase_if(node->receiver->ensure_data()->incoming, [&](const auto& p) {
                        return p == node;
                    });
                    node->receiver = nullptr;
                }
            }
            data->connections.clear();

            // As receiver: cut every incoming connection.
            for (auto& node : data->incoming) {
                if (!node) continue;
                node->alive = false;
                if (node->sender != nullptr) {
                    std::erase_if(node->sender->ensure_data()->connections, [&](const auto& p) {
                        return p == node;
                    });
                    node->sender = nullptr;
                }
            }
            data->incoming.clear();

            // As a context, cut every connection that named this object as its context.
            for (auto& node : data->context_connections) {
                if (!node) continue;
                node->alive = false;
                node->context = nullptr;
                if (node->sender != nullptr) {
                    std::erase_if(node->sender->ensure_data()->connections, [&](const auto& p) {
                        return p == node;
                    });
                    node->sender = nullptr;
                }
                if (node->receiver != nullptr) {
                    std::erase_if(node->receiver->ensure_data()->incoming, [&](const auto& p) {
                        return p == node;
                    });
                    node->receiver = nullptr;
                }
            }
            data->context_connections.clear();

            // The parent destroys its children, popping from the LIVE list: a child's destructor
            // removes itself from data->children, so a sibling a destroyed() slot already deleted is
            // simply no longer in the list and the same raw pointer is never deleted twice (a copy of
            // the list would keep a dangling entry).
            while (!data->children.empty()) {
                object_t* child = data->children.front();
                data->children.erase(data->children.begin());
                if (child != nullptr) {
                    delete child;
                }
            }
        }

        // Release the state block last: the lists above (and the death watch) live in it.
        if (data != nullptr) {
            delete data;
            _data = nullptr;
            detail::g_object_data_t_live.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    object_t* object_t::parent() const noexcept {
        return (_data != nullptr) ? _data->parent : nullptr;
    }

    void object_t::child_added(object_t* child) {
        (void)child;
    }

    void object_t::child_removed(object_t* child) {
        (void)child;
    }

    void object_t::set_parent(object_t* parent) {
        // Reject cycles: parent must not be this object or one of its descendants.
        for (object_t* p = parent; p != nullptr; p = p->parent()) {
            if (p == this) return;
        }
        object_t* const old_parent = (_data != nullptr) ? _data->parent : nullptr;
        if (old_parent != nullptr) {
            std::erase_if(old_parent->ensure_data()->children, [&](object_t* c) {
                return c == this;
            });
            old_parent->child_removed(this);
        }
        if (parent == nullptr) {
            // Detaching a bare object is a no-op: it has neither a parent nor a child list, so there
            // is nothing to update and nothing to allocate.
            if (_data != nullptr) {
                _data->parent = nullptr;
            }
            return;
        }
        ensure_data()->parent = parent;
        parent->ensure_data()->children.push_back(this);
        parent->child_added(this);
    }

    std::span<object_t* const> object_t::children() const noexcept {
        const std::vector<object_t*>& child_list_ref = child_list();
        return std::span<object_t* const>(child_list_ref.data(), child_list_ref.size());
    }

    object_t* object_t::root() const noexcept {
        // Walk the parent chain; set_parent() rejects cycles, so the walk terminates.
        const object_t* node = this;
        while (node->parent() != nullptr) {
            node = node->parent();
        }
        return const_cast<object_t*>(node);
    }

    void object_t::set_object_name(const std::string& name) {
        if (_data == nullptr) {
            // Still the empty name, so "" is a no-op: no allocation and no signal.
            if (name.empty()) {
                return;
            }
            ensure_data()->name = name;
            object_name_changed(_data->name); // emit: the signal body dispatches through activate_signal
            return;
        }
        if (_data->name != name) {
            _data->name = name;
            object_name_changed(_data->name); // emit: the signal body dispatches through activate_signal
        }
    }

    std::string object_t::object_name() const {
        return name_ref();
    }

    bool object_t::inherits(std::string_view class_name) const noexcept {
        return meta_object()->inherits(class_name);
    }

    void object_t::delete_later() {
        if (_data != nullptr && _data->delete_later_posted) {
            return; // idempotent: already posted
        }
        ensure_data()->delete_later_posted = true;
        // Record the loop nesting depth of the posting thread; the event also captures that thread's
        // identity, so the dispatcher applies the depth rule only when it dispatches on the same
        // thread (a cross-thread deferred delete never compares two threads' depths).
        core_application::post_event(this, new deferred_delete_event_t(event_loop_t::loop_level(), 0));
    }

    // ---- event handling (QEvent family) ----

    bool object_t::event(event_t* event) {
        if (event == nullptr) {
            return false;
        }
        switch (event->type()) {
            case event_type_enum::META_CALL:
                // Queued signal invocation: execute it through the slot handle carried by the event.
                return static_cast<meta_call_event_t*>(event)->place_meta_call(*this);
            case event_type_enum::DEFERRED_DELETE:
                // The actual delete is performed by the dispatcher (see
                // event_dispatcher::dispatch_one); accepting the event is enough here.
                event->accept();
                return true;
            case event_type_enum::TIMER:
                // A timer tick: hand it to the virtual hook (QObject::timerEvent).
                timer_event(static_cast<timer_event_t*>(event));
                event->accept();
                return true;
            case event_type_enum::DYNAMIC_PROPERTY_CHANGE:
                // Nothing to do by default: accepting it is enough (Qt behaves the same).
                event->accept();
                return true;
            default:
                break;
        }
        if (static_cast<std::uint16_t>(event->type()) >= static_cast<std::uint16_t>(event_type_enum::USER)) {
            custom_event(event);
            return true;
        }
        event->ignore();
        return false;
    }

    void object_t::custom_event(event_t* event) {
        event->ignore(); // ignored by default: derive and override to consume custom events
    }

    bool object_t::event_filter(object_t* watched, event_t* event) {
        (void)watched;
        (void)event;
        return false; // does not intercept by default
    }

    void object_t::install_event_filter(object_t* filter) {
        if (filter == nullptr || filter == this) {
            return;
        }
        if (_data != nullptr) {
            for (const auto& node : _data->event_filters) {
                if (node && node->alive && node->filter == filter) {
                    return; // already installed: idempotent
                }
            }
        }
        auto node = std::make_shared<detail::event_filter_node>();
        node->target = this;
        node->filter = filter;
        node->alive = true;
        ensure_data()->event_filters.push_back(node);
        filter->ensure_data()->installed_filters.push_back(node);
    }

    void object_t::remove_event_filter(object_t* filter) {
        if (filter == nullptr || _data == nullptr) {
            return;
        }
        std::erase_if(_data->event_filters, [&](const std::shared_ptr<detail::event_filter_node>& node) {
            if (!node || node->filter != filter) {
                return false;
            }
            node->alive = false;
            std::erase_if(filter->ensure_data()->installed_filters, [&](const auto& p) {
                return p == node;
            });
            node->filter = nullptr;
            return true;
        });
    }

    bool object_t::apply_event_filters(event_t* event) {
        if (_data == nullptr) {
            return false; // no filters installed: nothing can consume the event
        }
        for (const auto& node : _data->event_filters) {
            if (!node || !node->alive || node->filter == nullptr) {
                continue;
            }
            if (node->filter->event_filter(this, event)) {
                return true;
            }
        }
        return false;
    }

    namespace {
        /// @brief One entry of the thread-local dispatch stack (backs object_t::sender()).
        struct invoke_frame {
            object_t* sender = nullptr;
        };

        /// @brief The calls currently being dispatched on this thread.
        /// @note A never-freed thread_local POINTER, not a thread_local std::vector: a static
        ///       object_t is destroyed during static destruction, when this thread's thread_local
        ///       objects may already be gone, and its destroyed() slot must still be able to dispatch
        ///       and to read sender().
        thread_local std::vector<invoke_frame>* g_invoke_stack = nullptr;

        /// @brief The dispatch stack of this thread, created on first use.
        std::vector<invoke_frame>& invoke_stack() {
            if (g_invoke_stack == nullptr) {
                g_invoke_stack = new std::vector<invoke_frame>();
            }
            return *g_invoke_stack;
        }
    }

    namespace detail {
        void push_invoke_frame(object_t* sender) {
            invoke_stack().push_back(invoke_frame{ sender });
        }

        void pop_invoke_frame() {
            if (g_invoke_stack != nullptr && !g_invoke_stack->empty()) {
                g_invoke_stack->pop_back();
            }
        }
    }

    object_t* object_t::sender() const noexcept {
        return (g_invoke_stack == nullptr || g_invoke_stack->empty())
            ? nullptr
            : g_invoke_stack->back().sender;
    }



    bool object_t::block_signals(bool block) noexcept {
        const bool was_blocked = _signal_block_count > 0;
        if (block) {
            ++_signal_block_count;
        }
        else if (_signal_block_count > 0) {
            --_signal_block_count;
        }
        return was_blocked;
    }

    bool object_t::signals_blocked() const noexcept {
        return _signal_block_count > 0;
    }

    void object_t::activate_signal(const meta_method_t& signal, void** params,
        std::span<any_t> args) const {
        if (_signal_block_count > 0) {
            return; // blocked — no connection is invoked at all
        }
        if (_data == nullptr) {
            return; // no state block: there is no connection table, so nothing to notify
        }
        dispatch_signal(signal, params, args, _data->connections);
    }

    void object_t::dispatch_signal(const meta_method_t& signal, void** params, std::span<any_t> args,
        const std::vector<std::shared_ptr<detail::connection>>& connections) const {
        for (const auto& node : connections) {
            if (!node || !node->alive) {
                continue;
            }
            if (!(node->signal == signal)) {
                continue;
            }
            if (node->functor) {
                // A functor connection has no receiver object, but it does have a subscriber
                // thread: the one that created the connection. The same four-way rule as a slot
                // connection therefore applies, with subscriber_thread in the receiver role.
                const connection_type_t type = node->type;
                bool queued = bool(type & connection_type_flag::QUEUED)
                    || bool(type & connection_type_flag::BLOCKING_QUEUED);
                if (!queued && bool(type & connection_type_flag::AUTO)) {
                    // Qt::AutoConnection, resolved at EMIT time: Direct when the subscriber and the
                    // emitting object share a thread, Queued otherwise.
                    queued = node->subscriber_thread != nullptr && node->subscriber_thread != thread_data();
                }
                if (!queued) {
                    // Direct (explicit, or Auto on one thread): call inline. Pushing the frame keeps
                    // object_t::sender() working inside the lambda.
                    detail::push_invoke_frame(const_cast<object_t*>(this));
                    node->functor(args);
                    detail::pop_invoke_frame();
                    continue;
                }
                // Queued / BlockingQueued: copy the arguments by parameter type and post the call to
                // the subscriber thread's dispatcher. There is no receiver object to route
                // core_application::post_event() by, so the event is addressed by thread.
                if (node->subscriber_thread == nullptr) {
                    continue; // defensive: connect() always records it
                }
                auto args = detail::copy_signal_args(signal, *this, params);
                if (!args) {
                    continue; // a parameter is not copyable (or its type cannot be resolved): skip, no throw
                }
                if (type & connection_type_flag::BLOCKING_QUEUED) {
                    if (node->subscriber_thread == thread_data()) {
                        // Same thread: the poster would wait for an event loop that cannot run while
                        // we are blocking it. Skip the call (Qt prints a warning and keeps going).
                        std::fprintf(stderr,
                            "[metaobject] BlockingQueued functor call on the connection's own thread "
                            "is skipped to avoid a deadlock\n");
                        continue;
                    }
                    auto sync = std::make_shared<detail::event_sync_t>();
                    detail::post_event_to(*node->subscriber_thread,
                        new meta_call_event_t(this, signal, node->functor, std::move(*args), sync));
                    // Across threads the dispatching side wakes us up through notify_done().
                    sync->wait();
                }
                else {
                    detail::post_event_to(*node->subscriber_thread,
                        new meta_call_event_t(this, signal, node->functor, std::move(*args)));
                }
                continue;
            }
            if (node->receiver == nullptr) {
                continue;
            }
            const connection_type_t type = node->type;
            bool queued = bool(type & connection_type_flag::QUEUED)
                || bool(type & connection_type_flag::BLOCKING_QUEUED);
            if (!queued && bool(type & connection_type_flag::AUTO)) {
                // Qt::AutoConnection: Direct when sender and receiver share a thread, Queued
                // otherwise. An explicit Direct stays direct even across threads.
                queued = node->receiver->thread() != thread();
            }
            if (!queued) {
                // Direct (Auto resolves to Direct when both objects share a thread). Push a frame so
                // that object_t::sender() works inside the slot.
                detail::push_invoke_frame(const_cast<object_t*>(this));
                // A connection node carries the receiver's address, not its type: the object's cvref
                // is unknown at this layer, so it is passed as qualifier_flag::NONE. Signal dispatch
                // never matches arguments (the slot was fixed when the connection was made).
                node->slot.invoke_caller(node->receiver, params, qualifier_flag::NONE);
                detail::pop_invoke_frame();
                continue;
            }
            // Queued / BlockingQueued: copy the arguments by parameter type into an event and let
            // the event loop dispatch it.
            auto args = detail::copy_signal_args(signal, *node->receiver, params);
            if (!args) {
                continue; // a parameter is not copyable (or its type cannot be resolved): skip, no throw
            }
            if (type & connection_type_flag::BLOCKING_QUEUED) {
                if (node->receiver->thread() == thread()) {
                    // Same thread: the poster would wait for an event loop that cannot run while we
                    // are blocking it. Qt prints a warning and keeps going; do the same (skip the
                    // call) instead of deadlocking.
                    std::fprintf(stderr,
                        "[metaobject] BlockingQueued connection to the same thread is "
                        "skipped to avoid a deadlock\n");
                    continue;
                }
                auto sync = std::make_shared<detail::event_sync_t>();
                core_application::post_event(
                    node->receiver,
                    new meta_call_event_t(this, signal, node->slot, std::move(*args), sync)
                );
                // Across threads the dispatching side wakes us up through notify_done().
                sync->wait();
            }
            else {
                core_application::post_event(
                    node->receiver,
                    new meta_call_event_t(this, signal, node->slot, std::move(*args))
                );
            }
        }
    }

    bool object_t::disconnect(const connection_handle_t& handle) {
        if (!handle._node || !handle._node->alive) {
            return false;
        }
        auto& node = handle._node;
        node->alive = false;
        if (node->sender != nullptr) {
            std::erase_if(node->sender->ensure_data()->connections, [&](const auto& p) {
                return p == node;
            });
        }
        if (node->receiver != nullptr) {
            std::erase_if(node->receiver->ensure_data()->incoming, [&](const auto& p) {
                return p == node;
            });
        }
        node->sender = nullptr;
        node->receiver = nullptr;
        return true;
    }

    std::size_t object_t::disconnect(object_t* receiver) {
        std::size_t count = 0;
        if (_data == nullptr) {
            return count; // no sender table: nothing can match
        }
        for (auto& node : _data->connections) {
            if (!node || !node->alive || node->receiver != receiver) {
                continue;
            }
            node->alive = false;
            if (receiver != nullptr) {
                std::erase_if(receiver->ensure_data()->incoming, [&](const auto& p) {
                    return p == node;
                });
            }
            ++count;
        }
        std::erase_if(_data->connections, [](const auto& p) {
            return !p || !p->alive;
        });
        return count;
    }

    // ---- object_t: meta-object convenience entry points and connections ----

    any_t object_t::property(std::string_view name) const {
        // QObject::property semantics: a declared property is read through its reader, otherwise the
        // per-object dynamic table answers. No handle is involved on this path.
        const meta_property_t declared = meta_object()->property(name);
        if (declared.is_valid()) {
            return declared.read(*this);
        }
        if (_data != nullptr) {
            const auto it = _data->dynamic_properties.find(std::string(name));
            if (it != _data->dynamic_properties.end()) {
                return it->second;
            }
        }
        return any_t{ };
    }



    bool object_t::set_property(std::string_view name, const any_t& value) {
        meta_property_t p = meta_object()->property(name);
        if (p.is_valid()) {
            return p.write(*this, value);
        }
        // Not a declared property: store it as a dynamic property (Qt semantics). A typo therefore
        // creates a dynamic property instead of failing; dynamic_property_names() makes those visible.
        ensure_data()->dynamic_properties[std::string(name)] = value;
        // Notify listeners. Qt posts this on every dynamic write, even when the value is unchanged.
        core_application::post_event(this, new dynamic_property_change_event_t(std::string(name)));
        return true;
    }

    connection_handle_t object_t::connect(std::string_view signal_signature,
        object_t* receiver,
        std::string_view slot_signature,
        connection_type_t type) {
        if (receiver == nullptr) {
            return {};
        }
        const meta_method_t sig = meta_object()->signal(signal_signature);
        if (!sig.is_valid()) {
            return {};
        }
        const meta_method_t slot = receiver->meta_object()->method(slot_signature);
        if (!slot.is_valid()) {
            return {};
        }
        return connect(sig, receiver, slot, type);
    }

    connection_handle_t object_t::connect(const meta_method_t& signal,
        object_t* receiver,
        const meta_method_t& slot,
        connection_type_t type) {
        if (receiver == nullptr || !signal.is_valid() || !slot.is_valid() || !signal.is_signal()) {
            return {};
        }
        // A slot may drop trailing signal arguments (the extra ones are simply not passed), but it may
        // not ask for MORE than the signal carries: the dispatcher hands the slot the signal's argument
        // array, so an over-long slot parameter list would read past it. QMetaObject::checkConnectArgs
        // is the same rule.
        if (!meta_object_t::check_connect_args(signal, slot)) {
            HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL,
                "object_t::connect: slot '" + std::string(slot.signature())
                    + "' cannot accept signal '" + std::string(signal.signature())
                    + "'; a slot may drop trailing signal arguments, never add its own");
            return {};
        }
        if (type & connection_type_flag::UNIQUE) {
            if (_data != nullptr) {
                for (const auto& node : _data->connections) {
                    if (node && node->alive && node->receiver == receiver
                        && node->signal == signal && node->slot == slot) {
                        connection_handle_t handle;
                        handle._node = node;
                        return handle;
                    }
                }
            }
        }
        auto node = std::make_shared<detail::connection>();
        node->type = type;
        node->signal = signal;
        node->sender = this;
        node->receiver = receiver;
        node->slot = slot;
        node->alive = true;
        ensure_data()->connections.push_back(node);
        receiver->ensure_data()->incoming.push_back(node);
        connection_handle_t handle;
        handle._node = node;
        return handle;
    }

    connection_handle_t object_t::connect(std::string_view signal_signature,
        object_t* context,
        object_t* receiver,
        std::string_view slot_signature,
        connection_type_t type) {
        if (receiver == nullptr) {
            return {};
        }
        const meta_method_t sig = meta_object()->signal(signal_signature);
        if (!sig.is_valid()) {
            return {};
        }
        const meta_method_t slot = receiver->meta_object()->method(slot_signature);
        if (!slot.is_valid()) {
            return {};
        }
        connection_handle_t handle = connect(sig, receiver, slot, type);
        if (handle.is_valid() && context != nullptr && handle._node) {
            handle._node->context = context;
            context->ensure_data()->context_connections.push_back(handle._node);
        }
        return handle;
    }

    connection_handle_t object_t::variadic_connect(std::string_view signal_signature,
        std::function<void(std::span<any_t>)> fn, connection_type_t type) {
        // The erased form takes any signature, so the arity and the type names are not checked: the
        // callable is expected to inspect the handles it is given.
        return connect_functor(signal_signature, nullptr, { }, std::move(fn), type);
    }

    connection_handle_t object_t::connect_functor(std::string_view signal_signature, object_t* context,
        std::span<const std::string_view> parameter_types,
        std::function<void(std::span<any_t>)> sink, connection_type_t type) {
        if (!sink) {
            return { };
        }
        const meta_method_t sig = meta_object()->signal(signal_signature);
        if (!sig.is_valid()) {
            return { };
        }
        // FEWER parameters than the signal is legal (Qt's rule), so this is a prefix match: the
        // callable is matched against the signal's first parameter_types.size() entries. Order and
        // types must match exactly — a mismatch is refused, never coerced.
        if (parameter_types.size() > sig.param_count()) {
            return { };
        }
        for (std::size_t index = 0; index < parameter_types.size(); ++index) {
            if (sig.param_type_name(index) != parameter_types[index]) {
                return { };
            }
        }
        auto node = std::make_shared<detail::connection>();
        node->type = type;
        node->signal = sig;
        node->sender = this;
        node->functor = std::move(sink);
        node->context = context;
        // The creating thread is this functor connection's "receiver thread". Auto compares it
        // with the emitting thread at emit time, and Queued / BlockingQueued calls are posted to its
        // dispatcher. current() creates this thread's dispatcher, which is what makes the stored
        // pointer postable from another thread (and before this thread's loop starts).
        node->subscriber_thread = &detail::thread_data_t::current();
        node->alive = true;
        ensure_data()->connections.push_back(node);
        if (context != nullptr) {
            // A functor has no receiver object, so the context is what ends its life: destroying the
            // context cuts the connection (Qt's connect(sender, signal, context, functor)).
            context->ensure_data()->context_connections.push_back(node);
        }
        connection_handle_t handle;
        handle._node = node;
        return handle;
    }

    bool object_t::disconnect(std::string_view signal_signature,
        object_t* receiver,
        std::string_view slot_signature) {
        if (receiver == nullptr || _data == nullptr) {
            return false;
        }
        const meta_method_t sig = meta_object()->signal(signal_signature);
        if (!sig.is_valid()) {
            return false;
        }
        const meta_method_t slot = receiver->meta_object()->method(slot_signature);
        if (!slot.is_valid()) {
            return false;
        }
        for (auto& node : _data->connections) {
            if (node && node->alive && node->receiver == receiver
                && node->signal == sig && node->slot == slot) {
                node->alive = false;
                std::erase_if(receiver->ensure_data()->incoming, [&](const auto& p) {
                    return p == node;
                });
                std::erase_if(_data->connections, [&](const auto& p) {
                    return p == node;
                });
                return true;
            }
        }
        return false;
    }

    // ---- connection_handle_t ----

    bool connection_handle_t::is_valid() const noexcept {
        return _node != nullptr && _node->alive;
    }

    bool connection_handle_t::disconnect() {
        if (!_node || !_node->alive || _node->sender == nullptr) {
            return false;
        }
        return _node->sender->disconnect(*this);
    }

    // ---- interface casting ----
    //
    // There is no object_t::cast_to any more: the interface lookup is the type-erased
    // meta_object_t::cast(void*, iid), reached through the namespace-level cast<T>(U*) (which also
    // owns the OBJECT / GADGET class casts). Keeping it off object_t is what lets a GADGET, which is
    // not an object_t, use the same entry point.

    const std::shared_ptr<detail::death_watch_t>& object_t::watch() const noexcept {
        // First use creates the state block, and the token inside it if it is still missing — which
        // is exactly what object_pointer_t triggers. The returned reference points into the block and
        // stays valid (and non-null) for the object's lifetime.
        detail::object_data_t* const data = ensure_data();
        if (!data->watch) {
            data->watch = std::make_shared<detail::death_watch_t>();
        }
        return data->watch;
    }

}

META_OBJECT(hwshqtb::metaobject::object_t);
