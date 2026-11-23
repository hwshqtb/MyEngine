/// @file meta_method_t.cpp
/// @brief The method handle: identity plus the calls, read from the owner's private table row.
///
/// The metadata is answered from the row entry() returns; this file keeps the handle's own state
/// (its `_caller` — the callability criterion) and the invocation entry points.

#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/event_t.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/thread_data_t.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwshqtb::metaobject {
    namespace {
        /// @brief Resolve a declared type spelling (a parameter's or the return type's) to its metatype.
        /// @param declared The stored spelling, which keeps the declaration's top-level cv/ref.
        /// @return The corresponding metatype; an invalid meta_type_t when the bare name is unregistered.
        /// @note The registry only knows the bare type name, so the top-level cv/ref is stripped before
        ///       the lookup: a `const T&` parameter OR return type resolves to T. Qt does the same on
        ///       both columns (parameterMetaType() answers QString for a `const QString &` parameter),
        ///       and the two columns follow one rule here, not two.
        meta_type_t resolve_declared_type(std::string_view declared) {
            return meta_type_t::resolve_global(detail::bare_spelling(declared));
        }
    }

    const detail::method_info_t* meta_method_t::entry() const noexcept {
        // A handle can carry index_npos (a synthesised property accessor) or a stale slot, and both
        // must answer defensively instead of indexing the table; the guard is shared with the enum
        // and class-info handles (meta_object_t::row_at).
        return meta_object_t::row_at(_owner, _index, &meta_info_t::method_infos);
    }

    bool meta_method_t::method_matches_boxed(std::size_t slot, std::span<const any_t> args, bool exact,
                                          qualifier_t object_qualifier) const noexcept {
        // The slot is the handle's own index, so it passes the same guard entry() uses: an invalid
        // handle, a synthesised accessor (index_npos) and a stale slot all answer "no match" instead
        // of indexing the table. The table itself never names any_t — this bridge does, which is why
        // it lives on the handle.
        if (_owner == nullptr) {
            return false;
        }
        if (slot >= _owner->info().method_count()) {
            return false;
        }
        // Every argument must carry a VALUE: a declared-only any_t owns empty reserved storage, and
        // handing that address to a thunk would read an object that was never constructed.
        for (const any_t& arg : args) {
            if (!arg.is_initialized()) {
                return false;
            }
        }
        // Not a third rule: each value is re-expressed as the (bare type, cvref) identity the table's
        // matcher compares, and meta_info_t::method_matches() decides.
        std::vector<cvref_meta_type_t> identities;
        identities.reserve(args.size());
        for (const any_t& arg : args) {
            identities.push_back(cvref_meta_type_t::from(arg));
        }
        return _owner->info().method_matches(slot, identities, exact, object_qualifier);
    }

    meta_method_t::meta_method_t(const meta_object_t* owner, index_t index) noexcept:
        _owner(owner),
        _index(index),
        _caller(nullptr) {
        if (const detail::method_info_t* row = entry()) {
            _caller = row->caller;
        }
    }

    meta_method_t::meta_method_t(const meta_object_t* owner, detail::generic_call caller) noexcept
        :
        _owner(owner),
        _index(index_npos),
        _caller(caller) { }

    bool meta_method_t::is_valid() const noexcept {
        // A handle is valid exactly when it holds a callable invoker. Every method table entry has
        // one (a non-public member included); an invalid handle and a synthesised accessor without a
        // caller do not.
        return _caller != nullptr;
    }

    method_access_enum meta_method_t::access() const noexcept {
        // No table row — an invalid handle, or a synthesised property accessor — means there is no
        // declaration to report an access specifier for. Otherwise the row's column answers with the
        // specifier the class wrote, recorded at build time. It is a hint: invoke() reaches the entry
        // whatever this says.
        const detail::method_info_t* row = entry();
        return (row != nullptr) ? row->access : method_access_enum::NONE;
    }

    std::string_view meta_method_t::name() const noexcept {
        const detail::method_info_t* row = entry();
        return (row != nullptr) ? static_cast<std::string_view>(row->name) : std::string_view{};
    }

    std::string_view meta_method_t::signature() const noexcept {
        const detail::method_info_t* row = entry();
        return (row != nullptr) ? static_cast<std::string_view>(row->signature) : std::string_view{};
    }

    index_t meta_method_t::index() const noexcept {
        // The stored slot is local to the DECLARING meta-object; adding its ancestors' counts back
        // reproduces the coordinate method(index) accepts. A base link gives the same answer, because
        // the ancestor counts of a meta-object do not depend on the table the search started from.
        if (entry() == nullptr) {
            return index_npos;
        }
        index_t offset = 0;
        for (const meta_object_t* base = _owner->super_class(); base != nullptr; base = base->super_class()) {
            offset += static_cast<index_t>(base->info().method_count());
        }
        return offset + _index;
    }

    bool meta_method_t::is_signal() const noexcept {
        const detail::method_info_t* row = entry();
        return (row != nullptr) && row->kind == method_kind_enum::SIGNAL;
    }

    method_kind_enum meta_method_t::method_kind() const noexcept {
        const detail::method_info_t* row = entry();
        return (row != nullptr) ? row->kind : method_kind_enum::PLAIN_METHOD;
    }

    bool meta_method_t::has_receiver() const noexcept {
        // A handle with NO row (an invalid one, or a synthesised property accessor) answers true: a
        // property accessor is a real non-static member function, and an invalid handle carries no
        // fact that could say otherwise. Only a row whose kind column says STATIC_METHOD answers false.
        const detail::method_info_t* row = entry();
        return (row != nullptr) ? row->has_receiver() : true;
    }

    qualifier_t meta_method_t::qualifier() const noexcept {
        const detail::method_info_t* row = entry();
        return (row != nullptr) ? row->qualifier : qualifier_t{ qualifier_flag::NONE };
    }

    std::string_view meta_method_t::param_name(std::size_t index) const noexcept {
        const detail::method_info_t* row = entry();
        if (row == nullptr) {
            return {};
        }
        const std::span<const detail::compiling_string_t> names = row->param_names;
        return (index < names.size()) ? static_cast<std::string_view>(names[index]) : std::string_view{};
    }

    std::size_t meta_method_t::param_count() const noexcept {
        const detail::method_info_t* row = entry();
        return (row != nullptr) ? row->param_types.to_span().size() : 0;
    }

    std::string_view meta_method_t::param_type_name(std::size_t index) const noexcept {
        const detail::method_info_t* row = entry();
        if (row == nullptr) {
            return {};
        }
        const std::span<const detail::compiling_string_t> types = row->param_types;
        return (index < types.size()) ? static_cast<std::string_view>(types[index]) : std::string_view{};
    }

    qualifier_t meta_method_t::param_qualifier(std::size_t index) const noexcept {
        const detail::method_info_t* row = entry();
        if (row == nullptr) {
            return qualifier_flag::NONE;
        }
        const std::span<const qualifier_t> qualifiers = row->param_qualifiers;
        return (index < qualifiers.size()) ? qualifiers[index] : qualifier_t{ qualifier_flag::NONE };
    }

    std::string_view meta_method_t::return_type_name() const noexcept {
        const detail::method_info_t* row = entry();
        return (row != nullptr) ? static_cast<std::string_view>(row->return_type) : std::string_view{};
    }

    meta_type_t meta_method_t::param_type(std::size_t index) const {
        if (entry() == nullptr) {
            return {};
        }
        // The interface is derived from the parameter's type text, not carried in the table.
        return resolve_declared_type(param_type_name(index));
    }

    meta_type_t meta_method_t::return_type() const {
        if (entry() == nullptr) {
            return {};
        }
        // Same rule as param_type(): the return column keeps its cv/ref spelling too. A row with an
        // empty return column (a synthesised accessor) resolves to the invalid handle, which is Qt's
        // UnknownType.
        return resolve_declared_type(return_type_name());
    }

    any_t meta_method_t::declare_return_slot() const {
        // An unnamed handle (a synthesised property accessor) carries no table row and therefore no
        // return column: the call runs with a null slot, exactly the pre-change discard shape.
        if (signature().empty()) {
            return any_t{};
        }
        // A void entry has nothing to construct. The invalid any_t is this function's "no slot"
        // answer; the CALLER turns a successful void call into the declared-only void shape
        // (any_t(meta_type_t::from_type<void>())). Keeping the two apart here means "no slot" is
        // never confused with "the void slot".
        if (detail::bare_spelling(return_type_name()) == std::string_view("void")) {
            return any_t{};
        }
        // The row stores the return type's identity spelling; return_type() resolves it through the
        // process-wide registry under its bare spelling — the same rule param_type() uses. An
        // unregistered name (a run-time built row may leave one) leaves the slot invalid, so the
        // call is still dispatched, for effect only.
        const meta_type_t type = return_type();
        return type.is_valid() ? any_t(type) : any_t{};
    }

    const void* meta_method_t::tag(std::string_view type_name) const noexcept {
        // A handle with no table row (a synthesised accessor or an invalid handle) carries no
        // annotation column at all; answering nullptr keeps the query total, like every other
        // defensive query.
        const detail::method_info_t* row = entry();
        if (row == nullptr) {
            return nullptr;
        }
        // The table stores a FLATTENED entry per annotation (type name + staticised pointer), in
        // declaration order; the first match wins.
        const std::span<const detail::annotation_entry_t> annotations = row->annotations;
        for (const detail::annotation_entry_t& annotation : annotations) {
            if (static_cast<std::string_view>(annotation.type_name) == type_name) {
                return annotation.value;
            }
        }
        return nullptr;
    }

    const meta_info_t* meta_method_t::owner_table() const noexcept {
        return _owner != nullptr ? std::addressof(_owner->info()) : nullptr;
    }

    void meta_method_t::invoke_caller(void* obj, void** params, qualifier_t object_qualifier) const {
        // This entry runs an ALREADY-RESOLVED row: there is no parameter list to compare here, so the
        // object's cvref is not re-tested (the matcher of the entry point that resolved the row
        // refused a const object against a non-const member already). The parameter is carried so the
        // cvref travels with the call instead of being lost at the `void*` boundary; the queued
        // replay, the signal emission and the property plumbing have no receiver TYPE left and pass
        // qualifier_flag::NONE.
        //
        // has_receiver() names WHY the skip is right: a row that is called ON an object has an
        // implicit object parameter, and its cvref was already checked by the matcher that selected
        // it; a row with NO receiver (STATIC_METHOD) has no implicit object at all, so there is nothing
        // for a cvref to describe.
        if (has_receiver()) {
            (void)object_qualifier;
        }
        // A null object is a quiet no-op for a callable that NEEDS one: there is no object to call on.
        // A row with no receiver (STATIC_METHOD) is the exception — its thunk ignores its obj argument,
        // so a null obj is not a reason to skip it; static_caller() is invoked with the null and never
        // dereferences it. The typed entry points take the object by reference and cannot express
        // null, which is why the null answer lives here and in the any_t entry point (which accepts a
        // pointer).
        if (obj == nullptr && has_receiver()) {
            return;
        }
        // The invoker is the callability criterion (is_valid() reads the same member). For a handle
        // that has a method_infos row, the call is forwarded to the table's primitive — the floor —
        // rather than to the stored pointer. The slot handed over is _index, the LOCAL slot inside
        // the declaring table: index() is the chain-wide coordinate and one class's own method_infos
        // does not accept it.
        if (_caller == nullptr) {
            return;
        }
        if (entry() != nullptr) {
            _owner->info().invoke(static_cast<std::size_t>(_index), obj, params);
            return;
        }
        // A row-less handle (a run-time built property accessor carries its thunk and index_npos)
        // has no table slot to give the primitive, so it keeps its own invoker.
        _caller(obj, params);
    }

    bool meta_method_t::invoke_erased_args(void* obj, object_t* target,
        std::span<const any_t> args, any_t* ret_any, connection_type_t type,
        qualifier_t object_qualifier, bool static_call) const {
        if (!is_valid()) {
            return false;
        }
        // The receiver-mode rule, asked BEFORE anything else: a static form on a row with a receiver
        // (or the instance form on a row without one) is a misuse, not a bad argument list, so it must
        // not be reported as one. invoke_erased() asks the same question again on the way through; it
        // is idempotent and silent when the call is on the right side.
        if (!check_receiver_mode(static_call)) {
            return false;
        }
        // A null object is a quiet no-op for a callable that needs one; a STATIC row has no receiver,
        // so null is the normal (and only) value there.
        if (obj == nullptr && has_receiver()) {
            return false;
        }
        // Every argument must carry a value: a declared-only any_t owns empty reserved storage, and
        // handing that address to a thunk would read an object that was never constructed.
        for (const any_t& arg : args) {
            if (!arg.is_initialized()) {
                throw bad_any_uninitialized_t{};
            }
        }
        // "Is this an entry of the table?" is signature().empty(): a synthesised property accessor has
        // a caller but no table row, so it carries no parameter / return metadata to check.
        const bool named = !signature().empty();
        if (named) {
            // The handle's ONE boxed-argument matcher (method_matches_boxed), given the object's cvref
            // that the typed entry point computed before the address was erased. The table itself
            // never names any_t: this bridge turns each value into a (type, cvref) identity and the
            // table's method_matches() decides.
            if (!method_matches_boxed(static_cast<std::size_t>(_index), args, /*exact=*/false,
                    object_qualifier)) {
                // No implicit conversion: the argument types have to match the parameter types by
                // name (and the parameter's cvref), exactly like std::any_cast refuses a wrong type.
                throw bad_any_cast_t{};
            }
        }
        // A void-returning method ignores the caller's slot entirely; a synthesised accessor has no
        // return column and is treated as value-returning (its thunk writes through params[0]).
        const std::string_view declared_return =
            named ? detail::bare_spelling(return_type_name()) : std::string_view{};
        const bool value_returning = !named || declared_return != std::string_view("void");
        void* slot = nullptr;
        if (ret_any != nullptr && value_returning) {
            if (ret_any->is_initialized()) {
                // The contract is a declared-but-not-constructed slot: the thunk constructs into the
                // reserved storage, so a live value there would be built over.
                throw bad_any_unsupported_t{
                    "invoke: the return slot must be declared, not constructed"
                };
            }
            if (named && declared_return != std::string_view(ret_any->type().name())) {
                throw bad_any_cast_t{};
            }
            // any_t::operator&(): the reserved storage of the declared-only slot.
            any_t& target_slot = *ret_any;
            slot = &target_slot;
        }
        std::vector<void*> params(args.size() + 1, nullptr);
        params[0] = slot;
        for (std::size_t i = 0; i < args.size(); ++i) {
            params[i + 1] = const_cast<void*>(static_cast<const void*>(&args[i]));
        }
        // The SAME dispatch the typed forms use: resolve_invoke() owns the connection decision and
        // the refusal rules, and post_meta_invoke() copies the arguments by their DECLARED parameter
        // types. Nothing is registered here: the arguments are any_t values, so their interfaces
        // already exist under the very names method_matches_boxed() just compared against the row.
        bool deferred = false;
        if (!invoke_erased(type, obj, target, params.data(), args.size(), slot, object_qualifier,
                (ret_any != nullptr && slot != nullptr) ? ret_any : nullptr,
                nullptr, &deferred, static_call)) {
            return false;
        }
        if (ret_any != nullptr && slot != nullptr && !deferred) {
            // The thunk has built the object in the slot's storage: record it, so is_initialized()
            // answers true and the destructor destroys the value. (The one return shape that writes
            // nothing — a non-copyable reference return, which the thunk invokes for effect only —
            // still lands here, exactly like the typed form's silent "effect only" corner.)
            // A POSTED call is marked by the event instead: it is the only place that knows when the
            // value was actually written, and marking it now would claim a value that is not there.
            ret_any->mark_constructed();
        }
        return true;
    }

    // ---- the connection-type invocation machinery -------------------------------------------
    //
    // The caller-side decision (resolve_invoke), the argument copying plus posting
    // (post_meta_invoke) and the handle's own dispatch (invoke_erased). Everything that needs the
    // complete object_t lives here; the headers only carry the typed templates.

    namespace detail {
        std::size_t copy_args(const meta_method_t& method, const object_t& receiver, void** params,
                              std::size_t count, std::vector<any_t>& args) {
            // The declared type is the rule (not the caller's static type): a parameter declared
            // "const T&" arrives as a T, so the copy is made through that very interface. A runtime
            // built row can leave a spelling unregistered (the lookup answers a null interface) and
            // a type can lack a copy constructor; both are the same refusal, reported as the argument
            // index so the caller can say which one it was.
            args.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                const meta_type_t type = method.param_type(i);
                if (!type.is_valid() || !type.has_copy_constructor()) {
                    return i;
                }
                void* memory = type.allocate();
                if (!type.copy_construct(memory, params[i + 1])) {
                    // The interface leaves failed storage unconstructed / clean: it only needs
                    // releasing, exactly as the type_interface_t storage convention promises.
                    type.deallocate(memory);
                    return i;
                }
                args.push_back(any_t::adopt(type, memory));
            }
            return count;
        }

        namespace {
            /// @brief The signature as it appears in a diagnostic.
            /// @param signature The normalised signature; empty for a synthesised property accessor.
            /// @return @p signature, or "<unnamed>" when it has none.
            std::string shown_signature(std::string_view signature) {
                return signature.empty() ? std::string("<unnamed>") : std::string(signature);
            }

            /// @brief Report one refused Queued / BlockingQueued invoke().
            /// @param message The already formatted text.
            void warn_invoke(const std::string& message) {
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::WARNING, message);
            }

            /// @brief Copy the arguments of one queued invoke() call into @p args.
            /// @param slot The resolved entry (its param_types column is the rule).
            /// @param receiver The target object (type resolution is process-wide; kept for the API).
            /// @param params generic_call convention: params[0] is the return-value slot.
            /// @param arg_count The number of arguments supplied by the caller.
            /// @param args Receives one owning any_t per argument.
            /// @return true on success; false (with one Warning) for an arity mismatch or a parameter
            ///         that cannot be copied / resolved.
            /// @note The copy itself is detail::copy_args(), the one implementation of the rule; this
            ///       wrapper adds the two things only the queued invoke path has — the ARITY check
            ///       (a signal activation always supplies exactly its parameter count, a caller's
            ///       call does not) and the Warning that names the entry and the failing argument.
            bool copy_arguments(const meta_method_t& slot, const object_t& receiver, void** params,
                                std::size_t arg_count, std::vector<any_t>& args) {
                const std::string name = shown_signature(slot.signature());
                if (slot.param_count() != arg_count) {
                    warn_invoke("invoke: '" + name + "' takes " + std::to_string(slot.param_count())
                        + " argument(s), " + std::to_string(arg_count)
                        + " were supplied; the queued call is dropped");
                    return false;
                }
                const std::size_t copied = copy_args(slot, receiver, params, arg_count, args);
                if (copied != arg_count) {
                    warn_invoke("invoke: argument " + std::to_string(copied) + " of '" + name
                        + "' cannot be copied for a queued call");
                    return false;
                }
                return true;
            }

            /// @brief post_meta_invoke()'s common tail.
            /// @param slot The already-resolved entry.
            /// @param receiver The object whose thread runs it.
            /// @param arg_count The number of arguments.
            /// @param params generic_call array (params[0] is @p ret_slot).
            /// @param ret_slot The caller's return-value storage, or nullptr.
            /// @param ret_any The any_t whose storage IS @p ret_slot and that the queued form must mark
            ///        constructed once the event ran, or nullptr (a `Ret&` call, a void row, or the
            ///        blocking form, whose poster marks it itself after the wait).
            /// @param blocking true to wait for the target thread.
            /// @return true when the call was posted (and, when blocking, completed).
            bool post_resolved(const meta_method_t& slot, object_t& receiver, std::size_t arg_count,
                               void** params, void* ret_slot, any_t* ret_any, bool blocking) {
                std::vector<any_t> args;
                if (!copy_arguments(slot, receiver, params, arg_count, args)) {
                    return false;
                }
                std::shared_ptr<detail::event_sync_t> sync;
                if (blocking) {
                    // The waiter's half of the completion handshake; the dispatcher calls
                    // notify_done() after it placed the call (event_dispatcher::dispatch_one).
                    sync = std::make_shared<detail::event_sync_t>();
                }
                // The event owns the argument copies and carries the slot handle plus the caller's
                // return storage; the target thread's place_meta_call() rebuilds the void** array with
                // params[0] = ret_slot. The non-blocking form also carries the any_t to mark: the
                // value is produced AFTER the poster returned, so only the dispatcher can finish it.
                meta_call_event_t* event = blocking
                    ? new meta_call_event_t(slot, std::move(args), ret_slot, sync)
                    : new meta_call_event_t(slot, std::move(args), ret_slot, ret_any);
                // The event belongs to the receiver's thread (core_application::post_event routes it
                // there), so posting from a foreign thread is the supported case.
                core_application::post_event(&receiver, event);
                if (blocking) {
                    // Block WITHOUT holding any lock of ours: post_event() released the dispatcher's
                    // queue mutex before it returned, and event_sync_t only guards its own state. The
                    // caller's ret_slot is a reference in the typed invoke() frame, which is still
                    // alive here, so the target thread's write lands in live storage.
                    sync->wait();
                    // Two outcomes release the wait: dispatch_one()'s notify_done() (the call was
                    // placed) or ~meta_call_event_t()'s notify_cancelled() (the receiver died while
                    // the event was still queued, or the queue was purged). A cancelled call never
                    // ran, so reporting success would be a lie: answer false and leave ret_slot
                    // untouched (it still holds whatever the caller initialised).
                    return !sync->cancelled();
                }
                return true;
            }
        }

        invoke_dispatch_enum resolve_invoke(connection_type_t type, const object_t* target,
            thread_data_t* target_thread, std::string_view signature) {
            const bool blocking = bool(type & connection_type_flag::BLOCKING_QUEUED);
            const bool explicit_queue = blocking || bool(type & connection_type_flag::QUEUED);
            // Auto resolves at CALL time, never at "connect" time: the object's affinity DATA (not the
            // thread_t identity) is what object_t::thread_data() records, and the calling thread is
            // thread_data_t::current().
            const bool same_thread = target_thread != nullptr
                && target_thread == &thread_data_t::current();
            if (blocking && same_thread) {
                // A BlockingQueued call waits for the target thread's event loop; on the calling
                // thread that loop cannot run while this call blocks it. Skip it (with the same
                // "would deadlock" diagnostic dispatch_signal prints) instead of hanging forever.
                warn_invoke("invoke: BlockingQueued call of '" + shown_signature(signature)
                    + "' on the calling thread is skipped to avoid a deadlock");
                return invoke_dispatch_enum::REFUSED;
            }
            const bool queued = explicit_queue
                || (target != nullptr && bool(type & connection_type_flag::AUTO) && !same_thread);
            if (target == nullptr) {
                // A value type has no thread affinity and no receiver to route an event by. This is
                // the fundamental reason for such a target, and it applies to every deferred type.
                if (queued) {
                    warn_invoke("invoke: Queued call of '" + shown_signature(signature)
                        + "' is refused: the target type has no thread affinity to post to");
                    return invoke_dispatch_enum::REFUSED;
                }
                return invoke_dispatch_enum::DIRECT;
            }
            // A Queued call is NOT refused for carrying a return value any more: the event carries the
            // caller's storage, so the value lands there when the event runs (the typed form hands back
            // a declared-only any_t meanwhile, and the event marks it constructed once it wrote).
            return queued ? invoke_dispatch_enum::QUEUED : invoke_dispatch_enum::DIRECT;
        }

        bool post_meta_invoke(const meta_method_t& slot, object_t& receiver, std::size_t arg_count,
            void** params, void* ret_slot, any_t* ret_any, bool blocking) {
            return post_resolved(slot, receiver, arg_count, params, ret_slot, ret_any, blocking);
        }
    }

    bool meta_method_t::check_receiver_mode(bool static_call) const {
        // THE one place the two misuse directions are decided. A row with no receiver is a static
        // member function (method_kind_enum::STATIC_METHOD); everything else — a plain method, a signal, a
        // synthesised property accessor, an invalid handle (has_receiver() answers true for it) — is
        // invoked ON an object. One Critical each way, naming the form that would work; nothing else is
        // logged, and nothing is invoked.
        const bool receiver = has_receiver();
        if (static_call == receiver) {
            if (static_call) {
                std::string message = "invoke_static: '";
                message += detail::shown_signature(signature());
                message += "' has a receiver (it is not a static member function)";
                message += " - call it with invoke(obj, ...)";
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, message);
            }
            else {
                std::string message = "invoke: '";
                message += detail::shown_signature(signature());
                message += "' is a static member function (it has no receiver)";
                message += " - call it with invoke_static(...)";
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, message);
            }
            return false;
        }
        return true;
    }

    bool meta_method_t::invoke_erased(connection_type_t type, void* obj, object_t* target,
        void** params, std::size_t arg_count, void* ret_slot, qualifier_t object_qualifier,
        any_t* ret_any, detail::argument_registrar register_types, bool* deferred,
        bool static_call) const {
        if (deferred != nullptr) {
            *deferred = false;
        }
        // THE choke point for both misuse directions: every public invoke() / invoke_static() form
        // reaches this function (the erased-argument forms through invoke_erased_args, which asks the
        // same question first), so the rule is stated once, in check_receiver_mode().
        if (!check_receiver_mode(static_call)) {
            return false;
        }
        const detail::invoke_dispatch_enum how = detail::resolve_invoke(type, target,
            (target != nullptr) ? target->thread_data() : nullptr,
            (_owner != nullptr && _index != index_npos) ? signature() : std::string_view{});
        if (how == detail::invoke_dispatch_enum::REFUSED) {
            return false;
        }
        if (how == detail::invoke_dispatch_enum::QUEUED) {
            // Only now, once the connection type is KNOWN to be a deferred one, are the argument
            // types registered: the Direct branch below passes addresses and never resolves a name,
            // so it must not pay the registry cost.
            if (register_types != nullptr) {
                register_types();
            }
            const bool blocking = bool(type & connection_type_flag::BLOCKING_QUEUED);
            // A BlockingQueued call waits for the target thread, so by the time it reports success the
            // entry HAS run; a non-blocking one only posted the event and returns first. That is what
            // the typed non-Ret form needs in order not to mark a still-empty slot constructed.
            if (deferred != nullptr && !blocking) {
                *deferred = true;
            }
            // The blocking form's poster marks the any_t itself once the wait returned (the event is
            // already done then); the non-blocking form hands the any_t to the event, which is the
            // only place that knows WHEN the value was actually written.
            return detail::post_meta_invoke(*this, *target, arg_count, params, ret_slot,
                blocking ? nullptr : ret_any, blocking);
        }
        // Direct: on the calling thread, through the handle's erased entry so the call reaches the
        // table's primitive (the same floor the queued replay and the property plumbing use). The
        // object's cvref travels with it to the erased edge.
        invoke_caller(obj, params, object_qualifier);
        return true;
    }

    HWSHQTB_EXPORT bool operator==(const meta_method_t& lhs, const meta_method_t& rhs) noexcept {
        return lhs._owner == rhs._owner
            && lhs._index == rhs._index
            && lhs._caller == rhs._caller;
    }
}
