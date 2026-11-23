/// @file meta_enum_builder_t.cpp
/// @brief The mutable handle of one run-time built enumeration (QMetaEnumBuilder equivalent).

#include "hwshqtb/metaobject/meta_enum_builder_t.h"

#include "hwshqtb/metaobject/meta_object_builder_t.h"

#include <stdexcept>
#include <string>

namespace hwshqtb::metaobject {
    meta_enum_builder_t::meta_enum_builder_t(meta_object_builder_t* owner, index_t index) noexcept:
        _owner(owner),
        _index(index) { }

    index_t meta_enum_builder_t::index() const noexcept {
        return _index;
    }

    std::string_view meta_enum_builder_t::name() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->enumerator_row(_index) : nullptr;
        return (row != nullptr) ? std::string_view(row->name) : std::string_view{};
    }

    bool meta_enum_builder_t::is_flag() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->enumerator_row(_index) : nullptr;
        return row != nullptr && row->is_flag;
    }

    void meta_enum_builder_t::set_is_flag(bool is_flag) {
        auto* row = (_owner != nullptr) ? _owner->enumerator_row(_index) : nullptr;
        if (row == nullptr) {
            throw std::invalid_argument(detail::stale_row_message("meta_enum_builder_t", name(), "remove_enumerator"));
        }
        row->is_flag = is_flag;
    }

    void meta_enum_builder_t::add_key(std::string_view name, std::int64_t value) {
        auto* row = (_owner != nullptr) ? _owner->enumerator_row(_index) : nullptr;
        if (row == nullptr) {
            // this->name(): the parameter above shadows the handle's accessor.
            throw std::invalid_argument(detail::stale_row_message("meta_enum_builder_t", this->name(), "remove_enumerator"));
        }
        row->keys.emplace_back(std::string(name), value);
    }

    std::size_t meta_enum_builder_t::key_count() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->enumerator_row(_index) : nullptr;
        return (row != nullptr) ? row->keys.size() : 0;
    }

    std::string_view meta_enum_builder_t::key(std::size_t index) const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->enumerator_row(_index) : nullptr;
        if (row == nullptr || index >= row->keys.size()) {
            return {};
        }
        return std::string_view(row->keys[index].first);
    }

    std::int64_t meta_enum_builder_t::value(std::size_t index) const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->enumerator_row(_index) : nullptr;
        if (row == nullptr || index >= row->keys.size()) {
            return 0;
        }
        return row->keys[index].second;
    }

    void meta_enum_builder_t::remove_key(std::size_t index) {
        auto* row = (_owner != nullptr) ? _owner->enumerator_row(_index) : nullptr;
        if (row == nullptr) {
            throw std::invalid_argument(detail::stale_row_message("meta_enum_builder_t", name(), "remove_enumerator"));
        }
        if (index >= row->keys.size()) {
            return;                            // out of range is a no-op, as documented
        }
        row->keys.erase(row->keys.begin() + static_cast<std::ptrdiff_t>(index));
    }
}
