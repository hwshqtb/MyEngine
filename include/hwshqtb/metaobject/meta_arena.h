#ifndef HWSHQTB__METAOBJECT__META_ARENA_H
#define HWSHQTB__METAOBJECT__META_ARENA_H

/// @file meta_arena.h
/// @brief Stable storage for dynamically built meta-information.

#include "export.h"
#include "meta_info_t.h"
#include "meta_object_t.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace hwshqtb::metaobject::detail {
    /// @brief Process-wide owner of every value a dynamically built meta-object points at.
    ///
    /// The tables inside meta_info_t are pure views — spans plus hash tables, no ownership — so a
    /// dynamic meta-object needs a place whose addresses stay put AND whose arrays are contiguous.
    /// The finished meta_object_t (which owns its meta_info_t by value) is parked here too, so every
    /// handle taken from the returned pointer stays valid for the rest of the process.
    ///
    /// Layout: a deque of vectors. The deque keeps the vector objects stable while it grows, and each
    /// vector owns one heap block that never moves, so a span over a stored array stays valid for the
    /// rest of the process.
    ///
    /// @par Lifetime
    /// There is exactly ONE arena per process (meta_arena::instance()), and it is never destroyed:
    /// a dynamically built meta_object_t parked here stays valid for the rest of the process,
    /// including during static destruction (a static object holding a dynamic_base_t may be torn down
    /// after any ordinary static). Nothing owns the arena — not object_t, not a tree — so
    /// re-parenting or destroying the tree an object lived in cannot invalidate a built meta-object.
    ///
    /// @par Thread safety
    /// Every mutating operation (intern(), add_array(), add()) takes the internal mutex for exactly
    /// the length of that one operation. build() is deliberately NOT locked as a whole: each build
    /// allocates independent pieces and only add() publishes the finished meta-object, so two
    /// builders may interleave freely. A returned view points into std::deque storage, and a deque
    /// never relocates an element it already holds when it grows, so the lock can be released as soon
    /// as the view has been formed and the view stays valid for the arena's lifetime.
    class meta_arena {
    public:
        /// @brief Construct an empty arena.
        /// @note The arena owns stable storage: copying or moving it would break every pointer and
        ///       span already handed out (and meta_object_t itself is non-copyable / non-movable), so
        ///       all four operations are deleted. It is reached through meta_arena::instance(),
        ///       which allocates it once and never frees it.
        meta_arena() = default;
        meta_arena(const meta_arena&) = delete;
        meta_arena& operator=(const meta_arena&) = delete;
        meta_arena(meta_arena&&) = delete;
        meta_arena& operator=(meta_arena&&) = delete;
        ~meta_arena() = default;

        /// @brief The process-wide arena for dynamically built meta-objects (the run-time arena).
        /// @return The same arena on every call for as long as the process lives.
        /// @note Never destroyed on purpose: a dynamically built meta_object_t parked here must stay
        ///       valid for the rest of the process, including during static destruction (a static
        ///       object holding a dynamic_base_t may be destroyed after any ordinary static).
        /// @note Defined out of line in exactly one translation unit so the shared build has ONE
        ///       instance for the DLL and every consumer.
        /// @note Named instance(): the header defines exactly ONE arena, used only for dynamically
        ///       built meta-objects (a class's compile-time table lives in its own static_meta_object
        ///       and is not arena storage), so there is no compile-time/run-time arena pair to
        ///       distinguish. This also matches the other process-wide singletons
        ///       (event_dispatcher::instance(), core_application::instance()).
        /// @note meta_arena is not class-exported (it is an implementation detail), so the accessor
        ///       carries HWSHQTB_EXPORT itself.
        static HWSHQTB_EXPORT meta_arena& instance() noexcept;

        /// @brief Copy a string into the arena.
        /// @param s The text to copy.
        /// @return A view of the stored copy; valid for the arena's lifetime.
        /// @note Thread safe. The lock only covers the copy: the deque keeps the std::string object
        ///       in place and the string owns its own buffer, so the view cannot be invalidated by a
        ///       later intern() once it has been handed out.
        std::string_view intern(std::string_view s);

        /// @brief Store one array and return a contiguous view of it.
        /// @param items The elements to copy.
        /// @return A span over the stored copy; valid for the arena's lifetime.
        /// @note Thread safe. The lock only covers the copy: the deque element (the vector) and the
        ///       vector's heap block both stay where they are when the deque grows, so releasing the
        ///       lock right after the span is formed is safe.
        template <typename T>
        std::span<const T> add_array(std::span<const T> items) {
            const std::lock_guard<std::mutex> lock(_mutex);
            auto& slot = array_slot<T>();
            slot.emplace_back(items.begin(), items.end());
            const auto& stored = slot.back();
            return std::span<const T>(stored.data(), stored.size());
        }

        /// @brief Build one meta-object in place (the type is neither copyable nor movable).
        /// @param info The table the meta-object takes ownership of.
        /// @param super The base class's meta-object, or nullptr for a root class.
        /// @return A pointer to the stored meta-object; valid for the arena's lifetime.
        /// @note A deque element address is stable, so the pointer — and every handle taken from it —
        ///       stays valid while the arena grows. The element is CONSTRUCTED IN PLACE, which is what
        ///       lets meta_object_t stay non-copyable and non-movable: a deque never relocates its
        ///       existing elements, so no move constructor is ever needed.
        /// @note Thread safe, and this is the publication point: nothing outside sees the new
        ///       meta-object until the emplace_back has completed. The lock therefore only has to
        ///       cover this one call, not the build that prepared `info`.
        meta_object_t* add(meta_info_t info, const meta_object_t* super);

        /// @brief Number of stored meta-objects (diagnostics and tests).
        /// @note Thread safe. Not noexcept: taking the mutex can throw std::system_error, and a
        ///       diagnostic is not worth a std::terminate.
        std::size_t meta_object_count() const;

    private:
        /// @brief The deque backing arrays of type T.
        /// @note The two callable row types get a pool EACH: method_info_t rows and constructor_info_t
        ///       rows are unrelated types, so their arrays cannot share one deque — that independence
        ///       is what keeps a method table and a constructor table from ever being the same storage.
        template <typename T>
        std::deque<std::vector<T>>& array_slot() {
            if constexpr (std::is_same_v<T, method_info_t>) {
                return _methods;
            }
            else if constexpr (std::is_same_v<T, constructor_info_t>) {
                return _constructors;
            }
            else if constexpr (std::is_same_v<T, property_info_t>) {
                return _properties;
            }
            else if constexpr (std::is_same_v<T, enum_info_t>) {
                return _enums;
            }
            else if constexpr (std::is_same_v<T, enum_entry_t>) {
                return _enum_entries;
            }
            else if constexpr (std::is_same_v<T, class_info_t>) {
                return _class_infos;
            }
            else if constexpr (std::is_same_v<T, compiling_string_t>) {
                return _param_types;
            }
            else if constexpr (std::is_same_v<T, std::uint32_t>) {
                return _hash_words;
            }
            else {
                static_assert(sizeof(T) == 0, "meta_arena::add_array: unsupported element type");
            }
        }

        /// @brief Guards every deque below (one lock per mutating operation).
        /// @note `mutable` so the const query meta_object_count() can take it too. The arena is
        ///       never destroyed, so this mutex is never destroyed either.
        mutable std::mutex _mutex;

        /// @brief Interned strings.
        std::deque<std::string> _strings;
        /// @brief METHOD-row arrays (detail::method_info_t): the method table's rows.
        std::deque<std::vector<method_info_t>> _methods;
        /// @brief CONSTRUCTOR-row arrays (detail::constructor_info_t): the constructor table's rows.
        /// @note A pool of its own, beside _methods, exactly like the two tables it feeds: a
        ///       constructor row is a different type, so it cannot share the method rows' storage.
        std::deque<std::vector<constructor_info_t>> _constructors;
        /// @brief Property arrays.
        std::deque<std::vector<property_info_t>> _properties;
        /// @brief Enum arrays.
        std::deque<std::vector<enum_info_t>> _enums;
        /// @brief Enumerator arrays.
        std::deque<std::vector<enum_entry_t>> _enum_entries;
        /// @brief Class-info arrays.
        std::deque<std::vector<class_info_t>> _class_infos;
        /// @brief Parameter-type arrays (referenced by method_info_t::param_types and
        ///        constructor_info_t::param_types).
        std::deque<std::vector<compiling_string_t>> _param_types;
        /// @brief Hash-table word arrays (displacements and redirects, both uint32).
        std::deque<std::vector<std::uint32_t>> _hash_words;
        /// @brief Built meta-objects (each one owns its table and links the chain by pointer).
        std::deque<meta_object_t> _meta_objects;
    };

}

#endif
