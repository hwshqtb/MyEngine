#ifndef HWSHQTB__METAOBJECT__META_METHOD_H
#define HWSHQTB__METAOBJECT__META_METHOD_H

/// @file meta_method_t.h
/// @brief The reflection handle for methods, meta_method_t.

#include "basic_def.h"
#include "meta_type_t.h"
#include "meta_info_t.h"
#include "any_t.h"
#include "export.h"
#include "log.h"

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwshqtb::metaobject {
    class object_t;
    class meta_object_t;
    class meta_property_t;
    class meta_method_t;

    namespace detail {
        /// @brief Whether T (cv and references removed) is a connection-type argument.
        /// @note Disambiguation: every invoke() form excludes both the flag enum
        ///       (`connection_type_flag::DIRECT`) and the wrapper (`connection_type_t`) wherever a
        ///       connection type must not be read: as the OBJECT, as the OUT SLOT and inside an
        ///       argument pack. A flag_t is built from its enum implicitly, so a tag left in a pack
        ///       binds EXACTLY and would beat the tag form's converting parameter (GCC then reports
        ///       "template instantiation depth exceeds maximum" on the Auto recursion); a tag bound
        ///       to the out slot would be read as a return slot.
        template <typename T>
        inline constexpr bool is_connection_type_arg_v =
            std::is_same_v<std::remove_cvref_t<T>, connection_type_t>
            || std::is_same_v<std::remove_cvref_t<T>, connection_type_flag>;

        /// @brief Whether T is the any_t argument span of invoke(Obj&&, std::span<const any_t>, ...).
        /// @note Disambiguation: a type convertible to the span (std::vector<any_t>,
        ///       std::array<any_t, N>, std::span<any_t>) is excluded from the variadic
        ///       invoke(Obj&&, Ts&&...) candidates, which would otherwise bind it EXACTLY and then
        ///       fail the argument match silently.
        template <typename T>
        inline constexpr bool is_any_args_v = std::is_convertible_v<T, std::span<const any_t>>;

        /// @brief The object's OWN cvref for the erased entry points, whose target arrives as `Obj&&`.
        /// @tparam Obj The target parameter's type: an object value, a reference to one, or a POINTER.
        /// @return For a value/reference: get_param_qualifier\<Obj&&\>() — the forwarding-reference
        ///         reading (`T&` -> LVALUE_REF, `T&&`/bare `T` -> RVALUE_REF, cv bits as written).
        ///         For a POINTER: the cvref of the POINTEE read as an lvalue (`const T*` ->
        ///         CONST|LVALUE_REF, `T*` -> LVALUE_REF), because the object the erased channel calls on
        ///         is `*obj`, not the pointer. A null pointer never gets this far (it is answered
        ///         before the call).
        /// @note The value must be computed HERE, in the typed frame: the erased entry takes `void*`,
        ///       which has already lost the constness the matcher needs.
        template <typename Obj>
        constexpr qualifier_t object_qualifier_of() noexcept {
            if constexpr (std::is_pointer_v<std::remove_reference_t<Obj>>) {
                return get_param_qualifier<std::remove_pointer_t<std::remove_reference_t<Obj>>&>();
            }
            else {
                return get_param_qualifier<Obj&&>();
            }
        }

        /// @brief How one typed invoke() call must be dispatched on the calling thread.
        /// @note The decision is the caller-side half of connection_type_t: Direct runs the entry
        ///       inline, Queued posts it to the target object's thread, Refused means the call cannot
        ///       be honoured at all (a Warning has already been logged, and the typed invoke()
        ///       returns false).
        enum struct invoke_dispatch_enum : std::uint8_t {
            /// @brief Run the entry on the calling thread.
            DIRECT,
            /// @brief Post the call to the target object's thread.
            QUEUED,
            /// @brief Refused; exactly one Warning was logged.
            REFUSED,
        };

        /// @brief The registration callback a typed call hands across the erasure boundary.
        /// @note A plain function pointer on purpose: invoke_erased() is out of line and must not
        ///       know the argument types, while the typed wrappers that DO know them pass
        ///       &register_argument_types<Ts...>. It is called ONLY on the Queued branch, because
        ///       only that branch resolves the argument types by name.
        using argument_registrar = void (*)() noexcept;

        /// @brief Make one call's argument type findable by the registry, on demand.
        /// @tparam T The argument type as it appears at the call site (cv/ref allowed).
        /// @note The queued path copies each argument by its DECLARED parameter type, which it
        ///       resolves through the registry by NAME — so those names must be registered. The
        ///       caller passed values, not registrations, so the library does it here.
        /// @note No "is this type nameable" guard is needed: reaching this instantiation means the
        ///       typed invoke already ran check_args<Ts...>(), which instantiates arg_spelling<Ts>()
        ///       and therefore rejects an unspellable type at compile time — the same rule the table
        ///       build applies to a parameter type.
        template <typename T>
        inline void register_argument_type() noexcept {
            (void)meta_type_t::register_type<std::remove_cvref_t<T>>();
        }

        /// @brief register_argument_type() for a whole argument list.
        template <typename... Ts>
        inline void register_argument_types() noexcept {
            (register_argument_type<Ts>(), ...);
        }

        /// @brief Resolve a caller-chosen connection type for one typed invoke() call.
        /// @param type The caller's connection type (Auto resolves here, at call time).
        /// @param target The object the call is made on, or nullptr when the target type has no thread
        ///        affinity (a gadget value): then only Direct is possible, and an explicit
        ///        Queued / BlockingQueued is refused because there is nowhere to post.
        /// @param signature The normalised signature, for the refusal message (may be empty for a
        ///        synthesised property accessor).
        /// @return Direct / Queued / Refused, with a Refused answer having logged exactly one Warning
        ///         through log_connection.
        /// @note This is the single place the "would deadlock" / "no thread affinity" rules live, so
        ///       meta_object_t::invoke and meta_method_t::invoke cannot drift apart.
        /// @note Whether the call carries a return value plays NO part here: a Queued call may carry
        ///       one now. The event holds the caller's storage and fills it when it runs, so there is
        ///       nothing to refuse — the "no return channel" rule was removed (it was really a
        ///       lifetime hazard, and the call-side contract answers it: the slot must outlive the
        ///       event).
        HWSHQTB_EXPORT invoke_dispatch_enum resolve_invoke(connection_type_t type, const object_t* target,
            std::string_view signature);

        /// @brief Post one already-resolved call to @p receiver's thread.
        /// @param slot The entry to run there.
        /// @param receiver The object whose thread must run it.
        /// @param arg_count Number of arguments in @p params (checked against the entry's parameter count).
        /// @param params generic_call convention: params[0] is the return-value slot, params[1...] the
        ///        argument addresses.
        /// @param ret_slot The caller's return-value storage, or nullptr. It is forwarded to the target
        ///        thread as params[0], so the thunk writes the result straight into the caller's
        ///        variable (@c Ret& form) or into the returned any_t's reserved storage (non-Ret form).
        /// @param ret_any The any_t whose storage is @p ret_slot, or nullptr. Only the NON-BLOCKING
        ///        form uses it: the event marks that variable constructed once the call was placed,
        ///        because the value exists only then. The blocking form passes nullptr — its poster is
        ///        still inside invoke() and marks after the wait.
        /// @param blocking true for BlockingQueued: wait until the target thread has run the call.
        /// @return true when the call was posted (and, for @p blocking, actually placed on the target
        ///         thread); false when it could not be posted, or when a blocking call was CANCELLED —
        ///         the receiver was destroyed (or the queue purged) before the event ran, which the
        ///         event's destructor reports through event_sync_t::notify_cancelled().
        /// @note The arguments are copied by their DECLARED parameter types, the same rule
        ///       dispatch_signal's queued path uses (detail::copy_args); a parameter that cannot be
        ///       copied is refused with one Warning.
        /// @warning The event keeps the ADDRESS of @p ret_slot / @p ret_any, never a copy: both must
        ///          outlive the posted event. A non-blocking call therefore obliges the caller to keep
        ///          the returned any_t (or its own out variable) alive and unmoved until the event has
        ///          run; an event destroyed unexecuted writes nothing, so the slot stays declared only
        ///          (non-Ret form) or untouched (@c Ret& form).
        HWSHQTB_EXPORT bool post_meta_invoke(const meta_method_t& slot, object_t& receiver,
            std::size_t arg_count, void** params, void* ret_slot, any_t* ret_any, bool blocking);

        /// @brief Copy one call's arguments by the entry's DECLARED parameter types.
        /// @param method The resolved entry whose param_types column is the rule.
        /// @param receiver The target object (type resolution is process-wide; kept for the API).
        /// @param params generic_call convention: params[0] is the return-value slot, arguments start
        ///        at params[1].
        /// @param count How many arguments to copy (the entry's parameter count, normally).
        /// @param args Receives one owning any_t per copied argument, in declaration order.
        /// @return The number of arguments copied. Any value below @p count is the index of the first
        ///         argument that could not be copied: its declared type does not resolve (a null
        ///         interface: an empty / unregistered spelling), is not copy-constructible, or its
        ///         copy construction failed. That argument's storage has been released; the copies of
        ///         the earlier arguments stay in @p args.
        /// @note This is the ONE implementation of "copy the arguments by declared type". The
        ///       declared parameter type decides what is copied, never the static type of the
        ///       caller's pointer; the copies are owning any_t values, so the event (or the spy row)
        ///       can point at them after this function returns. A failure is reported as the index
        ///       of the argument that could not be copied, and every caller chooses its own channel.
        std::size_t copy_args(const meta_method_t& method, const object_t& receiver, void** params,
                              std::size_t count, std::vector<any_t>& args);

        /// @brief @p obj seen as an object_t, or nullptr when T is not an object_t.
        /// @tparam T The target type as deduced at the call site.
        /// @param obj The target value (an object, or a gadget value).
        /// @return The address of the object_t sub-object, or nullptr for a gadget (no thread affinity).
        /// @note The pointer is non-const: the queued path routes the event by the object's thread()
        ///       and by core_application::post_event(), both of which need a mutable receiver. The
        ///       underlying object is not modified through it.
        template <typename T>
        object_t* target_object(T& obj) noexcept {
            if constexpr (std::is_base_of_v<object_t, std::remove_cvref_t<T>>) {
                return const_cast<object_t*>(static_cast<const object_t*>(&obj));
            }
            else {
                return nullptr;
            }
        }
    }

    /// @brief The reflection handle of a callable member (a method, or an accessor synthesised for
    ///        a property).
    ///
    /// @par Two sources
    ///  - @b Named: an entry of the declaring meta-object's method_infos — the member functions obtained
    ///    with object_t::method(name), plus the named setter / reseter / notifier of a property (an
    ///    inherited one included: the handle then points at the DECLARING base's meta-object).
    ///    Metadata such as the signature, parameters, return value, signal / slot markers and the
    ///    function's own qualifiers only exists on named handles.
    ///  - @b Synthesised: when property_a is attached to a data member, the reader synthesised by
    ///    _reader_helper. It has a generic_call but no method_infos slot, so every metadata query
    ///    returns an empty view / 0 / None, and "is this an entry of the table?" is answered by
    ///    signature().empty().
    ///
    /// @par Ownership
    /// A handle stores `const meta_object_t* _owner` (the declaring meta-object of the chain) plus its
    /// local index. It is a pure handle: every table question — name, signature, parameters, return
    /// type, kind, caller — is answered from the row it points at in the owner's table. The handle is
    /// a friend of meta_object_t and reads that table directly (entry()); the table is owned by value
    /// by the meta-object, so a handle is valid while that meta-object lives (the class's
    /// static_meta_object, or an arena entry from meta_object_builder_t).
    ///
    /// @par Calling convention
    /// The same as meta_info_t's generic_call: @c params[0] is the return-value slot (may be
    /// nullptr) and @c params[1...] hold the argument addresses.
    ///
    /// @par Call entry points
    /// Values cross this API as compile-time types or as any_t — there is no erased `void*` channel.
    /// Every form takes the object FIRST, the out slot (when it has one) IMMEDIATELY after it, the
    /// connection type AFTER the out slot, and the arguments LAST — obj, (out), connectiontype, args
    /// — the same relative order meta_object_t::invoke / call use after their signature / name, so
    /// the two levels read alike:
    ///  - invoke(obj, vs...): universal forwarding, the argument types are the call site's; on a
    ///    named handle the template first performs a run-time argument match (type names +
    ///    qualifiers) and answers the INVALID any_t without calling when it does not match;
    ///  - invoke(obj, Ret& ret, vs...): the same, and the method's result is written into the
    ///    caller's declared-but-not-constructed slot (the declared return type is checked first);
    ///  - invoke(obj, connection_type_t type, vs...): the Auto form with a caller-chosen connection
    ///    type (Direct / Queued / BlockingQueued);
    ///  - invoke(obj, Ret& ret, connection_type_t type, vs...): both of the above;
    ///  - invoke(obj, std::span&lt;const any_t&gt; args): one any_t per parameter, matched BY NAME
    ///    (no implicit conversion), mismatch throws bad_any_cast_t;
    ///  - invoke(obj, any_t& ret, std::span&lt;const any_t&gt; args): the same, with a caller slot;
    ///  - invoke(obj, connection_type_t type, std::span&lt;const any_t&gt; args) and
    ///    invoke(obj, any_t& ret, connection_type_t type, std::span&lt;const any_t&gt; args): the
    ///    erased pair with a caller-chosen connection type.
    /// The non-`Ret&` forms all answer an any_t, built from the row's declared return type: the value on
    /// success (a synchronous call), a declared-only any_t for a void method, a declared-only any_t of
    /// the row's return type when the call was only POSTED (the event fills it later), and the INVALID
    /// any_t on failure (a miss, a refused connection type, a broken argument list).
    /// The library's own erased calls (queued event dispatch, the property read/write plumbing) go
    /// through the private invoke_caller(), never through the public surface.
    ///
    /// @sa meta_property_t
    /// @sa meta_object_t::method
    class HWSHQTB_EXPORT meta_method_t {
    public:
        /// @brief Default construction yields an invalid handle.
        meta_method_t() noexcept = default;

        /// @brief Map a pointer-to-member to its meta_method_t handle (QMetaMethod::fromSignal
        ///        generalised to any non-static member function; the pointer VALUE is the identity —
        ///        its nullness is checked and its value selects the member).
        /// @tparam Pm The member-pointer type, e.g. void (T::*)(int) const.
        /// @param pm The pointer; nullptr yields an invalid method.
        /// @return The method handle, or an invalid one when the pointer is null.
        /// @note Inherited members resolve naturally: `&derived_t::inherited_sig` has the DECLARING
        ///       class's pointer type, so the lookup happens in the declaring class's own table.
        /// @note Ref-qualified (&/&&) and volatile members are not supported (static_assert message);
        ///       the four supported shapes are R(T::*)(Args...), const, noexcept, const noexcept.
        /// @note The identity is the pointer VALUE: members that share one function type (two
        ///       `void() const` methods, say) are told apart by comparing the pointer itself against
        ///       the candidates, so each resolves to ITSELF (a type-only match would return the first).
        /// @note A member the table does not hold ([[= make_hidden()]]) has no method_infos entry, so
        ///       the lookup finds nothing and the answer is an invalid handle — never a throw. A
        ///       pointer that matches none of the class's candidates is invalid for the same reason.
        /// @note The class must be an OBJECT/GADGET class: the handle is resolved through its
        ///       static_meta_object.
        /// @note static member template: the pointer's TYPE selects the candidate set, so no object
        ///       is involved.
        /// @sa meta_object_t::method
        template <typename Pm>
        static meta_method_t from_method(Pm pm) noexcept;

        /// @name Identity
        /// @{
        /// @brief Whether the handle is valid: it can be executed (it holds a callable invoker).
        /// @return true when it holds a callable invoker — for a named entry that is exactly "exists
        ///         in the table and can be executed", and a synthesised property accessor is valid
        ///         too (it has an invoker, it just has no table row); false for an invalid handle.
        /// @note noexcept. A non-public member is VALID: the access specifier is only a hint
        ///       (access() reports it), never a gate.
        /// @note A method handle is never a constructor: a constructor is its own entity with its own
        ///       handle (meta_constructor_t), table and index space, so nothing here can name one.
        bool is_valid() const noexcept;

        /// @brief The declared access of the member, as a hint only: a meta-object invocation does not
        ///        enforce it (everything in the table can be called).
        /// @note none for an invalid handle.
        /// @note Also none for a synthesised property accessor: it has no declaration in the table.
        /// @note A run-time built entry (meta_object_builder_t) reports PUBLIC_SCOPE: the builder carries no
        ///       access information.
        /// @note noexcept.
        method_access_enum access() const noexcept;

        /// @brief Get the method name.
        /// @return The function name when named; an empty view for a synthesised accessor or an
        ///         invalid handle.
        /// @note noexcept.
        std::string_view name() const noexcept;

        /// @brief Get the normalised signature, of the form @c name(type,type).
        /// @return The signature when named; an empty view for a synthesised accessor or an invalid
        ///         handle.
        /// @note noexcept, no allocation.
        std::string_view signature() const noexcept;

        /// @brief The method's global index in its meta-object table; index_npos when the handle is
        ///        invalid.
        /// @return The coordinate meta_object_t::method(index) accepts (Qt's
        ///         QMetaMethod::methodIndex(), counted parents-first over the whole chain);
        ///         index_npos for an invalid handle and for a synthesised property accessor.
        /// @note noexcept. A handle taken from a base class's table reports the same coordinate the
        ///       most-derived table uses.
        index_t index() const noexcept;
        /// @}

        /// @name Classification and function qualifiers
        /// @{
        /// @brief Whether it is marked as a signal.
        /// @note noexcept; false for a synthesised accessor or an invalid handle.
        bool is_signal() const noexcept;

        /// @brief What kind of function this is (QMetaMethod::methodType).
        /// @return The kind: PLAIN_METHOD, SIGNAL or STATIC_METHOD (a method handle is never a
        ///         constructor — see meta_constructor_t); PLAIN_METHOD for an invalid handle and for a
        ///         synthesised property accessor.
        /// @note There is no separate "slot" kind: any reflected method can be the target of a
        ///       connection, so an ordinary method reports PLAIN_METHOD and nothing marks it as a slot.
        /// @note Qt's QMetaMethod::methodType() can also answer ConstructorMethod; this enum cannot,
        ///       because a constructor has its own handle (meta_constructor_t) and its own row type.
        method_kind_enum method_kind() const noexcept;

        /// @brief Whether this row is called ON an object.
        /// @return false exactly for method_kind_enum::STATIC_METHOD; true for every other kind, for an
        ///         invalid handle and for a synthesised property accessor (a property accessor IS a
        ///         non-static member function, so it has a receiver).
        /// @note noexcept. Reads the row's kind column; a handle with no row (invalid, or a
        ///       synthesised accessor) answers true, which is the honest default: nothing about it says
        ///       "no receiver".
        bool has_receiver() const noexcept;

        /// @brief Get the function's own qualifiers (a combination of const / volatile / & / &&).
        /// @return The function's qualifiers when named; None for a synthesised accessor or an
        ///         invalid handle.
        /// @note noexcept. Unrelated to any_t::qualifier(): that one is the value's cvref, this one
        ///       is the function's cvref.
        qualifier_t qualifier() const noexcept;
        /// @}

        /// @name Parameter and return-value metadata
        /// @{
        /// @brief Get the number of parameters.
        /// @return The parameter count when named; 0 for a synthesised accessor or an invalid handle.
        /// @note noexcept.
        std::size_t param_count() const noexcept;

        /// @brief The name of parameter i (empty when it is unnamed).
        std::string_view param_name(std::size_t index) const noexcept;

        /// @brief Get the normalised type name of the @p index -th parameter.
        /// @param index Parameter index, zero-based.
        /// @return The type name on a hit; an empty view when out of range or when the handle is not
        ///         named.
        /// @note noexcept, no allocation.
        std::string_view param_type_name(std::size_t index) const noexcept;

        /// @brief Get the qualifiers of the @p index -th parameter.
        /// @param index Parameter index, zero-based.
        /// @return The parameter's cvref qualifiers on a hit; None when out of range or when the
        ///         handle is not named.
        /// @note noexcept.
        qualifier_t param_qualifier(std::size_t index) const noexcept;

        /// @brief Get the normalised type name of the return value.
        /// @return The return type name when named; an empty view for a synthesised accessor or an
        ///         invalid handle.
        /// @note noexcept, no allocation.
        std::string_view return_type_name() const noexcept;
        /// @}

        /// @name Resolving parameter and return types through the process-wide type registry
        /// @{
        /// @brief Resolve the type of the @p index -th parameter.
        /// @param index Parameter index, zero-based.
        /// @return The corresponding type; an invalid meta_type_t when out of range, when the handle is
        ///         not named, or when the parameter type is not registered under its name.
        /// @note The type is resolved by NAME: the owner's entry is asked for the parameter's BARE
        ///       spelling (top-level cv/ref stripped) and the registry answers nullptr when that name
        ///       is not registered. A `const T&` parameter therefore answers T, because the cvref is
        ///       recorded separately by param_qualifier() (Qt's parameterMetaType() behaves the same
        ///       way). The lookup is the same for a reflected and a run-time built (meta_object_builder_t) entry.
        /// @note No object is needed: the registry is process-wide, so the answer does not depend on
        ///       the instance being queried.
        /// @note Like meta_type_t::from_type\<T\>(), this is a defensive query and never throws.
        /// @sa meta_type_t::is_valid
        meta_type_t param_type(std::size_t index) const;

        /// @brief Resolve the type of the return value.
        /// @return The corresponding type; an invalid meta_type_t when the handle is not named or the
        ///         return type is not registered under its name.
        /// @note The return column is resolved by name, under its BARE spelling; a void return value
        ///       resolves through the builtin "void" entry, which every registry lookup provides.
        /// @note No object is needed, for the same reason param_type() takes none: the registry is
        ///       process-wide.
        meta_type_t return_type() const;

        /// @}

        /// @name Annotations
        /// @{
        /// @brief Query an annotation's staticised value pointer by its type name.
        /// @param type_name The normalised type name of the carried value ("int",
        ///        "hwshqtb::metaobject::revision_t", "const char*", ...).
        /// @return The value pointer (the staticised object), or nullptr when this method has no
        ///         annotation of that type.
        /// @note The annotations are the [[= make_tag(v)]] / [[= make_str_tag(text)]] attributes of
        ///       the declaration. The pointer refers to a static object of the carried type, so read it
        ///       back through that type:
        ///         * [[= make_tag(revision_t{42, 0})]] -> *(const revision_t*)tag("hwshqtb::metaobject::revision_t")
        ///         * [[= make_str_tag("port_tag")]]  -> *(const char* const*)tag("const char*")  (the
        ///           stored object IS the const char*, hence the double indirection)
        /// @note noexcept. A non-named handle and a miss both answer nullptr; nothing is logged.
        /// @note There is no typed convenience: the caller names the type it expects, which keeps
        ///       the layer generic.
        const void* tag(std::string_view type_name) const noexcept;
        /// @}

        /// @name Argument checking
        /// @{
        /// @brief Whether a given set of argument types matches this method's parameters ON
        ///        @p object_qualifier's object.
        /// @tparam Ts The argument types (as deduced at the call site).
        /// @param object_qualifier The object's OWN cvref, from the typed call site
        ///        (get_param_qualifier\<Obj&&\>()). The default, qualifier_flag::NONE, is "no object
        ///        information at all": the parameter list is still checked, and a REF-QUALIFIED member
        ///        is refused because its reference kind cannot be honoured without an object — pass
        ///        the real qualifier whenever an object exists.
        /// @return true when the handle is named and meta_info_t's compile-time matcher accepts the
        ///         row for these types and this object; false otherwise.
        /// @note The criterion is meta_info_t::check_method_args (BARE type names — top-level cv/ref
        ///       removed — plus check_param_qualifier, plus the object-cvref rule). A synthesised
        ///       accessor has no parameter metadata and is always false.
        /// @note The ACCOMMODATING pass (/*exact=*/false), deliberately: this validates an
        ///       already-selected row rather than ranking overloads, so a `std::string` prvalue must
        ///       match `over(const std::string&)` and an int lvalue a by-value parameter, exactly as
        ///       the typed invoke always accepted. The exact pass is a SELECTION rule and is stated
        ///       by the resolver, not here. The object-cvref rule is unaffected by the pass and
        ///       applies in full.
        /// @note The table lives behind meta_object_t, which this header cannot complete, so the
        ///       lookup goes through the out-of-line owner_table(); the matching itself is the table's
        ///       and is addressed by SLOT — a handle never hands out a table row.
        /// @sa meta_info_t::check_method_args
        template <typename... Ts>
        bool check_args(qualifier_t object_qualifier = qualifier_flag::NONE) const {
            const meta_info_t* const table = owner_table();
            const detail::method_info_t* const row = entry();
            return table != nullptr && row != nullptr
                && table->check_method_args<Ts...>(static_cast<std::size_t>(_index), /*exact=*/false,
                                            object_qualifier);
        }

        /// @}

        /// @name Invocation
        /// @{
        // ---- the parameter order: obj, (out), connectiontype, args ----
        //
        // The object comes FIRST on every form, the out slot (when there is one) IMMEDIATELY after
        // it, the connection type AFTER the out slot, and the arguments LAST. meta_object_t spells
        // the same relative order with its signature / name as the second parameter — obj,
        // name/signature, (out), type, args — so a reader can move between the two levels without
        // re-reading; the handle simply has no name to sit between the object and the out slot.
        // (meta_info_t's erased invoke() is deliberately NOT part of this rule: it is
        // signature-first, a separate channel with no callers.)
        //
        // The shapes are the cross product of
        //   no-out (any_t) / out (bool)  x  typed (Ts&&...) / erased (std::span<const any_t>)  x
        //   Auto (no tag) / caller-chosen tag,
        // which is the eight forms below. Overload hygiene, and why each constraint sits where it
        // does:
        //  - a connection type is excluded as the OBJECT, as the OUT SLOT and from every ARGUMENT
        //    pack (detail::is_connection_type_arg_v). A flag_t is built from its enum implicitly, so
        //    an unguarded tag in a pack binds EXACTLY and beats the tag form's converting parameter
        //    (the Auto form would then forward to itself), and a tag bound to the out slot would be
        //    read as a return slot;
        //  - `any_t` is excluded from every typed out form (detail::is_any_value_v): the ANY_T OUT
        //    SLOT always means the erased-argument overload, because the two put the out slot in
        //    the same (second) position;
        //  - anything convertible to an any_t SPAN is excluded from every typed out slot and
        //    argument pack (detail::is_any_args_v), so the erased overload owns it: a
        //    std::vector&lt;any_t&gt; in the out position would otherwise bind to `Ret&` exactly;
        //  - the TYPED forms exclude an object POINTER (std::is_pointer_v): they hand @c &obj to the
        //    invoker, so a pointer would be the address of the pointer. The ERASED forms accept a
        //    pointer and treat a null one as "there is no object".
        //
        // POSITIONAL TRAP (deliberate, and the price of "the out slot immediately after the
        // object"): in a call with exactly two arguments, the second one is an OUT SLOT whenever it
        // is a non-const lvalue — `invoke(obj, x)` is `bool` and fills @p x. To pass an lvalue as an
        // ARGUMENT (a method taking `int&`, say) name the connection type instead:
        // `invoke(obj, connection_type_flag::DIRECT, x)` (returns any_t), which is the typed tag form and
        // leaves `x` in the argument pack. `std::move(x)` also escapes the out slot and still hands
        // the callee the address of `x`, but it reads as a move and is not the recommended spelling.
        // A lone rvalue (`invoke(obj, 5)`) is an argument, never a slot.

        /// @brief Invoke with one any_t per parameter (the value channel).
        /// @tparam Obj Target type: an object_t / gadget VALUE, or a POINTER to one. A null pointer is
        ///         answered with the INVALID any_t and nothing runs (the honest "there is no object");
        ///         a non-null pointer is used AS the object's address, so it is never mistaken for the
        ///         address of the pointer itself.
        /// @param obj Target object.
        /// @param args One any_t per parameter, in declaration order.
        /// @return The method's result in an any_t: a value-returning entry hands back a constructed
        ///         any_t holding the value, a void entry answers a DECLARED-ONLY void any_t
        ///         (is_valid() == true, is_initialized() == false). The INVALID any_t
        ///         (is_valid() == false) means the call did not run: an invalid handle, a null
        ///         @p obj, or a return type the registry cannot resolve.
        /// @throws bad_any_uninitialized_t If an argument is only declared (its value was never
        ///         constructed).
        /// @throws bad_any_cast_t If the argument count, an argument's type NAME, or an argument's
        ///         qualifier does not match the parameter — the run-time form of check_args, so no
        ///         implicit conversion happens (an int is not accepted for a double parameter).
        /// @note The result is CONSTRUCTED into a slot declared from the row's return type, exactly
        ///       like the `any_t& ret` overload below; that overload is the way to supply the slot
        ///       yourself (and to keep the result type under the caller's control).
        /// @note This is the Direct form: it takes no connection type, so the call runs on the
        ///       calling thread. Use invoke(obj, connection_type_t, args) for a caller-chosen one;
        ///       there the boxed values are copied into the queued event by their declared
        ///       parameter types, the same copies the typed forms make.
        template <typename Obj>
        any_t invoke(Obj&& obj, std::span<const any_t> args) const {
            if (!is_valid()) {
                return any_t{};
            }
            // Declared only, of the row's return type; INVALID for a void row / an unnamed handle (no
            // return column to build a slot from). declare_return_slot() explains the fallback.
            any_t ret = declare_return_slot();
            any_t* const slot = ret.is_valid() ? std::addressof(ret) : nullptr;
            if constexpr (std::is_pointer_v<std::remove_reference_t<Obj>>) {
                if (obj == nullptr) {
                    return any_t{};
                }
                if (!invoke_erased_args(const_cast<void*>(static_cast<const void*>(obj)),
                        detail::target_object(*obj), args, slot, connection_type_flag::DIRECT,
                        detail::object_qualifier_of<Obj>())) {
                    return any_t{};
                }
            }
            else {
                if (!invoke_erased_args(
                        const_cast<void*>(static_cast<const void*>(std::addressof(obj))),
                        detail::target_object(obj), args, slot, connection_type_flag::DIRECT,
                        detail::object_qualifier_of<Obj>())) {
                    return any_t{};
                }
            }
            if (slot == nullptr) {
                return any_t(meta_type_t::from_type<void>());
            }
            // invoke_erased_args() built the result in the slot's storage and marked it constructed.
            return ret;
        }

        /// @brief Invoke with one any_t per parameter and a caller-chosen connection type.
        /// @tparam Obj Target type: an object_t / gadget VALUE, or a POINTER to one, exactly like the
        ///         Direct form above (a null pointer answers the INVALID any_t and nothing runs).
        /// @param obj Target object.
        /// @param type The connection type: Direct, Auto, Queued or BlockingQueued.
        /// @param args One any_t per parameter, in declaration order.
        /// @return The method's result in an any_t, with the same four shapes as the typed
        ///         connection-type form: the value when the call ran synchronously, a DECLARED-ONLY
        ///         void any_t for a void entry, a DECLARED-ONLY any_t of the return type when the
        ///         call was only POSTED (the event writes that storage later), and the INVALID any_t
        ///         when the handle is invalid, @p obj is null, the connection type is refused (one
        ///         Warning: a Queued call on a type with no thread affinity, or a BlockingQueued call
        ///         on the calling thread), or a BlockingQueued call was CANCELLED.
        /// @throws bad_any_uninitialized_t If an argument is only declared.
        /// @throws bad_any_cast_t If the argument count / name / qualifier does not match.
        /// @note The connection decision is the SAME detail::resolve_invoke() the typed forms use —
        ///       one copy of the refusal rules, not a second one — and the queued branch copies the
        ///       boxed values by the entry's declared parameter types (detail::copy_args) exactly
        ///       like the typed branch does. Nothing is registered on the way: the arguments arrived
        ///       as any_t values, so their interfaces already exist under the very names the match
        ///       just compared against the declared column.
        /// @warning A Queued result is delivered through an ADDRESS: the returned any_t's storage is
        ///          what the event writes, so it must not be moved or copied before the event has
        ///          run — the typed form's @warning applies here verbatim.
        template <typename Obj>
        any_t invoke(Obj&& obj, connection_type_t type, std::span<const any_t> args) const {
            // ONE named result and ONE `return ret;` — the NRVO discipline the typed connection-type
            // form documents: a queued call hands the event THIS variable's storage, so it must be
            // the caller's object.
            any_t ret;
            if (!is_valid()) {
                return ret;
            }
            ret = declare_return_slot();
            any_t* const slot = ret.is_valid() ? std::addressof(ret) : nullptr;
            if constexpr (std::is_pointer_v<std::remove_reference_t<Obj>>) {
                if (obj == nullptr) {
                    ret = any_t{};
                    return ret;
                }
                if (!invoke_erased_args(const_cast<void*>(static_cast<const void*>(obj)),
                        detail::target_object(*obj), args, slot, type,
                        detail::object_qualifier_of<Obj>())) {
                    ret = any_t{};
                    return ret;
                }
            }
            else {
                if (!invoke_erased_args(
                        const_cast<void*>(static_cast<const void*>(std::addressof(obj))),
                        detail::target_object(obj), args, slot, type,
                        detail::object_qualifier_of<Obj>())) {
                    ret = any_t{};
                    return ret;
                }
            }
            if (slot == nullptr) {
                // A void row (or a row whose return type the registry cannot resolve): the success
                // shape is the DECLARED-ONLY void any_t, never the invalid one.
                ret = any_t(meta_type_t::from_type<void>());
            }
            return ret;
        }

        /// @brief Invoke with one any_t per parameter, constructing the result into a caller slot.
        /// @tparam Obj Target type (see the two overloads above; a null pointer answers false).
        /// @param obj Target object.
        /// @param ret The caller's DECLARED-BUT-NOT-CONSTRUCTED slot, whose base type must equal the
        ///        method's return type. The thunk constructs the result into its storage
        ///        (operator&()) and the slot is then marked constructed, so is_initialized() answers
        ///        true once the call returns. A void-returning method ignores @p ret and leaves it
        ///        declared only.
        /// @param args One any_t per parameter, in declaration order.
        /// @return true when the entry ran; false for an invalid handle or a null @p obj.
        /// @throws bad_any_uninitialized_t If an argument is only declared.
        /// @throws bad_any_cast_t If the argument count / name / qualifier does not match, or if
        ///         @p ret's base type is not the method's return type.
        /// @throws bad_any_unsupported_t If @p ret already holds a constructed object (the slot must
        ///         be declared only, otherwise the result would be built over a live value).
        /// @note A method whose return type cannot be assigned through the slot (a non-copyable
        ///       reference return) is invoked for its effect, like the typed form: the slot is left
        ///       untouched and NOT marked constructed.
        /// @note The out slot is the SECOND parameter, immediately after the object — the erased
        ///       twin of the typed `Ret&` form, and the same order (obj, out, args).
        template <typename Obj>
        bool invoke(Obj&& obj, any_t& ret, std::span<const any_t> args) const {
            any_t* const slot = std::addressof(ret);
            if constexpr (std::is_pointer_v<std::remove_reference_t<Obj>>) {
                if (obj == nullptr) {
                    return false;
                }
                return invoke_erased_args(const_cast<void*>(static_cast<const void*>(obj)),
                    detail::target_object(*obj), args, slot, connection_type_flag::DIRECT,
                    detail::object_qualifier_of<Obj>());
            }
            else {
                return invoke_erased_args(
                    const_cast<void*>(static_cast<const void*>(std::addressof(obj))),
                    detail::target_object(obj), args, slot, connection_type_flag::DIRECT,
                    detail::object_qualifier_of<Obj>());
            }
        }

        /// @brief The erased-argument form with a caller slot AND a caller-chosen connection type.
        /// @tparam Obj Target type (see the erased overload above; a null pointer answers false).
        /// @param obj Target object.
        /// @param ret The caller's DECLARED-BUT-NOT-CONSTRUCTED slot, of the method's return type.
        /// @param type The connection type: Direct, Auto, Queued or BlockingQueued.
        /// @param args One any_t per parameter, in declaration order.
        /// @return true when the entry ran, was posted, or ran on the target thread (BlockingQueued);
        ///         false for an invalid handle, a null @p obj, a refused connection type, or a
        ///         cancelled blocking call — @p ret is then left exactly as it was.
        /// @note The ERASED return-type rule is the one above, unchanged: bare spellings compared
        ///       against @p ret's base type, mismatch throws bad_any_cast_t. It is deliberately NOT
        ///       the typed form's Critical: the typed form has a compile-time @c Ret to name, the
        ///       erased one only has the caller's boxed slot.
        /// @note A Queued call reports true as soon as the event was posted; @p ret is still declared
        ///       only at that moment, and the event marks it constructed once it wrote the value. The
        ///       variable must therefore outlive the event, exactly like the typed @c Ret& form.
        template <typename Obj>
        bool invoke(Obj&& obj, any_t& ret, connection_type_t type, std::span<const any_t> args) const {
            any_t* const slot = std::addressof(ret);
            if constexpr (std::is_pointer_v<std::remove_reference_t<Obj>>) {
                if (obj == nullptr) {
                    return false;
                }
                return invoke_erased_args(const_cast<void*>(static_cast<const void*>(obj)),
                    detail::target_object(*obj), args, slot, type,
                    detail::object_qualifier_of<Obj>());
            }
            else {
                return invoke_erased_args(
                    const_cast<void*>(static_cast<const void*>(std::addressof(obj))),
                    detail::target_object(obj), args, slot, type,
                    detail::object_qualifier_of<Obj>());
            }
        }

        /// @brief Invoke with a return value.
        /// @tparam Obj Target object type.
        /// @tparam Ret The expected return type.
        /// @tparam Ts Argument types.
        /// @param obj Target object.
        /// @param ret Receives the return value.
        /// @param vs Arguments.
        /// @return true if the call succeeded; false if the handle is invalid, the arguments do not
        ///         match, or the return type differs from @p Ret.
        /// @note Parameter and return-type checks are only performed on named handles; a synthesised
        ///       accessor is called directly.
        /// @note @p obj must be the object itself (an lvalue) and **must not be an object pointer**:
        ///       this template passes @c &obj to the invoker, so a pointer would yield the address of
        ///       the pointer. To pass a pointer, use invoke(Obj&&, std::span&lt;const any_t&gt;).
        /// @note The out slot is the SECOND parameter, immediately after the object — the same
        ///       relative position meta_object_t's `Ret&` forms give it (third there, after the
        ///       name): obj, (out), connectiontype, args. A lone non-const lvalue in that position
        ///       IS the out slot; see the positional trap in the Invocation comment above.
        /// @note This is the Auto form: it forwards to invoke(Obj&&, Ret&, connection_type_t, ...)
        ///       with connection_type_flag::AUTO (Direct on the calling thread, Queued across threads —
        ///       the queued path then fills @p ret when the event runs, so it must outlive the event).
        template <typename Obj, typename Ret, typename... Ts>
            requires(!std::is_pointer_v<std::remove_reference_t<Obj>>
                     && !detail::is_connection_type_arg_v<Obj>
                     && !detail::is_any_value_v<Ret>
                     && !detail::is_any_args_v<Ret>
                     && !detail::is_connection_type_arg_v<Ret>
                     && (!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        bool invoke(Obj&& obj, Ret& ret, Ts&&... vs) const {
            return invoke(std::forward<Obj>(obj), ret, connection_type_flag::AUTO,
                std::forward<Ts>(vs)...);
        }

        /// @brief Invoke without a caller-supplied return slot.
        /// @tparam Obj Target object type.
        /// @tparam Ts Argument types.
        /// @param obj Target object.
        /// @param vs Arguments.
        /// @return As invoke(Obj&&, connection_type_t, Ts&&...) const: the method's result in an
        ///         any_t — the value when it ran synchronously, a declared-only void any_t for a void
        ///         method, a declared-only any_t of the return type when the call was only posted, and
        ///         the INVALID any_t when the handle is invalid or the arguments do not match.
        /// @note Parameter checks are only performed on named handles; a synthesised accessor is
        ///       called directly.
        /// @note @p obj must be the object itself and **must not be an object pointer** (otherwise
        ///       it steals the type-erased overload and then fails silently in the argument check).
        ///       For a pointer (or a null object) use invoke(Obj&&, std::span&lt;const any_t&gt;).
        /// @note This is the Auto form: it forwards to invoke(Obj&&, connection_type_t, ...) with
        ///       connection_type_flag::AUTO, which resolves to Direct when @p obj lives on the calling
        ///       thread and to Queued when it does not.
        template <typename Obj, typename... Ts>
            requires(!std::is_pointer_v<std::remove_reference_t<Obj>>
                     && !detail::is_connection_type_arg_v<Obj>
                     && (!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        any_t invoke(Obj&& obj, Ts&&... vs) const {
            return invoke(std::forward<Obj>(obj), connection_type_flag::AUTO, std::forward<Ts>(vs)...);
        }

        /// @brief Invoke with a caller-chosen connection type.
        /// @tparam Obj Target object type (an object_t, or a gadget value for the Direct path).
        /// @tparam Ts Argument types.
        /// @param obj Target object.
        /// @param type The connection type: Direct, Auto, Queued or BlockingQueued.
        /// @param vs Arguments.
        /// @return The method's result in an any_t, BUILT HERE from the row's declared return type:
        ///         - a value-returning entry that RUNS SYNCHRONOUSLY answers a constructed any_t
        ///           holding the value (Direct, Auto-on-the-calling-thread, or BlockingQueued — the
        ///           target thread writes the slot before the wait returns);
        ///         - a void entry answers a DECLARED-ONLY void any_t
        ///           (any_t(meta_type_t::from_type&lt;void&gt;()): is_valid() == true,
        ///           is_initialized() == false);
        ///         - a value-returning entry whose call was only POSTED (a non-blocking Queued call, or
        ///           Auto that resolved to one) answers a DECLARED-ONLY any_t of its return type: the
        ///           call succeeded, the storage is reserved, and the value arrives when the event runs
        ///           (see the @warning below);
        ///         - the INVALID any_t (is_valid() == false) when the handle is invalid, the arguments
        ///           do not match, the requested type cannot be honoured (one Warning is logged then:
        ///           a Queued call on a type with no thread affinity, or a BlockingQueued call on the
        ///           calling thread, which would deadlock), or when a BlockingQueued call was
        ///           CANCELLED because the receiver died before the event was placed.
        /// @note The checks are the same as the Auto form's and happen here, on the CALLER's side; the
        ///       target thread only replays an already-resolved call.
        /// @note Queued / BlockingQueued copy the arguments into the event by the entry's declared
        ///       parameter types, so a parameter that is not copyable is refused (one Warning).
        /// @note The slot is declared EXACTLY like the erased `any_t& ret` overload's: its storage is
        ///       handed to the thunk as params[0] and marked constructed once the thunk wrote into it.
        ///       A return type the registry cannot resolve (a run-time built row is free to leave it
        ///       unregistered) falls back to the discard shape: the call still succeeds and answers
        ///       the void result, because there is no slot that could be built from a name alone.
        /// @note Every connection type now reaches the same slot. What changes is only WHO fills it:
        ///       Direct fills it inside this call, BlockingQueued before this call returns (the wait
        ///       guarantees it), Queued fills it later, on the receiver's thread.
        /// @warning A Queued value is delivered through an ADDRESS, so the caller owns the lifetime:
        ///          the returned any_t's storage is what the posted event points at, so it MUST NOT be
        ///          moved or copied before the event has run (a declared-only move takes the storage
        ///          with it and leaves the event writing into the moved-from variable). Keep the
        ///          variable in place until `send_posted_events()` / the receiver's loop has processed
        ///          the call; after that is_initialized() answers true and get&lt;T&gt;() returns the
        ///          value. An event that is CANCELLED (the receiver was destroyed, or the queue was
        ///          purged) never writes and never marks, so the result stays declared only.
        /// @note Direct on a target that lives on another thread is honoured as given (the caller asked
        ///       for it); only Auto resolves itself, and it resolves at call time, not at connect time.
        /// @note The out-less twin of invoke(Obj&&, Ret&, connection_type_t, ...): the connection type
        ///       is the SECOND parameter, immediately after the object and before the arguments.
        /// @sa meta_object_t::invoke
        template <typename Obj, typename... Ts>
            requires(!std::is_pointer_v<std::remove_reference_t<Obj>>
                     && !detail::is_connection_type_arg_v<Obj>
                     && (!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        any_t invoke(Obj&& obj, connection_type_t type, Ts&&... vs) const {
            // ONE named result and ONE `return ret;` — no `return any_t{...}` shortcut anywhere below.
            // That is a hard requirement, not a style choice: a queued value-returning call hands this
            // very variable's address to the event, so it must be the CALLER's object. NRVO is what
            // makes it so, and GCC only performs NRVO when every return in the function returns the
            // same local; one early `return any_t{}` would leave the variable to be MOVED into the
            // return slot, and the event would then fill a moved-from object nobody reads.
            any_t ret;
            if (!is_valid()) {
                return ret;
            }
            // "Is this an entry of the table?" is signature().empty(): a synthesised property
            // accessor has a caller but no table row, so it carries no parameter metadata to check.
            // The object's OWN cvref is read HERE, in the typed frame: the handle must refuse a const
            // object a non-const member, and `Obj` is the last point that still knows constness.
            if (!signature().empty() && !check_args<Ts...>(get_param_qualifier<Obj&&>())) {
                return ret;
            }
            // The slot the thunk writes into: declared only, of the row's return type. For a void row
            // (or an unnamed handle) there is no slot: params[0] stays null, as it always was.
            ret = declare_return_slot();
            void* const slot = ret.is_valid() ? &ret : nullptr;
            // params[0] AND the event's return slot are this one storage: a value row writes there on
            // every connection type. (Direct / BlockingQueued write before this function returns;
            // Queued hands the same address to the event, which writes when it runs.)
            void* params[] = { slot, const_cast<void*>(static_cast<const void*>(&vs))... };
            bool deferred = false;
            if (!invoke_erased(type, const_cast<void*>(static_cast<const void*>(&obj)),
                    detail::target_object(obj), params, sizeof...(Ts), slot,
                    get_param_qualifier<Obj&&>(),
                    slot != nullptr ? std::addressof(ret) : nullptr,
                    &detail::register_argument_types<Ts...>, &deferred)) {
                // A refusal (a bad argument copy, a cancelled blocking call, ...): the INVALID state,
                // "nothing ran". No event exists that could point at @p ret, so resetting it is safe.
                ret = any_t{};
                return ret;
            }
            if (slot == nullptr) {
                // A void row (or a row whose return type the registry cannot resolve): the success
                // shape is the DECLARED-ONLY void any_t, never the invalid one. No event can be
                // pointing at @p ret on this path (ret_any was null), so replacing it is safe.
                ret = any_t(meta_type_t::from_type<void>());
                return ret;
            }
            if (deferred) {
                // Posted, not run: the slot keeps its DECLARED-ONLY state, which is the honest "the
                // call succeeded but the value is not here yet" answer. The event holds THIS variable's
                // address (not a copy) and marks it constructed when it writes the value; that is why
                // the returned any_t must not be moved or copied before then.
                return ret;
            }
            // Direct / Auto-on-this-thread / BlockingQueued: the thunk built the object in the slot's
            // storage before this call returned, so record it — is_initialized() answers true and the
            // any_t owns the value.
            ret.mark_constructed();
            return ret;
        }

        /// @brief Invoke with a return value and a caller-chosen connection type.
        /// @tparam Obj Target object type.
        /// @tparam Ret The expected return type.
        /// @tparam Ts Argument types.
        /// @param obj Target object.
        /// @param ret Receives the return value.
        /// @param type The connection type.
        /// @param vs Arguments.
        /// @return As invoke(Obj&&, connection_type_t, Ts&&...) const; in addition the return type is
        ///         checked against @p Ret on named handles.
        /// @note A return-type MISMATCH is a refusal that reports itself: nothing is invoked, @p ret
        ///       stays untouched, and exactly one Critical through log_connection names the row's
        ///       signature, its DECLARED return type and the expected @c Ret spelling (for example
        ///       "invoke: 'add(int,int)' returns 'int', not 'double'"). Only named rows are checked,
        ///       because a synthesised accessor has no return column.
        /// @note The comparison is on BARE spellings: the row's return column keeps its cv/ref
        ///       affixes, so it is peeled with detail::bare_spelling(), while @c Ret is already
        ///       decayed by detail::arg_spelling(). A `const T&`-returning entry is therefore
        ///       capturable by a `T` out-parameter (a value copy) AND by a `const T&` one (the same
        ///       copy into the caller's referent) — both spellings name the type the thunk assigns
        ///       into — while a genuinely different type is still refused. The message quotes the
        ///       row's DECLARED spelling, which is what tells the two apart in a diagnostic.
        /// @note A BlockingQueued call has a return channel: the target thread writes it into @p ret
        ///       before the wait returns, so @p ret is valid immediately after the call and must stay
        ///       alive until then (it is a reference, so it does).
        /// @note A Queued call has one too, but LATER: the event holds @p ret's ADDRESS and the target
        ///       thread fills it when it runs the entry. This call returns true as soon as the event is
        ///       posted, with @p ret still holding whatever the caller put there.
        /// @warning The slot must outlive the event. For Queued that means: keep @p ret alive, and do
        ///          not touch it, until the receiver's loop (or `send_posted_events()`) has processed
        ///          the call — a variable that goes out of scope first leaves the event writing into
        ///          freed storage. An event that is CANCELLED (the receiver died, or the queue was
        ///          purged) never runs, so @p ret is left UNTOUCHED — the same shape as the
        ///          BlockingQueued cancellation below.
        /// @note A BlockingQueued call whose receiver is destroyed (or whose queued event is purged)
        ///       before it runs is CANCELLED: the wait still returns (the event destructor releases
        ///       it), the call reports false and @p ret is left untouched.
        /// @note The full order at this level: object, out slot, connection type, arguments — the
        ///       same rule the Auto form above and meta_object_t::invoke follow.
        template <typename Obj, typename Ret, typename... Ts>
            requires(!std::is_pointer_v<std::remove_reference_t<Obj>>
                     && !detail::is_connection_type_arg_v<Obj>
                     && !detail::is_any_value_v<Ret>
                     && !detail::is_any_args_v<Ret>
                     && !detail::is_connection_type_arg_v<Ret>
                     && (!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        bool invoke(Obj&& obj, Ret& ret, connection_type_t type, Ts&&... vs) const {
            if (!is_valid()) {
                return false;
            }
            // Same "exists in the table" test as invoke(): signature().empty() is false only for a
            // synthesised accessor, whose metadata columns are empty by construction.
            if (!signature().empty()) {
                // Same object-cvref fact as invoke(Obj&&, connection_type_t, Ts&&...), read before
                // `&obj` is erased to void*.
                if (!check_args<Ts...>(get_param_qualifier<Obj&&>())) {
                    return false;
                }
                // BARE spellings on both sides: the row's column keeps its cv/ref affixes
                // ("const std::string&"), Ret is decayed ("std::string"), and detail::bare_spelling()
                // peels one to the other's shape. Comparing the RAW column would make a
                // reference-returning row capturable by nothing at all.
                if (detail::bare_spelling(return_type_name()) != detail::arg_spelling<Ret>()) {
                    // A refusal reports itself: the caller's slot is a Ret, the row's thunk would
                    // write another type through it, so the call is refused BEFORE anything runs and
                    // @p ret stays untouched. Name the row, what it DECLARES and what was asked for.
                    std::string message = "invoke: '";
                    message += signature();
                    message += "' returns '";
                    message += return_type_name();
                    message += "', not '";
                    message += detail::arg_spelling<Ret>();
                    message += "'";
                    HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, message);
                    return false;
                }
            }
            void* params[] = {
                const_cast<void*>(static_cast<const void*>(&ret)),
                const_cast<void*>(static_cast<const void*>(&vs))...
            };
            return invoke_erased(type, const_cast<void*>(static_cast<const void*>(&obj)),
                detail::target_object(obj), params, sizeof...(Ts),
                const_cast<void*>(static_cast<const void*>(&ret)),
                get_param_qualifier<Obj&&>(),
                // No any_t to mark: the caller's own variable is the slot, and the thunk writes a
                // VALUE into it, so there is no "declared only" state to leave behind.
                nullptr,
                &detail::register_argument_types<Ts...>);
        }

        /// @}

        /// @name Invocation of a STATIC member: there is no object
        /// @{
        // The eight forms mirror the instance family exactly — no-out (any_t) / out (bool) x typed
        // (Ts&&...) / erased (std::span<const any_t>) x Auto (no tag) / caller-chosen tag — with the
        // object parameter GONE, because a static member has no implicit object. They are separate
        // functions rather than overloads of invoke(): the first parameter of an invoke() form is the
        // object, and an overload set cannot tell "an object" from "the first argument".
        //
        // The checks are the instance forms' checks (signature().empty(), check_args<Ts...>, the bare
        // return-spelling test, declare_return_slot()) and the dispatch is the SAME invoke_erased():
        // this family adds no second implementation of anything. What differs is the two facts the
        // dispatch needs and cannot read for itself:
        //  - obj is nullptr and target is nullptr — there is no receiver and no thread affinity;
        //  - the call is flagged static, so invoke_erased() can refuse the OTHER misuse direction
        //    (invoke_static on a row that HAS a receiver) at its one choke point.
        // A Queued / BlockingQueued request is therefore REFUSED by the existing resolve_invoke() rule
        // ("the target type has no thread affinity to post to", one Warning) — a static call has no
        // receiver to post to. Direct and Auto (which resolves to Direct here) run on the calling
        // thread.

        /// @brief Invoke a STATIC member with a return value (the Auto form).
        /// @tparam Ts Argument types.
        /// @param vs Arguments.
        /// @return As invoke_static(connection_type_t, Ts&&...) const: the value when it ran
        ///         synchronously, a DECLARED-ONLY void any_t for a void entry, and the INVALID any_t
        ///         when the handle is invalid, the row has a receiver (one Critical: use invoke()),
        ///         the arguments do not match, or the connection type cannot be honoured.
        /// @note A static member has no object, so there is no cvref to check either: the argument
        ///       matcher runs with qualifier_flag::NONE and the row's own cvref column (None) is not
        ///       consulted — the matcher skips the object block for a receiver-less row.
        template <typename... Ts>
            requires((!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        any_t invoke_static(Ts&&... vs) const {
            return invoke_static(connection_type_flag::AUTO, std::forward<Ts>(vs)...);
        }

        /// @brief Invoke a STATIC member with a return value and a caller-chosen connection type.
        /// @param type The connection type: Direct or Auto (Queued / BlockingQueued are refused with
        ///        one Warning — a static call has no receiver to post an event to).
        /// @param vs Arguments.
        /// @return The method's result in an any_t, with the instance form's shapes: a constructed
        ///         any_t holding the value when it ran, a DECLARED-ONLY void any_t for a void entry,
        ///         or the INVALID any_t when nothing ran (invalid handle, a row with a receiver, a
        ///         refused connection type, or an argument mismatch).
        /// @note The NRVO discipline of the instance form applies unchanged: one named result and one
        ///       `return ret;` on every successful shape.
        template <typename... Ts>
            requires((!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        any_t invoke_static(connection_type_t type, Ts&&... vs) const {
            any_t ret;
            if (!is_valid()) {
                return ret;
            }
            // The misuse direction is decided BEFORE the arguments: invoke_static on a row that HAS a
            // receiver is not an argument problem, and reporting it as one would hide it. The same
            // check runs again inside invoke_erased() (the one choke point for both directions); it is
            // idempotent and logs nothing when the call is on the right side.
            if (!check_receiver_mode(/*static_call=*/true)) {
                return ret;
            }
            // No object, hence no cvref: qualifier_flag::NONE is the honest implicit-object value.
            if (!signature().empty() && !check_args<Ts...>(qualifier_flag::NONE)) {
                return ret;
            }
            ret = declare_return_slot();
            void* const slot = ret.is_valid() ? &ret : nullptr;
            void* params[] = { slot, const_cast<void*>(static_cast<const void*>(&vs))... };
            bool deferred = false;
            if (!invoke_erased(type, /*obj=*/nullptr, /*target=*/nullptr, params, sizeof...(Ts), slot,
                    qualifier_flag::NONE, slot != nullptr ? std::addressof(ret) : nullptr,
                    &detail::register_argument_types<Ts...>, &deferred, /*static_call=*/true)) {
                ret = any_t{};
                return ret;
            }
            if (slot == nullptr) {
                ret = any_t(meta_type_t::from_type<void>());
                return ret;
            }
            if (deferred) {
                return ret;
            }
            ret.mark_constructed();
            return ret;
        }

        /// @brief Invoke a STATIC member with a return value (the Auto form of the `Ret&` form).
        /// @tparam Ret The expected return type.
        /// @param ret Receives the return value.
        /// @param vs Arguments.
        /// @return As invoke_static(Ret&, connection_type_t, Ts&&...) const.
        template <typename Ret, typename... Ts>
            requires(!detail::is_any_value_v<Ret>
                     && !detail::is_any_args_v<Ret>
                     && !detail::is_connection_type_arg_v<Ret>
                     && (!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        bool invoke_static(Ret& ret, Ts&&... vs) const {
            return invoke_static(ret, connection_type_flag::AUTO, std::forward<Ts>(vs)...);
        }

        /// @brief Invoke a STATIC member with a caller-supplied return slot and connection type.
        /// @tparam Ret The expected return type.
        /// @param ret Receives the return value; a return-type mismatch is false with @p ret untouched
        ///        and ONE Critical (the instance form's rule, not a second one).
        /// @param type The connection type.
        /// @param vs Arguments.
        /// @return As invoke_static(connection_type_t, Ts&&...) const, plus the return-type check.
        template <typename Ret, typename... Ts>
            requires(!detail::is_any_value_v<Ret>
                     && !detail::is_any_args_v<Ret>
                     && !detail::is_connection_type_arg_v<Ret>
                     && (!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        bool invoke_static(Ret& ret, connection_type_t type, Ts&&... vs) const {
            if (!is_valid()) {
                return false;
            }
            if (!check_receiver_mode(/*static_call=*/true)) {
                return false;
            }
            if (!signature().empty()) {
                if (!check_args<Ts...>(qualifier_flag::NONE)) {
                    return false;
                }
                if (detail::bare_spelling(return_type_name()) != detail::arg_spelling<Ret>()) {
                    std::string message = "invoke_static: '";
                    message += signature();
                    message += "' returns '";
                    message += return_type_name();
                    message += "', not '";
                    message += detail::arg_spelling<Ret>();
                    message += "'";
                    HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, message);
                    return false;
                }
            }
            void* params[] = {
                const_cast<void*>(static_cast<const void*>(&ret)),
                const_cast<void*>(static_cast<const void*>(&vs))...
            };
            return invoke_erased(type, nullptr, nullptr, params, sizeof...(Ts),
                const_cast<void*>(static_cast<const void*>(&ret)), qualifier_flag::NONE, nullptr,
                &detail::register_argument_types<Ts...>, nullptr, /*static_call=*/true);
        }

        /// @brief Invoke a STATIC member with one any_t per parameter (the value channel).
        /// @param args One any_t per parameter, in declaration order.
        /// @return The method's result in an any_t; the INVALID any_t when nothing ran (invalid
        ///         handle, a row with a receiver, an argument mismatch).
        /// @throws bad_any_uninitialized_t If an argument is only declared.
        /// @throws bad_any_cast_t If an argument's count / type name / qualifier does not match.
        /// @note The Direct form, like the instance erased untagged pair; the erased tagged twins are
        ///       below.
        any_t invoke_static(std::span<const any_t> args) const {
            if (!is_valid()) {
                return any_t{};
            }
            any_t ret = declare_return_slot();
            any_t* const slot = ret.is_valid() ? std::addressof(ret) : nullptr;
            if (!invoke_erased_args(nullptr, nullptr, args, slot, connection_type_flag::DIRECT,
                    qualifier_flag::NONE, /*static_call=*/true)) {
                return any_t{};
            }
            if (slot == nullptr) {
                return any_t(meta_type_t::from_type<void>());
            }
            return ret;
        }

        /// @brief Invoke a STATIC member with one any_t per parameter and a caller-chosen type.
        /// @param type The connection type (Direct or Auto in practice).
        /// @param args One any_t per parameter, in declaration order.
        /// @return As invoke_static(std::span<const any_t>) const, with the caller's connection type.
        any_t invoke_static(connection_type_t type, std::span<const any_t> args) const {
            any_t ret;
            if (!is_valid()) {
                return ret;
            }
            ret = declare_return_slot();
            any_t* const slot = ret.is_valid() ? std::addressof(ret) : nullptr;
            if (!invoke_erased_args(nullptr, nullptr, args, slot, type, qualifier_flag::NONE,
                    /*static_call=*/true)) {
                ret = any_t{};
                return ret;
            }
            if (slot == nullptr) {
                ret = any_t(meta_type_t::from_type<void>());
            }
            return ret;
        }

        /// @brief Invoke a STATIC member with one any_t per parameter, into a caller slot.
        /// @param ret The caller's DECLARED-BUT-NOT-CONSTRUCTED slot, of the row's return type.
        /// @param args One any_t per parameter, in declaration order.
        /// @return true when the entry ran; false for an invalid handle, a row with a receiver, or a
        ///         refused / mismatched call.
        /// @throws bad_any_uninitialized_t / bad_any_cast_t / bad_any_unsupported_t As the instance
        ///         erased forms.
        bool invoke_static(any_t& ret, std::span<const any_t> args) const {
            return invoke_erased_args(nullptr, nullptr, args, std::addressof(ret), connection_type_flag::DIRECT,
                qualifier_flag::NONE, /*static_call=*/true);
        }

        /// @brief The tagged twin of the erased `any_t&` form.
        /// @param ret The caller's slot.
        /// @param type The connection type.
        /// @param args One any_t per parameter, in declaration order.
        /// @return As invoke_static(any_t&, std::span<const any_t>) const, with the caller's type.
        bool invoke_static(any_t& ret, connection_type_t type, std::span<const any_t> args) const {
            return invoke_erased_args(nullptr, nullptr, args, std::addressof(ret), type,
                qualifier_flag::NONE, /*static_call=*/true);
        }

        /// @}

        /// @name Equality comparison
        /// @{
        /// @brief Whether two handles point at the same callable entry.
        /// @param lhs Left operand.
        /// @param rhs Right operand.
        /// @return true when the declaring meta-object, the slot and the invoker all match.
        /// @note noexcept; used for Unique duplicate detection on connections and for matching a
        ///       signal when emitting. The owner is the DECLARING meta-object, so a handle taken from
        ///       a derived class for an inherited entry compares equal to the one the base hands out.
        friend bool operator==(const meta_method_t& lhs, const meta_method_t& rhs) noexcept;

        /// @}

    private:
        friend class object_t;
        friend class meta_object_t;
        friend class meta_property_t;
        /// @brief The queued-dispatch event places an already-resolved call on the receiver's thread.
        friend class meta_call_event_t;

        /// @brief Construct from a named entry of the method table.
        /// @param owner The declaring meta-object.
        /// @param index The slot inside that meta-object's method_infos.
        meta_method_t(const meta_object_t* owner, index_t index) noexcept;

        /// @brief Construct from a synthesised property accessor (unnamed).
        /// @param owner The declaring meta-object.
        /// @param caller The synthesised invoker.
        meta_method_t(const meta_object_t* owner, detail::generic_call caller) noexcept;

        /// @brief The declaring table, for the matcher check_method_args<Ts...>() runs.
        /// @return The owner's meta_info_t, or nullptr for an invalid handle.
        /// @note noexcept. Out of line because meta_object_t is incomplete in this header: the table's
        ///       matcher is a member of meta_info_t (which IS complete here), but reaching the table
        ///       through meta_object_t::info() needs the definition.
        const meta_info_t* owner_table() const noexcept;

        /// @brief The run-time half of the connection-type invoke() overloads.
        /// @param type The caller's connection type.
        /// @param obj The target object's address (as the typed templates computed it).
        /// @param target The same target as an object_t, or nullptr for a value with no thread affinity.
        /// @param params generic_call array: params[0] is @p ret_slot, params[1...] the arguments.
        /// @param arg_count The number of arguments.
        /// @param ret_slot The caller's return-value storage, or nullptr for a call that has no slot
        ///        (a void row, an unruly return type, an unnamed handle). It is params[0] on every
        ///        dispatch path, so Direct, BlockingQueued and Queued all write the same place.
        /// @param object_qualifier The object's OWN cvref, computed in the typed caller
        ///        (get_param_qualifier\<Obj&&\>()) before @p obj became a `void*`. It is forwarded to
        ///        invoke_caller(); the argument matcher that consumes it lives above this layer.
        /// @param ret_any The any_t owning @p ret_slot, or nullptr (`Ret&` form, void row). The queued
        ///        non-blocking form hands it to the event, which marks it constructed once the value
        ///        was written; every other path marks it in the caller (or has nothing to mark).
        /// @param register_types Makes the argument types findable by name; called on the Queued branch.
        /// @param deferred When non-null, receives whether the call was only POSTED (a non-blocking
        ///        Queued dispatch, which returns before the target thread runs the entry) rather than
        ///        run inline (Direct) or run and waited for (BlockingQueued). The typed non-`Ret&`
        ///        form uses it to decide between a constructed result and a declared-only one.
        /// @param static_call Whether the caller is invoke_static() (true) or invoke() (false). It is
        ///        the ONE flag the receiver-mode rule reads: `invoke(obj, ...)` on a row with no
        ///        receiver and `invoke_static(...)` on a row that has one are both refused here, with
        ///        one Critical each, so neither direction can be bypassed by reaching this function
        ///        through a different public form.
        /// @return true when the entry ran, was posted, or ran on the target thread; false on a refusal.
        /// @note The handle's own checks ran in the caller, so this only dispatches an
        ///       already-validated call.
        bool invoke_erased(connection_type_t type, void* obj, object_t* target, void** params,
                           std::size_t arg_count, void* ret_slot, qualifier_t object_qualifier,
                           any_t* ret_any = nullptr,
                           detail::argument_registrar register_types = nullptr,
                           bool* deferred = nullptr, bool static_call = false) const;

        /// @brief The run-time body of the four any_t argument invoke() entry points.
        /// @param obj The target object's address, or nullptr (a quiet no-op, as in invoke_caller).
        /// @param target The same target as an object_t, for the thread-affinity decision; nullptr for
        ///        a value with no thread affinity.
        /// @param args One any_t per parameter, in declaration order.
        /// @param ret_any The caller's return slot, or nullptr when the result is discarded (a void
        ///        row, an unregistered return type, the no-out forms that find no slot to build).
        /// @param type The caller's connection type (Direct for the two untagged forms).
        /// @param object_qualifier The object's OWN cvref, computed in the typed entry point before
        ///        @p obj became a `void*`; it is what the argument matcher compares the row's own
        ///        cvref against (a const object must not reach a non-const member).
        /// @param static_call Whether the caller is an invoke_static() form; see invoke_erased().
        /// @return true when the entry ran, was posted, or ran on the target thread; false for an
        ///         invalid handle, a null @p obj, a refused connection type, or a cancelled blocking
        ///         call.
        /// @throws bad_any_uninitialized_t / bad_any_cast_t / bad_any_unsupported_t See the public
        ///         declarations: this is the body that performs those checks and marks @p ret_any
        ///         constructed after the thunk wrote into it — except on the only-POSTED branch,
        ///         where the event is the only place that knows when the value was written, so the
        ///         event marks it (the same split the typed forms use).
        /// @note A STATIC call arrives with @p obj == nullptr and @p static_call == true: there is no
        ///       receiver, so the null-object refusal below does not apply (and the receiver-mode
        ///       check is what refuses the opposite mismatch).
        bool invoke_erased_args(void* obj, object_t* target, std::span<const any_t> args,
                                any_t* ret_any, connection_type_t type,
                                qualifier_t object_qualifier, bool static_call = false) const;

        /// @brief The erased call the library's OWN internals use (queued dispatch, property
        ///        plumbing): params[0] is the return slot, params[1...] the argument addresses.
        /// @param obj The target object's address; nullptr is a quiet no-op.
        /// @param params generic_call array, already built by the caller.
        /// @param object_qualifier The object's OWN cvref where the caller still knows it (the typed
        ///        invoke forms), or qualifier_flag::NONE where it genuinely does not: a queued replay,
        ///        a signal emission and a synthesised property accessor hold only a `void*`/object_t*
        ///        and have no receiver TYPE left to read a cvref from. This entry performs no argument
        ///        matching, so the qualifier is carried for the call chain (and for a future guard),
        ///        not compared here — the refusal already happened in the matcher of the typed or
        ///        any_t entry point that resolved this row.
        /// @note Forwards to the table's primitive, meta_info_t::invoke(slot, obj, params): a handle
        ///       that has a method_infos row names a slot of the declaring table, and THAT table — not
        ///       the handle's stored pointer — is what runs the call (the chain is
        ///       invoke_caller() -> meta_info_t::invoke() -> method_info_t::caller). Only a row-less
        ///       handle (a run-time built property accessor, index_npos) has no slot to give the
        ///       primitive and keeps its own stored thunk.
        /// @note Private on purpose: this is the erased value channel, and it exists only for the
        ///       paths that already sit above method_info_t::caller (a queued call replays boxed any_t
        ///       arguments, the property plumbing hands a raw destination block to a reader). Every
        ///       caller outside the library must go through the typed or the any_t entry points.
        void invoke_caller(void* obj, void** params, qualifier_t object_qualifier) const;

        /// @brief The method_infos row this handle names, or nullptr when it has none.
        /// @return The row inside the declaring meta-object's table; nullptr for an invalid handle, a
        ///         synthesised property accessor (index_npos) and a stale out-of-range slot.
        /// @note noexcept. The one table read every metadata accessor goes through: meta_method_t is
        ///       a friend of meta_object_t and reads the private table directly instead of
        ///       forwarding one query per column.
        const detail::method_info_t* entry() const noexcept;

        /// @brief Whether the row at @p slot accepts the BOXED arguments @p args on an object with
        ///        @p object_qualifier.
        /// @param slot A slot of the declaring table's method_infos; the caller guarantees it is in range
        ///        and that it names a method row.
        /// @param args The boxed values, in declaration order.
        /// @param exact true for the exact pass, false for the accommodating one.
        /// @param object_qualifier The object's OWN cvref (see meta_info_t::method_matches()).
        /// @return false when any argument is DECLARED ONLY (its value was never constructed — there
        ///         is nothing to hand a thunk), otherwise meta_info_t::method_matches()'s verdict over
        ///         the identities the values carry.
        /// @note THE BOXED BRIDGE, and the reason it lives on the HANDLE: meta_info_t must not know
        ///       any_t — that is the value type of the erased channel, not of the table. Each value is
        ///       re-expressed here as cvref_meta_type_t (its recorded type handle plus the cvref it
        ///       was bound with) and meta_info_t::method_matches() decides; this is NOT a third matching
        ///       rule. One std::vector is built per call and reserved once (the count is a run-time
        ///       value, so std::array cannot hold it); no other allocation happens.
        /// @note Private: it is the bridge between the erased invoke and the table, not API.
        /// @note noexcept, like the matcher it forwards to. The single vector allocation is the one
        ///       allocation allowed here; a failed allocation therefore terminates rather than throws.
        /// @note Out of line because it reaches the owner's table through meta_object_t::info(),
        ///       which this header cannot complete.
        /// @sa meta_info_t::method_matches
        bool method_matches_boxed(std::size_t slot, std::span<const any_t> args, bool exact,
                               qualifier_t object_qualifier) const noexcept;

        /// @brief The return slot a call on this handle must build, taken from the row's declared
        ///        return type.
        /// @return A DECLARED-ONLY any_t of the row's return type when the entry returns a value; the
        ///         INVALID any_t for a void entry, for a synthesised accessor (no table row, hence no
        ///         return column) and for a return type the registry cannot resolve.
        /// @note This is the one place the non-`Ret&` invoke() forms turn the metadata column back
        ///       into storage: the caller (a member template, hence a friend of any_t) hands the
        ///       slot's operator&() address to the thunk as params[0] and marks it constructed once
        ///       the thunk wrote into it — the same contract the erased `any_t& ret` overload states.
        /// @note A value row whose return type is not registered (a run-time built table is free to
        ///       leave it as a bare spelling) degrades to the discard shape: there is no interface to
        ///       allocate from a name alone, and the call itself is still a success.
        any_t declare_return_slot() const;

        /// @brief The ONE receiver-mode rule: does this call's form match this row's kind?
        /// @param static_call true when the caller is an invoke_static() form, false for invoke().
        /// @return true when the call may proceed; false when it was refused — and the refusal has
        ///         already logged exactly ONE Critical naming the row and the form that WOULD work
        ///         ("invoke: ... call it with invoke_static(...)" / the mirror image).
        /// @note The two misuse directions are the same question asked twice: a row with no receiver
        ///       (method_kind_enum::STATIC_METHOD) must not be invoked through invoke(obj, ...), and a row
        ///       WITH one must not be invoked through invoke_static(...). Keeping both in this one
        ///       function is what stops the rule from being re-stated in each of the eight public
        ///       forms: invoke_erased() and invoke_erased_args() are the only places that ask, and
        ///       every public form reaches one of them.
        /// @note noexcept in spirit (it may allocate a message string, so it is not marked noexcept).
        bool check_receiver_mode(bool static_call) const;

        /// @brief The declaring meta-object of the entry; nullptr means an invalid handle.
        /// @note The DECLARING one, not the most derived: a handle for an inherited entry points at
        ///       the base class's meta-object, which keeps `index()` (its ancestors' counts plus the
        ///       local slot) and the metadata columns exact.
        const meta_object_t* _owner = nullptr;
        /// @brief The method_infos slot inside _owner's table; index_npos for a synthesised accessor.
        index_t _index = index_npos;

        /// @brief The callable invoker; null only when the entry carries none (a synthesised
        ///        accessor created without one, or an invalid handle).
        detail::generic_call _caller = nullptr;
    };

    namespace detail {
        /// @brief The class and the plain function type named by a pointer-to-member-function type.
        /// @tparam Pm One of the four supported pointer shapes: R (C::*)(Args...), const, noexcept,
        ///         const noexcept.
        /// @note The four partial specialisations are the whole contract: a static member function
        ///       (an ordinary function pointer), a ref-qualified member and a volatile member are
        ///       rejected by the primary template's static_assert.
        template <typename Pm>
        struct member_ptr_traits {
            static_assert(!std::is_same_v<Pm, Pm>,
                "from_method(): Pm must be a pointer to a NON-STATIC member function of one of the "
                "supported shapes: R (C::*)(Args...), R (C::*)(Args...) const, optionally noexcept. "
                "Ref-qualified (&/&&), volatile members and static member functions are not "
                "supported.");
        };

        template <typename R, typename C, typename... Args>
        struct member_ptr_traits<R (C::*)(Args...)> {
            using class_t = C;
            using fn_t = R(Args...);
        };

        template <typename R, typename C, typename... Args>
        struct member_ptr_traits<R (C::*)(Args...) const> {
            using class_t = C;
            using fn_t = R(Args...) const;
        };

        template <typename R, typename C, typename... Args>
        struct member_ptr_traits<R (C::*)(Args...) noexcept> {
            using class_t = C;
            using fn_t = R(Args...) noexcept;
        };

        template <typename R, typename C, typename... Args>
        struct member_ptr_traits<R (C::*)(Args...) const noexcept> {
            using class_t = C;
            using fn_t = R(Args...) const noexcept;
        };

        /// @brief One candidate member of class_t whose function type is exactly fn_t: its pointer
        ///        VALUE plus the normalised signature it resolves to.
        /// @note The members must stay structural: define_static_array requires it (a
        ///       std::string_view is NOT structural, hence the compiling_string_t field).
        template <typename Pm>
        struct method_candidate_t {
            /// @brief `&class_t::<member>` — the value from_method() compares against.
            Pm ptr = nullptr;
            /// @brief The member's normalised signature, in static storage.
            compiling_string_t signature;
        };

        /// @brief Every non-static member function of class_t whose function type is exactly fn_t.
        /// @tparam Pm A supported pointer-to-member-function type.
        /// @return A static table of candidates (pointer value + signature), declaration order.
        /// @note The FUNCTION TYPE is only the compile-time filter that builds the table; the value
        ///       comparison in from_method() is what tells members sharing one function type apart.
        /// @note The guards exclude what must not be a candidate: a static member reflects with the
        ///       same plain function type as a non-static one (is_static_member), and an implicit
        ///       assignment operator is a function without an identifier (has_identifier — otherwise
        ///       signature_of/identifier_of would throw).
        template <typename Pm>
        consteval std::span<const method_candidate_t<Pm>> method_candidates() {
            using class_t = typename member_ptr_traits<Pm>::class_t;
            using fn_t = typename member_ptr_traits<Pm>::fn_t;
            std::vector<method_candidate_t<Pm>> candidates;
            template for (constexpr auto m :
                std::define_static_array(
                    std::meta::members_of(^^class_t, std::meta::access_context::unchecked()))) {
                if constexpr (std::meta::is_function(m) && std::meta::has_identifier(m)
                    && !std::meta::is_static_member(m) && !std::meta::is_constructor(m)
                    && !std::meta::is_destructor(m)) {
                    if constexpr (std::is_same_v<typename[: std::meta::type_of(m) :], fn_t>) {
                        method_candidate_t<Pm> candidate;
                        // `&[: m :]`, NOT `&class_t::[: m :]`: GCC parses the splice as the whole
                        // operand (a qualified splice is a syntax error); the pointer type is Pm
                        // because m is a member of class_t.
                        candidate.ptr = &[: m :];
                        // The single-argument form: a reflection-derived key never consults the
                        // user-text rewriting rules, so no table is needed (the two-argument
                        // overload ignores its table argument).
                        candidate.signature = meta_info_t::signature_of(m);
                        candidates.push_back(candidate);
                    }
                }
            }
            return std::define_static_array(candidates);
        }
    }

    template <typename Pm>
    meta_method_t meta_method_t::from_method(Pm pm) noexcept {
        if (pm == nullptr) {
            return {};
        }
        using class_t = typename detail::member_ptr_traits<Pm>::class_t;
        for (const detail::method_candidate_t<Pm>& candidate : detail::method_candidates<Pm>()) {
            if (pm == candidate.ptr) {
                return class_t::static_meta_object.method(candidate.signature);
            }
        }
        return {};
    }
}

#endif
