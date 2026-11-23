/// @file type_interface_t.cpp
/// @brief Interface identity comparison shared by every module.

#include "hwshqtb/metaobject/type_interface_t.h"

namespace hwshqtb::metaobject::detail {
    bool same_type(const type_interface_t* lhs, const type_interface_t* rhs) noexcept {
        if (lhs == nullptr || rhs == nullptr) {
            return false;
        }
        if (lhs == rhs) {
            return true;
        }
        const index_t lhs_id = interface_id(lhs);
        const index_t rhs_id = interface_id(rhs);
        if (lhs_id != 0 && rhs_id != 0) {
            return lhs_id == rhs_id;
        }
        return interface_name(lhs) == interface_name(rhs);
    }
}
