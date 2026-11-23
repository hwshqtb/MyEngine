/// @file meta_class_info_t.cpp
/// @brief A class-level key/value pair (the QMetaClassInfo equivalent).

#include "hwshqtb/metaobject/meta_class_info_t.h"

#include "hwshqtb/metaobject/meta_object_t.h"

#include <span>

namespace hwshqtb::metaobject {
    const detail::class_info_t* meta_class_info_t::aimed_info() const noexcept {
        // The slot guard is meta_object_t::row_at, shared with the method and enum handles.
        return meta_object_t::row_at(_owner, _index, &meta_info_t::class_info_infos);
    }

    meta_class_info_t::meta_class_info_t(const meta_object_t* owner, index_t index) noexcept:
        _owner(owner),
        _index(index) { }

    bool meta_class_info_t::is_valid() const noexcept {
        return aimed_info() != nullptr;
    }

    std::string_view meta_class_info_t::name() const {
        const detail::class_info_t* row = aimed_info();
        return (row != nullptr) ? static_cast<std::string_view>(row->name) : std::string_view{ };
    }

    std::string_view meta_class_info_t::value() const {
        const detail::class_info_t* row = aimed_info();
        return (row != nullptr) ? static_cast<std::string_view>(row->value) : std::string_view{ };
    }

    index_t meta_class_info_t::index() const noexcept {
        // The stored slot is local to the DECLARING meta-object; adding its ancestors' counts back
        // reproduces the coordinate class_info(index) accepts. The same rule meta_method_t::index()
        // uses, so every handle answers in one index space.
        if (aimed_info() == nullptr) {
            return index_npos;
        }
        index_t offset = 0;
        for (const meta_object_t* base = _owner->super_class(); base != nullptr; base = base->super_class()) {
            offset += static_cast<index_t>(base->info().class_info_infos.to_span().size());
        }
        return offset + _index;
    }

    const meta_object_t* meta_class_info_t::owner() const noexcept {
        return _owner;
    }
}
