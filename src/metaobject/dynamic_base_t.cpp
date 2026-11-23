/// @file dynamic_base_t.cpp
/// @brief Mixin for classes whose meta-object was built at run time.

#include "hwshqtb/metaobject/dynamic_base_t.h"

namespace hwshqtb::metaobject {
    dynamic_base_t::dynamic_base_t(const meta_object_t* facade) noexcept:
        _facade(facade) { }

    any_t dynamic_base_t::dynamic_value(std::string_view name) const {
        const auto it = _values.find(std::string(name));
        return (it != _values.end()) ? it->second : any_t{ };
    }

    void dynamic_base_t::set_dynamic_value(std::string_view name, const any_t& value) {
        _values[std::string(name)] = value;
    }

    std::vector<std::string> dynamic_base_t::dynamic_value_names() const {
        std::vector<std::string> names;
        names.reserve(_values.size());
        for (const auto& entry : _values) {
            names.push_back(entry.first);
        }
        return names;
    }

    const meta_object_t* dynamic_base_t::facade() const noexcept {
        return _facade;
    }
}