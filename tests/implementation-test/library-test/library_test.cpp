// M16: library_t - one dynamically loaded shared library (the QLibrary counterpart).
//
// library_t is the OS-level half that plugin_loader_t now builds on, so it is pinned down here
// without any plugin of this tree:
//  1. a path that does not exist: load() fails, is_loaded() stays false, error_string() is set;
//  2. an existing file that is not a shared library (this test's own source): same outcome;
//  3. a real system library (kernel32.dll on Windows): load() succeeds, a known symbol resolves
//     and runs, an unknown symbol does not resolve;
//  4. unload(): is_loaded() becomes false and resolve() returns null afterwards;
//  5. load() twice: both calls return true (idempotent);
//  6. is_library(): the four shared-library spellings are accepted, everything else is rejected;
//  7. set_path() / path() round-trip, and resolve() never loads the library implicitly.

#include <meta>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/library_t.h>
#include <hwshqtb/metaobject/log.h>

using namespace hwshqtb::metaobject;

namespace library_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// The system library to open and one function it exports. kernel32.dll is always present and
/// loadable on Windows; the POSIX names are the usual libc spellings, used when this test is built
/// on that platform.
#if defined(_WIN32)
constexpr const char* system_library_path = "kernel32.dll";
constexpr const char* system_library_symbol = "GetCurrentProcessId";
using system_library_fn = unsigned long (*)();
#elif defined(__APPLE__)
constexpr const char* system_library_path = "/usr/lib/libSystem.B.dylib";
constexpr const char* system_library_symbol = "getpid";
using system_library_fn = int (*)();
#else
constexpr const char* system_library_path = "libc.so.6";
constexpr const char* system_library_symbol = "getpid";
using system_library_fn = int (*)();
#endif

/// Keep the expected Critical logs (missing file, foreign file, missing symbol, resolve on an
/// unloaded wrapper) out of the test output; the CHECKs still report every failure.
void silence_logs() {
    install_log_handler([](log_level_enum, const log_context_t&, std::string_view) {
        return true;  // consumed: the default stderr sink stays silent
    });
}

}  // namespace library_test

using namespace library_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    std::printf("== M16: library_t ==\n");
    silence_logs();

    // -----------------------------------------------------------------------
    // (1) A path that does not exist: load() fails, no handle is left behind, and the failure is
    //     recorded as text.
    // -----------------------------------------------------------------------
    {
        library_t library("definitely/not/here.dll");
        CHECK(library.path() == "definitely/not/here.dll");
        CHECK(!library.is_loaded());
        CHECK(!library.load());
        CHECK(!library.is_loaded());
        CHECK(!library.error_string().empty());

        // resolve() on an unloaded wrapper is a miss, not an implicit load (QLibrary differs here).
        CHECK(library.resolve("anything") == nullptr);
        CHECK(!library.is_loaded());
        CHECK(!library.error_string().empty());
    }

    // An empty path is the other load() failure that never reaches the OS.
    {
        library_t library;
        CHECK(!library.load());
        CHECK(!library.is_loaded());
        CHECK(!library.error_string().empty());
    }

    // -----------------------------------------------------------------------
    // (2) An existing file that is not a shared library. This test's own .cpp is the most portable
    //     such file: it exists on every machine the test runs on.
    // -----------------------------------------------------------------------
    {
        const std::string source_file = LIBRARY_TEST_SOURCE_FILE;
        CHECK(std::filesystem::exists(std::filesystem::path(source_file)));

        library_t library(source_file);
        CHECK(!library.load());
        CHECK(!library.is_loaded());
        CHECK(!library.error_string().empty());
    }

    // -----------------------------------------------------------------------
    // (3) A real system library: load() succeeds, a known symbol resolves and runs, an unknown one
    //     does not resolve.
    // -----------------------------------------------------------------------
    {
        library_t library(system_library_path);
        CHECK(library.load());
        CHECK(library.is_loaded());
        CHECK(library.error_string().empty());  // a successful load leaves no failure behind

        const system_library_fn get_id = library.resolve_as<system_library_fn>(system_library_symbol);
        CHECK(get_id != nullptr);
        if (get_id != nullptr) {
            CHECK(get_id() != 0);  // the process always has an id
        }

        CHECK(library.resolve("NoSuchSymbolAtAll") == nullptr);
        CHECK(!library.error_string().empty());
    }

    // -----------------------------------------------------------------------
    // (4) unload(): the wrapper reports "not loaded" and resolve() is a miss again.
    // -----------------------------------------------------------------------
    {
        library_t library(system_library_path);
        CHECK(library.load());
        CHECK(library.is_loaded());
        library.unload();
        CHECK(!library.is_loaded());
        CHECK(library.resolve(system_library_symbol) == nullptr);
    }

    // -----------------------------------------------------------------------
    // (5) load() is idempotent: the second call is a no-op that still reports success.
    // -----------------------------------------------------------------------
    {
        library_t library(system_library_path);
        CHECK(library.load());
        CHECK(library.load());
        CHECK(library.is_loaded());
        library.unload();
        CHECK(!library.is_loaded());
    }

    // -----------------------------------------------------------------------
    // (6) is_library(): no filesystem access, only the extension decides.
    // -----------------------------------------------------------------------
    CHECK(library_t::is_library("a.dll"));
    CHECK(library_t::is_library("libb.so"));
    CHECK(library_t::is_library("libc.so.1"));
    CHECK(library_t::is_library("d.dylib"));

    CHECK(!library_t::is_library("e.txt"));
    CHECK(!library_t::is_library("f"));
    CHECK(!library_t::is_library(""));

    // Case-insensitive, directory part ignored, and a version suffix must be digits.
    CHECK(library_t::is_library("C:\\plugins\\A.DLL"));
    CHECK(library_t::is_library("../lib/x.SO.12"));
    CHECK(!library_t::is_library("libc.so.x"));
    CHECK(!library_t::is_library("libc.so."));
    CHECK(!library_t::is_library("libc.so.1.bak"));

    // -----------------------------------------------------------------------
    // (7) set_path() / path() round-trip; changing the path drops an open handle so the two never
    //     disagree.
    // -----------------------------------------------------------------------
    {
        library_t library;
        CHECK(library.path().empty());
        library.set_path("some/where/plugin.dll");
        CHECK(library.path() == "some/where/plugin.dll");
        CHECK(!library.is_loaded());
        CHECK(library_t::is_library(library.path()));

        library.set_path("other.so.1");
        CHECK(library.path() == "other.so.1");
        CHECK(library_t::is_library(library.path()));

        CHECK(!library.load());  // the file does not exist
        CHECK(!library.is_loaded());

        library_t loaded(system_library_path);
        CHECK(loaded.load());
        loaded.set_path("elsewhere/other.dll");
        CHECK(!loaded.is_loaded());
        CHECK(loaded.path() == "elsewhere/other.dll");
    }

    if (g_failures == 0) {
        std::printf("  [ok] all library_t checks passed\n");
        return 0;
    }
    std::printf("  [FAIL] %d check(s) failed\n", g_failures);
    return 1;
}
