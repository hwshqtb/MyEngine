#ifndef HWSHQTB__METAOBJECT__META_OBJECT_T_H
#define HWSHQTB__METAOBJECT__META_OBJECT_T_H

/// @file meta_object_t.h
/// @brief The run-time face of a meta-object (QMetaObject equivalent).

#include "export.h"
#include "any_t.h"
#include "meta_class_info_t.h"
#include "meta_constructor_t.h"
#include "meta_enum_t.h"
#include "meta_method_t.h"
#include "meta_property_t.h"
#include "meta_info_t.h"

#include <array>
#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace hwshqtb::metaobject {
    class object_t;

    class meta_object_t;

    /// @note Only EVER a forward declaration here: dynamic_base_t.h includes this header, so the
    ///       mixin cannot be included back. detail::derives_from_dynamic_base() reads the relation
    ///       only when the mixin is complete in the calling TU.
    class dynamic_base_t;

    namespace detail {
        /// @brief The argument identities an erased argument list carries, in call order.
        /// @param args One any_t per argument.
        /// @return Each boxed value's (bare type, cvref) identity — exactly the pair the typed forms
        ///         build with cvref_meta_type_t::from<Ts>()..., so the erased entry points and the
        ///         typed ones resolve through the SAME rule and cannot disagree about one call.
        /// @note A free function, not a member of meta_object_t: it only unpacks the erased arguments
        ///       and reads no part of the object's state, so it is not the class's responsibility.
        /// @note Not a public way to rebuild an argument list — it is the bridge between the erased
        ///       call entry points and the one chain resolver.
        /// @note Allocates (one std::vector) because the argument count is a run-time value here;
        ///       defined in meta_object_t.cpp.
        std::vector<cvref_meta_type_t> boxed_identities(std::span<const any_t> args);

        /// @brief Whether an argument list is the any_t new_instance call (an any_t span + the out slot).
        /// @note Used to constrain the typed new_instance overload. Without it, a std::vector<any_t> /
        ///       std::array<any_t, N> argument binds the typed form EXACTLY (better than the span's
        ///       conversion) and would be boxed as one single argument instead of selecting the
        ///       constructor by its element list.
        template <typename... Args>
        struct is_any_instance_call_trait : std::false_type { };

        /// @brief Specialisation for the two-argument form: a first argument that converts to the
        ///        any_t span and a second one that IS an any_t.
        template <typename ArgsSpan, typename Out>
        struct is_any_instance_call_trait<ArgsSpan, Out>
            : std::bool_constant<std::is_convertible_v<ArgsSpan, std::span<const any_t>>
                                 && std::is_same_v<std::remove_cvref_t<Out>, any_t>> { };

        /// @brief How @p T's direct bases relate to the meta-object chain.
        /// @note The chain is decided by the ACCESSOR, not by the table: OBJECT declares
        ///       `meta_object()`, GADGET declares only `static_meta_object`. A GADGET ancestor is
        ///       therefore not part of the chain, which is what makes `OBJECT : GADGET` legal (the
        ///       gadget's rows are reached through `GADGET::static_meta_object`).
        struct parent_link_t {
            /// @brief The direct bases that DECLARE meta_object(), in declaration order.
            /// @note Only an OBJECT class reads this list: zero means a root (no OBJECT ancestor),
            ///       one is the parent, two or more is an ambiguous chain.
            /// @note A span over STATIC storage (define_static_array): the value survives as a
            ///       constant expression, which is what lets `static constexpr auto link = ...` work.
            std::span<const std::meta::info> objects{};
            /// @brief A direct base that HAS the accessor without declaring it; empty if none.
            /// @note The illegal "non-OBJECT class between two OBJECT classes" shape: the accessor is
            ///       a public virtual, so having it without declaring it means an OBJECT class sits
            ///       above this base. One level is enough to see it — no walk to the root is needed.
            std::meta::info inherited_only{};
            /// @brief A direct base that DECLARES the accessor, seen from a GADGET class; empty if none.
            /// @note A GADGET is a value type with no polymorphic accessor, so it must not sit under an
            ///       OBJECT class: the inherited meta_object() would report the BASE's table while
            ///       T::static_meta_object is T's own.
            std::meta::info under_object{};
        };

        /// @brief How @p T relates to the meta-object chain, decided in ONE level.
        /// @tparam T The class whose direct bases are reflected, complete at the point of the call.
        /// @return The declaring bases and the two illegal shapes.
        /// @note The chain is a POLYMORPHIC concept: OBJECT declares `meta_object()`, and only such a
        ///       class can be found through a base-typed pointer at run time. GADGET declares only a
        ///       static table, so a gadget is always a root of its own chain — `GADGET : GADGET` is
        ///       allowed, and the derived gadget simply does not see the base's rows.
        /// @note consteval: every caller reads it from a constant expression. The check is structural,
        ///       so it needs no registry and no marker.
        /// @note A privately-inherited OBJECT base makes the accessor call inaccessible, so it lands
        ///       in neither list; such a base is a misuse on its own (meta_object() could not be
        ///       called through it), and the class it belongs to cannot be reached from there anyway.
        /// @note The base SCAN uses access_context::unchecked() on purpose: the chain records which
        ///       classes the class ACTUALLY derives from, not which of those a consumer may name.
        ///       Access is a different question and is answered where it belongs — a private
        ///       interface base is dropped by build_interface_infos (its static_cast would not
        ///       compile for a consumer), while a private OBJECT base still links, and the fact that
        ///       meta_object() cannot be called through it is the caller's problem, not the chain's.
        template <typename T>
        consteval parent_link_t aimed_parent() {
            constexpr bool is_object = detail::declares_meta_object_accessor<T>();
            std::vector<std::meta::info> objects;
            std::meta::info inherited_only{ };
            std::meta::info under_object{ };
            static constexpr auto ctx = std::meta::access_context::unchecked();
            static constexpr auto bases = std::define_static_array(std::meta::bases_of(^^T, ctx));
            template for (constexpr auto base: bases) {
                static constexpr auto base_type = std::meta::type_of(base);
                using B = typename[: base_type :];
                if constexpr (detail::declares_meta_object_accessor<B>()) {
                    if constexpr (is_object) {
                        objects.push_back(base_type);
                    }
                    else if (under_object == std::meta::info{}) {
                        under_object = base_type;
                    }
                }
                else if constexpr (detail::has_meta_object_accessor<B>()) {
                    if (inherited_only == std::meta::info{}) {
                        inherited_only = base_type;
                    }
                }
            }
            return { std::define_static_array(objects), inherited_only, under_object };
        }

        /// @brief A false value that depends on its template arguments.
        /// @note For a static_assert that must fire only when its template is INSTANTIATED: a
        ///       non-dependent false would be rejected while parsing the template itself.
        template <typename...>
        inline constexpr bool dependent_false_v = false;

        /// @brief Whether @p T has a static meta-object table (declared by OBJECT / GADGET, or
        ///        inherited from a reflected base).
        /// @note The test is structural, the same one aimed_parent() uses for a base, so it needs
        ///       no registry and no marker.
        template <typename T>
        inline constexpr bool has_static_meta_object_v = requires {
            { &T::static_meta_object } -> std::same_as<const meta_object_t*>;
        };

        /// @brief Whether @p T can be the OBJECT of a compile-time-name call: a type that names a
        ///        meta-object — it declares (or inherits) a static table, or it reports one through
        ///        meta_object() (a dynamic class, or a plain derived class).
        /// @note This is the compile-time form's own precondition ("Obj must DECLARE its meta-object
        ///       table", "must not derive from dynamic_base_t") expressed as a CONSTRAINT on the
        ///       object position. Every fixed_string_t form takes the object FIRST, so the constraint
        ///       pins the same position in all of them; the `Ret&` reading is (obj, Ret&, ...) and can
        ///       therefore no longer mistake the object for the out slot, but the constraint still
        ///       keeps a non-receiver out of the object position instead of letting it fail later,
        ///       inside the resolution.
        /// @note It does NOT replace the static_asserts in detail::ct_resolve(): a receiver that
        ///       does not DECLARE its table (a plain derived class, a dynamic class) still gets that
        ///       diagnostic, which says what to do instead.
        template <typename T>
        inline constexpr bool is_meta_receiver_v =
            has_static_meta_object_v<std::remove_cvref_t<T>>
            || requires(const std::remove_cvref_t<T>& object) { object.meta_object(); };

        /// @brief The meta-object table an object of type @p U reports.
        /// @param object The object; it may be nullptr (the callers answer nullptr before asking).
        /// @tparam U The source type, as deduced from the pointer.
        /// @return The RUNTIME table when @p U has the virtual meta_object() (an object_t-derived
        ///         class, including a dynamic_base_t override), the STATIC table otherwise — a
        ///         GADGET has no virtual accessor, so its table is the static one.
        /// @note A type with neither is rejected here with a static_assert: it carries no chain to
        ///       search, so an interface cannot be recovered from it either.
        template <typename U>
        const meta_object_t* meta_object_of(U* object) noexcept {
            if constexpr (requires { object->meta_object(); }) {
                return object->meta_object();
            }
            else if constexpr (has_static_meta_object_v<U>) {
                return &U::static_meta_object;
            }
            else {
                static_assert(dependent_false_v<U>,
                    "hwshqtb::metaobject::cast: the source type has neither a meta_object() "
                    "accessor nor a static_meta_object, so it is not a meta type (an "
                    "object_t-derived class or a GADGET); an interface can only be recovered "
                    "from a meta-object chain");
                return nullptr;
            }
        }

        // ---- the compile-time-name form: its pieces ------------------------------------------
        //
        // The runtime-name form walks the meta-object CHAIN (real tables, `_super` links). The
        // compile-time form has to answer the same question before any object exists, so it walks
        // the same chain as TYPES and resolves against the same table DATA rebuilt at compile time:
        // meta_info_t::build_meta<T>() is the very builder META_OBJECT initialises each class's
        // static table from, so the row set — and therefore the slot numbering — is identical by
        // construction, not by a second rule.
        //
        // Why not read `T::static_meta_object` itself? A class's table is `constinit const`
        // (macros.h): a static data member that is `constexpr` cannot have its initialiser in a
        // .cpp, and a constinit object is NOT usable in a constant expression — GCC answers
        // "the value of 'T::static_meta_object' is not usable in a constant expression". The same
        // holds for the type interfaces (type_interface_t::interface_of), so the argument identities
        // cannot be built with cvref_meta_type_t::from<T>() either (it registers: not constexpr).
        // Rebuilding each link with build_meta<T>() and matching with the argument's TYPE is the
        // only way to reach the row data at compile time; the comparison itself is meta_info_t's
        // compile-time matcher (check_args), which this level does not restate.

        /// @brief A string literal as a non-type template parameter.
        /// @tparam N The literal's length, terminator included.
        /// @note STRUCTURAL (one public array member, nothing else), so it is a valid class-type
        ///       NTTP: `resolve_method<"f", Obj, Ts...>()` names the method with no run-time string.
        /// @note The deduction guide below is what lets the literal be written bare — the compiler
        ///       deduces N from the array bound, like the usual `basic_fixed_string` idiom.
        template <std::size_t N>
        struct fixed_string_t {
            /// The characters, terminator included.
            char data[N] = { };

            /// @brief Copy @p text into the array.
            constexpr fixed_string_t(const char (&text)[N]) noexcept {
                for (std::size_t i = 0; i != N; ++i) {
                    data[i] = text[i];
                }
            }

            /// @brief The literal as a view, terminator EXCLUDED.
            /// @note constexpr: the resolution reads it inside the constant evaluation.
            constexpr std::string_view view() const noexcept {
                return std::string_view(data, N - 1);
            }
        };

        /// @brief Deduction guide: `fixed_string_t` written without its length.
        template <std::size_t N>
        fixed_string_t(const char (&)[N]) -> fixed_string_t<N>;

        /// @brief What a compile-time resolution found: which chain link, and which row of it.
        /// @note Plain aggregate: it crosses from the consteval helper to the run-time body as a
        ///       constant, so the body only walks pointers — it never scans a table.
        struct ct_call_site_t {
            /// @brief Links crossed from the class's own table: 0 = own, 1 = its parent, ...
            std::size_t depth = 0;

            /// @brief The slot inside that link's method_infos, or index_npos when nothing matched.
            index_t slot = index_npos;
        };

        /// @brief Whether @p U derives from dynamic_base_t (a class whose table is built at run time).
        /// @note The mixin's header includes this one, so its definition cannot be reached from here —
        ///       and it does not have to be: [meta.rel] requires only the DERIVED class to be
        ///       complete, and a class can only derive from a complete base, so an incomplete
        ///       dynamic_base_t always means "no such base".
        template <typename U>
        consteval bool derives_from_dynamic_base() {
            return std::is_base_of_v<dynamic_base_t, U>;
        }

        /// @brief Walk the STATIC chain of @p U at compile time, own table first.
        /// @tparam U The class whose table is asked now (the recursion's current link).
        /// @param name The BARE member name.
        /// @param object_qualifier The object's own cvref (get_param_qualifier\<Obj&&\>()).
        /// @param depth Links already crossed from the class the caller named.
        /// @return The link and slot of the first table that answers uniquely, or a site whose slot
        ///         is index_npos when the chain ends without an answer.
        /// @throws std::meta::exception On an ambiguity: the walk stops there (a nearer class's
        ///         ambiguity is terminal, exactly like the run-time form) and the message names both
        ///         candidate signatures.
        /// @note The chain is meta_object_t's own linkage rule — detail::aimed_parent<T>() is what the
        ///       consteval table constructor links `_super` from — so the compile-time walk visits
        ///       exactly the links the run-time walk would.
        template <typename U, typename... Ts>
        consteval ct_call_site_t ct_walk(std::string_view name, qualifier_t object_qualifier,
                                       std::size_t depth) {
            const meta_info_t table = meta_info_t::build_meta<U>();
            // The table answers with its OWN compile-time matcher: there is no second loop here.
            const method_resolution_t found = table.resolve_method_types<Ts...>(name, object_qualifier);
            if (found.ambiguous) {
                const std::string message = [&] {
                    std::string text = "resolve_method: ambiguous '";
                    text += name;
                    text += "' on class '";
                    text += std::string_view(table.class_name);
                    text += "': '";
                    text += std::string_view(table.method_at(static_cast<std::size_t>(found.slot)).signature);
                    text += "' and '";
                    text += std::string_view(table.method_at(static_cast<std::size_t>(found.other)).signature);
                    text += "' (compile-time name form)";
                    return text;
                }();
                throw std::meta::exception(std::define_static_string(message), ^^U);
            }
            if (found.slot != index_npos) {
                return ct_call_site_t{ depth, found.slot };
            }
            constexpr auto link = aimed_parent<U>();
            if constexpr (link.objects.size() == 1) {
                using parent_t = typename[: link.objects[0] :];
                return ct_walk<parent_t, Ts...>(name, object_qualifier, depth + 1);
            }
            else {
                // The chain ends here (a root class, or a GADGET — which has no chain at all).
                return ct_call_site_t{ 0, index_npos };
            }
        }

        /// @brief The miss diagnostic, spelled like the run-time form's Warning.
        /// @note The arguments are spelled BARE (detail::arg_spelling), which is the spelling the
        ///       table compares; the run-time form's spelled() adds the argument's own cvref.
        /// @note The class is spelled QUALIFIED (meta_info_t::original_name), where the run-time form
        ///       prints class_name(): a compile-time diagnostic has no other context to name the
        ///       class with, and two same-named classes in different namespaces must stay apart.
        template <typename... Ts>
        consteval std::string ct_miss_message(std::string_view name, std::string_view class_name) {
            constexpr std::array<std::string_view, sizeof...(Ts)> spellings{ arg_spelling<Ts>()... };
            std::string message = "resolve_method: no method '";
            message += name;
            message += "' on class '";
            message += class_name;
            message += "' accepts the arguments (";
            for (std::size_t i = 0; i != spellings.size(); ++i) {
                if (i != 0) {
                    message += ", ";
                }
                message += spellings[i];
            }
            message += ") (compile-time name form)";
            return message;
        }

        /// @brief Resolve a compile-time call against @p Obj's STATIC chain.
        /// @tparam Obj The object's type as a forwarded parameter would be written.
        /// @tparam Ts The argument types, cvref included.
        /// @return The link and slot of the unique match (never index_npos: a miss throws).
        /// @note The two static_asserts are the form's preconditions, worded so the compiler prints
        ///       what to do instead. Both are dependent on Obj, so they fire on instantiation only.
        /// @throws std::meta::exception On a miss (naming the class and the argument spellings) or on
        ///         an ambiguity (named by ct_walk). The class reflection is the exception's location,
        ///         the idiom meta_info_t::build_meta<T>() uses for its own rejections.
        template <typename Obj, typename... Ts>
        consteval ct_call_site_t ct_resolve(std::string_view name) {
            using bare_object = std::remove_cvref_t<Obj>;
            static_assert(declares_static_meta_object<bare_object>(),
                "resolve_method<Name, Obj, Ts...>: Obj must DECLARE its own meta-object table (the "
                "OBJECT or GADGET macro); a plain derived class only inherits one, so the chain the "
                "compile-time walk searches would belong to another class. Declare OBJECT/GADGET on "
                "it, or name the class that declared the table");
            static_assert(!derives_from_dynamic_base<bare_object>(),
                "resolve_method<Name, Obj, Ts...>: Obj must not derive from dynamic_base_t — such a "
                "class reports a meta-object built at RUN time, which no compile-time walk can see. "
                "Use the runtime-name form resolve_method(name, args) for it");

            // Guarded so a rejected call prints ONE diagnostic (the static_assert above), not that
            // message plus a pointless "no method" from a walk that was never valid.
            if constexpr (declares_static_meta_object<bare_object>()
                          && !derives_from_dynamic_base<bare_object>()) {
                constexpr qualifier_t object_qualifier = get_param_qualifier<Obj&&>();
                const ct_call_site_t found =
                    ct_walk<bare_object, Ts...>(name, object_qualifier, 0);
                if (found.slot == index_npos) {
                    throw std::meta::exception(
                        std::define_static_string(
                            ct_miss_message<Ts...>(name, meta_info_t::original_name<bare_object>())),
                        ^^bare_object);
                }
                return found;
            }
            else {
                // Unreachable: the static_asserts above already rejected the call.
                return ct_call_site_t{ };
            }
        }
    }

    /// @brief Run-time queries over a meta_info_t (the counterpart of QMetaObject).
    ///
    /// meta_info_t holds the compile-time information of ONE class: the tables, the class name and
    /// the invocation machinery. meta_object_t is the run-time face AND the chain: it OWNS its table
    /// by value, keeps a pointer to the base class's meta-object, and walks those tables to answer
    /// "which method is this", "which property is that".
    ///
    /// @par Shape: a chain of real meta-objects, never copies
    /// A meta_object_t is a complete meta-object, not a handle onto one somebody else keeps alive.
    /// It is deliberately NON-COPYABLE and NON-MOVABLE (Qt passes QMetaObject by pointer for the same
    /// reason): every link of the inheritance chain is a real meta_object_t — the base class's own
    /// `static_meta_object`, or an arena entry from meta_object_builder_t — and `_super` points at
    /// it.
    ///
    /// @par Ownership and handle lifetime
    /// meta_method_t / meta_constructor_t / meta_property_t / meta_enum_t / meta_class_info_t store a
    /// `const meta_object_t*` — the DECLARING meta-object of the chain — plus their local index. A
    /// handle is therefore valid while that meta-object lives: in practice the class's
    /// `static_meta_object`, the arena entry a meta_object_builder_t::build() returns, or any base
    /// link reachable from either, all of which outlive any handle taken from them.
    ///
    /// @par Calling a method: invoke, call, resolve_method
    ///  - invoke(obj, signature, [Ret&], [connection_type_t], args...) is SIGNATURE-based (the row is
    ///    named by its normalised signature) and answers the result as an any_t;
    ///  - call(obj, name, [Ret&], [connection_type_t], args...) is NAME-based: the bare name plus the
    ///    arguments pick the overload; it resolves ONCE and hands the call to the handle's invoke()
    ///    (the handle repeats this same relative order: obj, (out), connectiontype, args);
    ///  - resolve_method(...) only RESOLVES: it answers the handle (invalid on a miss / ambiguity,
    ///    reported once) without invoking anything.
    /// Every form takes the OBJECT FIRST, like QMetaObject::invokeMethod. The object is the one
    /// parameter no call can omit, and keeping the out slot out of the first position is also what
    /// stops a `std::string` out slot from colliding with the `std::string_view` name (see the
    /// invocation section below).
    /// The any_t answer is a CONSTRUCTED value, a DECLARED-ONLY void (is_valid() &&
    /// !is_initialized()), the INVALID any_t on failure, or — when a non-blocking Queued call was only
    /// POSTED — a DECLARED-ONLY any_t of the row's return type (the event fills it when it runs, so
    /// that variable must not be moved or copied before then).
    ///
    /// @par Compile-time names and the STATIC chain
    /// call\<"f"\>(...) and resolve_method\<"f"\>(...) resolve at COMPILE time over the object type's
    /// STATIC chain, so they refuse a dynamic_base_t object (static_assert): that table is built at run
    /// time. The runtime-name forms are the ones to use for such objects.
    ///
    /// @par Connection types and the return channel
    /// A Queued call carries a return value like every other type: the event holds the caller's slot
    /// (`Ret&`'s variable, or the returned any_t's storage) and fills it when the entry runs, so the
    /// call is posted successfully and the value is simply not there yet. The slot must outlive the
    /// event. BlockingQueued on the calling thread is skipped (one Warning: it would wait for the loop
    /// it blocks); BlockingQueued across threads delivers the value into Ret& before it returns.
    ///
    /// @note Run-time type: no public query is constexpr. Compile-time work stays on meta_info_t.
    class HWSHQTB_EXPORT meta_object_t {
    public:
        /// @brief Tag that carries the class under construction into the consteval constructor.
        /// @tparam T The class whose `static_meta_object` is being defined.
        /// @note Carries T into the constructor, which reflects T's direct bases and links the base
        ///       class's `static_meta_object` in as `_super`.
        /// @note Public because META_OBJECT names it in the translation unit that defines a class's
        ///       `static_meta_object`, and the macro must not depend on a private name; it stays an
        ///       implementation detail of META_OBJECT by convention.
        template <typename T>
        struct identity_class_t { };

        /// @brief Compile-time construction that links `_super` to the meta-object of T's base.
        /// @param info The table, by value, as built by meta_info_t::build_meta<T>().
        /// @tparam T The class under construction, carried by the identity_class_t tag META_OBJECT
        ///         passes.
        /// @note Compile-time only: META_OBJECT initialises `static_meta_object` from it. T's direct
        ///       bases are reflected; a class with no bases gets nullptr.
        /// @note The `noexcept` is moot: every failure is a std::meta::exception raised during
        ///       constant evaluation, so it makes the build fail with the message instead of
        ///       propagating to run time.
        /// @note Rejection conditions, both build errors naming the class:
        ///        - T has bases, but none of them declares a meta-object (interface-only bases, for
        ///          example: an interface is a base carrying make_interface, and it is not a
        ///          meta-object), so there is no base to link;
        ///        - T has two or more meta-object bases, so there is no single base to link.
        template <typename T>
        consteval meta_object_t(meta_info_t info, identity_class_t<T>) noexcept:
            _info(std::move(info)) {
            static constexpr auto link = detail::aimed_parent<T>();
            if constexpr (!detail::declares_meta_object_accessor<T>()) {
                // GADGET (or a plain class): no polymorphic accessor, so it is a root of its own
                // chain. Inheriting an OBJECT class is the illegal shape — the inherited
                // meta_object() would report the BASE's table while T::static_meta_object is T's own.
                if constexpr (link.under_object != std::meta::info{}
                              || link.inherited_only != std::meta::info{}) {
                    constexpr auto culprit = (link.under_object != std::meta::info{})
                        ? link.under_object
                        : link.inherited_only;
                    throw std::meta::exception(
                        std::define_static_string(
                            std::string("class '") + std::string(std::meta::display_string_of(^^T))
                            + "' is a GADGET, but its base '" + std::string(std::meta::display_string_of(culprit))
                            + "' carries OBJECT (declared there or inherited): a GADGET is a value type "
                              "with no polymorphic meta_object(), so it cannot sit under an OBJECT "
                              "class. Use OBJECT for this class instead"),
                        ^^T);
                }
            }
            else if constexpr (link.inherited_only != std::meta::info{}) {
                // A base that INHERITS the accessor without declaring it sits below an OBJECT class:
                // linking across it would name an ancestor as the parent while a real class of the
                // chain is skipped, so the chain would answer for the wrong level.
                throw std::meta::exception(
                    std::define_static_string(
                        std::string("class '") + std::string(std::meta::display_string_of(^^T))
                        + "' declares OBJECT, but its base '" + std::string(std::meta::display_string_of(link.inherited_only))
                        + "' does not: that base inherits meta_object() from an OBJECT ancestor, so a "
                          "non-OBJECT class would sit between two OBJECT classes. Declare OBJECT on "
                          "that base as well, or drop it from the chain"),
                    ^^T);
            }
            else if constexpr (link.objects.size() > 1) {
                // Several OBJECT bases: name the count and the bases themselves.
                std::string bases;
                for (auto base: link.objects) {
                    if (!bases.empty())
                        bases += ", ";
                    bases += std::meta::display_string_of(base);
                }
                std::string count;
                for (std::size_t left = link.objects.size(); left != 0; left /= 10)
                    count.insert(count.begin(), static_cast<char>('0' + left % 10));
                throw std::meta::exception(
                    std::define_static_string(
                        std::string("class '") + std::string(std::meta::display_string_of(^^T))
                        + "' has " + count + " bases that declare OBJECT (" + bases
                        + "); an OBJECT class has at most one"),
                    ^^T);
            }
            else if constexpr (link.objects.size() == 1) {
                // Exactly one OBJECT base: link it. Taking its address is a constant expression
                // because the base's static_meta_object is constant-initialised (OBJECT's constinit).
                _super = &[: link.objects[0] :]::static_meta_object;
            }
            // else: no base declares OBJECT — a root class, or one that only inherits GADGET /
            // plain classes. The chain of one is valid; _super stays nullptr.
        }

        /// @brief Take ownership of a meta-info table and link it to a base meta-object.
        /// @param info The table, by value; it is moved into this meta-object.
        /// @param super The base class's meta-object, or nullptr for a root class. The pointer is
        ///        stored as-is: a meta-object is neither copied nor moved into the chain, so the
        ///        caller keeps it alive (a class's `static_meta_object`, an arena entry, ...).
        /// @note noexcept. constexpr so that a `const` meta_object_t initialised from a consteval
        ///       build_meta<T>() is constant-initialised (no run-time constructor order to get wrong).
        /// @note This is the run-time path (meta_object_builder_t, tests); the OBJECT macro uses the consteval
        ///       identity_class_t overload above, which DISCOVERS super instead of being told it.
        constexpr explicit meta_object_t(meta_info_t info, const meta_object_t* super = nullptr) noexcept:
            _info(std::move(info)),
            _super(super) { }

        // Non-copyable and non-movable, on purpose (Qt's QMetaObject is passed by pointer):
        //  - every chain link must be THE base class's own meta-object, not a copy whose identity
        //    differs from the one the base hands out (handles are compared by owner pointer);
        //  - the object's address is what handles and `_super` links point at, so it must never move.
        meta_object_t(const meta_object_t&) = delete;
        meta_object_t& operator=(const meta_object_t&) = delete;
        meta_object_t(meta_object_t&&) = delete;
        meta_object_t& operator=(meta_object_t&&) = delete;

        // ---- identity ----

        /// @brief The class name (QMetaObject::className).
        /// @note noexcept. The identifier alone, e.g. "widget_t": use qualified_class_name() when the
        ///       namespace matters (keys, diagnostics, cross-namespace lookups).
        std::string_view class_name() const noexcept;

        /// @brief The namespace-qualified class name, e.g. "my::ui::widget_t".
        /// @note noexcept. A class in the global namespace answers exactly class_name(), so the two
        ///       agree for every type declared at global scope.
        std::string_view qualified_class_name() const noexcept;

        /// @brief The meta-object of the direct base class (QMetaObject::superClass).
        /// @return The base class's OWN meta-object (the one its accessor hands out), or nullptr when
        ///         this class has no base class.
        /// @note noexcept.
        /// @note A POINTER, not a value: every link of the chain is a real meta-object, so the
        ///       returned pointer stays identical to `&Base::static_meta_object`.
        /// @note The chain is acyclic by construction (a base cannot derive from its derived class),
        ///       so walking `super_class()` terminates.
        const meta_object_t* super_class() const noexcept;

        /// @brief Whether this class has a base class.
        /// @note noexcept.
        bool has_super_class() const noexcept;

        /// @brief Whether this class inherits @p base (a class inherits itself).
        /// @param base The base class's meta-object; nullptr answers false.
        /// @note noexcept. Pointer-taking on purpose: the chain is a graph of real meta-objects, so
        ///       the question is asked with the identity the caller already holds (e.g.
        ///       `&Base::static_meta_object`). A null pointer is answered with false instead of
        ///       being dereferenced.
        bool inherits(const meta_object_t* base) const noexcept;

        /// @brief Whether this class inherits the class named @p class_name.
        /// @note noexcept.
        bool inherits(std::string_view class_name) const noexcept;

        // ---- methods ----

        /// @brief Number of methods (methods and signals) including those of every base class.
        /// @note noexcept. Constructors are NOT counted (Qt's methodCount() counts methods only):
        ///       they have their own table and their own counter, constructor_count().
        std::size_t method_count() const noexcept;

        /// @brief Look a method up by its normalised signature.
        meta_method_t method(std::string_view signature) const;

        /// @brief Look a signal up by its normalised signature.
        meta_method_t signal(std::string_view signature) const;

        /// @brief The method at a global index.
        /// @note noexcept.
        meta_method_t method(index_t index) const noexcept;

        /// @brief Global index of a method, or index_npos.
        /// @note noexcept.
        index_t index_of_method(std::string_view signature) const noexcept;

        /// @brief Global index of a signal, or index_npos.
        /// @note noexcept.
        index_t index_of_signal(std::string_view signature) const noexcept;

        // ---- overload resolution (the chain level) ----
        //
        // meta_info_t::resolve_method() answers from ONE table and is silent. This layer is where the
        // chain and the diagnostics live: it asks `this` first, then super_class(), and turns an
        // ambiguity or a miss into a log record and an invalid handle. The table-level primitive
        // stays the only place that decides WHICH row a call means.

        /// @brief Resolve @p name against this table, then its ancestors (own table first).
        /// @tparam Obj The object's type as a forwarded parameter would be written: `widget_t`,
        ///         `const widget_t&`, `widget_t&&`, `volatile widget_t&`. Only its OWN cvref
        ///         (get_param_qualifier\<Obj&&\>()) is read; no object is needed.
        /// @tparam Ts The argument types, written the same way (cvref included).
        /// @param name The BARE member name to resolve ("f"); it is not a signature key.
        /// @return The handle of the unique match, or an INVALID handle when nothing matched or the
        ///         match was ambiguous (one diagnostic then: Critical for an ambiguity naming both
        ///         candidates, Warning for a miss naming the arguments and the class).
        /// @note Own-first: `this` is asked before super_class(), exactly like method(). The FIRST
        ///       table that answers wins — a unique hit stops the walk.
        /// @note An `ambiguous` answer from a table is TERMINAL: a nearer class's ambiguity means the
        ///       name is ambiguous at that level, so the base is never consulted.
        /// @note The returned handle names the DECLARING table, not `this`: an inherited row is
        ///       answered by the ancestor whose table holds it.
        /// @note NOT noexcept: from\<T\>() may register a type, the std::array of argument identities
        ///       is built on the stack, and both diagnostics format a std::string. Nothing here
        ///       throws by itself, but the calls it makes are not noexcept, so neither is this.
        /// @note The typed form is a thin front end: it builds exactly the identities the erased form
        ///       takes, so the two entry points cannot disagree about the same call.
        template <typename Obj, typename... Ts>
        meta_method_t resolve_method(std::string_view name) const {
            const std::array<cvref_meta_type_t, sizeof...(Ts)> args{ cvref_meta_type_t::from<Ts>()... };
            return resolve_method<Obj>(name, std::span<const cvref_meta_type_t>(args));
        }

        /// @brief The erased form: the arguments are already (type, cvref) pairs.
        /// @tparam Obj The object's type, read exactly as the typed form reads it.
        /// @param name The BARE member name to resolve ("f"); it is not a signature key.
        /// @param args The call's arguments, as (bare type, cvref) identities, in call order.
        /// @return The handle of the unique match in the first table that answers, or an invalid
        ///         handle on a miss or an ambiguity (one diagnostic, at the level named above).
        /// @note `Obj` supplies itself: get_param_qualifier\<Obj&&\>() is forwarded to the overload
        ///       below, which is where the walk lives — one implementation, two front ends (this one
        ///       and the explicit-qualifier one the STATIC call family uses, where there is no object
        ///       to read a cvref from).
        template <typename Obj>
        meta_method_t resolve_method(std::string_view name, std::span<const cvref_meta_type_t> args) const {
            return resolve_method(name, args, get_param_qualifier<Obj&&>());
        }

        /// @brief The erased form with an EXPLICIT object qualifier: the walk itself.
        /// @param name The BARE member name to resolve ("f"); it is not a signature key.
        /// @param args The call's arguments, as (bare type, cvref) identities, in call order.
        /// @param object_qualifier The object's OWN cvref, or qualifier_flag::NONE when there is no
        ///        object — the static-call family's value, and the honest one: a row with no receiver
        ///        skips the object-cvref rule entirely (see the matchers), so None cannot refuse it,
        ///        while a row WITH a receiver is matched as if on a plain object.
        /// @return The handle of the unique match in the first table that answers, or an invalid
        ///         handle on a miss or an ambiguity (one diagnostic, at the level named above).
        /// @note The walk is `this`, then super_class() until the chain ends; it allocates nothing
        ///       except a diagnostic message on the two failure paths.
        meta_method_t resolve_method(std::string_view name, std::span<const cvref_meta_type_t> args,
                                     qualifier_t object_qualifier) const {
            for (const meta_object_t* table = this; table != nullptr; table = table->super_class()) {
                const method_resolution_t found =
                    table->info().resolve_method(name, args, object_qualifier);
                if (found.ambiguous) {
                    // A nearer class's ambiguity is terminal: the base's rows never make a name that
                    // is already ambiguous one level up unique. Report both candidates, then stop.
                    std::string message = "resolve_method: ambiguous '";
                    message += name;
                    message += "' on class '";
                    message += class_name();
                    message += "': '";
                    message += std::string_view(table->info().method_at(static_cast<std::size_t>(found.slot)).signature);
                    message += "' and '";
                    message += std::string_view(table->info().method_at(static_cast<std::size_t>(found.other)).signature);
                    message += "'";
                    HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, message);
                    return meta_method_t{};
                }
                if (found.slot != index_npos) {
                    // The DECLARING table, which is `table`, not necessarily `this`.
                    return meta_method_t(table, found.slot);
                }
            }
            std::string message = "resolve_method: no method '";
            message += name;
            message += "' on class '";
            message += class_name();
            message += "' accepts the arguments (";
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (i != 0) {
                    message += ", ";
                }
                const std::string spelling = args[i].spelled();
                message += spelling.empty() ? std::string("<unknown>") : spelling;
            }
            message += ")";
            HWSHQTB_LOG_RAW(log_connection(), log_level_enum::WARNING, message);
            return meta_method_t{};
        }

        /// @brief Resolve by a COMPILE-TIME name, at COMPILE TIME, over the object class's STATIC chain.
        /// @tparam Name The method name as a string literal (a fixed_string_t NTTP).
        /// @tparam Obj The object's type as a forwarded parameter would be written (`widget_t`,
        ///         `const widget_t&`, `widget_t&&`, `volatile widget_t&`); only its cvref
        ///         (get_param_qualifier\<Obj&&\>()) is read.
        /// @tparam Ts The argument types, cvref included.
        /// @return The handle of the unique match.
        /// @note The resolution runs in a constant expression over `Obj`'s STATIC chain, so a miss or
        ///       an ambiguity is a COMPILE-TIME error (one std::meta::exception naming the involved
        ///       signatures), never a runtime log. The chain is walked as TYPES own-first —
        ///       detail::aimed_parent<T>() is the same linkage the consteval table constructor
        ///       follows — and each link's table is REBUILT with meta_info_t::build_meta<T>(), which
        ///       is the builder `static_meta_object` was initialised from; the slot numbering is
        ///       therefore the static table's. (`static_meta_object` itself is `constinit`, not a
        ///       constant expression, so its rows cannot be read directly — see the detail notes.)
        /// @note `Obj` must DECLARE `static_meta_object` and must not derive from `dynamic_base_t`: a
        ///       dynamic base reports a run-time built table, which no compile-time walk can see — use
        ///       the runtime-name form for those. Both conditions are static_asserts.
        /// @note `*this` must BE `Obj`'s static table; any other receiver is a Critical and an invalid
        ///       handle, because the answer would describe a different table.
        /// @note The run-time part is a pointer walk of the depth the compile-time pass recorded plus
        ///       the handle construction: NO table scan, no name comparison, no argument comparison.
        /// @note The argument facts are the same ones the runtime form takes: for each `Ts`, its bare
        ///       spelling (detail::arg_spelling) and its cvref (get_param_qualifier<Ts>()).
        template <detail::fixed_string_t Name, typename Obj, typename... Ts>
        meta_method_t resolve_method() const {
            constexpr detail::ct_call_site_t found = detail::ct_resolve<Obj, Ts...>(Name.view());

            if (this != &std::remove_cvref_t<Obj>::static_meta_object) {
                // The handle would name a table of another class: the slot belongs to THAT class's
                // row order, so answering here would hand back a row the caller never asked for.
                std::string message = "resolve_method: the compile-time name form was called on class '";
                message += class_name();
                message += "', but Obj is '";
                message += std::remove_cvref_t<Obj>::static_meta_object.class_name();
                message += "': the answer describes another table";
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, message);
                return meta_method_t{};
            }

            // The compile-time pass recorded WHICH link answered; walk that many `_super` links. The
            // chain is the same linkage the consteval constructor built, so the walk cannot run out.
            const meta_object_t* table = this;
            for (std::size_t crossed = 0; crossed != found.depth; ++crossed) {
                table = table->super_class();
                if (table == nullptr) {
                    return meta_method_t{};
                }
            }
            return meta_method_t(table, found.slot);
        }

        // ---- constructors ----
        //
        // A constructor is NOT a method: it has its own handle (meta_constructor_t), its own table
        // (meta_info_t::constructor_infos) and its own index space, exactly as in Qt, where
        // methodCount() / method(i) cover methods only and constructorCount() / constructor(i)
        // cover constructors. The two spaces are counted independently and constructors are never
        // inherited, so every query here reads this class's OWN table. Signatures keep the bare
        // class-name form "class_t(int,int)".

        /// @brief Number of constructors this class declares.
        /// @note noexcept. The counterpart of QMetaObject::constructorCount(), and the size of this
        ///       class's OWN constructor table: constructors are not inherited, so no base class's
        ///       constructors are counted. Only constructors marked [[= make_hidden()]] are absent. A
        ///       copy / move / deleted / non-public constructor and the constructors of an abstract
        ///       class ARE counted — the table describes what the class declares; new_instance()
        ///       reports the ones that cannot be called.
        std::size_t constructor_count() const noexcept;

        /// @brief The constructor at a constructor index.
        /// @param index The index inside the CONSTRUCTOR space — the value index_of_constructor()
        ///        returns, bounded by constructor_count().
        /// @return The handle; an invalid handle when @p index is out of range.
        /// @note noexcept. The counterpart of QMetaObject::constructor(index). @p index is NOT a
        ///       method index: method(index) does not accept it, and this one does not accept a
        ///       method coordinate.
        meta_constructor_t constructor(index_t index) const noexcept;

        /// @brief Constructor index of a constructor, or index_npos.
        /// @param signature The normalised signature, of the method form "class_t(int,int)".
        /// @note noexcept. The answer lives in the constructor space (it is the slot inside
        ///       constructor_infos), so it is directly usable with constructor(index) — unlike
        ///       index_of_method(), whose space spans the whole inheritance chain.
        index_t index_of_constructor(std::string_view signature) const noexcept;

        /// @brief Construct a new instance from one any_t per constructor parameter.
        /// @param args One any_t per parameter, in declaration order. The constructor is SELECTED by
        ///        this list from this class's OWN constructor table (constructor_infos): a parameter matches
        ///        when its BARE type name equals the argument's type
        ///        name and the parameter's cvref accepts the argument's qualifier (exactly like a
        ///        call), with an exact reference-kind match preferred — so an lvalue argument selects
        ///        the copy constructor and an rvalue the move one. No implicit conversion: an int
        ///        argument never selects a `double` parameter.
        /// @param out The caller's ALIAS slot, `std::ref(pointer_variable)`, whose variable is a
        ///        POINTER. The constructor thunk writes the new instance's address through it —
        ///        operator&() reaches the aliased variable — so after a successful call that variable
        ///        holds the instance, owned by the caller (destroy it with an ordinary `delete`).
        /// @return true when the instance was built and stored through @p out. False on every failure,
        ///         each reported through log_connection: no constructor matches the argument list
        ///         (Warning — the caller may have named the wrong types), the constructor exists but
        ///         cannot be called, that is a deleted one or any constructor of an abstract class
        ///         (Critical, and nothing is built), or the constructor threw (Critical; `new` released
        ///         its storage and @p out is untouched).
        /// @throws bad_any_uninitialized_t If an argument is only declared (no value was constructed).
        /// @throws bad_any_unsupported_t If @p out is not an alias to a pointer variable (it must name
        ///         the caller's own variable, and that variable must be able to hold a `void*`).
        /// @note The table cannot check the class the caller will cast the result to: a constructor
        ///       entry is selected by its parameter list only, so naming class A's constructor and
        ///       casting the result to class B is the caller's mistake, not a checked error.
        bool new_instance(std::span<const any_t> args, any_t& out) const;

        /// @brief Construct a new instance from a typed argument list (the forwarding form).
        /// @tparam Args The argument types as deduced at the call site.
        /// @param args The constructor arguments; each is boxed as an any_t with its recorded cvref.
        /// @return The new instance as a `void*`, to be cast back to the class named — or nullptr on
        ///         every failure the any_t overload reports (it forwards there, so the diagnostics are
        ///         the same).
        /// @note This is the convenience form of new_instance(std::span&lt;const any_t&gt;, any_t&):
        ///       it boxes the arguments and hands them to exactly the same resolution rule, so a
        ///       constructor declared with reference parameters IS reachable here (the parameter's
        ///       bare name is what is compared).
        template <typename... Args>
            requires(!detail::is_any_instance_call_trait<Args...>::value)
        void* new_instance(Args&&... args) const {
            std::array<any_t, sizeof...(Args)> boxed{ any_t::capture(std::forward<Args>(args))... };
            // The out slot is an alias to this local pointer variable: the thunk writes the new
            // instance's address straight into it, and a failed call leaves it null.
            void* result = nullptr;
            any_t out(std::ref(result));
            if (!new_instance(std::span<const any_t>(boxed), out)) {
                return nullptr;
            }
            return result;
        }

        // ---- properties ----

        /// @brief Number of properties including those of every base class.
        /// @note noexcept.
        std::size_t property_count() const noexcept;

        /// @brief Look a property up by name.
        meta_property_t property(std::string_view name) const;

        /// @brief The property at a global index.
        /// @note noexcept.
        meta_property_t property(index_t index) const noexcept;

        /// @brief The property carrying the User trait (QMetaObject::userProperty).
        /// @return The property, or an invalid handle when the class declares none. The compile-time
        ///         path already rejects two User properties per class.
        meta_property_t user_property() const;

        /// @brief Global index of a property, or index_npos.
        /// @note noexcept.
        index_t index_of_property(std::string_view name) const noexcept;

        // ---- enumerations ----

        /// @brief Number of enumerations including those of every base class.
        /// @note noexcept.
        std::size_t enumeration_count() const noexcept;

        /// @brief Look an enumeration up by name.
        meta_enum_t enumeration(std::string_view name) const;

        /// @brief The enumeration at a global index.
        /// @note noexcept.
        meta_enum_t enumeration(index_t index) const noexcept;

        /// @brief Global index of an enumeration, or index_npos.
        /// @note noexcept.
        index_t index_of_enumeration(std::string_view name) const noexcept;

        /// @brief Look an enumeration up by name (same rule as enumeration(string_view)).
        /// @param table The chain head meta-object; nullptr answers with an invalid handle.
        /// @param name The enum name.
        /// @return A handle pointing at the DECLARING meta-object inside the chain.
        /// @note Static and pointer-taking on purpose: a caller that already holds the declaring
        ///       meta-object (meta_property_t::_owner) resolves through this entry point, and the
        ///       result stays valid for as long as that meta-object does. No facade is built, so the
        ///       handle never points into a temporary.
        /// @note noexcept.
        static meta_enum_t enumeration_in(const meta_object_t* table, std::string_view name) noexcept;

        // ---- class info ----

        /// @brief Number of class-info pairs including those of every base class.
        /// @note noexcept.
        std::size_t class_info_count() const noexcept;

        /// @brief Look a class-info pair up by key.
        meta_class_info_t class_info(std::string_view name) const;

        /// @brief The class-info pair at a global index.
        /// @note noexcept.
        meta_class_info_t class_info(index_t index) const noexcept;

        /// @brief Global index of a class-info pair, or index_npos.
        /// @note noexcept.
        index_t index_of_class_info(std::string_view name) const noexcept;

        // ---- interfaces ----

        /// @brief Number of interfaces including those declared by every base class.
        /// @note noexcept.
        /// @note Qt has no counter for this (a QObject reports one interface at a time through
        ///       qt_metacast); here the whole chain is countable, so the number matches the entries
        ///       cast / implements can hit.
        std::size_t interface_count() const noexcept;

        /// @brief The IID of the interface at chain-global @p index.
        /// @param index A chain-global index, the same space interface_count() bounds: the ancestors'
        ///        rows come first, this class's own last (the layout every other index family uses).
        /// @return The IID, or an empty view when @p index is index_npos or out of range.
        /// @note noexcept.
        std::string_view interface_iid(index_t index) const noexcept;

        /// @brief The chain-global index of the interface with IID @p iid.
        /// @param iid The interface IID, e.g. "com.example.IFoo/1.0".
        /// @return The index interface_iid() accepts, or index_npos when no class of the chain
        ///         declares it.
        /// @note noexcept. Own table first, then the ancestors (a derived class's entry shadows a
        ///       same-IID base entry), the same rule method() and signal() follow.
        index_t index_of_interface(std::string_view iid) const noexcept;

        /// @brief Whether this class, or a base class, declares the interface with IID @p iid.
        /// @param iid The interface IID, e.g. "com.example.IFoo/1.0".
        /// @note noexcept.
        /// @note The IID namespace is separate from the class-name namespace: implements() and
        ///       inherits() deliberately do not share a table.
        bool implements(std::string_view iid) const noexcept;

        /// @brief Cast a type-erased object to an interface (QObject::qt_metacast equivalent).
        /// @param object The object to cast, type-erased; it must be an instance of this
        ///        meta-object's class (or of a class derived from it). nullptr is rejected. For an
        ///        object_t-derived class this is its object_t sub-object address, which is also the
        ///        complete object's address because object_t is the first base; for a GADGET it is
        ///        the gadget itself.
        /// @param iid The interface IID.
        /// @return The interface sub-object pointer, or nullptr when no class in the chain declares
        ///         @p iid.
        /// @note Walks super_class() exactly like method(): parents first, so an interface entry
        ///       declared by a base class is found (and wins over a same-IID entry of a derived
        ///       class) on the same precedence rule as the other lookup paths.
        /// @note A run-time built type (meta_object_builder_t) carries no interface table — its rows
        ///       are inferred from the C++ bases at compile time — so cast answers nullptr for
        ///       such an object while the rest of its meta-object works normally.
        /// @note This is the type-erased string entry point the namespace-level cast<T>() uses; the
        ///       template overload below casts to a CLASS of the chain, this one recovers an
        ///       interface row. The old cast_to(object_t*, iid) is this function.
        void* cast(void* object, std::string_view iid) const noexcept;

        // ---- casting ----

        /// @brief Cast @p object to the class this table describes.
        /// @param object The object to check; nullptr answers nullptr.
        /// @tparam U The object's type as deduced from the pointer (a GADGET or an object_t-derived
        ///         class, conventionally).
        /// @return The object pointer (type-erased, complete-object address) when it is an instance
        ///         of this class or of a class derived from it; nullptr otherwise.
        /// @note Uses the object's RUNTIME table when U derives from object_t (its virtual
        ///       meta_object()), and the static U::static_meta_object otherwise — a GADGET has no
        ///       virtual accessor, so its table is the static one.
        /// @note On a hit the erased pointer is `static_cast<void*>(object)`; for object_t-derived
        ///       types that IS the complete-object address the interface thunks expect, because
        ///       object_t is the first base.
        /// @note A source type that is neither object_t-derived nor a GADGET is rejected with a
        ///       static_assert: it has no table whose chain could be searched.
        template <typename U>
        void* cast(U* object) const noexcept {
            if (object == nullptr) {
                return nullptr;
            }
            const meta_object_t* const table = detail::meta_object_of(object);
            if (table == nullptr || !table->inherits(this)) {
                return nullptr;
            }
            return const_cast<void*>(static_cast<const void*>(object));
        }

        // ---- static ----

        /// @brief Connect on_<child>_<signal> slots (QMetaObject::connectSlotsByName).
        static void connect_slots_by_name(object_t* root);

        /// @brief Whether @p method can be connected to @p signal (QMetaObject::checkConnectArgs).
        /// @note Both signatures are expected to be normalised.
        static bool check_connect_args(std::string_view signal, std::string_view method);

        /// @brief Handle overload of check_connect_args.
        static bool check_connect_args(const meta_method_t& signal, const meta_method_t& method);

        // ---- invocation ----
        //
        // PARAMETER ORDER mirrors QMetaObject::invokeMethod: the OBJECT leads every form, then the
        // name / signature, then the caller's out slot, then the connection type, then the arguments:
        //
        //     obj, signature | name, [Ret&], [connection_type_t], vs...
        //
        // The object is the one parameter every form shares. Putting it first also takes the out slot
        // out of the first position, which is what removes the `std::string` out-slot collision: in
        // the old order `invoke(out, "label()", obj)` had TWO viable candidates — the typed form
        // (exact `std::string&`, user-defined `const char[]` -> string_view) and the untyped one
        // (user-defined `std::string` -> string_view, exact rest) — so a std::string out slot could
        // not be spelled at all. With the out slot third, its conversion no longer competes with the
        // signature's: the typed form is strictly more specialized and wins.
        //
        // The overload sets are kept apart by constraints, because the tag now sits AMONG the
        // arguments: detail::is_connection_type_arg_v excludes a connection type BOTH as the object
        // and from every variadic argument pack. A flag_t is built from its enum implicitly, so a
        // tag left in a pack binds EXACTLY and beats the tag form's converting parameter; the Auto
        // form would then forward to itself (GCC: "template instantiation depth exceeds maximum").
        //
        // The chain walk is method(signature): its own-first lookup answers the winner of this table and
        // the base chain (global_index). The non-`Ret&` forms then hand the call to that method handle,
        // which owns the one implementation of the return-slot machinery; keeping the call there is what
        // makes the table level and the handle level answer the same thing.
        //
        // The cross product has a second half at BOTH names: the arguments may arrive BOXED (one any_t
        // per parameter, in a std::span) instead of as `Ts&&...`. The object level now spells it fully:
        //   call   (obj, name, [out], [type], args)     4 forms — two existed, the tagged twins are new;
        //   invoke (obj, signature, [out], [type], args) 4 forms — all new.
        // None of them re-implements anything: each resolves its row exactly like the typed sibling
        // (resolve_method() by name, method() by signature) and then hands the span to the HANDLE's
        // matching erased form, which owns the argument match, the return slot and the connection
        // decision. check_invoke_args() is unusable here (see the family @par on the first erased
        // invoke below), so a boxed argument MISMATCH is the handle's throw, while a row that does not
        // exist is reported by the object level — once, never twice.

        /// @brief Invoke a member by signature, walking the chain (the Auto form).
        /// @param obj The object to invoke on.
        /// @param signature The normalised signature.
        /// @param vs The arguments.
        /// @return As invoke(T&&, std::string_view, connection_type_t, Ts&&...) const.
        /// @note Own table first, then super_class(): method(signature) is the same own-first lookup
        ///       every other entry point uses, so a signature present in both runs the derived entry.
        /// @note This is the Auto form: it forwards to invoke(T&&, std::string_view,
        ///       connection_type_t, Ts&&...) with connection_type_flag::AUTO. Auto is Direct when @p obj
        ///       lives on the calling thread and Queued when it does not.
        template <typename T, typename... Ts>
            requires(!detail::is_connection_type_arg_v<T>
                     && (!detail::is_connection_type_arg_v<Ts> && ...)
                     && (!detail::is_any_args_v<Ts> && ...))
        any_t invoke(T&& obj, std::string_view signature, Ts&&... vs) const {
            return invoke(std::forward<T>(obj), signature, connection_type_flag::AUTO,
                std::forward<Ts>(vs)...);
        }

        /// @brief Invoke a member by signature with a caller-chosen connection type, walking the chain.
        /// @param obj The object to invoke on. For a non-object value (a gadget) only Direct is
        ///        possible, because a value has no thread affinity.
        /// @param signature The normalised signature.
        /// @param type The connection type: Direct, Auto, Queued or BlockingQueued.
        /// @param vs The arguments.
        /// @return The method's result in an any_t, built from the row's declared return type:
        ///         - a value-returning entry that ran synchronously answers a constructed any_t holding
        ///           the value (Direct, Auto-on-the-calling-thread, or BlockingQueued — the target
        ///           thread writes into the slot before the wait returns);
        ///         - a void entry answers a DECLARED-ONLY void any_t
        ///           (is_valid() == true, is_initialized() == false);
        ///         - a value-returning entry whose call was only POSTED (a non-blocking Queued call, or
        ///           Auto that resolved to one) answers a DECLARED-ONLY any_t of its return type: the
        ///           call succeeded, the storage is reserved, and the value arrives when the event runs
        ///           — the returned variable is what the event points at, so do not move, copy or
        ///           destroy it before then (see the handle's invoke() @warning);
        ///         - the INVALID any_t (is_valid() == false) when no table in the chain declares
        ///           @p signature, the arguments do not match (one Warning), the requested type cannot
        ///           be honoured (one Warning then: a Queued call on a value type, or a BlockingQueued
        ///           call on the calling thread), or when a BlockingQueued call was CANCELLED because
        ///           the receiver died before the event was placed.
        /// @note The call itself is the HANDLE's: method(signature) resolves the own-first winner and
        ///       its invoke(Obj&&, connection_type_t, ...) owns the one return-slot implementation (a value
        ///       slot declared from the row's return type, handed to the thunk as params[0] and marked
        ///       constructed; the declared-only void shape for a void row). The table level and the
        ///       handle level therefore cannot drift apart.
        /// @note The argument check below runs FIRST, so a mismatch is reported here (one Warning)
        ///       before the handle is asked to dispatch; the handle then re-checks the same rule.
        /// @note Queued / BlockingQueued resolve the entry through the SAME chain — this table's own
        ///       entry wins — box the arguments and post a meta_call_event_t to @p obj's thread.
        ///       The argument copies are made by the entry's declared parameter types
        ///       (detail::copy_args).
        /// @note An explicit Direct on an object that lives on another thread is honoured as given:
        ///       only Auto resolves itself, and it resolves at call time.
        /// @note @p obj must be the object itself, not a pointer to it: the address that reaches the
        ///       invoker is @c &obj (the same rule as meta_method_t::invoke).
        template <typename T, typename... Ts>
            requires(!detail::is_connection_type_arg_v<T>
                     && (!detail::is_connection_type_arg_v<Ts> && ...)
                     && (!detail::is_any_args_v<Ts> && ...))
        any_t invoke(T&& obj, std::string_view signature, connection_type_t type, Ts&&... vs) const {
            if (!check_invoke_args<Ts...>(signature, get_param_qualifier<T&&>())) {
                return any_t{};
            }
            return method(signature).invoke(std::forward<T>(obj), type, std::forward<Ts>(vs)...);
        }

        /// @brief Invoke a member by signature, capturing the return value, walking the chain (Auto form).
        /// @note Same rule as the overload above: own table first, then super_class().
        /// @note This is the Auto form of invoke(T&&, std::string_view, Ret&, connection_type_t, ...):
        ///       Direct on the calling thread, Queued across threads — the queued path fills @p ret
        ///       when the event runs, so the variable must stay alive and unchanged until then.
        /// @note The out slot is the THIRD parameter, so its conversion never competes with the
        ///       `std::string_view` name's: `std::string out; meta.invoke(obj, "label()", out)` selects
        ///       THIS overload (the more specialized template), not the untyped any_t form. Under the
        ///       old order the two were equally viable and the call did not compile at all.
        template <typename Ret, typename T, typename... Ts>
            requires(!detail::is_connection_type_arg_v<Ret> && !detail::is_connection_type_arg_v<T>
                     && !detail::is_any_args_v<Ret>
                     && (!detail::is_connection_type_arg_v<Ts> && ...)
                     && (!detail::is_any_args_v<Ts> && ...))
        bool invoke(T&& obj, std::string_view signature, Ret& ret, Ts&&... vs) const {
            return invoke(std::forward<T>(obj), signature, ret, connection_type_flag::AUTO,
                std::forward<Ts>(vs)...);
        }

        /// @brief Invoke a member by signature, capturing the return value, with a caller-chosen type.
        /// @param obj The object to invoke on.
        /// @param signature The normalised signature.
        /// @param ret Receives the return value.
        /// @param type The connection type.
        /// @param vs The arguments.
        /// @return As invoke(T&&, std::string_view, connection_type_t, Ts&&...) const; in addition the
        ///         return type is checked against @p Ret by the handle.
        /// @note The call is the HANDLE's, exactly like the non-@c Ret& overload above:
        ///       method(signature) resolves the own-first winner and its invoke(Obj&&, Ret&,
        ///       connection_type_t, ...) owns the ONE return-slot implementation AND the ONE
        ///       return-type check, so the table level and the
        ///       handle level cannot disagree. A wrong @p Ret is therefore refused the way the handle
        ///       refuses it — nothing runs, @p ret stays untouched, and exactly one Critical names the
        ///       signature, the row's declared return type and the expected spelling — instead of the
        ///       row's thunk writing a foreign type through @c &ret.
        /// @note A Queued call delivers the result LATER, not never: the event holds @p ret's address
        ///       and the target thread fills it when it runs, while this call returns true as soon as
        ///       the event was posted. @p ret therefore must outlive the event — keep it alive and
        ///       unchanged until the receiver's loop (or `send_posted_events()`) has processed the call.
        ///       BlockingQueued delivers it before returning — the target thread writes the result into
        ///       @p ret before the wait returns; @p ret is a reference, so it is alive for exactly as
        ///       long as the call blocks.
        /// @note A BlockingQueued call that is CANCELLED (the receiver was destroyed, or its queued
        ///       event purged, before the call could be placed) still returns from the wait, reports
        ///       false, and leaves @p ret untouched.
        template <typename Ret, typename T, typename... Ts>
            requires(!detail::is_connection_type_arg_v<Ret> && !detail::is_connection_type_arg_v<T>
                     && !detail::is_any_args_v<Ret>
                     && (!detail::is_connection_type_arg_v<Ts> && ...)
                     && (!detail::is_any_args_v<Ts> && ...))
        bool invoke(T&& obj, std::string_view signature, Ret& ret, connection_type_t type, Ts&&... vs) const {
            if (!check_invoke_args<Ts...>(signature, get_param_qualifier<T&&>())) {
                return false;
            }
            const meta_method_t row = method(signature);
            if (!row.is_valid()) {
                // Unreachable after the check above (it resolves the same own-first winner), so no
                // second diagnostic is invented for one miss: the check already reported it.
                return false;
            }
            return row.invoke(std::forward<T>(obj), ret, type, std::forward<Ts>(vs)...);
        }

        // ---- the erased argument forms of invoke (one any_t per parameter) ----

        /// @brief Invoke with one any_t per parameter (the erased value channel, Direct).
        /// @tparam T The object type. Unlike the typed forms, a POINTER is accepted here — the handle's
        ///         erased overload supports it — and a null one is the honest "there is no object":
        ///         nothing runs and the INVALID any_t answers.
        /// @param obj The object to invoke on (the object itself, or a pointer to it).
        /// @param signature The normalised signature.
        /// @param args One any_t per parameter, in declaration order.
        /// @return As invoke(T&&, std::string_view, connection_type_t, Ts&&...) const; the untagged
        ///         erased shape is Direct (see the family note).
        /// @throws bad_any_uninitialized_t If an argument is only declared (its value was never
        ///         constructed).
        /// @throws bad_any_cast_t If the boxed list does not match the row's parameter column — see the
        ///         family note: that THROW is the erased argument check.
        /// @par The erased-argument family (one any_t per parameter)
        ///   Both names take the boxed list in the SAME order as their typed forms:
        ///     call  (obj, name,      [out], [type], args) — four forms (the two new ones are the
        ///                                                    tagged twins of the pair that existed);
        ///     invoke(obj, signature, [out], [type], args) — four forms (all four are new).
        ///   Each resolves its row exactly like the typed sibling — resolve_method() for `call`, method()
        ///   for `invoke` — and then hands @p args to the HANDLE's matching erased form, so the
        ///   argument match, the return-slot machinery and the connection decision have ONE
        ///   implementation each and the two levels cannot disagree.
        ///   check_invoke_args() CANNOT serve here: it is instantiated with the caller's `Ts...`, spells
        ///   each of them with arg_spelling<>() and compares that against the row, while a span of any_t
        ///   has no Ts to spell (its element type is any_t for every call). The boxed values carry
        ///   their own identities, so the handle matches them against the row's parameter column
        ///   (meta_method_t::method_matches_boxed, which turns each value into a (type, cvref) identity and
        ///   lets meta_info_t::method_matches decide), and a mismatch is that channel's error: a
        ///   bad_any_cast_t THROW, never a Warning. The object level therefore reports the one thing no
        ///   handle can report — a row that does not EXIST: method() is a silent lookup, so the erased
        ///   invoke forms log
        ///   exactly ONE Warning ("invoke: no method '<signature>' accepts these arguments", the very
        ///   wording check_invoke_args() uses) and answer the invalid any_t / false. A `call` miss is
        ///   already reported once by the resolver, so the erased `call` forms add no record.
        ///   The untagged ERASED shapes are Direct (the handle's untagged erased forms take no
        ///   connection type); the untagged TYPED shapes are Auto. That asymmetry predates this family
        ///   and is the erased channel's documented contract at the handle level.
        template <typename T>
            requires(!detail::is_connection_type_arg_v<T>)
        any_t invoke(T&& obj, std::string_view signature, std::span<const any_t> args) const {
            const meta_method_t row = method(signature);
            if (!row.is_valid()) {
                report_erased_invoke_miss(signature);
                return any_t{ };
            }
            return row.invoke(std::forward<T>(obj), args);
        }

        /// @brief Invoke with one any_t per parameter and a caller-chosen connection type.
        /// @param obj The object to invoke on.
        /// @param signature The normalised signature.
        /// @param type The connection type: Direct, Auto, Queued or BlockingQueued.
        /// @param args One any_t per parameter, in declaration order.
        /// @return As invoke(T&&, std::string_view, connection_type_t, Ts&&...) const — the same four
        ///         shapes: the value when the call ran synchronously, a DECLARED-ONLY void any_t for a
        ///         void row, a DECLARED-ONLY any_t of the return type when the call was only POSTED
        ///         (do not move or copy it before the event ran), and the INVALID any_t for a miss, a
        ///         null @p obj, a refused connection type, or a cancelled blocking call.
        /// @throws bad_any_uninitialized_t / bad_any_cast_t See the untagged erased form: the argument
        ///         match is the handle's, so its error channel is the same throw.
        /// @note The connection decision is the HANDLE's (detail::resolve_invoke), the one the typed
        ///       forms take — the refusal rules are not duplicated here. The queued copies are made by
        ///       the row's declared parameter types, exactly like the typed branch.
        /// @note Part of the erased family, documented once at
        ///       invoke(T&&, std::string_view, std::span<const any_t>) const.
        template <typename T>
            requires(!detail::is_connection_type_arg_v<T>)
        any_t invoke(T&& obj, std::string_view signature, connection_type_t type,
                     std::span<const any_t> args) const {
            const meta_method_t row = method(signature);
            if (!row.is_valid()) {
                report_erased_invoke_miss(signature);
                return any_t{ };
            }
            return row.invoke(std::forward<T>(obj), type, args);
        }

        /// @brief Invoke with one any_t per parameter, constructing the result into a caller slot.
        /// @param obj The object to invoke on.
        /// @param signature The normalised signature.
        /// @param ret The caller's DECLARED-BUT-NOT-CONSTRUCTED slot, of the row's return type.
        /// @param args One any_t per parameter, in declaration order.
        /// @return true when the entry ran; false for a miss, a null @p obj, a refused connection type,
        ///         or a wrong-typed / already-constructed slot — @p ret is then left as it was.
        /// @throws bad_any_cast_t If the boxed list does not match the row's parameter column, or if
        ///         @p ret's base type is not the row's return type (the erased return-type rule: bare
        ///         spellings compared, never the typed form's Critical — there is no `Ret` to name).
        /// @throws bad_any_unsupported_t If @p ret already holds a constructed object.
        /// @note Part of the erased family, documented once at
        ///       invoke(T&&, std::string_view, std::span<const any_t>) const.
        template <typename T>
            requires(!detail::is_connection_type_arg_v<T>)
        bool invoke(T&& obj, std::string_view signature, any_t& ret,
                    std::span<const any_t> args) const {
            const meta_method_t row = method(signature);
            if (!row.is_valid()) {
                report_erased_invoke_miss(signature);
                return false;
            }
            return row.invoke(std::forward<T>(obj), ret, args);
        }

        /// @brief The erased-argument form with a caller slot AND a caller-chosen connection type.
        /// @param obj The object to invoke on.
        /// @param signature The normalised signature.
        /// @param ret The caller's slot; a Queued call fills it when the event runs, so it must
        ///        outlive the event.
        /// @param type The connection type: Direct, Auto, Queued or BlockingQueued.
        /// @param args One any_t per parameter, in declaration order.
        /// @return As invoke(T&&, std::string_view, any_t&, std::span<const any_t>) const, with the
        ///         caller's connection type honoured by the handle's dispatch.
        /// @note Part of the erased family, documented once at
        ///       invoke(T&&, std::string_view, std::span<const any_t>) const.
        template <typename T>
            requires(!detail::is_connection_type_arg_v<T>)
        bool invoke(T&& obj, std::string_view signature, any_t& ret, connection_type_t type,
                    std::span<const any_t> args) const {
            const meta_method_t row = method(signature);
            if (!row.is_valid()) {
                report_erased_invoke_miss(signature);
                return false;
            }
            return row.invoke(std::forward<T>(obj), ret, type, args);
        }

        // ---- name-based invocation (the `call` family) ----
        //
        // `invoke` is SIGNATURE-based: the caller already knows which overload it means and spells
        // the normalised signature ("add(int,int)"). `call` is NAME-based: the bare name plus the
        // object and the arguments are enough, because the table picks the overload. The two are
        // siblings, not variants of each other — and `call` does NOT re-implement invocation: it
        // resolves the name ONCE, with the matching resolve_method() form, then hands the call to that
        // handle's matching invoke() — the same relative order at both levels: obj, (out),
        // connectiontype, args.
        //
        // What resolves: the name against the object class's chain, own table first. The runtime-name
        // forms go through resolve_method<Obj, Ts...>(); the fixed_string_t forms through
        // resolve_method<Name, Obj, Ts...>(), which is a COMPILE-time walk over the object's STATIC
        // chain — so it refuses an object whose table is built at run time (dynamic_base_t); use the
        // runtime-name form for those.
        // What logs: only the resolver — ONE Warning for a miss, ONE Critical for an ambiguity (and
        // ONE Critical for a compile-time receiver mismatch). `call` adds no diagnostic of its own:
        // it answers the invalid any_t (or false) that the invalid handle already produces, so a
        // failure is reported exactly once.
        //
        // Overload hygiene, mirroring the handle level:
        //  - `any_t` is excluded from every typed `Ret&` form (detail::is_any_value_v), so an any_t
        //    out slot always means the erased-argument overload;
        //  - a connection type is excluded as the OBJECT and from the ARGUMENT PACK
        //    (detail::is_connection_type_arg_v, see the invocation section): the object is the FIRST
        //    parameter now and the tag is third (fourth after an out slot), so `call(obj, "f", Queued)`
        //    must not bind the flag to the pack (which would recurse into the Auto form);
        //  - an any_t span is excluded from the variadic argument lists AND from every typed `Ret&`
        //    out slot (detail::is_any_args_v), so the erased overload owns it, exactly like
        //    meta_method_t::invoke: a std::array<any_t, N> / std::vector<any_t> in the out position
        //    would otherwise bind `Ret&` EXACTLY (better than the span's conversion) and be read as a
        //    return slot instead of an argument list;
        //  - the fixed_string_t forms additionally require the OBJECT to be a meta-object receiver
        //    (detail::is_meta_receiver_v): all four take the object first, so the constraint keeps a
        //    non-receiver out of that shared position. A receiver that does not DECLARE its table
        //    still reaches resolve_method's static_assert, which names the fix;
        //  - the ERASED forms (one any_t per parameter) are the same cross product on the boxed
        //    channel: the untagged pair existed, the tagged twins are new. They resolve through the
        //    same resolve_method() — with boxed_identities() standing in for `Ts...` — and hand the span
        //    to the handle's matching erased invoke(), so `call` stays a front end there as well. The
        //    family is documented once, at
        //    invoke(T&&, std::string_view, std::span<const any_t>) const.

        /// @brief Call a member by its BARE NAME, resolving then invoking (the Auto form).
        /// @param obj The object to call on (the object itself, never a pointer).
        /// @param name The bare member name ("add"); it is NOT a signature key.
        /// @param vs The arguments.
        /// @return Exactly what the resolved handle's invoke() answers: a constructed any_t for a
        ///         value row, a DECLARED-ONLY void any_t (is_valid() == true, is_initialized() ==
        ///         false) for a void row, a DECLARED-ONLY any_t of the row's return type when the
        ///         call was only posted (the event fills it later, so the variable must not be moved
        ///         or copied before then), and the INVALID any_t when the name does not resolve (one
        ///         Warning), is ambiguous (one Critical), or the connection type cannot be honoured.
        /// @note Auto is Direct on the calling thread and Queued across it, resolved at call time.
        /// @sa invoke
        template <typename T, typename... Ts>
            requires(!detail::is_connection_type_arg_v<T>
                     && (!detail::is_connection_type_arg_v<Ts> && ...)
                     && (!detail::is_any_args_v<Ts> && ...))
        any_t call(T&& obj, std::string_view name, Ts&&... vs) const {
            return call(std::forward<T>(obj), name, connection_type_flag::AUTO, std::forward<Ts>(vs)...);
        }

        /// @brief Call a member by name with a caller-chosen connection type.
        /// @param obj The object to call on.
        /// @param name The bare member name.
        /// @param type The connection type: Direct, Auto, Queued or BlockingQueued.
        /// @param vs The arguments.
        /// @return As call(T&&, std::string_view, Ts&&...) const; the row is resolved ONCE and the
        ///         call is the handle's, so the table level and the handle level cannot disagree.
        /// @note `Obj` is `T&&` exactly as the forwarding parameter deduced it: a bare `T` (an
        ///       rvalue), `T&`, `const T&` or `volatile T&`, whose get_param_qualifier is what
        ///       resolve_method() compares against the member's own qualifier.
        template <typename T, typename... Ts>
            requires(!detail::is_connection_type_arg_v<T>
                     && (!detail::is_connection_type_arg_v<Ts> && ...)
                     && (!detail::is_any_args_v<Ts> && ...))
        any_t call(T&& obj, std::string_view name, connection_type_t type, Ts&&... vs) const {
            const meta_method_t row = resolve_method<T&&, Ts...>(name);
            if (!row.is_valid()) {
                return any_t{ };
            }
            return row.invoke(std::forward<T>(obj), type, std::forward<Ts>(vs)...);
        }

        /// @brief Call a member by name, capturing the return value (the Auto form).
        /// @param obj The object to call on.
        /// @param name The bare member name.
        /// @param ret Receives the return value.
        /// @param vs The call's arguments.
        /// @note The handle's invoke() verifies the declared return type: a mismatch is false
        ///       with @p ret untouched and ONE Critical from the handle (nothing is invoked). The
        ///       resolver, not this function, owns the miss / ambiguity diagnostic, so the two
        ///       cannot stack: a mismatch requires a row that already resolved.
        template <typename Ret, typename T, typename... Ts>
            requires(!detail::is_any_value_v<Ret> && !detail::is_connection_type_arg_v<Ret>
                     && !detail::is_any_args_v<Ret>
                     && !detail::is_connection_type_arg_v<T>
                     && (!detail::is_connection_type_arg_v<Ts> && ...)
                     && (!detail::is_any_args_v<Ts> && ...))
        bool call(T&& obj, std::string_view name, Ret& ret, Ts&&... vs) const {
            return call(std::forward<T>(obj), name, ret, connection_type_flag::AUTO,
                std::forward<Ts>(vs)...);
        }

        /// @brief Call a member by name, capturing the return value, with a caller-chosen type.
        /// @param obj The object to call on.
        /// @param name The bare member name.
        /// @param ret Receives the return value.
        /// @param type The connection type.
        /// @param vs The arguments.
        /// @return As call(T&&, std::string_view, connection_type_t, Ts&&...) const, plus the
        ///         handle's return-type check against @p Ret (a mismatch is false, @p ret untouched,
        ///         and one Critical naming both spellings).
        template <typename Ret, typename T, typename... Ts>
            requires(!detail::is_any_value_v<Ret> && !detail::is_connection_type_arg_v<Ret>
                     && !detail::is_any_args_v<Ret>
                     && !detail::is_connection_type_arg_v<T>
                     && (!detail::is_connection_type_arg_v<Ts> && ...)
                     && (!detail::is_any_args_v<Ts> && ...))
        bool call(T&& obj, std::string_view name, Ret& ret, connection_type_t type, Ts&&... vs) const {
            const meta_method_t row = resolve_method<T&&, Ts...>(name);
            if (!row.is_valid()) {
                return false;
            }
            return row.invoke(std::forward<T>(obj), ret, type, std::forward<Ts>(vs)...);
        }

        /// @brief Call a member by a COMPILE-TIME name over the STATIC chain (the Auto form).
        /// @tparam Name The bare member name, as a string literal (a fixed_string_t NTTP).
        /// @return As call(T&&, std::string_view, Ts&&...) const.
        /// @note A miss or an ambiguity is a COMPILE-TIME error (resolve_method<Name, ...> throws in
        ///       the constant expression), never a runtime log; the runtime part is a pointer walk of
        ///       the depth the compile-time pass recorded.
        /// @note Refused for an object whose table is built at run time (dynamic_base_t), because no
        ///       compile-time walk can see such a table — use the runtime-name form there.
        template <detail::fixed_string_t Name, typename T, typename... Ts>
            requires(detail::is_meta_receiver_v<T>
                     && (!detail::is_connection_type_arg_v<Ts> && ...)
                     && (!detail::is_any_args_v<Ts> && ...))
        any_t call(T&& obj, Ts&&... vs) const {
            return call<Name>(std::forward<T>(obj), connection_type_flag::AUTO, std::forward<Ts>(vs)...);
        }

        /// @brief Call a member by a compile-time name with a caller-chosen connection type.
        /// @tparam Name The bare member name.
        /// @param obj The object to call on.
        /// @param type The connection type.
        /// @param vs The arguments.
        /// @return As call(T&&, std::string_view, connection_type_t, Ts&&...) const, with the row
        ///         resolved at compile time over @p obj's static chain.
        template <detail::fixed_string_t Name, typename T, typename... Ts>
            requires(detail::is_meta_receiver_v<T>
                     && (!detail::is_connection_type_arg_v<Ts> && ...)
                     && (!detail::is_any_args_v<Ts> && ...))
        any_t call(T&& obj, connection_type_t type, Ts&&... vs) const {
            const meta_method_t row = resolve_method<Name, T&&, Ts...>();
            if (!row.is_valid()) {
                return any_t{ };
            }
            return row.invoke(std::forward<T>(obj), type, std::forward<Ts>(vs)...);
        }

        /// @brief Call a member by a compile-time name, capturing the return value (the Auto form).
        /// @param obj The object to call on.
        /// @param ret Receives the return value; the handle checks it against the row's return type.
        /// @param vs The call's arguments.
        template <detail::fixed_string_t Name, typename Ret, typename T, typename... Ts>
            requires(!detail::is_any_value_v<Ret> && !detail::is_connection_type_arg_v<Ret>
                     && !detail::is_any_args_v<Ret>
                     && detail::is_meta_receiver_v<T>
                     && (!detail::is_connection_type_arg_v<Ts> && ...)
                     && (!detail::is_any_args_v<Ts> && ...))
        bool call(T&& obj, Ret& ret, Ts&&... vs) const {
            return call<Name, Ret>(std::forward<T>(obj), ret, connection_type_flag::AUTO,
                std::forward<Ts>(vs)...);
        }

        /// @brief Call a member by a compile-time name, capturing the return value, typed connection.
        /// @param obj The object to call on.
        /// @param ret Receives the return value.
        /// @param type The connection type.
        /// @param vs The arguments.
        /// @return As call<Name>(T&&, connection_type_t, Ts&&...) const, plus the handle's
        ///         return-type check against @p Ret.
        template <detail::fixed_string_t Name, typename Ret, typename T, typename... Ts>
            requires(!detail::is_any_value_v<Ret> && !detail::is_connection_type_arg_v<Ret>
                     && !detail::is_any_args_v<Ret>
                     && detail::is_meta_receiver_v<T>
                     && (!detail::is_connection_type_arg_v<Ts> && ...)
                     && (!detail::is_any_args_v<Ts> && ...))
        bool call(T&& obj, Ret& ret, connection_type_t type, Ts&&... vs) const {
            const meta_method_t row = resolve_method<Name, T&&, Ts...>();
            if (!row.is_valid()) {
                return false;
            }
            return row.invoke(std::forward<T>(obj), ret, type, std::forward<Ts>(vs)...);
        }

        /// @brief Call a member by name with one any_t per parameter (the erased-argument form).
        /// @param obj The object to call on; unlike the typed forms, a POINTER is accepted here
        ///        (the handle's erased overload supports it).
        /// @param name The bare member name.
        /// @param args One any_t per argument, in declaration order.
        /// @return As call(T&&, std::string_view, Ts&&...) const.
        /// @note The resolution uses each boxed value's recorded (bare type, cvref) identity, which
        ///       is the pair the typed form builds from `Ts...` — so the two entry points agree on
        ///       the same call.
        /// @note The untagged erased shape is Direct: it hands the span to the handle's untagged
        ///       erased invoke(), exactly as it did before the tagged twins were added.
        /// @note Part of the erased family, documented once at
        ///       invoke(T&&, std::string_view, std::span<const any_t>) const; the argument mismatch
        ///       is that family's shared rule — here it simply makes resolve_method() answer no row, so
        ///       one Warning comes from the resolver and no throw happens.
        template <typename T>
            requires(!detail::is_connection_type_arg_v<T>)
        any_t call(T&& obj, std::string_view name, std::span<const any_t> args) const {
            const meta_method_t row = resolve_method<T&&>(name, detail::boxed_identities(args));
            if (!row.is_valid()) {
                return any_t{ };
            }
            return row.invoke(std::forward<T>(obj), args);
        }

        /// @brief The erased-argument form with a caller-chosen connection type.
        /// @param obj The object to call on (a pointer is accepted, as on the untagged twin).
        /// @param name The bare member name.
        /// @param type The connection type: Direct, Auto, Queued or BlockingQueued.
        /// @param args One any_t per argument, in declaration order.
        /// @return As call(T&&, std::string_view, std::span<const any_t>) const, with the caller's
        ///         connection type honoured by the handle's dispatch: the value when it ran
        ///         synchronously, a DECLARED-ONLY any_t of the return type when the call was only
        ///         POSTED (the event fills that very variable later), a DECLARED-ONLY void any_t for a
        ///         void row, and the INVALID any_t for a miss (one Warning from the resolver), an
        ///         ambiguity (one Critical) or a refused connection type.
        /// @note The row is resolved ONCE, by the same resolve_method() the untagged twin uses, and the
        ///       call is the handle's — so the tagged and untagged forms cannot disagree about which
        ///       row a name means, and the tagged twin adds no diagnostic of its own.
        /// @note Part of the erased family, documented once at
        ///       invoke(T&&, std::string_view, std::span<const any_t>) const.
        template <typename T>
            requires(!detail::is_connection_type_arg_v<T>)
        any_t call(T&& obj, std::string_view name, connection_type_t type,
                   std::span<const any_t> args) const {
            const meta_method_t row = resolve_method<T&&>(name, detail::boxed_identities(args));
            if (!row.is_valid()) {
                return any_t{ };
            }
            return row.invoke(std::forward<T>(obj), type, args);
        }

        /// @brief The erased-argument form with a caller-supplied return slot.
        /// @param obj The object to call on.
        /// @param name The bare member name.
        /// @param ret The caller's DECLARED-BUT-NOT-CONSTRUCTED slot, of the row's return type.
        /// @param args One any_t per argument, in declaration order.
        /// @return false for an invalid handle (a miss / ambiguity, already logged once) or when the
        ///         handle refuses the call; @p ret is then left untouched.
        /// @note The `any_t&` out slot is why the typed `Ret&` forms exclude any_t: with the object
        ///       first, the two forms put the out slot in the SAME position (third), so without that
        ///       exclusion they would be indistinguishable for `Ret = any_t`.
        /// @note The untagged erased shape is Direct, like its no-out twin.
        /// @note Part of the erased family, documented once at
        ///       invoke(T&&, std::string_view, std::span<const any_t>) const.
        template <typename T>
            requires(!detail::is_connection_type_arg_v<T>)
        bool call(T&& obj, std::string_view name, any_t& ret, std::span<const any_t> args) const {
            const meta_method_t row = resolve_method<T&&>(name, detail::boxed_identities(args));
            if (!row.is_valid()) {
                return false;
            }
            return row.invoke(std::forward<T>(obj), ret, args);
        }

        /// @brief The erased-argument form with a caller slot AND a caller-chosen connection type.
        /// @param obj The object to call on.
        /// @param name The bare member name.
        /// @param ret The caller's slot, of the row's return type; a Queued call fills it when the
        ///        event runs, so it must outlive the event.
        /// @param type The connection type: Direct, Auto, Queued or BlockingQueued.
        /// @param args One any_t per argument, in declaration order.
        /// @return As call(T&&, std::string_view, any_t&, std::span<const any_t>) const, with the
        ///         caller's connection type honoured by the handle's dispatch.
        /// @note The tagged twin of the overload above: same resolver, same handle call, so the two
        ///       agree on the row; only the connection type differs, and it is the handle that applies
        ///       it (one implementation of the refusal rules).
        /// @note Part of the erased family, documented once at
        ///       invoke(T&&, std::string_view, std::span<const any_t>) const.
        template <typename T>
            requires(!detail::is_connection_type_arg_v<T>)
        bool call(T&& obj, std::string_view name, any_t& ret, connection_type_t type,
                  std::span<const any_t> args) const {
            const meta_method_t row = resolve_method<T&&>(name, detail::boxed_identities(args));
            if (!row.is_valid()) {
                return false;
            }
            return row.invoke(std::forward<T>(obj), ret, type, args);
        }

        /// @brief Normalise a raw signature (forwards; the body needs the rewriting table).
        std::string normalized_signature(std::string_view raw) const;

        /// @name Calling a STATIC member: no object
        /// @{
        // The instance families take the object FIRST, so a static call cannot be one of their
        // overloads: `call_static("make", 5)` and `call(obj, "make", 5)` differ only by a leading
        // parameter that is an object in one case and the first ARGUMENT in the other. The `_static`
        // name says which family is meant, and the parameter order is the instance one with the object
        // removed: (name | signature, [out], [type], args...).
        //
        // These are FRONT ENDS, not a second dispatch. Resolution is the instance families' resolver —
        // resolve_method() by NAME (here with qualifier_flag::NONE, because there is no object whose
        // cvref could be read) and method() by SIGNATURE — and the call is handed to the resolved
        // handle's invoke_static(), which owns the whole invocation protocol (return-slot, argument
        // matching, connection type). So a `call_static` and an `invoke_static` that name the same row
        // cannot disagree, and every failure is diagnosed once, by the resolver or by the handle.
        //
        // A name that resolves to a row WITH a receiver is not a resolution failure: the handle refuses
        // it with one Critical (invoke_static on a non-static row), exactly as invoke() refuses a
        // receiver-less one. That is the misuse rule, and it is stated once on the handle.
        //
        // The compile-time-name form (call_static\<"f"\>) resolves through the RUNTIME resolver here,
        // unlike call\<"f"\>: there is no object type for the compile-time chain walk to start from
        // (that walk is `Obj::static_meta_object`'s chain), so the fixed_string_t form is a spelling
        // convenience, not a compile-time verdict.

        /// @brief Call a STATIC member by its BARE NAME (the Auto form).
        /// @param name The bare member name ("make"); it is NOT a signature key.
        /// @param vs The arguments.
        /// @return Exactly what the resolved handle's invoke_static() answers: a constructed any_t for
        ///         a value row, a DECLARED-ONLY void any_t for a void row, and the INVALID any_t when
        ///         the name does not resolve (one Warning), is ambiguous (one Critical), resolves to a
        ///         row with a receiver (one Critical), or the connection type cannot be honoured.
        /// @note Auto is Direct here: a static call has no receiver and therefore no thread affinity
        ///       to resolve against.
        template <typename... Ts>
            requires((!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        any_t call_static(std::string_view name, Ts&&... vs) const {
            return call_static(name, connection_type_flag::AUTO, std::forward<Ts>(vs)...);
        }

        /// @brief Call a STATIC member by name with a caller-chosen connection type.
        /// @param name The bare member name.
        /// @param type The connection type: Direct or Auto (a static call has no receiver, so Queued
        ///        and BlockingQueued are refused with one Warning by the handle's dispatch).
        /// @param vs The arguments.
        /// @return As call_static(std::string_view, Ts&&...) const, with @p type honoured.
        template <typename... Ts>
            requires((!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        any_t call_static(std::string_view name, connection_type_t type, Ts&&... vs) const {
            const std::array<cvref_meta_type_t, sizeof...(Ts)> args{ cvref_meta_type_t::from<Ts>()... };
            const meta_method_t row =
                resolve_method(name, std::span<const cvref_meta_type_t>(args), qualifier_flag::NONE);
            if (!row.is_valid()) {
                return any_t{};
            }
            return row.invoke_static(type, std::forward<Ts>(vs)...);
        }

        /// @brief Call a STATIC member by name, capturing the return value (the Auto form).
        /// @param name The bare member name.
        /// @param ret Receives the return value; the handle checks it against the row's return type
        ///        (a mismatch is false, @p ret untouched, and one Critical).
        /// @param vs The arguments.
        template <typename Ret, typename... Ts>
            requires(!detail::is_any_value_v<Ret> && !detail::is_any_args_v<Ret>
                     && !detail::is_connection_type_arg_v<Ret>
                     && (!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        bool call_static(std::string_view name, Ret& ret, Ts&&... vs) const {
            return call_static(name, ret, connection_type_flag::AUTO, std::forward<Ts>(vs)...);
        }

        /// @brief Call a STATIC member by name, capturing the return value, typed connection.
        /// @param name The bare member name.
        /// @param ret Receives the return value.
        /// @param type The connection type.
        /// @param vs The arguments.
        /// @return As call_static(std::string_view, connection_type_t, Ts&&...) const, plus the
        ///         handle's return-type check against @p Ret.
        template <typename Ret, typename... Ts>
            requires(!detail::is_any_value_v<Ret> && !detail::is_any_args_v<Ret>
                     && !detail::is_connection_type_arg_v<Ret>
                     && (!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        bool call_static(std::string_view name, Ret& ret, connection_type_t type, Ts&&... vs) const {
            const std::array<cvref_meta_type_t, sizeof...(Ts)> args{ cvref_meta_type_t::from<Ts>()... };
            const meta_method_t row =
                resolve_method(name, std::span<const cvref_meta_type_t>(args), qualifier_flag::NONE);
            if (!row.is_valid()) {
                return false;
            }
            return row.invoke_static(ret, type, std::forward<Ts>(vs)...);
        }

        /// @brief Call a STATIC member by name with one any_t per parameter (the erased form).
        /// @param name The bare member name.
        /// @param args One any_t per argument, in declaration order.
        /// @return As call_static(std::string_view, Ts&&...) const; the resolution uses each boxed
        ///         value's recorded (bare type, cvref) identity.
        any_t call_static(std::string_view name, std::span<const any_t> args) const {
            const meta_method_t row = resolve_method(name, detail::boxed_identities(args), qualifier_flag::NONE);
            if (!row.is_valid()) {
                return any_t{};
            }
            return row.invoke_static(args);
        }

        /// @brief The tagged twin of the erased form.
        /// @param name The bare member name.
        /// @param type The connection type.
        /// @param args One any_t per argument, in declaration order.
        any_t call_static(std::string_view name, connection_type_t type,
                          std::span<const any_t> args) const {
            const meta_method_t row = resolve_method(name, detail::boxed_identities(args), qualifier_flag::NONE);
            if (!row.is_valid()) {
                return any_t{};
            }
            return row.invoke_static(type, args);
        }

        /// @brief The erased form with a caller-supplied return slot.
        /// @param name The bare member name.
        /// @param ret The caller's DECLARED-BUT-NOT-CONSTRUCTED slot, of the row's return type.
        /// @param args One any_t per argument, in declaration order.
        bool call_static(std::string_view name, any_t& ret, std::span<const any_t> args) const {
            const meta_method_t row = resolve_method(name, detail::boxed_identities(args), qualifier_flag::NONE);
            if (!row.is_valid()) {
                return false;
            }
            return row.invoke_static(ret, args);
        }

        /// @brief The tagged twin of the erased `any_t&` form.
        /// @param name The bare member name.
        /// @param ret The caller's slot.
        /// @param type The connection type.
        /// @param args One any_t per argument, in declaration order.
        bool call_static(std::string_view name, any_t& ret, connection_type_t type,
                         std::span<const any_t> args) const {
            const meta_method_t row = resolve_method(name, detail::boxed_identities(args), qualifier_flag::NONE);
            if (!row.is_valid()) {
                return false;
            }
            return row.invoke_static(ret, type, args);
        }

        /// @brief Call a STATIC member by a COMPILE-TIME name (the Auto form).
        /// @tparam Name The bare member name, as a string literal (a fixed_string_t NTTP).
        /// @return As call_static(std::string_view, Ts&&...) const.
        /// @note Unlike call\<Name\>, this resolves at RUN TIME: the compile-time resolver walks
        ///       `Obj::static_meta_object`'s chain, and a static call names no object type. The NTTP
        ///       is a spelling convenience here, not a compile-time verdict.
        template <detail::fixed_string_t Name, typename... Ts>
            requires((!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        any_t call_static(Ts&&... vs) const {
            return call_static(Name.view(), std::forward<Ts>(vs)...);
        }

        /// @brief Call a STATIC member by a compile-time name with a caller-chosen connection type.
        template <detail::fixed_string_t Name, typename... Ts>
            requires((!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        any_t call_static(connection_type_t type, Ts&&... vs) const {
            return call_static(Name.view(), type, std::forward<Ts>(vs)...);
        }

        /// @brief Call a STATIC member by a compile-time name, capturing the return value.
        template <detail::fixed_string_t Name, typename Ret, typename... Ts>
            requires(!detail::is_any_value_v<Ret> && !detail::is_any_args_v<Ret>
                     && !detail::is_connection_type_arg_v<Ret>
                     && (!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        bool call_static(Ret& ret, Ts&&... vs) const {
            return call_static(Name.view(), ret, std::forward<Ts>(vs)...);
        }

        /// @brief Call a STATIC member by a compile-time name, capturing the return value, typed.
        template <detail::fixed_string_t Name, typename Ret, typename... Ts>
            requires(!detail::is_any_value_v<Ret> && !detail::is_any_args_v<Ret>
                     && !detail::is_connection_type_arg_v<Ret>
                     && (!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        bool call_static(Ret& ret, connection_type_t type, Ts&&... vs) const {
            return call_static(Name.view(), ret, type, std::forward<Ts>(vs)...);
        }

        /// @brief The erased twin of the compile-time-name form.
        template <detail::fixed_string_t Name>
        any_t call_static(std::span<const any_t> args) const {
            return call_static(Name.view(), args);
        }

        /// @brief The tagged erased twin of the compile-time-name form.
        template <detail::fixed_string_t Name>
        any_t call_static(connection_type_t type, std::span<const any_t> args) const {
            return call_static(Name.view(), type, args);
        }

        /// @brief The erased `any_t&` twin of the compile-time-name form.
        template <detail::fixed_string_t Name>
        bool call_static(any_t& ret, std::span<const any_t> args) const {
            return call_static(Name.view(), ret, args);
        }

        /// @brief The tagged erased `any_t&` twin of the compile-time-name form.
        template <detail::fixed_string_t Name>
        bool call_static(any_t& ret, connection_type_t type, std::span<const any_t> args) const {
            return call_static(Name.view(), ret, type, args);
        }

        /// @brief Invoke a STATIC member by its SIGNATURE (the Auto form).
        /// @param signature The normalised signature, e.g. "make(int)" — a static member's key carries
        ///        NO cvref suffix (it has no implicit object to qualify).
        /// @param vs The arguments.
        /// @return As call_static(std::string_view, Ts&&...) const; the row is found by SIGNATURE
        ///         (method()), own-first through the chain, exactly like invoke().
        template <typename... Ts>
            requires((!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        any_t invoke_static(std::string_view signature, Ts&&... vs) const {
            return invoke_static(signature, connection_type_flag::AUTO, std::forward<Ts>(vs)...);
        }

        /// @brief Invoke a STATIC member by signature with a caller-chosen connection type.
        template <typename... Ts>
            requires((!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        any_t invoke_static(std::string_view signature, connection_type_t type, Ts&&... vs) const {
            const meta_method_t row = method(signature);
            if (!row.is_valid()) {
                return any_t{};
            }
            return row.invoke_static(type, std::forward<Ts>(vs)...);
        }

        /// @brief Invoke a STATIC member by signature, capturing the return value (the Auto form).
        template <typename Ret, typename... Ts>
            requires(!detail::is_any_value_v<Ret> && !detail::is_any_args_v<Ret>
                     && !detail::is_connection_type_arg_v<Ret>
                     && (!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        bool invoke_static(std::string_view signature, Ret& ret, Ts&&... vs) const {
            return invoke_static(signature, ret, connection_type_flag::AUTO, std::forward<Ts>(vs)...);
        }

        /// @brief Invoke a STATIC member by signature, capturing the return value, typed connection.
        template <typename Ret, typename... Ts>
            requires(!detail::is_any_value_v<Ret> && !detail::is_any_args_v<Ret>
                     && !detail::is_connection_type_arg_v<Ret>
                     && (!detail::is_any_args_v<Ts> && ...)
                     && (!detail::is_connection_type_arg_v<Ts> && ...))
        bool invoke_static(std::string_view signature, Ret& ret, connection_type_t type,
                           Ts&&... vs) const {
            const meta_method_t row = method(signature);
            if (!row.is_valid()) {
                return false;
            }
            return row.invoke_static(ret, type, std::forward<Ts>(vs)...);
        }

        /// @brief Invoke a STATIC member by signature with one any_t per parameter.
        any_t invoke_static(std::string_view signature, std::span<const any_t> args) const {
            const meta_method_t row = method(signature);
            if (!row.is_valid()) {
                return any_t{};
            }
            return row.invoke_static(args);
        }

        /// @brief The tagged twin of the erased signature form.
        any_t invoke_static(std::string_view signature, connection_type_t type,
                            std::span<const any_t> args) const {
            const meta_method_t row = method(signature);
            if (!row.is_valid()) {
                return any_t{};
            }
            return row.invoke_static(type, args);
        }

        /// @brief The erased signature form with a caller-supplied return slot.
        bool invoke_static(std::string_view signature, any_t& ret,
                           std::span<const any_t> args) const {
            const meta_method_t row = method(signature);
            if (!row.is_valid()) {
                return false;
            }
            return row.invoke_static(ret, args);
        }

        /// @brief The tagged twin of the erased signature `any_t&` form.
        bool invoke_static(std::string_view signature, any_t& ret, connection_type_t type,
                           std::span<const any_t> args) const {
            const meta_method_t row = method(signature);
            if (!row.is_valid()) {
                return false;
            }
            return row.invoke_static(ret, type, args);
        }

        /// @}

        /// @brief Normalise a type name (forwards; the body needs the rewriting table).
        std::string normalized_type(std::string_view type) const;

    private:
        // The table is an implementation seam, not API. These are the library's own readers:
        //  - the five handle classes answer their accessors from the row they point at;
        //  - object_t is the object half of the same seam.
        // Everything outside the library goes through the public queries above; no public
        // accessor hands the table out, so no column becomes part of the contract by accident.
        friend class meta_method_t;
        friend class meta_constructor_t;
        friend class meta_property_t;
        friend class meta_class_info_t;
        friend class meta_enum_t;
        friend class object_t;

        /// @brief The underlying compile-time table (the address handles take).
        /// @note noexcept, constexpr. PRIVATE: see the friend note above.
        constexpr const meta_info_t& info() const noexcept {
            return _info;
        }

        /// @brief The row a handle at @p index points at in one column of the declaring table.
        /// @tparam Row The column's element type (method_info_t / enum_info_t / class_info_t / ...).
        /// @param owner The DECLARING meta-object; may be nullptr.
        /// @param index The local slot inside @p owner's own table, or index_npos.
        /// @param column The info() column the handle reads, e.g. &meta_info_t::method_infos.
        /// @return The row, or nullptr when @p owner is null, @p index is index_npos, or @p index is
        ///         past the column's size.
        /// @note The one slot guard behind the handle accessors: a handle can carry index_npos (a
        ///       synthesised property accessor) or a stale slot, and both must answer defensively
        ///       instead of indexing the table. Private because the guard reads the private info();
        ///       the five handle classes are friends.
        template <typename Row>
        static const Row* row_at(const meta_object_t* owner, index_t index,
                                 detail::compiling_span_t<const Row> meta_info_t::* column) noexcept {
            if (owner == nullptr || index == index_npos) {
                return nullptr;
            }
            const std::span<const Row> rows = (owner->info().*column).to_span();
            if (index >= rows.size()) {
                return nullptr;
            }
            return &rows[static_cast<std::size_t>(index)];
        }

        /// @brief The property row at a local slot of THIS object's table.
        /// @param index The local slot inside property_infos.
        /// @return The row, or nullptr when @p index is index_npos or out of range.
        /// @note noexcept. PRIVATE: this is the one table access meta_property_t has, and it exposes
        ///       an internal table row, so it must not become API; meta_property_t is a friend. The
        ///       row lives in a meta_info_t that this meta-object owns by value, so the pointer stays
        ///       valid as long as the meta-object does.
        const detail::property_info_t* property_entry(index_t index) const noexcept;

        /// @brief Global index of an entry along a chain, parents first.
        /// @param object The chain head (the most derived class); may be nullptr.
        /// @param local Returns the local slot inside one class's own table, or index_npos.
        /// @param size Returns the number of entries one class's own table holds.
        /// @return The global index, or index_npos when no class in the chain has the entry.
        /// @note Parents first, matching the layout *(index) walks: a base-class entry precedes a
        ///       same-keyed entry of a derived class. The global index of a hit is the number of
        ///       entries declared by its ancestors plus its local slot, so the two entry points
        ///       (index_of_* and *(index)) agree.
        /// @note A private static member rather than a free function: the walk reads `info()`, so it
        ///       belongs to the class that owns the table.
        template <typename Local, typename Size>
        static index_t global_index(const meta_object_t* object, const Local& local, const Size& size) noexcept;

        /// @brief Resolve a global index to the meta-object that declares the entry plus its local slot.
        /// @param object The chain head (the most derived class); may be nullptr.
        /// @param index The global index (parents first).
        /// @param size Returns the number of entries one class's own table holds.
        /// @return The declaring meta-object and the local slot; (nullptr, index_npos) when out of
        ///         range. The returned pointer is a real chain link, so handles built from it name a
        ///         meta-object the caller can keep comparing against.
        /// @note The first entries of the chain belong to the ROOT class: the parent counts are
        ///       subtracted before the current class's own table is considered, which is the mirror
        ///       image of global_index(). A private static member for the same reason as
        ///       global_index().
        template <typename Size>
        static std::pair<const meta_object_t*, index_t> resolve_index(const meta_object_t* object, index_t index,
                                                                      const Size& size) noexcept;

        /// @brief Whether @p signature names a method whose DECLARED parameters accept @p Ts on
        ///        an object with @p object_qualifier.
        /// @param signature The normalised signature the caller asked for.
        /// @param object_qualifier The object's OWN cvref, read from `T` by the typed invoke form
        ///        (get_param_qualifier\<T&&\>()) BEFORE the object crosses into the handle layer.
        /// @return true when a row of the chain carries that signature and its parameter list matches
        ///         the argument types AND its own cvref accepts that object; false otherwise (one
        ///         Warning then).
        /// @note The thunk reinterprets each argument ADDRESS as the declared parameter type, so a
        ///       mismatch is not a conversion — it is undefined behaviour (a `double` handed to a
        ///       `long` parameter would be read back as the wrong bytes and the call would still
        ///       report success). Validating here is what makes the typed invoke safe.
        /// @note This is also where an argument type with no canonical spelling is REJECTED:
        ///       check_args<Ts...>() instantiates arg_spelling<Ts>(), whose namer static_asserts —
        ///       the same rule the table build applies to a parameter type, and the reason the Queued
        ///       registration below needs no separate guard.
        template <typename... Ts>
        bool check_invoke_args(std::string_view signature, qualifier_t object_qualifier) const {
            const meta_method_t row = method(signature);
            if (!row.is_valid() || !row.check_args<Ts...>(object_qualifier)) {
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::WARNING,
                    "invoke: no method '" + std::string(signature) + "' accepts these arguments");
                return false;
            }
            return true;
        }

        /// @brief Report one erased invoke() that resolved no row (the erased channel's own miss).
        /// @param signature The normalised signature the caller asked for.
        /// @note The ONE diagnostic the erased invoke forms own. check_invoke_args() cannot stand in
        ///       for it: that check spells the caller's `Ts...` with arg_spelling<>() and compares the
        ///       spellings against the row, while a std::span<const any_t> carries NO Ts at all — the
        ///       boxed values are matched by the handle against the row's parameter column
        ///       (meta_method_t::method_matches_boxed). That split is why a MISMATCH is reported there, as
        ///       a bad_any_cast_t throw, and only a missing ROW is reported here.
        /// @note method() is a silent lookup, so without this record a typo and a refused call would
        ///       look the same to the caller (both answer the invalid any_t / false). The wording is
        ///       check_invoke_args()'s on purpose: it is the same question, and the typed and the
        ///       erased channel should not answer it in two different sentences.
        void report_erased_invoke_miss(std::string_view signature) const {
            HWSHQTB_LOG_RAW(log_connection(), log_level_enum::WARNING,
                "invoke: no method '" + std::string(signature) + "' accepts these arguments");
        }

        /// @brief The one construction path: resolve a constructor and run its thunk.
        /// @param signature The normalised constructor signature to resolve (the factory path), or an
        ///        EMPTY view to select the constructor by @p args instead.
        /// @param args One any_t per parameter, in declaration order; empty when @p signature is
        ///        given. Every argument must already be initialised (the caller checks that).
        /// @param out_slot The address of the `void*` that receives the new instance; the thunk's
        ///        params[0]. Must not be null.
        /// @return true when the instance was built and written through @p out_slot.
        /// @note Private: it is the erased construction channel (a raw `void*` out slot), used by the
        ///       library's own factories — object_t::create() resolves a signature it composed at run
        ///       time. Every caller outside the library goes through new_instance(args, any_t&).
        /// @note Selection by @p args scans THIS meta-object's own CONSTRUCTOR table (constructor_infos), not
        ///       the chain and not the method table: constructors are not inherited, and a base-class
        ///       entry would build a base instance. Two passes are made — the exact reference-kind pass
        ///       first, then the accommodating one — so an lvalue argument selects `T(const T&)` and an
        ///       rvalue `T(T&&)`.
        bool construct_into(std::string_view signature, std::span<const any_t> args, void* out_slot) const;

        /// @brief The table this meta-object owns and queries (by value: a meta-object is complete).
        meta_info_t _info{};
        /// @brief The meta-object of the direct base class that declares one, or nullptr.
        /// @note Points at the BASE CLASS's own meta-object — its `static_meta_object`, or the arena
        ///       entry a meta_object_builder_t produced for a run-time built base — never at a copy, so
        ///       handles taken from a base link compare equal to the handles the base itself hands out.
        /// @note Must stay a compile-time constant on the OBJECT path: only the consteval constructor
        ///       assigns it, so `static_meta_object` stays a constant initializer (OBJECT's `constinit`
        ///       depends on that).
        const meta_object_t* _super = nullptr;
    };

    /// @brief Qt's qobject_cast: T is the TARGET TYPE (not a pointer).
    /// @tparam T The target type. An interface type (one carrying [[= make_interface("...")]]) is
    ///         matched by its IID; a reflected class (OBJECT or GADGET) by its meta-object chain;
    ///         any other type falls back to dynamic_cast and therefore needs RTTI and a
    ///         polymorphic source.
    /// @tparam U The source type, as deduced from the pointer.
    /// @param object The object to cast; nullptr answers nullptr.
    /// @return A T* when the conversion is valid, nullptr otherwise.
    /// @note An interface type is matched by its IID; a class type by its meta-object chain
    ///       (OBJECT and GADGET alike); anything else falls back to dynamic_cast, which needs
    ///       RTTI and a polymorphic type.
    /// @note The dispatch order is compile time: interface first, then "both sides DECLARE a
    ///       static_meta_object" (a plain derived class only inherits one, and its own relation is
    ///       for dynamic_cast to answer), then the language cast. An interface is not a class of the
    ///       chain, so it can never be answered by the chain check; a chain class is answered by the
    ///       object's own table (which knows the full C++ inheritance relation).
    template <typename T, typename U>
    T* cast(U* object) noexcept {
        if (object == nullptr) {
            return nullptr;
        }
        if constexpr (detail::is_interface_type<T>()) {
            // An interface is one IID row inside the chain, not a class of it: walk the chain of
            // U's table (runtime for object_t-derived, static for a GADGET) and run the matching
            // entry's type-erased thunk.
            static_assert(detail::has_static_meta_object_v<U> || requires { object->meta_object(); },
                "hwshqtb::metaobject::cast<T>(U*): T is an interface type, so U must be a meta type "
                "(an object_t-derived class or a type with a static_meta_object): an interface can "
                "only be recovered from a meta-object chain");
            const meta_object_t* const table = detail::meta_object_of(object);
            void* const sub = (table == nullptr)
                ? nullptr
                : table->cast(static_cast<void*>(object), interface_iid_of<T>());
            return static_cast<T*>(sub);
        }
        else if constexpr (detail::declares_static_meta_object<T>()
                           && detail::declares_static_meta_object<U>()) {
            if constexpr (std::is_convertible_v<U*, T*>) {
                // The compiler already knows the relation (T is a base of U, or the same type), and
                // that answer is exact: a GADGET is not polymorphic, so its static type IS its real
                // type and no table could add anything. This also makes the ordinary up-cast free for
                // OBJECT classes.
                return static_cast<T*>(object);
            }
            else {
                // Otherwise the question is a down-cast (or a cross-branch one): the table's chain
                // check answers it, and the erased pointer it returns is exactly the T* to hand back.
                return static_cast<T*>(T::static_meta_object.cast(object));
            }
        }
        else {
            // Either side is an ordinary type (including a plain derived class that only INHERITS
            // a static_meta_object): the language's own RTTI cast is the general answer, and it is
            // also the one that applies the base-to-derived pointer adjustment.
            return dynamic_cast<T*>(object);
        }
    }

    /// @brief The const form of cast<T>(U*): same dispatch, returns a const T*.
    /// @tparam T The target type, which must be const-qualified (e.g. const IFoo).
    /// @tparam U The source type, as deduced from the const pointer.
    /// @param object The object to cast; nullptr answers nullptr.
    /// @return A const T* when the conversion is valid, nullptr otherwise.
    /// @note It never casts const away: a const source plus a non-const T is a compile error (write
    ///       cast<const T>(p), or hand it a non-const pointer).
    /// @note The interface thunk is type-erased with a void* parameter, so the const pointer is
    ///       re-erased for the call; the result is handed back as a pointer to const, and nothing
    ///       mutable is ever returned for a const source.
    template <typename T, typename U>
    const T* cast(const U* object) noexcept {
        static_assert(std::is_const_v<T>,
            "hwshqtb::metaobject::cast<T>(const U*): T must be const-qualified; the const overload "
            "does not cast const away. Write cast<const T>(p), or pass a non-const pointer.");
        if (object == nullptr) {
            return nullptr;
        }
        if constexpr (detail::is_interface_type<T>()) {
            static_assert(detail::has_static_meta_object_v<U> || requires { object->meta_object(); },
                "hwshqtb::metaobject::cast<T>(const U*): T is an interface type, so U must be a meta "
                "type (an object_t-derived class or a type with a static_meta_object): an interface "
                "can only be recovered from a meta-object chain");
            const meta_object_t* const table = detail::meta_object_of(object);
            void* const sub = (table == nullptr)
                ? nullptr
                : table->cast(const_cast<void*>(static_cast<const void*>(object)),
                      interface_iid_of<T>());
            return static_cast<const T*>(sub);
        }
        else if constexpr (detail::declares_static_meta_object<T>()
                           && detail::declares_static_meta_object<U>()) {
            if constexpr (std::is_convertible_v<const U*, const T*>) {
                // Same short-circuit as the non-const overload: the compiler's own answer is exact,
                // and a GADGET has no runtime table that could know more.
                return static_cast<const T*>(object);
            }
            else {
                return static_cast<const T*>(T::static_meta_object.cast(object));
            }
        }
        else {
            return dynamic_cast<const T*>(object);
        }
    }
}

#endif
