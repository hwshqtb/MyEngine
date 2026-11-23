// M16 Phase D: end-to-end test against the real demo plugin.
//
// m16-loader-test pins plugin_loader_t/factory_loader_t down WITHOUT a plugin (missing files, a foreign
// DLL, parsing, live-object accounting). This test does the opposite: it loads examples/plugin's
// demo_plugin - a genuine shared library built from this tree - and checks the whole M16 contract
// against real cross-module code:
//  1. load() resolves both entry points and instance() is non-null;
//  2. the metadata carries the plugin's keys;
//  3. cast<IGreeter>() yields the interface and its virtual calls reach the plugin's implementation;
//  4. an any_t built INSIDE the plugin shares its meta_type_t id with one built in the host: the
//     one-registry proof that motivates the shared-library configuration;
//  5. an unknown IID is a miss (cast null, implements false);
//  6. factory_loader_t scans the plugin's directory, indexes it under "demo" and create()s it;
//  7. unload() is refused while a live object is registered, and succeeds afterwards.
//
// The DLL path is compiled in from CMake through M16_PLUGIN_PATH="$<TARGET_FILE:demo_plugin>" (see
// CMakeLists.txt), so the test never guesses a relative path nor depends on the working directory.

#ifndef M16_PLUGIN_PATH
#    error "M16_PLUGIN_PATH must be defined by tests/m16-plugin-test/CMakeLists.txt"
#endif

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <hwshqtb/metaobject/property_t.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/factory_loader_t.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/plugin_loader_t.h>

#include "demo_interface.h"

using namespace hwshqtb::metaobject;

namespace m16_plugin_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

/// The demo plugin DLL; CMake expands the generator expression to the exact output file.
std::string plugin_path() {
    return M16_PLUGIN_PATH;
}

/// The directory the plugin lives in, for factory_loader_t::add_search_path.
std::string plugin_directory() {
    return std::filesystem::path(plugin_path()).parent_path().string();
}

/// Whether @p keys contains @p wanted.
bool has_key(const std::vector<std::string>& keys, std::string_view wanted) {
    for (const std::string& key : keys) {
        if (key == wanted) {
            return true;
        }
    }
    return false;
}

/// Replace the log sink with a consumer: the factory scans the flat output directory, which also
/// holds libmetaobject.dll and SDL3.dll, and their expected "not a plugin" Criticals would otherwise
/// flood the test output. The CHECKs below still report every failure.
void silence_logs() {
    install_log_handler([](log_level_enum, const log_context_t&, std::string_view) {
        return true;  // consumed: nothing goes to stderr
    });
}

}  // namespace m16_plugin_test

using namespace m16_plugin_test;  // the file-local test namespace stays the file's own vocabulary

#define CHECK(cond) check((cond), #cond, __LINE__)

int main(int argc, char** argv) {
    core_application app(argc, argv);
    // No type registration is needed: std::basic_string<char>'s interface is produced on demand from
    // its reflection in each module, and the one process-wide registry dedups it by name and assigns
    // its id on first use — in the plugin and in the host alike, which is exactly what the id
    // equality below proves.
    core_application* root = core_application::instance();

    std::printf("== M16-D: demo plugin end-to-end ==\n");
    const std::string path = plugin_path();
    std::printf("  plugin path: %s\n", path.c_str());
    CHECK(std::filesystem::exists(path));

    // -----------------------------------------------------------------------
    // (1) Load the real DLL: both entry points must resolve and the main object must exist.
    // -----------------------------------------------------------------------
    plugin_loader_t loader(path);
    CHECK(loader.load());
    CHECK(loader.is_loaded());
    if (!loader.is_loaded()) {
        std::printf("  [FAIL] demo_plugin did not load (see the Critical log above)\n");
        return 1;
    }
    object_t* instance = loader.instance();
    CHECK(instance != nullptr);
    if (instance == nullptr) {
        std::printf("  [FAIL] demo_plugin loaded but has no main object\n");
        return 1;
    }

    // -----------------------------------------------------------------------
    // (2) The metadata is the plugin's ("key=value" lines).
    // -----------------------------------------------------------------------
    CHECK(loader.metadata_value("key") == "demo");
    CHECK(loader.metadata_value("name") == "Demo Plugin");
    CHECK(loader.metadata_value("version") == "1.0");
    CHECK(loader.metadata_value("missing").empty());
    CHECK(loader.metadata().find("key=demo") != std::string_view::npos);

    // -----------------------------------------------------------------------
    // (3) Interface conversion: cast<IGreeter> lands on the plugin's IGreeter sub-object, and the
    //     virtual calls execute the plugin's own implementation.
    // -----------------------------------------------------------------------
    void* raw_greeter = instance->meta_object()->cast(instance, demo_plugin_iid);
    CHECK(raw_greeter != nullptr);
    IGreeter* greeter = static_cast<IGreeter*>(raw_greeter);
    if (greeter != nullptr) {
        // cast<IGreeter> reads IGreeter's make_interface annotation and must agree with the IID row
        // (both come from demo_interface.h, so drift would fail here).
        CHECK(cast<IGreeter>(instance) == greeter);
        CHECK(greeter->magic() == IGreeter::magic_number);
        CHECK(greeter->greet() == demo_plugin_greeting);
        // init_count() is the identity probe: only the plugin's own DLL keeps this counter.
        CHECK(greeter->init_count() == 1);
        CHECK(instance->meta_object()->implements(demo_plugin_iid));
        CHECK(instance->meta_object()->interface_count() == 1);
        CHECK(instance->meta_object()->class_name() == "demo_plugin_object");
        CHECK(instance->inherits("object_t"));
    }

    // -----------------------------------------------------------------------
    // (4) Cross-module any_t / type identity: the value is built in the plugin, the comparison
    //     value in the host, and both must carry the SAME runtime id (one shared registry in the
    //     library DLL).
    //
    // The whole block is scoped: a handle of a plugin-declared type points into the plugin's image, so
    // the values must be destroyed before section (7) unloads the DLL (the same constraint Qt's
    // QMetaType has; see type_interface_t.h "Module lifetime").
    // -----------------------------------------------------------------------
    {
        any_t plugin_value;
        any_t host_value = std::string("host");
        if (greeter != nullptr) {
            plugin_value = greeter->make_any();
        }
        CHECK(plugin_value.is_initialized());
        CHECK(plugin_value.type().is_valid());
        CHECK(host_value.is_initialized());
        CHECK(host_value.type().is_valid());
        CHECK(plugin_value.type().id() != index_npos);
        CHECK(host_value.type().id() != 0);                  // std::basic_string was registered
        CHECK(plugin_value.type().id() == host_value.type().id());
        CHECK(plugin_value.type().name() == host_value.type().name());
        CHECK(plugin_value.type() == host_value.type());     // id equality is the handle identity
        if (plugin_value.is_initialized()) {
            CHECK(plugin_value.get<std::string>() == "demo_plugin");
        }
        // The host can resolve the same entry by the name the plugin reported through the global
        // registry: the id, not the spelling, is the identity, so the test does not hardcode it.
        const meta_type_t global_string = meta_type_t::resolve_global(host_value.type().name());
        CHECK(global_string.is_valid());
        CHECK(plugin_value.type().id() == global_string.id());
        std::printf("  cross-module any_t: type=%s id=%llu\n",
                    plugin_value.type().name().c_str(),
                    static_cast<unsigned long long>(plugin_value.type().id()));
    }

    // (4a) The same identity check for a USER type. std::basic_string is a builtin whose interface
    //      both modules produce from the same header; demo_payload_t is the harder case the
    //      Q_DECLARE_METATYPE model has to answer: the plugin stores its own copy's type, the host
    //      asks its own copy for the id, and the two type_interface_t OBJECTS are different (one per
    //      PE module) — only the registry's name-based id dedup can make them agree. Both copies spell
    //      the name with the same canonical namer, so the names agree by construction.
    {
        any_t plugin_payload;
        if (greeter != nullptr) {
            plugin_payload = greeter->make_payload_any();
        }
        const meta_type_t host_payload = meta_type_t::from_type<demo_payload_t>();
        CHECK(plugin_payload.is_initialized());
        CHECK(plugin_payload.type().is_valid());
        CHECK(host_payload.is_valid());
        CHECK(plugin_payload.type().name() == "demo_payload_t");
        CHECK(host_payload.name() == "demo_payload_t");
        CHECK(host_payload.id() != 0);                       // the registry assigned an id
        CHECK(plugin_payload.type().id() == host_payload.id());
        CHECK(plugin_payload.type() == host_payload);        // the handle equality rule (id, name tie-break)
        CHECK(plugin_payload.get<demo_payload_t>().value == 7);
        std::printf("  cross-module user metatype: type=%s id=%llu\n",
                    plugin_payload.type().name().c_str(),
                    static_cast<unsigned long long>(plugin_payload.type().id()));
    }

    // -----------------------------------------------------------------------
    // (4b) REGRESSION - the binding-evaluation context must be one per THREAD, not one per MODULE.
    //
    //      property_t's bodies are out-of-line in libmetaobject.dll, so the executable and the plugin
    //      DLL call the SAME exported implementation. The "context of the evaluation in progress"
    //      that it consults must live in ONE shared place - the exported detail::thread_data_t - and
    //      not in a function-local thread_local (in a header, or per module).
    //
    //      (A) The plugin installs its own binding; every piece of that flow runs in the DLL.
    //      (B) The HOST installs a binding whose closure calls the DLL's bound_value(), i.e. a
    //          bindable read that happens in DLL code while the HOST owns the evaluation. This is
    //          the scenario a per-module context slot breaks: the DLL sees
    //          current_context() == nullptr, records no dependency, nothing marks the host binding
    //          dirty, and mirrored stays 4 forever - silently, with no error and no log entry.
    //
    //      Do NOT delete (B) as "just another binding test": (A) alone passes even with the context
    //      split, precisely because all of its code is on one side of the module boundary. (B) is
    //      the assertion that fails when the slot is moved back into the header (verified on
    //      GCC 16.2.0/MinGW-w64).
    // -----------------------------------------------------------------------
    if (greeter != nullptr) {
        // (A) Plugin-owned binding: install_binding() and bound_value() are DLL code.
        greeter->install_binding();
        CHECK(greeter->source_value() == 1);
        CHECK(greeter->bound_value() == 1);
        CHECK(greeter->bound_evaluations() == 1);    // the install evaluated the DLL closure once

        greeter->set_source_value(4);
        CHECK(greeter->bound_value() == 4);      // invalidate() marked it dirty: the read recomputes now
        CHECK(greeter->bound_evaluations() == 2);    // the read evaluated it...
        // The DLL's invalidate() reached the HOST's per-thread round (thread_data_t is exported) and
        // the read above consumed its entry. This one already agreed before the context fix: the
        // scheduler was never the split slot.
        core_application::process_events();
        CHECK(greeter->bound_value() == 4);
        CHECK(greeter->bound_evaluations() == 2);    // ... and consumed the entry: the round did not re-evaluate

        // (B) Host-installed binding over a DLL-owned property handle: the cross-module regression.
        {
            property_t mirrored{ any_t(0) };
            int mirrored_evaluations = 0;
            mirrored.set_binding([greeter, &mirrored_evaluations]() {
                ++mirrored_evaluations;
                return any_t(greeter->bound_value());
            });
            CHECK(mirrored.has_binding());
            CHECK(mirrored.value().get<int>() == 4);   // the initial evaluation already calls into the DLL
            CHECK(mirrored_evaluations == 1);

            greeter->set_source_value(9);
            // invalidate() sets the DLL property dirty and propagates to its dependants at once:
            // _bound and the host's mirrored binding are both queued for the round. Neither closure
            // has run yet.
            CHECK(greeter->bound_evaluations() == 2);    // the DLL closure is still batched
            CHECK(mirrored_evaluations == 1);            // ... and so is the host closure
            core_application::process_events();

            CHECK(greeter->bound_value() == 9);  // the DLL-side flow still works
            CHECK(mirrored_evaluations == 2);    // the round recomputed the host dependant once
            // THE regression assertion: with a split context the dependency on the DLL's property
            // was never recorded, so mirrored is never recomputed and stays 4.
            if (mirrored.value().get<int>() != 9) {
                std::printf("  [REGRESSION] host-side binding is stale: mirrored=%d, DLL property=%d"
                            " (the DLL's property read recorded no dependency)\n",
                            mirrored.value().get<int>(), greeter->bound_value());
            }
            CHECK(mirrored.value().get<int>() == 9);

            // Release the cross-module registration while the DLL is still loaded: mirrored's
            // dependency list holds pointers into the DLL, and section (7) unloads it.
            mirrored.clear_binding();
        }
    }

    // -----------------------------------------------------------------------
    // (5) Negative: an IID no class in the chain declares is a miss, not an error.
    // -----------------------------------------------------------------------
    CHECK(instance->meta_object()->cast(instance, "com.example.INonexistent/1.0") == nullptr);
    CHECK(!instance->meta_object()->implements("com.example.INonexistent/1.0"));
    CHECK(!instance->meta_object()->implements(""));

    // -----------------------------------------------------------------------
    // (6) factory_loader_t: scan the plugin's directory, index it by "demo", create its object.
    //     (The flat output directory also contains libmetaobject.dll and SDL3.dll, which the scan
    //     skips with a Warning; that is why the log sink is silenced below.)
    // -----------------------------------------------------------------------
    const std::string directory = plugin_directory();
    std::printf("  plugin dir:  %s\n", directory.c_str());
    CHECK(std::filesystem::is_directory(directory));
    {
        silence_logs();
        factory_loader_t factory;
        factory.add_search_path(directory);
        factory.scan();

        const std::vector<std::string> keys = factory.keys();
        CHECK(has_key(keys, "demo"));
        const plugin_loader_t* found = factory.find("demo");
        CHECK(found != nullptr);
        if (found != nullptr) {
            CHECK(found->is_loaded());
            CHECK(found->metadata_value("key") == "demo");
            CHECK(found->metadata_value("name") == "Demo Plugin");
            std::error_code error;
            CHECK(std::filesystem::equivalent(std::filesystem::path(found->path()),
                                              std::filesystem::path(path), error));
        }

        object_t* created = factory.create("demo");
        CHECK(created != nullptr);
        // The plugin's instance entry returns a function-local static, so both loaders see the
        // exact same object even though the OS handed out one DLL image.
        CHECK(created == instance);
        if (found != nullptr) {
            CHECK(found->live_object_count() == 1);
        }
        CHECK(factory.find("no_such_key") == nullptr);
        CHECK(factory.create("no_such_key") == nullptr);
        // ~factory: create() registered a live object, so the destructor's unload() is refused and
        // the DLL stays loaded - the next section still observes the direct loader.
    }

    // -----------------------------------------------------------------------
    // (7) Unload safety: a live object pins the library in memory; only draining the count lets
    //     unload() close it.
    // -----------------------------------------------------------------------
    CHECK(loader.live_object_count() == 0);
    loader.register_object();
    CHECK(loader.live_object_count() == 1);
    loader.unload();  // refused: one live object keeps the plugin's static tables referenced
    CHECK(loader.is_loaded());
    CHECK(loader.live_object_count() == 1);
    CHECK(loader.instance() == instance);
    loader.unregister_object();
    CHECK(loader.live_object_count() == 0);
    loader.unload();
    CHECK(!loader.is_loaded());
    CHECK(loader.instance() == nullptr);
    CHECK(loader.metadata().empty());
    CHECK(loader.metadata_value("key").empty());

    if (g_failures == 0) {
        std::printf("  [ok] all demo-plugin checks passed\n");
        return 0;
    }
    std::printf("  [FAIL] %d check(s) failed\n", g_failures);
    return 1;
}
