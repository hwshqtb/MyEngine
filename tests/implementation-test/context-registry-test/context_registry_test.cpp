// The type registry is ONE process-wide table, so a name resolves to the same type whatever objects
// exist around it — inside or outside the application's object tree, attached or detached — and no
// bootstrap call is needed anywhere. This test keeps its historical name and its agreement
// assertions; the only thing that changed is WHY they hold (one registry, not a per-tree fallback:
// the by-name entry point is meta_type_t::resolve_global() and it takes no object).

#include <meta>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_type_t.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::metaobject;

namespace context_registry_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// A child of a detached tree root: it must see the same registry as everything else.
struct probe_t : object_t {
    OBJECT
};
META_OBJECT(probe_t);

/// A type nothing else registers: the no-alias path must still work exactly as before.
struct alias_probe_t { };

struct log_record_t {
    log_level_enum level = log_level_enum::DEBUG;
    std::string message;
};

std::vector<log_record_t>& log_records() {
    static std::vector<log_record_t> records;
    return records;
}

std::size_t count_level(log_level_enum level) {
    std::size_t count = 0;
    for (const log_record_t& record : log_records()) {
        if (record.level == level) {
            ++count;
        }
    }
    return count;
}

bool has_message(log_level_enum level, std::string_view needle) {
    for (const log_record_t& record : log_records()) {
        if (record.level == level && record.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace context_registry_test

using namespace context_registry_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    std::printf("== context registry fallback ==\n");

    // (1) A standalone object (no parent) needs no bootstrap call: builtin names resolve through the
    //     one process-wide registry, and the by-name lookup takes only the name.
    object_t standalone;
    CHECK(standalone.parent() == nullptr);
    CHECK(any_t(42).is_initialized());
    const meta_type_t int_bare = meta_type_t::resolve_global("int");
    CHECK(int_bare.is_valid());
    CHECK(meta_type_t::resolve_global("double").is_valid());
    CHECK(meta_type_t::resolve_global("no_such_type").is_valid() == false);

    // (1b) The other primitive spellings the canonical namer knows are registered by the application
    //      as well (B10/L10): before the append these five resolved to nothing even with an app,
    //      because register_builtin_types() only covered the historical METAOBJECT_FOR_EACH_BUILTIN
    //      order. Appended last, so the historical ids (int == 8) do not move.
    CHECK(meta_type_t::resolve_global("wchar_t").is_valid());
    CHECK(meta_type_t::resolve_global("char8_t").is_valid());
    CHECK(meta_type_t::resolve_global("char16_t").is_valid());
    CHECK(meta_type_t::resolve_global("char32_t").is_valid());
    CHECK(meta_type_t::resolve_global("std::nullptr_t").is_valid());
    CHECK(meta_type_t::resolve_global("std::nullptr_t").name() == "std::nullptr_t");
    CHECK(int_bare.id() == 8);          // the historical builtin id, unmoved by the appended five

    // (2) An object inside the application tree behaves identically: its position never takes part in
    //     the lookup, so the answer cannot differ.
    probe_t in_tree;
    in_tree.set_parent(root);
    CHECK(meta_type_t::resolve_global("int") == int_bare);
    CHECK(any_t(7).get<int>() == 7);

    // (3) A detached tree root and its child agree with each other.
    probe_t detached_root;
    probe_t detached_child;
    detached_child.set_parent(&detached_root);
    CHECK(detached_child.parent() == &detached_root);
    CHECK(meta_type_t::resolve_global("int") == int_bare);
    CHECK(meta_type_t::resolve_global("int").id() == int_bare.id());

    // (4) The registry is process-wide by design (per-tree type isolation went with the global id
    //     space), so the name answers the same handle while an object sits in the application tree
    //     (via_app), in a detached tree (via_detached) and standalone (via_standalone) — with no
    //     registration call in between.
    const meta_type_t via_app = meta_type_t::resolve_global("int");
    const meta_type_t via_detached = meta_type_t::resolve_global("int");
    const meta_type_t via_standalone = meta_type_t::resolve_global("int");
    CHECK(via_app.is_valid());
    CHECK(via_app == int_bare);
    CHECK(via_detached == int_bare);
    CHECK(via_standalone == int_bare);
    CHECK(via_app.id() == via_standalone.id());

    // (5) any_t round-trips a builtin value.
    any_t value(3.5);
    CHECK(value.is_initialized());
    CHECK(value.get<double>() == 3.5);

    // (6) Type aliases: register_type<T>(alias) binds an extra name to the type without moving any
    //     existing spelling. The alias answers the SAME handle (operator==), and the primary table is
    //     never touched.
    log_records().clear();
    install_log_handler([](log_level_enum level, const log_context_t& context, std::string_view message) {
        (void)context;
        log_records().push_back(log_record_t{ level, std::string(message) });
        return true; // consumed: the default stderr sink stays silent
    });

    // (6a) An alias resolves to the type and compares equal to from_type<int>() (not merely by name).
    const index_t int_alias_id = meta_type_t::register_type<int>("myspace::int_alias");
    CHECK(int_alias_id == int_bare.id());
    CHECK(int_alias_id == meta_type_t::from_type<int>().id());
    const meta_type_t int_alias = meta_type_t::resolve_global("myspace::int_alias");
    CHECK(int_alias.is_valid());
    CHECK(int_alias == meta_type_t::from_type<int>());
    CHECK(int_alias == int_bare);
    CHECK(count_level(log_level_enum::WARNING) == 0u);

    // (6b) Repeating the same pair is harmless: same id, no Warning.
    CHECK(meta_type_t::register_type<int>("myspace::int_alias") == int_alias_id);
    CHECK(count_level(log_level_enum::WARNING) == 0u);

    // (6c) An alias that equals the type's own primary name is a no-op success.
    CHECK(meta_type_t::register_type<int>("int") == int_bare.id());
    CHECK(count_level(log_level_enum::WARNING) == 0u);

    // (6d) A name already bound to another type is refused with exactly one Warning; the first
    //      binding keeps answering.
    const index_t double_id = meta_type_t::register_type<double>("myspace::int_alias");
    CHECK(double_id == meta_type_t::from_type<double>().id());
    CHECK(count_level(log_level_enum::WARNING) == 1u);
    CHECK(has_message(log_level_enum::WARNING, "first binding is kept"));
    CHECK(meta_type_t::resolve_global("myspace::int_alias") == int_bare);

    // (6e) An alias may not equal another type's primary name: one Warning, and the primary name
    //      still answers its own type.
    CHECK(meta_type_t::register_type<double>("int") == double_id);
    CHECK(count_level(log_level_enum::WARNING) == 2u);
    CHECK(has_message(log_level_enum::WARNING, "already a registered type name"));
    CHECK(meta_type_t::resolve_global("int") == int_bare);

    // (6f) The no-alias call still registers the primary name exactly as before, and the explicit
    //      empty alias binds nothing (no Warning).
    const index_t alias_probe_id = meta_type_t::register_type<alias_probe_t>();
    CHECK(alias_probe_id >= 1);
    CHECK(alias_probe_id == meta_type_t::from_type<alias_probe_t>().id());
    const meta_type_t alias_probe = meta_type_t::resolve_global("context_registry_test::alias_probe_t");
    CHECK(alias_probe.is_valid());
    CHECK(alias_probe == meta_type_t::from_type<alias_probe_t>());
    CHECK(meta_type_t::register_type<alias_probe_t>(std::string_view{ }) == alias_probe_id);
    CHECK(count_level(log_level_enum::WARNING) == 2u);

    install_log_handler(log_handler{ });

    std::printf(g_failures == 0 ? "  context registry: all checks passed\n"
                                : "  context registry: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
