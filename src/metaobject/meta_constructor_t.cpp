/// @file meta_constructor_t.cpp
/// @brief The constructor handle: identity plus construction, read from the owner's private table row.
///
/// The metadata is answered from the row entry() returns; this file keeps the handle's own state
/// (the declaring meta-object plus its constructor_infos slot) and the construction entry point.

#include <hwshqtb/metaobject/meta_constructor_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/log.h>

#include <string>

namespace hwshqtb::metaobject {
    const detail::constructor_info_t* meta_constructor_t::entry() const noexcept {
        // A handle can carry index_npos (the default one) or a stale slot, and both must answer
        // defensively instead of indexing the table; the guard is shared with every other handle
        // (meta_object_t::row_at).
        return meta_object_t::row_at(_owner, _index, &meta_info_t::constructor_infos);
    }

    meta_constructor_t::meta_constructor_t(const meta_object_t* owner, index_t index) noexcept:
        _owner(owner),
        _index(index) { }

    bool meta_constructor_t::is_valid() const noexcept {
        // A constructor handle is valid exactly when its row carries a construction thunk. Every
        // constructor the class declares is in the table, but a deleted one — and every constructor
        // of an abstract class — has no thunk (the compile-time pass could not make `new T(...)`
        // compile), so it reports false while signature() still answers. "Exists in the table" and
        // "can be called" are two questions, and only the second one is is_valid().
        const detail::constructor_info_t* row = entry();
        return (row != nullptr) && row->caller != nullptr;
    }

    std::string_view meta_constructor_t::signature() const noexcept {
        const detail::constructor_info_t* row = entry();
        return (row != nullptr) ? static_cast<std::string_view>(row->signature) : std::string_view{};
    }

    std::string_view meta_constructor_t::name() const noexcept {
        const detail::constructor_info_t* row = entry();
        return (row != nullptr) ? static_cast<std::string_view>(row->name) : std::string_view{};
    }

    method_access_enum meta_constructor_t::access() const noexcept {
        // No table row — an invalid handle — means there is no declaration to report an access
        // specifier for. Otherwise the row's column answers with the specifier the class wrote,
        // recorded at build time. It is a hint: construct() reaches the constructor whatever this says.
        const detail::constructor_info_t* row = entry();
        return (row != nullptr) ? row->access : method_access_enum::NONE;
    }

    qualifier_t meta_constructor_t::qualifier() const noexcept {
        // A constructor has no cvref — it cannot be const/volatile/&/&& qualified — and its row type
        // (detail::constructor_info_t) accordingly has NO qualifier column. The accessor is kept so
        // this handle reads like meta_method_t::qualifier(), and it answers None structurally.
        // None is the same answer for every table the library builds: the compile-time builder only
        // ever wrote a qualifier on the method path, and the run-time builder would have needed a
        // nonsensical text such as "class_t() const" to set one — the column no longer exists.
        return qualifier_flag::NONE;
    }

    index_t meta_constructor_t::index() const noexcept {
        // Constructors are never inherited, so the stored slot IS the coordinate
        // meta_object_t::constructor(index) accepts: no ancestor counts are added here (unlike
        // meta_method_t::index(), whose space is the whole chain).
        return (entry() != nullptr) ? _index : index_npos;
    }

    const meta_object_t* meta_constructor_t::owner() const noexcept {
        return _owner;
    }

    index_t meta_constructor_t::param_count() const noexcept {
        const detail::constructor_info_t* row = entry();
        return (row != nullptr) ? static_cast<index_t>(row->param_types.to_span().size()) : 0;
    }

    std::string_view meta_constructor_t::param_name(index_t index) const noexcept {
        const detail::constructor_info_t* row = entry();
        if (row == nullptr) {
            return {};
        }
        const std::span<const detail::compiling_string_t> names = row->param_names;
        return (index < names.size()) ? static_cast<std::string_view>(names[static_cast<std::size_t>(index)])
                                      : std::string_view{};
    }

    std::string_view meta_constructor_t::param_type_name(index_t index) const noexcept {
        const detail::constructor_info_t* row = entry();
        if (row == nullptr) {
            return {};
        }
        const std::span<const detail::compiling_string_t> types = row->param_types;
        return (index < types.size()) ? static_cast<std::string_view>(types[static_cast<std::size_t>(index)])
                                      : std::string_view{};
    }

    qualifier_t meta_constructor_t::param_qualifier(index_t index) const noexcept {
        const detail::constructor_info_t* row = entry();
        if (row == nullptr) {
            return qualifier_flag::NONE;
        }
        const std::span<const qualifier_t> qualifiers = row->param_qualifiers;
        return (index < qualifiers.size()) ? qualifiers[static_cast<std::size_t>(index)]
                                           : qualifier_t{ qualifier_flag::NONE };
    }

    const void* meta_constructor_t::tag(std::string_view type_name) const noexcept {
        // A handle with no table row carries no annotation column at all; answering nullptr keeps the
        // query total, like every other defensive query.
        const detail::constructor_info_t* row = entry();
        if (row == nullptr) {
            return nullptr;
        }
        // The table stores a FLATTENED entry per annotation (type name + staticised pointer), in
        // declaration order; the first match wins — the same shape meta_method_t::tag() reads.
        const std::span<const detail::annotation_entry_t> annotations = row->annotations;
        for (const detail::annotation_entry_t& annotation : annotations) {
            if (static_cast<std::string_view>(annotation.type_name) == type_name) {
                return annotation.value;
            }
        }
        return nullptr;
    }

    bool meta_constructor_t::construct(std::span<const any_t> args, void* out_slot) const {
        const detail::constructor_info_t* row = entry();
        if (row == nullptr) {
            return false;                        // there is no row to run
        }
        // The handle has ALREADY chosen the constructor, so unlike new_instance() there is no
        // selection pass left to match the argument list: the arity is checked here, against this
        // row, and the types are the caller's responsibility exactly as they are for the signature
        // form of meta_object_t::construct_into.
        const std::size_t arity = row->param_types.to_span().size();
        if (arity != args.size()) {
            HWSHQTB_LOG_RAW(log_connection(), log_level_enum::WARNING,
                "meta_constructor_t::construct: '" + std::string(row->signature) + "' takes "
                + std::to_string(arity) + " argument(s), " + std::to_string(args.size())
                + " were supplied");
            return false;
        }
        // The ONE construction path: the caller-side resolution is bypassed (the signature names this
        // very row), and the thunk run, the null-caller verdict and the throwing-constructor
        // diagnostic are the ones new_instance() reports.
        return _owner->construct_into(signature(), args, out_slot);
    }

    HWSHQTB_EXPORT bool operator==(const meta_constructor_t& lhs,
                                   const meta_constructor_t& rhs) noexcept {
        // Constructors are not inherited, so the declaring meta-object plus the slot identify the
        // row exactly.
        return lhs._owner == rhs._owner && lhs._index == rhs._index;
    }
}
