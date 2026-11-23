#ifndef HWSHQTB__UI__IM_OBJECT_POOL_T_H
#define HWSHQTB__UI__IM_OBJECT_POOL_T_H

/// @file im_object_pool_t.h
/// @brief im_object_pool_t: a reuse pool of immediate controls keyed by object id.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hwshqtb/metaobject/object_pointer_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/ui/export.h"

namespace hwshqtb::ui {
    /// @brief A reuse pool of objects identified by id, kept alive by recent use.
    ///
    /// @par Reuse only
    /// The pool saves construction: take() answers the live object registered under an id, or builds
    /// one with the supplied factory. It holds no state whatsoever — everything a control must
    /// remember between passes lives in im_state_store_t. Dropping the whole pool (clear()) or
    /// collecting every row therefore costs speed and nothing else; a later take() builds a fresh
    /// object whose state is restored from the store.
    ///
    /// @par Ownership
    /// The pool is a lookup table, never an owner: an object is held by the meta-object tree (its
    /// parent), and the pool only stores a guarded pointer to it. A row whose object died is dropped
    /// on the next lookup or sweep without touching the object. collect() hands the object back
    /// instead of deleting it, because the caller owns that decision.
    ///
    /// @par The clock is the caller's
    /// take() and collect() take the pass number as an argument and the pool keeps no counter of its
    /// own, so the lifetime policy is decided outside. Calling take() with a clock that does not
    /// advance keeps every entry; collect() compares the argument with the stored numbers only.
    class HWSHQTB_UI_EXPORT im_object_pool_t {
    public:
        /// @brief Construct an empty pool.
        im_object_pool_t() = default;
        /// @brief Destroy the pool; the registered objects are not deleted (the pool owns none).
        ~im_object_pool_t() = default;

        im_object_pool_t(const im_object_pool_t&) = delete;
        im_object_pool_t& operator=(const im_object_pool_t&) = delete;
        im_object_pool_t(im_object_pool_t&&) = delete;
        im_object_pool_t& operator=(im_object_pool_t&&) = delete;

        /// @brief Take the live object registered under @p id, or create and register one.
        /// @tparam factory_t Callable returning object_t*; invoked only when no live object is
        ///         registered under @p id.
        /// @param id The name that identifies the object.
        /// @param pass The caller's pass number; the row records it as its last use.
        /// @param factory The creation callback; it may return nullptr, in which case nothing is
        ///        registered and nullptr is returned.
        /// @return The live object, or nullptr when @p factory failed.
        /// @note A type rule for "the same id created as different types" is the CALLER's: the pool
        ///       returns whatever object is already there and never compares types.
        /// @note A row whose object already died is replaced by the new object, so the id is not
        ///       poisoned by a dead entry.
        template <typename factory_t>
        ::hwshqtb::metaobject::object_t* take(std::string_view id, std::uint64_t pass,
                                              factory_t&& factory) {
            if (::hwshqtb::metaobject::object_t* const existing = find(id)) {
                touch(id, pass);
                return existing;
            }
            ::hwshqtb::metaobject::object_t* const created =
                std::forward<factory_t>(factory)();
            if (created == nullptr) {
                return nullptr;
            }
            _entries.insert_or_assign(std::string(id), entry_t{ created, pass });
            return created;
        }

        /// @brief The live object registered under @p id, or nullptr.
        /// @param id The name to look up.
        /// @note A row whose object died answers nullptr even though it is still listed by size()
        ///       until a sweep removes it.
        /// @note This query does not mark the row as used; only take() advances the clock.
        ::hwshqtb::metaobject::object_t* find(std::string_view id) const;

        /// @brief Whether @p object is a live registered object (by identity, not by id).
        /// @param object The object to look for.
        /// @note Linear in the number of rows: it exists for the query side of a caller that holds a
        ///       pointer but not the id.
        bool contains(const ::hwshqtb::metaobject::object_t& object) const noexcept;

        /// @brief Record that @p id was used in pass @p pass.
        /// @param id The name that was visited; an unknown id is ignored.
        /// @param pass The caller's pass number; the pool never invents one.
        void touch(std::string_view id, std::uint64_t pass);

        /// @brief Unregister the rows that were not touched within @p max_idle_passes.
        /// @param pass The current caller pass.
        /// @param max_idle_passes The idle limit. A row is idle when
        ///        `pass - last_seen_pass >= max_idle_passes`; 0 removes every row that was not
        ///        touched in the pass being collected.
        /// @return The live objects whose rows were removed. The pool does NOT delete them: the
        ///         caller owns that decision (and any cleanup the object needs). The state stored
        ///         under their ids is unaffected.
        /// @note A row whose object has already died is removed too but is not returned (there is
        ///       nothing to hand back).
        /// @note The comparison uses @p pass, never an internal counter; a @p pass smaller than a
        ///       row's last touch keeps that row.
        std::vector<::hwshqtb::metaobject::object_t*> collect(std::uint64_t pass,
                                                              std::uint32_t max_idle_passes);

        /// @brief The number of registered rows (dead rows included until swept).
        std::size_t size() const noexcept;

        /// @brief Unregister every row; the objects are left alive and untouched.
        void clear();

        /// @brief Visit every live registered object.
        /// @tparam visitor_t Callable as `void(std::string_view id, object_t& object)`.
        /// @param visitor The callback; iteration order is unspecified and a dead row is skipped.
        /// @note The visitor must not create or remove rows (it would invalidate the iteration).
        template <typename visitor_t>
        void for_each(visitor_t&& visitor) const {
            for (const auto& [id, entry] : _entries) {
                ::hwshqtb::metaobject::object_t* const object = entry.object.data();
                if (object != nullptr) {
                    visitor(std::string_view(id), *object);
                }
            }
        }

    private:
        /// @brief One registered object and the pass it was last taken in.
        struct entry_t {
            /// @brief The registered object, guarded against deletion elsewhere.
            ::hwshqtb::metaobject::object_pointer_t<::hwshqtb::metaobject::object_t> object;
            /// @brief The caller pass number of the last use.
            std::uint64_t last_seen_pass = 0;
        };

        /// @brief Transparent hash, so string_view lookups allocate no key.
        struct key_hash_t {
            using is_transparent = void;

            /// @brief Hash one spelling of an id.
            std::size_t operator()(std::string_view text) const noexcept {
                return std::hash<std::string_view>{}(text);
            }
        };

        /// @brief Transparent equality, so string_view lookups allocate no key.
        struct key_equal_t {
            using is_transparent = void;

            /// @brief Compare two spellings of an id.
            bool operator()(std::string_view left, std::string_view right) const noexcept {
                return left == right;
            }
        };

        /// @brief id -> entry.
        std::unordered_map<std::string, entry_t, key_hash_t, key_equal_t> _entries;
    };
}

#endif
