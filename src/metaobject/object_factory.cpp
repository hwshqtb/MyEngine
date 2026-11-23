/// @file object_factory.cpp
/// @brief The process-wide class registry behind object_t::register_type / create, and the object_t
///        entry points that reach it.
///
/// The registry owns neither meta-objects nor instances: it maps a name (the primary class name plus
/// the optional short-name alias) to the `const meta_object_t*` the registration supplied. That is the
/// whole index — the object tree keeps owning the objects, and the meta-object keeps knowing how to
/// build one of its class.
///
/// Creation is deliberately narrow: the class's OWN table must contain a callable 0-argument
/// constructor. There is no creator function and no second construction path. A class without such a
/// constructor (an abstract class, a class whose constructors all take arguments, a run-time built
/// meta_object_builder_t table) is not creatable by name: object_t::create() reports it with one
/// Warning and returns nullptr.
///
/// Parenting is applied AFTER construction with object_t::set_parent(). A widget is creatable by name
/// because its 0-argument constructor leaves the visual parent null: the visual-parent parameter is
/// `widget_t*`, while the object-level parent link is separate and takes an object_t*.

#include "hwshqtb/metaobject/register_object.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hwshqtb/metaobject/log.h"
#include "hwshqtb/metaobject/meta_info_t.h"

namespace hwshqtb::metaobject::detail {
    namespace {
        /// @brief The signature of the class's own callable 0-argument constructor.
        /// @param type The class's meta-object.
        /// @return The normalised signature (method form), or an empty view when there is none.
        /// @note Only the class's OWN constructor table is scanned: a base class's 0-argument
        ///       constructor would build an instance of the BASE class, not of the registered one.
        ///       constructor_count() / constructor(index) already answer for this class alone
        ///       (constructors are never inherited), so no chain arithmetic is involved.
        /// @note "Callable" is the entry's caller: build_method_info leaves it null for a constructor
        ///       whose `new T(...)` does not compile, so every constructor of an abstract class is
        ///       null and the class has no usable constructor. The public handle exposes that verdict
        ///       as is_valid().
        std::string_view zero_argument_constructor(const meta_object_t& type) {
            const std::size_t total = type.constructor_count();
            for (std::size_t i = 0; i < total; ++i) {
                const meta_constructor_t row = type.constructor(static_cast<index_t>(i));
                if (row.param_count() != 0 || !row.is_valid()) {
                    continue;
                }
                return row.signature();
            }
            return {};
        }
    }

    class_registry_t::class_registry_t() = default;

    class_registry_t::~class_registry_t() = default;

    bool class_registry_t::add(const meta_object_t* type) {
        if (type == nullptr) {
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING,
                "object_t::register_type: type is null; nothing was registered");
            return false;
        }
        // The QUALIFIED name is the primary key: a bare identifier collides between two classes of
        // the same name in different namespaces, and then one of them could never be created.
        const std::string_view name = type->qualified_class_name();
        if (name.empty()) {
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING,
                "object_t::register_type: the meta-object has no class name; nothing was registered");
            return false;
        }

        const std::string full{ name };
        std::lock_guard lock(_mutex);
        const auto [position, inserted] = _classes.try_emplace(full, type);
        (void)position;
        if (!inserted) {
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING,
                "object_t::register_type: '" + full
                    + "' is already registered; the first registration is kept");
            return false;
        }

        // Short-name alias: the tail after the last "::". It is only added when it is not already taken
        // by a primary key or another alias, so first registration wins here as well.
        const std::size_t separator = full.rfind("::");
        if (separator != std::string::npos) {
            std::string alias = full.substr(separator + 2);
            if (!alias.empty() && !_classes.contains(alias) && !_aliases.contains(alias)) {
                _aliases.emplace(alias, full);
            }
        }
        return true;
    }

    const meta_object_t* class_registry_t::find(std::string_view type_name) const {
        std::lock_guard lock(_mutex);
        if (const auto found = _classes.find(type_name); found != _classes.end()) {
            return found->second;
        }
        const auto alias = _aliases.find(type_name);
        if (alias == _aliases.end()) {
            return nullptr;
        }
        const auto primary = _classes.find(alias->second);
        return primary != _classes.end() ? primary->second : nullptr;
    }

    std::vector<std::string> class_registry_t::names() const {
        std::lock_guard lock(_mutex);
        std::vector<std::string> result;
        result.reserve(_classes.size() + _aliases.size());
        for (const auto& [name, type] : _classes) {
            result.push_back(name);
        }
        for (const auto& [alias, primary] : _aliases) {
            result.push_back(alias);
        }
        // One sorted, duplicate-free list: each map is sorted on its own, but an alias may sort between
        // primary names and the contract is the merged order.
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    std::size_t class_registry_t::size() const {
        std::lock_guard lock(_mutex);
        return _classes.size();
    }

    class_registry_t& class_registry_t::instance() {
        // Function-local static: one registry per module (the meta_arena::instance() rule).
        // It owns nothing but strings and borrowed pointers, so destroying it at exit cannot delete a
        // registered meta-object (static / arena storage) nor an instance.
        static class_registry_t registry;
        return registry;
    }
}

namespace hwshqtb::metaobject {
    bool object_t::register_type(const meta_object_t* type) {
        return detail::class_registry_t::instance().add(type);
    }

    object_t* object_t::create(std::string_view type_name, object_t* parent) {
        const meta_object_t* type = detail::class_registry_t::instance().find(type_name);
        if (type == nullptr) {
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING,
                "object_t::create: no class registered as '" + std::string(type_name) + "'");
            return nullptr;
        }

        // The one creation path: the class's own 0-argument constructor through its meta-object. A
        // run-time built table has no constructors, an abstract class has only null-caller constructors,
        // and a class whose every constructor takes arguments has no 0-argument one — all three end
        // here, with one Warning.
        const std::string_view constructor = detail::zero_argument_constructor(*type);
        if (constructor.empty()) {
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING,
                "object_t::create: '" + std::string(type->class_name())
                    + "' has no usable 0-argument constructor; it cannot be created by name");
            return nullptr;
        }

        // The construction thunk writes the new instance's address into this slot: the table describes
        // any class, so this caller — which registered an object_t subclass — names the target type by
        // casting. The cast is the one place that knows the class is an object_t.
        void* created = nullptr;
        if (!type->construct_into(constructor, {}, &created) || created == nullptr) {
            return nullptr; // construct_into() reported the exact reason (Critical for a failed call)
        }
        object_t* object = static_cast<object_t*>(created);

        // A constructor signature is the class NAME plus parameters, and new_instance() resolves it
        // through the chain. Verifying the dynamic meta-object keeps the registry honest: a signature
        // that resolved to a different class must not hand back the wrong instance.
        if (object->meta_object() != type) {
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING,
                "object_t::create: constructor '" + std::string(constructor) + "' built a '"
                    + std::string(object->meta_object()->class_name()) + "', not the registered '"
                    + std::string(type->class_name()) + "'; the instance was deleted");
            delete object;
            return nullptr;
        }

        // The object-level parent is a SEPARATE link from a widget's visual parent, and it is applied
        // here, after construction. create() therefore needs no object_t* -> widget_t* adaptation: the
        // 0-argument widget constructor leaves the visual parent null, and set_parent() performs the
        // tree link (for a widget parent this also makes parent_widget() answer it).
        if (parent != nullptr && object->parent() != parent) {
            object->set_parent(parent);
        }
        return object;
    }

    std::vector<std::string> object_t::registered_type_names() {
        return detail::class_registry_t::instance().names();
    }
}
