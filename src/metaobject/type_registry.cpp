#include <hwshqtb/metaobject/type_interface_t.h>

#include <hwshqtb/metaobject/meta_type_t.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "hwshqtb/metaobject/log.h"

namespace hwshqtb::metaobject {
    namespace detail {
        namespace {
            /// @brief One registered conversion: (source id, target id) -> caller.
            using conversion_key = std::pair<index_t, index_t>;

            /// @brief One name-table entry: the first interface that carried the name plus the id the
            ///        registry assigned to that name (0 until ensure_registered assigns it).
            /// @note The id is stored by VALUE so a re-registration never dereferences the stored
            ///       interface pointer — that pointer may belong to a module which has since been
            ///       unloaded (a value of a module-declared type must not outlive its module; the
            ///       name table only needs the id to keep the identity stable).
            struct name_entry_t {
                index_t id = 0;
                const type_interface_t* iface = nullptr;
            };

            /// @brief The one process-wide type registry.
            ///
            /// @note Exactly ONE instance per process, shared by the library, every test executable and
            ///       every plugin DLL (the meta_arena::instance() rule). An inline header variable
            ///       would give each module its own copy and split the id space.
            struct registry_t {
                /// @brief Guards every member below.
                std::mutex lock;

                /// @brief The next id to hand out; ids start at 1 (0 is "not assigned").
                index_t next_id = 1;

                /// @brief Identity name -> first interface (+ the id assigned to that name).
                /// @note The map owns the key string, so the identity survives the declaring module.
                std::map<std::string, name_entry_t, std::less<>> by_name;

                /// @brief Extra name -> PRIMARY name of the type it answers to.
                /// @note Stores the primary NAME, not an interface pointer: like name_entry_t's id,
                ///       the entry then survives the unload of the module that registered the type.
                ///       interface_by_name() resolves the primary name through by_name.
                std::map<std::string, std::string, std::less<>> aliases;

                /// @brief (source id, target id) -> registered converter.
                std::map<conversion_key, metatype_conversion> conversions;
            };

            /// @brief The one registry instance (function-local static: safe during static init).
            /// @note Deliberately never destroyed (the meta_arena::instance() policy): registration
            ///       runs during static initialisation and a late static destructor may still query
            ///       the table, so the one instance must outlive every static object that can reach
            ///       it. C++11 magic statics make the first call thread safe.
            registry_t& instance() {
                static registry_t* state = new registry_t();
                return *state;
            }

            /// @brief Assign @p iface its runtime id, deduplicated by NAME across module copies
            ///        (lock held).
            /// @return The id (>= 1), or 0 when the interface / name is unusable.
            /// @note The dedup is by NAME: a second module's copy of the same interface arrives with
            ///       id 0 and leaves with the id the name was assigned (Qt's registerCustomType).
            /// @note First use assigns the id; the name entry keeps it by value so a later copy can
            ///       adopt it without reading a possibly-unloaded module's interface object.
            index_t assign_id_locked(registry_t& state, const type_interface_t* iface) {
                if (iface == nullptr) {
                    return 0;
                }
                if (const index_t already = iface->id.load(std::memory_order_relaxed); already != 0) {
                    return already;
                }
                const std::string_view name = interface_name(iface);
                if (name.empty()) {
                    return 0;
                }
                const auto it = state.by_name.find(name);
                if (it != state.by_name.end()) {
                    if (it->second.id == 0) {
                        it->second.id = state.next_id++;
                    }
                    iface->id.store(it->second.id, std::memory_order_relaxed);
                    return it->second.id;
                }
                // Defensive: the registry has never seen this name. Register it and assign the id in
                // one step, so a by-type handle is always findable by name afterwards.
                const index_t id = state.next_id++;
                iface->id.store(id, std::memory_order_relaxed);
                state.by_name.emplace(std::string(name), name_entry_t{ id, iface });
                return id;
            }
        }

        HWSHQTB_EXPORT index_t ensure_registered(const type_interface_t* iface) noexcept {
            if (iface == nullptr) {
                return 0;
            }
            if (const index_t already = iface->id.load(std::memory_order_relaxed); already != 0) {
                return already;
            }
            try {
                registry_t& state = instance();
                std::lock_guard<std::mutex> guard(state.lock);
                return assign_id_locked(state, iface);
            }
            catch (...) {
                return 0;
            }
        }

        HWSHQTB_EXPORT bool bind_alias(const type_interface_t* iface, std::string_view alias) noexcept {
            if (iface == nullptr || alias.empty()) {
                return false;
            }
            const std::string_view primary_name = interface_name(iface);
            if (primary_name.empty()) {
                return false;
            }
            // Register the primary name first: the alias table stores that NAME, and a later lookup
            // has to find it in by_name (a by-type handle assigns the id on first use anyway).
            if (ensure_registered(iface) == 0) {
                return false;
            }
            if (alias == primary_name) {
                return true; // aliasing a type to its own name binds nothing extra
            }
            try {
                registry_t& state = instance();
                std::lock_guard<std::mutex> guard(state.lock);
                // A primary name always wins: an alias must not shadow another type.
                if (state.by_name.find(alias) != state.by_name.end()) {
                    HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING,
                        "meta_type_t::register_type: '" + std::string(alias)
                            + "' is already a registered type name; the alias was not bound");
                    return false;
                }
                const auto existing = state.aliases.find(alias);
                if (existing != state.aliases.end()) {
                    if (existing->second == primary_name) {
                        return true; // idempotent: the same pair again binds nothing new
                    }
                    HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING,
                        "meta_type_t::register_type: alias '" + std::string(alias) + "' is already bound to '"
                            + existing->second + "'; the first binding is kept");
                    return false;
                }
                state.aliases.emplace(std::string(alias), std::string(primary_name));
                return true;
            }
            catch (...) {
                return false;
            }
        }

        HWSHQTB_EXPORT const type_interface_t* interface_by_name(std::string_view name) noexcept {
            if (name.empty()) {
                return nullptr;
            }
            try {
                registry_t& state = instance();
                std::lock_guard<std::mutex> guard(state.lock);
                // Primary names first: an alias never shadows a registered type's own name.
                if (const auto it = state.by_name.find(name); it != state.by_name.end()) {
                    return it->second.iface;
                }
                const auto alias = state.aliases.find(name);
                if (alias == state.aliases.end()) {
                    return nullptr;
                }
                const auto primary = state.by_name.find(alias->second);
                return (primary != state.by_name.end()) ? primary->second.iface : nullptr;
            }
            catch (...) {
                return nullptr;
            }
        }

        HWSHQTB_EXPORT bool add_conversion(const type_interface_t* from, const type_interface_t* to,
                                           metatype_conversion caller) noexcept {
            if (from == nullptr || to == nullptr || caller == nullptr) {
                return false;
            }
            const index_t from_id = ensure_registered(from);
            const index_t to_id = ensure_registered(to);
            if (from_id == 0 || to_id == 0) {
                return false;
            }
            try {
                registry_t& state = instance();
                std::lock_guard<std::mutex> guard(state.lock);
                const conversion_key key{ from_id, to_id };
                if (state.conversions.find(key) != state.conversions.end()) {
                    return false; // the conversion exists already
                }
                state.conversions.emplace(key, caller);
                return true;
            }
            catch (...) {
                return false;
            }
        }

        HWSHQTB_EXPORT metatype_conversion find_conversion(const type_interface_t* from,
                                                           const type_interface_t* to) noexcept {
            if (from == nullptr || to == nullptr) {
                return nullptr;
            }
            const index_t from_id = ensure_registered(from);
            const index_t to_id = ensure_registered(to);
            if (from_id == 0 || to_id == 0) {
                return nullptr;
            }
            try {
                registry_t& state = instance();
                std::lock_guard<std::mutex> guard(state.lock);
                const auto it = state.conversions.find(conversion_key{ from_id, to_id });
                return (it != state.conversions.end()) ? it->second : nullptr;
            }
            catch (...) {
                return nullptr;
            }
        }

        /// @brief Register the builtin type NAMES in the one process-wide registry.
        ///
        /// The qRegisterMetaType equivalent for the library's own primitives: a run-time table
        /// (meta_object_builder_t) can only resolve a type by name if some interface carrying that
        /// name has been used before, and these names can never come from a reflection.
        ///
        /// @note Runs from a STATIC INITIALISER in this translation unit (see
        ///       default_type_registrar_t below), so a by-name lookup answers before main() and
        ///       without a core_application object (Qt's QMetaType::fromName works without a
        ///       QCoreApplication for the same reason).
        /// @note The ORDER is fixed (void first): ids are handed out on first registration, so
        ///       keeping the order keeps the id a process assigns to a builtin stable.
        /// @note Idempotent: meta_type_t::register_type<T>() is an atomic load after the first call, so a second
        ///       application instance (a test that creates one per case) costs nothing.
        HWSHQTB_EXPORT void register_default_types() noexcept {
            (void)meta_type_t::register_type<void>();
            (void)meta_type_t::register_type<bool>();
            (void)meta_type_t::register_type<char>();
            (void)meta_type_t::register_type<signed char>();
            (void)meta_type_t::register_type<unsigned char>();
            (void)meta_type_t::register_type<short>();
            (void)meta_type_t::register_type<unsigned short>();
            (void)meta_type_t::register_type<int>();
            (void)meta_type_t::register_type<unsigned int>();
            (void)meta_type_t::register_type<long>();
            (void)meta_type_t::register_type<unsigned long>();
            (void)meta_type_t::register_type<long long>();
            (void)meta_type_t::register_type<unsigned long long>();
            (void)meta_type_t::register_type<float>();
            (void)meta_type_t::register_type<double>();
            (void)meta_type_t::register_type<long double>();
            (void)meta_type_t::register_type<const char*>();
            (void)meta_type_t::register_type<std::basic_string<char>>();
            (void)meta_type_t::register_type<std::basic_string_view<char>>();
            // The remaining primitive spellings are APPENDED: ids are handed out in registration
            // order, and inserting them in the middle would shift every later builtin's id.
            (void)meta_type_t::register_type<wchar_t>();
            (void)meta_type_t::register_type<char8_t>();
            (void)meta_type_t::register_type<char16_t>();
            (void)meta_type_t::register_type<char32_t>();
            (void)meta_type_t::register_type<std::nullptr_t>();
            // The standard-library typedef names, so a by-name lookup answers them without the user
            // writing a registration first. This is the same mechanism as a user alias — the second
            // argument of meta_type_t::register_type<T>(alias) — only registered by default. They are
            // APPENDED for the same reason as the primitives above: ids follow registration order.
            (void)meta_type_t::register_type<std::basic_string<char>>("std::string");
            (void)meta_type_t::register_type<std::basic_string_view<char>>("std::string_view");
            (void)meta_type_t::register_type<std::basic_string<wchar_t>>("std::wstring");
        }

        namespace {
            /// @brief Static-initialisation trigger for the default registrations.
            ///
            /// A namespace-scope object in the registry's OWN translation unit: its constructor runs
            /// while the module is loaded, before main() of any executable that links the library, so
            /// resolve_global("std::string") / resolve_global("int") answer with no core_application.
            ///
            /// @note Order safety, and why there is NO namespace-scope registry object: the registry
            ///       state lives in instance()'s function-local static, so it is created on first use.
            ///       This object is the only namespace-scope state here, and everything it calls
            ///       (ensure_registered / bind_alias) funnels through instance(): whether this
            ///       initialiser, core_application's constructor or another static initialiser reaches
            ///       the registry first, the registry is constructed at that first call. The interfaces
            ///       themselves are `constinit` (detail::interface_of<T>), so their storage and names
            ///       are already valid during static initialisation — only their mutable id atomic is
            ///       written, under the registry's lock.
            struct default_type_registrar_t {
                default_type_registrar_t() {
                    register_default_types();
                }
            };

            /// @note The side effect IS the point: it is never read after construction.
            [[maybe_unused]] const default_type_registrar_t g_default_type_registrar{};
        }
    }
}
