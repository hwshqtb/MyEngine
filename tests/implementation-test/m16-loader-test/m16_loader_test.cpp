// M16 Phase B/C: plugin_loader_t and factory_loader_t.
//
// Real plugin DLLs arrive in a later phase, so this test pins down everything observable without
// one:
//  1. a non-existent file: load() fails with a Critical log and leaves instance() null;
//  2. an existing file that is not a shared library (this test's own source): same outcome;
//  3. an existing shared library that lacks the two entry points (the OS's kernel32.dll): load()
//     fails, names the missing symbol and reports the OS error text;
//  4. metadata_value() parsing: comments, blank lines, spaces, '=' inside a value, CRLF, and a
//     loader that is not loaded;
//  5. factory_loader_t on empty / nonexistent search paths: no throw, no keys, no object;
//  6. live-object accounting and unload() on an unloaded loader.

#include <meta>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/factory_loader_t.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/plugin_loader_t.h>

using namespace hwshqtb::metaobject;

namespace m16_loader_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// One captured log record (the same shape tests/log-test uses).
struct record_t {
    std::string category;
    log_level_enum level = log_level_enum::DEBUG;
    std::string message;
};

std::vector<record_t>& records() {
    static std::vector<record_t> captured;
    return captured;
}

/// Install the capturing handler and start from an empty record list.
void capture_logs() {
    records().clear();
    install_log_handler([](log_level_enum level, const log_context_t& context, std::string_view message) {
        records().push_back(record_t{ std::string(context.category), level, std::string(message) });
        return true;  // consumed: the default stderr sink stays silent
    });
}

/// Whether some captured record has @p level and contains @p needle.
bool has_record(log_level_enum level, std::string_view needle) {
    for (const record_t& record : records()) {
        if (record.level == level && record.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/// The directory holding this test's sources.
std::filesystem::path source_dir() {
    return std::filesystem::path(M16_LOADER_TEST_SOURCE_FILE).parent_path();
}

}  // namespace m16_loader_test

using namespace m16_loader_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    std::printf("== M16 Phase B/C: plugin_loader_t + factory_loader_t ==\n");

    // -----------------------------------------------------------------------
    // (1) A path that does not exist: load() fails, nothing is cached, and the failure is logged.
    // -----------------------------------------------------------------------
    {
        capture_logs();
        const std::string path = (source_dir() / "no_such_plugin_9f3c2a.dll").string();
        plugin_loader_t loader(path);
        CHECK(loader.path() == std::string_view(path));
        CHECK(!loader.is_loaded());
        CHECK(!loader.load());
        CHECK(!loader.is_loaded());
        CHECK(loader.instance() == nullptr);
        CHECK(loader.metadata().empty());
        CHECK(loader.metadata_value("key").empty());
        CHECK(has_record(log_level_enum::CRITICAL, "no_such_plugin_9f3c2a.dll"));
    }

    // -----------------------------------------------------------------------
    // (2) An existing file that is not a shared library. This test's own .cpp is the most portable
    //     such file: it exists on every machine the test runs on.
    // -----------------------------------------------------------------------
    {
        capture_logs();
        const std::string source_file = M16_LOADER_TEST_SOURCE_FILE;
        CHECK(std::filesystem::exists(std::filesystem::path(source_file)));
        plugin_loader_t loader(source_file);
        CHECK(!loader.load());
        CHECK(!loader.is_loaded());
        CHECK(loader.instance() == nullptr);
        CHECK(has_record(log_level_enum::CRITICAL, "m16_loader_test.cpp"));
    }

    // -----------------------------------------------------------------------
    // (3) A real shared library without the entry points. kernel32.dll is always present and
    //     loadable on Windows, so this exercises the missing-symbol path (Critical log naming the
    //     symbol plus the OS error text) without a plugin of our own.
    // -----------------------------------------------------------------------
#if defined(_WIN32)
    {
        capture_logs();
        plugin_loader_t loader("kernel32.dll");
        CHECK(!loader.load());
        CHECK(!loader.is_loaded());
        CHECK(loader.instance() == nullptr);
        CHECK(has_record(log_level_enum::CRITICAL, "metaobject_plugin_instance"));
        CHECK(has_record(log_level_enum::CRITICAL, "GetLastError()"));
    }
#endif

    // -----------------------------------------------------------------------
    // (4) Metadata parsing. The static helper is the exact parser metadata_value() uses, so it can
    //     be tested without a plugin library.
    // -----------------------------------------------------------------------
    {
        capture_logs();
        plugin_loader_t unloaded("never_loaded.dll");
        CHECK(unloaded.metadata_value("key").empty());
        CHECK(unloaded.metadata().empty());

        const std::string_view metadata =
            "# plugin metadata\n"
            "\n"
            "  key  =  demo_plugin  \n"
            "name=with spaces\n"
            "  # indented comment\n"
            "expr=a=b=c\n"
            "empty=\n"
            "malformed line without equals\n"
            "crlf=windows\r\n"
            "key=second_ignored\n";

        CHECK(plugin_loader_t::parse_metadata_value(metadata, "key") == "demo_plugin");
        CHECK(plugin_loader_t::parse_metadata_value(metadata, "name") == "with spaces");
        CHECK(plugin_loader_t::parse_metadata_value(metadata, "expr") == "a=b=c");
        CHECK(plugin_loader_t::parse_metadata_value(metadata, "empty").empty());
        CHECK(plugin_loader_t::parse_metadata_value(metadata, "crlf") == "windows");
        CHECK(plugin_loader_t::parse_metadata_value(metadata, "malformed").empty());
        CHECK(plugin_loader_t::parse_metadata_value(metadata, "missing").empty());
        CHECK(plugin_loader_t::parse_metadata_value("", "key").empty());
        CHECK(plugin_loader_t::parse_metadata_value("key=no trailing newline", "key") == "no trailing newline");
        CHECK(plugin_loader_t::parse_metadata_value("key=a=b\n", "key") == "a=b");
        CHECK(plugin_loader_t::parse_metadata_value("# only a comment\nkey=hidden\n", "key") == "hidden");
    }

    // -----------------------------------------------------------------------
    // (5) factory_loader_t without any actual plugin: empty search paths and a missing directory must
    //     not throw, must yield no keys, and must refuse unknown keys.
    // -----------------------------------------------------------------------
    {
        capture_logs();
        factory_loader_t factory;
        CHECK(factory.keys().empty());
        factory.scan();  // no search path at all
        CHECK(factory.keys().empty());
        CHECK(factory.find("anything") == nullptr);
        CHECK(factory.create("anything") == nullptr);

        factory.add_search_path((source_dir() / "no_such_directory_9f3c2a").string());
        factory.scan();  // missing directory: Warning, but the scan carries on
        CHECK(factory.keys().empty());
        CHECK(factory.find("") == nullptr);
        CHECK(factory.create("") == nullptr);
        factory.scan();  // idempotent: nothing is loaded twice
        CHECK(factory.keys().empty());

        // An existing directory that holds no plugin libraries: the test source directory.
        factory.add_search_path(source_dir().string());
        factory.scan();
        CHECK(factory.keys().empty());
        CHECK(factory.find("key") == nullptr);
        CHECK(factory.create("key") == nullptr);
    }

    // -----------------------------------------------------------------------
    // (6) Live-object accounting and unload() on an unloaded loader.
    // -----------------------------------------------------------------------
    {
        plugin_loader_t loader("never_loaded.dll");
        CHECK(loader.live_object_count() == 0);

        loader.register_object();
        loader.register_object();
        CHECK(loader.live_object_count() == 2);
        loader.unregister_object();
        CHECK(loader.live_object_count() == 1);

        CHECK(!loader.is_loaded());
        loader.unload();  // unloaded loader: no-op, no crash
        CHECK(!loader.is_loaded());
        CHECK(loader.live_object_count() == 1);
        CHECK(loader.instance() == nullptr);

        loader.unregister_object();
        loader.unregister_object();  // underflow guard: stays at zero
        CHECK(loader.live_object_count() == 0);
    }

    if (g_failures == 0) {
        std::printf("  [ok] all plugin/factory loader checks passed\n");
        return 0;
    }
    std::printf("  [FAIL] %d check(s) failed\n", g_failures);
    return 1;
}
