#ifndef HWSHQTB__METAOBJECT__PROPERTY_T_H
#define HWSHQTB__METAOBJECT__PROPERTY_T_H

/// @file property_t.h
/// @brief The type-erased property handle property_t, its binding engine, the per-thread
///        invalidation scheduler and the property update groups.
///
/// One type-erased detail::core_t holds the mutable state of a variable (owning backend) or of a
/// property view (reference backend); the public property_t handle is one shared_ptr to it.
///
/// @note The function bodies live in src/property_t.cpp, except the ones that must stay here: the
///       template (the typed constructor of property_binding_t) and the constexpr constants.

#include "any_t.h"
#include "log.h"
#include "meta_type_t.h"
#include "thread_data_t.h"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <meta>
#include <new>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace hwshqtb::metaobject {
    /// @brief What went wrong when a binding was evaluated (Qt's QPropertyBindingError).
    ///
    /// A binding that evaluated successfully carries kind_enum::NONE. A closure reports an error
    /// through set_binding_error() (Qt's currentlyEvaluatingBinding()->setError), and the library
    /// reports a dependency cycle of its own as kind_enum::BINDING_LOOP.
    struct HWSHQTB_EXPORT property_binding_t_error_t {
        /// @brief The kind of failure (Qt's QPropertyBindingError::Type).
        enum struct kind_enum {
            /// @brief No error: the binding evaluated successfully.
            NONE,
            /// @brief The closure reported an error of its own (Qt's UnknownError).
            UNKNOWN,
            /// @brief Evaluation was stopped because a property depended on its own value.
            BINDING_LOOP
        };

        /// @brief The kind of failure.
        kind_enum kind = kind_enum::NONE;

        /// @brief A human-readable description (Qt's description()); empty when there is none.
        std::string message;

        /// @brief Whether an error occurred (Qt's QPropertyBindingError::hasError()).
        /// @note noexcept. Defined in src/property_t.cpp.
        bool has_error() const noexcept;
    };

    /// @brief The property view was asked to observe a property that cannot notify (property_t).
    ///
    /// Derives from std::logic_error, like the bad_any_t family: the caller broke the contract
    /// (property_t::from() requires meta_property_t::is_notifiable()).
    class HWSHQTB_EXPORT bad_property_t: public std::logic_error {
    public:
        /// @brief Construct with the given description.
        /// @param what Exception description.
        /// @note Defined in src/property_t.cpp.
        explicit bad_property_t(const std::string& what);
    };

    class object_t;
    class meta_property_t;
    class property_t;
    class property_change_handler_t;
    class binding_scheduler_t;

    class property_binding_t;

    namespace detail {
        struct core_t;
        class binding_context_t;
        struct reference_state_t;
        struct reference_connection_t;
        class property_subscription_t;
        class handler_node_t;
        struct subscription_node_t;

        // ---------------------------------------------------------------------
        // The reference backend's state (defined in src/property_t.cpp; only the forward
        // declarations live here)
        // ---------------------------------------------------------------------

        // ---------------------------------------------------------------------
        // The single notification list
        // ---------------------------------------------------------------------

        /// @brief One entry of a core's notification list.
        ///
        /// There are exactly two kinds: a dependency edge (a dependant of a binding installed on the
        /// core) and a change handler. Both live in core::subscribers, in registration order.
        /// @note No virtual functions: the kind tag is enough to dispatch, and the concrete objects
        ///       are never destroyed through this base.
        struct subscription_node_t {
            /// @brief What the entry is.
            enum class kind_enum : unsigned char {
                /// @brief A dependant of a binding installed on the core.
                DEPENDENCY,
                /// @brief A user change handler.
                HANDLER
            };

            /// @brief Construct one entry of the given kind.
            /// @param node_kind The entry's kind.
            /// @note Defined in src/property_t.cpp.
            explicit subscription_node_t(kind_enum node_kind) noexcept;

            subscription_node_t(const subscription_node_t&) = delete;
            subscription_node_t& operator=(const subscription_node_t&) = delete;
            ~subscription_node_t() = default;

            /// @brief The entry's kind (the dispatch tag).
            kind_enum kind;
        };

        /// @brief The shared state of one installed binding (location, error channel, stickiness).
        ///
        /// Shared between the core that evaluates the binding and every property_binding_t handle that
        /// refers to it, which is what makes set_sticky() and error() through a handle change the
        /// installed binding (Qt's QPropertyBindingPrivate).
        struct binding_state_t {
            /// @brief Where the binding was installed (Qt's QPropertyBindingSourceLocation).
            std::source_location location{};
            /// @brief The error of the last evaluation (Qt's QPropertyBindingError).
            property_binding_t_error_t error;
            /// @brief Whether a write keeps this binding installed (Qt's sticky flag).
            bool sticky = false;
            /// @brief Whether the next install notifies even when the value does not change.
            /// @note Qt's QPropertyBindingPrivate::scheduleNotify(): the request is set through a
            ///       property_binding_t handle and consumed by the install.
            bool pending_notify = false;
        };

        /// @brief One registered change handler (the shared state behind a property_change_handler_t).
        ///
        /// The node owns the callback and a type-erased link to the core it is registered with. Its
        /// ownership is shared: the property_change_handler_t holds it through std::shared_ptr and the
        /// core holds a std::shared_ptr in core::handlers while the node is registered. A core that
        /// is being destroyed "orphans" its nodes instead of destroying them (unbind()), so a handle
        /// that outlives its core stays alive, becomes inert (is_bound() == false), can be destroyed
        /// without touching freed memory and can be re-targeted with set_source().
        /// @note NOT exported: the class is a detail type, and property_change_handler_t's constructor
        ///       takes the callable already erased as std::function<void()> and allocates the node in
        ///       its own out-of-line body. No consumer TU instantiates an allocation of this class
        ///       at all, so consumers only need the forward declaration (std::shared_ptr<T> works
        ///       with an incomplete T when the deleter lives where the object was created).
        class handler_node_t: public subscription_node_t {
        public:
            /// @brief Construct with the callback to invoke on a value change.
            /// @param callback The callback; may be empty.
            /// @note Defined in src/property_t.cpp.
            explicit handler_node_t(std::function<void()> callback);

            handler_node_t(const handler_node_t&) = delete;
            handler_node_t& operator=(const handler_node_t&) = delete;

            /// @brief Destroy the node: detach it from its owner if it is still registered.
            /// @note Defined in src/property_t.cpp.
            ~handler_node_t();

            /// @brief Invoke the callback.
            /// @note May throw whatever the callback throws.
            /// @note Defined in src/property_t.cpp.
            void invoke() const;

            /// @brief Whether the node is registered with a live core.
            /// @note noexcept.
            /// @note Defined in src/property_t.cpp.
            bool is_bound() const noexcept;

            /// @brief Register with @p owner, whose @p detach hook removes this node again.
            /// @param owner The core (type-erased).
            /// @param detach The owner's removal hook.
            /// @note noexcept.
            /// @note Defined in src/property_t.cpp.
            void bind(void* owner, void (*detach)(void*, handler_node_t*) noexcept) noexcept;

            /// @brief Forget the owner without calling its detach hook (the owner is dying).
            /// @note noexcept.
            /// @note Defined in src/property_t.cpp.
            void unbind() noexcept;

            /// @brief Remove this node from its owner, if any (the handle is dying or re-targeted).
            /// @note noexcept. The fields are cleared before the hook runs and the caller keeps a
            ///       reference to this node, so neither re-entrance nor the owner's erase can
            ///       destroy the node from inside the hook.
            /// @note Defined in src/property_t.cpp.
            void detach() noexcept;

        private:
            /// @brief The callback.
            std::function<void()> _callback;
            /// @brief The core the node is registered with (null when inert).
            void* _owner = nullptr;
            /// @brief Removes this node from _owner (null when inert).
            void (*_detach)(void*, handler_node_t*) noexcept = nullptr;
        };

        /// @brief One owning dependency edge, owned by the dependant's read set.
        ///
        /// The edge lives inside the dependant's property_subscription_t, and the dependency core holds
        /// a plain pointer to it in core::subscribers. Attaching registers it there; detaching
        /// removes it. The dependant is a plain pointer because the edge dies with the dependant.
        /// @note Its address must stay stable while it is attached (core::dependencies is resized
        ///       before the edges are registered and never grown afterwards).
        class dependency_edge_t: public subscription_node_t {
        public:
            /// @brief Construct a detached edge.
            /// @note noexcept.
            /// @note Defined in src/property_t.cpp.
            dependency_edge_t() noexcept;

            dependency_edge_t(const dependency_edge_t&) = delete;
            dependency_edge_t& operator=(const dependency_edge_t&) = delete;

            /// @brief Detach on destruction.
            ~dependency_edge_t();

            /// @brief Register this edge in @p dependency_core and remember @p dependant_core.
            /// @param dependency_core The core that was read.
            /// @param dependant_core The core that read it.
            /// @note Not noexcept: the dependency's subscriber list allocates.
            void attach(core_t& dependency_core, core_t& dependant_core);

            /// @brief Remove this edge from its dependency's subscriber list.
            /// @note noexcept. Idempotent.
            void detach() noexcept;

            /// @brief Tell the dependant that the dependency changed.
            /// @note noexcept.
            void propagate() noexcept;

            /// @brief Tell the dependant that the dependency is being destroyed.
            /// @note noexcept. Called from the dependency's destructor: the edge forgets the dying
            ///       core and the dependant drops the queued recompute (it keeps its last value).
            void dependency_destroyed() noexcept;

            /// @brief The dependency core this edge is registered with (null while detached).
            /// @note noexcept.
            /// @note Defined in src/property_t.cpp.
            core_t* dependency() const noexcept;

        private:
            /// @brief The dependency core (null while detached).
            core_t* _dependency = nullptr;
            /// @brief The dependant core (dies with this edge).
            core_t* _dependant = nullptr;
        };

        /// @brief One recorded dependency, as collected by a running evaluation.
        ///
        /// A binding_context_t collects these while the closure runs; the finished read set is turned
        /// into property_subscription_t objects by the dependant afterwards.
        struct pending_dependency_t {
            /// @brief The owning dependency's core (null for a reference dependency).
            const core_t* owning = nullptr;
            /// @brief The reference view's state (null for an owning dependency).
            std::shared_ptr<reference_state_t> reference;
            /// @brief The reference dependency's object identity.
            void* reference_owner = nullptr;
            /// @brief The reference dependency's property identity.
            const void* reference_property = nullptr;
            /// @brief Whether this dependency is the core that is evaluating (a binding loop).
            bool self_dependency = false;

            /// @brief Whether this dependency is the same as another (owning core and owner/property).
            /// @param other_owning The other dependency's owning core.
            /// @param other_owner The other dependency's owner token.
            /// @param other_property The other dependency's property identity.
            /// @return true when both describe the same owning core or the same (owner, property) pair.
            /// @note noexcept.
            /// @note Defined in src/property_t.cpp.
            bool same(const core_t* other_owning, void* other_owner, const void* other_property) const noexcept;
        };

        /// @brief The identity of one property_t: owning core, or (object, property) for a reference.
        struct dependency_identity_t {
            /// @brief The owning core (null for a reference view).
            const core_t* owning = nullptr;
            /// @brief The reference view's state (null for an owning core).
            std::shared_ptr<reference_state_t> reference;
            /// @brief The reference view's object.
            void* owner = nullptr;
            /// @brief The reference view's property.
            const void* property = nullptr;
        };

        /// @brief One recorded dependency of an installed binding.
        ///
        /// Owned by the dependant's core (core::dependencies) and holding the stable dependency_edge_t
        /// that the dependency core points at, plus the connection of a reference dependency.
        class property_subscription_t {
        public:
            /// @brief Construct a detached subscription.
            /// @note noexcept.
            property_subscription_t() noexcept = default;

            property_subscription_t(const property_subscription_t&) = delete;
            property_subscription_t& operator=(const property_subscription_t&) = delete;

            /// @brief Move an UNATTACHED subscription (core::dependencies growth).
            /// @param other The subscription being moved; it is left detached.
            /// @note noexcept. Only legal while @p other is not registered in a dependency.
            /// @note Defined in src/property_t.cpp.
            property_subscription_t(property_subscription_t&& other) noexcept;

            /// @brief Move-assign an UNATTACHED subscription.
            /// @param other The subscription being moved; it is left detached.
            /// @return @c *this.
            /// @note noexcept.
            /// @note Defined in src/property_t.cpp.
            property_subscription_t& operator=(property_subscription_t&& other) noexcept;

            /// @brief Detach on destruction.
            /// @note Defined in src/property_t.cpp.
            ~property_subscription_t();

            /// @brief Attach this subscription to the dependency described by @p pending.
            /// @param pending The recorded dependency.
            /// @param dependant The core that read it.
            /// @note Not noexcept: attaching allocates (subscriber list, connection).
            void attach(const pending_dependency_t& pending, core_t& dependant);

            /// @brief Detach this subscription from its dependency.
            /// @note noexcept. Idempotent.
            void detach() noexcept;

            /// @brief The dependency edge (also the entry in the dependency's subscriber list).
            /// @note noexcept.
            /// @note Defined in src/property_t.cpp.
            dependency_edge_t& edge() noexcept;

        private:
            /// @brief The stable notification entry registered in the dependency.
            dependency_edge_t _edge;
            /// @brief The reference dependency's object identity (null for an owning dependency).
            void* _reference_owner = nullptr;
            /// @brief The reference dependency's property identity.
            const void* _reference_property = nullptr;
            /// @brief The reference dependency's connection (disconnects when the cell dies).
            std::shared_ptr<reference_connection_t> _connection;
        };

        // ---------------------------------------------------------------------
        // The evaluation context
        // ---------------------------------------------------------------------

        /// @brief Collects the dependencies read during one evaluation.
        ///
        /// One instance lives on the stack while a binding closure runs; the thread-local
        /// "current context" (see current_context()) lets property_t::value() find it without passing
        /// it around. The context collects pending_dependency_t descriptors (not live nodes): the
        /// dependant turns them into subscriptions once the closure has finished, and a dependency
        /// destroyed *during* the evaluation is forgotten before that happens.
        /// @note Library-internal: a consumer reports an error through set_binding_error(), which
        ///       reaches this object through the thread-local slot; the class is not exported.
        class binding_context_t {
        public:
            binding_context_t() noexcept = default;
            binding_context_t(const binding_context_t&) = delete;
            binding_context_t& operator=(const binding_context_t&) = delete;

            /// @brief Record a dependency; duplicates are ignored.
            /// @param property The property that was read.
            /// @note Defined in src/property_t.cpp (it needs the property's identity).
            /// @note A read of the evaluating core itself is flagged as a self-dependency: the
            ///       dependant reports kind_enum::BINDING_LOOP when the finished read set is installed.
            void record(const property_t& property);

            /// @brief Report an error for the binding being evaluated (Qt's setError).
            /// @param error The error to record.
            /// @note The error is stored in the binding state that started this evaluation, so the
            ///       closure sees the same channel Qt's currentlyEvaluatingBinding() offers. When no
            ///       target was installed (a context used on its own) the error is kept in the
            ///       context itself.
            /// @note Defined in src/property_t.cpp.
            void set_error(property_binding_t_error_t error);

            /// @brief Point set_error() at the binding state that is being evaluated.
            /// @param target The error slot of that state (null to keep the local one).
            /// @note noexcept.
            /// @note Defined in src/property_t.cpp.
            void set_error_target(property_binding_t_error_t* target) noexcept;

            /// @brief Install the core that this evaluation belongs to (internal).
            /// @param target The evaluating core (never null while a closure runs).
            /// @param dependant The same core as a weak handle (used by reference subscriptions).
            /// @note noexcept.
            /// @note Defined in src/property_t.cpp.
            void set_target(core_t* target, std::weak_ptr<core_t> dependant) noexcept;

            /// @brief Forget a dependency that was destroyed during this evaluation.
            /// @param dependency The destroyed dependency's core.
            /// @note noexcept.
            /// @note Defined in src/property_t.cpp.
            void forget(const core_t* dependency) noexcept;

            /// @brief Forget a reference dependency that was destroyed during this evaluation.
            /// @param owner The destroyed object.
            /// @param property The property identity.
            /// @note noexcept.
            /// @note Defined in src/property_t.cpp.
            void forget(void* owner, const void* property) noexcept;

            /// @brief The dependencies recorded so far, in first-read order.
            /// @note noexcept.
            /// @note Defined in src/property_t.cpp.
            const std::vector<pending_dependency_t>& pending() const noexcept;

        private:
            /// @brief The dependencies, in first-read order.
            std::vector<pending_dependency_t> _pending;
            /// @brief Where set_error() writes (null: into _error).
            property_binding_t_error_t* _error_target = nullptr;
            /// @brief The error of a context that has no target.
            property_binding_t_error_t _error;
            /// @brief The core being evaluated (null outside an evaluation).
            core_t* _target = nullptr;
            /// @brief The core being evaluated, as a weak handle.
            std::weak_ptr<core_t> _dependant;
        };

        /// @brief The thread-local "current context" slot.
        /// @return A reference to the slot.
        /// @note The slot lives in detail::thread_data_t so the shared-library build has one context per
        ///       thread across modules (a function-local thread_local would be emitted per module).
        binding_context_t*& current_context_slot() noexcept;

        /// @brief The context of the evaluation in progress on this thread.
        /// @return The active context, or nullptr when no binding is being evaluated.
        /// @note noexcept.
        binding_context_t* current_context() noexcept;

        /// @brief Installs a binding_context_t as "the current one" for the enclosing scope (RAII).
        /// @note Library-internal: it is used only by the evaluation engine and is not exported.
        class context_scope_t {
        public:
            /// @brief Install @p context.
            /// @param context The context that evaluations in this scope record into.
            /// @note noexcept.
            /// @note Defined in src/property_t.cpp.
            explicit context_scope_t(binding_context_t& context) noexcept;

            /// @brief Restore the previously installed context.
            /// @note noexcept.
            /// @note Defined in src/property_t.cpp.
            ~context_scope_t() noexcept;

            context_scope_t(const context_scope_t&) = delete;
            context_scope_t& operator=(const context_scope_t&) = delete;

        private:
            /// @brief The context that was current before this scope.
            binding_context_t* _previous;
        };

        // ---------------------------------------------------------------------
        // The core
        // ---------------------------------------------------------------------

        /// @brief The single mutable state of one variable (owning) or one property view (reference).
        ///
        /// Owning backend: @c value holds the current value and @c binding (empty = none) the
        /// installed closure. Reference backend: @c is_reference is true and the view's object,
        /// property, notifier signature and connections live in @c reference (defined in
        /// src/property_t.cpp).
        ///
        /// The binding's location, error channel and stickiness live in @c state, which every
        /// property_binding_t handle of the binding shares: that sharing is what makes set_sticky() and
        /// error() through a handle refer to the installed binding.
        ///
        /// @note The core is always owned by a std::shared_ptr (property_t holds one), which is what
        ///       lets a recorded dependency keep a weak handle to its dependant.
        struct core_t: std::enable_shared_from_this<core_t> {
            core_t() = default;
            core_t(const core_t&) = delete;
            core_t& operator=(const core_t&) = delete;
            ~core_t();

            /// @brief The installed binding closure (empty = none).
            std::function<any_t()> binding;

            /// @brief The state shared with every property_binding_t handle of the installed binding.
            std::shared_ptr<binding_state_t> state;

            /// @brief THE notifier: dependency edges of our dependants plus our change handlers.
            std::vector<subscription_node_t*> subscribers;

            /// @brief The dependencies of the currently installed binding (what it read last time).
            std::vector<property_subscription_t> dependencies;

            /// @brief Ownership of the registered handler nodes (their entries live in subscribers).
            std::vector<std::shared_ptr<handler_node_t>> handlers;

            /// @brief Whether the cached value is stale.
            bool dirty = false;
            /// @brief Whether an evaluation is running (read re-entrancy guard).
            bool evaluating = false;
            /// @brief Whether the change handlers are being called (no-re-entry guard).
            bool notifying = false;
            /// @brief Whether a handler notification is queued in the running pass.
            bool handler_pending = false;

            /// @brief The owning value (owning backend) or the last read (reference backend).
            any_t value;

            /// @brief Whether this core is a reference view over an object property.
            bool is_reference = false;
            /// @brief The reference view's state (null for an owning core; defined in src/property_t.cpp).
            std::shared_ptr<reference_state_t> reference;
            /// @brief The reference view's object identity (null for an owning core).
            void* reference_owner = nullptr;
            /// @brief The reference view's property identity (null for an owning core).
            const void* reference_property = nullptr;
            /// @brief The reference view's resolved value type.
            meta_type_t reference_type;
        };

        // ---------------------------------------------------------------------
        // The engine (free functions over one core)
        // ---------------------------------------------------------------------

        /// @brief Evaluate @p fn with @p context installed, turning an exception into a diagnostic.
        /// @param target The core being evaluated.
        /// @param fn The closure.
        /// @param context The evaluation context (its target is installed here).
        /// @return The value, or std::nullopt when the closure threw.
        std::optional<any_t> evaluate(core_t& target, const std::function<any_t()>& fn,
                                      binding_context_t& context);

        /// @brief Re-evaluate the installed binding (the scheduler's free recompute hook).
        /// @param target The core to recompute.
        void recompute(core_t& target);

        /// @brief Forget the queued scheduler entry of @p target and report it clean.
        /// @param target The core.
        /// @note noexcept.
        void discard(core_t& target) noexcept;

        /// @brief Remove the installed binding (if any) and release its dependencies; the value stays.
        /// @param target The core.
        void clear_binding(core_t& target);

        /// @brief Detach every recorded dependency of @p target.
        /// @param target The core.
        /// @note noexcept.
        void detach_dependencies(core_t& target) noexcept;

        /// @brief Forget the dependency edge @p edge of @p dependant (it is dying).
        /// @param dependant The dependant core.
        /// @param edge The dead edge (already detached from the dying dependency).
        /// @note noexcept. The edge stays in the read set as a dead hole until the next evaluation;
        ///       the dependant keeps the value it last computed and drops its queued recompute.
        void forget_dependency(core_t& dependant, dependency_edge_t* edge) noexcept;

        /// @brief Install the subscriptions recorded by @p context on @p target.
        /// @param target The dependant core.
        /// @param context The finished evaluation.
        void take_dependencies(core_t& target, binding_context_t& context);

        /// @brief Install @p fn (with @p state) into @p target, evaluating it first.
        /// @param target The core.
        /// @param state The binding's shared state.
        /// @param fn The erased closure.
        /// @param replaced_state When not null, receives the replaced binding's state.
        /// @param replaced_fn When not null, receives the replaced binding's closure.
        /// @param declared_type When not null and valid, the value type the binding declares: a first
        ///        result of another type (or a property of another type) refuses the install.
        /// @return true when a binding is installed afterwards.
        /// @note A refused install (a binding set while one is evaluating) reports a loop and leaves
        ///       the previous binding untouched; a failed evaluation drops the binding; a declared-type
        ///       mismatch leaves the previous binding untouched too.
        /// @note A state with pending_notify set notifies once after the install even when the value
        ///       did not change (the flag is consumed).
        bool install_binding(core_t& target, std::shared_ptr<binding_state_t> state,
                             std::function<any_t()> fn,
                             std::shared_ptr<binding_state_t>* replaced_state = nullptr,
                             std::function<any_t()>* replaced_fn = nullptr,
                             const meta_type_t* declared_type = nullptr);

        /// @brief Report a refused operation on a read-only handle (one Warning per call).
        /// @param operation The operation's name ("set_value", "set_binding", ...).
        /// @note Not noexcept: the log handler may throw.
        void warn_read_only(const char* operation);

        /// @brief Verify a declared binding value type against the property's own value type.
        /// @param target The core the binding would be installed on.
        /// @param state The binding's state; a mismatch records kind_enum::UNKNOWN + message there.
        /// @param declared_type The type the binding declares.
        /// @return true when the install may proceed (a valid matching type, or an invalid handle on
        ///         either side, which is "no check possible").
        /// @note This is Qt's pre-evaluation check (QUntypedBindable::setBinding's metatype compare);
        ///       a mismatch logs one Warning.
        bool check_binding_type(const core_t& target, binding_state_t& state, const meta_type_t& declared_type);

        /// @brief Mark @p target dirty when one of its dependencies changed.
        /// @param target The dependant.
        /// @note noexcept.
        void mark_dependency_changed(core_t& target) noexcept;

        /// @brief Propagate a change: mark every dependant and call every handler (group aware).
        /// @param target The core that changed.
        void notify_change(core_t& target);

        /// @brief Queue the deferred handler notification of @p target (one per pass).
        /// @param target The core whose handlers are owed a notification.
        void request_handler_notification(core_t& target);

        /// @brief Drain the queued handler notifications (the end-of-pass flush).
        /// @param target The core whose debt is paid.
        /// @note noexcept.
        void notify_handler_debt(core_t& target) noexcept;

        /// @brief Register @p node with @p target (idempotent).
        /// @param target The core.
        /// @param node The handler node.
        void register_handler(core_t& target, const std::shared_ptr<handler_node_t>& node);

        /// @brief The drain callback: recompute (if still stale) and call the handlers.
        /// @param target The core.
        /// @note noexcept: it runs from end_notification().
        void fire_handler_notification(void* target) noexcept;

        /// @brief The deferred-change callback (binding_scheduler_t::pending_change_t::fire).
        /// @param target The core whose change was deferred.
        void fire_deferred_change(void* target);

        /// @brief Make sure a deferred notification requested just now is drained.
        /// @note Only the reference backend calls this: an owning core is allowed to stay dirty until
        ///       it is read or the round-end flush runs.
        void close_notification_pass();

        /// @brief The type-erased detach hook handed to handler_node_t::bind().
        /// @param owner The core the node was registered with.
        /// @param node The node to remove.
        /// @note noexcept.
        void detach_handler(void* owner, handler_node_t* node) noexcept;
    }

    // -------------------------------------------------------------------------
    // property_t
    // -------------------------------------------------------------------------

    /// @brief A type-erased handle for one variable or one property view (24 bytes).
    ///
    /// One shared_ptr to a detail::core_t plus the handle's read-only flag. Copies alias the same core
    /// (they are handles, not values). The owning backend is created from an initial any_t; the
    /// reference backend is created by from() and views a notifiable object property.
    ///
    /// The read-only flag (as_read_only(), Qt's read-only QBindable) lives on the HANDLE, not on the
    /// core: a read-only copy refuses writes and binding installs, yet still reports the value and the
    /// binding a writable handle installed on the same variable.
    ///
    /// @note Every operation is legal on an invalid (default-constructed) handle: is_valid() is
    ///       false, value() answers an empty any_t, set_value() answers false and the rest is a
    ///       no-op.
    class HWSHQTB_EXPORT property_t {
    public:
        /// @brief Construct an invalid handle.
        /// @note noexcept.
        property_t() noexcept = default;

        /// @brief Construct an owning variable with an initial value.
        /// @param initial The initial value (moved into the core).
        /// @note Not noexcept: allocating the core and copying @p initial may throw.
        explicit property_t(any_t initial);

        /// @brief Create a reference view over a notifiable object property.
        /// @param owner The object that owns the property.
        /// @param property The property to view.
        /// @return The view.
        /// @throws bad_property_t When the property is invalid or not notifiable.
        /// @note Recomputation rule of the reference backend (differs from the owning one): a binding
        ///       installed on the view is recomputed EAGERLY at the end of the notification pass that
        ///       invalidated it, then written back through the property's setter (whose notifier
        ///       publishes the new value). It has to be eager: the value lives in the object and
        ///       callers read the object, not the handle, so there is no read of the view to trigger
        ///       a lazy evaluation. See detail::mark_dependency_changed().
        /// @note Defined in src/property_t.cpp (it needs the complete meta_property_t/object_t types).
        static property_t from(object_t& owner, const meta_property_t& property);

        /// @brief Whether the handle refers to a core at all.
        /// @note noexcept.
        bool is_valid() const noexcept;

        /// @brief A copy of this handle that refuses writes and binding installs.
        /// @return A handle over the same core with the read-only flag set.
        /// @note The flag is per HANDLE, not per variable (Qt's read-only QBindable): value(),
        ///       has_binding() and the change handlers keep working and report what a writable handle
        ///       installed, while set_value() / set_value_bypassing_bindings() / set_binding() /
        ///       clear_binding() / notify() are refused with one Warning each.
        /// @note noexcept: it only copies the handle and sets the flag.
        property_t as_read_only() const noexcept;

        /// @brief Whether this handle refuses writes and binding installs (Qt's isReadOnly()).
        /// @note noexcept.
        bool is_read_only() const noexcept;

        /// @brief Whether the handle owns a value (owning backend, as opposed to a reference view).
        /// @note noexcept.
        bool is_owning() const noexcept;

        /// @brief The value type (an invalid meta_type_t for an invalid handle).
        /// @note noexcept.
        meta_type_t type() const noexcept;

        /// @brief Read the value, evaluating a dirty binding first (and recording a dependency when
        ///        a binding_context_t is active).
        /// @return The current value, by value.
        /// @note A read of a dirty, bound property evaluates the binding immediately and consumes its
        ///       queued scheduler entry, so the end-of-round flush does not evaluate it twice.
        /// @note A read that re-enters the property currently being evaluated logs one Critical and
        ///       records kind_enum::BINDING_LOOP instead of recursing.
        any_t value() const;

        /// @brief Assign a concrete value, breaking any non-sticky binding first.
        /// @param value The new value.
        /// @return true when the value changed (and dependants were notified); false for an invalid or
        ///         read-only handle.
        /// @note A write always removes the binding first, even when the value turns out unchanged
        ///       (Qt's setValue() does the same). A sticky binding survives the write.
        /// @note A type without a usable operator== always counts as changed (the library's
        ///       type-erased equality gate).
        /// @note A read-only handle refuses the write with one Warning and changes nothing.
        bool set_value(const any_t& value);

        /// @brief Assign without notifying and without breaking the binding (owning backend only).
        /// @param value The new value.
        /// @return true when the value was assigned; false for a reference view, an invalid handle or a
        ///         read-only handle.
        /// @note Qt's setValueBypassingBindings(): the assignment is unconditional, nothing is
        ///       notified and the installed binding stays installed.
        bool set_value_bypassing_bindings(const any_t& value);

        /// @brief Notify dependants and change handlers now (Qt's QPropertyData::notify()).
        /// @return true when the notification was started; false for an invalid or read-only handle.
        /// @note UNCONDITIONAL: it fires even when the value did not change, and it does not re-evaluate
        ///       the installed binding.
        /// @note Inside an open property update group the notification is deferred.
        bool notify();

        /// @brief The value source this binding reads changed outside the property system:
        ///        mark it dirty, queue it, and invalidate dependants.
        /// @note A no-op on an invalid handle and on a property that has no installed binding;
        ///       noexcept.
        /// @note This is the operation a binding over a plain member needs: the library cannot see
        ///       that member, so its owner says "it changed". Unlike
        ///       binding_scheduler_t::mark_dirty(), which only appends to the next round's queue, this
        ///       sets the dirty flag, so a read BEFORE the next flush already sees the new value.
        /// @note Defined in src/property_t.cpp.
        void invalidate() noexcept;

        /// @brief Install a binding and evaluate it immediately.
        /// @param fn The evaluation closure; an empty closure clears the binding.
        /// @param location Where the binding was installed (defaulted to the call site). The location
        ///        travels with the binding and is reported by property_binding_t::source_location().
        /// @return true when a binding is installed afterwards.
        /// @note A refused install (a binding set while one is evaluating) reports a loop; a throwing
        ///       closure logs Critical, keeps the old value and leaves no binding installed.
        /// @note A read-only handle refuses the install with one Warning and changes nothing.
        bool set_binding(std::function<any_t()> fn,
                         std::source_location location = std::source_location::current());

        /// @brief Install a binding that declares the value type it produces.
        /// @param fn The evaluation closure; an empty closure clears the binding.
        /// @param declared_type The value type the binding declares (Qt's QUntypedPropertyBinding's
        ///        valueMetaType()).
        /// @return true when a binding is installed afterwards.
        /// @note Refused — nothing assigned, no binding installed, a previously installed binding left
        ///       in place — when @p declared_type is not the property's value type (checked before the
        ///       evaluation, like Qt's QUntypedBindable::setBinding()) or when the first evaluation
        ///       returns a value of another type. The refusal records kind_enum::UNKNOWN with a message on
        ///       the binding's error channel and logs it as one Warning.
        /// @note This is Qt's metatype check, opt-in: the untyped set_binding(fn) overload keeps the
        ///       dynamically typed behaviour.
        bool set_binding(std::function<any_t()> fn,
                         const meta_type_t& declared_type);

        /// @brief Install a binding handle that was taken or created earlier (Qt's
        ///        QUntypedBindable::setBinding(const QUntypedPropertyBinding&)).
        /// @param handle The handle to install. A NULL handle clears the installed binding instead
        ///        (Qt's setBinding(QUntypedPropertyBinding())).
        /// @param location The location to record when @p handle carries none of its own; a handle
        ///        keeps the location it was created with (as Qt does).
        /// @return true when a binding is installed afterwards; false when the handle was refused
        ///         (read-only handle, a refused install or a null handle, which installs nothing).
        /// @note The handle's shared state is SHARED, not moved: the handle stays valid and can be
        ///       installed again later.
        /// @note Declared here and defined after property_binding_t is complete.
        bool set_binding(const property_binding_t& handle,
                         std::source_location location = std::source_location::current());

        /// @brief Detach the installed binding and hand it back (Qt's QUntypedBindable::takeBinding()).
        /// @return A handle for the binding that was installed; a null handle when there was none.
        /// @note The value freezes at its current value. The property leaves every dependency's
        ///       subscriber list, so a later change of a dependency neither marks nor re-evaluates the
        ///       detached binding, and the returned handle keeps the closure alive for a later
        ///       set_binding(). An invalid handle has nothing to take and answers a null handle.
        /// @note Declared here and defined after property_binding_t is complete.
        property_binding_t take_binding();

        /// @brief A handle for the installed binding (Qt's QUntypedBindable::binding()).
        /// @return A handle sharing the installed binding's state; a null handle when none is
        ///         installed (and for an invalid handle).
        /// @note const, like Qt's const QBindable::binding(): a read-only handle still reports the
        ///       binding a writable handle installed on the same variable.
        property_binding_t binding() const;

        /// @brief Whether a binding is installed.
        /// @note noexcept.
        bool has_binding() const noexcept;

        /// @brief Remove the binding (if any) and release its dependencies; the value is kept.
        /// @note A read-only handle refuses with one Warning and leaves the binding installed.
        void clear_binding();

        /// @brief Register a change handler; it is not called immediately (Qt's addNotifier() /
        ///        QProperty::onValueChanged()).
        /// @param callback Invocable with no arguments.
        /// @return A move-only handle; destroying it unregisters @p callback.
        /// @note const, like Qt's QBindable::addNotifier(): the handle is read-only for the caller, the
        ///       variable is not (the registration lives in the core the handle shares).
        /// @note One name only: Qt's addNotifier() is the same registration as onValueChanged() (they
        ///       differ in Qt only by the name of the Qt class they belong to), so there is no separate
        ///       add_notifier().
        property_change_handler_t on_value_changed(std::function<void()> callback) const;

        /// @brief Register a change handler and call it now (Qt's QProperty::subscribe()).
        /// @param callback Invocable with no arguments.
        /// @return A move-only handle; destroying it unregisters @p callback.
        /// @note Qt calls the functor first and registers it afterwards: a throwing callback
        ///       registers nothing.
        /// @note const, like Qt's QBindable::subscribe().
        property_change_handler_t subscribe(std::function<void()> callback) const;

        /// @brief The core behind this handle (internal: the library machinery and the scheduler).
        /// @return The core; null when the handle is invalid.
        /// @note noexcept.
        /// @note Defined in src/property_t.cpp.
        const std::shared_ptr<detail::core_t>& core_handle() const noexcept;

    private:
        // property_change_handler_t::set_source() registers its node with a property.
        friend class property_change_handler_t;
        // detail::binding_context_t::record() asks every read for its dependency identity.
        friend class detail::binding_context_t;

        /// @brief The identity of this property (internal: dependency de-duplication).
        /// @return The owning core, or the (object, property) pair of a reference view.
        /// @note noexcept.
        detail::dependency_identity_t identity() const noexcept;

        /// @brief Read the value without copying it (internal: value() reads through this).
        /// @return The cached value; for a reference view the cache is refreshed first.
        /// @note This is the body of value(); it records dependencies and evaluates like value().
        const any_t& checked_value() const;

        /// @brief The shared core (null for an invalid handle).
        std::shared_ptr<detail::core_t> _core;

        /// @brief Whether the handle refuses writes and binding installs (Qt's read-only QBindable).
        bool _read_only = false;
    };

    // -------------------------------------------------------------------------
    // property_binding_t
    // -------------------------------------------------------------------------

    /// @brief A move-only handle for a property binding (Qt's QUntypedPropertyBinding).
    ///
    /// A handle is returned by property_t::take_binding() and property_t::binding() and accepted by
    /// property_t::set_binding(). A default-constructed (or moved-from) handle is null: it holds no
    /// binding at all, which is legal everywhere (Qt's QPropertyBinding::isNull()).
    ///
    /// The value type is erased together with the closure: the handle stores
    /// `std::function<any_t()>`, and a caller that needs T reads it back from
    /// the any_t (property_t::value().get<T>()). A typed closure is accepted by the constructor and
    /// its result is stored in one any_t, so property_binding_t(f) keeps the same shape as Qt's
    /// QPropertyBinding<T>(f) without naming T in the handle.
    ///
    /// @note The handle stores a copy of the erased closure plus a shared_ptr to the binding's shared
    ///       state (location, error channel, stickiness). The state is shared with the core while the
    ///       binding is installed, so set_sticky()/error() through a handle change the installed
    ///       binding; take_binding() moves the closure out of the core and the handle keeps it alive
    ///       without being installed anywhere (a detached binding is never evaluated again).
    /// @note Move-only, like property_change_handler_t: a handle has exactly one owner. Installing it
    ///       with set_binding() shares the state instead of consuming the handle, so a handle stays
    ///       valid across an install and can be installed again later.
    class HWSHQTB_EXPORT property_binding_t {
    public:
        /// @brief Construct a null handle.
        /// @note noexcept.
        property_binding_t() noexcept = default;

        /// @brief Construct a handle for @p fn (Qt's QPropertyBinding<T>(Functor) constructor).
        /// @tparam F Invocable with no arguments; a result that is not an any_t is
        ///         stored in one by its value constructor, which produces the type entry from the
        ///         result type itself (an lvalue result is copied, an rvalue one is moved; no
        ///         declaration is involved).
        /// @param fn The evaluation closure.
        /// @param location Where the binding was created (Qt's QT_PROPERTY_DEFAULT_BINDING_LOCATION).
        template <typename F> requires std::is_invocable_v<F&>
        explicit property_binding_t(F&& fn,
                                  std::source_location location = std::source_location::current()) {
            using result_t = std::invoke_result_t<F&>;
            static_assert(!std::is_void_v<result_t>,
                          "a binding closure must produce a value");
            _state = std::make_shared<detail::binding_state_t>();
            if constexpr (std::is_same_v<std::remove_cvref_t<result_t>, any_t>) {
                _fn = std::function<any_t()>(std::forward<F>(fn));
            }
            else {
                _fn = [callable = std::function<result_t()>(std::forward<F>(fn))]() -> any_t {
                    return any_t(callable());
                };
            }
            _state->location = location;
        }

        /// @brief Move a handle; @p other becomes null.
        property_binding_t(property_binding_t&& other) noexcept = default;

        /// @brief Move-assign; the previous state (if any) is released.
        property_binding_t& operator=(property_binding_t&& other) noexcept = default;

        property_binding_t(const property_binding_t&) = delete;
        property_binding_t& operator=(const property_binding_t&) = delete;

        /// @brief Whether this handle refers to no binding at all (Qt's isNull()).
        /// @note noexcept.
        bool is_null() const noexcept;

        /// @brief Whether this handle refers to a binding (Qt's QUntypedPropertyBinding's bool form).
        /// @note noexcept.
        explicit operator bool() const noexcept;

        /// @brief Make a write keep this binding installed (Qt's QPropertyBindingPrivate::setSticky()).
        /// @param sticky The new stickiness (defaulted to true, like Qt's setSticky()); ignored by a
        ///        null handle.
        /// @note noexcept.
        void set_sticky(bool sticky = true) noexcept;

        /// @brief Whether a write keeps this binding installed.
        /// @return false for a null handle.
        /// @note noexcept.
        bool is_sticky() const noexcept;

        /// @brief Request one notification on the binding's next install, even when the value it
        ///        produces equals the current one (Qt's QPropertyBindingPrivate::scheduleNotify()).
        /// @note noexcept; ignored by a null handle. The request is consumed by the install that
        ///       honors it (an install that is refused keeps it for the next attempt).
        void schedule_notify() noexcept;

        /// @brief Whether schedule_notify() is still waiting for its install.
        /// @return false for a null handle and after the install consumed the request.
        /// @note noexcept.
        bool notify_scheduled() const noexcept;

        /// @brief The error of the binding's last evaluation (Qt's QPropertyBinding::error()).
        /// @return kind_enum::NONE for a healthy binding and for a null handle.
        /// @note A closure reports an error through set_binding_error(); the library
        ///       reports a dependency cycle as kind_enum::BINDING_LOOP.
        property_binding_t_error_t error() const;

        /// @brief Where the binding was installed (Qt's sourceLocation()).
        /// @return The recorded location; a default-constructed one for a null handle.
        /// @note noexcept.
        std::source_location source_location() const noexcept;

    private:
        // property_t installs, takes and reports the binding.
        friend class property_t;

        /// @brief The shared state (null for a null handle).
        std::shared_ptr<detail::binding_state_t> _state;
        /// @brief The erased closure (empty for a null handle).
        std::function<any_t()> _fn;
    };

    // -------------------------------------------------------------------------
    // property_change_handler_t
    // -------------------------------------------------------------------------

    /// @brief A move-only RAII handle for a property change handler.
    ///
    /// While it is alive the callback stays registered and is invoked whenever the property's value
    /// actually changes. Destroying the handle (or move-assigning an empty one to it) unregisters the
    /// callback; copying is deleted, so a registration has exactly one owner. set_source()
    /// re-targets the same callback to another property (Qt's QPropertyChangeHandler::setSource).
    ///
    /// @note One type covers Qt's QPropertyChangeHandler<Functor> and QPropertyNotifier: Qt's two
    ///       names differ only in the return type of the API that created them.
    /// @note A handle that outlives its property becomes inert instead of dangling: the core's
    ///       destructor orphans the registration, so is_bound() turns false, the handle's destructor
    ///       does not touch the destroyed core, and set_source() can still register the callback on a
    ///       new property.
    class HWSHQTB_EXPORT property_change_handler_t {
    public:
        /// @brief Construct an empty handle (nothing is registered and nothing is ever called).
        property_change_handler_t() noexcept = default;

        /// @brief Construct a handle for @p callback (not registered until the property registers it).
        /// @param callback Invocable with no arguments; stored type-erased, so the node is allocated
        ///        in the library and `detail::handler_node_t` never enters a consumer's ABI.
        explicit property_change_handler_t(std::function<void()> callback);

        /// @brief Move a handle; @p other becomes empty.
        property_change_handler_t(property_change_handler_t&& other) noexcept = default;

        /// @brief Move-assign: the current registration (if any) is removed first.
        /// @param other The handle taking over; becomes empty.
        /// @note Qt's QPropertyNotifier::operator=() has the same detach-then-take semantics.
        property_change_handler_t& operator=(property_change_handler_t&& other) noexcept;

        /// @brief Destroy the handle: unregister the callback.
        ~property_change_handler_t();

        property_change_handler_t(const property_change_handler_t&) = delete;
        property_change_handler_t& operator=(const property_change_handler_t&) = delete;

        /// @brief Re-target the same callback to @p property (Qt's QPropertyChangeHandler::setSource).
        /// @param property The property to observe from now on (any handle over the variable, owning
        ///        or reference, read-only or not: the registration lives in the shared core).
        /// @note The callback leaves the previous property first, so it does not fire for two
        ///       properties. A default-constructed (callback-less) handle is left untouched.
        /// @note Not noexcept: registering allocates.
        void set_source(property_t& property);

        /// @brief Whether the callback is currently registered with a live property.
        /// @return false for an empty handle, a detached one and one whose property was destroyed.
        /// @note noexcept.
        bool is_bound() const noexcept;

    private:
        // property_t builds a handle and registers its node; no other code may name _node.
        friend class property_t;

        /// @brief The registration, shared with the core while it is live.
        std::shared_ptr<detail::handler_node_t> _node;
    };

    // -------------------------------------------------------------------------
    // is_binding_evaluating
    // -------------------------------------------------------------------------

    /// @brief Whether a binding closure is being evaluated on the calling thread (Qt's
    ///        QtPrivate::isAnyBindingEvaluating()).
    /// @return true while the evaluation started by property_t::value() / set_binding() is running
    ///         on this thread; false everywhere else, including inside a change handler (the handlers
    ///         run after the evaluation has finished).
    /// @note noexcept, no allocation.
    bool is_binding_evaluating() noexcept;

    // -------------------------------------------------------------------------
    // set_binding_error
    // -------------------------------------------------------------------------

    /// @brief Report an error on the binding that is being evaluated right now.
    /// @param error The error to record.
    /// @note Does nothing when no binding is being evaluated (matching Qt, where the call
    ///       simply has no target outside an evaluation).
    /// @note Still leaves the binding installed and keeps the value the closure returned.
    /// @note Defined in src/property_t.cpp.
    HWSHQTB_EXPORT void set_binding_error(const property_binding_t_error_t& error);

    // -------------------------------------------------------------------------
    // flush_bindings
    // -------------------------------------------------------------------------

    /// @brief Run the pending binding work now: recompute dirty bindings and deliver the
    ///        notifications they queued. The event dispatcher calls this at the end of every
    ///        round; call it yourself when you drive bindings without an event loop.
    /// @note Defined in src/property_t.cpp.
    HWSHQTB_EXPORT void flush_bindings();

    // -------------------------------------------------------------------------
    // binding_scheduler_t
    // -------------------------------------------------------------------------

    /// @brief Per-thread invalidation queue: mark dirty now, recompute once per round until stable.
    ///
    /// A dependency change calls mark_dirty(); the event loop calls flush() at the end of every
    /// process_events() round. Entries are de-duplicated, so N changes to the same dependency in one
    /// round cost one recompute. A recompute that marks more bindings extends the same round; more
    /// than max_recomputes_per_flush recomputes therefore mean the dependency graph has a cycle.
    ///
    /// @note Library-internal: a consumer drains the queue through the public flush_bindings() free
    ///       function; the class itself is not exported.
    class binding_scheduler_t {
    public:
        /// @brief Recompute budget of one flush(); exceeding it is diagnosed as a cycle.
        /// @note The end-of-notification-pass drain (drain_pending_handlers) applies the same limit
        ///       per core: a mutual binding that would otherwise re-invalidate itself forever is
        ///       reported as a binding_loop instead of spinning.
        static constexpr std::size_t max_recomputes_per_flush = 100;

        binding_scheduler_t() = default;
        binding_scheduler_t(const binding_scheduler_t&) = delete;
        binding_scheduler_t& operator=(const binding_scheduler_t&) = delete;

        /// @brief Queue a core for recomputation; null and already-queued entries are ignored.
        /// @param binding The core to recompute; nullptr is ignored.
        /// @note A library caller reaches the core with property_t::core_handle().get(); the
        ///       scheduler speaks cores only.
        /// @note Defined in src/property_t.cpp.
        void mark_dirty(detail::core_t* binding);

        /// @brief Recompute the queued bindings, including the ones queued while flushing.
        ///
        /// A re-entrant call (a recompute that ends up calling flush() again) returns immediately:
        /// the outer call owns the round. When the recompute budget is exceeded the queue is
        /// cleared and a Critical diagnostic is logged through log_connection.
        /// @note Defined in src/property_t.cpp.
        void flush();

        /// @brief Recompute @p b now when it is queued; @return whether it was queued.
        /// @param b The core to recompute; may be null.
        /// @note The queued entry is consumed, so the caller's read does not cause a second
        ///       recompute at the end of the round. Bindings that @p b marks dirty **during** this
        ///       recompute stay queued and are still recomputed by the round (batching preserved).
        /// @note Returns false when @p b is not queued; the caller then evaluates it directly.
        /// @note Not noexcept: a recompute allocates and may log.
        /// @note Defined in src/property_t.cpp.
        bool flush_one(detail::core_t* b);

        /// @brief Forget every queued reference to @p b without recomputing it.
        /// @param b The core to forget; may be null.
        /// @return true when at least one entry was dropped.
        /// @note Used when a core is destroyed or loses its dependency state: the round-end flush
        ///       must not call into it afterwards.
        /// @note noexcept: only erases existing entries.
        /// @note Defined in src/property_t.cpp.
        bool discard(detail::core_t* b) noexcept;

        /// @brief Forget a queued entry on the calling thread without creating a scheduler.
        /// @param b The core to forget; may be null.
        /// @return true when at least one entry was dropped.
        /// @note noexcept; safe to call from a destructor (it does not allocate a scheduler).
        /// @note Defined in src/property_t.cpp.
        static bool discard_if_queued(detail::core_t* b) noexcept;

        /// @brief Whether a flush() is running on the calling thread.
        /// @return true while a round owns the recomputation.
        /// @note noexcept; does not create a scheduler. A read uses it to keep the round's batching:
        ///       inside a round the scheduler is already recomputing every dirty entry (and
        ///       re-queues the readers of one that changed), so a read must not start a second,
        ///       nested evaluation of the same graph.
        /// @note Defined in src/property_t.cpp.
        static bool flushing_on_this_thread() noexcept;

        /// @brief Number of bindings waiting for the next flush().
        /// @return The queue length.
        /// @note noexcept. While a flush is running this counts the entries queued by recomputes
        ///       (those are already part of the running round).
        /// @note Defined in src/property_t.cpp.
        std::size_t pending_count() const noexcept;

        /// @brief One deferred property change.
        /// @note While a property update group is open a write assigns its value but records the
        ///       change here instead of propagating it: no dependant is marked dirty and no change
        ///       handler runs (Qt's QPropertyProxyBindingData). end_update_group() fires them in
        ///       write order and then flushes, so every dependant is recomputed after every write of
        ///       the group (Qt's endPropertyUpdateGroup).
        struct pending_change_t {
            /// @brief The core that was written.
            void* target = nullptr;
            /// @brief Propagates the change and calls its handlers.
            void (*fire)(void*) = nullptr;
        };

        /// @brief Whether a property update group is open on this thread.
        /// @note noexcept.
        /// @note Defined in src/property_t.cpp.
        bool in_update_group() const noexcept;

        /// @brief Open a property update group (Qt's Qt::beginPropertyUpdateGroup()).
        /// @note noexcept. Nested groups only count depth; the outermost end_update_group() drains.
        /// @note Defined in src/property_t.cpp.
        void begin_update_group() noexcept;

        /// @brief Close a property update group; the outermost close drains and flushes.
        /// @note noexcept: a throwing change handler is reported like a failed evaluation, so the
        ///       RAII wrapper's destructor cannot call std::terminate.
        /// @note The deferred changes are fired one at a time: a handler may destroy another property
        ///       (whose destructor removes its entry) or open another group.
        /// @note Defined in src/property_t.cpp.
        void end_update_group() noexcept;

        /// @brief Record a change that must be notified when the open group ends.
        /// @param target The written core.
        /// @param fire The callback that propagates the change and calls its handlers.
        /// @note Duplicates are ignored: a property written twice inside a group notifies once.
        /// @note Not noexcept: the queue allocates.
        /// @note Defined in src/property_t.cpp.
        void mark_change_pending(void* target, void (*fire)(void*));

        /// @brief Forget the deferred change of @p target.
        /// @param target The core being destroyed.
        /// @return true when an entry was dropped.
        /// @note noexcept; used by ~core() so the queue holds no pointer to freed storage.
        /// @note Defined in src/property_t.cpp.
        bool discard_change(void* target) noexcept;

        /// @brief discard_change() for the calling thread without creating a scheduler.
        /// @param target The core being destroyed.
        /// @return true when an entry was dropped.
        /// @note noexcept; safe to call from a destructor.
        /// @note Defined in src/property_t.cpp.
        static bool discard_change_if_queued(void* target) noexcept;

        /// @brief One deferred change-handler notification.
        /// @note A handled binding queues one of these when a dependency invalidates it during a
        ///       notification pass; end_notification() drains the queue once the pass has finished
        ///       propagating the invalidation, so a handler runs only after every reader of the
        ///       changed dependency has been marked (a diamond must not be recomputed while one of
        ///       its arms is still marked stale).
        struct pending_handler_t {
            /// @brief The core to notify.
            void* target = nullptr;
            /// @brief Recomputes it if needed and calls its change handlers.
            void (*fire)(void*) noexcept = nullptr;
        };

        /// @brief Enter a notification pass; nested passes only count depth.
        /// @note noexcept. Every call is paired with end_notification(), also on the exception path.
        /// @note Defined in src/property_t.cpp.
        void begin_notification() noexcept;

        /// @brief Leave a notification pass; the outermost leave drains the queued handlers.
        /// @note Not noexcept: draining recomputes bindings and calls handlers. An exception that is
        ///       already unwinding suppresses the drain (those handlers run in a later pass), and so
        ///       does a running flush(): it drains when its round is complete, so a handler never
        ///       observes a peer that the round has not recomputed yet.
        /// @note Defined in src/property_t.cpp.
        void end_notification();

        /// @brief Queue one change-handler notification for the end of the running pass.
        /// @param target The core.
        /// @param fire The non-throwing callback that notifies it.
        /// @note Not noexcept: the queue allocates.
        /// @note Defined in src/property_t.cpp.
        void mark_handler_pending(void* target, void (*fire)(void*) noexcept);

        /// @brief Forget every queued handler notification for @p target.
        /// @param target The core being destroyed.
        /// @return true when at least one entry was dropped.
        /// @note noexcept; used by ~core() so the queue holds no pointer to freed storage.
        /// @note Defined in src/property_t.cpp.
        bool discard_handler(void* target) noexcept;

        /// @brief discard_handler() for the calling thread without creating a scheduler.
        /// @param target The core being destroyed.
        /// @return true when at least one entry was dropped.
        /// @note noexcept; safe to call from a destructor.
        /// @note Defined in src/property_t.cpp.
        static bool discard_handler_if_queued(void* target) noexcept;

        /// @brief The calling thread's scheduler.
        /// @return A reference to the thread's scheduler, created on first use.
        /// @note The instance is anchored in detail::thread_data_t (whose current() accessor is
        ///       exported) so the shared-library build has one queue per thread across modules (a
        ///       function-local thread_local would be emitted per module).
        /// @note Never destroyed, like the thread's dispatcher: thread_data_t must stay trivially
        ///       destructible.
        /// @note Not noexcept: the first call allocates.
        /// @note Defined in src/property_t.cpp.
        static binding_scheduler_t& current();

    private:
        /// @brief Run every queued change-handler notification.
        /// @note Entries queued by a notification are drained in the same loop (a recomputed property
        ///       that invalidates a handled dependant appends to the queue).
        /// @note An exception that is already unwinding suppresses the drain; the entries stay queued
        ///       for a later pass (a destroyed core removes its entry from the queue).
        /// @note Cycle guard: one drain recomputes each core at most max_recomputes_per_flush times;
        ///       beyond that the core is reported as a dependency cycle and its entry is dropped.
        /// @note Defined in src/property_t.cpp.
        void drain_pending_handlers();

        /// @brief One core's recompute count inside the running drain (the end-of-pass cycle guard).
        struct drain_count_t {
            /// @brief The core.
            void* target = nullptr;
            /// @brief How many times this drain already recomputed it.
            std::size_t count = 0;
        };

        /// @brief The per-core counter of @p target in the running drain (created on first use).
        /// @param target The core; must not be null.
        /// @return A reference to its counter.
        /// @note Defined in src/property_t.cpp.
        std::size_t& drain_count_of(void* target);

        /// @brief Recompute one core, turning an exception into a Critical diagnostic.
        /// @param binding The core to recompute.
        /// @note Defined in src/property_t.cpp.
        static void recompute_one(detail::core_t& binding);

        /// @brief Remove every queued reference to @p b (pending queue or running round).
        /// @param b The core to remove; may be null.
        /// @return true when at least one entry was removed.
        /// @note noexcept: it only erases. The entry currently being recomputed cannot match:
        ///       recompute() has already cleared the binding's dirty flag and @a _round_next points
        ///       past it, so removal never invalidates the running loop.
        /// @note Defined in src/property_t.cpp.
        bool remove_queued(detail::core_t* b) noexcept;

        /// @brief Bindings waiting for the next flush (or queued by a running flush).
        std::vector<detail::core_t*> _dirty;

        /// @brief Whether a flush is running (re-entrancy guard).
        bool _flushing = false;

        /// @brief The queue of the flush() that is running, if any (null while no flush runs).
        std::vector<detail::core_t*>* _round_queue = nullptr;

        /// @brief Index of the next unprocessed entry in *_round_queue.
        std::size_t _round_next = 0;

        /// @brief Change-handler notifications queued by the running notification pass.
        std::vector<pending_handler_t> _pending_handlers;

        /// @brief Per-core recompute counts of the running drain (the end-of-pass cycle guard).
        /// @note Cleared at the start of every drain; the capacity is kept, so a drain does not
        ///       allocate once the vector has grown once.
        std::vector<drain_count_t> _drain_counts;

        /// @brief Nesting depth of the notification pass (0 when none runs).
        std::size_t _notification_depth = 0;

        /// @brief Whether end_notification() is draining (suppresses a nested drain).
        bool _draining_handlers = false;

        /// @brief Property changes deferred by the open property update group, in write order.
        std::vector<pending_change_t> _pending_changes;

        /// @brief Nesting depth of the property update group (0 when none is open).
        std::size_t _update_group_depth = 0;
    };

    // -------------------------------------------------------------------------
    // property update group
    // -------------------------------------------------------------------------

    /// @brief A property update group (Qt's QScopedPropertyUpdateGroup).
    ///
    /// While an instance is alive, a write to a property assigns its value immediately but does not
    /// propagate it: dependants are not marked dirty and change handlers do not run, so a read of a
    /// dependant inside the group still returns its previous value (Qt's "still stale inside the
    /// group"). When the outermost group ends, every deferred change fires in write order and the
    /// scheduler runs one round, so each dependant recomputes once and each handler runs at most
    /// once per real change.
    ///
    /// @note Groups nest (counter-based): only the outermost destructor drains.
    /// @note Not copyable and not movable, like Qt's QScopedPropertyUpdateGroup: the destructor is
    ///       the end of the group.
    /// @note A change handler that throws is reported through log_connection by
    ///       end_property_update_group_t() instead of escaping the destructor (Qt documents that its
    ///       own destructor terminates in that case).
    class HWSHQTB_EXPORT property_update_group_t {
    public:
        /// @brief Begin the group (Qt's Qt::beginPropertyUpdateGroup()).
        /// @note noexcept. The first call on a thread allocates its scheduler, so an allocation
        ///       failure terminates.
        property_update_group_t() noexcept;

        /// @brief End the group; the outermost end fires the deferred notifications.
        ~property_update_group_t();

        property_update_group_t(const property_update_group_t&) = delete;
        property_update_group_t& operator=(const property_update_group_t&) = delete;
        property_update_group_t(property_update_group_t&&) = delete;
        property_update_group_t& operator=(property_update_group_t&&) = delete;
    };

    /// @brief Begin a property update group (Qt's Qt::beginPropertyUpdateGroup()).
    /// @note noexcept.
    void begin_property_update_group_t() noexcept;

    /// @brief End a property update group; the outermost end drains the deferred notifications.
    /// @note noexcept: a throwing change handler is logged, not propagated.
    void end_property_update_group_t() noexcept;
}

#endif
