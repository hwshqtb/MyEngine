/// @file demo_plugin.cpp
/// @brief The M16 Phase D demo plugin: a real shared library the end-to-end test loads.
///
/// The DLL exports the two fixed C-linkage entry points plugin_loader_t resolves
/// ("metaobject_plugin_instance" / "metaobject_plugin_metadata") and implements IGreeter
/// through an ordinary object_t subclass. Everything the host observes - the interface vtable, the
/// meta-object table, the any_t it builds - is compiled into this DLL, so the test would notice if
/// it were talking to a host-side copy instead.

#include <atomic>
#include <string>
#include <string_view>

#include <hwshqtb/metaobject/property_t.h>
#include <hwshqtb/metaobject/object_t.h>

#include "demo_interface.h"

using namespace hwshqtb::metaobject;

/// @brief Marks the entry points as exported on platforms that need it.
/// @note The loader resolves the names with GetProcAddress/dlsym, so they need default visibility
///       even though everything else in this DLL may stay internal.
#if defined(_WIN32)
#    define DEMO_PLUGIN_EXPORT __declspec(dllexport)
#else
#    define DEMO_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

namespace {
    /// @brief How many demo_plugin_object instances this DLL has constructed.
    /// @note The host reads it through IGreeter::init_count(): a value only this plugin's code owns.
    std::atomic<int> g_init_count{ 0 };

    /// @brief The plugin's metadata, one "key=value" pair per line.
    constexpr const char* plugin_metadata =
        "key=demo\n"
        "name=Demo Plugin\n"
        "version=1.0";
}

/// @brief The plugin's main object: an object_t that implements the IGreeter interface.
/// @note object_t must stay the FIRST base: single-inheritance assumption — object_t is the one
///       meta-object base, the rest are the interfaces (meta_object_t's consteval constructor links
///       `_super` to that base's meta-object). IGreeter is an interface because its own definition
///       carries make_interface (demo_interface.h); inheriting it publicly is what puts the row in
///       this class's table — there is no INTERFACES list.
struct demo_plugin_object: object_t, IGreeter {
    OBJECT
public:
    demo_plugin_object() {
        g_init_count.fetch_add(1, std::memory_order_relaxed);
    }

    int magic() const override {
        return IGreeter::magic_number;
    }

    std::string greet() const override {
        greeted("hello from demo_plugin");  // exercise the SIGNAL path inside the plugin
        return std::string(demo_plugin_greeting);
    }

    int init_count() const override {
        return g_init_count.load(std::memory_order_relaxed);
    }

    any_t make_any() const override {
        return any_t(std::string("demo_plugin"));
    }

    any_t make_payload_any() const override {
        // A USER-declared metatype built inside this DLL: its type_interface_t is this module's copy,
        // so the host can only see the same id through the shared registry's name dedup.
        return any_t(demo_payload_t{ 7 });
    }

    void install_binding() override {
        // Evaluated by DLL code through the library's exported property_t::set_binding(). The
        // closure reads a plain member, so no dependency is recorded here: set_source_value() calls
        // invalidate() explicitly, which is what makes the recompute happen at all. The counter is
        // the host's observable "the binding was evaluated again".
        _bound.set_binding([this]() -> any_t {
            ++_bound_evaluations;
            return any_t(source_value());
        });
    }

    int bound_value() const override {
        // DLL code reads the DLL-owned property_t. When the HOST's binding evaluates this call,
        // property_t::value() - the one exported implementation in libmetaobject.dll - must see the
        // evaluation context the HOST installed. If the context slot were per-module, it would see
        // nullptr, record no dependency and the host's binding would silently go stale - the
        // regression that tests/m16-plugin-test (4b) pins down.
        return _bound.value().get<int>();
    }

    int bound_evaluations() const override {
        return _bound_evaluations;
    }

    int source_value() const override {
        return _source;
    }

    void set_source_value(int v) override {
        _source = v;
        // invalidate() is the library's exported "the source changed" operation: it sets the dirty
        // flag, queues the recompute in the HOST's per-thread round (detail::thread_data_t is
        // exported) and invalidates dependants. Appending only a queue entry would leave a read
        // before the next flush stale.
        _bound.invalidate();
    }

    SIGNAL(void greeted(const std::string& message) const)

private:
    /// @brief The plugin-owned property: both its storage and its binding closure live in the DLL.
    property_t _bound{ any_t(0) };

    /// @brief How many times _bound's closure ran (exposed through IGreeter::bound_evaluations()).
    int _bound_evaluations = 0;

    /// @brief The plain value the binding closure reads (source_value()).
    int _source = 1;
};
META_OBJECT(demo_plugin_object);

extern "C" {
/// @brief Return the plugin's main object (the loader caches the pointer).
/// @note Function-local static: the object is constructed on the first call, which is after the host
///       has created its core_application, and destroyed if/when the DLL is really unloaded.
DEMO_PLUGIN_EXPORT object_t* metaobject_plugin_instance() {
    static demo_plugin_object instance;
    return &instance;
}

/// @brief Return the plugin's metadata ("key=value" lines, static storage).
DEMO_PLUGIN_EXPORT const char* metaobject_plugin_metadata() {
    return plugin_metadata;
}
}
