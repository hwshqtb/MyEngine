#ifndef HWSHQTB__METAOBJECT__MACRO_H
#define HWSHQTB__METAOBJECT__MACRO_H

#include <cstddef>
#include <string_view>

// The friend declarations below name these two classes (a qualified friend declaration may only
// REFER to a type, never declare one). They are forward-declared here, not #included: macros.h stays
// free of the library headers, so a translation unit may include it before, after or without them.
namespace hwshqtb::metaobject {
    class meta_info_t;
    class meta_object_t;
}

// SIGNAL: the [[= signal_a{}]] marker plus an injected definition. That definition is the
// emitter: at compile time it reflects this function, resolves the signal handle through the
// object's virtual meta_object(), builds the params array and hands it to
// object_t::activate_signal, which dispatches according to connection_type (Direct).
//
// Constraints:
//  - Only usable for member functions of a class derived from object_t (it needs
//    this->activate_signal and this->meta_object()).
//  - A signal must not be declared static (emission needs this).
//  - Signals SHOULD be declared const: emitting does not change the sender's observable state, and a
//    const signal can be emitted from const member functions and from const objects. Non-const
//    signals are accepted, not rejected — the const-ness is a recommendation, not a rule.
//  - Consumers must #include <meta> before using these macros: the emitters splice on the function
//    reflection, and this header deliberately includes no library header.
//
// The emitter goes through this->meta_object(), NOT through the declaring class's static: that is
// what makes a signal declared in a dynamic_base_t subclass resolve to the run-time built table,
// while a statically declared class gets the same answer from its static facade either way.
//
// The signal's lookup key is built at COMPILE TIME from the function reflection itself
// (meta_info_t::signature_of -> normalized_function: the real parameter types through
// detail::normalized_type_name, i.e. the ORIGINAL spelling), which is exactly how build_method_infos
// spells the table key. No user text is parsed and the run-time normalizer (normalized_signature,
// with its type-registry lookup) is NOT involved.
#define SIGNAL(declaration)                                                                          \
    [[= hwshqtb::metaobject::make_signal()]] declaration {                                              \
        constexpr auto _sig_fn = std::meta::current_function();                                       \
        static constexpr std::string_view _sig_key =                                                  \
            ::hwshqtb::metaobject::meta_info_t::signature_of(_sig_fn);                                \
        static constexpr auto _sig_params = std::define_static_array(std::meta::parameters_of(_sig_fn)); \
        void* _sig_argv[_sig_params.size() + 1];                                                      \
        _sig_argv[0] = nullptr;                                                                       \
        std::size_t _sig_i = 1;                                                                       \
        template for (constexpr auto _sig_p : _sig_params) {                                          \
            _sig_argv[_sig_i++] =                                                                     \
                const_cast<void*>(static_cast<const void*>(&[: std::meta::variable_of(_sig_p) :]));   \
        }                                                                                             \
        ::std::array<::hwshqtb::metaobject::any_t, _sig_params.size()> _sig_args{ };                   \
        std::size_t _sig_a = 0;                                                                       \
        /* A NON-OWNING alias per parameter: std::ref on a const object yields                            \
           reference_wrapper<const T>, so from_ref() records CONST and a T& callback is refused            \
           while a const T& one is accepted. capture() checks the base type matches.                     */ \
        template for (constexpr auto _sig_p : _sig_params) {                                          \
            using _sig_decl_t = [: std::meta::type_of(_sig_p) :];                                     \
            using _sig_t = ::std::remove_cvref_t<_sig_decl_t>;                                        \
            if constexpr (::std::is_const_v<::std::remove_reference_t<_sig_decl_t>>) {                \
                _sig_args[_sig_a++] = ::hwshqtb::metaobject::any_t::capture(                          \
                    ::hwshqtb::metaobject::meta_type_t::from_type<_sig_t>(),                          \
                    ::std::cref(static_cast<const _sig_t&>([: std::meta::variable_of(_sig_p) :])));        \
            }                                                                                         \
            else {                                                                                    \
                _sig_args[_sig_a++] = ::hwshqtb::metaobject::any_t::capture(                          \
                    ::hwshqtb::metaobject::meta_type_t::from_type<_sig_t>(),                          \
                    ::std::ref(const_cast<_sig_t&>(                                                     \
                        static_cast<const _sig_t&>([: std::meta::variable_of(_sig_p) :]))));                \
            }                                                                                         \
        }                                                                                             \
        this->activate_signal(this->meta_object()->signal(_sig_key), _sig_argv, _sig_args);           \
    }

// OBJECT: declares the class's ONE meta-object table and the virtual accessor that hands it out.
//
//  - static_meta_object: a real static data member, defined by META_OBJECT in a .cpp. It is one
//    symbol for the whole program and is what the shared library exports. It is `const`, not
//    `constexpr`, because a static data member's initialiser cannot be split from its declaration;
//    `constinit` restores the guarantee that split would otherwise lose — the initialiser must be a
//    constant expression, so the object is constant-initialised. That is not cosmetic: another
//    translation unit may use `T::static_meta_object` inside its own constant expression (the base
//    link `_super` does exactly that), and a dynamically initialised object would not be usable
//    there; constant initialisation is also what keeps it clear of the static-init-order problem.
//  - meta_object(): the virtual accessor. It returns a POINTER: the meta-object owns its table by
//    value, so handing out a copy would invalidate every handle taken from it. dynamic_base_t
//    overrides this one function to report a run-time built table (see DYNAMIC_BASE).
//
// The accessor is `virtual`, not `override`: OBJECT also declares the table of the class that
// INTRODUCES the virtual (object_t), and that class has no base to override. Every derived class
// still overrides it; DYNAMIC_BASE is the one place that spells `override` (it always has a base).
//
// There is deliberately no meta_info() and no static_meta_info() accessor: the table is private to
// meta_object_t, and every caller reaches its contents through the meta-object's public queries
// (class_name(), method(), property(), enumeration(), ...).
//
// The two friend declarations open the class's NON-PUBLIC members to the table. The constructor /
// method thunks (construct_caller, construct_at, construct_new_with, helpcaller, ...) are static
// members of meta_info_t, so the friend declaration gives them the access a member of the class has.
// That is what makes a private / protected CONSTRUCTOR actually constructible: the thunk's
// `new T(args...)` is an ordinary access-checked expression.
// This is the owner's rule: in a meta-object system anything in the table is invocable, whatever its
// access specifier; access is a hint, reported by meta_method_t::access(), not a gate.
// meta_object_t is befriended as well (the meta-object facade is part of the same seam).
//
// The meta-object CHAIN is decided by this accessor, not by the table: a class that declares
// meta_object() is an OBJECT class, and only such a class can be found through a base-typed pointer
// at run time (the function is virtual, so the answer follows the dynamic type). The consteval
// constructor classifies T's DIRECT bases in one level and requires one of two shapes:
//   - exactly one base declares meta_object(): it becomes super_class();
//   - no base declares it AND no base inherits it: T is a root, super_class() is nullptr.
// Anything else is a compile error: a base that INHERITS meta_object() without declaring it proves an
// OBJECT class sits above it, so linking across would name an ancestor while a real class of the chain
// is skipped. Declare OBJECT on that base too, or drop it from the chain. Because GADGET declares no
// accessor, a GADGET base is not a chain link: `OBJECT : GADGET` is legal and the object is a root
// (the gadget's rows stay behind GADGET::static_meta_object).
//
// Interfaces are NOT listed: an interface type carries [[= make_interface("...")]] on its own
// definition, and a class implements it by inheriting it PUBLICLY. build_meta<T>() reflects T's
// direct bases and records one interface row per accessible interface base, so the base list is the
// one place the relationship is written — there is no INTERFACES(...) list to keep in sync. A private
// or protected interface base is deliberately NOT recorded: a consumer's static_cast<I*>(T*) is
// ill-formed for it, so the table must not hand out a pointer the class does not expose.
#define OBJECT                                                                                  \
public:                                                                                         \
    static constinit const ::hwshqtb::metaobject::meta_object_t static_meta_object;             \
    virtual const ::hwshqtb::metaobject::meta_object_t* meta_object() const noexcept {          \
        return &static_meta_object;                                                             \
    }                                                                                           \
    friend class ::hwshqtb::metaobject::meta_info_t;                                            \
    friend class ::hwshqtb::metaobject::meta_object_t;                                          \
private:

/// @brief Declare the meta-object table of a value type: no virtual function, no accessor.
/// @note Usage: GADGET inside the class, META_OBJECT(TYPE) in exactly one .cpp.
/// @note Reached as TYPE::static_meta_object — there is deliberately no meta_object() member.
///
/// The Q_GADGET equivalent. It declares the static table plus the two friend declarations that open
/// the type's non-public members to the table (the same seam OBJECT declares — a constructor or a
/// method is invocable whatever its access specifier), and nothing else: no virtual function (so no
/// vtable, sizeof stays that of the bare struct, and the type stays trivially copyable / usable in a
/// union), and no member accessor — a value type has no object identity to dispatch through, so
/// `Type::static_meta_object` is the one honest way to reach the table.
///
/// What still works through the static table: class name, properties, methods, enumerations,
/// class-info, the interface table (a gadget implements interfaces by inheriting them publicly: the
/// rows are inferred from its direct bases and their cast thunk is type-erased, so it does not
/// require an object_t; implements() and interface_count() answer from it) and new_instance()
/// (meta_info_t is built by exactly the same build_meta<T>() as for OBJECT). What does not:
/// everything that needs an object_t — signals, events, the object tree and the object-bound cast()
/// entry. new_instance() returns the constructed value as a `void*`: the caller knows it named the
/// gadget and casts it back (e.g. `static_cast<point_t*>`).
///
/// A GADGET is a VALUE type: no vtable, so no virtual meta_object() and NO meta-object chain. Its
/// table is reached as `TYPE::static_meta_object`, and `super_class()` is always nullptr:
///   - `GADGET : GADGET` is allowed, but the derived table does NOT link the base — each gadget is a
///     root, and the base's rows stay behind `Base::static_meta_object`;
///   - `GADGET : OBJECT` is a compile error: the inherited meta_object() would report the BASE's
///     table while `static_meta_object` is the gadget's own, so one object would have two answers.
/// Use OBJECT for a class that must take part in a chain.
///
/// An interface type carries [[= make_interface("...")]] on its own definition, and a class
/// implements it by inheriting it PUBLICLY — the same rule as OBJECT; there is no list to keep in
/// sync. build_meta<T>() reflects the gadget's direct bases, so a private / protected interface base
/// is not reported (a consumer's static_cast<I*>(T*) would not compile for it).
///
/// META_OBJECT(TYPE) defines the declared static in exactly one .cpp, unchanged: the definition
/// does not care whether the declaration came from OBJECT or GADGET.
#define GADGET                                                                  \
public:                                                                         \
    static constinit const ::hwshqtb::metaobject::meta_object_t static_meta_object; \
    friend class ::hwshqtb::metaobject::meta_info_t;                            \
    friend class ::hwshqtb::metaobject::meta_object_t;                          \
private:

/// @brief Define the meta-object declared by OBJECT (or GADGET) (one .cpp per class).
/// @note Direct-initialisation (braces, not `=`): the converting constructor is explicit.
/// @note The second argument, the identity_class_t tag, is how the consteval constructor learns TYPE:
///       the meta_info_t it receives is already built and carries no reflection, so without the tag
///       it could not reflect TYPE's direct bases and link `_super` to the base class's
///       `static_meta_object`. The tag is an implementation detail of this macro (it is the only
///       thing that names it), not part of the public surface; it is spelled through meta_object_t
///       rather than detail so the macro only depends on the documented surface.
#define META_OBJECT(TYPE)                                                                       \
    const ::hwshqtb::metaobject::meta_object_t TYPE::static_meta_object{                        \
        ::hwshqtb::metaobject::meta_info_t::build_meta<TYPE>(),                                 \
        ::hwshqtb::metaobject::meta_object_t::identity_class_t<TYPE>{} }                          \

/// @brief Declare that a class reports the mixin's run-time built meta-object.
///
/// meta_object() is generated by the OBJECT macro, so a sibling base class cannot override it; the
/// final class spells it out, and this macro is that one line. There is no meta_info(): DYNAMIC_BASE
/// only redirects the one virtual accessor.
#define DYNAMIC_BASE                                                                            \
public:                                                                                         \
    const ::hwshqtb::metaobject::meta_object_t* meta_object() const noexcept override {         \
        return this->facade();                                                                  \
    }                                                                                           \
private:

#endif
