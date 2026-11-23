#ifndef HWSHQTB__METAOBJECT__CHD_HASH_H
#define HWSHQTB__METAOBJECT__CHD_HASH_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace hwshqtb::metaobject::detail {
    constexpr std::uint32_t fnv1a_32(std::span<const char> data) noexcept {
        uint32_t hash = 0x811C9DC5u;
        constexpr uint32_t prime = 0x01000193u;

        for (char b : data) {
            hash ^= static_cast<std::uint32_t>(b);
            hash *= prime;
        }
        return hash;
    }

    constexpr std::uint64_t fnv1a_64(std::span<const char> data) {
        uint64_t hash = 0xCBF29CE484222325ULL;
        const uint64_t prime = 0x100000001B3ULL;

        for (char b : data) {
            hash ^= static_cast<std::uint32_t>(b);
            hash *= prime;
        }
        return hash;
    }

    struct fnv1a_hash_t {
        std::uint64_t prefix = 0;

        constexpr std::uint64_t operator()(std::span<const char> data) const noexcept {
            std::uint64_t hash = 0xCBF29CE484222325ULL;
            hash ^= prefix * 0x9E3779B97F4A7C15ULL;
            hash ^= hash >> 30;
            hash *= 0xBF58476D1CE4E5B9ULL;
            hash ^= hash >> 27;
            hash *= 0x94D049BB133111EBULL;
            hash ^= hash >> 31;

            constexpr std::uint64_t prime = 0x100000001B3ULL;
            for (char b : data) {
                hash ^= static_cast<std::uint8_t>(b);
                hash *= prime;
            }

            hash ^= hash >> 30;
            hash *= 0xBF58476D1CE4E5B9ULL;
            hash ^= hash >> 27;
            hash *= 0x94D049BB133111EBULL;
            hash ^= hash >> 31;

            return hash;
        }
    };

    /// @brief The mixing step every candidate seed is drawn from.
    /// @param x The raw counter.
    /// @return A well-distributed 64-bit value.
    /// @note Deterministic on purpose: a seed is a pure function of the attempt index, so the same
    ///       key list always produces the same table — no time, no addresses, no random_device. That
    ///       is what lets build_meta() be consteval and still reproducible across compilers/builds.
    constexpr std::uint64_t splitmix64(std::uint64_t x) noexcept {
        x += 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    }

    /// @brief How many candidate (seed0, seed1) pairs chd_hash() tries before giving up.
    /// @note Exposed so the "no seed found" diagnostics can name the budget they exhausted, and so
    ///       a test can document it. Attempt i uses splitmix64(2*i) and splitmix64(2*i + 1).
    inline constexpr std::size_t chd_hash_max_attempts = 256;

    /// @brief One built table: the seed pair, the per-bucket displacements and the per-slot redirect.
    ///
    /// Layout: N buckets (one per key) and M = N + N/4 + 1 slots, so the load factor is ~0.8 instead
    /// of 1.0; the slack is what lets a displacement usually be found on the first attempt.
    ///
    /// A lookup computes bucket = h0(key) % N, d = displacements[bucket], slot = (h1(key) % M + d) % M
    /// and answers redirect[slot]. Slots are hashed positions, while the value stored there is the
    /// key's ORIGINAL index in the caller's key list — which is why the dense arrays built from these
    /// lookups come out in construction/declaration order rather than in perfect-hash order.
    ///
    /// A missing key can land on an occupied slot, so a caller MUST compare the stored key before
    /// trusting the hit.
    struct chd_hash_result_t {
        static constexpr std::uint64_t npos = ~std::uint64_t{0};

        /// @brief Seed of the bucket hash h0; npos when the build gave up.
        std::uint64_t seed0 = npos;
        /// @brief Seed of the slot hash h1; npos when the build gave up.
        std::uint64_t seed1 = npos;
        /// @brief One displacement per BUCKET (N entries); std::uint32_t(-1) for an empty bucket.
        std::vector<std::uint32_t> displacements;
        /// @brief One original key index per SLOT (M entries); std::uint32_t(-1) for an empty slot.
        std::vector<std::uint32_t> redirect;
    };

    /// @brief Build a (slack) CHD perfect hash over @p keys.
    /// @param keys The keys to place; duplicates are not checked here (the callers reject them).
    /// @return The seeds, the per-bucket displacements and the per-slot redirect; a default-constructed
    ///         result (npos seeds, empty vectors) when the key set is empty or no seed pair worked.
    /// @note Deterministic: candidate seeds come from splitmix64(2*i)/splitmix64(2*i + 1) for attempt
    ///       i = 0..255, and every step (bucket order, displacement scan) is a pure function of the
    ///       key list, so the same input yields a byte-identical result on every run.
    /// @note Per attempt the work is O(N) hashing plus the displacement scan; with the 0.8 load
    ///       factor the first attempt almost always succeeds.
    constexpr chd_hash_result_t chd_hash(std::span<const std::string_view> keys) {
        const std::size_t N = keys.size();
        if (N == 0) return {};

        // N buckets (one per key) but ~1.25 N slots: the slack is what makes placement succeed.
        const std::size_t M = N + N / 4 + 1;

        std::vector<std::size_t> bucket_of(N);          // h0(key) % N
        std::vector<std::size_t> slot_base(N);          // h1(key) % M
        std::vector<std::size_t> bucket_count(N, 0);
        std::vector<std::size_t> bucket_start(N, 0);
        std::vector<std::size_t> bucket_write(N, 0);
        std::vector<std::size_t> bucket_items(N);
        std::vector<std::size_t> order(N);
        std::vector<std::uint8_t> used(M, 0);
        std::vector<std::size_t> touched;
        std::vector<std::uint32_t> displacements(N, std::uint32_t(-1));
        std::vector<std::uint32_t> redirect(M, std::uint32_t(-1));

        for (std::size_t attempt = 0; attempt < chd_hash_max_attempts; ++attempt) {
            const std::uint64_t seed0 = splitmix64(2 * static_cast<std::uint64_t>(attempt));
            const std::uint64_t seed1 = splitmix64(2 * static_cast<std::uint64_t>(attempt) + 1);

            fnv1a_hash_t h0;
            h0.prefix = seed0;

            std::fill(bucket_count.begin(), bucket_count.end(), 0);
            for (std::size_t pos = 0; pos < N; ++pos) {
                const std::size_t b = static_cast<std::size_t>(h0(keys[pos]) % N);
                bucket_of[pos] = b;
                ++bucket_count[b];
            }

            std::size_t sum = 0, max_count = 0;
            for (std::size_t b = 0; b < N; ++b) {
                bucket_start[b] = sum;
                bucket_write[b] = sum;
                sum += bucket_count[b];
                if (bucket_count[b] > max_count) max_count = bucket_count[b];
            }
            touched.reserve(max_count);

            for (std::size_t pos = 0; pos < N; ++pos) {
                const std::size_t b = bucket_of[pos];
                bucket_items[bucket_write[b]++] = pos;
            }

            // Largest bucket first: the big ones get the empty table, the small ones fill the gaps.
            // The index tie-break keeps the order independent of the sort implementation's tie
            // handling, so the built table is reproducible down to the byte.
            for (std::size_t b = 0; b < N; ++b)
                order[b] = b;
            std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                if (bucket_count[a] != bucket_count[b]) return bucket_count[a] > bucket_count[b];
                return a < b;
            });

            fnv1a_hash_t h1;
            h1.prefix = seed1;

            // Precompute h1(key) % M so the hash is not recomputed inside the d loop.
            for (std::size_t pos = 0; pos < N; ++pos) {
                slot_base[pos] = static_cast<std::size_t>(h1(keys[pos]) % M);
            }

            std::fill(used.begin(), used.end(), 0);
            std::fill(displacements.begin(), displacements.end(), std::uint32_t(-1));
            std::fill(redirect.begin(), redirect.end(), std::uint32_t(-1));

            bool ok = true;
            for (std::size_t b : order) {
                const std::size_t count = bucket_count[b];
                if (count == 0) break;

                const std::size_t start = bucket_start[b];
                bool bucket_ok = false;

                for (std::size_t d = 0; d < M; ++d) {
                    touched.clear();
                    bool cur_ok = true;
                    for (std::size_t k = start; k < start + count; ++k) {
                        const std::size_t pos = bucket_items[k];
                        const std::size_t slot = (slot_base[pos] + d) % M;
                        if (used[slot]) {
                            cur_ok = false;
                            break;
                        }
                        used[slot] = 1;
                        touched.push_back(slot);
                    }
                    if (cur_ok) {
                        // Commit the displacement and the slot -> ORIGINAL index mapping for every
                        // key of the bucket; find() answers with redirect.
                        displacements[b] = static_cast<std::uint32_t>(d);
                        for (std::size_t k = start; k < start + count; ++k) {
                            const std::size_t pos = bucket_items[k];
                            const std::size_t slot = (slot_base[pos] + d) % M;
                            redirect[slot] = static_cast<std::uint32_t>(pos);
                        }
                        bucket_ok = true;
                        break;
                    }
                    for (std::size_t slot : touched)
                        used[slot] = 0;
                }

                if (!bucket_ok) {
                    ok = false;
                    break;
                }
            }

            if (ok) {
                return chd_hash_result_t{
                    seed0, seed1, std::move(displacements), std::move(redirect)
                };
            }
        }
        return {};
    }

    /// @brief Look @p key up in a built table.
    /// @return The key's original index in the list the table was built from; npos when the table is
    ///         empty/unbuilt, when the bucket has no displacement, or when the slot is empty.
    /// @note A hit is not proof that @p key is in the table: a missing key can land on an occupied
    ///       slot, so callers compare the stored key (they all do). npos is always a miss.
    constexpr std::uint64_t find(const chd_hash_result_t& r, std::string_view key) {
        if (r.seed0 == chd_hash_result_t::npos || r.seed1 == chd_hash_result_t::npos)
            return chd_hash_result_t::npos;

        const std::size_t buckets = r.displacements.size();
        const std::size_t slots = r.redirect.size();
        if (buckets == 0 || slots == 0) return chd_hash_result_t::npos;

        fnv1a_hash_t h0;
        h0.prefix = r.seed0;
        fnv1a_hash_t h1;
        h1.prefix = r.seed1;

        const std::span<const char> view(key.data(), key.size());

        const std::size_t bucket = static_cast<std::size_t>(h0(view) % buckets);

        const std::uint32_t d = r.displacements[bucket];
        if (d == std::uint32_t(-1)) return chd_hash_result_t::npos;

        const std::size_t slot = (static_cast<std::size_t>(h1(view) % slots) + d) % slots;
        const std::uint32_t pos = r.redirect[slot];
        if (pos == std::uint32_t(-1)) return chd_hash_result_t::npos;

        return pos;
    }
}

#endif
