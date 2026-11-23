/// @file meta_object_t.cpp
/// @brief Implementation of the run-time meta-object facade.
///
/// meta_object_t owns its meta_info_t (the tables) by value and points at the base class's
/// meta-object through super_class(); this file only walks them. Every query follows the same rules:
///  - indices are GLOBAL and the chain is laid out parents first: count_*() adds every base class's
///    table, and *(index) walks down the chain to find the meta-object holding that index;
///  - that walk is an OBJECT walk — `for (const meta_object_t* o = this; o != nullptr;
///    o = o->super_class())` — because every link of the chain is a real meta-object, never a copy
///    of a table;
///  - a perfect-hash hit is always verified against the stored key, because the hash can map a
///    missing key onto an occupied bucket.
///
/// The row accessors (property_entry, ...) answer defensively for an out-of-range slot, because a
/// handle can carry index_npos.

#include <hwshqtb/metaobject/meta_object_t.h>

#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/log.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwshqtb::metaobject {
    namespace detail {
        /// @brief The argument identities an erased argument list carries, in call order.
        /// @param args One any_t per argument.
        /// @return Each boxed value's (bare type, cvref) identity.
        /// @note Exported because the erased entry points are templates: a consumer that instantiates
        ///       one calls this definition through the DLL, exactly like the registry helpers in
        ///       type_registry.cpp.
        HWSHQTB_EXPORT std::vector<cvref_meta_type_t> boxed_identities(std::span<const any_t> args) {
            std::vector<cvref_meta_type_t> identities;
            identities.reserve(args.size());
            for (const any_t& arg : args) {
                identities.push_back(cvref_meta_type_t::from(arg));
            }
            return identities;
        }
    }

    namespace {
        /// @brief Look @p key up in one class's own perfect-hash table.
        /// @param table The class's own key -> slot table.
        /// @param entries The class's own entry table (the table the slots index into).
        /// @param key The key to look for.
        /// @param key_of Extracts the stored key of one entry.
        /// @return The local slot when the stored key equals @p key; index_npos otherwise.
        /// @note The hash may map a missing key onto an occupied bucket, so the stored key is
        ///       compared after the lookup; every table query in the library uses the same guard.
        template <typename Entry, typename KeyOf>
        index_t find_slot(const detail::hashtable_t& table,
                          std::span<const Entry> entries,
                          std::string_view key,
                          const KeyOf& key_of) noexcept {
            const std::uint64_t slot = table.find(key);
            if (slot == static_cast<std::uint64_t>(-1)
                || static_cast<std::size_t>(slot) >= entries.size()) {
                return index_npos;
            }
            const Entry& entry = entries[static_cast<std::size_t>(slot)];
            return (key_of(entry) == key) ? static_cast<index_t>(slot) : index_npos;
        }

        /// @brief Local slot of a normalised signature in one class's own function table.
        /// @param table One class's meta-info table.
        /// @param signature The normalised signature.
        /// @param signals_only true to treat a non-signal entry as a miss.
        /// @return The local slot, or index_npos.
        index_t find_method(const meta_info_t& table, std::string_view signature,
                            bool signals_only) noexcept {
            const index_t slot = static_cast<index_t>(table.find_method(signature));
            if (slot == index_npos || !signals_only) {
                return slot;
            }
            return table.method_at(static_cast<std::size_t>(slot)).kind == method_kind_enum::SIGNAL
                ? slot
                : index_npos;
        }

        /// @brief Local slot of a name in one class's own property table.
        /// @return The local slot, or index_npos.
        index_t find_property(const meta_info_t& table, std::string_view name) noexcept {
            return static_cast<index_t>(table.find_property(name));
        }

        /// @brief Reconstruct the signature an any_t argument list describes, for diagnostics.
        /// @param scope The class name the construction was attempted on.
        /// @param args The argument list that matched nothing.
        /// @return "<class>(<name>,<name>)", built from the argument identity names — the same bare
        ///         spellings the constructor table uses for by-value parameters, so the message points
        ///         at the signature a reader would look for.
        std::string described_signature(std::string_view scope, std::span<const any_t> args) {
            std::string result(scope);
            result += '(';
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (i > 0) {
                    result += ',';
                }
                result += args[i].type().name();
            }
            result += ')';
            return result;
        }

        /// @brief Constructor slot of a normalised signature in one class's own constructor table.
        /// @param table One class's meta-info table.
        /// @param signature The normalised signature, of the method form "class_t(int,int)".
        /// @return The slot inside constructor_infos — which IS the constructor coordinate — or index_npos
        ///         when the signature names no constructor of this class.
        /// @note Same guard as every other table query: the perfect hash may map a missing key onto an
        ///       occupied bucket, so find_slot() compares the stored signature after the lookup. No
        ///       kind test is needed any more: constructor_infos holds constructors only, so the table
        ///       self-describes.
        index_t find_constructor(const meta_info_t& table, std::string_view signature) noexcept {
            return static_cast<index_t>(table.find_constructor(signature));
        }

        /// @brief Local slot of a name in one class's own enumeration table.
        /// @return The local slot, or index_npos.
        index_t find_enumeration(const meta_info_t& table, std::string_view name) noexcept {
            return static_cast<index_t>(table.find_enum(name));
        }

        /// @brief Local slot of a key in one class's own class-info table.
        /// @return The local slot, or index_npos.
        index_t find_class_info(const meta_info_t& table, std::string_view name) noexcept {
            return static_cast<index_t>(table.find_class_info(name));
        }

        /// @brief Local slot of an IID in one class's own interface table.
        /// @return The local slot, or index_npos.
        index_t find_interface(const meta_info_t& table, std::string_view iid) noexcept {
            return static_cast<index_t>(table.find_interface(iid));
        }

        /// @brief The number of method_infos entries of one class's own table.
        std::size_t func_count_of(const meta_info_t& table) noexcept {
            return table.method_count();
        }

        /// @brief The text between the parentheses of a normalised signature.
        /// @param signature A signature such as "changed(int,double)".
        /// @return The parameter list ("int,double"); empty when the signature is not parenthesised.
        std::string_view parameter_list(std::string_view signature) noexcept {
            const std::size_t open = signature.find('(');
            if (open == std::string_view::npos) {
                return {};
            }
            const std::size_t close = signature.rfind(')');
            if (close == std::string_view::npos || close < open) {
                return {};
            }
            return signature.substr(open + 1, close - open - 1);
        }

        /// @brief Whether a signature carries a "(...)" parameter list at all.
        bool has_parameter_list(std::string_view signature) noexcept {
            const std::size_t open = signature.find('(');
            if (open == std::string_view::npos) {
                return false;
            }
            const std::size_t close = signature.rfind(')');
            return close != std::string_view::npos && close > open;
        }
    }

    // ---- chain helpers (private static members: they read the private table) ----

    template <typename Local, typename Size>
    index_t meta_object_t::global_index(const meta_object_t* object, const Local& local, const Size& size) noexcept {
        if (object == nullptr) {
            return index_npos;
        }
        // Own table first.
        const index_t slot = local(object->info());
        if (slot != index_npos) {
            index_t offset = 0;
            for (const meta_object_t* base = object->super_class(); base != nullptr; base = base->super_class()) {
                offset += static_cast<index_t>(size(base->info()));
            }
            return offset + slot;
        }
        // Only then the ancestor.
        return global_index(object->super_class(), local, size);
    }

    template <typename Size>
    std::pair<const meta_object_t*, index_t> meta_object_t::resolve_index(const meta_object_t* object, index_t index,
                                                                          const Size& size) noexcept {
        if (object == nullptr) {
            return { nullptr, index_npos };
        }
        index_t parent_count = 0;
        for (const meta_object_t* base = object->super_class(); base != nullptr; base = base->super_class()) {
            parent_count += static_cast<index_t>(size(base->info()));
        }
        if (index < parent_count) {
            return resolve_index(object->super_class(), index, size);
        }
        const index_t local = index - parent_count;
        if (local >= static_cast<index_t>(size(object->info()))) {
            return { nullptr, index_npos };
        }
        return { object, local };
    }

    // The constructors and info() are inline constexpr in the header: they must be usable in a
    // constant expression to constant-initialise a class's static_meta_object.

    // ---- identity ----

    std::string_view meta_object_t::class_name() const noexcept {
        return static_cast<std::string_view>(info().class_name);
    }

    std::string_view meta_object_t::qualified_class_name() const noexcept {
        return static_cast<std::string_view>(info().qualified_name);
    }

    const meta_object_t* meta_object_t::super_class() const noexcept {
        // The whole contract: the base class's own meta-object, or nullptr. No facade is built and
        // nothing is copied — meta_object_t is non-copyable, and every chain link is a real
        // meta-object (the base's static_meta_object, or an arena entry).
        return _super;
    }

    bool meta_object_t::inherits(const meta_object_t* base) const noexcept {
        // A null base is answered false instead of being dereferenced. The walk compares the tables
        // (by-value owned, so table identity is meta-object identity).
        if (base == nullptr) {
            return false;
        }
        for (const meta_object_t* object = this; object != nullptr; object = object->super_class()) {
            if (&object->info() == &base->info()) {
                return true;
            }
        }
        return false;
    }

    bool meta_object_t::inherits(std::string_view class_name) const noexcept {
        // The QUALIFIED name first: it is the unique one, so `inherits("my::ui::widget_t")` answers
        // for exactly that class. The bare identifier is still accepted, because every type declared
        // at global scope answers the same spelling for both and callers rely on it; when two
        // namespaces declare the same identifier, the qualified spelling is the one that decides.
        for (const meta_object_t* object = this; object != nullptr; object = object->super_class()) {
            if (static_cast<std::string_view>(object->info().qualified_name) == class_name) {
                return true;
            }
        }
        for (const meta_object_t* object = this; object != nullptr; object = object->super_class()) {
            if (static_cast<std::string_view>(object->info().class_name) == class_name) {
                return true;
            }
        }
        return false;
    }

    // ---- methods ----

    std::size_t meta_object_t::method_count() const noexcept {
        std::size_t total = 0;
        for (const meta_object_t* object = this; object != nullptr; object = object->super_class()) {
            total += object->info().method_count();
        }
        return total;
    }

    meta_method_t meta_object_t::method(std::string_view signature) const {
        const index_t index = index_of_method(signature);
        return (index == index_npos) ? meta_method_t{} : method(index);
    }

    meta_method_t meta_object_t::signal(std::string_view signature) const {
        const index_t index = index_of_signal(signature);
        return (index == index_npos) ? meta_method_t{} : method(index);
    }

    meta_method_t meta_object_t::method(index_t index) const noexcept {
        const auto [owner, local] = resolve_index(this, index, func_count_of);
        return (owner == nullptr) ? meta_method_t{} : meta_method_t(owner, local);
    }

    index_t meta_object_t::index_of_method(std::string_view signature) const noexcept {
        return global_index(this, [signature](const meta_info_t& table) noexcept {
            return find_method(table, signature, false);
        },
            func_count_of);
    }

    index_t meta_object_t::index_of_signal(std::string_view signature) const noexcept {
        return global_index(this, [signature](const meta_info_t& table) noexcept {
            return find_method(table, signature, true);
        },
            func_count_of);
    }

    // ---- constructors ----

    std::size_t meta_object_t::constructor_count() const noexcept {
        // Qt's model: the constructor array is per class and constructors are not inherited
        // (QMetaObject::constructorCount() answers for THIS class), so the count is this class's own
        // constructor table — no chain walk here, unlike method_count().
        return _info.constructor_count();
    }

    meta_constructor_t meta_object_t::constructor(index_t index) const noexcept {
        // The constructor space IS the constructor_infos slot, so the coordinate needs no translation and
        // there is no resolve_index() walk: constructors are never inherited.
        if (index == index_npos || index >= _info.constructor_count()) {
            return {};
        }
        return meta_constructor_t(this, index);
    }

    index_t meta_object_t::index_of_constructor(std::string_view signature) const noexcept {
        // Own constructor space only, and the answer is directly usable with constructor(index).
        return find_constructor(_info, signature);
    }

    bool meta_object_t::construct_into(std::string_view signature, std::span<const any_t> args,
                                       void* out_slot) const {
        if (out_slot == nullptr) {
            return false;
        }
        // ---- 1) Resolve the constructor entry ------------------------------------------------
        // Both paths read THIS meta-object's own CONSTRUCTOR table (constructor_infos): constructors are not
        // inherited, so a base entry would build a base instance, and no method row is ever a
        // candidate. The signature path addresses the constructor by its coordinate in this class's
        // own constructor space.
        const detail::constructor_info_t* entry = nullptr;
        if (!signature.empty()) {
            const index_t slot = index_of_constructor(signature);
            if (slot == index_npos) {
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::WARNING,
                    "meta_object_t::new_instance: no constructor '" + std::string(signature)
                    + "' in " + std::string(class_name()));
                return false;
            }
            entry = &_info.constructor_at(static_cast<std::size_t>(slot));
        }
        else {
            // Two passes: the exact reference-kind pass first (an lvalue argument selects
            // T(const T&), an rvalue selects T(T&&)), then the accommodating pass in which
            // check_param_qualifier() decides. Within a pass the scan follows the stored table, which
            // the perfect hash permuted into slot order. That order needs no rule of its own: two
            // constructors that both match the same argument list are ambiguous in C++ itself, so
            // neither gets a thunk and the scan can never pick a usable wrong one.
            //
            // The boxed arguments are re-expressed ONCE, here: the table's matcher takes (type, cvref)
            // identities and never sees an any_t (that value type belongs to the erased channel; the
            // method path's bridge is meta_method_t::method_matches_boxed). A CONSTRUCTOR has no
            // meta_method_t handle — its rows live in constructor_infos, not method_infos — so this scan goes
            // through the one runtime rule at the level it is addressed at: (column, slot), with no
            // table row in hand.
            bool all_initialized = true;
            for (const any_t& arg : args) {
                if (!arg.is_initialized()) {
                    all_initialized = false;
                    break;
                }
            }
            // A declared-only argument has no value to hand the constructor thunk, so it matches
            // nothing — the same refusal method_matches_boxed() makes on the method path.
            const std::vector<cvref_meta_type_t> identities =
                all_initialized ? detail::boxed_identities(args) : std::vector<cvref_meta_type_t>{};
            if (all_initialized) {
                for (const bool exact : { true, false }) {
                    for (std::size_t slot = 0; slot < _info.constructor_count(); ++slot) {
                        // No object exists yet: a constructor builds one, so it has no receiver cvref
                        // to satisfy. Constructor rows carry no cvref either, and qualifier_flag::NONE
                        // is the honest "there is no object here" (the cvref block imposes nothing).
                        if (_info.constructor_matches(slot, identities, exact,
                                qualifier_flag::NONE)) {
                            entry = &_info.constructor_at(slot);
                            break;
                        }
                    }
                    if (entry != nullptr) {
                        break;
                    }
                }
            }
            if (entry == nullptr) {
                // Nothing matched: the caller may simply have named the wrong types. The described
                // signature is built from the argument identity names, so the diagnostic shows what
                // was looked for.
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::WARNING,
                    "meta_object_t::new_instance: no constructor '"
                    + described_signature(class_name(), args) + "' in " + std::string(class_name()));
                return false;
            }
        }
        // From here on the entry is known, so the diagnostics use its DECLARED signature (the
        // caller-facing text a reader can look up in the table) rather than a reconstruction.
        const std::string declared(entry->signature);
        // A constructor the class declares but cannot call is in the table on purpose: the table
        // describes the class, not what happens to be callable. The verdict is the entry's caller —
        // null for a constructor whose `new T(args...)` does not compile even with full access (a
        // deleted one, or any constructor of an abstract type) — and that null is reported here,
        // BEFORE anything is built, so the non-callable path allocates nothing.
        if (entry->caller == nullptr) {
            HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL,
                "meta_object_t::new_instance: constructor '" + declared
                + "' is not callable (deleted, abstract, or not selectable unambiguously from its own"
                  " parameter types)");
            return false;
        }
        // The signature path carries no argument count, so the one check available there is that the
        // caller supplied an argument list when the constructor takes arguments. The any_t path
        // already matched arity during resolution.
        const std::span<const detail::compiling_string_t> param_types = entry->param_types;
        if (!signature.empty() && !param_types.empty() && args.empty()) {
            HWSHQTB_LOG_RAW(log_connection(), log_level_enum::WARNING,
                "meta_object_t::new_instance: '" + declared + "' needs "
                + std::to_string(param_types.size()) + " argument(s), but none were supplied");
            return false;
        }
        // ---- 2) Run the thunk -----------------------------------------------------------------
        // The constructor's caller receives a null object and writes the new instance's ADDRESS
        // through params[0]: the caller's own `void*` slot (out_slot), reached through the any_t
        // alias for the public entry point. params[1...] hold the argument addresses, exactly like
        // meta_method_t's generic_call convention.
        std::vector<void*> params(args.size() + 1, nullptr);
        params[0] = out_slot;
        for (std::size_t i = 0; i < args.size(); ++i) {
            params[i + 1] = const_cast<void*>(static_cast<const void*>(&args[i]));
        }
        try {
            entry->caller(nullptr, params.data());
        }
        catch (...) {
            // The constructor thunk already logged the Critical with the exception text (and `new`
            // released the storage it had allocated), so nothing leaks; report the failed creation.
            HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL,
                "meta_object_t::new_instance: construction through '" + declared
                + "' threw; no object was created");
            return false;
        }
        return true;
    }

    bool meta_object_t::new_instance(std::span<const any_t> args, any_t& out) const {
        // The out slot must be an ALIAS (`std::ref(pointer_variable)`): the constructor thunk writes
        // the new instance's address THROUGH the slot, so the slot has to name the caller's own
        // variable, and that variable has to be a pointer to be able to hold a `void*`.
        // any_t::operator&() answers the aliased variable's address; an owning or declared-only any_t
        // owns storage of its own and would silently swallow the write.
        const bool is_alias = !out._owns && (out.qualifier() & qualifier_flag::LVALUE_REF);
        if (!is_alias || !(out.type().tag() & type_tag_flag::IS_POINTER)) {
            throw bad_any_unsupported_t{
                "new_instance: the out slot must be an alias (std::ref) to a pointer variable"
            };
        }
        for (const any_t& arg : args) {
            if (!arg.is_initialized()) {
                throw bad_any_uninitialized_t{};
            }
        }
        // any_t::operator&() on an alias: the aliased pointer variable's address.
        void* const out_slot = &out;
        return construct_into({}, args, out_slot);
    }

    // ---- properties ----

    std::size_t meta_object_t::property_count() const noexcept {
        std::size_t total = 0;
        for (const meta_object_t* object = this; object != nullptr; object = object->super_class()) {
            total += object->info().property_count();
        }
        return total;
    }

    meta_property_t meta_object_t::property(std::string_view name) const {
        const index_t index = index_of_property(name);
        return (index == index_npos) ? meta_property_t{} : property(index);
    }

    meta_property_t meta_object_t::property(index_t index) const noexcept {
        const auto [owner, local] = resolve_index(this, index,
            [](const meta_info_t& scope) noexcept {
                return scope.property_count();
            });
        return (owner == nullptr) ? meta_property_t{} : meta_property_t(owner, local);
    }

    meta_property_t meta_object_t::user_property() const {
        // Most derived class first, like QMetaObject::userProperty: a derived class may declare a
        // User property of its own even when a base class already declares one. The compile time
        // path guarantees at most one User property per class, so the first hit is the only candidate.
        for (const meta_object_t* object = this; object != nullptr; object = object->super_class()) {
            const meta_info_t& table = object->info();
            for (std::size_t i = 0; i < table.property_count(); ++i) {
                if (table.property_at(i).traits & property_trait_flag::USER) {
                    return meta_property_t(object, static_cast<index_t>(i));
                }
            }
        }
        return {};
    }

    index_t meta_object_t::index_of_property(std::string_view name) const noexcept {
        return global_index(this, [name](const meta_info_t& table) noexcept {
            return find_property(table, name);
        },
            [](const meta_info_t& table) noexcept {
                return table.property_count();
            });
    }

    const detail::property_info_t* meta_object_t::property_entry(index_t index) const noexcept {
        if (index == index_npos) {
            return nullptr;
        }
        const meta_info_t& table = info();
        if (index >= table.property_count()) {
            return nullptr;
        }
        return &table.property_at(static_cast<std::size_t>(index));
    }

    // ---- enumerations ----

    std::size_t meta_object_t::enumeration_count() const noexcept {
        std::size_t total = 0;
        for (const meta_object_t* object = this; object != nullptr; object = object->super_class()) {
            total += object->info().enum_count();
        }
        return total;
    }

    meta_enum_t meta_object_t::enumeration(std::string_view name) const {
        return enumeration_in(this, name);
    }

    meta_enum_t meta_object_t::enumeration_in(const meta_object_t* table,
                                                             std::string_view name) noexcept {
        if (table == nullptr) {
            return {};
        }
        // Own table first, then the parents, exactly like global_index(): a derived-class entry
        // shadows a same-named base entry, so the two entry points agree. The returned handle points
        // at the DECLARING meta-object inside the chain (never at a copy), so it stays valid for as
        // long as that meta-object does — which is what lets meta_property_t::enumerator() resolve
        // without a facade.
        const index_t slot = find_enumeration(table->info(), name);
        if (slot != index_npos) {
            return meta_enum_t(table, slot);
        }
        return enumeration_in(table->super_class(), name);
    }

    meta_enum_t meta_object_t::enumeration(index_t index) const noexcept {
        const auto [owner, local] = resolve_index(this, index,
            [](const meta_info_t& scope) noexcept {
                return scope.enum_count();
            });
        return (owner == nullptr) ? meta_enum_t{} : meta_enum_t(owner, local);
    }

    index_t meta_object_t::index_of_enumeration(std::string_view name) const noexcept {
        return global_index(this, [name](const meta_info_t& table) noexcept {
            return find_enumeration(table, name);
        },
            [](const meta_info_t& table) noexcept {
                return table.enum_count();
            });
    }

    // ---- class info ----

    std::size_t meta_object_t::class_info_count() const noexcept {
        std::size_t total = 0;
        for (const meta_object_t* object = this; object != nullptr; object = object->super_class()) {
            total += object->info().class_info_count();
        }
        return total;
    }

    meta_class_info_t meta_object_t::class_info(std::string_view name) const {
        const index_t index = index_of_class_info(name);
        return (index == index_npos) ? meta_class_info_t{} : class_info(index);
    }

    meta_class_info_t meta_object_t::class_info(index_t index) const noexcept {
        const auto [owner, local] = resolve_index(this, index,
            [](const meta_info_t& scope) noexcept {
                return scope.class_info_count();
            });
        return (owner == nullptr) ? meta_class_info_t{} : meta_class_info_t(owner, local);
    }

    index_t meta_object_t::index_of_class_info(std::string_view name) const noexcept {
        return global_index(this, [name](const meta_info_t& table) noexcept {
            return find_class_info(table, name);
        },
            [](const meta_info_t& table) noexcept {
                return table.class_info_count();
            });
    }

    // ---- interfaces ----

    std::size_t meta_object_t::interface_count() const noexcept {
        std::size_t total = 0;
        for (const meta_object_t* object = this; object != nullptr; object = object->super_class()) {
            total += object->info().interface_count();
        }
        return total;
    }

    std::string_view meta_object_t::interface_iid(index_t index) const noexcept {
        // Chain-global decode, the mirror of interface_count(): the ancestors' rows come first. The
        // walk subtracts one table at a time instead of summing first, so the count and the accessor
        // cannot drift apart.
        if (index == index_npos) {
            return { };
        }
        index_t remaining = index;
        for (const meta_object_t* object = this; object != nullptr; object = object->super_class()) {
            const std::size_t size = object->info().interface_count();
            if (remaining < static_cast<index_t>(size)) {
                return static_cast<std::string_view>(
                    object->info().interface_at(static_cast<std::size_t>(remaining)).iid);
            }
            remaining -= static_cast<index_t>(size);
        }
        return { };
    }

    index_t meta_object_t::index_of_interface(std::string_view iid) const noexcept {
        // Own table first: a derived class's entry shadows a same-IID base entry, the same rule
        // method() and signal() follow. The offset is the ancestors' row count, which keeps the
        // answer in the space interface_iid() decodes.
        index_t offset = 0;
        for (const meta_object_t* base = super_class(); base != nullptr; base = base->super_class()) {
            offset += static_cast<index_t>(base->info().interface_count());
        }
        for (const meta_object_t* object = this; object != nullptr; object = object->super_class()) {
            const index_t slot = find_interface(object->info(), iid);
            if (slot != index_npos) {
                return offset + slot;
            }
            if (object->super_class() != nullptr) {
                offset -= static_cast<index_t>(object->super_class()->info().interface_count());
            }
        }
        return index_npos;
    }

    bool meta_object_t::implements(std::string_view iid) const noexcept {
        // Any hit in the chain answers true; the walk direction does not matter here.
        for (const meta_object_t* object = this; object != nullptr; object = object->super_class()) {
            if (find_interface(object->info(), iid) != index_npos) {
                return true;
            }
        }
        return false;
    }

    void* meta_object_t::cast(void* object, std::string_view iid) const noexcept {
        if (object == nullptr) {
            return nullptr;
        }
        // Parents first: a base-class entry precedes a same-IID entry of a derived class. Walking
        // from the most derived class up and keeping the LAST hit lands on the root-most
        // declaration — the entry cast() and implements() agree on.
        const meta_object_t* owner = nullptr;
        for (const meta_object_t* object = this; object != nullptr; object = object->super_class()) {
            if (find_interface(object->info(), iid) != index_npos) {
                owner = object;
            }
        }
        if (owner == nullptr) {
            return nullptr;
        }
        const index_t slot = find_interface(owner->info(), iid);
        if (slot == index_npos) {
            return nullptr;
        }
        // The entry belongs to the DECLARING class, so its thunk down-casts through that class: for
        // an inherited entry that is a base of the object's dynamic type, which is an ordinary
        // upcast — still valid, and still a static (compile-time) conversion.
        // The thunk takes a void* (so a GADGET can carry interface entries too); for an object_t
        // class this erasure is the object's address unchanged, because object_t is the FIRST base —
        // that is exactly the pointer static_cast<T*> then recovers inside the thunk. The caller
        // (the namespace-level cast<T>) hands that address in already erased.
        const detail::interface_entry_t& entry = owner->info().interface_at(static_cast<std::size_t>(slot));
        return (entry.cast == nullptr) ? nullptr : entry.cast(object);
    }

    // ---- static ----

    void meta_object_t::connect_slots_by_name(object_t* root) {
        if (root == nullptr) {
            return;
        }
        // Copy the child list: connecting does not re-parent, but the walk recurses and a slot could.
        const std::vector<object_t*> children(root->children().begin(), root->children().end());
        for (object_t* child : children) {
            if (child == nullptr) {
                continue;
            }
            const std::string child_name(child->object_name());
            if (!child_name.empty()) {
                const std::string prefix = "on_" + child_name + "_";
                const meta_object_t& child_meta = *child->meta_object();
                const std::size_t signal_count = child_meta.method_count();
                for (std::size_t signal_index = 0; signal_index < signal_count; ++signal_index) {
                    const meta_method_t signal_handle = child_meta.method(static_cast<index_t>(signal_index));
                    if (!signal_handle.is_valid() || !signal_handle.is_signal()) {
                        continue;
                    }
                    const std::string wanted = prefix + std::string(signal_handle.name());
                    const meta_object_t& root_meta = *root->meta_object();
                    const std::size_t slot_count = root_meta.method_count();
                    for (std::size_t slot_index = 0; slot_index < slot_count; ++slot_index) {
                        const meta_method_t slot_handle = root_meta.method(static_cast<index_t>(slot_index));
                        if (slot_handle.is_valid() && slot_handle.name() == wanted) {
                            child->connect(signal_handle, root, slot_handle);
                            break;
                        }
                    }
                }
            }
            connect_slots_by_name(child);
        }
    }

    bool meta_object_t::check_connect_args(std::string_view signal,
                                                         std::string_view method) {
        if (!has_parameter_list(signal) || !has_parameter_list(method)) {
            return false;
        }
        const std::string_view signal_params = parameter_list(signal);
        const std::string_view method_params = parameter_list(method);
        // A slot without parameters accepts any signal, and an identical parameter list is always
        // compatible (QMetaObject::checkConnectArgs).
        if (method_params.empty() || method_params == signal_params) {
            return true;
        }
        // Otherwise the slot may only drop trailing arguments: its list must be a proper prefix of
        // the signal's, ending exactly at a comma boundary.
        return method_params.size() < signal_params.size()
            && signal_params.compare(0, method_params.size(), method_params) == 0
            && signal_params[method_params.size()] == ',';
    }

    bool meta_object_t::check_connect_args(const meta_method_t& signal,
                                                         const meta_method_t& method) {
        if (!signal.is_valid() || !method.is_valid()) {
            return false;
        }
        return check_connect_args(signal.signature(), method.signature());
    }

    // ---- forwarding: normalisation needs the rewriting table ----

    std::string meta_object_t::normalized_signature(std::string_view raw) const {
        return info().normalized_signature(raw);
    }

    std::string meta_object_t::normalized_type(std::string_view type) const {
        return info().normalized_type(type);
    }

    bool meta_object_t::has_super_class() const noexcept {
        return _super != nullptr;
    }
}
