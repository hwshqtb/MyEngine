#ifndef HWSHQTB__METAOBJECT__REGISTER_OBJECT_H
#define HWSHQTB__METAOBJECT__REGISTER_OBJECT_H

/// @file register_object.h
/// @brief The class registry behind object_t::register_type / create: the process-wide name -> class
///        map, its introspection entry point and the registration macros.
///
/// @par What the registry stores
/// `const meta_object_t*` and nothing else — there is deliberately no creator function: a meta-object
/// already knows how to allocate and construct an instance of its class, so creation is always
/// `meta_object_t::new_instance()`, one construction path for the library, the tests and the host.
///
/// @par Keys
/// The PRIMARY key is `type->class_name()`. When the class name contains "::" — which is how a
/// run-time built type (meta_object_builder_t) may name itself — the part after the last "::" is
/// registered as an ALIAS, unless that short name is already a primary key or an alias. First
/// registration wins.
///
/// @par One instance per module
/// class_registry_t::instance() is a function-local static, so a shared build has one registry
/// inside the DLL that owns object_t: a type registered in an executable and a type registered in
/// the DLL are in different registries.
///
/// @par Thread safety and lifetime
/// One mutex guards both maps; registration may happen from the static initialisers of several
/// translation units and creation from any thread. The registry owns neither the meta-objects (they
/// outlive it: static or arena storage) nor the instances (the caller or the parent owns them).

#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "hwshqtb/metaobject/export.h"
#include "hwshqtb/metaobject/meta_object_t.h"
#include "hwshqtb/metaobject/object_t.h"

namespace hwshqtb::metaobject::detail {
    /// @brief The process-wide name -> class registry behind object_t::register_type / create.
    /// @note Nothing is built here: the map only answers "which meta-object is registered under this
    ///       name", and meta_object_t::new_instance() does the constructing. ("type registry" is taken
    ///       by meta_type_t's process-wide metatype registry, so this one maps class names to
    ///       meta-objects.)
    class HWSHQTB_EXPORT class_registry_t {
    public:
        /// @brief Construct an empty registry.
        class_registry_t();

        /// @brief Destroy the registry (its entries are non-owning; nothing is deleted).
        ~class_registry_t();

        class_registry_t(const class_registry_t&) = delete;
        class_registry_t& operator=(const class_registry_t&) = delete;
        class_registry_t(class_registry_t&&) = delete;
        class_registry_t& operator=(class_registry_t&&) = delete;

        /// @brief The process-wide class registry (one instance per module, like
        ///        detail::meta_arena::instance()).
        /// @return The same registry on every call for as long as the module lives.
        /// @note static because there is exactly one registry per module; the class-level
        ///       HWSHQTB_EXPORT covers this member too.
        static class_registry_t& instance();

        /// @brief Register @p type under its class name (first registration wins).
        /// @param type The class's meta-object; stored by address, so it must outlive the registry.
        /// @return true when it was added; false (plus one Warning) when @p type is null, has no class
        ///         name, or the class name is already registered.
        bool add(const meta_object_t* type);

        /// @brief Look a class up by name (primary key first, then alias).
        /// @return The registered meta-object, or nullptr when the name is unknown.
        const meta_object_t* find(std::string_view type_name) const;

        /// @brief Every name find() accepts (primary names and aliases), sorted and duplicate-free.
        std::vector<std::string> names() const;

        /// @brief The number of registered classes (aliases are not counted).
        std::size_t size() const;

    private:
        /// @brief Guards both maps: registration may run concurrently from several static initialisers.
        mutable std::mutex _mutex;

        /// @brief Primary class name -> meta-object; a std::map (not unordered) so names() is sorted and
        ///        the insertion order never leaks into introspection.
        std::map<std::string, const meta_object_t*, std::less<>> _classes;
        /// @brief Short name -> primary class name; first registration wins, like the primary table.
        std::map<std::string, std::string, std::less<>> _aliases;
    };
}

/// @brief Paste two macro arguments after expanding both (the indirection is what lets __COUNTER__
///        expand before the paste). The variable name cannot be derived from TYPE: a token-pasted
///        qualified spelling (`ns::type`) would not form an identifier.
#define HWSHQTB_DETAIL_JOIN(A, B) A##B
#define HWSHQTB_DETAIL_JOIN_EXPAND(A, B) HWSHQTB_DETAIL_JOIN(A, B)

/// @brief Register a class so object_t::create() can build it by name.
/// @param TYPE The class name (a qualified spelling works: `&TYPE::static_meta_object` is expanded
///        as-is). The class must derive from object_t and declare OBJECT.
///
/// Expands to one namespace-scope constant in an unnamed namespace: registration happens during the
/// static initialisation of the using translation unit. `TYPE::static_meta_object` is `constinit`
/// (macros.h), so the table is a constant before any dynamic initialiser runs — there is no
/// static-initialisation-order hazard.
#define META_REGISTER_TYPE(TYPE)                                                                     \
    namespace {                                                                                      \
        [[maybe_unused]] const bool HWSHQTB_DETAIL_JOIN_EXPAND(hwshqtb_registered_type_, __COUNTER__) \
            = ::hwshqtb::metaobject::object_t::register_type(&TYPE::static_meta_object);              \
    }

/// @brief Register a ui::widget_t subclass with the same class registry.
/// @param TYPE The class name; it must be default-constructible.
///
/// A widget is registered exactly like any other class: create() runs the class's 0-argument
/// constructor and attaches the parent with object_t::set_parent(), so this macro is only the
/// call-site statement of intent.
#define META_REGISTER_WIDGET(TYPE) \
    META_REGISTER_TYPE(TYPE)

#endif
