// Meta-object identity: one static meta-object per class + one virtual accessor returning a pointer.
//
// The three things this test pins down:
//  1. a statically declared class reports exactly &Type::static_meta_object;
//  2. a run-time built class reports the DYNAMIC class name through the same virtual accessor, and a
//     SIGNAL declared in that class resolves/emits through the run-time table (proving the virtual
//     path — the static object_t table does not even know the signal);
//  3. meta_object_builder_t::build() hands back a meta-object that is queryable on its own.

#include <meta>
#include <cstdio>
#include <string_view>

#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/dynamic_base_t.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_object_builder_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::metaobject;

namespace meta_object_identity_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// (1) A statically reflected class: exactly one meta-object, owned by the class.
struct static_probe_t : object_t {
    OBJECT
public:
    int hits = 0;
    int last = 0;

    void on_pinged(int v) {
        ++hits;
        last = v;
    }
};
META_OBJECT(static_probe_t);

/// (2) A run-time built class. It has NO OBJECT, hence no static_meta_object at all: the only
/// meta-object it has is the one the mixin owns, which is exactly what meta_object() must report.
class dynamic_probe_t : public object_t, public dynamic_base_t {
public:
    explicit dynamic_probe_t(const meta_object_t* meta, object_t* parent = nullptr)
        : dynamic_base_t(meta) {
        if (parent != nullptr) {
            set_parent(parent);
        }
    }

    DYNAMIC_BASE
public:

    /// Declared in C++ and registered in the run-time table by the builder below under the same
    /// signature ("pinged(int) const"). If the emitter went through a static table this lookup would miss.
    SIGNAL(void pinged(int v) const)
};

/// Where the run-time built signal's own thunk lands (proves the entry exists and is callable).
int g_thunk_hits = 0;

void call_pinged(void* obj, void** params) {
    (void)obj;
    ++g_thunk_hits;
}

}  // namespace meta_object_identity_test

using namespace meta_object_identity_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    std::printf("== meta-object identity ==\n");

    // ---------------------------------------------------------------------
    // (1) Static class: the accessor IS the class's one static meta-object.
    // ---------------------------------------------------------------------
    static_probe_t probe;
    probe.set_parent(root);

    CHECK(probe.meta_object() == &static_probe_t::static_meta_object);
    // The accessor and the static name the same table: a meta_object_t owns its meta_info_t by value
    // and is non-copyable, so the identity half of the old &info() address check is the pointer
    // comparison above, and this line checks that both faces report the same class name.
    CHECK(probe.meta_object()->class_name() == static_probe_t::static_meta_object.class_name());
    CHECK(probe.meta_object()->class_name() == "static_probe_t");
    CHECK(static_probe_t::static_meta_object.class_name() == "static_probe_t");

    // Through a base-class pointer the same virtual call still reports the most derived table.
    object_t* base_view = &probe;
    CHECK(base_view->meta_object() == &static_probe_t::static_meta_object);
    CHECK(base_view->meta_object() == probe.meta_object());

    // Handles taken twice from that one table are interchangeable.
    const meta_method_t slot = probe.meta_object()->method("on_pinged(int)");
    CHECK(slot.is_valid());
    CHECK(slot == static_probe_t::static_meta_object.method("on_pinged(int)"));

    // ---------------------------------------------------------------------
    // (3) The builder's return value is queryable on its own.
    // ---------------------------------------------------------------------
    meta_object_builder_t builder("DynamicProbe");
    builder.add_signal("pinged(int) const").set_caller(&call_pinged);
    const meta_object_t& built = *builder.build();

    CHECK(built.class_name() == "DynamicProbe");
    CHECK(built.method_count() == 1);
    CHECK(built.signal("pinged(int) const").is_valid());
    CHECK(built.method("pinged(int) const").is_signal());
    CHECK(!built.method("on_pinged(int)").is_valid());   // no static table leaked in

    // ---------------------------------------------------------------------
    // (2) Dynamic class: the same accessor reports the DYNAMIC class, and a SIGNAL declared in the
    //     class resolves and emits through the run-time table.
    // ---------------------------------------------------------------------
    dynamic_probe_t widget(&built, root);

    CHECK(widget.meta_object() != &object_t::static_meta_object);
    CHECK(widget.meta_object()->class_name() == "DynamicProbe");     // not "object_t"
    CHECK(widget.meta_object()->class_name() != object_t::static_meta_object.class_name());
    // The static table of the base class knows nothing about "pinged(int) const": the next checks can only
    // pass if the emitter went through the object's own (run-time built) meta-object.
    CHECK(!object_t::static_meta_object.signal("pinged(int) const").is_valid());
    CHECK(widget.meta_object()->signal("pinged(int) const").is_valid());

    static_probe_t listener;
    listener.set_parent(root);
    CHECK(widget.connect("pinged(int) const", &listener, "on_pinged(int)").is_valid());

    widget.pinged(11);                       // the SIGNAL macro's emitter, through the virtual path
    CHECK(listener.hits == 1);
    CHECK(listener.last == 11);

    widget.pinged(-3);
    CHECK(listener.hits == 2);
    CHECK(listener.last == -3);

    // The run-time signal entry really carries the thunk the builder was given. The argument channel
    // is one any_t per parameter, matched against "pinged(int)" by name.
    const meta_method_t runtime_signal = widget.meta_object()->signal("pinged(int) const");
    CHECK(runtime_signal.is_valid());
    const any_t call_args[] = { any_t(0) };
    CHECK(runtime_signal.invoke(widget, call_args).is_valid());
    CHECK(g_thunk_hits == 1);

    // Blocker: no delivery while the signals are blocked (the dispatch is a real one).
    widget.block_signals(true);
    widget.pinged(12);
    widget.block_signals(false);
    CHECK(listener.hits == 2);

    std::printf(g_failures == 0 ? "  meta-object identity: all checks passed\n"
                                : "  meta-object identity: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
