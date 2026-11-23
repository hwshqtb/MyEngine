// The out-of-line part of the type-erased property core: the reference backend (it needs the
// complete object_t / meta_property_t types, which property_t.h only forward-declares) and the
// bodies of the classes declared in property_t.h.

#include <hwshqtb/metaobject/property_t.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_property_t.h>
#include <hwshqtb/metaobject/object_pointer_t.h>
#include <hwshqtb/metaobject/object_t.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwshqtb::metaobject {
    namespace {
        /// @brief Type-erased equality: same type and a usable equals thunk -> compare; otherwise
        ///        "not equal".
        /// @param a Left value.
        /// @param b Right value.
        /// @return The comparison result; false when the two values are not of the same type or when
        ///         the type has no usable operator== (a null equals thunk means "always notify").
        /// @note noexcept: a throwing operator== reports "not equal" instead of escaping a
        ///       notification.
        /// @note This is the value-change gate of the binding engine (Qt's has_operator_equal
        ///       short-circuit), type-erased through type_interface_t::equals. File-local: no header
        ///       declares it and no consumer calls it.
        bool equal_any(const any_t& a, const any_t& b) noexcept {
            try {
                // The INVALID state has no value to compare, so it is never equal to anything — the
                // boolean answer this noexcept gate owes its callers. (An invalid any_t's value access
                // throws bad_meta_type_t; here that is reported as "not equal", exactly like a
                // throwing operator== is.)
                if (!a.is_valid() || !b.is_valid()) {
                    return false;
                }
                const meta_type_t type = a.type();
                // A declared-only variable has no constructed object to compare: answer false
                // instead of reading its reserved (empty) storage.
                if (!(type == b.type()) || !a.is_initialized() || !b.is_initialized()) {
                    return false;
                }
                if (!type.has_equal_to()) {
                    return false;
                }
                return type.equal_to(&a, &b);
            }
            catch (...) {
                return false;
            }
        }
    }
}

namespace hwshqtb::metaobject::detail {
    /// @brief Report kind_enum::BINDING_LOOP on the core's installed binding (internal).
    /// @param target The core.
    /// @param message The description to record.
    /// @note noexcept: it runs from noexcept paths (destructors, dependency notifications).
    /// @note Declared here because no header needs it: every call site is in this translation unit.
    void report_binding_loop(core_t& target, const char* message) noexcept;

    /// @brief The reference backend of one core: the viewed object, its property and the notifier.
    ///
    /// One instance per property_t created by property_t::from(). It keeps a live object_pointer_t (so
    /// a destroyed owner reads as null instead of dangling), the property handle, the resolved
    /// notifier signature (connected once per dependant), and the view's OWN connection that forwards
    /// the notifier to the view's change handlers.
    struct reference_state_t {
        /// @brief The viewed object (null once the object is destroyed).
        object_pointer_t<object_t> owner;
        /// @brief The viewed property.
        meta_property_t property;
        /// @brief The property's notifier signature, resolved once at from() time.
        std::string_view notifier_signature;
        /// @brief The view's own connection: notifier -> notify_change(core).
        connection_handle_t own_connection;

        /// @brief Disconnect the view's own connection (connection_handle_t is not RAII).
        ~reference_state_t() {
            (void)own_connection.disconnect();
        }
    };

    /// @brief One dependant's connection to a reference view's notifier.
    ///
    /// Owned by the dependant's property_subscription_t. Destroying it disconnects the notifier, so a
    /// re-evaluation drops the edges it no longer reads.
    struct reference_connection_t {
        /// @brief The notifier connection.
        connection_handle_t handle;

        /// @brief Disconnect on destruction (a destroyed object makes this a safe no-op).
        ~reference_connection_t() {
            (void)handle.disconnect();
        }
    };

    static std::shared_ptr<core_t> make_reference_core(object_t& owner, const meta_property_t& property) {
        if (!property.is_valid()) {
            throw bad_property_t("property_t::from: invalid property");
        }
        if (!property.is_notifiable()) {
            throw bad_property_t("property_t::from: the property cannot notify (no notifier)");
        }
        const meta_method_t notifier = property.notifier();
        if (!notifier.is_valid() || notifier.signature().empty()) {
            throw bad_property_t("property_t::from: the property's notifier does not resolve");
        }

        auto state = std::make_shared<reference_state_t>();
        state->owner = object_pointer_t<object_t>(std::addressof(owner));
        state->property = property;
        state->notifier_signature = notifier.signature();

        auto result = std::make_shared<core_t>();
        result->is_reference = true;
        result->reference = state;
        result->reference_owner = std::addressof(owner);
        result->reference_property = property.name().data();
        result->reference_type = property.type(owner);

        // The view's own change handlers fire from the property's notifier (a write through the view
        // is published by the setter's notify signal, exactly like an external write).
        std::weak_ptr<core_t> weak = result;
        state->own_connection = owner.connect(
            // A property notifier carries NO parameters: the property's identity is the signal, so a
            // zero-parameter callable is what it takes (the old void** parameter was never read).
            state->notifier_signature, std::function<void()>([weak]() {
                if (std::shared_ptr<core_t> locked = weak.lock()) {
                    notify_change(*locked);
                }
            }));
        return result;
    }

    static void reference_read_into(core_t& target) {
        reference_state_t* state = target.reference.get();
        if (state == nullptr) {
            return;
        }
        object_t* owner = state->owner.data();
        if (owner == nullptr) {
            target.value = any_t{};
            return;
        }
        target.value = state->property.read(*owner);
    }

    static bool reference_write(core_t& target, const any_t& value) {
        reference_state_t* state = target.reference.get();
        if (state == nullptr) {
            return false;
        }
        object_t* owner = state->owner.data();
        if (owner == nullptr) {
            return false;
        }
        if (!state->property.write(*owner, value)) {
            return false;
        }
        // Keep the cached read in step with the value the setter accepted.
        target.value = value;
        return true;
    }

    static bool reference_install_binding(core_t& target, std::function<any_t()> fn) {
        if (target.binding && (target.evaluating || target.notifying)) {
            report_binding_loop(target, "Binding set during binding evaluation!");
            return false;
        }
        auto state = std::make_shared<binding_state_t>();
        binding_context_t context;
        context.set_error_target(&state->error);
        std::optional<any_t> evaluated = evaluate(target, fn, context);
        if (!evaluated.has_value()) {
            clear_binding(target);
            return false;
        }
        detach_dependencies(target);
        discard(target);
        target.binding = std::move(fn);
        target.state = std::move(state);
        take_dependencies(target, context);
        // Publish the result through the property: the setter emits the notifier, which drives the
        // dependants and the view's own handlers.
        (void)reference_write(target, *evaluated);
        return true;
    }

    static std::shared_ptr<reference_connection_t> reference_connect(
        const reference_state_t& state, std::weak_ptr<core_t> dependant) {
        auto connection = std::make_shared<reference_connection_t>();
        object_t* owner = state.owner.data();
        if (owner == nullptr || state.notifier_signature.empty()) {
            return connection;
        }
        connection->handle = owner->connect(
            state.notifier_signature, std::function<void(void**)>([dependant](void**) {
                if (std::shared_ptr<core_t> locked = dependant.lock()) {
                    mark_dependency_changed(*locked);
                }
            }));
        return connection;
    }
}

namespace hwshqtb::metaobject::detail {
    // -------------------------------------------------------------------------
    // The notification list and the evaluation context (declared in property_t.h)
    // -------------------------------------------------------------------------

    subscription_node_t::subscription_node_t(kind_enum node_kind) noexcept:
        kind(node_kind) { }

    // ---- handler_node_t ----

    handler_node_t::handler_node_t(std::function<void()> callback):
        subscription_node_t(subscription_node_t::kind_enum::HANDLER), _callback(std::move(callback)) { }

    handler_node_t::~handler_node_t() {
        detach();
    }

    void handler_node_t::invoke() const {
        if (_callback) {
            _callback();
        }
    }

    bool handler_node_t::is_bound() const noexcept {
        return _detach != nullptr;
    }

    void handler_node_t::bind(void* owner, void (*detach)(void*, handler_node_t*) noexcept) noexcept {
        _owner = owner;
        _detach = detach;
    }

    void handler_node_t::unbind() noexcept {
        _owner = nullptr;
        _detach = nullptr;
    }

    void handler_node_t::detach() noexcept {
        if (_detach == nullptr) {
            return;
        }
        void (*detach)(void*, handler_node_t*) noexcept = _detach;
        void* owner = _owner;
        unbind();
        detach(owner, this);
    }

    // ---- dependency_edge_t ----

    dependency_edge_t::dependency_edge_t() noexcept:
        subscription_node_t(subscription_node_t::kind_enum::DEPENDENCY) { }

    dependency_edge_t::~dependency_edge_t() {
        detach();
    }

    void dependency_edge_t::attach(core_t& dependency_core, core_t& dependant_core) {
        _dependency = &dependency_core;
        _dependant = &dependant_core;
        dependency_core.subscribers.push_back(this);
    }

    void dependency_edge_t::detach() noexcept {
        if (_dependency == nullptr) {
            return;
        }
        std::erase(_dependency->subscribers, static_cast<subscription_node_t*>(this));
        _dependency = nullptr;
        _dependant = nullptr;
    }

    void dependency_edge_t::propagate() noexcept {
        if (_dependant != nullptr) {
            mark_dependency_changed(*_dependant);
        }
    }

    void dependency_edge_t::dependency_destroyed() noexcept {
        core_t* dependant = _dependant;
        _dependency = nullptr;
        _dependant = nullptr;
        if (dependant != nullptr) {
            forget_dependency(*dependant, this);
        }
    }

    core_t* dependency_edge_t::dependency() const noexcept {
        return _dependency;
    }

    // ---- pending_dependency_t ----

    bool pending_dependency_t::same(const core_t* other_owning, void* other_owner,
                                  const void* other_property) const noexcept {
        if (owning != nullptr || other_owning != nullptr) {
            return owning == other_owning;
        }
        return reference_owner == other_owner && reference_property == other_property;
    }

    // ---- property_subscription_t ----

    property_subscription_t::property_subscription_t(property_subscription_t&& other) noexcept:
        _reference_owner(other._reference_owner),
        _reference_property(other._reference_property),
        _connection(std::move(other._connection)) {
        other._reference_owner = nullptr;
        other._reference_property = nullptr;
    }

    property_subscription_t& property_subscription_t::operator=(property_subscription_t&& other) noexcept {
        if (this != &other) {
            detach();
            _reference_owner = other._reference_owner;
            _reference_property = other._reference_property;
            _connection = std::move(other._connection);
            other._reference_owner = nullptr;
            other._reference_property = nullptr;
        }
        return *this;
    }

    property_subscription_t::~property_subscription_t() {
        detach();
    }

    void property_subscription_t::attach(const pending_dependency_t& pending, core_t& dependant) {
        detach();
        _reference_owner = pending.reference_owner;
        _reference_property = pending.reference_property;
        if (pending.owning != nullptr) {
            _edge.attach(*const_cast<core_t*>(pending.owning), dependant);
        }
        else if (pending.reference) {
            _connection = reference_connect(*pending.reference, dependant.weak_from_this());
        }
    }

    void property_subscription_t::detach() noexcept {
        _edge.detach();
        _connection.reset();
        _reference_owner = nullptr;
        _reference_property = nullptr;
    }

    dependency_edge_t& property_subscription_t::edge() noexcept {
        return _edge;
    }

    // ---- binding_context_t ----

    void binding_context_t::record(const property_t& property) {
        const dependency_identity_t identity = property.identity();
        if (identity.owning == nullptr && !identity.reference) {
            return;
        }
        for (const pending_dependency_t& entry : _pending) {
            if (entry.same(identity.owning, identity.owner, identity.property)) {
                return;
            }
        }
        pending_dependency_t entry;
        entry.owning = identity.owning;
        entry.reference = identity.reference;
        entry.reference_owner = identity.owner;
        entry.reference_property = identity.property;
        entry.self_dependency = identity.owning != nullptr && identity.owning == _target;
        _pending.push_back(std::move(entry));
    }

    void binding_context_t::set_error(property_binding_t_error_t error) {
        if (_error_target != nullptr) {
            *_error_target = std::move(error);
        }
        else {
            _error = std::move(error);
        }
    }

    void binding_context_t::set_error_target(property_binding_t_error_t* target) noexcept {
        _error_target = target;
    }

    void binding_context_t::set_target(core_t* target, std::weak_ptr<core_t> dependant) noexcept {
        _target = target;
        _dependant = std::move(dependant);
    }

    void binding_context_t::forget(const core_t* dependency) noexcept {
        std::erase_if(_pending, [dependency](const pending_dependency_t& entry) {
            return entry.owning == dependency;
        });
    }

    void binding_context_t::forget(void* owner, const void* property) noexcept {
        std::erase_if(_pending, [owner, property](const pending_dependency_t& entry) {
            return entry.owning == nullptr && entry.reference_owner == owner
                && entry.reference_property == property;
        });
    }

    const std::vector<pending_dependency_t>& binding_context_t::pending() const noexcept {
        return _pending;
    }

    // ---- context_scope_t ----

    context_scope_t::context_scope_t(binding_context_t& context) noexcept:
        _previous(current_context_slot()) {
        current_context_slot() = &context;
    }

    context_scope_t::~context_scope_t() noexcept {
        current_context_slot() = _previous;
    }

    // ---- core ----

    core_t::~core_t() {
        // A pending change-handler notification or deferred change must not call into a
        // destroyed core; a queued recompute must not either.
        binding_scheduler_t::discard_handler_if_queued(this);
        binding_scheduler_t::discard_change_if_queued(this);
        binding_scheduler_t::discard_if_queued(this);
        // A closure may destroy a property it has already read during the evaluation that is
        // running right now; the finished context must not carry this pointer.
        if (binding_context_t* context = current_context(); context != nullptr) {
            context->forget(this);
            if (is_reference) {
                context->forget(reference_owner, reference_property);
            }
        }
        // Dependants first: their edges live in their own cores and stay valid.
        const std::vector<subscription_node_t*> snapshot = subscribers;
        for (subscription_node_t* node : snapshot) {
            if (node != nullptr && node->kind == subscription_node_t::kind_enum::DEPENDENCY) {
                static_cast<dependency_edge_t*>(node)->dependency_destroyed();
            }
        }
        subscribers.clear();
        // Then orphan the handlers: a handle that outlives this core becomes inert.
        for (const std::shared_ptr<handler_node_t>& handler : handlers) {
            if (handler) {
                handler->unbind();
            }
        }
        handlers.clear();
        // Finally release our own read set (reference connections disconnect here).
        detach_dependencies(*this);
    }
}

namespace hwshqtb::metaobject {
    // -------------------------------------------------------------------------
    // property_binding_t_error_t / bad_property_t
    // -------------------------------------------------------------------------

    bool property_binding_t_error_t::has_error() const noexcept {
        return kind != kind_enum::NONE;
    }

    bad_property_t::bad_property_t(const std::string& what):
        std::logic_error(what) { }

    // -------------------------------------------------------------------------
    // property_binding_t
    // -------------------------------------------------------------------------

    bool property_binding_t::is_null() const noexcept {
        return !_state;
    }

    property_binding_t::operator bool() const noexcept {
        return !is_null();
    }

    void property_binding_t::set_sticky(bool sticky) noexcept {
        if (_state) {
            _state->sticky = sticky;
        }
    }

    bool property_binding_t::is_sticky() const noexcept {
        return _state != nullptr && _state->sticky;
    }

    void property_binding_t::schedule_notify() noexcept {
        if (_state) {
            _state->pending_notify = true;
        }
    }

    bool property_binding_t::notify_scheduled() const noexcept {
        return _state != nullptr && _state->pending_notify;
    }

    property_binding_t_error_t property_binding_t::error() const {
        return _state ? _state->error : property_binding_t_error_t{};
    }

    std::source_location property_binding_t::source_location() const noexcept {
        return _state ? _state->location : std::source_location{};
    }

    // -------------------------------------------------------------------------
    // property_change_handler_t
    // -------------------------------------------------------------------------

    // The only place a handler_node_t is allocated: consumers never instantiate the class, so it
    // needs no export annotation.
    property_change_handler_t::property_change_handler_t(std::function<void()> callback):
        _node(std::make_shared<detail::handler_node_t>(std::move(callback))) { }

    property_change_handler_t& property_change_handler_t::operator=(property_change_handler_t&& other) noexcept {
        if (this != &other) {
            if (_node) {
                _node->detach();
            }
            _node = std::move(other._node);
        }
        return *this;
    }

    property_change_handler_t::~property_change_handler_t() {
        if (_node) {
            _node->detach();
        }
    }

    bool property_change_handler_t::is_bound() const noexcept {
        return _node && _node->is_bound();
    }

    void property_change_handler_t::set_source(property_t& property) {
        if (!_node) {
            return;                     // no callback: nothing to register
        }
        _node->detach();
        if (property._core) {
            detail::register_handler(*property._core, _node);
        }
    }

    // -------------------------------------------------------------------------
    // property_update_group_t
    // -------------------------------------------------------------------------

    property_update_group_t::property_update_group_t() noexcept {
        binding_scheduler_t::current().begin_update_group();
    }

    property_update_group_t::~property_update_group_t() {
        binding_scheduler_t::current().end_update_group();
    }

    // -------------------------------------------------------------------------
    // property_t: handle state
    // -------------------------------------------------------------------------

    property_t property_t::from(object_t& owner, const meta_property_t& property) {
        property_t view;
        view._core = detail::make_reference_core(owner, property);
        return view;
    }

    bool property_t::is_valid() const noexcept {
        return static_cast<bool>(_core);
    }

    property_t property_t::as_read_only() const noexcept {
        property_t view = *this;
        view._read_only = true;
        return view;
    }

    bool property_t::is_read_only() const noexcept {
        return _read_only;
    }

    bool property_t::is_owning() const noexcept {
        return _core != nullptr && !_core->is_reference;
    }

    bool property_t::has_binding() const noexcept {
        return _core != nullptr && static_cast<bool>(_core->binding);
    }

    detail::dependency_identity_t property_t::identity() const noexcept {
        detail::dependency_identity_t result;
        if (!_core) {
            return result;
        }
        if (_core->is_reference) {
            result.reference = _core->reference;
            result.owner = _core->reference_owner;
            result.property = _core->reference_property;
            return result;
        }
        result.owning = _core.get();
        return result;
    }

    const std::shared_ptr<detail::core_t>& property_t::core_handle() const noexcept {
        return _core;
    }

    // -------------------------------------------------------------------------
    // property_t (definitions that need the scheduler and change handlers)
    // -------------------------------------------------------------------------

    property_t::property_t(any_t initial) {
        _core = std::make_shared<detail::core_t>();
        _core->value = std::move(initial);
    }

    meta_type_t property_t::type() const noexcept {
        if (!_core) {
            return meta_type_t{};
        }
        return _core->is_reference ? _core->reference_type : _core->value.type();
    }

    const any_t& property_t::checked_value() const {
        static const any_t no_value;
        if (!_core) {
            return no_value;
        }
        if (detail::binding_context_t* context = detail::current_context(); context != nullptr) {
            context->record(*this);
        }
        detail::core_t& target = *_core;
        if (target.evaluating) {
            // The closure (directly or through a cycle) read the value it is computing: do not
            // recurse, report once and hand out the value that is already cached.
            HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL,
                "binding cycle detected during evaluation");
            detail::report_binding_loop(target, "binding cycle detected during evaluation");
            return target.value;
        }
        if (target.binding && target.dirty && !target.notifying
            && !binding_scheduler_t::flushing_on_this_thread()) {
            // Consume the queued entry together with the evaluation: if flush_one() reports that
            // nothing was queued (the cycle budget cleared the queue), evaluate here instead.
            if (!binding_scheduler_t::current().flush_one(std::addressof(target))) {
                detail::recompute(target);
            }
        }
        if (target.is_reference) {
            detail::reference_read_into(target);
        }
        return target.value;
    }

    any_t property_t::value() const {
        return checked_value();
    }

    bool property_t::set_value(const any_t& value) {
        if (!_core) {
            return false;
        }
        if (_read_only) {
            detail::warn_read_only("set_value");
            return false;
        }
        if (_core->is_reference) {
            return detail::reference_write(*_core, value);
        }
        detail::core_t& target = *_core;
        if (target.binding && target.state && target.state->sticky) {
            // Keep the binding installed (Qt skips removeBinding() for a sticky binding).
        }
        else {
            detail::clear_binding(target);
        }
        if (equal_any(target.value, value)) {
            return false;               // clear_binding() already reported the property clean
        }
        target.value = value;
        detail::notify_change(target);
        return true;
    }

    bool property_t::set_value_bypassing_bindings(const any_t& value) {
        if (!_core || _core->is_reference) {
            return false;
        }
        if (_read_only) {
            detail::warn_read_only("set_value_bypassing_bindings");
            return false;
        }
        _core->value = value;
        return true;
    }

    bool property_t::notify() {
        if (!_core) {
            return false;
        }
        if (_read_only) {
            detail::warn_read_only("notify");
            return false;
        }
        detail::notify_change(*_core);
        return true;
    }

    void property_t::invalidate() noexcept {
        if (_core) {
            detail::mark_dependency_changed(*_core);
        }
    }

    namespace {
        /// @brief The guard every property_t::set_binding() overload starts with.
        /// @param core The handle's shared core (null for a detached handle).
        /// @param read_only The handle's read-only flag (Qt's read-only QBindable).
        /// @return true when the binding may be installed; false when the call must answer false —
        ///         quietly for a detached handle, with one Warning for a read-only one.
        bool can_set_binding(const detail::core_t* core, bool read_only) {
            if (core == nullptr) {
                return false;               // detached handle: there is nothing to bind to
            }
            if (read_only) {
                detail::warn_read_only("set_binding");
                return false;
            }
            return true;
        }
    }

    bool property_t::set_binding(std::function<any_t()> fn,
                                        std::source_location location) {
        if (!can_set_binding(_core.get(), _read_only)) {
            return false;
        }
        if (!fn) {
            clear_binding();
            return true;
        }
        if (_core->is_reference) {
            return detail::reference_install_binding(*_core, std::move(fn));
        }
        auto state = std::make_shared<detail::binding_state_t>();
        state->location = location;
        return detail::install_binding(*_core, std::move(state), std::move(fn));
    }

    bool property_t::set_binding(std::function<any_t()> fn,
                                        const meta_type_t& declared_type) {
        if (!can_set_binding(_core.get(), _read_only)) {
            return false;
        }
        if (!fn) {
            clear_binding();
            return true;
        }
        auto state = std::make_shared<detail::binding_state_t>();
        if (!detail::check_binding_type(*_core, *state, declared_type)) {
            return false;               // declared type <> property type: refused, nothing touched
        }
        if (_core->is_reference) {
            // A view's result is published through the property's setter, which owns the type; the
            // declared type was still checked against the property's above.
            return detail::reference_install_binding(*_core, std::move(fn));
        }
        return detail::install_binding(*_core, std::move(state), std::move(fn), nullptr, nullptr,
                                       &declared_type);
    }

    bool property_t::set_binding(const property_binding_t& handle,
                                        std::source_location location) {
        if (!can_set_binding(_core.get(), _read_only)) {
            return false;
        }
        if (handle.is_null()) {
            // Qt's setBinding(QUntypedPropertyBinding()): the empty binding removes the installed one
            // and installs nothing (the call reports "no binding installed").
            detail::clear_binding(*_core);
            return false;
        }
        std::shared_ptr<detail::binding_state_t> state;
        if (handle._state && handle._state->location.line() != 0) {
            state = handle._state;      // the handle keeps the location it was created with
        }
        else {
            state = std::make_shared<detail::binding_state_t>();
            state->location = location;
        }
        if (_core->is_reference) {
            // A view's result is published through the property's setter (reference backend); the
            // handle's state is not the one the view keeps, exactly like the functor overloads.
            return detail::reference_install_binding(*_core, handle._fn);
        }
        return detail::install_binding(*_core, std::move(state), handle._fn);
    }

    property_binding_t property_t::take_binding() {
        property_binding_t taken;
        if (!_core) {
            return taken;
        }
        detail::core_t& target = *_core;
        if (!target.binding) {
            detail::clear_binding(target);      // no binding: stay clean and hand back null
            return taken;
        }
        detail::detach_dependencies(target);
        detail::discard(target);
        taken._state = std::move(target.state);
        taken._fn = std::move(target.binding);
        return taken;
    }

    property_binding_t property_t::binding() const {
        property_binding_t handle;
        if (!_core || !_core->binding) {
            return handle;
        }
        handle._state = _core->state;
        handle._fn = _core->binding;
        return handle;
    }

    void property_t::clear_binding() {
        if (!_core) {
            return;
        }
        if (_read_only) {
            detail::warn_read_only("clear_binding");
            return;
        }
        detail::clear_binding(*_core);
    }

    property_change_handler_t property_t::on_value_changed(std::function<void()> callback) const {
        property_change_handler_t handle(std::move(callback));
        if (_core && handle._node) {
            detail::register_handler(*_core, handle._node);
        }
        return handle;
    }

    property_change_handler_t property_t::subscribe(std::function<void()> callback) const {
        property_change_handler_t handle(std::move(callback));
        if (!_core || !handle._node) {
            return handle;
        }
        handle._node->invoke();         // Qt: the immediate call happens exactly once
        detail::register_handler(*_core, handle._node);
        return handle;
    }
}
namespace hwshqtb::metaobject {
    // ---- is_binding_evaluating ----
    HWSHQTB_EXPORT bool is_binding_evaluating() noexcept {
        return detail::current_context() != nullptr;
    }

    // ---- set_binding_error ----
    HWSHQTB_EXPORT void set_binding_error(const property_binding_t_error_t& error) {
        if (auto* context = detail::current_context()) {
            context->set_error(error);
        }
    }

    // ---- flush_bindings ----
    HWSHQTB_EXPORT void flush_bindings() {
        binding_scheduler_t::current().flush();
    }

    // ---- begin_property_update_group_t ----
    HWSHQTB_EXPORT void begin_property_update_group_t() noexcept {
        binding_scheduler_t::current().begin_update_group();
    }

    // ---- end_property_update_group_t ----
    HWSHQTB_EXPORT void end_property_update_group_t() noexcept {
        binding_scheduler_t::current().end_update_group();
    }

}

namespace hwshqtb::metaobject {
    // -------------------------------------------------------------------------
    // binding_scheduler_t (declared in property_t.h)
    // -------------------------------------------------------------------------

    void binding_scheduler_t::mark_dirty(detail::core_t* binding) {
        if (binding == nullptr) {
            return;
        }
        for (detail::core_t* queued : _dirty) {
            if (queued == binding) {
                return;
            }
        }
        _dirty.push_back(binding);
    }

    void binding_scheduler_t::flush() {
        if (_flushing) {
            return;
        }
        _flushing = true;
        // Reset _flushing on every exit path (normal return, budget stop, exception from a
        // logging handler) and stop exposing the round queue. The guard is an object so that
        // neither flag can be forgotten.
        struct flushing_guard_t {
            bool& flag;
            std::vector<detail::core_t*>*& queue;
            std::size_t& next;
            ~flushing_guard_t() {
                flag = false;
                queue = nullptr;
                next = 0;
            }
        } flushing_guard{ _flushing, _round_queue, _round_next };

        std::vector<detail::core_t*> queue;
        queue.swap(_dirty);
        // Expose the running round so a read (value() -> flush_one()) can consume the entry of
        // a binding that is still waiting here instead of leaving it for a second recompute.
        _round_queue = &queue;
        _round_next = 0;

        std::size_t recomputes = 0;
        while (_round_next < queue.size()) {
            if (recomputes >= max_recomputes_per_flush) {
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, "binding dependency cycle detected");
                // The same diagnosis on the per-binding error channel. The scheduler
                // cannot tell which entry closed the cycle, so every binding still waiting in the
                // round (and every one queued behind it) reports the loop.
                for (detail::core_t* stuck : queue) {
                    if (stuck != nullptr) {
                        detail::report_binding_loop(*stuck, "binding dependency cycle detected");
                    }
                }
                for (detail::core_t* stuck : _dirty) {
                    if (stuck != nullptr) {
                        detail::report_binding_loop(*stuck, "binding dependency cycle detected");
                    }
                }
                queue.clear();
                _dirty.clear();
                return;
            }
            detail::core_t* binding = queue[_round_next++];
            ++recomputes;
            if (binding != nullptr) {
                recompute_one(*binding);
            }
            // A recompute may have marked further bindings: they belong to this same round.
            if (!_dirty.empty()) {
                queue.insert(queue.end(), _dirty.begin(), _dirty.end());
                _dirty.clear();
            }
        }
        // The notifications end_notification() deferred while the round was running are
        // drained now, with every binding of the round already recomputed.
        drain_pending_handlers();
    }

    bool binding_scheduler_t::flush_one(detail::core_t* b) {
        if (b == nullptr || !remove_queued(b)) {
            return false;
        }
        detail::recompute(*b);
        return true;
    }

    bool binding_scheduler_t::discard(detail::core_t* b) noexcept {
        return remove_queued(b);
    }

    bool binding_scheduler_t::discard_if_queued(detail::core_t* b) noexcept {
        detail::thread_data_t& data = detail::thread_data_t::current();
        return data.bindings != nullptr && data.bindings->discard(b);
    }

    bool binding_scheduler_t::flushing_on_this_thread() noexcept {
        detail::thread_data_t& data = detail::thread_data_t::current();
        return data.bindings != nullptr && data.bindings->_flushing;
    }

    std::size_t binding_scheduler_t::pending_count() const noexcept {
        return _dirty.size();
    }

    bool binding_scheduler_t::in_update_group() const noexcept {
        return _update_group_depth != 0;
    }

    void binding_scheduler_t::begin_update_group() noexcept {
        ++_update_group_depth;
    }

    void binding_scheduler_t::end_update_group() noexcept {
        if (_update_group_depth == 0) {
            return;
        }
        if (--_update_group_depth != 0) {
            return;
        }
        while (!_pending_changes.empty()) {
            const pending_change_t change = _pending_changes.front();
            _pending_changes.erase(_pending_changes.begin());
            if (change.fire == nullptr) {
                continue;
            }
            try {
                change.fire(change.target);
            }
            catch (const std::exception& error) {
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL,
                    std::string("grouped change notification failed: ") + error.what());
            }
            catch (...) {
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, "grouped change notification failed");
            }
        }
        // Every write of the group has been assigned and propagated: one round brings the whole
        // graph up to date (each dependant recomputes once) and drains the handler notifications.
        try {
            flush();
        }
        catch (const std::exception& error) {
            HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL,
                std::string("grouped binding flush failed: ") + error.what());
        }
        catch (...) {
            HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, "grouped binding flush failed");
        }
    }

    void binding_scheduler_t::mark_change_pending(void* target, void (*fire)(void*)) {
        for (const pending_change_t& entry : _pending_changes) {
            if (entry.target == target) {
                return;
            }
        }
        _pending_changes.push_back(pending_change_t{ target, fire });
    }

    bool binding_scheduler_t::discard_change(void* target) noexcept {
        return std::erase_if(_pending_changes, [target](const pending_change_t& entry) {
                   return entry.target == target;
               })
            != 0;
    }

    bool binding_scheduler_t::discard_change_if_queued(void* target) noexcept {
        detail::thread_data_t& data = detail::thread_data_t::current();
        return data.bindings != nullptr && data.bindings->discard_change(target);
    }

    void binding_scheduler_t::begin_notification() noexcept {
        ++_notification_depth;
    }

    void binding_scheduler_t::end_notification() {
        if (_notification_depth > 0) {
            --_notification_depth;
        }
        if (_notification_depth != 0 || _draining_handlers || _flushing
            || std::uncaught_exceptions() != 0) {
            return;
        }
        drain_pending_handlers();
    }

    void binding_scheduler_t::mark_handler_pending(void* target, void (*fire)(void*) noexcept) {
        _pending_handlers.push_back(pending_handler_t{ target, fire });
    }

    bool binding_scheduler_t::discard_handler(void* target) noexcept {
        return std::erase_if(_pending_handlers, [target](const pending_handler_t& entry) {
                   return entry.target == target;
               })
            != 0;
    }

    bool binding_scheduler_t::discard_handler_if_queued(void* target) noexcept {
        detail::thread_data_t& data = detail::thread_data_t::current();
        return data.bindings != nullptr && data.bindings->discard_handler(target);
    }

    binding_scheduler_t& binding_scheduler_t::current() {
        detail::thread_data_t& data = detail::thread_data_t::current();
        if (data.bindings == nullptr) {
            data.bindings = new binding_scheduler_t();
        }
        return *data.bindings;
    }

    void binding_scheduler_t::drain_pending_handlers() {
        if (_draining_handlers || _pending_handlers.empty()
            || std::uncaught_exceptions() != 0) {
            return;
        }
        _draining_handlers = true;
        struct draining_guard_t {
            bool& flag;
            ~draining_guard_t() {
                flag = false;
            }
        } draining_guard{ _draining_handlers };
        // Counts are per drain (the vector keeps its capacity between drains).
        _drain_counts.clear();
        while (!_pending_handlers.empty()) {
            // Pop before firing: the notification may destroy another property, whose
            // destructor removes its still-queued entry (discard_handler()). Entries queued by
            // the notification itself stay in the queue and run in this same loop.
            const pending_handler_t entry = _pending_handlers.front();
            _pending_handlers.erase(_pending_handlers.begin());
            if (entry.fire == nullptr || entry.target == nullptr) {
                continue;
            }
            std::size_t& recomputes = drain_count_of(entry.target);
            if (++recomputes > max_recomputes_per_flush) {
                detail::core_t& stuck = *static_cast<detail::core_t*>(entry.target);
                if (recomputes == max_recomputes_per_flush + 1) {
                    // Report once per core and stop evaluating it in this drain. The pending flag
                    // is cleared so a later pass (or the round-end flush) can try again.
                    stuck.handler_pending = false;
                    try {
                        HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL,
                            "binding dependency cycle detected");
                    }
                    catch (...) {
                        // A diagnostic must never escape the drain (it runs inside noexcept paths).
                    }
                    detail::report_binding_loop(stuck, "binding dependency cycle detected");
                }
                continue;
            }
            entry.fire(entry.target);
        }
    }

    std::size_t& binding_scheduler_t::drain_count_of(void* target) {
        for (drain_count_t& entry : _drain_counts) {
            if (entry.target == target) {
                return entry.count;
            }
        }
        _drain_counts.push_back(drain_count_t{ target, 0 });
        return _drain_counts.back().count;
    }

    void binding_scheduler_t::recompute_one(detail::core_t& binding) {
        try {
            detail::recompute(binding);
        }
        catch (const std::exception& error) {
            HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL,
                std::string("binding recompute failed: ") + error.what());
        }
        catch (...) {
            HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, "binding recompute failed");
        }
    }

    bool binding_scheduler_t::remove_queued(detail::core_t* b) noexcept {
        if (b == nullptr) {
            return false;
        }
        bool removed = std::erase(_dirty, b) != 0;
        if (_round_queue != nullptr) {
            for (auto it = _round_queue->begin() + static_cast<std::ptrdiff_t>(_round_next);;) {
                it = std::find(it, _round_queue->end(), b);
                if (it == _round_queue->end()) {
                    break;
                }
                it = _round_queue->erase(it);
                removed = true;
            }
        }
        return removed;
    }
}

namespace hwshqtb::metaobject::detail {
    // ---- current_context_slot ----
        binding_context_t*& current_context_slot() noexcept {
            return thread_data_t::current().context;
        }

    // ---- current_context ----
        binding_context_t* current_context() noexcept {
            return current_context_slot();
        }

    // ---- forget_dependency ----
        void forget_dependency(core_t& dependant, dependency_edge_t* edge) noexcept {
            (void)edge;
            binding_scheduler_t::discard_if_queued(&dependant);
            dependant.dirty = false;
        }

    // ---- register_handler ----
        void register_handler(core_t& target, const std::shared_ptr<handler_node_t>& node) {
            if (!node) {
                return;
            }
            for (subscription_node_t* existing : target.subscribers) {
                if (existing == node.get()) {
                    return;
                }
            }
            node->bind(std::addressof(target), &detach_handler);
            target.subscribers.push_back(node.get());
            target.handlers.push_back(node);
        }

    // ---- detach_handler ----
        void detach_handler(void* owner, handler_node_t* node) noexcept {
            core_t& target = *static_cast<core_t*>(owner);
            std::erase(target.subscribers, static_cast<subscription_node_t*>(node));
            std::erase_if(target.handlers, [node](const std::shared_ptr<handler_node_t>& held) {
                return held.get() == node;
            });
        }

    // ---- report_binding_loop ----
        void report_binding_loop(core_t& target, const char* message) noexcept {
            if (!target.state) {
                return;
            }
            try {
                property_binding_t_error_t& slot = target.state->error;
                slot.kind = property_binding_t_error_t::kind_enum::BINDING_LOOP;
                slot.message = message != nullptr ? message : "";
            }
            catch (...) {
                // The message allocation failed: the kind alone still reports the loop.
                target.state->error.kind = property_binding_t_error_t::kind_enum::BINDING_LOOP;
            }
        }

    // ---- evaluate ----
        std::optional<any_t> evaluate(core_t& target, const std::function<any_t()>& fn,
                                             binding_context_t& context) {
            context.set_target(std::addressof(target), target.weak_from_this());
            try {
                context_scope_t scope(context);
                return fn();
            }
            catch (const std::exception& error) {
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL,
                    std::string("binding evaluation failed: ") + error.what());
            }
            catch (...) {
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, "binding evaluation failed");
            }
            return std::nullopt;
        }

    // ---- discard ----
        void discard(core_t& target) noexcept {
            binding_scheduler_t::discard_if_queued(std::addressof(target));
            target.dirty = false;
        }

    // ---- detach_dependencies ----
        void detach_dependencies(core_t& target) noexcept {
            for (property_subscription_t& subscription : target.dependencies) {
                subscription.detach();
            }
            target.dependencies.clear();
        }

    // ---- take_dependencies ----
        void take_dependencies(core_t& target, binding_context_t& context) {
            const std::vector<pending_dependency_t>& pending = context.pending();
            // Size the read set once, then register: a subscription's edge address must stay stable
            // while the dependency points at it.
            target.dependencies.resize(pending.size());
            for (std::size_t i = 0; i < pending.size(); ++i) {
                target.dependencies[i].attach(pending[i], target);
                if (pending[i].self_dependency) {
                    report_binding_loop(target, "binding cycle detected during evaluation");
                }
            }
        }

    // ---- clear_binding ----
        void clear_binding(core_t& target) {
            detach_dependencies(target);
            discard(target);
            target.binding = nullptr;
            target.state.reset();
            target.dirty = false;
        }

    // ---- warn_read_only ----
        void warn_read_only(const char* operation) {
            HWSHQTB_LOG_RAW(log_connection(), log_level_enum::WARNING,
                std::string(operation != nullptr ? operation : "write")
                    + ": the property handle is read-only");
        }

    // ---- check_binding_type ----
        bool check_binding_type(const core_t& target, binding_state_t& state,
                                       const meta_type_t& declared_type) {
            const meta_type_t expected = target.is_reference ? target.reference_type : target.value.type();
            if (!declared_type.is_valid() || !expected.is_valid() || declared_type == expected) {
                return true;
            }
            std::string message = "the binding declares '" + declared_type.name()
                + "' but the property is '" + expected.name() + "'";
            state.error.kind = property_binding_t_error_t::kind_enum::UNKNOWN;
            state.error.message = message;
            HWSHQTB_LOG_RAW(log_connection(), log_level_enum::WARNING, "set_binding: " + message);
            return false;
        }

    // ---- install_binding ----
        bool install_binding(core_t& target, std::shared_ptr<binding_state_t> state,
                                    std::function<any_t()> fn,
                                    std::shared_ptr<binding_state_t>* replaced_state,
                                    std::function<any_t()>* replaced_fn,
                                    const meta_type_t* declared_type) {
            if (target.binding && (target.evaluating || target.notifying)) {
                report_binding_loop(target, "Binding set during binding evaluation!");
                return false;
            }
            binding_context_t context;
            context.set_error_target(state ? &state->error : nullptr);
            std::optional<any_t> evaluated = evaluate(target, fn, context);
            if (!evaluated.has_value()) {
                clear_binding(target);      // failed evaluation: old value kept, no binding installed
                return false;
            }
            if (declared_type != nullptr && declared_type->is_valid()) {
                // The declared type is the binding's contract, so a first result of another type is
                // a refusal too. Nothing is assigned and a previously installed binding stays (Qt's
                // order: setBinding() that returns false does not touch the installed binding).
                const meta_type_t expected =
                    target.is_reference ? target.reference_type : target.value.type();
                if (!(evaluated->type() == *declared_type)
                    || (expected.is_valid() && !(evaluated->type() == expected))) {
                    const std::string message = "the binding produced '" + evaluated->type().name()
                        + "' but the property is '" + expected.name() + "'";
                    if (state) {
                        state->error.kind = property_binding_t_error_t::kind_enum::UNKNOWN;
                        state->error.message = message;
                    }
                    HWSHQTB_LOG_RAW(log_connection(), log_level_enum::WARNING, "set_binding: " + message);
                    return false;
                }
            }
            // A scheduled notification is consumed by the install that honors it.
            const bool force_notify = state != nullptr && state->pending_notify;
            if (state != nullptr) {
                state->pending_notify = false;
            }
            // Detach the previous binding (if any) and, when asked, hand it to the caller.
            detach_dependencies(target);
            discard(target);
            if (replaced_state != nullptr && replaced_fn != nullptr) {
                *replaced_state = std::move(target.state);
                *replaced_fn = std::move(target.binding);
            }
            else {
                target.state.reset();
                target.binding = nullptr;
            }
            target.binding = std::move(fn);
            target.state = std::move(state);
            take_dependencies(target, context);
            if (force_notify || !equal_any(target.value, *evaluated)) {
                target.value = std::move(*evaluated);
                notify_change(target);
            }
            return true;
        }

    // ---- recompute ----
        void recompute(core_t& target) {
            if (!target.binding) {
                target.dirty = false;
                return;
            }
            if (target.evaluating) {
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL,
                    "binding cycle detected during evaluation");
                report_binding_loop(target, "binding cycle detected during evaluation");
                return;
            }
            target.dirty = false;
            std::optional<any_t> evaluated;
            {
                // The guard covers the evaluation only: the notification below is not "evaluating",
                // so a change handler that reads this property is not mistaken for a binding cycle.
                target.evaluating = true;
                struct evaluating_guard_t {
                    bool& flag;
                    ~evaluating_guard_t() {
                        flag = false;
                    }
                } evaluating_guard{ target.evaluating };

                binding_context_t context;
                context.set_error_target(target.state ? &target.state->error : nullptr);
                evaluated = evaluate(target, target.binding, context);
                if (!evaluated.has_value()) {
                    return;             // logged by evaluate(); keep the old value
                }
                // The read set is replaced even when the value did not change: the closure may have
                // taken another branch and no longer reads the old dependencies.
                detach_dependencies(target);
                take_dependencies(target, context);
            }
            if (equal_any(target.value, *evaluated)) {
                return;                 // equal value: no assignment, no handlers, no dependants
            }
            if (target.is_reference) {
                // A reference view publishes through the property's setter: the notifier drives the
                // dependants and the view's own handlers (see reference_write()).
                (void)reference_write(target, *evaluated);
                return;
            }
            target.value = std::move(*evaluated);
            notify_change(target);
        }

    // ---- request_handler_notification ----
        void request_handler_notification(core_t& target) {
            if (target.handler_pending) {
                return;
            }
            target.handler_pending = true;
            binding_scheduler_t::current().mark_handler_pending(std::addressof(target),
                                                             &fire_handler_notification);
        }

    // ---- notify_handler_debt ----
        void notify_handler_debt(core_t& target) noexcept {
            target.handler_pending = false;
            if (!target.dirty || !target.binding) {
                return;
            }
            try {
                if (!binding_scheduler_t::current().flush_one(std::addressof(target))) {
                    recompute(target);
                }
            }
            catch (const std::exception& error) {
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL,
                    std::string("change handler notification failed: ") + error.what());
            }
            catch (...) {
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, "change handler notification failed");
            }
        }

    // ---- fire_handler_notification ----
        void fire_handler_notification(void* target) noexcept {
            notify_handler_debt(*static_cast<core_t*>(target));
        }

    // ---- fire_deferred_change ----
        void fire_deferred_change(void* target) {
            // Propagate the change directly and call the handlers; the update group is not consulted.
            core_t& changed = *static_cast<core_t*>(target);
            binding_scheduler_t& scheduler = binding_scheduler_t::current();
            scheduler.begin_notification();
            struct notification_guard_t {
                binding_scheduler_t& scheduler;
                ~notification_guard_t() {
                    scheduler.end_notification();
                }
            } notification_guard{ scheduler };

            // Dependants are invalidated first, the handlers run afterwards.
            const std::vector<subscription_node_t*> snapshot = changed.subscribers;
            for (subscription_node_t* node : snapshot) {
                if (node != nullptr && node->kind == subscription_node_t::kind_enum::DEPENDENCY) {
                    static_cast<dependency_edge_t*>(node)->propagate();
                }
            }
            if (changed.handlers.empty() || changed.notifying) {
                return;
            }
            changed.notifying = true;
            struct notifying_guard_t {
                bool& flag;
                ~notifying_guard_t() {
                    flag = false;
                }
            } notifying_guard{ changed.notifying };
            const std::vector<std::shared_ptr<handler_node_t>> handlers = changed.handlers;
            for (const std::shared_ptr<handler_node_t>& handler : handlers) {
                if (handler && handler->is_bound()) {
                    handler->invoke();
                }
            }
        }

    // ---- notify_change ----
        void notify_change(core_t& target) {
            binding_scheduler_t& scheduler = binding_scheduler_t::current();
            if (scheduler.in_update_group()) {
                scheduler.mark_change_pending(std::addressof(target), &fire_deferred_change);
                return;
            }
            fire_deferred_change(std::addressof(target));
        }

    // ---- close_notification_pass ----
        void close_notification_pass() {
            binding_scheduler_t& scheduler = binding_scheduler_t::current();
            if (scheduler.in_update_group()) {
                return;
            }
            scheduler.begin_notification();
            scheduler.end_notification();
        }

    // ---- mark_dependency_changed ----
        void mark_dependency_changed(core_t& target) noexcept {
            if (!target.binding) {
                return;
            }
            if (target.handlers.empty()) {
                if (!target.dirty) {
                    target.dirty = true;
                    binding_scheduler_t::current().mark_dirty(std::addressof(target));
                    const std::vector<subscription_node_t*> snapshot = target.subscribers;
                    for (subscription_node_t* node : snapshot) {
                        if (node != nullptr && node->kind == subscription_node_t::kind_enum::DEPENDENCY) {
                            static_cast<dependency_edge_t*>(node)->propagate();
                        }
                    }
                }
                // A reference view publishes at the end of this notification pass (its value lives in
                // the object, so no read of the view pulls it); an owning core recomputes at read time.
                if (target.is_reference) {
                    request_handler_notification(target);
                    close_notification_pass();
                }
                return;
            }
            if (target.notifying) {
                // A dependency changed while this property's change handlers run: Qt's re-entrant
                // evaluation is refused with a loop error and the value is left as it is.
                report_binding_loop(target, "binding cycle detected during evaluation");
                return;
            }
            if (target.evaluating) {
                // A dependency changed while this property is evaluating: re-mark it and let the
                // running recompute (or the round-end flush) converge.
                target.dirty = true;
                binding_scheduler_t::current().mark_dirty(std::addressof(target));
                return;
            }
            request_handler_notification(target);
            if (!target.dirty) {
                target.dirty = true;
                binding_scheduler_t::current().mark_dirty(std::addressof(target));
            }
            if (target.is_reference) {
                close_notification_pass();
            }
        }

}
