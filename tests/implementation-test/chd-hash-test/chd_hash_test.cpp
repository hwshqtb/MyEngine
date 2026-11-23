// Correctness, determinism and cost probe for the compile-time perfect hash used by the meta-info
// tables.
//
// Covered here:
//  - the sampled 64-bit seed search (attempt i = splitmix64(2i)/splitmix64(2i+1), 256 attempts),
//  - the 1.25x slot slack (M = N + N/4 + 1) and the per-slot redirect array,
//  - every key resolving to its ORIGINAL index (not to a perfect-hash slot),
//  - byte-identical rebuilding (same key list -> same table, independent of where the keys live),
//  - a deliberately hostile key list at N=500 (shared long prefixes, keys differing in one char).
//
// A note on absent keys, because it shapes the checks below: a CHD table stores no keys, so a key
// that was never inserted can compute a bucket that HAS a displacement and a slot that IS occupied —
// the lookup then answers some other key's index. npos is always a miss, but a hit is only a
// candidate. Every call site in the library therefore compares the stored key after the lookup, and
// so does this test ("verified miss"). The strictly-required npos case is checked separately: a
// bucket without a displacement and a free slot must both answer npos.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "hwshqtb/metaobject/chd_hash.hpp"

using namespace hwshqtb::metaobject::detail;

namespace chd_hash_test {

int g_failures = 0;
std::size_t g_checks = 0;

/// Number of slots the build must produce for @p n keys (the slack is part of the contract).
std::size_t slot_count(std::size_t n) {
    return n + n / 4 + 1;
}

std::vector<std::string_view> view_of(const std::vector<std::string>& keys) {
    std::vector<std::string_view> views;
    views.reserve(keys.size());
    for (const auto& k : keys) views.emplace_back(k);
    return views;
}

/// Build the table for @p keys and verify every invariant the callers rely on.
/// @return true when the build and all checks succeeded.
bool exercise(const std::vector<std::string>& keys, const char* label, bool print) {
    const std::vector<std::string_view> views = view_of(keys);
    const std::size_t N = keys.size();
    const std::size_t M = slot_count(N);

    const auto start = std::chrono::steady_clock::now();
    const chd_hash_result_t table = chd_hash(views);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);

    // ---- empty key set: no seeds, no vectors, and a lookup that cannot divide by zero ----
    if (N == 0) {
        ++g_checks;
        if (!table.displacements.empty() || !table.redirect.empty()
            || table.seed0 != chd_hash_result_t::npos || table.seed1 != chd_hash_result_t::npos) {
            std::printf("  [FAIL] %s: empty key set did not give an empty result\n", label);
            ++g_failures;
            return false;
        }
        ++g_checks;
        if (find(table, "anything") != chd_hash_result_t::npos) {
            std::printf("  [FAIL] %s: find() on the empty table did not answer npos\n", label);
            ++g_failures;
            return false;
        }
        std::printf("  ok   %-26s N=0    empty result, find()=npos\n", label);
        return true;
    }

    // ---- structural invariants ----
    bool ok = true;
    ++g_checks;
    if (table.seed0 == chd_hash_result_t::npos || table.seed1 == chd_hash_result_t::npos) {
        std::printf("  [FAIL] %s N=%zu: chd_hash gave up (no seed pair found in %zu attempts)\n", label,
                    N, chd_hash_max_attempts);
        ++g_failures;
        return false;
    }
    ++g_checks;
    if (table.displacements.size() != N) {
        std::printf("  [FAIL] %s N=%zu: %zu displacements for N buckets\n", label, N,
                    table.displacements.size());
        ok = false;
    }
    ++g_checks;
    if (table.redirect.size() != M) {
        std::printf("  [FAIL] %s N=%zu: %zu redirect entries, expected M=%zu\n", label, N,
                    table.redirect.size(), M);
        ok = false;
    }
    if (!ok) {
        ++g_failures;
        return false;
    }

    // ---- every key resolves to its own ORIGINAL index, and those answers are distinct ----
    std::size_t wrong = 0;
    std::vector<std::uint8_t> seen(N, 0);
    std::size_t out_of_range = 0;
    for (std::size_t i = 0; i < N; ++i) {
        ++g_checks;
        const std::uint64_t pos = find(table, views[i]);
        if (pos != i) {
            if (wrong < 3) {
                std::printf("  [FAIL] %s N=%zu: key #%zu resolves to %llu\n", label, N, i,
                            static_cast<unsigned long long>(pos));
            }
            ++wrong;
            continue;
        }
        ++seen[static_cast<std::size_t>(pos)];
    }
    for (std::size_t i = 0; i < N; ++i) {
        ++g_checks;
        if (seen[i] != 1) ++out_of_range;
    }
    if (wrong != 0 || out_of_range != 0) {
        std::printf("  [FAIL] %s N=%zu: %zu wrong positions, %zu slots not hit once\n", label, N,
                    wrong, out_of_range);
        g_failures += static_cast<int>(wrong + out_of_range);
        ok = false;
    }

    // ---- redirect shape: exactly N occupied slots, each pointing at a real key ----
    std::size_t occupied = 0, bad_redirect = 0;
    for (std::uint32_t pos : table.redirect) {
        if (pos == std::uint32_t(-1)) continue;
        ++occupied;
        if (static_cast<std::size_t>(pos) >= N) ++bad_redirect;
    }
    ++g_checks;
    if (occupied != N || bad_redirect != 0) {
        std::printf("  [FAIL] %s N=%zu: %zu occupied slots (expected %zu), %zu out of range\n", label,
                    N, occupied, N, bad_redirect);
        ++g_failures;
        ok = false;
    }

    // ---- absent keys: the two "npos is mandatory" branches must answer npos, and a hit may only
    //      ever point at a DIFFERENT key (the verified miss every call site performs) ----
    const std::size_t probe_count = N < 64 ? 16 : N / 4;
    std::size_t guarded_probes = 0, empty_bucket_probes = 0, free_slot_probes = 0;
    std::size_t occupied_slot_probes = 0, false_hits = 0;
    fnv1a_hash_t h0;
    h0.prefix = table.seed0;
    fnv1a_hash_t h1;
    h1.prefix = table.seed1;
    for (std::size_t p = 0; p < probe_count; ++p) {
        const std::string probe = label + std::string("_absent_") + std::to_string(p);
        const std::span<const char> view(probe.data(), probe.size());
        const std::uint64_t pos = find(table, probe);

        const std::size_t bucket = static_cast<std::size_t>(h0(view) % N);
        const std::uint32_t d = table.displacements[bucket];
        bool mandatory_npos = false;
        bool free_slot = false;
        if (d == std::uint32_t(-1)) {
            mandatory_npos = true;
            ++empty_bucket_probes;
        }
        else {
            const std::size_t slot = (static_cast<std::size_t>(h1(view) % M) + d) % M;
            if (table.redirect[slot] == std::uint32_t(-1)) {
                mandatory_npos = true;
                free_slot = true;
                ++free_slot_probes;
            }
        }

        ++g_checks;
        if (mandatory_npos) {
            ++guarded_probes;
            if (pos != chd_hash_result_t::npos) {
                std::printf("  [FAIL] %s N=%zu: '%s' hit an empty %s but find() answered %llu\n",
                            label, N, probe.c_str(), free_slot ? "slot" : "bucket",
                            static_cast<unsigned long long>(pos));
                ++g_failures;
                ok = false;
            }
            continue;
        }

        // Occupied slot: the answer must be a real key and must NOT be the probe.
        ++occupied_slot_probes;
        if (pos != chd_hash_result_t::npos
            && (static_cast<std::size_t>(pos) >= N
                || keys[static_cast<std::size_t>(pos)] == probe)) {
            ++false_hits;
        }
    }
    ++g_checks;
    if (false_hits != 0) {
        std::printf("  [FAIL] %s N=%zu: %zu absent probes looked like a real hit\n", label, N,
                    false_hits);
        ++g_failures;
        ok = false;
    }

    if (print) {
        std::printf("  ok   %-26s N=%-4zu M=%-5zu load=%.3f probes=%zu: %zu / %zu guarded "
                    "(%zu empty bucket, %zu free slot), %zu occupied slot (verified miss), "
                    "build=%lldus\n",
                    label, N, M, double(occupied) / double(M), probe_count, guarded_probes,
                    probe_count, empty_bucket_probes, free_slot_probes, occupied_slot_probes,
                    static_cast<long long>(elapsed.count()));
    }
    return ok;
}

std::vector<std::string> sequential(std::size_t n, const char* prefix) {
    std::vector<std::string> keys;
    keys.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        keys.push_back(std::string(prefix) + std::to_string(i));
    }
    return keys;
}

/// Same key list, two independent copies (so the strings live at different addresses): the built
/// table must be byte-identical, which is what "deterministic, no time, no addresses" means.
bool determinism(const std::vector<std::string>& keys) {
    const std::vector<std::string> copy = keys;      // separate storage, same contents
    const chd_hash_result_t a = chd_hash(view_of(keys));
    const chd_hash_result_t b = chd_hash(view_of(copy));

    ++g_checks;
    const bool seeds_equal = a.seed0 == b.seed0 && a.seed1 == b.seed1;
    if (!seeds_equal) {
        std::printf("  [FAIL] determinism: seeds differ (%llu/%llu vs %llu/%llu)\n",
                    static_cast<unsigned long long>(a.seed0),
                    static_cast<unsigned long long>(a.seed1),
                    static_cast<unsigned long long>(b.seed0),
                    static_cast<unsigned long long>(b.seed1));
    }
    ++g_checks;
    const bool disp_equal = a.displacements.size() == b.displacements.size()
        && (a.displacements.empty()
            || std::memcmp(a.displacements.data(), b.displacements.data(),
                           a.displacements.size() * sizeof(std::uint32_t)) == 0);
    if (!disp_equal) std::printf("  [FAIL] determinism: displacements differ\n");
    ++g_checks;
    const bool redirect_equal = a.redirect.size() == b.redirect.size()
        && (a.redirect.empty()
            || std::memcmp(a.redirect.data(), b.redirect.data(),
                           a.redirect.size() * sizeof(std::uint32_t)) == 0);
    if (!redirect_equal) std::printf("  [FAIL] determinism: redirect differs\n");
    ++g_checks;
    const bool seeds_set = a.seed0 != chd_hash_result_t::npos && a.seed1 != chd_hash_result_t::npos;
    if (!seeds_set) std::printf("  [FAIL] determinism: build gave up\n");

    if (!seeds_equal || !disp_equal || !redirect_equal || !seeds_set) {
        ++g_failures;
        return false;
    }
    std::printf("  ok   %-26s N=%zu  two builds byte-identical (seed0=%llu seed1=%llu)\n",
                "determinism", keys.size(), static_cast<unsigned long long>(a.seed0),
                static_cast<unsigned long long>(a.seed1));
    return true;
}

/// N=500 hostile list: a long shared prefix, keys differing in one or two characters near the end —
/// the shape where 8-bit seeds and a 1.0 load factor used to be expected to break down.
std::vector<std::string> hostile_prefix_shared() {
    std::vector<std::string> keys;
    const std::string prefix(64, 'a');
    for (int i = 0; i < 500; ++i) {
        std::string k = prefix;
        k += static_cast<char>('A' + (i / 26) % 26);
        k += static_cast<char>('a' + i % 26);
        k += "::method()";
        keys.push_back(std::move(k));
    }
    return keys;
}

std::vector<std::string> hostile_signatures() {
    std::vector<std::string> keys;
    const std::string prefix = "void hwshqtb::metaobject::some_long_class_name::method_";
    for (int i = 0; i < 500; ++i) {
        keys.push_back(prefix + std::to_string(i) + "(const std::string&,int)");
    }
    return keys;
}

}  // namespace chd_hash_test

using namespace chd_hash_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    std::printf("== chd_hash: sampled 64-bit seeds, 1.25x slack slots, redirect ==\n");

    // (1) size sweep: the required 0/1/2/5/100/500/2000 plus the intermediate sizes the older
    //     version of this test covered (they exercise the bucket-size tail of the distribution).
    exercise({}, "empty", true);
    exercise({ "single" }, "one key", true);
    exercise(sequential(2, "k"), "two keys", true);
    exercise(sequential(5, "k"), "five keys", true);
    exercise(sequential(8, "k"), "eight keys", true);
    exercise(sequential(16, "method_"), "sixteen keys", true);
    exercise(sequential(32, "method_"), "thirty-two keys", true);
    exercise(sequential(64, "method_"), "sixty-four keys", true);
    exercise(sequential(96, "method_"), "ninety-six keys", true);
    exercise(sequential(100, "method_"), "hundred keys", true);
    exercise(sequential(128, "method_"), "128 keys", true);
    exercise(sequential(192, "method_"), "192 keys", true);
    exercise(sequential(256, "method_"), "256 keys", true);
    exercise(sequential(500, "method_"), "five hundred keys", true);
    exercise(sequential(2000, "method_"), "two thousand keys", true);

    // Realistic meta-info key shapes: signatures with punctuation.
    std::vector<std::string> signatures;
    for (std::size_t i = 0; i < 40; ++i) {
        signatures.push_back("method" + std::to_string(i) + "(int,double)");
    }
    exercise(signatures, "signature-shaped keys", true);

    // (3) the hostile regression: shared long prefixes, one-character differences, N=500.
    exercise(hostile_prefix_shared(), "hostile shared prefix", true);
    exercise(hostile_signatures(), "hostile signatures", true);

    // (2) determinism: two builds from two independent copies of the same list.
    determinism(hostile_prefix_shared());
    determinism(sequential(500, "method_"));

    std::printf("  chd_hash: %zu checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
