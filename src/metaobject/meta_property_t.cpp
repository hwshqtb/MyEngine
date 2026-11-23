#include <hwshqtb/metaobject/meta_property_t.h>
#include <hwshqtb/metaobject/meta_enum_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/log.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace hwshqtb::metaobject {
    namespace {
        // Miss-memo bits of meta_property_t::_miss_logged, one per accessor role.
        constexpr std::uint8_t miss_writer = 0x1;
        constexpr std::uint8_t miss_reseter = 0x2;
        constexpr std::uint8_t miss_notifier = 0x4;
    }

    meta_property_t::meta_property_t(const meta_object_t* owner, index_t index) noexcept
        :
        _owner(owner),
        _index(index) { }

    const detail::property_info_t* meta_property_t::entry() const noexcept {
        // The one table access this handle has: meta_object_t owns the meta_info_t by value, so the
        // row pointer stays valid as long as the declaring meta-object does.
        return (_owner != nullptr) ? _owner->property_entry(_index) : nullptr;
    }

    bool meta_property_t::is_valid() const noexcept {
        return entry() != nullptr;
    }

    std::string_view meta_property_t::name() const noexcept {
        const detail::property_info_t* row = entry();
        return (row != nullptr) ? static_cast<std::string_view>(row->name) : std::string_view{};
    }

    std::string_view meta_property_t::type_name() const noexcept {
        const detail::property_info_t* row = entry();
        return (row != nullptr) ? static_cast<std::string_view>(row->type) : std::string_view{};
    }

    meta_type_t meta_property_t::type(const object_t& obj) const {
        (void)obj; // the registry is process-wide; the argument is kept for source compatibility
        return resolved_type();
    }

    meta_type_t meta_property_t::resolved_type() const noexcept {
        const detail::property_info_t* row = entry();
        if (row == nullptr) {
            return {};
        }
        // Resolve the stored identity name in the one process-wide registry. The property column
        // keeps the cvref spelling ("const std::__cxx11::basic_string<char>&") while the registry is
        // keyed by the DECAYED bare name, so strip it; otherwise a `const T&` getter's property
        // resolves to nothing and read() throws bad_meta_type_t.
        return meta_type_t::resolve_global(detail::bare_spelling(static_cast<std::string_view>(row->type)));
    }

    bool meta_property_t::is_enum_type() const noexcept {
        const detail::property_info_t* row = entry();
        return row != nullptr && !static_cast<std::string_view>(row->enum_name).empty();
    }

    bool meta_property_t::is_flag_type() const noexcept {
        const detail::property_info_t* row = entry();
        return row != nullptr && row->is_flags;
    }

    meta_enum_t meta_property_t::enumerator() const {
        const detail::property_info_t* row = entry();
        if (row == nullptr || _owner == nullptr) {
            return {};
        }
        // The enum name is recorded at build time from the real C++ type (an enum, or the E of a
        // flag_t<E> wrapper), so no run-time parsing of the property type is needed on that path.
        // enumeration_in() takes the declaring meta-object POINTER: no facade is built, so the
        // returned handle points into a chain link that really exists.
        const std::string_view recorded = static_cast<std::string_view>(row->enum_name);
        if (!recorded.empty()) {
            return meta_object_t::enumeration_in(_owner, recorded);
        }
        // Fallback for run-time built properties (meta_object_builder_t records no column): parse the type-name tail.
        // Normalised type names are qualified (meta_test_t::Mode) while an enum is registered under its
        // bare identifier inside the declaring class, so try the full name first and then the tail.
        const std::string_view full = type_name();
        meta_enum_t found = meta_object_t::enumeration_in(_owner, full);
        if (found.is_valid()) {
            return found;
        }
        if (const auto pos = full.rfind("::"); pos != std::string_view::npos) {
            return meta_object_t::enumeration_in(_owner, full.substr(pos + 2));
        }
        return found;
    }

    bool meta_property_t::is_writable() const noexcept {
        return writer().is_valid();
    }

    bool meta_property_t::is_resettable() const noexcept {
        return reseter().is_valid();
    }

    bool meta_property_t::is_notifiable() const noexcept {
        return notifier().is_valid();
    }

    bool meta_property_t::is_constant() const noexcept {
        return !is_writable() && !is_notifiable();
    }

    property_trait_t meta_property_t::traits() const noexcept {
        const detail::property_info_t* row = entry();
        return (row != nullptr) ? row->traits : property_trait_t{ property_trait_flag::NONE };
    }

    bool meta_property_t::is_user() const noexcept {
        return bool(traits() & property_trait_flag::USER);
    }

    bool meta_property_t::is_required() const noexcept {
        return bool(traits() & property_trait_flag::REQUIRED);
    }

    bool meta_property_t::is_hidden() const noexcept {
        return bool(traits() & property_trait_flag::HIDDEN);
    }

    bool meta_property_t::is_final() const noexcept {
        return bool(traits() & property_trait_flag::FINAL);
    }

    bool meta_property_t::designable() const noexcept {
        return !is_hidden();
    }

    meta_method_t meta_property_t::resolve_named_accessor(const detail::compiling_string_t& name,
                                                          std::initializer_list<std::uint8_t> arities,
                                                          std::uint8_t role_bit,
                                                          const char* role) const noexcept {
        const std::string_view accessor_name = name;
        const detail::property_info_t* row = entry();
        const std::string_view property_type = (row != nullptr) ? static_cast<std::string_view>(row->type)
                                                                : std::string_view{};

        // A bare accessor NAME carries no cvref (an annotation has no room for a signature), so the
        // candidate is matched by the facts the annotation DOES give: its NAME and its SHAPE (arity,
        // and the property's type for the one-argument roles) — the same facts the compile-time pass
        // matches on. Signature lookups stay exact; this is not one of them.
        //
        // Scope order decides first: the chain is walked from the declaring class up to the root, and
        // the FIRST class that can answer wins. Within one class a LEVEL is one matching rule, tried
        // in this order (the compile-time pick_overload sequence): the exact type, then — for a
        // one-argument role — the bare type. A level that finds exactly one candidate answers; a level
        // that would answer with SEVERAL is an ambiguity: a bare accessor name cannot choose between
        // overloads, so the pass reports a Critical and yields no handle instead of silently taking
        // the first table entry.
        const std::string_view property_bare = detail::bare_spelling(property_type);
        // The notifier is the change-notification SIGNAL of the property (Q_PROPERTY's NOTIFY names a
        // signal), so a plain slot that happens to share the name must not bind: it would satisfy
        // is_notifiable() and then fail at connect(). The other roles name ordinary methods.
        const bool notifier_role = std::string_view(role) == "notifier";
        // One scope, one ARITY and one LEVEL. An arity of 0 has no parameter to compare: the shape
        // itself is the match (the compile-time pick_overload rule). An arity of 1 compares the
        // parameter with the property's type at two levels: `exact_only` is the fine level; the
        // second pass also accepts a parameter whose bare spelling matches.
        struct match_result {
            /// @brief The unique candidate of the level, or an invalid handle.
            meta_method_t method;
            /// @brief Whether the level that would have answered holds several candidates.
            bool ambiguous = false;
        };
        auto match_scope = [&](const meta_object_t* scope, std::size_t arity,
                               bool exact_only) -> match_result {
            const meta_info_t& scope_info = scope->info();
            index_t candidate = index_npos;
            std::size_t hits = 0;
            for (std::size_t i = 0; i < scope_info.method_count(); ++i) {
                const detail::method_info_t& entry = scope_info.method_at(i);
                if (notifier_role && entry.kind != method_kind_enum::SIGNAL) {
                    continue;
                }
                // An accessor is called ON an object, so a row with NO receiver (a static member
                // function) can never be one, however well its bare name and parameter list fit. The
                // compile-time scan excludes statics for exactly this reason (detail::get_functions
                // with its default want_static=false); the method table now HOLDS such rows, so this
                // run-time sibling has to state the same exclusion — otherwise a static
                // `set_value(int)` would bind as the property's writer and be run through a thunk that
                // ignores the object.
                if (!entry.has_receiver()) {
                    continue;
                }
                if (static_cast<std::string_view>(entry.name) != accessor_name) {
                    continue;
                }
                const auto params = entry.param_types.to_span();
                if (params.size() != arity) {
                    continue;
                }
                if (arity == 0) {
                    if (hits == 0) {
                        candidate = static_cast<index_t>(i);
                    }
                    ++hits;
                    continue;
                }
                const std::string_view declared = params[0];
                if (declared == property_type
                    || (!exact_only && detail::bare_spelling(declared) == property_bare)) {
                    if (hits == 0) {
                        candidate = static_cast<index_t>(i);
                    }
                    ++hits;
                }
            }
            if (hits == 1) {
                return { meta_method_t(scope, candidate), false };
            }
            if (hits > 1) {
                // This is the level that would have answered, and it has no unique candidate.
                return { { }, true };
            }
            return { };
        };

        // The whole body is guarded: this query is noexcept by contract, and the diagnostic allocates.
        meta_method_t found;
        bool ambiguous = false;
        try {
            // Flood control is shared with the miss diagnostic below: asking is_writable() + writer()
            // + write() through the same handle reports one Critical about its broken annotation.
            const auto report_ambiguity = [&] {
                if ((_miss_logged & role_bit) != 0) {
                    return;
                }
                _miss_logged = static_cast<std::uint8_t>(_miss_logged | role_bit);
                try {
                    std::string message("meta_property_t: property \"");
                    message += (row != nullptr) ? static_cast<std::string_view>(row->name) : std::string_view{};
                    message += "\" names the ";
                    message += role;
                    message += " \"";
                    message += accessor_name;
                    message += "\", which matches several overloads in its meta-object chain; a bare name"
                               " cannot choose between them";
                    HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, message);
                }
                catch (...) {
                    // A diagnostic must never escape a noexcept query.
                }
            };
            // Scope-major: walk from the declaring class up to the root, and within ONE class try its
            // EXACT level and then (for a one-argument role) its BARE level. The first class that
            // answers wins, so a nearer class's approximate match beats a farther class's exact one —
            // ordinary name hiding, and what the compile-time pass does inside the one class it scans.
            // Returns true when the search stops: a unique match was found, or the level that would
            // have answered is ambiguous.
            const auto attempt = [&](std::size_t arity) -> bool {
                for (const meta_object_t* scope = _owner; scope != nullptr; scope = scope->super_class()) {
                    const match_result exact = match_scope(scope, arity, true);
                    if (exact.ambiguous) {
                        ambiguous = true;
                        report_ambiguity();
                        return true;
                    }
                    if (exact.method.is_valid()) {
                        found = exact.method;
                        return true;
                    }
                    if (arity == 0) {
                        continue;   // no type to compare: one level per class
                    }
                    const match_result bare = match_scope(scope, arity, false);
                    if (bare.ambiguous) {
                        ambiguous = true;
                        report_ambiguity();
                        return true;
                    }
                    if (bare.method.is_valid()) {
                        found = bare.method;
                        return true;
                    }
                }
                return false;
            };

            // The compile-time sequence, role by role (meta_info_t.h: the three pick_overload call
            // sites): the writer takes the property type only; the reseter takes the no-argument form
            // only; the notifier tries the one-argument form first and the no-argument form as its
            // fallback.
            for (const std::size_t arity : arities) {
                if (attempt(arity)) {
                    break;
                }
            }
        }
        catch (...) {
            return {};
        }
        if (ambiguous) {
            return {};
        }
        if (found.is_valid()) {
            return found;
        }

        // Flood control (documented choice): the miss is memoised IN THE HANDLE, per role. A handle
        // reports a given unresolved accessor once, so is_writable() + writer() + write() on the same
        // handle produce one Critical, not three. A newly constructed handle (object_t::property /
        // meta_object_t::property) starts a new attempt and therefore may report again — the log stays
        // a diagnostic about a broken annotation, not a per-call siren.
        if ((_miss_logged & role_bit) == 0) {
            _miss_logged = static_cast<std::uint8_t>(_miss_logged | role_bit);
            try {
                std::string message("meta_property_t: property \"");
                message += (row != nullptr) ? static_cast<std::string_view>(row->name) : std::string_view{};
                message += "\" names the ";
                message += role;
                message += " \"";
                message += accessor_name;
                message += "\", which no class in its meta-object chain declares";
                HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, message);
            }
            catch (...) {
                // A diagnostic must never escape a noexcept query.
            }
        }
        return {};
    }

    meta_method_t meta_property_t::writer() const noexcept {
        const detail::property_info_t* row = entry();
        if (row == nullptr) {
            return {};
        }
        if (row->writer != nullptr) {
            if (row->writer_slot != index_npos) {
                // A compile-time hit is always in this meta-object: the slot indexes this table's own
                // method_infos. An inherited writer is resolved by name at run time, below.
                return meta_method_t(_owner, row->writer_slot);
            }
            // A run-time built table (meta_object_builder_t) carries the thunk itself and no slot.
            return meta_method_t(_owner, row->writer);
        }
        const std::string_view name = row->writer_name;
        if (!name.empty()) {
            return resolve_named_accessor(row->writer_name, { 1 }, miss_writer, "writer");
        }
        return {};
    }

    meta_method_t meta_property_t::reseter() const noexcept {
        const detail::property_info_t* row = entry();
        if (row == nullptr) {
            return {};
        }
        if (row->reseter != nullptr) {
            if (row->reseter_slot != index_npos) {
                return meta_method_t(_owner, row->reseter_slot);
            }
            return meta_method_t(_owner, row->reseter);
        }
        const std::string_view name = row->reseter_name;
        if (!name.empty()) {
            return resolve_named_accessor(row->reseter_name, { 0 }, miss_reseter, "reseter");
        }
        return {};
    }

    meta_method_t meta_property_t::notifier() const noexcept {
        const detail::property_info_t* row = entry();
        if (row == nullptr) {
            return {};
        }
        if (row->notifier != nullptr) {
            if (row->notifier_slot != index_npos) {
                return meta_method_t(_owner, row->notifier_slot);
            }
            return meta_method_t(_owner, row->notifier);
        }
        const std::string_view name = row->notifier_name;
        if (!name.empty()) {
            return resolve_named_accessor(row->notifier_name, { 1, 0 }, miss_notifier, "notifier");
        }
        return {};
    }

    any_t meta_property_t::read_value(const void* obj, const meta_type_t& t) const {
        const detail::property_info_t* row = entry();
        if (row == nullptr) {
            throw bad_meta_property_t{};
        }
        if (!t.is_valid()) {
            throw bad_meta_type_t{};
        }
        if (row->reader == nullptr) {
            // Defensive: meta_object_builder_t::build() refuses a readerless property and the
            // compile-time path always records one, so this is unreachable through the public
            // builders. A declared-only any_t is the honest "no value" answer for a row that cannot
            // read; it is not a success value.
            return any_t();
        }
        void* mem = t.allocate();
        if (!t.default_construct(mem)) {
            t.deallocate(mem);
            throw bad_any_unsupported_t{};
        }
        void* args[1] = { mem };
        row->reader(const_cast<void*>(obj), args);
        return any_t::adopt(t, mem);
    }

    any_t meta_property_t::read(const object_t& obj) const {
        if (!is_valid()) {
            throw bad_meta_property_t{};
        }
        // resolved_type() prefers the table's embedded interface (a declared type) and falls back to
        // the stored name; the read itself is shared with the GADGET overload.
        return read_value(&obj, resolved_type());
    }

    bool meta_property_t::write_value(void* obj, const meta_type_t& t, const any_t& value) const {
        if (!is_valid()) {
            throw bad_meta_property_t{};
        }
        // Resolve FIRST: the accessor may be an inherited one (the run-time fallback), and "no writer"
        // is still answered with false rather than an exception.
        const meta_method_t target = writer();
        if (!target.is_valid()) {
            return false; // read-only (or a writer name that resolves to nothing)
        }
        if (!t.is_valid()) {
            throw bad_meta_type_t{};
        }
        if (!value.is_initialized()) {
            throw bad_any_uninitialized_t{};
        }

        const void* src = nullptr;
        any_t converted;
        // Same type is meta_type_t's identity rule (runtime id, name tie-break while an id is still
        // 0); comparing raw ids would confuse two different not-yet-registered types with each other.
        if (value.type() == t) {
            src = &value;
        }
        else {
            if (!value.type().has_conversion_to(t)) {
                throw bad_any_cast_t{};
            }
            void* mem = t.allocate();
            if (!value.type().convert(mem, t, &value)) {
                t.deallocate(mem);
                throw bad_any_cast_t{};
            }
            converted = any_t::adopt(t, mem);
            src = &converted;
        }

        void* args[2] = { nullptr, const_cast<void*>(src) };
        // The property plumbing hands the writer the raw source block, so it goes through the
        // handle's private erased entry rather than the public typed/any_t ones. A synthesised
        // accessor has no receiver TYPE left (the caller passes a void*), so the object's cvref is
        // honestly unknown: qualifier_flag::NONE.
        target.invoke_caller(obj, args, qualifier_flag::NONE);
        return true;
    }

    bool meta_property_t::write(object_t& obj, const any_t& value) const {
        if (!is_valid()) {
            throw bad_meta_property_t{};
        }
        return write_value(&obj, resolved_type(), value);
    }

    bool meta_property_t::reset_value(void* obj, const meta_type_t& t) const {
        if (!is_valid()) {
            throw bad_meta_property_t{};
        }
        // Same order as write_value: resolve first (an inherited reseter is found at run time), and a
        // missing reseter is false, not an exception.
        const meta_method_t target = reseter();
        if (!target.is_valid()) {
            return false;
        }
        if (!t.is_valid()) {
            throw bad_meta_type_t{};
        }
        void* args[1] = { nullptr };
        // Same "no receiver type" situation as the writer above: a reseter is a synthesised accessor
        // called through a void*, so its object cvref is unknown.
        target.invoke_caller(obj, args, qualifier_flag::NONE);
        return true;
    }

    bool meta_property_t::reset(object_t& obj) const {
        if (!is_valid()) {
            throw bad_meta_property_t{};
        }
        return reset_value(&obj, resolved_type());
    }
}
