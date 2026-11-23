#ifndef HWSHQTB__METAOBJECT__OBJECT_H
#define HWSHQTB__METAOBJECT__OBJECT_H

#include "basic_def.h"
#include "meta_type_t.h"
#include "meta_info_t.h"
#include "any_t.h"
#include "meta_method_t.h"
#include "meta_object_t.h"
#include <array>
#include <tuple>
#include <type_traits>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "macros.h"
#include "thread_data_t.h"
#include "annotation.h"
#include "export.h"

namespace hwshqtb::metaobject {
    class meta_property_t;
    class meta_enum_t;
    class any_t;
    class event_t;
    class object_t;

    namespace detail {
        /// @brief Internal node of one connection (reference counted, held by connection_handle_t).
        /// @note Defined in the implementation file; only forward-declared here so that the
        ///       connection handle and the connection tables can hold a shared_ptr.
        struct connection;

        /// @brief Internal node of one event-filter relation (reference counted, shared by both
        ///        sides).
        /// @note Defined in the implementation file; only forward-declared here so that an
        ///       object's filter tables can hold a shared_ptr.
        struct event_filter_node;

        /// @brief Liveness token shared with object_pointer_t instances.
        ///
        /// Each object_t owns one; ~object_t sets @c dead. An object_pointer_t holds a weak_ptr to it
        /// plus the raw pointer, so it can tell "destroyed" from "still alive" without dereferencing.
        struct death_watch_t {
            /// @brief Set by the owning object's destructor.
            std::atomic<bool> dead{ false };
        };

        /// @brief The lazily allocated per-object state block.
        /// @note Exactly one block per object, created by object_t::ensure_data() on first use and
        ///       deleted by ~object_t(); the pointer is never shared. An object that never needs state
        ///       keeps the block null and performs no heap allocation.
        struct object_data_t {
            /// @brief Parent object; nullptr means root.
            object_t* parent = nullptr;
            /// @brief Child list (deleted when the parent is destroyed).
            std::vector<object_t*> children;
            /// @brief Connection table of this object as a sender.
            std::vector<std::shared_ptr<connection>> connections;
            /// @brief Incoming connections of this object as a receiver (used at destruction time to
            ///        unlink the matching entries from each sender's table).
            std::vector<std::shared_ptr<connection>> incoming;
            /// @brief Connections that named this object as their context: destroying the
            ///        context cuts them, independently of sender and receiver lifetime.
            std::vector<std::shared_ptr<connection>> context_connections;
            /// @brief Event filters installed on this object (installation order).
            std::vector<std::shared_ptr<event_filter_node>> event_filters;
            /// @brief Records of this object being installed as a filter elsewhere (used at
            ///        destruction time to clean up).
            std::vector<std::shared_ptr<event_filter_node>> installed_filters;
            /// @brief Liveness token handed out to object_pointer_t instances.
            std::shared_ptr<death_watch_t> watch;
            /// @brief The object name (QObject::objectName).
            std::string name;
            /// @brief Dynamic (run-time created) properties; not part of the meta-info table.
            std::map<std::string, any_t> dynamic_properties;
            /// @brief Whether the deferred-delete event has been posted already (delete_later is
            ///        idempotent).
            bool delete_later_posted = false;
        };

        /// @brief Number of detail::object_data_t blocks currently alive in this process.
        /// @return The live-block count (0 when every object is bare or destroyed).
        /// @note Diagnostic, read by tests only; the object_t bookkeeping updates it on every
        ///        allocation / release. Exported because the shared library owns the counter.
        HWSHQTB_EXPORT std::size_t object_data_live_count() noexcept;

        /// @brief Number of detail::object_data_t blocks ever allocated in this process.
        /// @return The cumulative allocation count (monotonic).
        /// @note Diagnostic, read by tests only; the object_t bookkeeping updates it on every
        ///        allocation. Exported because the shared library owns the counter.
        HWSHQTB_EXPORT std::size_t object_data_total_allocations() noexcept;
    }

    /// @brief Connection handle (modelled on QMetaObject::Connection): holds a reference-counted
    ///        connection node.
    ///
    /// The node stores raw sender / receiver pointers, which are cleared when either side is
    /// destroyed. Therefore:
    ///  - it is not RAII: destruction does not disconnect;
    ///  - once sender / receiver are gone, is_valid() is false and disconnect() safely returns
    ///    false without dereferencing a dead object.
    /// @sa object_t::connect
    class HWSHQTB_EXPORT connection_handle_t {
    public:
        /// @brief Default-constructs an invalid handle.
        connection_handle_t() noexcept = default;

        /// @brief Whether the handle still refers to an existing connection.
        /// @note noexcept.
        bool is_valid() const noexcept;

        /// @brief Disconnect this connection.
        /// @return true if it was actually disconnected; false if the handle is invalid, the
        ///         connection is already cut, or either side has been destroyed.
        bool disconnect();

    private:
        friend class object_t;

        /// @brief Internal node (reference counted).
        std::shared_ptr<detail::connection> _node;
    };

    /// @brief Meta-object base class: provides reflective access to properties / methods plus the
    ///        global instance.
    /// @note Types need no registration (their interfaces are produced on demand); only CONVERSIONS
    ///       are registered, once, through register_conversion\<From, To\>().
    class object_t;
    class thread_t;
    class timer_event_t;

    namespace detail {
        /// @brief Push the dispatch frame that backs object_t::sender().
        /// @param sender The object emitting the signal being dispatched.
        void push_invoke_frame(object_t* sender);

        /// @brief Pop the innermost dispatch frame.
        void pop_invoke_frame();
    }

    namespace detail {
        /// @brief The DECLARED spelling of T with cv and references PRESERVED.
        /// @tparam T The type to name.
        /// @return A view of the same normalisation the SIGNAL macro's parameter column uses.
        /// @note This is deliberately NOT registered_type_name(): that one decays, so a signal whose
        ///       parameter is declared const item_index_t& is spelled "const item_index_t&" in the
        ///       table while the decayed form is "item_index_t". Comparing the two would refuse every
        ///       reference parameter, which is exactly what it did before this helper existed.
        template <typename T>
        consteval std::string_view declared_type_name() {
            return std::define_static_string(normalized_type_name<T>());
        }

        /// @brief Whether T is an instantiated std::function (the explicit-parameter-list form).
        template <typename T>
        struct is_std_function_trait: std::false_type { };

        template <typename R, typename... A>
        struct is_std_function_trait<std::function<R(A...)>>: std::true_type { };

        /// @brief is_std_function_trait as a variable template.
        template <typename T>
        inline constexpr bool is_std_function_v = is_std_function_trait<T>::value;

        /// @brief The parameter types of a callable's operator() (or of a function pointer).
        template <typename Signature>
        struct callable_signature;

        template <typename R, typename C, typename... A>
        struct callable_signature<R (C::*)(A...) const> {
            using type = std::tuple<A...>;
        };

        template <typename R, typename C, typename... A>
        struct callable_signature<R (C::*)(A...)> {
            using type = std::tuple<A...>;
        };

        template <typename R, typename... A>
        struct callable_signature<R (*)(A...)> {
            using type = std::tuple<A...>;
        };

        /// @brief Expand a deduced parameter tuple into the explicit connect<Args...>() form.
        struct deduced_connect {
            /// @brief Connect @p fn after deducing its parameters.
            /// @note Self is a template parameter so the call to self.connect<A...>() is DEPENDENT and
            ///       its lookup is deferred until object_t is complete; naming object_t directly here
            ///       would be an incomplete-type use, because these helpers sit before the class.
            template <typename Self, typename Tuple, typename F>
            static connection_handle_t invoke(Self& self, std::string_view signal_signature,
                                              object_t* context, F&& fn, connection_type_t type) {
                return [&]<typename... A>(std::tuple<A...>*) {
                    std::function<void(A...)> typed{ std::forward<F>(fn) };
                    return self.template connect<A...>(signal_signature, context, std::move(typed), type);
                }(static_cast<Tuple*>(nullptr));
            }

            /// @brief The context-free form.
            template <typename Self, typename F>
            static connection_handle_t run(Self& self, std::string_view signal_signature, F&& fn,
                                           connection_type_t type) {
                return invoke<Self, typename callable_signature<
                    std::remove_cvref_t<decltype(&std::decay_t<F>::operator())>>::type>(
                    self, signal_signature, nullptr, std::forward<F>(fn), type);
            }

            /// @brief The context form.
            template <typename Self, typename F>
            static connection_handle_t run(Self& self, std::string_view signal_signature,
                                           object_t* context, F&& fn, connection_type_t type) {
                return invoke<Self, typename callable_signature<
                    std::remove_cvref_t<decltype(&std::decay_t<F>::operator())>>::type>(
                    self, signal_signature, context, std::forward<F>(fn), type);
            }
        };

    }

    class HWSHQTB_EXPORT object_t {
        OBJECT

        friend class meta_type_t;
        friend class meta_property_t;
        friend class meta_method_t;
        friend class any_t;
        friend class core_application;
        friend class event_dispatcher;
        /// @brief thread_t's creating constructor binds object_t::_thread (the adopted identity).
        friend class thread_t;

    public:
        object_t();
        virtual ~object_t();

        /// @brief Copy and move are disabled: an object is a node in a tree, identified by its address.
        object_t(const object_t&) = delete;
        object_t& operator=(const object_t&) = delete;
        object_t(object_t&&) = delete;
        object_t& operator=(object_t&&) = delete;

        /// @brief Get the parent object.
        /// @return The parent pointer if there is one; nullptr for a root object.
        /// @note noexcept.
        object_t* parent() const noexcept;

        /// @brief Set the parent object (Qt semantics: the parent deletes all children when destroyed).
        /// @param parent The new parent; nullptr detaches from the current parent and makes this
        ///        object a root itself.
        /// @note Re-parenting only updates the two child lists: nothing else is tree-scoped, so the
        ///       object's meta-object and types are unaffected.
        /// @warning Must not be attached under itself or one of its descendants (a cycle); the
        ///          behaviour is undefined otherwise.
        void set_parent(object_t* parent);

        /// @brief Move this object and its whole subtree to the thread a thread_t runs on.
        /// @param target The target thread object; nullptr or a not-yet-started thread is ignored.
        /// @note The object must have no parent: re-parenting across threads is not allowed.
        /// @warning Timers are **not** migrated: they stay registered with the dispatcher of the
        ///          thread that started them, exactly like Qt (a QObject::moveToThread makes the old
        ///          timers unusable). Call kill_timer() before moving and start_timer() afterwards.
        /// @note Hidden from the meta-info: thread_t is a C++ object, not a value a caller can supply
        ///       by name.
        [[= make_hidden()]]
        void move_to_thread(thread_t* target);

        /// @brief Start a timer; each tick calls timer_event() on this object.
        /// @param milliseconds The interval; a value <= 0 is treated as 1.
        /// @param repeating Whether the timer re-arms after each tick.
        /// @return The timer id (> 0), or 0 on failure.
        /// @note The timer belongs to this thread's event loop and is dropped automatically when this
        ///       object is destroyed.
        int start_timer(int milliseconds, bool repeating = true);

        /// @brief Stop a timer started with start_timer().
        /// @param timer_id The id; unknown ids are ignored.
        void kill_timer(int timer_id);

        /// @brief Called when one of this object's timers ticks.
        /// @param e The timer event; the default implementation ignores it.
        virtual void timer_event(timer_event_t* e);

        /// @brief The thread this object belongs to (QObject::thread).
        /// @return The owning thread's identity; non-null while that thread lives, because every
        ///         thread that has data adopts an identity (the main thread included).
        /// @note noexcept.
        /// @note The identity lives in the owning thread's data: reading it after that thread has
        ///       finished is not supported (the data is gone), exactly as in Qt.
        /// @note Hidden from the meta-info: a thread_t is a C++ object, not a value a caller can
        ///       supply by name (the same reason move_to_thread(thread_t*) is hidden).
        [[= make_hidden()]]
        thread_t* thread() const noexcept;

        /// @brief The object that emitted the signal currently being dispatched.
        /// @return The sender inside a slot; nullptr outside any dispatch.
        /// @note noexcept.
        object_t* sender() const noexcept;

        /// @brief Get the child list.
        /// @return A non-owning view; invalidated by any set_parent or by a child's destruction.
        /// @note noexcept.
        /// @note Hidden from the meta-info: std::span is a C++-only type and reflecting it costs more
        ///       constexpr budget than the entry is worth. Use find_child()/find_children() by name.
        [[= make_hidden()]]
        std::span<object_t* const> children() const noexcept;

        /// @brief Block or unblock signal delivery from this object (QObject::blockSignals).
        /// @param block true to stop emitting, false to resume.
        /// @return The previous blocked state.
        /// @note Blocking is a counter: nested blockers must all be released before signals flow again
        ///       (only the true/false transition of the counter is exposed here).
        /// @note destroyed() is the one signal this does not stop: the destructor clears the counter
        ///       before emitting it, exactly like QObject::~QObject (`d->blockSig = 0`).
        bool block_signals(bool block) noexcept;

        /// @brief Whether signal delivery from this object is currently suppressed.
        /// @note noexcept.
        bool signals_blocked() const noexcept;

        /// @brief Print one line describing this object (QObject::dumpObjectInfo).
        /// @param os The stream to write to.
        /// @note Read-only diagnostics: class name, object name, connection count, property count.
        /// @note Hidden from the meta-info: a diagnostic helper, not a callable API entry.
        [[= make_hidden()]]
        void dump_object_info(std::ostream& os) const;

        /// @brief Print this object and its whole subtree (QObject::dumpObjectTree).
        /// @param os The stream to write to.
        /// @note Depth-first, one line per object, indented two spaces per level.
        /// @note Hidden from the meta-info: a diagnostic helper, not a callable API entry.
        [[= make_hidden()]]
        void dump_object_tree(std::ostream& os) const;

        /// @brief Find the first descendant of type T (optionally by name), depth-first.
        /// @tparam T The type to look for; defaults to object_t (any object).
        /// @param name When non-empty, only objects whose object_name() equals it match.
        /// @return The first match in depth-first order (children before grandchildren), or nullptr.
        template <typename T = object_t>
        T* find_child(std::string_view name = { }) const {
            for (object_t* child : child_list()) {
                if (child == nullptr) {
                    continue;
                }
                if (name.empty() || child->object_name() == name) {
                    // cast<T> is the RTTI-free path for a reflected T (meta-object chain) and the
                    // dynamic_cast fallback for an ordinary one; nullptr-on-failure is unchanged.
                    if (auto* typed = cast<T>(child); typed != nullptr) {
                        return typed;
                    }
                }
                if (T* nested = child->find_child<T>(name); nested != nullptr) {
                    return nested;
                }
            }
            return nullptr;
        }

        /// @brief Collect every descendant of type T (optionally by name), depth-first.
        /// @tparam T The type to look for; defaults to object_t (any object).
        /// @param name When non-empty, only objects whose object_name() equals it are collected.
        /// @return The matches in depth-first order.
        template <typename T = object_t>
        std::vector<T*> find_children(std::string_view name = { }) const {
            std::vector<T*> result;
            for (object_t* child : child_list()) {
                if (child == nullptr) {
                    continue;
                }
                if (name.empty() || child->object_name() == name) {
                    if (auto* typed = cast<T>(child); typed != nullptr) {
                        result.push_back(typed);
                    }
                }
                for (T* nested : child->find_children<T>(name)) {
                    result.push_back(nested);
                }
            }
            return result;
        }

        /// @brief The root object of the tree this object belongs to.
        /// @return The root object; returns this when this object is itself the root.
        /// @note noexcept.
        /// @note Computed by walking the parent chain: the cost is O(depth), and no state has to be
        ///       fixed up when an object is re-parented.
        object_t* root() const noexcept;

        /// @brief Whether this object is a visual widget (overridden by ui::widget_t).
        /// @return false here; true for every ui::widget_t.
        /// @note The object tree may own non-visual children; rendering, hit testing and layout must
        ///       skip them. The invariant is: is_widget() == true implies the object really is a
        ///       ui::widget_t (the widget layer static_casts after this check).
        /// @note Hidden from the meta-info: it is a C++-layer type marker, not scriptable API, and
        ///       method_count() adds base counts, so a reflected entry here would also change the
        ///       measured table sizes of every derived class.
        [[= make_hidden()]]
        virtual bool is_widget() const noexcept {
            return false;
        }

        /// @brief Whether this object takes part in the item tree (overridden by ui::item_t).
        /// @return false here; true for every ui::item_t (and therefore for every ui::widget_t).
        /// @note The core layer does not know the ui types: it answers a yes/no only, in exactly the
        ///       shape of is_widget(). The widget layer uses is_item() for every visual traversal and
        ///       keeps is_widget() narrowed to "is a ui::widget_t" (parent_widget(), type checks).
        /// @note Hidden from the meta-info for the same reason as is_widget().
        [[= make_hidden()]]
        virtual bool is_item() const noexcept {
            return false;
        }

        /// @brief Notification that @p child has just been attached to this object.
        /// @param child The new child; never nullptr.
        /// @note Called from set_parent() AFTER the child list contains @p child, so an override may
        ///       walk children() and see it.
        /// @note Hidden from the meta-info: an internal tree hook, not scriptable API (the same
        ///       reason is_widget() is hidden).
        [[= make_hidden()]]
        virtual void child_added(object_t* child);

        /// @brief Notification that @p child has just been detached from this object.
        /// @param child The detached child; no longer in children().
        /// @note Called from set_parent() and from ~object_t() (before the state block is released).
        ///       In the destructor path the child's dynamic type is already object_t, so an override
        ///       must not rely on the child's virtual is_widget() there.
        /// @note Hidden from the meta-info for the same reason as child_added().
        [[= make_hidden()]]
        virtual void child_removed(object_t* child);

        /// @brief Read a property by name, static or dynamic (QObject::property).
        /// @param name Property name.
        /// @return The value; an invalid any_t when neither a declared property nor a dynamic one has
        ///         that name.
        /// @note The handle-based lookup lives on the meta-object: use meta_object()->property(name).
        /// @note Hidden from the meta-info: any_t is not a parameter type a script could supply.
        [[= make_hidden()]]
        any_t property(std::string_view name) const;



        /// @brief Put this object on the pending-delete queue; the event loop deletes it when
        ///        dispatching (modelled on QObject::deleteLater).
        /// @note Idempotent; the actual delete happens after the event loop has drained its posting
        ///       queue (so this object lives at least until the end of that round).
        void delete_later();

        /// @name Event handling (QEvent family)
        /// @{
        /// @brief Event handling entry point (QObject::event).
        /// @param event The event; may be nullptr.
        /// @return true if the event was handled.
        /// @note Default implementation: MetaCall -> run the queued call; type >= User ->
        ///       custom_event; anything else -> ignore and return false (DeferredDelete / Child*
        ///       are handled internally by the dispatcher and friends).
        virtual bool event(event_t* event);

        /// @brief Dedicated entry point for user events (type >= User) (QObject::customEvent).
        /// @param event The event.
        virtual void custom_event(event_t* event);

        /// @brief Event filter (QObject::eventFilter).
        /// @param watched The object the event is targeted at.
        /// @param event The event.
        /// @return true if the event was consumed (event() is then skipped).
        virtual bool event_filter(object_t* watched, event_t* event);

        /// @brief Install an event filter (QObject::installEventFilter).
        /// @param filter The filter object; nullptr or this object -> ignored.
        /// @note Idempotent; filters run before this object's event() (installation order).
        void install_event_filter(object_t* filter);

        /// @brief Remove an event filter (QObject::removeEventFilter).
        /// @param filter The filter object.
        void remove_event_filter(object_t* filter);
        /// @}

        /// @brief Whether this object (directly or indirectly) inherits from the class named
        ///        @p class_name.
        /// @param class_name Class name.
        /// @return true if it does.
        /// @note noexcept.
        bool inherits(std::string_view class_name) const noexcept;

        /// @brief Convenience: look up a property by name and write it.
        /// @param name Property name.
        /// @param value The value to write.
        /// @return true if the property exists and the write succeeded; false if the name misses,
        ///         the property is read-only, or the value does not fit.
        /// @note Equivalent to property(name).write(*this, value); the same exception contract as
        ///       meta_property_t::write applies.
        /// @note Hidden from the meta-info: any_t is not a parameter type a script could supply, so
        ///       the entry only costs constexpr budget. Use the property table instead.
        [[= make_hidden()]]
        bool set_property(std::string_view name, const any_t& value);

        /// @brief The names of every dynamic property currently stored on this object.
        /// @return The names, in unspecified order.
        /// @note Hidden from the meta-info: std::vector<std::string> is a C++-only container type.
        [[= make_hidden()]]
        std::vector<std::string> dynamic_property_names() const;

        void set_object_name(const std::string& name);
        [[=make_property("object_name")]]
        std::string object_name()const;

        /// @brief Emitted by the destructor immediately before the object is destroyed
        ///        (QObject::destroyed).
        /// @param obj This object; the argument carries the address so a slot can tell several
        ///        dying senders apart.
        /// @note Emitted from ~object_t at Qt's point: after the death watch has been marked (an
        ///       object_pointer_t already reads as null inside the slot, like a QPointer) and before
        ///       the children are deleted, before the connection set is torn down and before the
        ///       object is detached from its parent — so the slot still sees a live object.
        /// @note It CANNOT be blocked (Qt: "cannot be blocked"): the destructor clears the
        ///       block_signals() counter and dispatches this one signal directly, so a slot runs even
        ///       when block_signals(true) is held. Qt does the same (`d->blockSig = 0` in
        ///       QObject::~QObject).
        /// @note Deleting the object again from its own destroyed() slot is a user error, as in Qt;
        ///       destroying OTHER objects from the slot is supported (the emission walks a snapshot of
        ///       the connection table, and a child list that is popped as it is deleted).
        SIGNAL(void destroyed(object_t* obj = nullptr) const)
        SIGNAL(void object_name_changed(const std::string& objectName) const)

        /// @name Connections and emission
        /// @{
        /// @brief Connect: resolve the sender's own signal and the receiver's own method by
        ///        signature (both walk the chain).
        /// @param signal_signature The sender's signal signature (normalised).
        /// @param receiver The receiver; nullptr -> returns an invalid handle.
        /// @param slot_signature The receiver's method signature (normalised).
        /// @param type Connection type; Auto by default.
        /// @return A valid handle on success; an invalid handle if the sender has no such signal,
        ///         the receiver has no such method, or @p receiver is null.
        /// @note Unique: if a connection with the same (signal, receiver, slot) already exists, its
        ///       handle is returned.
        /// @note Auto is resolved at emit time: Direct when the receiver lives on the emitting thread,
        ///       Queued otherwise. Queued / BlockingQueued are accepted and recorded; emission goes
        ///       through the event loop.
        /// @note A BlockingQueued connection whose receiver lives on the emitting thread is skipped
        ///       with a stderr message, because a blocking wait on the emitting thread would deadlock.
        connection_handle_t connect(std::string_view signal_signature,
                                  object_t* receiver,
                                  std::string_view slot_signature,
                                  connection_type_t type = connection_type_flag::AUTO);

        /// @brief Connect a signal to a callable, deducing its parameter types.
        /// @tparam F The callable's type; its single non-generic operator() is reflected.
        /// @param signal_signature The sender's signal signature (normalised).
        /// @param fn The callable.
        /// @param type Connection type; Auto by default.
        /// @return A valid handle on success; an invalid one on the same failures as the explicit form.
        /// @note This overload exists because template deduction does NOT convert a lambda to
        ///       std::function: without it every call site would have to spell the parameter list in a
        ///       std::function wrapper, which is the noise the typed form was meant to remove.
        /// @note A GENERIC lambda (auto parameters) has no single operator() and is refused by a
        ///       static_assert naming that reason.
        template <typename F>
            requires(::std::is_class_v<::std::decay_t<F>> && !detail::is_std_function_v<::std::decay_t<F>> && requires { &::std::decay_t<F>::operator(); })
        connection_handle_t connect(std::string_view signal_signature, F&& fn,
                                    connection_type_t type = connection_type_flag::AUTO) {
            return detail::deduced_connect::run(*this, signal_signature, std::forward<F>(fn), type);
        }

        /// @brief Connect a signal to a callable, deducing its parameter types.
        /// @param signal_signature The sender's signal signature (normalised).
        /// @param context Context object; destroying it cuts the connection.
        /// @param fn The callable.
        /// @param type Connection type; Auto by default.
        template <typename F>
            requires(::std::is_class_v<::std::decay_t<F>> && !detail::is_std_function_v<::std::decay_t<F>> && requires { &::std::decay_t<F>::operator(); })
        connection_handle_t connect(std::string_view signal_signature, object_t* context, F&& fn,
                                    connection_type_t type = connection_type_flag::AUTO) {
            return detail::deduced_connect::run(*this, signal_signature, context, std::forward<F>(fn), type);
        }

        /// @brief Connect a signal to a TYPED callable (Qt's connect-to-lambda equivalent).
        /// @tparam Args The callable's parameter types, in the signal's order.
        /// @param signal_signature The sender's signal signature (normalised).
        /// @param fn The callable. It receives the signal's arguments as VALUES, in order.
        /// @param type Connection type; Auto by default.
        /// @return A valid handle on success; an INVALID handle when the signal does not exist, when
        ///         @p fn is empty, or when the parameter list does not match the signal.
        /// @note FEWER parameters than the signal declares is legal (Qt's rule): the callable is
        ///       matched against the FIRST sizeof...(Args) parameters, so `[]()` observes "something
        ///       happened" and `[](int v)` observes the first value. The order and the types of the
        ///       parameters it DOES take must match exactly; a mismatch is refused, never coerced.
        /// @note A reference parameter (`T&`) WRITES THROUGH on a Direct call, because the callable
        ///       receives an alias of the emitter's argument. On a Queued call the arguments are
        ///       copies, so a write reaches a copy and is lost — the same rule Qt has.
        /// @note A functor has no receiver OBJECT, so the thread that CREATES the connection is its
        ///       "receiver thread": Direct calls inline, Queued runs on the creating thread's event
        ///       loop, Auto picks Queued when that thread differs from the emitting one.
        template <typename... Args>
        connection_handle_t connect(std::string_view signal_signature,
                                    std::function<void(Args...)> fn,
                                    connection_type_t type = connection_type_flag::AUTO) {
            return connect_functor(signal_signature, nullptr,
                                   std::array<std::string_view, sizeof...(Args)>{
                                       ::hwshqtb::metaobject::detail::declared_type_name<Args>()... },
                                   make_typed_sink<Args...>(std::move(fn)), type);
        }

        /// @brief Connect a signal to a typed callable, tying the connection's lifetime to a context
        ///        object (Qt's connect(sender, signal, context, functor)).
        /// @param signal_signature The sender's signal signature (normalised).
        /// @param context Context object; destroying it cuts the connection. nullptr behaves like the
        ///        plain form.
        /// @param fn The callable; same matching rules as the context-free overload.
        /// @param type Connection type; Auto by default.
        /// @return A valid handle on success; an invalid handle on the same failures as above.
        /// @note This is the way to make a functor connection die with an owner: a functor has no
        ///       receiver object, so without a context it outlives everything it captured.
        template <typename... Args>
        connection_handle_t connect(std::string_view signal_signature, object_t* context,
                                    std::function<void(Args...)> fn,
                                    connection_type_t type = connection_type_flag::AUTO) {
            return connect_functor(signal_signature, context,
                                   std::array<std::string_view, sizeof...(Args)>{
                                       ::hwshqtb::metaobject::detail::declared_type_name<Args>()... },
                                   make_typed_sink<Args...>(std::move(fn)), type);
        }

        /// @brief Connect a signal to a callable that takes the arguments ERASED (a spy, a recorder).
        /// @param signal_signature The sender's signal signature (normalised).
        /// @param fn The callable; it receives the arguments as a span of ANY-typed handles.
        /// @param type Connection type; Auto by default.
        /// @return A valid handle on success; an invalid handle when the signal does not exist or
        ///         @p fn is empty.
        /// @note The span is a VIEW that is only valid during the call. On a Direct call an element is
        ///       a NON-OWNING ALIAS of the emitter's argument (so writing through it reaches the
        ///       emitter); on a Queued call it is the event's own copy.
        /// @note Prefer the typed overload whenever the parameter list is known: this one gives up the
        ///       type names and the arity check, which is exactly what a generic recorder wants.
        /// @note Hidden from the meta-info: std::function<void(std::span<any_t>)> is a C++-only
        ///       parameter type, exactly like the removed raw-argv form was.
        [[= make_hidden()]]
        connection_handle_t variadic_connect(std::string_view signal_signature,
                                             std::function<void(std::span<any_t>)> fn,
                                             connection_type_t type = connection_type_flag::AUTO);

        /// @brief Connect a signal to a slot, tying the connection's lifetime to a context object
        ///        (Qt's connect(sender, signal, context, slot)).
        /// @param signal_signature The sender's signal signature (normalised).
        /// @param context Context object; destroying it cuts the connection. nullptr behaves like a
        ///        plain connection with no context.
        /// @param receiver The receiver; nullptr -> returns an invalid handle.
        /// @param slot_signature The receiver's method signature (normalised).
        /// @param type Connection type; Auto by default.
        /// @return A valid handle on success.
        /// @note Five parameters, not four: a four-parameter (signal, object, signature, type) form
        ///       would have the same parameter types as the plain receiver overload and fail to
        ///       compile (redeclaration).
        /// @note Hidden from the meta-info, like the functor overload: object_t already carries
        ///       several connect() overloads and the reflection budget is close to the limit.
        [[= make_hidden()]]
        connection_handle_t connect(std::string_view signal_signature,
                                  object_t* context,
                                  object_t* receiver,
                                  std::string_view slot_signature,
                                  connection_type_t type = connection_type_flag::AUTO);

        /// @brief Connect: pass the method handles directly.
        /// @param signal The sender's signal handle; must satisfy is_signal().
        /// @param receiver The receiver; nullptr -> returns an invalid handle.
        /// @param slot The receiver's method handle.
        /// @param type Connection type.
        /// @return A valid handle on success; an invalid handle if an argument is invalid.
        connection_handle_t connect(const meta_method_t& signal,
                                  object_t* receiver,
                                  const meta_method_t& slot,
                                  connection_type_t type = connection_type_flag::AUTO);

        /// @brief Disconnect: by handle.
        /// @param handle The connection handle.
        /// @return true if it was actually disconnected; false if the handle is invalid or already cut.
        bool disconnect(const connection_handle_t& handle);

        /// @brief Disconnect: exact match on (signal signature, receiver, method signature).
        /// @return true if any connection was disconnected; false if nothing matched.
        bool disconnect(std::string_view signal_signature,
                        object_t* receiver,
                        std::string_view slot_signature);

        /// @brief Disconnect: every connection where this object is the sender and the given object
        ///        is the receiver.
        /// @param receiver The receiver.
        /// @return The number of connections actually disconnected.
        std::size_t disconnect(object_t* receiver);
        /// @}

    public:
        /// @brief Low-level entry point for signal activation: walk this object's connection table
        ///        and dispatch according to connection_type.
        /// @param signal The method handle of the current signal (must be named and satisfy
        ///        is_signal()).
        /// @param args The same arguments as ANY-TYPED handles, in the same order: a NON-OWNING
        ///        alias of each parameter on a Direct call, the event's own copy on a Queued one.
        ///        This is what a functor connection receives; @p params stays for slot invocation.
        /// @param params Argument array, same convention as meta_method_t::invoke (params[0] is
        ///        always nullptr).
        /// @note The name mirrors QMetaObject::activate. Direct dispatches by invoking the slot's
        ///       caller; Queued / BlockingQueued hand the call to the event loop.
        /// @note No argument checking is performed: the slot's caller reads @p params by convention
        ///       (signal and slot parameters must match).
        /// @sa SIGNAL
        /// @note Hidden from the meta-info: a low-level entry point, not something a script calls by
        ///       name.
        /// @note Public because a dynamically built signal is emitted from a plain function pointer
        ///       (a thunk) that cannot reach a private member — the same reason
        ///       QMetaObject::activate is public in Qt.
        [[= make_hidden()]]
        void activate_signal(const meta_method_t& signal, void** params,
                              std::span<any_t> args = { }) const;

    private:
        /// @brief Dispatch one signal to the live entries of @p connections (the shared body of
        ///        activate_signal() and of the destructor's destroyed() emission).
        /// @param signal The method handle of the signal.
        /// @param params Argument array, same convention as activate_signal().
        /// @param connections The connection nodes to walk: the object's own table for a normal
        ///        emission, a snapshot of it for the destructor's emission.
        /// @note The caller owns the block-signal decision: activate_signal() refuses to dispatch
        ///       while block_signals() is held, while destroyed() must be emitted regardless (Qt's
        ///       "cannot be blocked").
        /// @note Hidden from the meta-info: internal dispatch helper, not a callable API entry.
        [[= make_hidden()]]
        void dispatch_signal(const meta_method_t& signal, void** params, std::span<any_t> args,
                             const std::vector<std::shared_ptr<detail::connection>>& connections) const;

        /// @brief The one place a functor connection is created (every public form funnels here).
        /// @param signal_signature The signal to connect to.
        /// @param context Optional context object, nullptr for none.
        /// @param parameter_types The callable's parameter type names, in order; matched against the
        ///        signal's FIRST entries (fewer is legal).
        /// @param sink The callable, already adapted to the erased dispatch form.
        /// @param type Connection type.
        /// @return A valid handle, or an INVALID one when the signal does not exist, the callable is
        ///         empty, or the arity / a type name does not match the signal.
        /// @note The single validation point: the public forms only build the arguments, so the
        ///       rules ("fewer is legal, order and types must match") live in exactly one place.
        /// @note Hidden from the meta-info: private plumbing, not part of the reflected API.
        [[= make_hidden()]]
        connection_handle_t connect_functor(std::string_view signal_signature, object_t* context,
                                           std::span<const std::string_view> parameter_types,
                                           std::function<void(std::span<any_t>)> sink,
                                           connection_type_t type);

        /// @brief Adapt a typed callable to the erased dispatch form.
        /// @tparam Args The callable's parameter types.
        /// @param fn The callable.
        /// @return A sink that unpacks each any_t into the declared type.
        /// @note get<Args>() does the work: a reference argument reads (or writes) the aliased object,
        ///       a value argument copies, and a const mismatch is refused by any_t itself rather than
        ///       silently reinterpreted here.
        template <typename... Args>
        static std::function<void(std::span<any_t>)> make_typed_sink(std::function<void(Args...)> fn) {
            return [fn = std::move(fn)](std::span<any_t> args) {
                [&]<std::size_t... Index>(std::index_sequence<Index...>) {
                    fn(args[Index].template get<Args>()...);
                }(std::index_sequence_for<Args...>{ });
            };
        }

        /// @brief Run every event filter installed on this object in turn (used by
        ///        core_application::notify).
        /// @param event The event.
        /// @return true if any filter consumed it.
        /// @note The filter node type is only complete in the implementation file, so the traversal
        ///       lives here.
        /// @note Hidden from the meta-info: internal filter loop, not a callable API entry.
        [[= make_hidden()]]
        bool apply_event_filters(event_t* event);

    public:
        /// @name The class registry: create an object by type name
        ///
        /// A process-wide (one registry per module) name -> class map, so any object_t subclass can be
        /// built by name. The registry stores `const meta_object_t*` and nothing else — no creator
        /// functions.
        ///
        /// The primary key is `type->class_name()`; the short name after the last "::" is registered as
        /// an alias when it is not already taken. Registration is FIRST WINS: a duplicate name warns
        /// and returns false without touching the existing entry.
        /// @{
        /// @brief Register a class so create() can build it by name.
        /// @param type The class's meta-object; stored by address, so it must outlive the process (a
        ///        `static_meta_object`, a meta_object_builder_t arena entry, ...). nullptr registers nothing.
        /// @return true when it was added; false (plus one Warning) when @p type is null, has no class
        ///         name, or that name is already registered.
        /// @note Registration does NOT ask whether the class is constructible: the key is a name, and
        ///       whether an instance can be built is create()'s question (it reports a class without a
        ///       0-argument constructor with one Warning).
        /// @note Hidden from the meta-info: a process-wide service entry point, not scriptable API.
        [[= make_hidden()]]
        static bool register_type(const meta_object_t* type);

        /// @brief Create an instance of a class registered under @p type_name.
        /// @param type_name The class name (or its short-name alias). Lookup tries the primary key
        ///        first, then the alias table.
        /// @param parent The parent for the new object; nullptr keeps it a root. The object is created
        ///        through the class's ZERO-ARGUMENT constructor and then attached with set_parent(), so
        ///        a widget's visual-parent parameter never has to be adapted from object_t*.
        /// @return The new object, or nullptr (plus one Warning when the name is unknown or the class
        ///         has no usable 0-argument constructor).
        /// @note "Usable" is the reflected table's verdict: a 0-argument constructor declared by the
        ///       class itself and marked constructible. A run-time built meta-object (meta_object_builder_t)
        ///       answers nullptr unless add_constructor() gave it a callable 0-argument row; so does an
        ///       abstract class.
        /// @note Hidden from the meta-info: see register_type().
        [[= make_hidden()]]
        static object_t* create(std::string_view type_name, object_t* parent = nullptr);

        /// @brief Every name create() accepts (primary class names and short-name aliases), sorted.
        /// @return The names in ascending order, duplicate-free; the stable order a designer or a
        ///         document model iterates.
        /// @note Hidden from the meta-info: introspection for tools, not scriptable API.
        [[= make_hidden()]]
        static std::vector<std::string> registered_type_names();
        /// @}

    private:
        /// @brief The child pointer vector, or one shared immutable empty vector when this object has
        ///        no state block.
        /// @return A reference that is valid until the next set_parent on this object; the empty
        ///         fallback is a function-local static, so it is immutable and thread-safely
        ///         initialised.
        /// @note Hidden from the meta-info: internal storage access, not a callable API entry.
        [[= make_hidden()]]
        const std::vector<object_t*>& child_list() const noexcept;

        /// @brief The object name, or one shared immutable empty string when this object has no state
        ///        block.
        /// @return A reference to the name; the empty fallback is a function-local static.
        /// @note Hidden from the meta-info: internal storage access, not a callable API entry.
        [[= make_hidden()]]
        const std::string& name_ref() const noexcept;

        /// @brief The lazily allocated state block, created on first use.
        /// @return The object's data block; never nullptr.
        /// @note The ONE allocation site of object_t. Every accessor that needs state (parent,
        ///       children, connections, filters, watch, name, dynamic properties) calls this first,
        ///       so a bare object allocates nothing.
        /// @note Hidden from the meta-info: internal storage access, not a callable API entry.
        [[= make_hidden()]]
        detail::object_data_t* ensure_data() const;

        /// @brief The affinity data behind thread() (internal).
        /// @return The thread_data_t this object's affinity points at; never nullptr after
        ///         construction.
        /// @note Internal callers compare affinity DATA (the dispatcher and the invoke path);
        ///       thread() answers the public identity built on top of it.
        /// @note Hidden from the meta-info: internal storage access, not a callable API entry.
        [[= make_hidden()]]
        detail::thread_data_t* thread_data() const noexcept;

        /// @brief Move this object and its whole subtree to a thread's data (the affinity type).
        /// @param target The target thread data; nullptr or the current affinity is ignored.
        /// @note This is the implementation of move_to_thread(thread_t*), which resolves the thread
        ///       object's data and delegates here.
        /// @note The same restrictions as the thread_t* overload apply (no parent, no timer
        ///       migration).
        /// @note Hidden from the meta-info: its parameter is a detail:: type; it is private because
        ///       the public API speaks in thread_t*.
        [[= make_hidden()]]
        void move_to_thread(detail::thread_data_t* target);

        /// @brief The thread this object belongs to (recorded at construction, changed by
        ///        move_to_thread).
        /// @note Deliberately inline: it is the thread affinity, not per-object state, and keeping it
        ///       in the data block would make every object allocate.
        detail::thread_data_t* _thread = nullptr;

        /// @brief The lazily allocated state block; nullptr until an accessor needs state.
        /// @note mutable: watch() is const but has to create the liveness token on first use.
        mutable detail::object_data_t* _data = nullptr;

        /// @brief Nesting depth of block_signals(true) calls.
        /// @note 16 bits: nesting beyond 65535 wraps around.
        std::uint16_t _signal_block_count = 0;

    public:
        /// @brief The liveness token of this object (internal).
        /// @return A shared pointer to this object's death_watch_t; never null while the object lives.
        /// @note Exposed so object_pointer_t can hold a weak_ptr without befriending a template.
        /// @note Creates the state block on first use (an object_pointer_t to a bare object is what
        ///       triggers it), so the returned reference is into the data block.
        /// @note Hidden from the meta-info: internal accessor used by object_pointer_t, not a callable
        ///       API entry.
        [[= make_hidden()]]
        const std::shared_ptr<detail::death_watch_t>& watch() const noexcept;

    };
}

#endif
