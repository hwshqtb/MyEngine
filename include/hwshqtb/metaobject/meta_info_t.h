#ifndef HWSHQTB__METAOBJECT__META_INFO_H
#define HWSHQTB__METAOBJECT__META_INFO_H

/// @file meta_info_t.h
/// @brief Meta-object information structure and related utilities for runtime reflection and invocation.

#include "annotation.h"
#include "chd_hash.hpp"
#include "reflection.hpp"
#include "register_type.hpp"
#include "basic_def.h"
#include "cvref_meta_type_t.h"
#include "log.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <meta>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace hwshqtb::metaobject {
    class meta_method_t;
    class meta_property_t;
    class meta_enum_t;

    /// @brief A class-level key/value pair view (defined in meta_class_info_t.h).
    class meta_class_info_t;

    /// @brief The object base class (defined in object_t.h).
    class object_t;

    /// @brief The run-time meta-object that owns one meta_info_t (defined in meta_object_t.h).
    /// @note Forward-declared for the code that names the owner of a table; the table itself keeps no
    ///       pointer to it — the inheritance chain lives on meta_object_t.
    class meta_object_t;

    /// @brief The compile-time meta-object table (defined below).
    struct meta_info_t;

    namespace detail {
        
        /// @brief The namespace-qualified spelling of a type, e.g. "my::ui::widget_t".
        /// @param type The type reflection (the class itself, not a member).
        /// @return The identifiers of its enclosing classes and namespaces, outermost first, joined by
        ///         "::" and ending with the type's own identifier. A type in the global namespace
        ///         answers just its identifier, so a global-namespace class keys exactly as before.
        /// @note The registry keys on this and a constructor signature starts with it: a bare
        ///       identifier is not unique across namespaces.
        consteval std::string qualified_name_of(std::meta::info type) {
            std::string out(std::meta::identifier_of(type));
            std::meta::info parent = std::meta::parent_of(type);
            while (parent != std::meta::info{}) {
                // Stop at anything that is not a class or namespace (a local class inside a function
                // has a function as its parent, and that contributes no prefix). The GLOBAL namespace
                // has no identifier and is where the walk ends — parent_of() is not asked of it.
                if (!std::meta::is_namespace(parent) && !std::meta::is_class_type(parent)) {
                    break;
                }
                if (!std::meta::has_identifier(parent)) {
                    break;
                }
                out = std::string(std::meta::identifier_of(parent)) + "::" + out;
                parent = std::meta::parent_of(parent);
            }
            return out;
        }

        template <typename>
        struct function_params_traits;

        /// @brief The parameters of a STATIC member function: `&T::static_fn` has no class in its
        ///        type, it is an ordinary function pointer, so it needs its own specialisation.
        /// @note The four flags are false by construction: a static member function has no implicit
        ///       object and therefore nothing to cv- or ref-qualify. static_caller() reads args_type
        ///       (to expand the pack) and ret_type (for the return-slot protocol); it never reads the
        ///       flags, but they are part of the function_params_traits shape both callers share.
        template <class Ret, typename... Args>
        struct function_params_traits<Ret (*)(Args...)> {
            using args_type = std::tuple<Args...>;
            using ret_type = Ret;
            static constexpr bool is_const_qualified = false;
            static constexpr bool is_volatile_qualified = false;
            static constexpr bool is_lvalue_qualified = false;
            static constexpr bool is_rvalue_qualified = false;
        };

        template <class Ret, typename... Args>
        struct function_params_traits<Ret (*)(Args...) noexcept> {
            using args_type = std::tuple<Args...>;
            using ret_type = Ret;
            static constexpr bool is_const_qualified = false;
            static constexpr bool is_volatile_qualified = false;
            static constexpr bool is_lvalue_qualified = false;
            static constexpr bool is_rvalue_qualified = false;
        };

#define FUNCTION_PARAMS_HELPER(code, is_const_, is_volatile_, is_lref_, is_rref_) \
    template <class T, typename Ret, typename... Args>                          \
    struct function_params_traits<Ret (T::*)(Args...) code> {                           \
        using args_type = std::tuple<Args...>;                                  \
        using ret_type = Ret;                                                   \
        static constexpr bool is_const_qualified = is_const_;                   \
        static constexpr bool is_volatile_qualified = is_volatile_;             \
        static constexpr bool is_lvalue_qualified = is_lref_;                   \
        static constexpr bool is_rvalue_qualified = is_rref_;                   \
    }

        FUNCTION_PARAMS_HELPER(, false, false, false, false);
        FUNCTION_PARAMS_HELPER(const, true, false, false, false);
        FUNCTION_PARAMS_HELPER(volatile, false, true, false, false);
        FUNCTION_PARAMS_HELPER(const volatile, true, true, false, false);
        FUNCTION_PARAMS_HELPER(noexcept, false, false, false, false);
        FUNCTION_PARAMS_HELPER(const noexcept, true, false, false, false);
        FUNCTION_PARAMS_HELPER(volatile noexcept, false, true, false, false);
        FUNCTION_PARAMS_HELPER(const volatile noexcept, true, true, false, false);
        FUNCTION_PARAMS_HELPER(&, false, false, true, false);
        FUNCTION_PARAMS_HELPER(const &, true, false, true, false);
        FUNCTION_PARAMS_HELPER(volatile &, false, true, true, false);
        FUNCTION_PARAMS_HELPER(const volatile &, true, true, true, false);
        FUNCTION_PARAMS_HELPER(& noexcept, false, false, true, false);
        FUNCTION_PARAMS_HELPER(const & noexcept, true, false, true, false);
        FUNCTION_PARAMS_HELPER(volatile & noexcept, false, true, true, false);
        FUNCTION_PARAMS_HELPER(const volatile & noexcept, true, true, true, false);
        FUNCTION_PARAMS_HELPER(&&, false, false, false, true);
        FUNCTION_PARAMS_HELPER(const &&, true, false, false, true);
        FUNCTION_PARAMS_HELPER(volatile &&, false, true, false, true);
        FUNCTION_PARAMS_HELPER(const volatile &&, true, true, false, true);
        FUNCTION_PARAMS_HELPER(&& noexcept, false, false, false, true);
        FUNCTION_PARAMS_HELPER(const && noexcept, true, false, false, true);
        FUNCTION_PARAMS_HELPER(volatile && noexcept, false, true, false, true);
        FUNCTION_PARAMS_HELPER(const volatile && noexcept, true, true, false, true);

#undef FUNCTION_PARAMS_HELPER

        using generic_call = void (*)(void*, void**);

        // ---- signature text ------------------------------------------------------------------
        //
        // A signature is "name(types)" PLUS the member's cvref suffix, in C++ spelling:
        //   "f(int)"  /  "f(int) const"  /  "f(int) const &"  /  "f(int) &&"  /  "f(int) const volatile &"
        //
        // Constructors have no cvref suffix: a constructor's key stays "class_t(int)".
        //
        // Lookups are EXACT: index_of_method() / method() / signal() / connect() compare the
        // canonical text byte for byte, so "f()" does NOT name "f() const"; there is no
        // ignore-cvref fallback.

        /// @brief "name(t1,t2)" split into the name and the parameters.
        struct signature_parts_t {
            /// @brief The identifier before '('.
            std::string_view name;
            /// @brief The parameters, split on commas at bracket depth 0.
            std::vector<std::string_view> parameters;
        };

        /// @brief Split a signature, honouring nested brackets.
        /// @param signature Text of the form "name(t1,t2)", optionally with a return type and a
        ///        qualified name in front ("void S::f(int)") and a cvref suffix behind
        ///        ("f(int) const &").
        /// @return The extracted name and the untrimmed parameter pieces.
        /// @note Only the name and the parameter list are extracted; the trailing cvref suffix is
        ///       normalized_signature()'s business.
        /// @note constexpr: normalized_signature() calls it and must stay usable in a constant
        ///       expression.
        constexpr signature_parts_t split_signature(std::string_view signature) {
            signature_parts_t parts;
            const std::size_t open = signature.find('(');
            if (open == std::string_view::npos) {
                parts.name = signature;
                return parts;
            }
            std::size_t name_end = open;
            while (name_end > 0 && is_space(signature[name_end - 1])) {
                --name_end;
            }
            std::size_t name_start = name_end;
            while (name_start > 0 && is_ident_continue(signature[name_start - 1])) {
                --name_start;
            }
            parts.name = signature.substr(name_start, name_end - name_start);

            const std::size_t close = signature.rfind(')');
            if (close == std::string_view::npos || close <= open + 1) {
                return parts;
            }
            const std::string_view body = signature.substr(open + 1, close - open - 1);
            std::size_t start = 0;
            for (;;) {
                std::size_t comma = std::string_view::npos;
                int depth = 0;
                for (std::size_t i = start; i < body.size(); ++i) {
                    const char c = body[i];
                    if (c == '<' || c == '(' || c == '[' || c == '{') {
                        ++depth;
                    }
                    else if (c == '>' || c == ')' || c == ']' || c == '}') {
                        if (depth > 0) {
                            --depth;
                        }
                    }
                    else if (c == ',' && depth == 0) {
                        comma = i;
                        break;
                    }
                }
                parts.parameters.emplace_back((comma == std::string_view::npos)
                    ? body.substr(start)
                    : body.substr(start, comma - start));
                if (comma == std::string_view::npos) {
                    break;
                }
                start = comma + 1;
            }
            return parts;
        }

        struct compiling_string_t {
            const char* data = nullptr;
            std::size_t size = 0;

            constexpr compiling_string_t() = default;
            constexpr compiling_string_t(const compiling_string_t& other) = default;
            constexpr compiling_string_t(compiling_string_t&&) = default;
            constexpr ~compiling_string_t() = default;
            constexpr compiling_string_t& operator=(const compiling_string_t&) = default;
            constexpr compiling_string_t& operator=(compiling_string_t&&) = default;

            constexpr compiling_string_t(std::string_view sv) noexcept:
                data(sv.data()),
                size(sv.size()) {}

            constexpr operator std::string_view() const noexcept {
                return std::string_view(data, size);
            }
        };

        /// @brief A span in its consteval-friendly form: plain data (structural), so
        ///        std::define_static_array can build one at compile time; converts to std::span on
        ///        demand.
        /// @note The default size is Extent, or 0 for a dynamic extent, so an unassigned column is
        ///       empty: every consumer may rely on "unassigned column == empty".
        template <typename T, std::size_t Extent = std::dynamic_extent>
        struct compiling_span_t {
            T* data = nullptr;
            std::size_t size = (Extent == std::dynamic_extent) ? 0 : Extent;

            constexpr compiling_span_t() = default;
            constexpr compiling_span_t(const compiling_span_t&) = default;
            constexpr compiling_span_t(compiling_span_t&&) = default;
            constexpr ~compiling_span_t() = default;
            constexpr compiling_span_t& operator=(const compiling_span_t&) = default;
            constexpr compiling_span_t& operator=(compiling_span_t&&) = default;

            constexpr compiling_span_t(std::span<T, Extent> sp) noexcept:
                data(sp.data()),
                size(sp.size()) {}

            constexpr operator std::span<T, Extent>() const noexcept {
                return std::span<T, Extent>(data, size);
            }

            constexpr std::span<T, Extent> to_span() const noexcept {
                return std::span<T, Extent>(data, size);
            }
        };

        /// @brief A built table in its consteval-friendly form: spans over static storage.
        ///
        /// The same three facts chd_hash_result_t carries, narrowed to what a lookup needs: the seed
        /// pair, one displacement per bucket (std::uint32_t(-1) = empty bucket) and one redirect
        /// entry per slot (std::uint32_t(-1) = empty slot).
        ///
        /// A hit answers the key's ORIGINAL position — the index it had in the key list the table was
        /// built from — because redirect[] stores that position.
        struct hashtable_t {
            static constexpr std::uint64_t npos = std::uint64_t(-1);

            /// @brief Bucket-hash seed; npos while the table is empty/unbuilt.
            std::uint64_t seed0 = npos;
            /// @brief Slot-hash seed; npos while the table is empty/unbuilt.
            std::uint64_t seed1 = npos;
            /// @brief Buckets: one displacement per key slot in the bucket space.
            compiling_span_t<const std::uint32_t> displacements;
            /// @brief Slots: redirect[slot] is the key's original index, std::uint32_t(-1) if free.
            compiling_span_t<const std::uint32_t> redirect;

            /// @brief Look @p key up.
            /// @return The key's original index, or npos when the table is empty, the bucket has no
            ///         displacement, or the slot is empty.
            /// @note Same logic as detail::find(); a hit is still verified against the stored key by
            ///       the caller, because a missing key can land on an occupied slot.
            constexpr std::uint64_t find(const std::string_view key) const noexcept {
                if (seed0 == npos || seed1 == npos) return npos;

                const std::size_t buckets = displacements.to_span().size();
                const std::size_t slots = redirect.to_span().size();
                if (buckets == 0 || slots == 0) return npos;

                fnv1a_hash_t h0;
                h0.prefix = seed0;
                fnv1a_hash_t h1;
                h1.prefix = seed1;

                const std::span<const char> view(key.data(), key.size());

                const std::size_t bucket = static_cast<std::size_t>(h0(view) % buckets);

                const std::uint32_t d = displacements.to_span()[bucket];
                if (d == std::uint32_t(-1)) return npos;

                const std::size_t slot = (static_cast<std::size_t>(h1(view) % slots) + d) % slots;
                const std::uint32_t pos = redirect.to_span()[slot];
                if (pos == std::uint32_t(-1)) return npos;

                return pos;
            }
        };

        /// @brief Whether @p T carries a [[= make_interface("...")]] annotation, i.e. is an
        ///        interface type.
        /// @note The annotation is the ONE definition of an interface (make_interface). A class
        ///       implements an interface by inheriting it PUBLICLY; build_interface_infos() reads
        ///       this same annotation off each direct base class.
        template <typename T>
        consteval bool is_interface_type() {
            constexpr auto annotations =
                std::define_static_array(std::meta::annotations_of_with_type(^^T, ^^interface_a));
            return !annotations.empty();
        }

        /// @brief Whether @p T DECLARES its own static meta-object table (OBJECT / GADGET).
        /// @note "Declares" is the important part: a static data member is inherited, so
        ///       `requires { T::static_meta_object; }` also holds for every plain derived class —
        ///       a class that never expanded OBJECT/GADGET and has no table of its own. Naming the
        ///       inherited one would answer the BASE's chain question while the caller asked about
        ///       T (and would hand back the object_t address as if it were T*, which is a wrong
        ///       pointer when object_t is not the first base). members_of(^^T) keeps the entries in
        ///       the class that DECLARED them.
        /// @note The type comparison also tells the interface-base scan (build_interface_infos)
        ///       that a base is the class's meta-object parent, not an interface: a parent is linked
        ///       separately by meta_object_t's consteval constructor and contributes no interface row.
        /// @note consteval: every caller reads it from an if constexpr branch.
        template <typename T>
        consteval bool declares_static_meta_object() {
            if constexpr (std::meta::is_class_type(^^T)) {
                static constexpr auto ctx = std::meta::access_context::unchecked();
                static constexpr auto members =
                    std::define_static_array(std::meta::members_of(^^T, ctx));
                template for (constexpr auto member: members) {
                    if (std::meta::is_variable(member) && std::meta::is_static_member(member)
                        && std::meta::has_identifier(member)
                        && std::meta::identifier_of(member) == "static_meta_object"
                        && std::meta::type_of(member) == ^^const meta_object_t) {
                        return true;
                    }
                }
            }
            return false;
        }

        /// @brief Whether T DECLARES the virtual accessor meta_object() — i.e. it expanded OBJECT.
        /// @return true when T's OWN members hold a `meta_object()` function returning
        ///         `const meta_object_t*`, false otherwise.
        /// @note "Declares" is the whole point: the accessor is a public virtual, so every class
        ///       BELOW an OBJECT class inherits it as well. GADGET declares only the static table, so
        ///       it never answers true here even though it has rows (its table is reached through
        ///       `GADGET::static_meta_object`, not through the chain).
        /// @note A non-virtual function of that exact name and signature also answers true: what the
        ///       chain follows is the accessor, and OBJECT's is virtual only because DYNAMIC_BASE
        ///       overrides it.
        template <typename T>
        consteval bool declares_meta_object_accessor() {
            if constexpr (std::meta::is_class_type(^^T)) {
                static constexpr auto ctx = std::meta::access_context::unchecked();
                static constexpr auto members =
                    std::define_static_array(std::meta::members_of(^^T, ctx));
                template for (constexpr auto member: members) {
                    if (std::meta::is_function(member) && std::meta::has_identifier(member)
                        && std::meta::identifier_of(member) == "meta_object"
                        && std::meta::parameters_of(member).size() == 0
                        && std::meta::return_type_of(member) == ^^const meta_object_t*) {
                        return true;
                    }
                }
            }
            return false;
        }

        /// @brief Whether T HAS the accessor, declared by T or inherited from an ancestor.
        /// @note One level is enough to find a broken chain: the accessor is a public virtual, so a
        ///       class that has it without declaring it necessarily sits below an OBJECT class — that
        ///       is exactly the "non-OBJECT class between two OBJECT classes" case. Conversely, a
        ///       class that does not have it has no OBJECT ancestor at all.
        /// @note A privately-inherited OBJECT base makes the call inaccessible, so it answers false;
        ///       such a base is a misuse on its own (meta_object() could not be called through it).
        template <typename T>
        consteval bool has_meta_object_accessor() {
            if constexpr (std::meta::is_class_type(^^T)) {
                return requires(const T& object) {
                    { object.meta_object() } -> std::same_as<const meta_object_t*>;
                };
            }
            return false;
        }

        /// @brief One interface a class implements: an accessible direct base carrying a
        ///        [[= make_interface("...")]] annotation.
        /// @note The members must stay public: define_static_array requires a structural element type.
        struct interface_entry_t {
            /// @brief The IID, e.g. "com.example.IFoo/1.0".
            /// @note Points into static storage: build_meta copies the interface's IID text with
            ///       define_static_string, exactly like every other table key.
            compiling_string_t iid;
            /// @brief Down-cast the type-erased object to the interface pointer through the concrete
            ///        class.
            /// @note The parameter is `void*`, not `object_t*`: build_meta<T>() serves OBJECT and
            ///       GADGET alike, and a gadget is not an object_t, so an object_t-bound thunk could
            ///       never be formed for it. Erasing the pointer keeps one thunk shape for both.
            /// @note nullptr only for a default-constructed entry; build_meta never emits one.
            void* (*cast)(void*) = nullptr;
        };

        /// @brief The cast thunk: void* -> I* down-cast through the concrete class T.
        /// @tparam T The class that implements the interface (the class whose base scan emitted the
        ///         row: the most derived type for an entry taken from T's own table, an ancestor for
        ///         an entry inherited from the chain).
        /// @tparam I The interface type.
        /// @param object The object, type-erased. For an object_t class it is the object_t sub-object
        ///        address, which is also the complete object's address because object_t is the first
        ///        base; for a gadget it is the gadget itself.
        /// @return The interface sub-object pointer of @p object.
        /// @note static_cast, not dynamic_cast: the relation is known at compile time, so the
        ///       conversion is the ordinary base-class pointer adjustment and needs no RTTI.
        /// @note The pointer is recovered with `static_cast<T*>(void*)`, which is valid for any
        ///       complete object type — that is what lets a GADGET implement interfaces too.
        /// @note Only an accessible (public) interface base ever instantiates this thunk: the base
        ///       scan skips the others (access_context::unprivileged()), so the conversion is always
        ///       one a consumer could write itself.
        template <typename T, typename I>
        void* interface_cast(void* object) noexcept {
            return static_cast<I*>(static_cast<T*>(object));
        }

        struct type_rewriting_t {
            compiling_string_t from;
            compiling_string_t to;
            qualifier_t modifiers = qualifier_flag::NONE;
        };

        /// @note Construction knowledge lives in each constructor entry's `caller` thunk
        ///       (constructor_info_t): `new T(...)` at the one place that knows T. There are deliberately no
        ///       allocation hooks on the meta-object or on this table.
        template <typename U>
        constexpr std::string_view arg_spelling() {
            using R = std::decay_t<U>;
            return std::define_static_string(detail::normalized_type_name(std::meta::dealias(^^R)));
        }

        /// @brief One flattened tag annotation (tag_a) of a method.
        /// @note A std::meta::info is consteval-only and cannot live in a runtime table, so the
        ///       build pass records the value's TYPE NAME instead; the pointer is the staticised
        ///       value itself (make_tag / make_str_tag).
        /// @note The members must stay public and structural: define_static_array requires it.
        struct annotation_entry_t {
            /// @brief The normalised type name of the carried value (baseline: "int").
            compiling_string_t type_name;
            /// @brief The staticised value; nullptr only for a default-constructed entry.
            const void* value = nullptr;
        };

        /// @brief The method table's row: one callable that a meta_method_t describes.
        /// @note Carries the columns a MEMBER FUNCTION has: its own cvref, a return type and a
        ///       method_kind_enum (the signal query and has_receiver() derive from the latter).
        /// @note The members must stay public and structural: define_static_array requires it.
        struct method_info_t {
            detail::compiling_string_t signature;
            detail::compiling_string_t name;
            /// @brief The member's own cvref (its implicit-object qualifiers).
            qualifier_t qualifier = qualifier_flag::NONE;
            detail::compiling_span_t<const detail::compiling_string_t> param_types;
            /// @brief Parameter names; an entry is empty when the parameter is unnamed.
            detail::compiling_span_t<const detail::compiling_string_t> param_names;
            detail::compiling_span_t<const qualifier_t> param_qualifiers;
            detail::compiling_string_t return_type;
            /// @brief The entry's single invoker.
            /// @note A method or a signal is called on an existing object: caller(obj, args),
            ///       with args[0] the return-value slot and args[1...] the argument addresses.
            /// @note A STATIC_METHOD row keeps the SAME thunk shape: it is still called with the
            ///       caller's object argument (which it must ignore), so the generic_call protocol has
            ///       one shape for every method row and the dispatch never has to branch on kind.
            generic_call caller = nullptr;
            /// @brief What kind of function this is; the signal query and has_receiver() derive from it.
            method_kind_enum kind = method_kind_enum::PLAIN_METHOD;
            /// @brief The declared access of the entry (method_access_enum), a hint and never a gate.
            /// @note Recorded at build time from the member reflection (members_of with
            ///       access_context::unchecked()), so it reports the DECLARED specifier, not what the
            ///       current access context allows.
            /// @note A run-time built entry (meta_object_builder_t) keeps this default: the builder carries no
            ///       access information, so it reports PUBLIC_SCOPE.
            method_access_enum access = method_access_enum::PUBLIC_SCOPE;
            /// @brief The tag annotations of this method (make_tag / make_str_tag), flattened.
            /// @note Empty for an un-annotated method (and for a run-time built entry).
            /// @note Read through meta_method_t::tag(type_name): a string match on type_name that
            ///       returns the staticised value pointer.
            detail::compiling_span_t<const annotation_entry_t> annotations;

            /// @brief Whether this row is called ON an object.
            /// @return false exactly for STATIC_METHOD: a static member has no implicit object.
            /// @note constexpr and noexcept: the kind column is plain data, so the predicate is usable
            ///       inside a constant expression — the compile-time matcher reads it too
            ///       (detail::callable_check_args), which is what SKIPS the object-cvref block for a
            ///       receiver-less row.
            constexpr bool has_receiver() const noexcept {
                return kind != method_kind_enum::STATIC_METHOD;
            }
        };

        /// @brief The constructor table's row: one constructor that a meta_constructor_t describes.
        /// @note A DIFFERENT type, deliberately unrelated to method_info_t: a constructor has no return
        ///       type, cannot be cv/ref-qualified, and has no method_kind_enum (its kind is answered by the
        ///       handle type). Keeping the columns that cannot apply out of this struct is the point of
        ///       the split.
        /// @note The members must stay public and structural: define_static_array requires it.
        struct constructor_info_t {
            detail::compiling_string_t signature;
            detail::compiling_string_t name;
            detail::compiling_span_t<const detail::compiling_string_t> param_types;
            /// @brief Parameter names; an entry is empty when the parameter is unnamed.
            detail::compiling_span_t<const detail::compiling_string_t> param_names;
            detail::compiling_span_t<const qualifier_t> param_qualifiers;
            /// @brief The entry's construction thunk.
            /// @note A constructor is called with a NULL object — there is none yet — and args[0] is
            ///       its return slot: a `void**` that receives the new instance as a `void*`. The
            ///       pointer names the constructed T, which need not be an object_t subclass — the
            ///       table describes any class — so the caller must know which class it named and
            ///       cast the result back itself. A constructor that cannot be called (a deleted one,
            ///       or any constructor of an abstract type) keeps this null, and
            ///       meta_object_t::new_instance() reports that null as a Critical "not callable".
            /// @note A NON-PUBLIC constructor is a real thunk like any other: access() reports the
            ///       specifier; it is a hint, not a gate.
            generic_call caller = nullptr;
            /// @brief The declared access of the entry (method_access_enum), a hint and never a gate.
            /// @note Recorded at build time from the member reflection (members_of with
            ///       access_context::unchecked()), so it reports the DECLARED specifier, not what the
            ///       current access context allows.
            /// @note A run-time built entry (meta_object_builder_t) keeps this default: the builder
            ///       carries no access information, so it reports PUBLIC_SCOPE.
            method_access_enum access = method_access_enum::PUBLIC_SCOPE;
            /// @brief The tag annotations of this constructor (make_tag / make_str_tag), flattened.
            /// @note Empty for an un-annotated constructor (and for a run-time built entry).
            /// @note Read through meta_constructor_t::tag(type_name): the same string match
            ///       meta_method_t::tag() performs.
            detail::compiling_span_t<const annotation_entry_t> annotations;

            /// @brief Whether this row is called ON an object.
            /// @return Always false: a constructor BUILDS an object, it is not called on one, so it
            ///         has no implicit object parameter.
            /// @note The column pair (qualifier, has_receiver) is what tells the matcher whether the
            ///       object-cvref block applies: this row type has no `qualifier` column at all, so
            ///       the block is skipped for it — and this predicate says the same thing in the
            ///       vocabulary the method row uses, so a reader asking "does this row have a
            ///       receiver" gets a true answer for both table types.
            constexpr bool has_receiver() const noexcept {
                return false;
            }
        };

        struct property_info_t {
            detail::compiling_string_t name;
            detail::compiling_string_t type;

            /// @brief The simple name of the enum this property's type names; empty when it names none.
            /// @note Recorded at build time from the real C++ type — an enum type, or the E of a
            ///       flag_t\<E\> wrapper — never by parsing the property type's spelling at run time.
            ///       is_enum_type() / is_flag_traitype() / enumerator() read this column.
            /// @note A run-time built property (meta_object_builder_t) leaves it empty; enumerator() then falls
            ///       back to the type-name tail parse, so dynamic classes keep their behaviour.
            detail::compiling_string_t enum_name;

            /// @brief Whether the property type is a flag_t\<E\> wrapper (enum_name still names E).
            /// @note A wrapper fact: the VALUE question "are this enum's enumerators a bit set" stays
            ///       on meta_enum_t::is_flag().
            bool is_flags = false;

            /// @brief Read: getters and data members share the same thunk (helpcaller / _reader_helper)
            generic_call reader = nullptr;

            /// @brief Write: the caller of a named writer; null means read-only.
            /// @note A data-member property is writable only when its annotation names a writer: this
            ///       library does not synthesise a writer for a member (see meta_property_t.h).
            generic_call writer = nullptr;
            /// @brief The named slot of writer; index_npos for a synthesised accessor, for an
            ///        inherited one, and for a name the compile-time pass could not bind.
            index_t writer_slot = index_npos;
            /// @brief The writer's BARE NAME from the annotation; empty means "no writer declared".
            /// @note Recorded even when the compile-time pass resolved it (it documents the
            ///       annotation); meta_property_t consults it exactly when writer_slot is index_npos,
            ///       i.e. for an inherited writer or a name the compile-time pass did not bind, and
            ///       resolves it at run time against the meta-object chain.
            compiling_string_t writer_name;

            /// @brief Reseter
            generic_call reseter = nullptr;
            /// @brief The named slot of reseter; index_npos for a synthesised accessor, an inherited
            ///        one, or a name the compile-time pass could not bind.
            index_t reseter_slot = index_npos;
            /// @brief The reseter's BARE NAME from the annotation; empty means "no reseter declared".
            compiling_string_t reseter_name;

            /// @brief Notifier
            generic_call notifier = nullptr;
            /// @brief The named slot of notifier; index_npos for a synthesised accessor, an inherited
            ///        one, or a name the compile-time pass could not bind.
            index_t notifier_slot = index_npos;
            /// @brief The notifier's BARE NAME from the annotation; empty means "no notifier declared".
            compiling_string_t notifier_name;

            property_trait_t traits = property_trait_flag::NONE;
        };

        /// @brief One enumerator: its name plus its underlying value.
        /// @note The members must stay public: define_static_array requires a structural element type.
        struct enum_entry_t {
            compiling_string_t name;
            std::int64_t value = 0;
        };

        /// @brief One class-level key/value pair (Q_CLASSINFO equivalent).
        struct class_info_t {
            /// @brief The key.
            compiling_string_t name;
            /// @brief The value.
            compiling_string_t value;
        };

        /// @brief The metadata of one enum.
        struct enum_info_t {
            /// @brief The enum name (the identifier declared inside the type).
            compiling_string_t name;

            /// @brief The name of the class declaring this enum (QMetaEnum::scope).
            compiling_string_t scope;

            /// @brief Whether it is a scoped enum (enum class / enum struct).
            bool is_scoped = false;

            /// @brief Whether it is a flag enum.
            /// @note Criterion: the enumerator values are pairwise disjoint under bitwise AND (each
            ///       value occupies its own bits). An enum containing a "combined value" (such as
            ///       All = A|B) is reported as false; an empty or single-entry enum is reported as
            ///       true (vacuous truth).
            bool is_flag = false;

            /// @brief The size in bytes of the enum's underlying type (1, 2, 4 or 8).
            /// @note The table widens every enumerator to std::int64_t, so an int-backed INT_MIN
            ///       enumerator is stored sign-extended. value_to_keys() masks both the query and the
            ///       enumerators to this width, which lets a 0x80000000 query match that enumerator.
            /// @note Run-time built enums (meta_object_builder_t) have no C++ underlying type; the builder
            ///       records sizeof(std::int64_t), the full width of the values it accepts.
            std::size_t underlying_size = sizeof(std::int64_t);

            /// @brief The enumerator table (declaration order).
            compiling_span_t<const enum_entry_t> entries;
        };

        /// @brief The RUNTIME matcher's rule, applied to the row at @p slot of ONE callable column.
        /// @tparam Row The column's element type: method_info_t (the method table) or
        ///         constructor_info_t (the constructor table). Both carry the columns this rule reads
        ///         (the member's own cvref, the parameter spellings / qualifiers), which is why the
        ///         rule can be written once over either.
        /// @param rows The column the slot belongs to: method_infos (methods) or constructor_infos
        ///        (constructors).
        /// @param slot A slot of @p rows; the caller guarantees it is in range.
        /// @param args The call's arguments, as (bare type, cvref) identities.
        /// @param exact true for the exact pass, false for the accommodating one.
        /// @param object_qualifier The object's OWN cvref (see meta_info_t::method_matches()).
        /// @return true when the row's parameter list matches @p args AND the row's own cvref can be
        ///         called on that object.
        /// @note THE ONE runtime rule. It is a template because there are TWO callable columns of
        ///       DIFFERENT element types: meta_info_t::method_matches() is the METHOD-column entry
        ///       point (its slot is a method_infos slot), while meta_info_t::constructor_matches() is
        ///       the CONSTRUCTOR-column one (a constructor has no meta_method_t handle). Factoring the
        ///       comparison out keeps ONE copy of the object-cvref / arity / bare-spelling /
        ///       reference-kind rule for both columns — the row TYPE differs, the RULE does not. The
        ///       compile-time twin of this rule is detail::callable_check_args().
        /// @note noexcept and ALLOCATION-FREE: both spellings are compared in place (the column is
        ///       static storage, the argument handle answers a name VIEW). spelled() is never used —
        ///       it builds a std::string.
        /// @note An INVALID argument handle names the empty string (cvref_meta_type_t's "no type"
        ///       state), which no stored parameter spelling can equal, so an unknown argument matches
        ///       nothing. The boxed channel's is_initialized() guard belongs to the handle that owns
        ///       any_t (meta_method_t::method_matches_boxed): this rule never names that value type.
        /// @note A qualifier column of size 0 imposes nothing: the run-time built tables
        ///       (meta_object_builder_t) leave param_qualifiers empty, so such a row matches on
        ///       spelling alone (there is no per-argument cvref to compare).
        /// @note The member's own cvref is tested FIRST: it is one bit per row and it is what tells
        ///       f() from f() const / f() volatile / f() && before a single parameter is looked at.
        ///       A constructor row reaches this block with qualifier_flag::NONE (it has no cvref
        ///       column), and a None member accepts every object cvref, so the block is a no-op there.
        /// @note A row with NO RECEIVER (a static member function) skips the block whole: there is no
        ///       implicit object for a cvref to describe, so the call's object_qualifier (which is
        ///       qualifier_flag::NONE on the invoke_static path) must not be able to refuse it. The
        ///       guard is the same requires-expression plus has_receiver() check as the compile-time
        ///       rule's — one rule, two worlds.
        /// @sa meta_info_t::method_matches
        /// @sa meta_info_t::constructor_matches
        /// @sa detail::callable_check_args
        template <typename Row>
        inline bool callable_matches_at(std::span<const Row> rows, std::size_t slot,
                                   std::span<const cvref_meta_type_t> args, bool exact,
                                   qualifier_t object_qualifier) noexcept {
            const Row& row = rows[slot];
            // ---- the OBJECT-CVREF block (runtime copy; the compile-time copy is in
            // detail::callable_check_args — keep the two in step) ----
            // C++'s implicit-object-parameter rule:
            //  - cv: EVERY cv bit the OBJECT carries must also be on the member (a const object can only
            //    call const members, a volatile object only volatile ones); the member may carry more
            //    (a non-const object can call a const member);
            //  - ref: a member with NO ref qualifier accepts either value category; `&` requires an
            //    lvalue object and `&&` an rvalue object. The object's reference kind is exact — there
            //    is no collapse between the two bits, and get_param_qualifier<Obj&&>() always sets one.
            //
            // The block is guarded by the presence of the column, NOT duplicated per row type: a
            // constructor_info_t has no `qualifier` member at all (a constructor cannot be
            // cv/ref-qualified), so for that column the block is skipped whole and the cvref question
            // imposes nothing — which is exactly what a None-qualified row used to make it do.
            // It is skipped for a receiver-less row (a static member function) for a different
            // reason: there is no implicit object to describe, so the call's object_qualifier must not
            // be able to refuse it. has_receiver() is that fact; the column guard alone would not
            // catch it, because a static row DOES carry a `qualifier` column (always None).
            if constexpr (requires(const Row& probe) { probe.qualifier; probe.has_receiver(); }) {
                if (row.has_receiver()) {
                    const qualifier_t member = row.qualifier;
                    const qualifier_t object = object_qualifier;
                    if ((object & qualifier_flag::CONST) && !(member & qualifier_flag::CONST)) {
                        return false;
                    }
                    if ((object & qualifier_flag::VOLATILE) && !(member & qualifier_flag::VOLATILE)) {
                        return false;
                    }
                    const qualifier_t member_ref = member & qualifier_flag::REF;
                    if (member_ref != qualifier_flag::NONE && member_ref != (object & qualifier_flag::REF)) {
                        return false;
                    }
                }
            }

            // Arity first: a different count can never match, whatever the column holds.
            const std::span<const compiling_string_t> types = row.param_types;
            if (types.size() != args.size()) {
                return false;
            }
            const std::span<const qualifier_t> aim_qualifiers = row.param_qualifiers;
            const bool has_qualifiers = aim_qualifiers.size() == args.size();
            for (std::size_t i = 0; i < args.size(); ++i) {
                // Compare BARE spellings: the stored parameter column keeps the declared cvref
                // (carried separately in param_qualifiers), while the argument's identity name never
                // has one.
                if (bare_spelling(std::string_view(types[i])) != args[i].type.name_view()) {
                    return false;
                }
                if (!has_qualifiers) {
                    continue;
                }
                const qualifier_t aim = aim_qualifiers[i];
                const qualifier_t got = args[i].qualifier;
                if (exact && (aim & qualifier_flag::REF) != (got & qualifier_flag::REF)) {
                    // The reference kind has to agree exactly: this is what tells f(const x_t&) from
                    // f(x_t&&) when both spell their parameter "x_t". A by-value parameter (no
                    // reference kind) therefore only matches in the accommodating pass.
                    return false;
                }
                if (!check_param_qualifier(aim, got)) {
                    return false;
                }
            }
            return true;
        }

        /// @brief The COMPILE-TIME matcher's rule, applied to @p row.
        /// @tparam Row The row's type: method_info_t or constructor_info_t — the columns this rule
        ///         reads are the ones both row types carry.
        /// @tparam Ts The argument types as they appear at the call site (cvref included).
        /// @param row The row to test; the caller already addressed it by slot.
        /// @param exact true for the exact pass, false for the accommodating one.
        /// @param object_qualifier The object's OWN cvref (see meta_info_t::method_matches()).
        /// @return true when the row's parameter list accepts `Ts...` AND the row's own cvref can be
        ///         called on an object with @p object_qualifier.
        /// @note THE ONE compile-time rule, and the twin of callable_matches_at(): the object-cvref
        ///       block below is a deliberate copy of the one in that function, and each copy names the
        ///       other, so a change made to one and not mirrored shows up in review (and the two-world
        ///       agreement test in M25 fails).
        /// @note It takes the ROW, not a slot, because the two columns are different element types and
        ///       each caller (meta_info_t::check_method_args / check_constructor_args) fetches its own
        ///       column's row. A slot would force this function to know which table it is reading.
        /// @note A row with NO RECEIVER (a static member function) skips the object-cvref block: the
        ///       same has_receiver() condition the runtime copy applies. Without it a static row would
        ///       be refused whenever the call carried a const or ref-qualified object qualifier — but
        ///       there IS no object to qualify, which is the whole meaning of the kind column.
        /// @note It uses arg_spelling\<Ts\>() / get_param_qualifier\<Ts\>() so no type handle (and no
        ///       registry lookup) is needed, which is what makes it usable in a constant expression.
        template <typename Row, typename... Ts>
        constexpr bool callable_check_args(const Row& row, bool exact,
                                           qualifier_t object_qualifier) noexcept {
            // ---- the OBJECT-CVREF block (compile-time copy; the runtime copy is in
            // callable_matches_at, which the method/constructor column entry points forward to) ----
            // The member's own cvref against the object, by C++'s implicit-object-parameter rule:
            // every cv bit the object carries must be on the member (the member may carry more), and
            // a member with a ref qualifier demands the matching object reference kind.
            // Same guard as the runtime copy: a constructor_info_t has no `qualifier` member, so for
            // that column the block imposes nothing (a constructor has no cvref to compare); and a
            // row with no receiver (a static member function) imposes nothing either, because there is
            // no implicit object for the call's qualifier to describe.
            if constexpr (requires(const Row& probe) { probe.qualifier; probe.has_receiver(); }) {
                if (row.has_receiver()) {
                    const qualifier_t member = row.qualifier;
                    const qualifier_t object = object_qualifier;
                    if ((object & qualifier_flag::CONST) && !(member & qualifier_flag::CONST)) {
                        return false;
                    }
                    if ((object & qualifier_flag::VOLATILE) && !(member & qualifier_flag::VOLATILE)) {
                        return false;
                    }
                    const qualifier_t member_ref = member & qualifier_flag::REF;
                    if (member_ref != qualifier_flag::NONE && member_ref != (object & qualifier_flag::REF)) {
                        return false;
                    }
                }
            }

            // Arity first, exactly like the runtime matcher.
            const std::span<const detail::compiling_string_t> types = row.param_types;
            if (types.size() != sizeof...(Ts)) {
                return false;
            }
            constexpr std::array<std::string_view, sizeof...(Ts)> spellings{ detail::arg_spelling<Ts>()... };
            constexpr std::array<qualifier_t, sizeof...(Ts)> qualifiers{ get_param_qualifier<Ts>()... };
            const std::span<const qualifier_t> aim_qualifiers = row.param_qualifiers;
            const bool has_qualifiers = aim_qualifiers.size() == sizeof...(Ts);
            for (std::size_t i = 0; i < sizeof...(Ts); ++i) {
                // BARE spellings: the stored column keeps the declared cvref, a call-site spelling is
                // decayed. The cvref question is check_param_qualifier()'s (plus the exact pass's
                // reference-kind test), exactly as in callable_matches_at().
                if (detail::bare_spelling(std::string_view(types[i])) != spellings[i]) {
                    return false;
                }
                if (!has_qualifiers) {
                    continue;
                }
                const qualifier_t aim = aim_qualifiers[i];
                const qualifier_t got = qualifiers[i];
                if (exact && (aim & qualifier_flag::REF) != (got & qualifier_flag::REF)) {
                    return false;
                }
                if (!check_param_qualifier(aim, got)) {
                    return false;
                }
            }
            return true;
        }

    }

    /// @brief What resolve_method() found in ONE table.
    /// @note Both indices are slots of THIS meta_info_t's method_infos, so a caller can hand them
    ///       straight to meta_method_t (they are the method index space, not a global coordinate).
    struct method_resolution_t {
        /// @brief The matched row; index_npos when nothing matched.
        index_t slot = index_npos;

        /// @brief An ambiguity's SECOND candidate, for the caller's message; index_npos otherwise.
        /// @note Recorded but not ranked: a flat table has no "better conversion" order, so the
        ///       second hit is reported rather than silently preferred or dropped.
        index_t other = index_npos;

        /// @brief Whether two or more rows matched at the same level.
        bool ambiguous = false;
    };

    namespace detail {
        /// @brief The declared type of parameter @p I of a constructor.
        /// @note parameters_of() / type_of() work on an inaccessible or deleted constructor: merely
        ///       describing a constructor never needs access to it.
        template <std::meta::info Ctor, std::size_t I>
        consteval std::meta::info constructor_param_type() {
            return std::meta::type_of(std::meta::parameters_of(Ctor)[I]);
        }

        /// @brief The type the thunk hands to the constructor for parameter @p I.
        ///
        /// @par Why this is a function and not a trait class
        /// The type is derived from a reflection splice ([: ... :]). A consteval function keeps the
        /// splice in a body and answers the adjusted type as std::meta::info, so a call site splices
        /// the result directly instead of materialising a class-template specialisation just to name
        /// it.
        ///
        /// @tparam Ctor The constructor reflection.
        /// @tparam I The zero-based parameter index.
        /// @return The declared type with add_rvalue_reference for an rvalue-reference parameter and
        ///         add_lvalue_reference otherwise: reference collapsing makes this the same type the
        ///         former remove_reference_t + &/&& formulation produced.
        template <std::meta::info Ctor, std::size_t I>
        consteval std::meta::info constructor_arg_type() {
            using declared = typename[: constructor_param_type<Ctor, I>() :];
            if constexpr (std::is_rvalue_reference_v<declared>) {
                return ^^std::add_rvalue_reference_t<declared>;
            }
            else {
                return ^^std::add_lvalue_reference_t<declared>;
            }
        }
    }

    /// @brief The compile-time meta-object table: ONE class's own data, nothing else.
    ///
    /// A meta_info_t holds the tables of exactly one class — its class name, its type-rewriting rules
    /// and its method / property / enum / class-info / interface rows — plus the machinery that
    /// invokes a row (invoke / check_method_args / helpcaller). It carries NO parent link: the chain lives on
    /// meta_object_t, which owns a meta_info_t by value and keeps the `_super` pointer.
    ///
    /// @note The lookup tables hold OWN keys only. Inheritance is meta_object_t's walk
    ///       (super_class()), not a column of this struct.
    struct meta_info_t {
        /// @brief The class name (the identifier of ^^T), e.g. "widget_t".
        detail::compiling_string_t class_name;

        /// @brief The namespace-qualified name, e.g. "my::ui::widget_t".
        /// @note What the class registry keys on and what a constructor signature starts with: the
        ///       bare identifier is not unique across namespaces. A type in the global namespace
        ///       answers its identifier, so existing text keeps working.
        detail::compiling_string_t qualified_name;

        /// @brief rewriting type name to index mapping
        detail::hashtable_t rewriting_table;
        /// @brief rewriting types
        detail::compiling_span_t<const detail::type_rewriting_t> type_rewrites;

        // methods and signals
        /// @brief method name to slot lookup
        detail::hashtable_t method_hashtable;
        /// @brief the METHOD rows, indexed by slot
        /// @note Methods and signals only. A constructor is NOT a method — it is its own entity with
        ///       its own handle, its own table and its own index space (Qt: methodCount() counts
        ///       methods only, constructorCount() / constructor(index) cover constructors) — so it
        ///       lives in constructor_infos below, never here. Its signature keeps the bare class-name form
        ///       "class_t(int,int)".
        /// @note A row is a detail::method_info_t — a different TYPE from a constructor row, not a
        ///       row of a shared type with a discriminator: a constructor has no return type, no
        ///       cvref and no method_kind_enum, so it cannot be described by this struct at all.
        detail::compiling_span_t<const detail::method_info_t> method_infos;

        // constructors
        /// @brief constructor signature to slot lookup
        /// @note A key namespace of its own: a constructor key ("class_t(int,int)") is never looked
        ///       up in method_hashtable, and its slot is a CONSTRUCTOR index, not a method index.
        detail::hashtable_t constructor_hashtable;
        /// @brief the CONSTRUCTOR rows, indexed by slot
        /// @note Constructors only, in declaration order. This table IS the constructor index space
        ///       (meta_object_t::constructor(index) / index_of_constructor()). It is never inherited:
        ///       a meta-object answers its own class's constructors and no others.
        /// @note A row is a detail::constructor_info_t: the columns that cannot apply to a
        ///       constructor (return type, own cvref, method kind) do not exist on it.
        detail::compiling_span_t<const detail::constructor_info_t> constructor_infos;

        // properties
        /// @brief property name to index mapping
        detail::hashtable_t properties_hashtable;
        /// @brief property informations
        detail::compiling_span_t<const detail::property_info_t> property_infos;

        // enumerations
        /// @brief enumeration name to index mapping
        detail::hashtable_t enums_hashtable;
        /// @brief enumeration informations, indexed by slot
        detail::compiling_span_t<const detail::enum_info_t> enum_infos;

        // class info
        /// @brief class-info key to index mapping
        detail::hashtable_t class_infos_hashtable;
        /// @brief class-info entries, indexed by slot
        detail::compiling_span_t<const detail::class_info_t> class_info_infos;

        // interfaces
        /// @brief IID to index mapping (a namespace of its own: IIDs, never class names — the
        ///        class-name namespace is the class chain itself, see meta_object_t::inherits).
        detail::hashtable_t interfaces_hashtable;
        /// @brief interface entries declared by THIS class, indexed by slot
        /// @note Own entries only, like every other table: the chain is walked by meta_object_t.
        detail::compiling_span_t<const detail::interface_entry_t> interface_infos;

        /// @brief Call the function at @p slot with @p params, exactly as the thunk protocol expects.
        /// @param slot The method_infos slot. THE CALLER GUARANTEES it is in range — an out-of-range slot
        ///        is undefined behaviour; this primitive does not check it.
        /// @param obj The target object's address (an object_t, or the address of a gadget value), or
        ///        nullptr for a quiet no-op.
        /// @param params The addresses of the arguments; for a value-returning entry params[0] is the
        ///        address of the return slot.
        /// @note Does nothing else: no lookup, no signature check, no return-type check, no logging, no
        ///       allocation. Every guarantee is the caller's — this is the floor the whole call layer
        ///       stands on.
        constexpr void invoke(std::size_t slot, void* obj, void** params) const {
            method_infos.to_span()[slot].caller(obj, params);
        }

        /// @par Querying the table
        /// The tables above are public data, so a reader can walk a span directly; these accessors
        /// name that walk, so the layers above can ask the ordinary questions without reaching into
        /// the columns. Every table gets the same three-member family: count() answers its span size,
        /// at(i) the row at slot i, and find(key) the slot the table holds that key at — index_npos
        /// when it holds no such key. Constructors are their own family, never part of the methods.

        /// @brief The class name: the class_name column.
        /// @return The bare identifier the table was built for, e.g. "widget_t".
        /// @note constexpr and noexcept, and it allocates nothing: the column is already a view into
        ///       static storage.
        [[nodiscard]] constexpr std::string_view name() const noexcept {
            return std::string_view(class_name);
        }

        /// @brief The namespace-qualified name: the qualified_name column.
        /// @return The name a constructor signature starts with, e.g. "my::ui::widget_t". A class in
        ///         the global namespace answers exactly name(), the same as the column.
        [[nodiscard]] constexpr std::string_view qualified_name_of() const noexcept {
            return std::string_view(qualified_name);
        }

        /// @brief The number of method / signal rows in this table.
        /// @return method_infos' span size. Constructors are not counted: they have their own table.
        [[nodiscard]] constexpr std::size_t method_count() const noexcept {
            return method_infos.to_span().size();
        }

        /// @brief The method / signal row at @p slot.
        /// @return A reference to the row; valid while this table lives.
        /// @note The caller guarantees @p slot is in range: this is a trusted primitive, like
        ///       invoke(), and performs no bounds check.
        [[nodiscard]] constexpr const detail::method_info_t& method_at(std::size_t slot) const noexcept {
            return method_infos.to_span()[slot];
        }

        /// @brief Find a method or signal by its normalised signature in THIS table.
        /// @param signature The normalised signature, e.g. "f(int)" plus the cvref suffix.
        /// @return The row's slot; index_npos when this table holds no such key.
        /// @note The guard is invoke()'s, copied: a hash hit alone is not a match, because the chd
        ///       table can answer a wrong slot for a key it does not hold, so the stored signature
        ///       column is compared before the slot is answered.
        [[nodiscard]] constexpr std::size_t find_method(std::string_view signature) const noexcept {
            const std::uint64_t slot = method_hashtable.find(signature);
            if (slot == static_cast<std::uint64_t>(-1)
                || signature != method_infos.to_span()[static_cast<std::size_t>(slot)].signature) {
                return index_npos;
            }
            return static_cast<std::size_t>(slot);
        }

        /// @brief The number of constructor rows in this table.
        /// @return constructor_infos' span size.
        [[nodiscard]] constexpr std::size_t constructor_count() const noexcept {
            return constructor_infos.to_span().size();
        }

        /// @brief The constructor row at @p i.
        /// @return A reference to the row; valid while this table lives.
        /// @note The caller guarantees @p i is in range: this is a trusted primitive, like
        ///       invoke(), and performs no bounds check.
        [[nodiscard]] constexpr const detail::constructor_info_t& constructor_at(std::size_t i) const noexcept {
            return constructor_infos.to_span()[i];
        }

        /// @brief Find a constructor by its signature in THIS table.
        /// @param signature The constructor signature, e.g. "class_t(int,int)".
        /// @return The row's slot; index_npos when this table holds no such key.
        /// @note A key namespace of its own: a constructor key is never looked up in method_hashtable.
        /// @note The same stored-key guard as find_method(): a hash hit alone is not a match.
        [[nodiscard]] constexpr std::size_t find_constructor(std::string_view signature) const noexcept {
            const std::uint64_t slot = constructor_hashtable.find(signature);
            if (slot == static_cast<std::uint64_t>(-1)
                || signature != constructor_infos.to_span()[static_cast<std::size_t>(slot)].signature) {
                return index_npos;
            }
            return static_cast<std::size_t>(slot);
        }

        /// @brief The number of property rows in this table.
        /// @return property_infos' span size.
        [[nodiscard]] constexpr std::size_t property_count() const noexcept {
            return property_infos.to_span().size();
        }

        /// @brief The property row at @p i.
        /// @return A reference to the row; valid while this table lives.
        /// @note The caller guarantees @p i is in range: this is a trusted primitive, like
        ///       invoke(), and performs no bounds check.
        [[nodiscard]] constexpr const detail::property_info_t& property_at(std::size_t i) const noexcept {
            return property_infos.to_span()[i];
        }

        /// @brief Find a property by its name in THIS table.
        /// @param prop_name The bare property name, e.g. "value".
        /// @return The row's slot; index_npos when this table holds no such key.
        /// @note The same stored-key guard as find_method(): a hash hit alone is not a match.
        [[nodiscard]] constexpr std::size_t find_property(std::string_view prop_name) const noexcept {
            const std::uint64_t slot = properties_hashtable.find(prop_name);
            if (slot == static_cast<std::uint64_t>(-1)
                || prop_name != property_infos.to_span()[static_cast<std::size_t>(slot)].name) {
                return index_npos;
            }
            return static_cast<std::size_t>(slot);
        }

        /// @brief The number of enum rows in this table.
        /// @return enum_infos' span size.
        [[nodiscard]] constexpr std::size_t enum_count() const noexcept {
            return enum_infos.to_span().size();
        }

        /// @brief The enum row at @p i.
        /// @return A reference to the row; valid while this table lives.
        /// @note The caller guarantees @p i is in range: this is a trusted primitive, like
        ///       invoke(), and performs no bounds check.
        [[nodiscard]] constexpr const detail::enum_info_t& enum_at(std::size_t i) const noexcept {
            return enum_infos.to_span()[i];
        }

        /// @brief Find an enum by its name in THIS table.
        /// @param enum_name The bare enum identifier, e.g. "Mode".
        /// @return The row's slot; index_npos when this table holds no such key.
        /// @note The same stored-key guard as find_method(): a hash hit alone is not a match.
        [[nodiscard]] constexpr std::size_t find_enum(std::string_view enum_name) const noexcept {
            const std::uint64_t slot = enums_hashtable.find(enum_name);
            if (slot == static_cast<std::uint64_t>(-1)
                || enum_name != enum_infos.to_span()[static_cast<std::size_t>(slot)].name) {
                return index_npos;
            }
            return static_cast<std::size_t>(slot);
        }

        /// @brief The number of class-info rows in this table.
        /// @return class_info_infos' span size.
        [[nodiscard]] constexpr std::size_t class_info_count() const noexcept {
            return class_info_infos.to_span().size();
        }

        /// @brief The class-info row at @p i.
        /// @return A reference to the row; valid while this table lives.
        /// @note The caller guarantees @p i is in range: this is a trusted primitive, like
        ///       invoke(), and performs no bounds check.
        [[nodiscard]] constexpr const detail::class_info_t& class_info_at(std::size_t i) const noexcept {
            return class_info_infos.to_span()[i];
        }

        /// @brief Find a class-info entry by its key in THIS table.
        /// @param class_name The class-info key, e.g. "author".
        /// @return The row's slot; index_npos when this table holds no such key.
        /// @note The same stored-key guard as find_method(): a hash hit alone is not a match.
        [[nodiscard]] constexpr std::size_t find_class_info(std::string_view class_name) const noexcept {
            const std::uint64_t slot = class_infos_hashtable.find(class_name);
            if (slot == static_cast<std::uint64_t>(-1)
                || class_name != class_info_infos.to_span()[static_cast<std::size_t>(slot)].name) {
                return index_npos;
            }
            return static_cast<std::size_t>(slot);
        }

        /// @brief The number of interface rows in this table.
        /// @return interface_infos' span size. Own entries only: the chain is meta_object_t's walk.
        [[nodiscard]] constexpr std::size_t interface_count() const noexcept {
            return interface_infos.to_span().size();
        }

        /// @brief The interface row at @p i.
        /// @return A reference to the row; valid while this table lives.
        /// @note The caller guarantees @p i is in range: this is a trusted primitive, like
        ///       invoke(), and performs no bounds check.
        [[nodiscard]] constexpr const detail::interface_entry_t& interface_at(std::size_t i) const noexcept {
            return interface_infos.to_span()[i];
        }

        /// @brief Find an interface by its IID in THIS table.
        /// @param iid The interface ID, e.g. "com.example.IFoo/1.0".
        /// @return The row's slot; index_npos when this table holds no such key.
        /// @note The same stored-key guard as find_method(): a hash hit alone is not a match.
        [[nodiscard]] constexpr std::size_t find_interface(std::string_view iid) const noexcept {
            const std::uint64_t slot = interfaces_hashtable.find(iid);
            if (slot == static_cast<std::uint64_t>(-1)
                || iid != interface_infos.to_span()[static_cast<std::size_t>(slot)].iid) {
                return index_npos;
            }
            return static_cast<std::size_t>(slot);
        }

        /// @name The matchers: "does this row accept these arguments and this object"
        /// @{
        //
        // TWO implementations, deliberately: the runtime world compares (type handle, cvref)
        // identities, the compile-time world compares TYPES. They cannot share code because the
        // compile-time side cannot name a cvref_meta_type_t (its from<T>() needs the type registry)
        // while the runtime side cannot name a type at all. The ARGUMENT rule is identical in both;
        // the OBJECT-CVREF block is written out in both — in detail::callable_matches_at() (the runtime
        // rule) and in detail::callable_check_args() (the compile-time rule) — and each copy names
        // the other, so a change made to one and not mirrored in the other shows up in review (and the
        // two-world agreement test in M25 fails).
        //
        // Each world has exactly ONE rule, addressed per COLUMN by a thin member: method_matches /
        // check_method_args read method_infos, constructor_matches / check_constructor_args read
        // constructor_infos. The two columns hold DIFFERENT row types, which is why the rules are
        // templates over the row type instead of being duplicated for constructors.
        //
        // Neither matcher names any_t. The boxed (erased) channel's bridge lives on the HANDLE —
        // meta_method_t::method_matches_boxed() — which re-expresses each value as an identity and calls
        // method_matches(); this table knows (type, cvref) identities and TYPES, never a value type.

        /// @brief Whether the METHOD row at @p slot accepts @p args AND can be called on an object
        ///        with @p object_qualifier.
        /// @param slot A slot of THIS table's method_infos; the caller guarantees it is in range.
        /// @param args The call's arguments, as (bare type, cvref) identities.
        /// @param exact true for the exact pass, false for the accommodating one.
        /// @param object_qualifier The object's OWN cvref, as an implicit object parameter would carry it
        ///        (get_param_qualifier\<Obj&&\>(): const / volatile bits plus the reference kind). It
        ///        must include the Volatile bit: a volatile object can only call volatile members.
        /// @return true when the row's parameter list matches @p args AND the row's own cvref can be
        ///         called on that object.
        /// @note NOT constexpr, deliberately — and it is not an oversight. This is the RUNTIME
        ///       matcher: its slot is a run-time index and its arguments are run-time identities
        ///       (type handles + cvref), so a constexpr marker would claim a constant-expression
        ///       ability the inputs cannot have. The compile-time world has its own matcher,
        ///       check_method_args\<Ts...\>(), which takes TYPES and stays constexpr.
        /// @note THE METHOD COLUMN'S ENTRY POINT to the one runtime rule, which lives in
        ///       detail::callable_matches_at(): it is shared with the CONSTRUCTOR column
        ///       (constructor_infos), whose rows have no meta_method_t handle. Keeping it there means
        ///       one copy of the object-cvref / arity / bare-spelling / reference-kind rule, addressed
        ///       by (column, slot) in both cases — the caller never handles a table row.
        /// @note noexcept and ALLOCATION-FREE, like the rule it forwards to.
        /// @sa detail::callable_matches_at
        /// @sa constructor_matches
        /// @sa resolve_method
        /// @sa check_method_args
        bool method_matches(std::size_t slot, std::span<const cvref_meta_type_t> args,
                            bool exact, qualifier_t object_qualifier) const noexcept {
            return detail::callable_matches_at(method_infos.to_span(), slot, args, exact, object_qualifier);
        }

        /// @brief Whether the CONSTRUCTOR row at @p slot accepts @p args.
        /// @param slot A slot of THIS table's constructor_infos; the caller guarantees it is in range.
        /// @param args The call's arguments, as (bare type, cvref) identities.
        /// @param exact true for the exact pass, false for the accommodating one.
        /// @param object_qualifier The object's cvref. A constructor has no object to be called on —
        ///        it BUILDS one — so the only honest value is qualifier_flag::NONE, which the
        ///        object-cvref block of the shared rule accepts unconditionally. The parameter exists
        ///        so the two columns are addressed through ONE rule signature.
        /// @return true when the row's parameter list matches @p args. The cvref block imposes nothing:
        ///         a constructor row has no cvref column (it cannot be cv-qualified).
        /// @note THE CONSTRUCTOR COLUMN'S ENTRY POINT to detail::callable_matches_at(), the SAME rule
        ///       method_matches() forwards to — a constructor row is a different TYPE, not a different
        ///       rule. NOT constexpr for the same reason as method_matches(): a run-time slot and
        ///       run-time identities.
        /// @sa detail::callable_matches_at
        /// @sa method_matches
        /// @sa check_constructor_args
        bool constructor_matches(std::size_t slot, std::span<const cvref_meta_type_t> args,
                                 bool exact, qualifier_t object_qualifier) const noexcept {
            return detail::callable_matches_at(constructor_infos.to_span(), slot, args, exact,
                object_qualifier);
        }

        /// @brief Whether the METHOD row at @p slot accepts `Ts...` AND can be called on an object
        ///        with @p object_qualifier.
        /// @tparam Ts The argument types as they appear at the call site (cvref included).
        /// @param slot A slot of THIS table's method_infos; the caller guarantees it is in range.
        /// @param exact true for the exact pass, false for the accommodating one. There is NO DEFAULT
        ///        on purpose — see the note below.
        /// @param object_qualifier The object's OWN cvref (see method_matches()).
        /// @note THE COMPILE-TIME MATCHER — the twin of method_matches(). The rule itself lives in
        ///       detail::callable_check_args() (one copy for both columns); this member only fetches
        ///       the METHOD column's row and hands it over.
        /// @note Why @p exact has NO DEFAULT: the pass is a DECISION every caller must state. The
        ///       exact pass is a SELECTION rule — resolving a NAME against a set of overloads, where
        ///       f(const T&) and f(T&&) must stay apart. Validating an ALREADY-SELECTED row is the
        ///       accommodating pass. A default here once hid that distinction and refused legal calls
        ///       (a std::string prvalue against over(const std::string&), an int lvalue against a
        ///       by-value parameter); the two questions are different, so their callers say which
        ///       one they are asking.
        /// @sa method_matches
        /// @sa resolve_method_types
        /// @sa detail::callable_check_args
        template <typename... Ts>
        constexpr bool check_method_args(std::size_t slot, bool exact,
                                         qualifier_t object_qualifier) const noexcept {
            return detail::callable_check_args<detail::method_info_t, Ts...>(
                method_infos.to_span()[slot], exact, object_qualifier);
        }

        /// @brief Whether the CONSTRUCTOR row at @p slot accepts `Ts...`.
        /// @tparam Ts The argument types as they appear at the call site (cvref included).
        /// @param slot A slot of THIS table's constructor_infos; the caller guarantees it is in range.
        /// @param exact true for the exact pass, false for the accommodating one. Same no-default rule
        ///        as check_method_args(): the pass is the caller's decision.
        /// @param object_qualifier The object's cvref; a constructor has none, so this is
        ///        qualifier_flag::NONE in practice (and imposes nothing on the rule).
        /// @return true when the row's parameter list accepts `Ts...`. The cvref block imposes
        ///         nothing: a constructor row has no cvref column.
        /// @note It calls the SAME detail::callable_check_args() check_method_args() calls: the rule
        ///       exists once per world, and the two columns differ only in the row type they hand it.
        /// @sa check_method_args
        /// @sa constructor_matches
        template <typename... Ts>
        constexpr bool check_constructor_args(std::size_t slot, bool exact,
                                               qualifier_t object_qualifier) const noexcept {
            return detail::callable_check_args<detail::constructor_info_t, Ts...>(
                constructor_infos.to_span()[slot], exact, object_qualifier);
        }

        /// @}

        /// @brief Resolve @p name against THIS table by argument types and cvref.
        /// @param name The BARE member name to resolve ("f"); it is not a signature key.
        /// @param args The call's arguments, as (bare type, cvref) identities, in call order.
        /// @param object_qualifier The object's own cvref (const / volatile bits and reference kind),
        ///        exactly what method_matches() compares the member's qualifier against.
        /// @return The slot of the row the call means; index_npos when nothing matches. When two or
        ///         more rows match at the SAME level, ambiguous is set and slot / other hold the first
        ///         and the second hit (declaration order), because a flat table has no ranking rule.
        /// @note SILENT: this layer never logs. A miss and an ambiguity are answers, not diagnostics —
        ///       the chain layer (meta_object_t, looking up through super_class()) decides what each
        ///       one means and what to say about it.
        /// @note Own table only: a meta_info_t has no parent link, by design, so the chain walk is
        ///       never started here.
        /// @note The table is keyed by SIGNATURE, so a name lookup is a LINEAR scan of method_infos:
        ///       there is no name index to consult, and adding one would cost every table a column it
        ///       may never need.
        /// @note Constructors are not in this table (they have their own, constructor_infos), so they can
        ///       never match — resolving "class_t" answers index_npos even though the class declares
        ///       constructors.
        /// @note The two passes are tried in order: EXACT (the reference kind must agree) first, then
        ///       ACCOMMODATING (check_param_qualifier decides). A hit in the exact pass is returned
        ///       without ever looking at the accommodating one, which is what keeps f(const T&) and
        ///       f(T&&) apart for an rvalue argument.
        /// @note NOT constexpr, deliberately — the same reason method_matches() is not: every input is a
        ///       run-time identity (a name, an argument span, a qualifier), so a constexpr marker
        ///       would be a lie. Constant-expression resolution is resolve_method_types\<Ts...\>(),
        ///       which takes TYPES; that second form exists precisely so this one need not pretend.
        /// @note noexcept, and it allocates nothing (method_matches does the comparing).
        /// @note It resolves in the METHOD column only; a constructor is not a method and has no
        ///       name-based resolution here (its table is keyed by full signature, see find_constructor).
        /// @sa method_matches
        /// @sa resolve_method_types
        method_resolution_t resolve_method(std::string_view name,
                                     std::span<const cvref_meta_type_t> args,
                                     qualifier_t object_qualifier) const noexcept {
            const std::span<const detail::method_info_t> rows = method_infos.to_span();
            for (int pass = 0; pass < 2; ++pass) {
                const bool exact = (pass == 0);
                method_resolution_t found;
                std::size_t hits = 0;
                for (std::size_t i = 0; i < rows.size(); ++i) {
                    if (std::string_view(rows[i].name) != name) {
                        continue;
                    }
                    if (!method_matches(i, args, exact, object_qualifier)) {
                        continue;
                    }
                    if (hits == 0) {
                        found.slot = i;
                    }
                    else if (hits == 1) {
                        found.other = i;
                    }
                    ++hits;
                }
                if (hits != 0) {
                    found.ambiguous = hits > 1;
                    return found;
                }
            }
            return method_resolution_t{};
        }

        /// @brief Resolve @p name against THIS table by TYPES and cvref (the compile-time twin of
        ///        resolve_method()).
        /// @tparam Ts The argument types, written with their cvref.
        /// @param name The BARE member name to resolve ("f"); it is not a signature key.
        /// @param object_qualifier The object's OWN cvref (get_param_qualifier\<Obj&&\>()).
        /// @return The same shape resolve_method() answers: a unique slot, index_npos for a miss, or
        ///         ambiguous with the first and second hits (declaration order) at the same level.
        /// @note constexpr, not consteval: its caller (meta_object_t's compile-time chain walk)
        ///       rebuilds each link with build_meta\<T\>() and must resolve inside a constant
        ///       expression, and nothing in the body prevents that — it reads the spans and calls
        ///       check_method_args. Keeping it constexpr also lets a caller resolve at run time on
        ///       a table it already holds, which a consteval function would forbid. The argument
        ///       identities cvref_meta_type_t::from\<T\>() builds are NOT constexpr (they register
        ///       a type), which is exactly why this second form exists instead of reusing the
        ///       runtime resolver.
        /// @note Same two-pass loop as resolve_method() — EXACT first, then ACCOMMODATING, and two hits
        ///       in one pass are ambiguous. Only the per-row test differs: check_method_args, the
        ///       compile-time matcher.
        /// @note SILENT, like resolve_method(): a miss and an ambiguity are answers; the chain layer
        ///       turns them into diagnostics.
        /// @note The name says "types", not "static": this form selects the COMPILE-TIME (type-pack)
        ///       world, it does NOT select static members. A static member has no receiver, which is
        ///       method_kind_enum::STATIC_METHOD / has_receiver()'s question, not this resolver's.
        /// @sa resolve_method
        /// @sa check_method_args
        template <typename... Ts>
        constexpr method_resolution_t resolve_method_types(std::string_view name,
                                                       qualifier_t object_qualifier) const {
            const std::span<const detail::method_info_t> rows = method_infos.to_span();
            for (int pass = 0; pass < 2; ++pass) {
                const bool exact = (pass == 0);
                method_resolution_t found;
                std::size_t hits = 0;
                for (std::size_t i = 0; i < rows.size(); ++i) {
                    if (std::string_view(rows[i].name) != name) {
                        continue;
                    }
                    if (!check_method_args<Ts...>(i, exact, object_qualifier)) {
                        continue;
                    }
                    if (hits == 0) {
                        found.slot = i;
                    }
                    else if (hits == 1) {
                        found.other = i;
                    }
                    ++hits;
                }
                if (hits != 0) {
                    found.ambiguous = hits > 1;
                    return found;
                }
            }
            return method_resolution_t{ };
        }

        /// @brief The canonical spelling of @p T, in static storage: the USER-FACING spelling.
        /// @tparam T The type to name (reference, array and cv-qualified types are accepted as-is; no
        ///         decay is applied).
        /// @return A view of the spelling produced by the algorithm (canonical_name.hpp), copied into
        ///         static storage by `std::define_static_string`.
        /// @note The spelling is the owner's canonical form, not a vendor rendering: primitive short
        ///       forms, only the template arguments that do not follow from the template's own defaults
        ///       (rule 2), and no implementation-detail namespace scope (rule 3).
        /// @note This is NOT the identity spelling and NOT a table key: tables are keyed by
        ///       original_name<T>(), the same walk with those namespaces kept. The registry binds this
        ///       spelling as an ALIAS (meta_type_t::register_type<T>() / from_type<T>()), so by-name
        ///       lookup accepts either spelling.
        /// @note Throws `std::meta::exception` when a type in the walk is declared in an anonymous
        ///       namespace (rule 4: it has no stable global name, so it cannot be a table key). Also
        ///       ill-formed if the walk reaches an entity without an identifier (GCC: `reflection with
        ///       has_identifier false`) or an argument with no spelling rule; use has_canonical_name()
        ///       to test first.
        template <typename T>
        static consteval std::string_view canonical_name() {
            return std::define_static_string(detail::canonical_name_of(^^T));
        }

        /// @brief The type's ORIGINAL spelling: canonical_name<T>() with implementation-detail
        ///        namespaces kept — the spelling the compiler and the ABI actually use.
        /// @note The two spellings come from ONE walk with a flag, so they can never disagree; they are
        ///       equal exactly when the type has no reserved scope.
        /// @note This is the IDENTITY spelling: `type_interface_t::name` and every table key use it, and
        ///       it is the primary registry name. canonical_name<T>() stays the user-facing spelling and
        ///       is bound as an alias by meta_type_t::register_type<T>() / from_type<T>().
        template <typename T>
        static consteval std::string_view original_name() {
            return std::define_static_string(detail::original_name_of(^^T));
        }

        /// @brief Whether canonical_name<T>() is well-formed.
        /// @tparam T The type to test.
        /// @return true when the algorithm can produce a spelling for @p T.
        template <typename T>
        static consteval bool has_canonical_name() {
            return detail::canonical_name_exists(^^T);
        }

        /// @brief The ORIGINAL spelling of @c std::decay_t\<T\>, in static storage (the registry name).
        /// @tparam T The type to name; cv, references and arrays are removed (std::decay_t), exactly as
        ///         the registry's keys do.
        /// @return A view of the spelling produced by canonical_name.hpp, copied into static storage by
        ///         `std::define_static_string` so it can be used as a table key.
        /// @note Same identity spelling as original_name<T>() and the reflected tables' type columns,
        ///       which is what lets a name resolve across the compile-time and run-time layers. The
        ///       canonical spelling is the user-facing one and is only a registry alias.
        /// @note consteval: the name is a table key, never a run-time rendering.
        template <typename T>
        static consteval std::string_view registered_type_name() {
            return std::define_static_string(detail::normalized_type_name<std::decay_t<T>>());
        }

        // normalizing part

        constexpr std::string normalized_type(std::string_view str) const {
            if (type_rewrites.to_span().empty()) return std::string{ str };
            std::vector<std::pair<std::string_view, std::string_view>> keys;
            keys.reserve(type_rewrites.to_span().size());
            for (const auto& [k, v, _] : type_rewrites.to_span()) {
                keys.emplace_back(k, v);
            }
            std::ranges::sort(keys, [](auto& a, auto& b) noexcept {
                return a.first.size() > b.first.size();
            });
            std::string out;
            out.reserve(str.size());
            std::size_t i = 0;
            while (i < str.size()) {
                bool matched = false;
                for (const auto& [k, v] : keys) {
                    if (k.empty() || str.compare(i, k.size(), k) != 0) continue;
                    if (detail::is_ident_continue(k.front()) && i > 0
                        && detail::is_ident_continue(str[i - 1])) continue;
                    const std::size_t j = i + k.size();
                    if (detail::is_ident_continue(k.back()) && j < str.size()
                        && detail::is_ident_continue(str[j])) continue;
                    out.append(v);
                    i = j;
                    matched = true;
                    break;
                }
                if (!matched) {
                    out += str[i];
                    ++i;
                }
            }
            return out;
        }

        /// @brief Normalise a raw signature to the spelling a USER-SUPPLIED key takes.
        /// @param str The raw signature, e.g. "name(type,type)"; a leading return type is ignored.
        /// @return "name(param,param)" plus the canonical cvref suffix, the spelling
        ///         method("...") / signal("...") accepts.
        /// @note RUNTIME layer: the input is text a CALLER wrote, so the normalisation has to undo
        ///       the spellings C++ allows — detail::normalize_type (whitespace, GCC's std::__cxx11
        ///       namespace), this table's type-rewriting rules and detail::original_parameter_alias,
        ///       which asks the TYPE REGISTRY for the base (meta_type_t::resolve_global, so the alias,
        ///       the canonical spelling and the identity spelling all resolve) and returns the
        ///       IDENTITY spelling the tables store. The COMPILE-TIME layer runs the reflection-based
        ///       normalized_function instead, which needs none of that.
        /// @note The parameter piece keeps the caller's cv/ref spelling; original_parameter_alias
        ///       peels those affixes only to find the base and re-attaches them byte for byte, so
        ///       "f(const std::string&)" becomes "f(const std::__cxx11::basic_string<char>&)".
        /// @note ONE part of the spelling is normalised away first: the parameter's TOP-LEVEL cv.
        ///       C++ drops it from a function parameter's type ([dcl.fct]), so the compile-time
        ///       spelling (normalized_function over the reflected parameter) never carries it, and the
        ///       run-time text must agree or the two spellings are different keys for one member:
        ///       "f(const int)" and "f(int)" are the same row, as are "f(int* const)" and "f(int*)",
        ///       while a REFERENT's cv ("f(const int&)") and a POINTEE's cv ("f(const int*)") are part
        ///       of the type and are kept. detail::strip_parameter_top_level_cv() owns that rule and
        ///       runs BEFORE the alias lookup, so "const std::string" is stripped to "std::string" and
        ///       only then resolved to the identity spelling.
        /// @note The trailing cvref suffix is parsed from the text after the parameter list and
        ///       canonicalised to C++ order, whatever spelling the caller used: "f(int)const",
        ///       "f(int)const&" and "f( int ) const &" all become "f(int) const &". A tail that is
        ///       not cvref text (noexcept, junk) is left out of the key.
        constexpr std::string normalized_signature(std::string_view str) const {
            // Parsing is shared with the run-time builder: split_signature() extracts the name and the
            // depth-aware parameter pieces; only the rewriting below is specific to this table.
            const detail::signature_parts_t parts = detail::split_signature(str);

            std::string out;
            out.reserve(str.size());
            out.append(parts.name);
            out += '(';

            bool first = true;
            for (const std::string_view raw : parts.parameters) {
                std::string_view arg = raw;
                while (!arg.empty() && detail::is_space(arg.front()))
                    arg.remove_prefix(1);
                while (!arg.empty() && detail::is_space(arg.back()))
                    arg.remove_suffix(1);
                if (!first) out += ',';
                // The RUN-TIME user-text pass: normalize_type normalises whitespace and the GCC
                // namespace spelling, the table's rewriting rules apply, the TOP-LEVEL cv of the
                // parameter is dropped (C++ drops it from the parameter's type too — see the @note
                // above and detail::strip_parameter_top_level_cv), and original_parameter_alias asks
                // the type registry for the base and puts it on the IDENTITY spelling the tables
                // store. Stripping before the alias lookup is what lets "const std::string" resolve
                // as "std::string" instead of falling through as an unknown spelling.
                const std::string normalized_piece =
                    normalized_type(std::ranges::to<std::string>(detail::normalize_type(arg)));
                out += detail::original_parameter_alias(
                    detail::strip_parameter_top_level_cv(normalized_piece));
                first = false;
            }

            out += ')';

            // The cvref suffix follows the parameter list, canonicalised to C++ order (const,
            // volatile, then & / &&); only the FLAGS matter, so the input order never leaks in.
            std::string_view suffix;
            const std::size_t open = str.find('(');
            if (open != std::string_view::npos) {
                const std::size_t close = str.rfind(')');
                if (close != std::string_view::npos && close > open) {
                    suffix = str.substr(close + 1);
                }
            }
            bool suffix_const = false;
            bool suffix_volatile = false;
            int suffix_ref = 0; // 0 none, 1 &, 2 &&
            for (std::size_t i = 0; i < suffix.size();) {
                while (i < suffix.size() && detail::is_space(suffix[i])) {
                    ++i;
                }
                if (i >= suffix.size()) {
                    break;
                }
                if (suffix[i] == '&') {
                    suffix_ref = (i + 1 < suffix.size() && suffix[i + 1] == '&') ? 2 : 1;
                    i += static_cast<std::size_t>(suffix_ref);
                    continue;
                }
                const auto word_ends_at = [&](std::size_t length) {
                    return i + length == suffix.size()
                        || !detail::is_ident_continue(suffix[i + length]);
                };
                if (suffix.substr(i).starts_with("const") && word_ends_at(5)) {
                    suffix_const = true;
                    i += 5;
                    continue;
                }
                if (suffix.substr(i).starts_with("volatile") && word_ends_at(8)) {
                    suffix_volatile = true;
                    i += 8;
                    continue;
                }
                // Not cvref text (noexcept, an annotation, junk): not part of the key.
                break;
            }
            if (suffix_const) out += " const";
            if (suffix_volatile) out += " volatile";
            if (suffix_ref == 1) out += " &";
            else if (suffix_ref == 2) out += " &&";
            return out;
        }

        /// @brief Build the normalised signature from a function reflection (used by the SIGNAL macro).
        /// @param function The function reflection.
        /// @return The normalised signature in static storage.
        /// @note It uses the same normalisation as build_method_infos, so it matches the method_infos keys.
        /// @note The SIGNAL macro expands inside the class definition, where ^^T is still incomplete
        ///       and no type-rewriting table can be materialised; it does not need one — see
        ///       normalized_function (COMPILE-TIME layer: reflection + type normalisation only).
        /// @sa normalized_function
        static consteval std::string_view signature_of(std::meta::info function) {
            return std::define_static_string(normalized_function(function));
        }

        template <class T>
        static consteval meta_info_t build_meta() {
            // detail::check_no_vararg<T>();

            // FIRST, before anything is collected: a class that declares a NON-HIDDEN member with an
            // explicit object parameter is refused by name. The collection below skips such members
            // (is_aimed_function), so without this check they would vanish silently; the rejection is
            // what turns "not supported" into a diagnostic. A [[= make_hidden()]] member is the
            // author's opt-out and is skipped by the rejection too — see the helper.
            detail::reject_explicit_object_members<T>();

            meta_info_t mo;

            std::vector<detail::type_rewriting_t> type_rewrites = build_type_rewrites<T>();
            std::vector<std::string_view> rewrite_keys;
            for (const auto& type_rewrite : type_rewrites) {
                rewrite_keys.push_back((std::string_view)type_rewrite.from);
            }
            const auto rewrite_ht = detail::chd_hash(rewrite_keys);
            if (!rewrite_keys.empty()
                && (rewrite_ht.displacements.empty() || rewrite_ht.redirect.empty())) {
                auto msg = std::string("chd_hash error, type is ") + std::string(std::meta::display_string_of(^^T));
                msg += ", keys=";
                msg += std::to_string(rewrite_keys.size());
                msg += ", attempts=";
                msg += std::to_string(detail::chd_hash_max_attempts);
                msg += "|";
                for (auto key : rewrite_keys) {
                    msg += key;
                    msg += "|";
                }
                throw std::meta::exception(std::define_static_string(msg), ^^T);
            }

            mo.rewriting_table = detail::hashtable_t{
                rewrite_ht.seed0, rewrite_ht.seed1,
                std::define_static_array(rewrite_ht.displacements),
                std::define_static_array(rewrite_ht.redirect)
            };
            std::vector<detail::type_rewriting_t> real_type_rewrites(type_rewrites.size());
            for (const auto& type_rewrite : type_rewrites) {
                real_type_rewrites[mo.rewriting_table.find(type_rewrite.from)] = type_rewrite;
            }
            mo.type_rewrites = std::define_static_array(real_type_rewrites);

            std::vector<detail::method_info_t> method_infos = build_method_infos<T>();
            std::vector<std::string_view> method_keys;
            for (const auto& method_info : method_infos) {
                method_keys.push_back((std::string_view)method_info.signature);
            }
            const auto method_ht = detail::chd_hash(method_keys);
            if (!method_keys.empty()
                && (method_ht.displacements.empty() || method_ht.redirect.empty())) {
                auto msg = std::string("chd_hash error, type is ") + std::string(std::meta::display_string_of(^^T));
                msg += ", keys=";
                msg += std::to_string(method_keys.size());
                msg += ", attempts=";
                msg += std::to_string(detail::chd_hash_max_attempts);
                msg += "|";
                for (auto key : method_keys) {
                    msg += key;
                    msg += "|";
                }
                throw std::meta::exception(std::define_static_string(msg), ^^T);
            }

            mo.method_hashtable = detail::hashtable_t{
                method_ht.seed0, method_ht.seed1,
                std::define_static_array(method_ht.displacements),
                std::define_static_array(method_ht.redirect)
            };
            std::vector<detail::method_info_t> real_method_infos(method_infos.size());
            for (const auto& method_info : method_infos) {
                real_method_infos[mo.method_hashtable.find(method_info.signature)] = method_info;
            }
            mo.method_infos = std::define_static_array(real_method_infos);

            // constructor table: its own keys, its own perfect hash and its own array. Nothing here
            // reaches method_infos: a constructor is not a method (Qt's constructorCount()).
            std::vector<detail::constructor_info_t> constructor_infos = build_constructor_infos<T>();
            std::vector<std::string_view> constructor_keys;
            for (const auto& constructor_info : constructor_infos) {
                constructor_keys.push_back((std::string_view)constructor_info.signature);
            }
            const auto constructor_ht = detail::chd_hash(constructor_keys);
            if (!constructor_keys.empty()
                && (constructor_ht.displacements.empty() || constructor_ht.redirect.empty())) {
                auto msg = std::string("chd_hash error, type is ") + std::string(std::meta::display_string_of(^^T));
                msg += ", keys=";
                msg += std::to_string(constructor_keys.size());
                msg += ", attempts=";
                msg += std::to_string(detail::chd_hash_max_attempts);
                msg += "|";
                for (auto key : constructor_keys) {
                    msg += key;
                    msg += "|";
                }
                throw std::meta::exception(std::define_static_string(msg), ^^T);
            }

            mo.constructor_hashtable = detail::hashtable_t{
                constructor_ht.seed0, constructor_ht.seed1,
                std::define_static_array(constructor_ht.displacements),
                std::define_static_array(constructor_ht.redirect)
            };
            std::vector<detail::constructor_info_t> real_constructor_infos(constructor_infos.size());
            for (const auto& constructor_info : constructor_infos) {
                real_constructor_infos[mo.constructor_hashtable.find(constructor_info.signature)] = constructor_info;
            }
            mo.constructor_infos = std::define_static_array(real_constructor_infos);

            // class name. The parent link is not built here: a meta_info_t has no parent column, so
            // the chain belongs entirely to meta_object_t, whose consteval constructor reflects T's
            // direct bases and links `_super` (detail::aimed_parent<T>()).
            mo.class_name = (std::string_view)std::define_static_string(std::meta::identifier_of(^^T));
            mo.qualified_name = (std::string_view)std::define_static_string(detail::qualified_name_of(^^T));

            // property table
            std::vector<detail::property_info_t> prop_infos = build_property_infos<T>(mo);
            std::vector<std::string_view> prop_keys;
            for (const auto& prop : prop_infos) {
                prop_keys.push_back((std::string_view)prop.name);
            }
            const auto prop_ht = detail::chd_hash(prop_keys);
            if (!prop_keys.empty()
                && (prop_ht.displacements.empty() || prop_ht.redirect.empty())) {
                auto msg = std::string("chd_hash error, type is ") + std::string(std::meta::display_string_of(^^T));
                msg += ", keys=";
                msg += std::to_string(prop_keys.size());
                msg += ", attempts=";
                msg += std::to_string(detail::chd_hash_max_attempts);
                msg += "|";
                for (auto key : prop_keys) {
                    msg += key;
                    msg += "|";
                }
                throw std::meta::exception(std::define_static_string(msg), ^^T);
            }
            mo.properties_hashtable = detail::hashtable_t{
                prop_ht.seed0, prop_ht.seed1,
                std::define_static_array(prop_ht.displacements),
                std::define_static_array(prop_ht.redirect)
            };
            std::vector<detail::property_info_t> real_prop_infos(prop_infos.size());
            for (const auto& prop : prop_infos) {
                real_prop_infos[mo.properties_hashtable.find((std::string_view)prop.name)] = prop;
            }
            mo.property_infos = std::define_static_array(real_prop_infos);

            // enum table
            std::vector<detail::enum_info_t> enum_infos = build_enum_infos<T>();
            std::vector<std::string_view> enum_keys;
            for (const auto& enum_info : enum_infos) {
                enum_keys.push_back((std::string_view)enum_info.name);
            }
            const auto enum_ht = detail::chd_hash(enum_keys);
            if (!enum_keys.empty()
                && (enum_ht.displacements.empty() || enum_ht.redirect.empty())) {
                auto msg = std::string("chd_hash error, type is ") + std::string(std::meta::display_string_of(^^T));
                msg += ", keys=";
                msg += std::to_string(enum_keys.size());
                msg += ", attempts=";
                msg += std::to_string(detail::chd_hash_max_attempts);
                msg += "|";
                for (auto key : enum_keys) {
                    msg += key;
                    msg += "|";
                }
                throw std::meta::exception(std::define_static_string(msg), ^^T);
            }
            mo.enums_hashtable = detail::hashtable_t{
                enum_ht.seed0, enum_ht.seed1,
                std::define_static_array(enum_ht.displacements),
                std::define_static_array(enum_ht.redirect)
            };
            std::vector<detail::enum_info_t> real_enum_infos(enum_infos.size());
            for (const auto& enum_info : enum_infos) {
                real_enum_infos[mo.enums_hashtable.find((std::string_view)enum_info.name)] = enum_info;
            }
            mo.enum_infos = std::define_static_array(real_enum_infos);

            // class-info table
            std::vector<detail::class_info_t> class_infos = build_class_infos<T>();
            std::vector<std::string_view> ci_keys;
            for (const auto& ci : class_infos) {
                ci_keys.push_back((std::string_view)ci.name);
            }
            const auto ci_ht = detail::chd_hash(ci_keys);
            if (!ci_keys.empty()
                && (ci_ht.displacements.empty() || ci_ht.redirect.empty())) {
                auto msg = std::string("chd_hash error, type is ") + std::string(std::meta::display_string_of(^^T));
                msg += ", keys=";
                msg += std::to_string(ci_keys.size());
                msg += ", attempts=";
                msg += std::to_string(detail::chd_hash_max_attempts);
                msg += "|";
                for (auto key : ci_keys) {
                    msg += key;
                    msg += "|";
                }
                throw std::meta::exception(std::define_static_string(msg), ^^T);
            }
            mo.class_infos_hashtable = detail::hashtable_t{
                ci_ht.seed0, ci_ht.seed1,
                std::define_static_array(ci_ht.displacements),
                std::define_static_array(ci_ht.redirect)
            };
            std::vector<detail::class_info_t> real_ci_infos(class_infos.size());
            for (const auto& ci : class_infos) {
                real_ci_infos[mo.class_infos_hashtable.find((std::string_view)ci.name)] = ci;
            }
            mo.class_info_infos = std::define_static_array(real_ci_infos);

            // interface table. One row per ACCESSIBLE interface base of T: an interface type carries
            // its IID in its own [[= make_interface("...")]] annotation, and a class implements it by
            // inheriting it PUBLICLY, so the base list is the one source — there is no INTERFACES
            // list to keep in sync. The scan uses access_context::unprivileged(): a private or
            // protected interface base is NOT reported, because the consumer's own
            // `static_cast<I*>(T*)` would not compile for it. A meta-object base is the parent,
            // linked separately by meta_object_t's consteval constructor, and contributes no row.
            std::vector<detail::interface_entry_t> interface_infos = build_interface_infos<T>();
            std::vector<std::string_view> iid_keys;
            for (const auto& interface_info : interface_infos) {
                iid_keys.push_back((std::string_view)interface_info.iid);
            }
            const auto interface_ht = detail::chd_hash(iid_keys);
            if (!iid_keys.empty()
                && (interface_ht.displacements.empty() || interface_ht.redirect.empty())) {
                auto msg = std::string("chd_hash error, type is ") + std::string(std::meta::display_string_of(^^T));
                msg += ", keys=";
                msg += std::to_string(iid_keys.size());
                msg += ", attempts=";
                msg += std::to_string(detail::chd_hash_max_attempts);
                msg += "|";
                for (auto key : iid_keys) {
                    msg += key;
                    msg += "|";
                }
                throw std::meta::exception(std::define_static_string(msg), ^^T);
            }

            mo.interfaces_hashtable = detail::hashtable_t{
                interface_ht.seed0, interface_ht.seed1,
                std::define_static_array(interface_ht.displacements),
                std::define_static_array(interface_ht.redirect)
            };
            std::vector<detail::interface_entry_t> real_interface_infos(interface_infos.size());
            for (const auto& interface_info : interface_infos) {
                real_interface_infos[mo.interfaces_hashtable.find((std::string_view)interface_info.iid)] = interface_info;
            }
            mo.interface_infos = std::define_static_array(real_interface_infos);
            return mo;
        }

    private:
        template <class ArgsTuple, std::size_t I>
        static decltype(auto) read_arg(void** args) {
            using ArgType = std::tuple_element_t<I, ArgsTuple>;
            using Raw = std::remove_reference_t<ArgType>;
            auto* p = static_cast<Raw*>(args[I + 1]);
            if constexpr (std::is_rvalue_reference_v<ArgType>) {
                return std::move(*p);
            }
            else {
                return *p;
            }
        }

        /// @brief Hand a thunk's result to the caller through the generic_call return slot.
        /// @tparam FP The function_params_traits<> of the invoked function (or function pointer): its
        ///         ret_type is the DECLARED return type, which is what decides the shape.
        /// @param args generic_call convention: args[0] is the return-value slot (may be null).
        /// @param invoke A nullary callable that runs the call and yields its result.
        /// @note THE ONE COPY of the return-slot protocol. Both invokers use it — helpcaller (a
        ///       member call) and static_caller (a plain function call) — so the rules live in one
        ///       place: a null args[0] means DISCARD, a void result is never written, an assignable
        ///       result is assigned into the slot, and a result that cannot be assigned (a
        ///       non-copyable reference return, for instance) is invoked for its effect only.
        /// @note The callable is invoked AFTER the slot decision, in every branch: the number of
        ///       times the callee runs is one, always.
        template <class FP, class Invoke>
        static void deliver_result(void** args, Invoke&& invoke) {
            using ret_t = typename FP::ret_type;
            using Ret = std::remove_cvref_t<ret_t>;
            if constexpr (std::is_void_v<Ret>) {
                invoke();
            }
            else if constexpr (std::is_assignable_v<Ret&, ret_t>) {
                if (args[0]) {
                    *(static_cast<Ret*>(args[0])) = invoke();
                }
                else {
                    (void)invoke();
                }
            }
            else {
                // Not assignable into the return slot (a non-copyable reference return, for
                // instance): invoke for its effect.
                (void)invoke();
            }
        }

        /// @brief The invoker behind every method entry (methods, signals and slots).
        /// @tparam T The class declaring the function.
        /// @tparam func_ptr The member function, formed by splicing the function reflection.
        /// @param obj The object, type-erased.
        /// @param args generic_call convention: args[0] is the return-value slot (may be null),
        ///        args[I + 1] holds the address of the I-th argument.
        /// @note The invocation keeps the declared cv / ref qualification of the member (four branches),
        ///       and its result is handed to the caller by ASSIGNMENT (deliver_result()).
        /// @note A return type that cannot be assigned into the slot — the case is a member returning a
        ///       non-copyable, non-movable type by reference, such as a deleted typed facade's `T&`
        ///       — is invoked for its effect only. Without that branch the METHOD TABLE could not even
        ///       be built for a class that declares such a member.
        template <class T, auto func_ptr>
        static void helpcaller(void* obj, void** args) {
            using FP = detail::function_params_traits<decltype(func_ptr)>;
            using Args = typename FP::args_type;
            constexpr std::size_t param_count = std::tuple_size_v<Args>;
            // decltype(auto), not Ret: a reference-returning member must not be forced through a value.
            auto apply = [&]<std::size_t... Is>(std::index_sequence<Is...>) -> decltype(auto) {
                if constexpr (FP::is_const_qualified && FP::is_volatile_qualified) {
                    const volatile T* self = static_cast<volatile T*>(obj);
                    if constexpr (FP::is_rvalue_qualified) {
                        return (std::move(*self).*func_ptr)(read_arg<Args, Is>(args)...);
                    }
                    else {
                        return ((*self).*func_ptr)(read_arg<Args, Is>(args)...);
                    }
                }
                else if constexpr (FP::is_volatile_qualified) {
                    volatile T* self = static_cast<volatile T*>(obj);
                    if constexpr (FP::is_rvalue_qualified) {
                        return (std::move(*self).*func_ptr)(read_arg<Args, Is>(args)...);
                    }
                    else {
                        return ((*self).*func_ptr)(read_arg<Args, Is>(args)...);
                    }
                }
                else {
                    T* self = static_cast<T*>(obj);
                    if constexpr (FP::is_rvalue_qualified) {
                        return (std::move(*self).*func_ptr)(read_arg<Args, Is>(args)...);
                    }
                    else {
                        return ((*self).*func_ptr)(read_arg<Args, Is>(args)...);
                    }
                }
            };
            deliver_result<FP>(args, [&]() -> decltype(auto) {
                return apply(std::make_index_sequence<param_count>{});
            });
        }

        /// @brief Expand a plain function pointer's argument pack from the generic_call array.
        /// @tparam Args The parameter tuple (function_params_traits::args_type).
        /// @tparam Fn The function pointer type.
        /// @param fn The function to call.
        /// @param args args[I + 1] holds the address of the I-th argument.
        /// @param Is The index sequence that unpacks the tuple.
        /// @return decltype(auto), like helpcaller's apply: a reference-returning function must not
        ///         be forced through a value.
        template <typename Args, class Fn, std::size_t... Is>
        static decltype(auto) call_with_args(Fn fn, void** args, std::index_sequence<Is...>) {
            return fn(read_arg<Args, Is>(args)...);
        }

        /// @brief The invoker behind every STATIC method row.
        /// @tparam T The class declaring the function. It is NOT used in the call — a static member
        ///         needs no object — and is kept for symmetry with helpcaller and to name the
        ///         declaring class in the row's source.
        /// @tparam func_ptr The static member function, formed by splicing its reflection. It is a
        ///         PLAIN function pointer (`&T::static_fn` has no class in its type), which is exactly
        ///         why helpcaller cannot run it: `(*self).*func_ptr` is ill-formed for one.
        /// @param obj IGNORED. A static member has no implicit object; the parameter exists so
        ///        generic_call has ONE shape for every method row and the dispatch never branches on
        ///        kind (see method_info_t::caller).
        /// @param args generic_call convention, byte for byte helpcaller's: args[0] is the
        ///        return-value slot (may be null), args[I + 1] holds the address of the I-th argument.
        /// @note The return-slot protocol is deliver_result()'s — the same one helpcaller uses, not a
        ///       second copy of it.
        template <class T, auto func_ptr>
        static void static_caller(void* obj, void** args) {
            (void)obj;
            (void)sizeof(T);
            using FP = detail::function_params_traits<decltype(func_ptr)>;
            using Args = typename FP::args_type;
            constexpr std::size_t param_count = std::tuple_size_v<Args>;
            deliver_result<FP>(args, [&]() -> decltype(auto) {
                return call_with_args<Args>(func_ptr, args, std::make_index_sequence<param_count>{});
            });
        }

        /// @brief Read the I-th constructor argument by the constructor's declared parameter type.
        /// @tparam Ctor The constructor reflection.
        /// @tparam I Zero-based parameter index.
        /// @param args generic_call convention: args[0] is the return-value slot (unused here),
        ///        args[I + 1] holds the address of the I-th argument.
        /// @return The argument, forwarded exactly as the parameter type requires (an
        ///         rvalue-reference parameter is moved from, like helpcaller does for methods).
        template <std::meta::info Ctor, std::size_t I>
        static decltype(auto) read_ctor_arg(void** args) {
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Ctor));
            using ArgType = typename[: std::meta::type_of(params[I]) :];
            using Raw = std::remove_reference_t<ArgType>;
            auto* p = static_cast<Raw*>(args[I + 1]);
            if constexpr (std::is_rvalue_reference_v<ArgType>) {
                return std::move(*p);
            }
            else {
                return *p;
            }
        }

        /// @brief The declared type of parameter @p I of a constructor.
        /// @note Delegates to detail::constructor_param_type.
        template <std::meta::info Ctor, std::size_t I>
        static consteval std::meta::info constructor_param_type() {
            return detail::constructor_param_type<Ctor, I>();
        }

        template <class T, std::meta::info Ctor, std::size_t... Is>
        static consteval bool constructor_constructible_impl(std::index_sequence<Is...>) {
            // A REQUIRES-EXPRESSION, deliberately, not std::is_constructible_v. The trait performs
            // access checking "as if in a context unrelated to T and the Args" ([meta.unary.prop]),
            // so it cannot see a `friend class meta_info_t;` declared by T. A requires-expression is
            // access-checked in the context it is WRITTEN in — this static member of meta_info_t,
            // the very context the thunk's `new T(...)` runs in — so the friendship applies.
            // Do not "simplify" this to std::is_constructible_v: every non-public constructor would
            // silently become unconstructible.
            return requires {
                new T(std::declval<typename[: detail::constructor_arg_type<Ctor, Is>() :]>()...);
            };
        }

        /// @brief Whether the constructor thunk can compile its `new T(...)` for this constructor.
        /// @tparam T The class being constructed.
        /// @tparam Ctor The constructor reflection.
        /// @return false exactly for the shapes an ordinary `new T(args...)` rejects even with full
        ///         access: a deleted constructor, and every constructor of an abstract class.
        /// @note A non-public constructor is TRUE here as long as T declares the friend macros.h
        ///       adds (OBJECT / GADGET do): the friend gives meta_info_t's members — the thunk is
        ///       one — the access a member of T would have. That is the owner's rule: anything in
        ///       the table is invocable, whatever its access specifier (access() only reports it).
        /// @note The criterion is the requires-expression above, NOT std::is_constructible_v: the
        ///       trait's access checking is context-free by specification and can never see the
        ///       friend (see the comment on the implementation).
        /// @note It also answers false for a constructor whose parameter list cannot be reproduced
        ///       from parameters_of().
        template <class T, std::meta::info Ctor>
        static consteval bool constructor_constructible() {
            return constructor_constructible_impl<T, Ctor>(
                std::make_index_sequence<std::meta::parameters_of(Ctor).size()>{});
        }

        /// @brief Build one T and store it in the constructor's return slot.
        /// @tparam T The class being constructed; its constructor is access-checked like any other
        ///         call, which is why build_method_info picks this thunk only when the call compiles
        ///         (see constructor_constructible).
        /// @tparam Ctor The constructor reflection.
        /// @param args generic_call convention: args[0] is the return slot (a `void**`), args[I + 1]
        ///        holds the address of the I-th argument.
        /// @note `new T(...)` both allocates and constructs, and releases the storage itself when the
        ///       constructor throws. The result is written as a plain `void*`: the table describes any
        ///       class, not only an object_t subclass, so the thunk must not assume an object_t
        ///       sub-object exists (a GADGET has none). Casting the result back is the caller's job,
        ///       because only the caller knows which class it named.
        template <class T, std::meta::info Ctor, std::size_t... Is>
        static void construct_at(void** args, std::index_sequence<Is...>) {
            *static_cast<void**>(args[0]) =
                construct_new_with<T>(read_ctor_arg<Ctor, Is>(args)...);
        }

        /// @brief Log a constructor failure through log_connection at Critical.
        /// @tparam T The class whose constructor threw.
        /// @param what The exception text.
        template <class T>
        static void log_construct_failure(const char* what) {
            static constexpr std::string_view class_name =
                (std::string_view)std::define_static_string(std::meta::identifier_of(^^T));
            std::string message("constructor of ");
            message += class_name;
            message += " threw: ";
            message += (what != nullptr) ? what : "unknown exception";
            HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, message);
        }

        /// @brief The invoker behind every constructor entry (detail::constructor_info_t::caller).
        /// @tparam T The class being constructed.
        /// @tparam Ctor The constructor reflection.
        /// @param obj Unused: a constructor has no existing object (new_instance() passes nullptr).
        /// @param params params[0] is the return slot (a `void**` that receives the new instance's
        ///        address); params[1...] are the argument addresses.
        /// @note This is the only entry kind whose caller receives a null object: it builds the
        ///       instance with `new T(...)` and writes it as a `void*` through params[0]. A
        ///       method / signal / slot keeps using caller(obj, args).
        /// @note A throwing constructor is logged at Critical and then rethrown, so new_instance() can
        ///       report it and answer nullptr; `new` releases the storage it allocated.
        template <class T, std::meta::info Ctor>
        static void construct_caller(void* obj, void** params) {
            static_cast<void>(obj);
            constexpr std::size_t param_count = std::meta::parameters_of(Ctor).size();
            try {
                construct_at<T, Ctor>(params, std::make_index_sequence<param_count>{});
            }
            catch (const std::exception& e) {
                log_construct_failure<T>(e.what());
                throw;
            }
            catch (...) {
                log_construct_failure<T>(nullptr);
                throw;
            }
        }

        /// @brief `new T(args...)`, as the plain `void*` the return slot carries.
        /// @tparam T The class being constructed.
        /// @tparam Args The argument types read off the constructor's declared parameters.
        /// @param args The arguments.
        /// @return The new object as a `void*`; the constructor thunk stores it in the return slot.
        /// @note A separate function on purpose: the pack expansion of reflection-templated
        ///       read_ctor_arg() calls must happen in a plain CALL argument list (GCC 16.2 rejects
        ///       both a static_cast around it and a new-expression).
        template <class T, class... Args>
        static void* construct_new_with(Args&&... args) {
            return static_cast<void*>(new T(std::forward<Args>(args)...));
        }

        /// @brief The DECLARED access of a reflected member, as method_access_enum.
        /// @param member A function / constructor reflection.
        /// @return PRIVATE_SCOPE / PROTECTED_SCOPE / PUBLIC_SCOPE, in that order of test; none when
        ///         the reflection names no access (never the case for a class member, but the query
        ///         stays total).
        /// @note The predicates are the C++26 access queries std::meta::is_private / is_protected /
        ///       is_public. The member reflections come from members_of(..., access_context::unchecked),
        ///       so these report the SPECIFIER THE CLASS DECLARED, not whether the current context
        ///       may reach the member. The three are mutually exclusive and exhaustive for a class
        ///       member; private is tested first only to keep the order fixed.
        static consteval method_access_enum member_access(std::meta::info member) {
            if (std::meta::is_private(member)) {
                return method_access_enum::PRIVATE_SCOPE;
            }
            if (std::meta::is_protected(member)) {
                return method_access_enum::PROTECTED_SCOPE;
            }
            if (std::meta::is_public(member)) {
                return method_access_enum::PUBLIC_SCOPE;
            }
            return method_access_enum::NONE;
        }

        static consteval qualifier_t func_modifiers(std::meta::info func) {
            qualifier_t result = qualifier_flag::NONE;
            if (std::meta::is_const(func)) {
                result = result | qualifier_flag::CONST;
            }
            if (std::meta::is_volatile(func)) {
                result = result | qualifier_flag::VOLATILE;
            }
            if (std::meta::is_lvalue_reference_qualified(func)) {
                result = result | qualifier_flag::LVALUE_REF;
            }
            else if (std::meta::is_rvalue_reference_qualified(func)) {
                result = result | qualifier_flag::RVALUE_REF;
            }
            return result;
        }

        static consteval auto type_split(std::meta::info type) {
            struct result_t {
                std::meta::info type;
                qualifier_t modifiers;
            };

            qualifier_t modifier = qualifier_flag::NONE;
            if (std::meta::is_lvalue_reference_type(type)) {
                modifier = modifier | qualifier_flag::LVALUE_REF;
            }
            else if (std::meta::is_rvalue_reference_type(type)) {
                modifier = modifier | qualifier_flag::RVALUE_REF;
            }
            type = std::meta::remove_reference(type);
            if (std::meta::is_const(type)) {
                modifier = modifier | qualifier_flag::CONST;
            }
            if (std::meta::is_volatile(type)) {
                modifier = modifier | qualifier_flag::VOLATILE;
            }
            return result_t{
                std::meta::remove_cv(type),
                modifier
            };
        }

        /// @brief Strip the top-level cv and reference qualifiers off a type reflection.
        /// @param type A type reflection (a getter's return type, a data member's type, a parameter
        ///        type).
        /// @return The reflection of the same type with top-level const/volatile and any & / && removed.
        /// @note A std::meta QUERY, not a splice: `^^std::remove_cvref_t<typename[: type :]>` does not
        ///       compile when @p type is a consteval function PARAMETER, so the split is done with
        ///       remove_reference + remove_cv, which is exactly the property those aliases name.
        /// @note Both transformations also canonicalise the spelling: `std::string` and
        ///       `std::basic_string<char>` strip to the same reflection, which is what makes the
        ///       "bare type" comparison spelling-insensitive while `==` on the raw types is not.
        static consteval std::meta::info bare_type(std::meta::info type) {
            return std::meta::remove_cv(std::meta::remove_reference(type));
        }

        template <typename T>
        static consteval std::vector<detail::type_rewriting_t> build_type_rewrites() {
            static constexpr auto members = std::define_static_array(std::meta::members_of(^^T, std::meta::access_context::unchecked()));
            std::vector<detail::type_rewriting_t> type_rewrites;
            template for (constexpr auto member : members) {
                if (!std::meta::is_type_alias(member)) {
                    continue;
                }
                detail::type_rewriting_t type_rewrite;
                type_rewrite.from = (std::string_view)std::define_static_string(std::meta::identifier_of(member));
                type_rewrite.to = (std::string_view)std::define_static_string(detail::normalized_type_name(std::meta::dealias(member)));
                type_rewrite.modifiers = type_split(member).modifiers;
                type_rewrites.push_back(type_rewrite);
            }
            return type_rewrites;
        }

        /// @brief Build the normalised signature of a function or constructor reflection.
        /// @param func The function / constructor reflection.
        /// @return "name(type,type)" plus the member's cvref suffix in C++ order ("f(int) const &").
        ///         A constructor is named after its class — it has no identifier of its own, so the
        ///         name is taken from parent_of() — giving the form build_method_infos stores:
        ///         "class_t(int,int)".
        /// @note COMPILE-TIME layer: the parameter spelling is detail::normalized_type_name() applied
        ///       to the reflected type — no user text is parsed and no alias substitution happens.
        ///       The key is a pure function of the declaration, which is what lets the SIGNAL macro
        ///       build the very same key from its own function reflection.
        /// @note The cvref suffix tells two otherwise identical declarations apart ("f(int)" vs
        ///       "f(int) const"): const, volatile, then & / &&. A CONSTRUCTOR has neither and keeps
        ///       the bare "class_t(...)" form.
        /// @note The table's type-rewriting rules are NOT applied here: they exist for the run-time
        ///       layer, where the input is USER-PROVIDED TEXT (see normalized_signature).
        static consteval std::string normalized_function(std::meta::info func) {
            const bool is_ctor = std::meta::is_constructor(func);
            std::string sig;
            if (is_ctor) {
                // The BARE class name: a constructor signature is a key inside ONE table, so it needs
                // no namespace. The globally unique spelling is qualified_name, which the registry and
                // inherits() use.
                sig = std::meta::identifier_of(std::meta::parent_of(func));
            }
            else {
                sig = std::meta::identifier_of(func);
            }
            auto params = std::meta::parameters_of(func);
            sig += "(";
            for (auto param : params) {
                std::string type = detail::normalized_type_name(std::meta::type_of(param));
                sig += type + ",";
            }
            if (params.size()) {
                sig.pop_back();
            }
            sig += ")";
            if (!is_ctor && !std::meta::is_static_member(func)) {
                // A constructor has no cvref to report, and asking is_const() about one is not
                // meaningful — hence the guard. A STATIC member function has none either: its key is
                // the bare "f(int)" a user writes, never "f(int) const". (GCC answers false for all
                // four traits on a static member today, so the guard is also what keeps us from
                // depending on that answer: the suffix is a fact about an IMPLICIT OBJECT, and a
                // static member has none.)
                // The order is C++'s: const, volatile, & / &&.
                if (std::meta::is_const(func)) {
                    sig += " const";
                }
                if (std::meta::is_volatile(func)) {
                    sig += " volatile";
                }
                if (std::meta::is_lvalue_reference_qualified(func)) {
                    sig += " &";
                }
                else if (std::meta::is_rvalue_reference_qualified(func)) {
                    sig += " &&";
                }
            }
            return sig;
        }

        /// @brief Build the columns a METHOD row and a CONSTRUCTOR row hold in common.
        /// @tparam Row The row type to build: detail::method_info_t or detail::constructor_info_t.
        /// @tparam T The class the row belongs to.
        /// @tparam Func The member-function / constructor reflection.
        /// @return A row with its signature, name, access, parameter columns and flattened annotations filled.
        /// @note The two row types are UNRELATED (no base class, no inheritance): this function is a
        ///       template over them, not a shared base. It exists so the signature spelling, the
        ///       parameter columns and the annotation flattening are written ONCE — those columns mean
        ///       the same thing in both tables.
        /// @note Only the COMMON columns are filled here. A method row's own cvref / return type /
        ///       kind, and the caller thunk, differ per table and are added by the two builders below.
        template <typename Row, class T, std::meta::info Func>
        static consteval Row build_callable_columns() {
            constexpr bool is_ctor = std::meta::is_constructor(Func);
            static_assert(is_ctor == std::is_same_v<Row, detail::constructor_info_t>,
                "build_callable_columns: the row type must match the reflection (a constructor "
                "reflection builds a constructor_info_t, a member function a method_info_t)");

            Row row;
            // The declared access specifier of this very declaration, a hint only (see access()).
            row.access = member_access(Func);
            if constexpr (is_ctor) {
                // A constructor has no identifier of its own, so its row is named after its class.
                row.name = (std::string_view)std::define_static_string(std::meta::identifier_of(std::meta::parent_of(Func)));
            }
            else {
                row.name = (std::string_view)std::define_static_string(std::meta::identifier_of(Func));
            }

            std::vector<detail::compiling_string_t> param_types;
            std::vector<detail::compiling_string_t> param_names;
            std::vector<qualifier_t> param_qualifiers;
            // template for, not a plain loop: define_static_string() needs each parameter's
            // reflection as a constant expression.
            template for (constexpr auto param : std::define_static_array(std::meta::parameters_of(Func))) {
                std::string type = detail::normalized_type_name(std::meta::type_of(param));
                param_types.push_back((std::string_view)std::define_static_string(type));
                // An unnamed parameter yields an empty name; that is allowed (Qt does the same).
                // identifier_of throws for an unnamed reflection, so guard with has_identifier.
                param_names.push_back((std::string_view)std::define_static_string(
                    std::meta::has_identifier(param) ? std::meta::identifier_of(param) : std::string_view{ }));
                param_qualifiers.push_back(type_split(std::meta::type_of(param)).modifiers);
            }
            row.signature = (std::string_view)std::define_static_string(normalized_function(Func));
            row.param_types = std::define_static_array(param_types);
            row.param_names = std::define_static_array(param_names);
            row.param_qualifiers = std::define_static_array(param_qualifiers);

            // Tag annotations (make_tag / make_str_tag), flattened to plain data: a std::meta::info is
            // consteval-only and cannot live in a runtime table, so the entry keeps the carried value's
            // normalised type NAME plus the staticised pointer.
            //
            // TRAP: the annotation's type is CV-QUALIFIED — type_of(ann) is `const tag_a` — so a manual
            // `type_of(ann) == ^^tag_a` filter matches NOTHING; annotations_of_with_type() performs the
            // cv-insensitive match.
            std::vector<detail::annotation_entry_t> annotations;
            for (auto ann : std::meta::annotations_of_with_type(Func, ^^tag_a)) {
                const tag_a tag = std::meta::extract<tag_a>(ann);
                detail::annotation_entry_t entry;
                const std::string annotation_type_name = detail::normalized_type_name(tag.type);
                entry.type_name = (std::string_view)std::define_static_string(annotation_type_name);
                entry.value = tag.value;
                annotations.push_back(entry);
            }
            row.annotations = std::define_static_array(annotations);

            return row;
        }

        /// @brief Build one METHOD row (a method, a signal or a static member function).
        /// @tparam T The class the row belongs to (the thunk is generated for it).
        /// @tparam Func The member-function reflection.
        /// @return The row: build_callable_columns()'s columns plus this table's own cvref / return
        ///         type / kind and the invoker.
        /// @note The signature takes the method form "name(type,type)" plus the member's cvref suffix
        ///       ("f(int) const"), and only a NON-STATIC method carries one: a static member is
        ///       keyed as "f(int)", which is what a user writes.
        /// @note The three kinds are decided HERE, from the declaration alone: a static member is
        ///       STATIC_METHOD (it has no receiver), a member annotated [[= make_signal()]] is SIGNAL,
        ///       everything else is PLAIN_METHOD. A static member annotated as a signal is a
        ///       COMPILE-TIME ERROR: emitting a signal needs `this` (see macros.h), so there is no
        ///       function such a row could name.
        /// @note A constructor never reaches here: it has no return type (return_type_of() is a
        ///       consteval error for one), no cvref and no method kind, so it has its own row type and
        ///       its own builder — build_constructor_info() below. The static_assert is the guard that
        ///       says so instead of letting the wrong reflection produce a malformed method row.
        template <class T, std::meta::info Func>
        static consteval detail::method_info_t build_method_info() {
            static_assert(!std::meta::is_constructor(Func),
                "build_method_info: a constructor reflection belongs to build_constructor_info() — a "
                "constructor has no return type, no cvref and no method kind");
            constexpr bool is_static = std::meta::is_static_member(Func);
            detail::method_info_t info = build_callable_columns<detail::method_info_t, T, Func>();
            // The COMPILE-TIME layer spells the real type through detail::normalized_type_name
            // (the ORIGINAL spelling; see normalized_function). The table's type-rewriting rules
            // belong to the run-time user-text layer and are deliberately not applied to a
            // reflected type.
            const std::string return_type_name = detail::normalized_type_name(std::meta::return_type_of(Func));
            info.return_type = (std::string_view)std::define_static_string(return_type_name);
            if constexpr (is_static) {
                // A signal must not be declared static: emission goes through `this`, and a static
                // member has no implicit object. macros.h states the rule; this is where a violation
                // is caught, at compile time and by name, instead of building a row nobody can emit.
                static_assert(std::meta::annotations_of_with_type(Func, ^^signal_a).empty(),
                    "build_method_info: a static member function cannot be a signal — emitting a "
                    "signal needs `this`, and a static member has no implicit object");
                // No implicit object, so no cvref: the column is None and func_modifiers() is not
                // even asked (a static member cannot be cv- or ref-qualified; asking would be a
                // question about an object that does not exist).
                info.qualifier = qualifier_flag::NONE;
                info.kind = method_kind_enum::STATIC_METHOD;
                // The static thunk: `&[:Func:]` of a static member is a PLAIN function pointer, so
                // the member-pointer invoker (helpcaller) cannot run it. The row keeps the same
                // generic_call shape; only the callee differs.
                info.caller = &static_caller<T, &[:Func:]>;
            }
            else {
                info.qualifier = func_modifiers(Func);
                if (std::meta::annotations_of_with_type(Func, ^^signal_a).size()) {
                    info.kind = method_kind_enum::SIGNAL;
                }
                else {
                    info.kind = method_kind_enum::PLAIN_METHOD;
                }
                info.caller = &helpcaller<T, &[:Func:]>;
            }
            return info;
        }

        /// @brief Build one CONSTRUCTOR row.
        /// @tparam T The class being constructed (the thunk is generated for it).
        /// @tparam Ctor The constructor reflection.
        /// @return The row: build_callable_columns()'s columns plus the construction thunk. There is no
        ///         return type, cvref or kind column: a constructor has none of the three (its KIND is
        ///         answered by the handle type meta_constructor_t, not by a column).
        /// @note Whether the constructor is callable is decided HERE, at compile time, by
        ///       constructor_constructible: `new T(args...)` either compiles or it does not. The row is
        ///       emitted in both cases — the constructor is still what the class declares — but an
        ///       unconstructible one keeps caller == nullptr, and that null IS its callability verdict.
        /// @note The caller protocol is the constructor one: a NULL object (there is none yet) and
        ///       params[0] as a `void**` that receives the new instance's address.
        template <class T, std::meta::info Ctor>
        static consteval detail::constructor_info_t build_constructor_info() {
            static_assert(std::meta::is_constructor(Ctor),
                "build_constructor_info: a constructor row is built from a constructor reflection");
            detail::constructor_info_t info = build_callable_columns<detail::constructor_info_t, T, Ctor>();
            // The row's caller is the constructibility verdict: a deleted or abstract constructor
            // leaves it null, and new_instance() reports that null as the Critical "not callable"
            // path. A NON-PUBLIC constructor gets a real thunk like any other (the friend declarations
            // OBJECT / GADGET add give meta_info_t's members the class's own access). The thunk itself
            // knows T, builds the instance and writes it as a void*.
            if constexpr (constructor_constructible<T, Ctor>()) {
                info.caller = &construct_caller<T, Ctor>;
            }
            return info;
        }

        /// @brief Build the METHOD table: the methods, the signals and the static member functions.
        /// @tparam T The type to reflect.
        /// @return One entry per reflected method, in declaration order; the table key is the
        ///         normalised signature.
        /// @note STATIC member functions are part of this table (get_functions(true)): a static member
        ///       is a callable entry of the class, and its row says so through method_kind_enum
        ///       (STATIC_METHOD) and has_receiver(). The PROPERTY scan is deliberately not widened:
        ///       it keeps the default get_functions() call, so a static accessor-shaped function can
        ///       never bind as an instance property accessor.
        /// @note Constructors are NOT in this table: they have their own table and their own builder
        ///       (build_constructor_infos below), which is what keeps method_count() / method(index) /
        ///       signal() counting methods only, exactly like Qt.
        /// @note Each row is a detail::method_info_t — a different TYPE from a constructor row.
        template <class T>
        static consteval std::vector<detail::method_info_t> build_method_infos() {
            static constexpr auto funcs = std::define_static_array(detail::get_functions<T>(/*want_static=*/true));
            std::vector<detail::method_info_t> tables;
            template for (constexpr auto func : funcs) {
                tables.push_back(build_method_info<T, func>());
            }
            return tables;
        }

        /// @brief Build the constructor table: the CONSTRUCTORS of the class.
        /// @tparam T The type to reflect.
        /// @return One entry per reflected constructor, in members_of() order; the table key is the
        ///         normalised signature ("class_t(int,int)").
        /// @note A table of its own, and a separate INDEX SPACE: meta_object_t::constructor(index) /
        ///       index_of_constructor() count constructors only (Qt's constructorCount() /
        ///       constructor(index)), and the table is never inherited.
        /// @note Each row is a detail::constructor_info_t — a different TYPE from a method row, with no
        ///       return-type, cvref or kind column, because a constructor has none of those.
        template <class T>
        static consteval std::vector<detail::constructor_info_t> build_constructor_infos() {
            static constexpr auto ctors = std::define_static_array(detail::get_constructors<T>());
            std::vector<detail::constructor_info_t> tables;
            template for (constexpr auto ctor : ctors) {
                tables.push_back(build_constructor_info<T, ctor>());
            }
            return tables;
        }

        /// @brief Build the class-info table: every class_info_a annotation on the type T.
        /// @tparam T The type to reflect.
        /// @return The entries in declaration order; the keys and values live in static storage.
        template <class T>
        static consteval std::vector<detail::class_info_t> build_class_infos() {
            std::vector<detail::class_info_t> tables;
            static constexpr auto annos = std::define_static_array(std::meta::annotations_of_with_type(^^T, ^^class_info_a));
            template for (constexpr auto a : annos) {
                const auto info = std::meta::extract<class_info_a>(a);
                detail::class_info_t entry;
                entry.name = (std::string_view)std::define_static_string((std::string_view)info.name);
                entry.value = (std::string_view)std::define_static_string((std::string_view)info.value);
                tables.push_back(entry);
            }
            return tables;
        }

        /// @brief Build the enum metadata table: every enum declared inside the type T.
        /// @tparam T The type to reflect.
        /// @return The list of enum infos (declaration order); the enum names and the enumerator names
        ///         both live in static storage.
        /// @note Values are taken with extract\<E\>: under GCC the type of constant_of(enumerator) is
        ///       the enum type itself.
        /// @note is_flag criterion: the enumerator values are pairwise disjoint under bitwise AND.
        template <class T>
        static consteval std::vector<detail::enum_info_t> build_enum_infos() {
            static constexpr auto enums = std::define_static_array(detail::get_enums<T>());
            std::vector<detail::enum_info_t> tables;
            template for (constexpr auto e : enums) {
                using E = typename[: e :];
                detail::enum_info_t info;
                info.name = (std::string_view)std::define_static_string(std::meta::identifier_of(e));
                info.scope = (std::string_view)std::define_static_string(std::meta::identifier_of(^^T));
                info.is_scoped = std::meta::is_scoped_enum_type(e);
                info.underlying_size = std::meta::size_of(std::meta::underlying_type(e));

                std::vector<detail::enum_entry_t> entries;
                static constexpr auto items = std::define_static_array(std::meta::enumerators_of(e));
                template for (constexpr auto item : items) {
                    detail::enum_entry_t entry;
                    entry.name = (std::string_view)std::define_static_string(std::meta::identifier_of(item));
                    entry.value = static_cast<std::int64_t>(std::meta::extract<E>(std::meta::constant_of(item)));
                    entries.push_back(entry);
                }

                // is_flag: the pairwise bitwise AND is always 0 (each value owns its own bits); a
                // zero-valued entry does not affect the verdict.
                bool is_flag = true;
                for (std::size_t i = 0; i < entries.size() && is_flag; ++i) {
                    for (std::size_t j = i + 1; j < entries.size(); ++j) {
                        if ((entries[i].value & entries[j].value) != 0) {
                            is_flag = false;
                            break;
                        }
                    }
                }
                info.is_flag = is_flag;
                info.entries = std::define_static_array(entries);
                tables.push_back(info);
            }
            return tables;
        }

        /// @brief Build the interface table from T's direct base classes.
        /// @tparam T The class whose direct bases are scanned; its cast thunks down-cast through T.
        /// @return One entry per accessible interface base, in the bases' declaration order. The IIDs
        ///         are copied into static storage and every cast is a plain function pointer to
        ///         detail::interface_cast<T, I>.
        /// @note An interface is a base type carrying [[= make_interface("...")]] on its own
        ///       definition; a class implements it by inheriting it PUBLICLY. The scan uses
        ///       std::meta::access_context::unprivileged(), so a private or protected interface base
        ///       is skipped: the consumer's own `static_cast<I*>(T*)` is ill-formed for it, so a row
        ///       would hand out a pointer the class does not expose.
        /// @note A base that DECLARES a static meta-object is the class's meta-object parent (linked
        ///       separately by meta_object_t's consteval constructor, not by this table), so it is
        ///       skipped; a plain base class is skipped too, because it carries no make_interface
        ///       annotation. Declaration order is bases_of()'s order, so the interface row order is
        ///       the base list's order — nothing to keep in sync.
        /// @note The IID comes from the interface type's OWN annotation (interface_iid_of): an
        ///       interface base without one is a compile error naming it — never a null table key.
        /// @note The IID goes through define_static_string() even though make_interface already
        ///       staticises it: a pointer to the array define_static_string produces is what
        ///       reflect_constant accepts, as for every other table key in build_meta.
        /// @note The per-base entry is built by a helper call (interface_iid_of) rather than by
        ///       inlining annotations_of_with_type() here: inside a template-for body GCC rejects
        ///       span::operator[] in constant evaluation (the libstdc++ constexpr assert), while a
        ///       separate consteval callee evaluates the same expression fine.
        template <class T>
        static consteval std::vector<detail::interface_entry_t> build_interface_infos() {
            std::vector<detail::interface_entry_t> tables;
            std::vector<std::string_view> recorded;
            collect_interface_infos<T, T>(tables, recorded);
            return tables;
        }

        /// @brief Append one row per interface @p U carries, descending into each interface's bases.
        /// @tparam T The concrete class the thunks cast THROUGH (always the class being built).
        /// @tparam U The class whose direct bases are scanned (T itself, then each interface).
        /// @param tables The row list being built (one per distinct IID).
        /// @param recorded The IIDs already appended, so a diamond contributes one row.
        /// @note Interfaces are transitive: `IFoo2 : IFoo` means a class deriving from IFoo2 also
        ///       implements IFoo, and cast<IFoo> must find it. The inference therefore descends into
        ///       each interface's own bases. A non-interface base stops that branch: a meta-object
        ///       class's interfaces are reached through the meta-object chain instead, and a plain
        ///       base carries no IID at all.
        template <class T, class U>
        static consteval void collect_interface_infos(std::vector<detail::interface_entry_t>& tables,
                                                     std::vector<std::string_view>& recorded) {
            static constexpr auto ctx = std::meta::access_context::unprivileged();
            static constexpr auto bases = std::define_static_array(std::meta::bases_of(^^U, ctx));
            template for (constexpr auto base : bases) {
                using I = typename[: std::meta::type_of(base) :];
                // Cheap test first: only an interface base can contribute a row. Then reject the
                // PARENT: a base that DECLARES meta_object() is linked as the class's meta-object
                // parent by meta_object_t's consteval constructor, never a row here. A GADGET base
                // declares no accessor, so it may well carry an interface — it used to be skipped by
                // the (wrong) static_meta_object test, which lost that row.
                if constexpr (detail::is_interface_type<I>()
                              && !detail::declares_meta_object_accessor<I>()) {
                    const std::string_view iid = interface_iid_of<I>();
                    bool duplicate = false;
                    for (const std::string_view seen : recorded) {
                        if (seen == iid) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate) {
                        recorded.push_back(iid);
                        tables.push_back(detail::interface_entry_t{
                            detail::compiling_string_t(std::string_view(
                                std::define_static_string(iid))),
                            &detail::interface_cast<T, I> });
                        collect_interface_infos<T, I>(tables, recorded);
                    }
                }
            }
        }

        /// @brief Read a non-static data-member property into the return-value slot.
        /// @tparam T The class declaring the member.
        /// @tparam data_ptr The member pointer, formed by splicing the member reflection.
        /// @param obj The object, type-erased.
        /// @param args generic_call convention: args[0] is the return-value slot (may be null).
        /// @note The second template parameter must stay `auto` (a member pointer). Passing the
        ///       member's TYPE as a template argument cannot work: an `info` value is not a type
        ///       template argument (GCC: "couldn't deduce template parameter 'auto'"). The member type
        ///       is derived from the pointer instead.
        /// @note Unlike the getter thunk (helpcaller), this reads through `object.*data_ptr`, which is
        ///       also why a data member needs no separate reader per cv-qualification.
        template <class T, auto data_ptr>
        static void _reader_helper(void* obj, void** args) {
            using member_t = std::remove_cvref_t<decltype(std::declval<T&>().*data_ptr)>;
            T* self = static_cast<T*>(obj);
            if (args[0]) {
                *static_cast<member_t*>(args[0]) = (*self).*data_ptr;
            }
            else {
                (void)((*self).*data_ptr);
            }
        }

        template <class T>
        static consteval std::vector<detail::property_info_t> build_property_infos(const meta_info_t& infos) {
            static constexpr auto props = std::define_static_array(detail::get_properties<T>());
            // The candidate accessor set is the class's OWN reflected member functions — the very set
            // the method table is built from — materialised once for every property of the class.
            static constexpr auto accessor_candidates = std::define_static_array(detail::get_functions<T>());
            std::vector<detail::property_info_t> tables;
            std::size_t user_count = 0;
            template for (constexpr auto prop : props) {
                auto anno = std::meta::extract<property_a>(std::meta::annotations_of_with_type(prop, ^^property_a)[0]);
                detail::property_info_t prop_info;
                prop_info.name = (std::string_view)std::define_static_string((std::string_view)anno.name);
                prop_info.traits = anno.traits;

                // Required and Hidden contradict each other (a required property must be settable by
                // the front end, a hidden one is not shown there), and at most one property per class
                // may carry User.
                if (bool(anno.traits & property_trait_flag::REQUIRED)
                    && bool(anno.traits & property_trait_flag::HIDDEN)) {
                    throw std::meta::exception("a property cannot be both Required and Hidden", prop);
                }
                if (bool(anno.traits & property_trait_flag::USER)) {
                    if (user_count > 0) {
                        throw std::meta::exception("at most one property per class may be User", prop);
                    }
                    ++user_count;
                }

                // The COMPILE-TIME spelling of the property type: detail::normalized_type_name, i.e.
                // the ORIGINAL spelling. The class's type-rewriting rules belong to the run-time
                // user-text layer (normalized_signature), so they are never applied to a reflected
                // type.
                constexpr std::meta::info property_type_info = std::meta::is_function(prop)
                    ? std::meta::return_type_of(prop)
                    : std::meta::type_of(prop);
                const std::meta::info property_bare_type = bare_type(property_type_info);
                // Alias sugar removed, cv/ref KEPT — the "same type and qualifiers" comparison below.
                // GCC quirk this works around: a by-value getter's return type reflects as the ALIAS
                // it was written with (`std::string`), while a parameter of the same type reflects as
                // the dealiased type (`std::__cxx11::basic_string<char>`), so a raw `==` says "not the
                // same" for a setter that is in fact the exact match. dealias() strips only the alias
                // SPELLING; `const double&` and `double` stay different types, which is what the
                // qualifier rule needs.
                const std::meta::info property_exact_type = std::meta::dealias(property_type_info);

                if constexpr (std::meta::is_function(prop)) {
                    prop_info.type = (std::string_view)std::define_static_string(detail::normalized_type_name(property_type_info));
                    prop_info.reader = &helpcaller<T, &[:prop:]>;
                }
                else {
                    prop_info.type = (std::string_view)std::define_static_string(detail::normalized_type_name(property_type_info));
                    prop_info.reader = &_reader_helper<T, &[:prop:]>;
                }

                // Record the enum the property's type names, at build time and from the real
                // C++ type (no run-time string parsing of flag_t<...>). A plain enum records its own
                // simple name; a flag_t<E> records E's simple name and the wrapper fact in is_flags,
                // while its `type` column keeps the full flag_t<...> spelling.
                {
                    using property_t = std::remove_cvref_t<typename[: property_type_info :]>;
                    if constexpr (std::is_enum_v<property_t>) {
                        // The simple name is the tail of the normalized property type string.
                        std::string_view enum_tail = (std::string_view)prop_info.type;
                        if (const auto pos = enum_tail.rfind("::"); pos != std::string_view::npos) {
                            enum_tail.remove_prefix(pos + 2);
                        }
                        prop_info.enum_name = (std::string_view)std::define_static_string(enum_tail);
                    }
                    else if constexpr (detail::is_flag_v<property_t>) {
                        prop_info.is_flags = true;
                        const std::string enum_full = detail::normalized_type_name(^^detail::enum_of_flag_t<property_t>);
                        std::string_view enum_tail = enum_full;
                        if (const auto pos = enum_tail.rfind("::"); pos != std::string_view::npos) {
                            enum_tail.remove_prefix(pos + 2);
                        }
                        prop_info.enum_name = (std::string_view)std::define_static_string(enum_tail);
                    }
                }

                // ---- accessors --------------------------------------------------------------
                //
                // The annotation names an accessor by BARE NAME:
                //   [[= make_property("value", "set_value", "value_changed", "reset_value")]]
                // "" means "no such accessor". Resolution is by REFLECTION over the class's OWN
                // member functions (detail::get_functions<T>), never by matching text against the
                // table: the role fixes the parameter shape (writer = one parameter, reseter = none,
                // notifier = one matching parameter, then none), an exact type match wins, else a
                // SINGLE cv/ref-stripped match. Two candidates at the SAME level are a compile-time
                // error: a bare name cannot choose, and never a guess.
                //
                // Only the DECLARING class is scanned: an accessor declared by a base class keeps its
                // bare name and meta_property_t resolves it at run time against the meta-object chain.
                // A compile-time hit stores this table's own slot and thunk.
                auto annotated_name = [](const char* text) noexcept -> std::string_view {
                    return text != nullptr ? std::string_view(text) : std::string_view{};
                };
                prop_info.writer_name = (std::string_view)std::define_static_string(annotated_name(anno.writer));
                prop_info.reseter_name = (std::string_view)std::define_static_string(annotated_name(anno.reseter));
                prop_info.notifier_name = (std::string_view)std::define_static_string(annotated_name(anno.notifier));

                // The role rule of one property: the reflection of the declared accessor, or an empty
                // reflection (std::meta::info{}) when this class declares no candidate of that shape.
                auto pick_overload = [&](std::string_view name, std::size_t arity, const char* role) -> std::meta::info {
                    std::vector<std::meta::info> exact;
                    std::vector<std::meta::info> stripped;
                    // The notifier is the change-notification SIGNAL of the property (Q_PROPERTY's
                    // NOTIFY names a signal): a plain slot sharing the name must not bind, or
                    // is_notifiable() would be true and the later connect() would fail.
                    const bool notifier_role = std::string_view(role) == "notifier";
                    for (const std::meta::info candidate : accessor_candidates) {
                        if (!std::meta::has_identifier(candidate) || std::meta::identifier_of(candidate) != name) {
                            continue;
                        }
                        if (notifier_role
                            && std::meta::annotations_of_with_type(candidate, ^^signal_a).empty()) {
                            continue;
                        }
                        auto params = std::meta::parameters_of(candidate);
                        if (params.size() != arity) {
                            continue;
                        }
                        if (arity == 0) {
                            // No type to compare: the shape itself is the match.
                            exact.push_back(candidate);
                            continue;
                        }
                        const std::meta::info param_type = std::meta::type_of(params[0]);
                        if (std::meta::dealias(param_type) == property_exact_type) {
                            exact.push_back(candidate);
                        }
                        else if (bare_type(param_type) == property_bare_type) {
                            stripped.push_back(candidate);
                        }
                    }
                    auto ambiguous = [&](const std::vector<std::meta::info>& hits) {
                        std::string msg("property \"");
                        msg += (std::string_view)prop_info.name;
                        msg += "\": the ";
                        msg += role;
                        msg += " name \"";
                        msg += name;
                        msg += "\" matches several overloads that accept its type (";
                        for (std::size_t i = 0; i < hits.size(); ++i) {
                            if (i > 0) {
                                msg += ", ";
                            }
                            msg += normalized_function(hits[i]);
                        }
                        msg += "); a bare name cannot choose between them";
                        throw std::meta::exception(std::define_static_string(msg), prop);
                    };
                    if (exact.size() > 1) {
                        ambiguous(exact);
                    }
                    if (!exact.empty()) {
                        return exact.front();
                    }
                    if (stripped.size() > 1) {
                        ambiguous(stripped);
                    }
                    return stripped.empty() ? std::meta::info{} : stripped.front();
                };

                // Turn a matched reflection into a slot: the key is exactly the one build_method_infos
                // stored, and the stored signature is verified, so the perfect hash cannot map a
                // non-existent key onto some other slot.
                auto find_sig_in = [](const meta_info_t& table, const std::string& sig) -> index_t {
                    const std::uint64_t slot = table.method_hashtable.find(sig);
                    if (slot == static_cast<std::uint64_t>(-1)
                        || (std::string_view)table.method_infos.to_span()[static_cast<std::size_t>(slot)].signature != sig) {
                        return index_npos;
                    }
                    return static_cast<index_t>(slot);
                };
                auto slot_of_match = [&](std::meta::info fn) -> index_t {
                    if (fn == std::meta::info{}) {
                        return index_npos;
                    }
                    return find_sig_in(infos, normalized_function(fn));
                };

                const std::string_view writer_name = (std::string_view)prop_info.writer_name;
                if (!writer_name.empty()) {
                    prop_info.writer_slot = slot_of_match(pick_overload(writer_name, 1, "writer"));
                }
                const std::string_view reseter_name = (std::string_view)prop_info.reseter_name;
                if (!reseter_name.empty()) {
                    prop_info.reseter_slot = slot_of_match(pick_overload(reseter_name, 0, "reseter"));
                }
                const std::string_view notifier_name = (std::string_view)prop_info.notifier_name;
                if (!notifier_name.empty()) {
                    std::meta::info notifier_fn = pick_overload(notifier_name, 1, "notifier");
                    if (notifier_fn == std::meta::info{}) {
                        notifier_fn = pick_overload(notifier_name, 0, "notifier");
                    }
                    prop_info.notifier_slot = slot_of_match(notifier_fn);
                }

                if (prop_info.writer_slot != index_npos) {
                    prop_info.writer = infos.method_infos.to_span()[static_cast<std::size_t>(prop_info.writer_slot)].caller;
                }
                if (prop_info.reseter_slot != index_npos) {
                    prop_info.reseter = infos.method_infos.to_span()[static_cast<std::size_t>(prop_info.reseter_slot)].caller;
                }
                if (prop_info.notifier_slot != index_npos) {
                    prop_info.notifier = infos.method_infos.to_span()[static_cast<std::size_t>(prop_info.notifier_slot)].caller;
                }


                tables.push_back(prop_info);
            }
            return tables;
        }
    };
}

#endif