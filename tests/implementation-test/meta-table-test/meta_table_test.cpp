// Runtime verification of the perfect-hash algorithm against the *real* signature sets of
// object_t: (a) every member function, hidden or not, constructors included, and (b) the subset that
// make_hidden leaves in the reflected table. Both are fed through chd_hash() at run time and checked
// to be perfect hashes.

#include <meta>
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/chd_hash.hpp>
#include <hwshqtb/metaobject/meta_info_t.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::metaobject;

namespace meta_table_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// Signatures of every member function of object_t, including the ones make_hidden() removes from
/// the table and the constructors (which the table now holds as well, M18). consteval because
/// members_of() allocates; the result is a fixed array so it can be copied out at run time
/// (std::string_view is not a structural type, so define_static_array() is not an option here).
inline constexpr std::size_t max_signatures = 96;

struct signature_table {
    std::array<std::string_view, max_signatures> items{ };
    std::size_t count = 0;
};

consteval signature_table all_member_signatures_ct() {
    signature_table result;
    for (auto member : std::meta::members_of(^^object_t, std::meta::access_context::unchecked())) {
        if (!std::meta::is_function(member)) {
            continue;
        }
        if (std::meta::is_destructor(member)) {
            continue;
        }
        // A constructor has no identifier of its own - signature_of() names it after its class - so
        // it passes this guard; template members and operators have no identifier and are skipped.
        if (!std::meta::is_constructor(member) && !std::meta::has_identifier(member)) {
            continue;
        }
        if (result.count >= max_signatures) {
            break;
        }
        result.items[result.count++] = meta_info_t::signature_of(member);
    }
    return result;
}

std::vector<std::string> all_member_signatures() {
    constexpr signature_table table = all_member_signatures_ct();
    std::vector<std::string> result;
    result.reserve(table.count);
    for (std::size_t i = 0; i < table.count; ++i) {
        result.emplace_back(table.items[i]);
    }
    return result;
}

/// Signatures of the entries that actually ended up in the reflected table.
std::vector<std::string> table_signatures() {
    std::vector<std::string> result;
    const meta_info_t& raw = meta_info_t::build_meta<object_t>();
    const meta_object_t info{ raw };
    const std::size_t count = info.method_count();
    for (std::size_t i = 0; i < count; ++i) {
        const meta_method_t m = info.method(static_cast<index_t>(i));
        // signature().empty() is the "resolved to a table entry" test now (has_name() is gone), not
        // is_valid(): every entry build_meta stores carries a signature, but is_valid() answers
        // "callable" (object_t's deleted copy / move constructors are in the table and are not
        // callable).
        if (!m.signature().empty()) {
            result.emplace_back(m.signature());
        }
    }
    return result;
}

/// Build a perfect hash from `keys` at run time and verify it end to end.
void verify_perfect_hash(const std::vector<std::string>& keys, const char* label) {
    std::vector<std::string_view> views;
    views.reserve(keys.size());
    for (const auto& k : keys) {
        views.emplace_back(k);
    }

    std::printf("  %-24s keys=%zu", label, keys.size());

    const detail::chd_hash_result_t table = detail::chd_hash(views);
    if (table.seed0 == detail::chd_hash_result_t::npos
        || table.seed1 == detail::chd_hash_result_t::npos) {
        std::printf("  -> chd_hash gave up\n");
        ++g_failures;
        return;
    }
    if (table.displacements.size() != keys.size()) {
        std::printf("  -> displacements size %zu\n", table.displacements.size());
        ++g_failures;
        return;
    }

    std::vector<int> hits(keys.size(), 0);
    for (const auto& k : keys) {
        const std::uint64_t slot = detail::find(table, k);
        if (slot == std::uint64_t(-1) || slot >= keys.size()) {
            std::printf("  -> '%s' maps outside [0,%zu)\n", k.c_str(), keys.size());
            ++g_failures;
            return;
        }
        ++hits[static_cast<std::size_t>(slot)];
    }
    for (std::size_t i = 0; i < hits.size(); ++i) {
        if (hits[i] != 1) {
            std::printf("  -> slot %zu hit %d times\n", i, hits[i]);
            ++g_failures;
            return;
        }
    }
    std::printf("  -> perfect (seed0=%llu seed1=%llu, slots=%zu)\n",
                static_cast<unsigned long long>(table.seed0),
                static_cast<unsigned long long>(table.seed1), table.redirect.size());
}

}  // namespace meta_table_test

using namespace meta_table_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    std::printf("== meta table: runtime hash verification ==\n");

    const auto all = all_member_signatures();
    const auto visible = table_signatures();

    std::printf("  member functions: %zu (visible in table: %zu)\n", all.size(), visible.size());

    // The table must be a subset of all member functions.
    std::set<std::string> all_set(all.begin(), all.end());
    std::size_t not_a_member = 0;
    for (const auto& s : visible) {
        if (all_set.find(s) == all_set.end()) {
            std::printf("  [FAIL] table signature not from a member: %s\n", s.c_str());
            ++not_a_member;
        }
    }
    CHECK(not_a_member == 0);

    // (1) The hash must be perfect for the full member set (hidden entries included).
    verify_perfect_hash(all, "all member functions");

    // (2) ... and for the visible subset, which is what build_meta actually stores.
    verify_perfect_hash(visible, "reflected table");

    // (3) Round-trip through the real table: every entry's signature resolves back to itself.
    // signature().empty() is the "resolved" predicate now (has_name() is gone); is_valid() is the
    // callability verdict, and a named but not-callable constructor entry still resolves (and
    // compares equal) through the table.
    const meta_info_t& raw_info = meta_info_t::build_meta<object_t>();
    const meta_object_t info{ raw_info };
    std::size_t mismatches = 0;
    for (std::size_t i = 0; i < visible.size(); ++i) {
        const meta_method_t m = info.method(static_cast<index_t>(i));
        const meta_method_t again = info.method(m.signature());
        if (again.signature().empty() || !(again == m)) {
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);

    // (4) object_t::arena() is gone — from the class, and therefore from every reflected table. It
    // was a public, non-hidden method returning a detail:: type: the dynamic-meta arena used to leak
    // into the callable API of every object. The arena is process-wide now and object_t does not own
    // it, so nothing may resolve "arena()" any more (checked against the real table and against the
    // full member list, hidden entries included).
    CHECK(!object_t::static_meta_object.method("arena()").is_valid());
    CHECK(std::find(all.begin(), all.end(), "arena()") == all.end());
    CHECK(std::find(visible.begin(), visible.end(), "arena()") == visible.end());

    // (5) The access column is populated for EVERY entry: every reflected member was declared in
    // some access section, so access() answers a specifier and never none. The value itself is a
    // hint (object_t's members are public here), so only the "no specifier missing" fact is asserted
    // over the whole table; the per-specifier cases live in constructor_test.
    std::size_t without_access = 0;
    for (std::size_t i = 0; i < visible.size(); ++i) {
        if (info.method(static_cast<index_t>(i)).access() == method_access_enum::NONE) {
            ++without_access;
        }
    }
    CHECK(without_access == 0);

    std::printf(g_failures == 0 ? "  runtime hash: all checks passed\n"
                                : "  runtime hash: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}