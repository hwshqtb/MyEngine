/// @file meta_object_builder_t.cpp
/// @brief The run-time meta-object builder family (QMetaObjectBuilder equivalent).

#include "hwshqtb/metaobject/meta_object_builder_t.h"

#include "hwshqtb/metaobject/chd_hash.hpp"
#include "hwshqtb/metaobject/meta_arena.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwshqtb::metaobject {
    namespace detail {
        std::string stale_row_message(std::string_view handle, std::string_view name,
                                      std::string_view removal) {
            std::string message(handle);
            message += ": the handle no longer names a row";
            if (!name.empty()) {
                message += " (was '";
                message += name;
                message += "')";
            }
            message += "; ";
            message += removal;
            message += "() shifts the rows, so take a fresh handle after a removal";
            return message;
        }
    }

    namespace {
        // ---- supplemental stable storage ------------------------------------------------------
        //
        // The arena admits a closed list of element types; the two callable row types' columns span
        // types it does not admit (param_qualifiers and annotations). These slots use the arena's own
        // layout rule — a deque of vectors, so the vector's heap block never moves while the deque
        // grows — and the same lifetime rule: never destroyed, because a built table may be read during
        // static destruction.

        /// @brief Process-wide storage for one element type the arena does not admit.
        template <typename T>
        std::deque<std::vector<T>>& supplemental_slot() {
            static std::deque<std::vector<T>>* slot = new std::deque<std::vector<T>>();
            return *slot;
        }

        /// @brief The one mutex guarding every supplemental slot.
        std::mutex& supplemental_mutex() {
            static std::mutex* mutex = new std::mutex();
            return *mutex;
        }

        /// @brief Copy @p items into process-lifetime storage and return a view of the copy.
        template <typename T>
        std::span<const T> store_array(std::span<const T> items) {
            const std::lock_guard<std::mutex> lock(supplemental_mutex());
            std::deque<std::vector<T>>& slot = supplemental_slot<T>();
            slot.emplace_back(items.begin(), items.end());
            const std::vector<T>& stored = slot.back();
            return std::span<const T>(stored.data(), stored.size());
        }

        // ---- tag values ------------------------------------------------------------------------
        //
        // A run-time tag has no reflection to staticise a value from, so it carries make_str_tag()'s
        // shape: the type name is "const char*" and the stored pointer is a staticised `const char*`.
        // The CHARACTERS live in the arena (intern()); the `const char*` OBJECT needs its own stable
        // address, which is what this deque provides.

        /// @brief Process-wide storage for the `const char*` objects a run-time tag points at.
        std::deque<const char*>& tag_value_slot() {
            static std::deque<const char*>* slot = new std::deque<const char*>();
            return *slot;
        }

        /// @brief The one mutex guarding the tag-value slot.
        std::mutex& tag_value_mutex() {
            static std::mutex* mutex = new std::mutex();
            return *mutex;
        }

        /// @brief Staticise one interned tag text and return the `const char*` object it points at.
        /// @param text The tag text, already interned, so its characters never move.
        /// @return The address of the `const char*` object holding @p text's address.
        const void* store_tag_value(std::string_view text) {
            const std::lock_guard<std::mutex> lock(tag_value_mutex());
            std::deque<const char*>& slot = tag_value_slot();
            slot.push_back(text.data());
            return static_cast<const void*>(&slot.back());
        }

        /// @brief Refuse a table whose key list holds a duplicate.
        /// @param keys Every key of one table, in row order.
        /// @param what The row kind, for the message ("method").
        /// @param key_label What the key is, for the message ("signature").
        /// @note The perfect hash is built over the key list and cannot place two equal keys, and a
        ///       duplicated key would leave one row unreachable in any case. The compile-time path
        ///       needs no check: the language guarantees the keys are unique (the signature is
        ///       derived injectively from the declaration, so two members of one class cannot
        ///       normalise to the same signature), whereas a run-time table's keys come from user
        ///       text, where duplicates are possible, so it must reject them here.
        void reject_duplicate_keys(std::vector<std::string_view> keys, const char* what, const char* key_label) {
            std::ranges::sort(keys);
            const auto duplicate = std::ranges::adjacent_find(keys);
            if (duplicate == keys.end()) {
                return;
            }
            throw std::invalid_argument(std::string("meta_object_builder_t: two ") + what
                + " rows share the " + key_label + " '" + std::string(*duplicate)
                + "'; the " + key_label + " is the table key");
        }

        /// @brief The message every perfect-hash failure carries.
        std::string perfect_hash_message(const char* what, std::size_t keys) {
            return "meta_object_builder_t: could not build a perfect hash for the " + std::string(what)
                + " (keys=" + std::to_string(keys)
                + ", attempts=" + std::to_string(detail::chd_hash_max_attempts) + ")";
        }
    }

    meta_object_builder_t::meta_object_builder_t(std::string_view class_name, const meta_object_t* super):
        _class_name(class_name),
        _super(super) { }

    std::string_view meta_object_builder_t::class_name() const noexcept {
        return _class_name;
    }

    const meta_object_t* meta_object_builder_t::super_class() const noexcept {
        return _super;
    }

    // ---- adding members --------------------------------------------------------------------

    meta_method_builder_t meta_object_builder_t::add_method(std::string_view signature) {
        _methods.push_back(parse_method(signature, method_kind_enum::PLAIN_METHOD));
        return meta_method_builder_t(this, static_cast<index_t>(_methods.size() - 1));
    }

    meta_method_builder_t meta_object_builder_t::add_signal(std::string_view signature) {
        _methods.push_back(parse_method(signature, method_kind_enum::SIGNAL));
        return meta_method_builder_t(this, static_cast<index_t>(_methods.size() - 1));
    }

    meta_method_builder_t meta_object_builder_t::add_slot(std::string_view signature) {
        // A slot IS a method here: the library has no separate slot kind, because any reflected
        // method can be the target of a connection.
        return add_method(signature);
    }

    meta_method_builder_t meta_object_builder_t::add_constructor(std::string_view signature) {
        // The CONSTRUCTOR space: its own vector, its own table and its own index space in the built
        // meta-object, exactly like Qt's constructor array.
        // No kind argument: a constructor row has no kind (method_kind_enum::Constructor no longer exists, and
        // the built row type has no kind column). The parser only needs the signature text.
        _constructors.push_back(parse_method(signature));
        return meta_method_builder_t(this, static_cast<index_t>(_constructors.size() - 1), true);
    }

    meta_property_builder_t meta_object_builder_t::add_property(std::string_view name, std::string_view type) {
        property_spec_t spec;
        spec.name.assign(name);
        // The type is a run-time text spelling, so it goes through the same canonicaliser a parameter
        // does: the stored column then compares equal to a reflected one.
        qualifier_t qualifier = qualifier_flag::NONE;
        const std::string base = canonical_base_type(type, &qualifier);
        spec.type = spell_type(base, qualifier);
        _properties.push_back(std::move(spec));
        return meta_property_builder_t(this, static_cast<index_t>(_properties.size() - 1));
    }

    meta_enum_builder_t meta_object_builder_t::add_enumerator(std::string_view name) {
        enum_spec_t spec;
        spec.name.assign(name);
        _enums.push_back(std::move(spec));
        return meta_enum_builder_t(this, static_cast<index_t>(_enums.size() - 1));
    }

    index_t meta_object_builder_t::add_class_info(std::string_view name, std::string_view value) {
        class_info_spec_t spec;
        spec.name.assign(name);
        spec.value.assign(value);
        _class_infos.push_back(std::move(spec));
        return static_cast<index_t>(_class_infos.size() - 1);
    }

    // ---- traversal -------------------------------------------------------------------------

    std::size_t meta_object_builder_t::method_count() const noexcept {
        // Methods and signals only: constructors are a space of their own (constructor_count()).
        return _methods.size();
    }

    std::size_t meta_object_builder_t::constructor_count() const noexcept {
        return _constructors.size();
    }

    // The three row factories are const queries (asking a builder for a row modifies nothing) that
    // hand out handles over the builder's MUTABLE rows — editing rows is what a builder is for, so a
    // handle's mutability does not follow the builder's own constness.

    meta_method_builder_t meta_object_builder_t::method(index_t index) const noexcept {
        return meta_method_builder_t(const_cast<meta_object_builder_t*>(this), index);
    }

    meta_method_builder_t meta_object_builder_t::constructor(index_t index) const noexcept {
        return meta_method_builder_t(const_cast<meta_object_builder_t*>(this), index, true);
    }

    std::size_t meta_object_builder_t::property_count() const noexcept {
        return _properties.size();
    }

    meta_property_builder_t meta_object_builder_t::property(index_t index) const noexcept {
        return meta_property_builder_t(const_cast<meta_object_builder_t*>(this), index);
    }

    std::size_t meta_object_builder_t::enumerator_count() const noexcept {
        return _enums.size();
    }

    meta_enum_builder_t meta_object_builder_t::enumerator(index_t index) const noexcept {
        return meta_enum_builder_t(const_cast<meta_object_builder_t*>(this), index);
    }

    std::size_t meta_object_builder_t::class_info_count() const noexcept {
        return _class_infos.size();
    }

    // ---- finding a row ---------------------------------------------------------------------

    index_t meta_object_builder_t::index_of_method(std::string_view signature) const {
        // The query goes through the very normalisation add_method() used: normalized_signature()
        // first (name, parameter list, member cvref), then the per-parameter alias/split pass that
        // produces the stored spellings. That makes the alias spelling ("f(const std::string&)"), the
        // canonical spelling ("f(const std::basic_string<char>&)") and the identity spelling the row
        // stores ("f(const std::__cxx11::basic_string<char>&)") all name the same row.
        // parse_method() reports "no member" for an empty name; a lookup is total, so that is a miss.
        method_spec_t query;
        try {
            query = parse_method(signature, method_kind_enum::PLAIN_METHOD);
        }
        catch (const std::invalid_argument&) {
            return index_npos;
        }
        for (std::size_t i = 0; i < _methods.size(); ++i) {
            if (_methods[i].signature == query.signature) {
                return static_cast<index_t>(i);
            }
        }
        return index_npos;
    }

    index_t meta_object_builder_t::index_of_method_kind(std::string_view signature, method_kind_enum kind) const {
        const index_t index = index_of_method(signature);
        if (index == index_npos || _methods[static_cast<std::size_t>(index)].kind != kind) {
            return index_npos;
        }
        return index;
    }

    index_t meta_object_builder_t::index_of_signal(std::string_view signature) const {
        return index_of_method_kind(signature, method_kind_enum::SIGNAL);
    }

    index_t meta_object_builder_t::index_of_constructor(std::string_view signature) const {
        // The CONSTRUCTOR space only. The same canonicalisation as every other lookup: parse_method()
        // rejects a text that names no member, which a lookup reports as a miss.
        method_spec_t query;
        try {
            query = parse_method(signature);
        }
        catch (const std::invalid_argument&) {
            return index_npos;
        }
        for (std::size_t i = 0; i < _constructors.size(); ++i) {
            if (_constructors[i].signature == query.signature) {
                return static_cast<index_t>(i);
            }
        }
        return index_npos;
    }

    index_t meta_object_builder_t::index_of_property(std::string_view name) const noexcept {
        for (std::size_t i = 0; i < _properties.size(); ++i) {
            if (_properties[i].name == name) {
                return static_cast<index_t>(i);
            }
        }
        return index_npos;
    }

    index_t meta_object_builder_t::index_of_enumerator(std::string_view name) const noexcept {
        for (std::size_t i = 0; i < _enums.size(); ++i) {
            if (_enums[i].name == name) {
                return static_cast<index_t>(i);
            }
        }
        return index_npos;
    }

    // ---- removing members ------------------------------------------------------------------

    void meta_object_builder_t::remove_method(index_t index) {
        if (index >= _methods.size()) {
            return;
        }
        _methods.erase(_methods.begin() + static_cast<std::ptrdiff_t>(index));
        // Every property stores its notifier as a METHOD ROW index, so the shift has to reach them:
        // one that named the erased row loses its notifier, one that named a later row follows it
        // down. Without this a surviving property would silently notify through somebody else's row.
        // The constructor rows are a separate space, so they are untouched.
        for (property_spec_t& spec : _properties) {
            if (spec.notifier == index_npos) {
                continue;
            }
            if (spec.notifier == index) {
                spec.notifier = index_npos;
            }
            else if (spec.notifier > index) {
                spec.notifier -= 1;
            }
        }
    }

    void meta_object_builder_t::remove_constructor(index_t index) {
        if (index >= _constructors.size()) {
            return;
        }
        // Only the constructor space shifts: nothing else names a constructor row (a property
        // notifier must be a SIGNAL, i.e. a method row), so no other index needs re-pointing.
        _constructors.erase(_constructors.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void meta_object_builder_t::remove_property(index_t index) {
        if (index >= _properties.size()) {
            return;
        }
        _properties.erase(_properties.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void meta_object_builder_t::remove_enumerator(index_t index) {
        if (index >= _enums.size()) {
            return;
        }
        _enums.erase(_enums.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void meta_object_builder_t::remove_class_info(index_t index) {
        if (index >= _class_infos.size()) {
            return;
        }
        _class_infos.erase(_class_infos.begin() + static_cast<std::ptrdiff_t>(index));
    }

    // ---- the row accessors the handles use -------------------------------------------------

    meta_object_builder_t::method_spec_t* meta_object_builder_t::handle_row(index_t index,
                                                                         bool constructor_row) noexcept {
        // The two row spaces are a private detail of the builder: the handle carries the flag and this
        // is the one place that turns it into a vector. An out-of-range index answers nullptr, which
        // every handle accessor maps onto its own empty / None / 0 answer (and every setter onto a
        // std::invalid_argument, see stale_row_message).
        std::vector<method_spec_t>& rows = constructor_row ? _constructors : _methods;
        return (index < rows.size()) ? &rows[static_cast<std::size_t>(index)] : nullptr;
    }

    meta_object_builder_t::property_spec_t* meta_object_builder_t::property_row(index_t index) noexcept {
        return (index < _properties.size()) ? &_properties[static_cast<std::size_t>(index)] : nullptr;
    }

    const meta_object_builder_t::property_spec_t* meta_object_builder_t::property_row(index_t index) const noexcept {
        return (index < _properties.size()) ? &_properties[static_cast<std::size_t>(index)] : nullptr;
    }

    meta_object_builder_t::enum_spec_t* meta_object_builder_t::enumerator_row(index_t index) noexcept {
        return (index < _enums.size()) ? &_enums[static_cast<std::size_t>(index)] : nullptr;
    }

    const meta_object_builder_t::enum_spec_t* meta_object_builder_t::enumerator_row(index_t index) const noexcept {
        return (index < _enums.size()) ? &_enums[static_cast<std::size_t>(index)] : nullptr;
    }

    // ---- type spelling ---------------------------------------------------------------------

    std::string meta_object_builder_t::canonical_base_type(std::string_view text, qualifier_t* qualifier) {
        if (qualifier != nullptr) {
            *qualifier = qualifier_flag::NONE;
        }
        while (!text.empty() && detail::is_space(text.front())) {
            text.remove_prefix(1);
        }
        while (!text.empty() && detail::is_space(text.back())) {
            text.remove_suffix(1);
        }
        if (text.empty()) {
            return {};
        }
        // detail::split_qt_arg() is the library's existing type-spelling splitter: it normalises the
        // spelling and peels the top-level cvref off into qualifiers. It peels ONE leading cv word per
        // call, so run it again over what is left to cover the stacked spelling ("const volatile T").
        detail::split_type_t split = detail::split_qt_arg(text);
        const detail::split_type_t outer = detail::split_qt_arg(split.type);
        // The second pass peeled something when the modifiers stopped being None.
        if (outer.modifiers != qualifier_flag::NONE) {
            split.type = outer.type;
            split.modifiers = split.modifiers | outer.modifiers;
        }
        // The type registry is the run-time layer's source of names: original_parameter_alias asks
        // meta_type_t::resolve_global for the base and returns the IDENTITY spelling. Applying it to
        // the BARE spelling is what makes a cv/ref-qualified alias resolve too: split_qt_arg() has
        // already peeled "const ...&" off, so the remaining piece is the whole piece the registry
        // matches. This is the difference that makes check_args() agree with a reflected table.
        split.type = detail::original_parameter_alias(split.type);
        if (qualifier != nullptr) {
            *qualifier = split.modifiers;
        }
        return split.type;
    }

    std::string meta_object_builder_t::spell_type(std::string_view base, qualifier_t qualifier) {
        std::string out;
        // The reflected path spells a non-pointer's cv BEFORE the base ("const int") and a pointer's
        // cv AFTER its declarator ("int* const"); the reference follows either way.
        const bool pointer = !base.empty() && base.back() == '*';
        if (!pointer) {
            if (qualifier & qualifier_flag::CONST) {
                out += "const ";
            }
            if (qualifier & qualifier_flag::VOLATILE) {
                out += "volatile ";
            }
        }
        out.append(base);
        if (pointer) {
            if (qualifier & qualifier_flag::CONST) {
                out += " const";
            }
            if (qualifier & qualifier_flag::VOLATILE) {
                out += " volatile";
            }
        }
        if (qualifier & qualifier_flag::LVALUE_REF) {
            out += '&';
        }
        else if (qualifier & qualifier_flag::RVALUE_REF) {
            out += "&&";
        }
        return out;
    }

    // ---- signature parsing -----------------------------------------------------------------

    meta_object_builder_t::method_spec_t meta_object_builder_t::parse_method(std::string_view signature,
                                                                           method_kind_enum kind) {
        // The library's ONE signature parser: normalized_signature() parses the name, the parameter
        // list and the trailing cvref suffix. A run-time built class has no type-rewriting rules,
        // hence the empty table. normalized_signature() already resolves a name the type registry
        // knows to the IDENTITY spelling the reflected tables store, so its text is the run-time
        // row's spelling; the KEY is still composed again at the end of this function from the
        // base+cvref columns the row actually stores (canonical_base_type() re-runs the registry
        // lookup on the bare base, which is idempotent on the identity spelling).
        std::string canonical = meta_info_t{}.normalized_signature(signature);

        method_spec_t spec;
        spec.kind = kind;

        // Take the cvref suffix back off the parser's text. normalized_signature() appends it in C++'s
        // order (const, volatile, then & / &&), so peeling it in reverse is unambiguous; the member
        // cvref is stored in its own column and re-appended to the key at the end.
        std::string_view key = canonical;
        qualifier_t qualifier = qualifier_flag::NONE;
        if (key.ends_with(" &&")) {
            qualifier = qualifier | qualifier_flag::RVALUE_REF;
            key.remove_suffix(3);
        }
        else if (key.ends_with(" &")) {
            qualifier = qualifier | qualifier_flag::LVALUE_REF;
            key.remove_suffix(2);
        }
        if (key.ends_with(" volatile")) {
            qualifier = qualifier | qualifier_flag::VOLATILE;
            key.remove_suffix(9);
        }
        if (key.ends_with(" const")) {
            qualifier = qualifier | qualifier_flag::CONST;
            key.remove_suffix(6);
        }
        spec.qualifier = qualifier;

        const detail::signature_parts_t parts = detail::split_signature(key);
        spec.name.assign(parts.name);
        if (spec.name.empty()) {
            throw std::invalid_argument("meta_object_builder_t: the signature '" + std::string(signature)
                + "' names no member; write it as name(type,type)");
        }
        spec.parameters.reserve(parts.parameters.size());
        for (const std::string_view piece : parts.parameters) {
            parameter_spec_t parameter;
            // The base spelling and the cvref go into separate columns, which is what lets a later
            // set_parameter_qualifiers() override re-shape the spelling coherently.
            parameter.type = canonical_base_type(piece, &parameter.qualifier);
            spec.parameters.push_back(std::move(parameter));
        }

        // Compose the table key from the SAME spellings build() will store in param_types. The alias
        // map is applied to the BARE piece here (canonical_base_type has already split the cvref off),
        // so `const std::string&` becomes `const std::__cxx11::basic_string<char>&` in the key exactly
        // as it does in the column. The member cvref is re-appended in the canonical order.
        std::string table_key;
        table_key.reserve(spec.name.size() + signature.size());
        table_key.append(spec.name);
        table_key += '(';
        for (std::size_t i = 0; i < spec.parameters.size(); ++i) {
            if (i != 0) {
                table_key += ',';
            }
            table_key += spell_type(spec.parameters[i].type, spec.parameters[i].qualifier);
        }
        table_key += ')';
        if (qualifier & qualifier_flag::CONST) {
            table_key += " const";
        }
        if (qualifier & qualifier_flag::VOLATILE) {
            table_key += " volatile";
        }
        if (qualifier & qualifier_flag::LVALUE_REF) {
            table_key += " &";
        }
        else if (qualifier & qualifier_flag::RVALUE_REF) {
            table_key += " &&";
        }
        spec.signature = std::move(table_key);
        return spec;
    }

    // ---- build -----------------------------------------------------------------------------

    const meta_object_t* meta_object_builder_t::build() {
        if (_built != nullptr) {
            return _built;                        // build() is idempotent
        }

        // ---- validation: a run-time built type has no compile step to report these mistakes ----
        {
            std::vector<std::string_view> method_keys;
            method_keys.reserve(_methods.size());
            for (const method_spec_t& spec : _methods) {
                method_keys.push_back(spec.signature);
            }
            reject_duplicate_keys(std::move(method_keys), "method", "signature");

            // The constructor rows are keyed in a table of their own, so a duplicate is only a
            // duplicate inside that space (the same text may legitimately name a method and a
            // constructor — they never collide).
            std::vector<std::string_view> constructor_keys;
            constructor_keys.reserve(_constructors.size());
            for (const method_spec_t& spec : _constructors) {
                constructor_keys.push_back(spec.signature);
            }
            reject_duplicate_keys(std::move(constructor_keys), "constructor", "signature");

            std::vector<std::string_view> property_keys;
            property_keys.reserve(_properties.size());
            for (const property_spec_t& spec : _properties) {
                property_keys.push_back(spec.name);
            }
            reject_duplicate_keys(std::move(property_keys), "property", "name");

            std::vector<std::string_view> enum_keys;
            enum_keys.reserve(_enums.size());
            for (const enum_spec_t& spec : _enums) {
                enum_keys.push_back(spec.name);
            }
            reject_duplicate_keys(std::move(enum_keys), "enumeration", "name");

            std::vector<std::string_view> class_info_keys;
            class_info_keys.reserve(_class_infos.size());
            for (const class_info_spec_t& spec : _class_infos) {
                class_info_keys.push_back(spec.name);
            }
            reject_duplicate_keys(std::move(class_info_keys), "class info", "name");
        }

        std::size_t user_count = 0;
        for (const property_spec_t& spec : _properties) {
            // Qt's Q_PROPERTY requires a READ accessor, and the documented contract of read() is
            // "always an initialised value on success". A row without a reader could only answer with
            // a declared-only any_t, which would silently break that contract, so it is not a legal
            // state for a built table: refuse it here, naming the property and its cure.
            if (spec.reader == nullptr) {
                throw std::invalid_argument("meta_object_builder_t: property '" + spec.name
                    + "' has no reader; a readable property needs a reader (set it with set_reader())");
            }
            const bool is_user = bool(spec.traits & property_trait_flag::USER);
            if (bool(spec.traits & property_trait_flag::REQUIRED)
                && bool(spec.traits & property_trait_flag::HIDDEN)) {
                throw std::invalid_argument(
                    "meta_object_builder_t: property '" + spec.name + "' cannot be both Required and Hidden");
            }
            if (is_user) {
                if (user_count > 0) {
                    throw std::invalid_argument(
                        "meta_object_builder_t: at most one property per class may be User");
                }
                ++user_count;
            }
            if (spec.notifier != index_npos && spec.notifier >= _methods.size()) {
                throw std::invalid_argument("meta_object_builder_t: property '" + spec.name
                    + "' names a notifier row this class no longer has");
            }
            // The notifier is the change-notification SIGNAL of the property (Q_PROPERTY's NOTIFY
            // names a signal). A plain method row would satisfy is_notifiable() and then fail at
            // connect(), so it is refused here rather than recorded as a lie.
            if (spec.notifier != index_npos && _methods[spec.notifier].kind != method_kind_enum::SIGNAL) {
                throw std::invalid_argument("meta_object_builder_t: property '" + spec.name
                    + "' names a notifier that is not a signal; add_signal() the notifier, not add_method()");
            }
        }

        detail::meta_arena& arena = detail::meta_arena::instance();
        const auto intern = [&arena](std::string_view text) {
            return detail::compiling_string_t(arena.intern(text));
        };

        meta_info_t info;
        info.class_name = intern(_class_name);
        // A run-time built class names itself, so it may already be namespace-qualified (the builder
        // takes the text as given) and that text IS its qualified name. The registry keys on it, so
        // leaving it empty would make the class unregisterable.
        info.qualified_name = info.class_name;

        // ---- functions ----
        //
        // method_slot maps a BUILDER row index to its final table slot, because a property stores its
        // notifier as a builder row and the table's slot is what the built row must record.
        std::vector<index_t> method_slot(_methods.size(), index_npos);
        {
            std::vector<detail::method_info_t> entries;
            std::vector<std::string_view> keys;
            entries.reserve(_methods.size());
            keys.reserve(_methods.size());
            for (const method_spec_t& spec : _methods) {
                detail::method_info_t entry;
                entry.signature = intern(spec.signature);
                entry.name = intern(spec.name);
                entry.qualifier = spec.qualifier;
                entry.return_type = intern(spec.return_type);
                entry.caller = spec.caller;
                entry.kind = spec.kind;
                entry.access = spec.access;

                std::vector<detail::compiling_string_t> types;
                std::vector<detail::compiling_string_t> names;
                std::vector<qualifier_t> qualifiers;
                types.reserve(spec.parameters.size());
                names.reserve(spec.parameters.size());
                qualifiers.reserve(spec.parameters.size());
                for (const parameter_spec_t& parameter : spec.parameters) {
                    // The column is re-spelled from base + qualifier HERE, so an override recorded by
                    // set_parameter_qualifiers() reaches the table and the two columns cannot drift.
                    types.emplace_back(intern(spell_type(parameter.type, parameter.qualifier)));
                    names.emplace_back(intern(parameter.name));
                    qualifiers.push_back(parameter.qualifier);
                }
                // Every parameter column is emitted at the parameter count, INCLUDING the qualifiers:
                // check_args() needs that column to accept a parameterised row.
                entry.param_types = detail::compiling_span_t<const detail::compiling_string_t>(
                    arena.add_array<detail::compiling_string_t>(types));
                entry.param_names = detail::compiling_span_t<const detail::compiling_string_t>(
                    arena.add_array<detail::compiling_string_t>(names));
                entry.param_qualifiers = detail::compiling_span_t<const qualifier_t>(
                    store_array<qualifier_t>(qualifiers));

                if (!spec.tag.empty()) {
                    // The one shape a run-time tag can carry: make_str_tag()'s "const char*".
                    detail::annotation_entry_t annotation;
                    annotation.type_name = intern("const char*");
                    annotation.value = store_tag_value(arena.intern(spec.tag));
                    const detail::annotation_entry_t single[] = { annotation };
                    entry.annotations = detail::compiling_span_t<const detail::annotation_entry_t>(
                        store_array<detail::annotation_entry_t>(single));
                }

                entries.push_back(std::move(entry));
                keys.push_back(static_cast<std::string_view>(entries.back().signature));
            }

            const auto table = detail::chd_hash(keys);
            if (!keys.empty() && (table.displacements.empty() || table.redirect.empty())) {
                throw std::invalid_argument(perfect_hash_message("methods", keys.size()));
            }
            info.method_hashtable = detail::hashtable_t{
                table.seed0, table.seed1,
                arena.add_array<std::uint32_t>(table.displacements),
                arena.add_array<std::uint32_t>(table.redirect)
            };
            std::vector<detail::method_info_t> ordered(entries.size());
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const std::uint64_t slot = info.method_hashtable.find(keys[i]);
                if (slot == detail::hashtable_t::npos) {
                    throw std::invalid_argument(perfect_hash_message("methods", keys.size()));
                }
                method_slot[i] = static_cast<index_t>(slot);
                ordered[static_cast<std::size_t>(slot)] = std::move(entries[i]);
            }
            info.method_infos = arena.add_array<detail::method_info_t>(ordered);
        }

        // ---- constructors ----
        //
        // A table of their own: own keys, own perfect hash, own array. Nothing here is a method row,
        // so the two index spaces never mix (Qt's constructorCount()).
        {
            std::vector<detail::constructor_info_t> entries;
            std::vector<std::string_view> keys;
            entries.reserve(_constructors.size());
            keys.reserve(_constructors.size());
            for (const method_spec_t& spec : _constructors) {
                detail::constructor_info_t entry;
                entry.signature = intern(spec.signature);
                entry.name = intern(spec.name);
                // No qualifier / return-type / kind column: a constructor has none of the three, and
                // the row type it is built into does not carry them.
                entry.caller = spec.caller;
                entry.access = spec.access;

                std::vector<detail::compiling_string_t> types;
                std::vector<detail::compiling_string_t> names;
                std::vector<qualifier_t> qualifiers;
                types.reserve(spec.parameters.size());
                names.reserve(spec.parameters.size());
                qualifiers.reserve(spec.parameters.size());
                for (const parameter_spec_t& parameter : spec.parameters) {
                    types.emplace_back(intern(spell_type(parameter.type, parameter.qualifier)));
                    names.emplace_back(intern(parameter.name));
                    qualifiers.push_back(parameter.qualifier);
                }
                entry.param_types = detail::compiling_span_t<const detail::compiling_string_t>(
                    arena.add_array<detail::compiling_string_t>(types));
                entry.param_names = detail::compiling_span_t<const detail::compiling_string_t>(
                    arena.add_array<detail::compiling_string_t>(names));
                entry.param_qualifiers = detail::compiling_span_t<const qualifier_t>(
                    store_array<qualifier_t>(qualifiers));

                if (!spec.tag.empty()) {
                    detail::annotation_entry_t annotation;
                    annotation.type_name = intern("const char*");
                    annotation.value = store_tag_value(arena.intern(spec.tag));
                    const detail::annotation_entry_t single[] = { annotation };
                    entry.annotations = detail::compiling_span_t<const detail::annotation_entry_t>(
                        store_array<detail::annotation_entry_t>(single));
                }

                entries.push_back(std::move(entry));
                keys.push_back(static_cast<std::string_view>(entries.back().signature));
            }

            const auto table = detail::chd_hash(keys);
            if (!keys.empty() && (table.displacements.empty() || table.redirect.empty())) {
                throw std::invalid_argument(perfect_hash_message("constructors", keys.size()));
            }
            info.constructor_hashtable = detail::hashtable_t{
                table.seed0, table.seed1,
                arena.add_array<std::uint32_t>(table.displacements),
                arena.add_array<std::uint32_t>(table.redirect)
            };
            std::vector<detail::constructor_info_t> ordered(entries.size());
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const std::uint64_t slot = info.constructor_hashtable.find(keys[i]);
                if (slot == detail::hashtable_t::npos) {
                    throw std::invalid_argument(perfect_hash_message("constructors", keys.size()));
                }
                ordered[static_cast<std::size_t>(slot)] = std::move(entries[i]);
            }
            info.constructor_infos = arena.add_array<detail::constructor_info_t>(ordered);
        }

        // ---- properties ----
        {
            std::vector<detail::property_info_t> entries;
            std::vector<std::string_view> keys;
            entries.reserve(_properties.size());
            keys.reserve(_properties.size());
            for (const property_spec_t& spec : _properties) {
                detail::property_info_t entry;
                entry.name = intern(spec.name);
                entry.type = intern(spec.type);
                entry.enum_name = intern(spec.enum_name);
                entry.is_flags = spec.is_flags;
                entry.traits = spec.traits;
                entry.reader = spec.reader;
                entry.writer = spec.writer;
                entry.reseter = spec.reseter;
                if (spec.notifier != index_npos) {
                    const index_t slot = method_slot[static_cast<std::size_t>(spec.notifier)];
                    const detail::generic_call caller = info.method_at(static_cast<std::size_t>(slot)).caller;
                    // A notifier is a SIGNAL row whose thunk is the notification channel; a row without
                    // a thunk cannot notify, so it is recorded as no notifier at all instead of a
                    // column combination that would answer is_notifiable() with a lie.
                    if (caller != nullptr) {
                        entry.notifier = caller;
                        entry.notifier_slot = slot;
                    }
                }
                entries.push_back(std::move(entry));
                keys.push_back(static_cast<std::string_view>(entries.back().name));
            }

            const auto table = detail::chd_hash(keys);
            if (!keys.empty() && (table.displacements.empty() || table.redirect.empty())) {
                throw std::invalid_argument(perfect_hash_message("properties", keys.size()));
            }
            info.properties_hashtable = detail::hashtable_t{
                table.seed0, table.seed1,
                arena.add_array<std::uint32_t>(table.displacements),
                arena.add_array<std::uint32_t>(table.redirect)
            };
            std::vector<detail::property_info_t> ordered(entries.size());
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const std::uint64_t slot = info.properties_hashtable.find(keys[i]);
                if (slot == detail::hashtable_t::npos) {
                    throw std::invalid_argument(perfect_hash_message("properties", keys.size()));
                }
                ordered[static_cast<std::size_t>(slot)] = std::move(entries[i]);
            }
            info.property_infos = arena.add_array<detail::property_info_t>(ordered);
        }

        // ---- enumerations ----
        {
            std::vector<detail::enum_info_t> entries;
            std::vector<std::string_view> keys;
            entries.reserve(_enums.size());
            keys.reserve(_enums.size());
            for (const enum_spec_t& spec : _enums) {
                detail::enum_info_t entry;
                entry.name = intern(spec.name);
                entry.scope = intern(_class_name);
                // No C++ declaration exists here, so the row takes the shape a builder can express: a
                // scoped enum whose underlying width is the full width of the values it stores
                // (std::int64_t).
                entry.is_scoped = true;
                entry.is_flag = spec.is_flag;
                entry.underlying_size = sizeof(std::int64_t);

                std::vector<detail::enum_entry_t> enumerators;
                enumerators.reserve(spec.keys.size());
                for (const auto& [key, value] : spec.keys) {
                    enumerators.push_back(detail::enum_entry_t{ intern(key), value });
                }
                entry.entries = detail::compiling_span_t<const detail::enum_entry_t>(
                    arena.add_array<detail::enum_entry_t>(enumerators));
                entries.push_back(std::move(entry));
                keys.push_back(static_cast<std::string_view>(entries.back().name));
            }

            const auto table = detail::chd_hash(keys);
            if (!keys.empty() && (table.displacements.empty() || table.redirect.empty())) {
                throw std::invalid_argument(perfect_hash_message("enumerations", keys.size()));
            }
            info.enums_hashtable = detail::hashtable_t{
                table.seed0, table.seed1,
                arena.add_array<std::uint32_t>(table.displacements),
                arena.add_array<std::uint32_t>(table.redirect)
            };
            std::vector<detail::enum_info_t> ordered(entries.size());
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const std::uint64_t slot = info.enums_hashtable.find(keys[i]);
                if (slot == detail::hashtable_t::npos) {
                    throw std::invalid_argument(perfect_hash_message("enumerations", keys.size()));
                }
                ordered[static_cast<std::size_t>(slot)] = std::move(entries[i]);
            }
            info.enum_infos = arena.add_array<detail::enum_info_t>(ordered);
        }

        // ---- class info ----
        {
            std::vector<detail::class_info_t> entries;
            std::vector<std::string_view> keys;
            entries.reserve(_class_infos.size());
            keys.reserve(_class_infos.size());
            for (const class_info_spec_t& spec : _class_infos) {
                detail::class_info_t entry;
                entry.name = intern(spec.name);
                entry.value = intern(spec.value);
                entries.push_back(std::move(entry));
                keys.push_back(static_cast<std::string_view>(entries.back().name));
            }

            const auto table = detail::chd_hash(keys);
            if (!keys.empty() && (table.displacements.empty() || table.redirect.empty())) {
                throw std::invalid_argument(perfect_hash_message("class infos", keys.size()));
            }
            info.class_infos_hashtable = detail::hashtable_t{
                table.seed0, table.seed1,
                arena.add_array<std::uint32_t>(table.displacements),
                arena.add_array<std::uint32_t>(table.redirect)
            };
            std::vector<detail::class_info_t> ordered(entries.size());
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const std::uint64_t slot = info.class_infos_hashtable.find(keys[i]);
                if (slot == detail::hashtable_t::npos) {
                    throw std::invalid_argument(perfect_hash_message("class infos", keys.size()));
                }
                ordered[static_cast<std::size_t>(slot)] = std::move(entries[i]);
            }
            info.class_info_infos = arena.add_array<detail::class_info_t>(ordered);
        }

        // The table is assembled from copies of every row, so editing a handle afterwards cannot reach
        // it: the snapshot is one-shot. The arena entry IS the meta-object the caller keeps, and the
        // chain link is the base's own meta-object, never a copy of its table.
        // The built table is NOT registered with the class registry: registration stays the caller's
        // job (object_t::register_type), exactly as it is for a hand-written class.
        _built = arena.add(std::move(info), _super);
        return _built;
    }
}
