/// @file meta_method_builder_t.cpp
/// @brief The mutable handle of one run-time built method (QMetaMethodBuilder equivalent).

#include "hwshqtb/metaobject/meta_method_builder_t.h"

#include "hwshqtb/metaobject/meta_object_builder_t.h"

#include <stdexcept>
#include <string>

namespace hwshqtb::metaobject {
    namespace {
        /// @brief Throw the diagnostic of a setter called on a handle whose row is gone.
        /// @param name The handle's name() at the time of the call (may be empty).
        /// @param constructor_row Which row space the handle names; it selects the removal the cure
        ///        names, because remove_method() and remove_constructor() shift different rows.
        /// @note [[noreturn]] so a setter can call it in its stale branch and keep going afterwards.
        [[noreturn]] void throw_stale_row(std::string_view name, bool constructor_row) {
            throw std::invalid_argument(detail::stale_row_message("meta_method_builder_t", name,
                constructor_row ? "remove_constructor" : "remove_method"));
        }
    }

    meta_method_builder_t::meta_method_builder_t(meta_object_builder_t* owner, index_t index,
                                                 bool constructor_row) noexcept:
        _owner(owner),
        _index(index),
        _constructor_row(constructor_row) { }

    index_t meta_method_builder_t::index() const noexcept {
        return _index;
    }

    std::string_view meta_method_builder_t::name() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->handle_row(_index, _constructor_row) : nullptr;
        return (row != nullptr) ? std::string_view(row->name) : std::string_view{};
    }

    std::string_view meta_method_builder_t::signature() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->handle_row(_index, _constructor_row) : nullptr;
        return (row != nullptr) ? std::string_view(row->signature) : std::string_view{};
    }

    std::string_view meta_method_builder_t::return_type() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->handle_row(_index, _constructor_row) : nullptr;
        return (row != nullptr) ? std::string_view(row->return_type) : std::string_view{};
    }

    void meta_method_builder_t::set_return_type(std::string_view type) {
        auto* row = (_owner != nullptr) ? _owner->handle_row(_index, _constructor_row) : nullptr;
        if (row == nullptr) {
            throw_stale_row(name(), _constructor_row);
        }
        // The spelling is built from the base and the cvref the text carried, so "const std::string&"
        // is stored exactly as the reflected path would spell it
        // ("const std::__cxx11::basic_string<char>&").
        qualifier_t qualifier = qualifier_flag::NONE;
        const std::string base = meta_object_builder_t::canonical_base_type(type, &qualifier);
        row->return_type = meta_object_builder_t::spell_type(base, qualifier);
    }

    std::size_t meta_method_builder_t::parameter_count() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->handle_row(_index, _constructor_row) : nullptr;
        return (row != nullptr) ? row->parameters.size() : 0;
    }

    void meta_method_builder_t::set_parameter_names(std::span<const std::string_view> names) {
        auto* row = (_owner != nullptr) ? _owner->handle_row(_index, _constructor_row) : nullptr;
        if (row == nullptr) {
            throw_stale_row(name(), _constructor_row);
        }
        // The names are a column of the same row, so a different length would leave it misaligned:
        // refuse instead of truncating or padding.
        if (names.size() != row->parameters.size()) {
            throw std::invalid_argument("meta_method_builder_t: set_parameter_names() needs one name per parameter (method '"
                + row->name + "' has " + std::to_string(row->parameters.size())
                + ", got " + std::to_string(names.size()) + ")");
        }
        for (std::size_t i = 0; i < names.size(); ++i) {
            row->parameters[i].name = std::string(names[i]);
        }
    }

    void meta_method_builder_t::set_parameter_qualifiers(std::span<const qualifier_t> qualifiers) {
        auto* row = (_owner != nullptr) ? _owner->handle_row(_index, _constructor_row) : nullptr;
        if (row == nullptr) {
            throw_stale_row(name(), _constructor_row);
        }
        if (qualifiers.size() != row->parameters.size()) {
            throw std::invalid_argument("meta_method_builder_t: set_parameter_qualifiers() needs one qualifier per parameter (method '"
                + row->name + "' has " + std::to_string(row->parameters.size())
                + ", got " + std::to_string(qualifiers.size()) + ")");
        }
        for (std::size_t i = 0; i < qualifiers.size(); ++i) {
            row->parameters[i].qualifier = qualifiers[i];
        }
    }

    method_kind_enum meta_method_builder_t::method_kind() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->handle_row(_index, _constructor_row) : nullptr;
        return (row != nullptr) ? row->kind : method_kind_enum::PLAIN_METHOD;
    }

    void meta_method_builder_t::set_method_kind(method_kind_enum kind) {
        auto* row = (_owner != nullptr) ? _owner->handle_row(_index, _constructor_row) : nullptr;
        if (row == nullptr) {
            throw_stale_row(name(), _constructor_row);
        }
        // Nothing to reject: method_kind_enum has no Constructor value any more, and a constructor row —
        // which lives in the builder's constructor space and becomes a detail::constructor_info_t with
        // no kind column — cannot be produced by reclassifying a method row. The value is simply
        // recorded; a STATIC_METHOD row is the caller's declaration (its thunk must ignore `obj`).
        row->kind = kind;
    }

    method_access_enum meta_method_builder_t::access() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->handle_row(_index, _constructor_row) : nullptr;
        return (row != nullptr) ? row->access : method_access_enum::NONE;
    }

    void meta_method_builder_t::set_access(method_access_enum access) {
        auto* row = (_owner != nullptr) ? _owner->handle_row(_index, _constructor_row) : nullptr;
        if (row == nullptr) {
            throw_stale_row(name(), _constructor_row);
        }
        row->access = access;
    }

    qualifier_t meta_method_builder_t::qualifier() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->handle_row(_index, _constructor_row) : nullptr;
        return (row != nullptr) ? row->qualifier : qualifier_t{ qualifier_flag::NONE };
    }

    void meta_method_builder_t::set_qualifier(qualifier_t qualifier) {
        auto* row = (_owner != nullptr) ? _owner->handle_row(_index, _constructor_row) : nullptr;
        if (row == nullptr) {
            throw_stale_row(name(), _constructor_row);
        }
        row->qualifier = qualifier;
    }

    void meta_method_builder_t::set_tag(std::string_view text) {
        auto* row = (_owner != nullptr) ? _owner->handle_row(_index, _constructor_row) : nullptr;
        if (row == nullptr) {
            throw_stale_row(name(), _constructor_row);
        }
        // An empty text clears the tag: the row has no other way to say "no annotation".
        row->tag.assign(text);
    }

    void meta_method_builder_t::set_caller(detail::generic_call caller) {
        auto* row = (_owner != nullptr) ? _owner->handle_row(_index, _constructor_row) : nullptr;
        if (row == nullptr) {
            throw_stale_row(name(), _constructor_row);
        }
        row->caller = caller;
    }
}
