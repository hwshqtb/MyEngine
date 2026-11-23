/// @file meta_arena.cpp
/// @brief The process-wide arena for dynamically built meta-objects.

#include <hwshqtb/metaobject/meta_arena.h>

namespace hwshqtb::metaobject::detail {
    std::string_view meta_arena::intern(std::string_view s) {
        const std::lock_guard<std::mutex> lock(_mutex);
        _strings.emplace_back(s);
        return _strings.back();
    }

    meta_object_t* meta_arena::add(meta_info_t info, const meta_object_t* super) {
        const std::lock_guard<std::mutex> lock(_mutex);
        return &_meta_objects.emplace_back(std::move(info), super);
    }

    std::size_t meta_arena::meta_object_count() const {
        const std::lock_guard<std::mutex> lock(_mutex);
        return _meta_objects.size();
    }

    meta_arena& meta_arena::instance() noexcept {
        // Deliberately never deleted: a dynamically built meta_object_t parked in the arena must stay
        // valid for the rest of the process, and a static object holding a dynamic_base_t may be
        // destroyed after any ordinary static. This heap allocation is therefore the ONE owner, and
        // its lifetime is "longer than every object that can point into it".
        //
        // Function-local static: exactly one initialisation per process, thread safe by C++11 magic
        // statics; out of line here so the shared build has one instance for the DLL and every
        // consumer that calls this accessor.
        static auto* arena = new meta_arena();
        return *arena;
    }
}
