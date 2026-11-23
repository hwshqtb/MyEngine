/// @file im_object_pool_t.cpp
/// @brief im_object_pool_t: lookup, use marking and the idle sweep (see im_object_pool_t.h).

#include "hwshqtb/ui/im_object_pool_t.h"

#include <string>
#include <string_view>
#include <vector>

namespace hwshqtb::ui {
    ::hwshqtb::metaobject::object_t* im_object_pool_t::find(std::string_view id) const {
        const auto found = _entries.find(id);
        if (found == _entries.end()) {
            return nullptr;
        }
        return found->second.object.data();
    }

    bool im_object_pool_t::contains(const ::hwshqtb::metaobject::object_t& object) const noexcept {
        for (const auto& entry : _entries) {
            if (entry.second.object.data() == &object) {
                return true;
            }
        }
        return false;
    }

    void im_object_pool_t::touch(std::string_view id, std::uint64_t pass) {
        const auto found = _entries.find(id);
        if (found != _entries.end()) {
            found->second.last_seen_pass = pass;
        }
    }

    std::vector<::hwshqtb::metaobject::object_t*> im_object_pool_t::collect(
        std::uint64_t pass, std::uint32_t max_idle_passes) {
        std::vector<::hwshqtb::metaobject::object_t*> dropped;
        for (auto current = _entries.begin(); current != _entries.end();) {
            ::hwshqtb::metaobject::object_t* const object = current->second.object.data();
            bool idle = false;
            if (object != nullptr && pass >= current->second.last_seen_pass) {
                // 0 means "not touched in the pass being collected": a touched row has a zero
                // distance too, so it needs the strict comparison; otherwise the usual rule applies.
                idle = max_idle_passes == 0
                    ? pass > current->second.last_seen_pass
                    : (pass - current->second.last_seen_pass) >= max_idle_passes;
            }
            if (object == nullptr) {
                current = _entries.erase(current); // the object died: the row has nothing to hand back
            }
            else if (idle) {
                dropped.push_back(object);
                current = _entries.erase(current);
            }
            else {
                ++current;
            }
        }
        return dropped;
    }

    std::size_t im_object_pool_t::size() const noexcept {
        return _entries.size();
    }

    void im_object_pool_t::clear() {
        _entries.clear();
    }
}
