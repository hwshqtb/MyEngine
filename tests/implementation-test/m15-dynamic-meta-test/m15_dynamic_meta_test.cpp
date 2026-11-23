// M15: a meta-object built at run time is indistinguishable from a compile-time one.

#include <meta>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include <hwshqtb/metaobject/annotation.h>
#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/dynamic_base_t.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_enum_builder_t.h>
#include <hwshqtb/metaobject/meta_enum_t.h>
#include <hwshqtb/metaobject/meta_method_builder_t.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_object_builder_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/meta_property_builder_t.h>
#include <hwshqtb/metaobject/meta_property_t.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::metaobject;

namespace m15_dynamic_meta_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// The dynamic type: object_t + the mixin. This is what "dynamic_xxx" means.
class dynamic_widget_t : public object_t, public dynamic_base_t {
public:
    explicit dynamic_widget_t(const meta_object_t* meta, object_t* parent = nullptr)
        : dynamic_base_t(meta) {
        if (parent != nullptr) {
            set_parent(parent);
        }
    }
    DYNAMIC_BASE
};
// ---- user-written thunks: the library brings no dynamic-invoke machinery ----

void call_set_value(void* obj, void** params) {
    static_cast<dynamic_widget_t*>(obj)->set_dynamic_value(
        "value", any_t(*static_cast<const double*>(params[1])));
}

void read_value(void* obj, void** params) {
    const any_t stored = static_cast<dynamic_widget_t*>(obj)->dynamic_value("value");
    *static_cast<double*>(params[0]) = stored.is_initialized() ? stored.get<double>() : 0.0;
}

/// A notifier thunk for a run-time built property. Only its existence matters here: is_notifiable() is
/// the notifier fact (M17 stage 3), and this test asserts that query, not the emission.
void notify_value(void*, void**) { }

/// A static object the dynamic type can talk to.
struct static_listener_t : object_t {
    OBJECT
public:
    int hits = 0;
    double last = 0.0;

    void on_dynamic_value(double value) {
        ++hits;
        last = value;
    }
};
META_OBJECT(static_listener_t);

/// A static base used to check inherits() across the dynamic/static boundary.
struct static_base_t : object_t {
    OBJECT
public:
    [[= make_property("base_value")]]
    int base_value() const { return 7; }
};
META_OBJECT(static_base_t);

}  // namespace m15_dynamic_meta_test

using namespace m15_dynamic_meta_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    std::printf("== M15: dynamic meta-object ==\n");

    // (1) Build a type with one method, one property, one enum and one class info.
    meta_object_builder_t builder("DynamicWidget", &static_base_t::static_meta_object);
    builder.add_method("setValue(double)").set_caller(&call_set_value);
    builder.add_signal("valueChanged(double)").set_caller(&call_set_value);
    meta_property_builder_t value = builder.add_property("value", "double");
    value.set_reader(&read_value);
    value.set_writer(&call_set_value);
    meta_enum_builder_t mode_builder = builder.add_enumerator("Mode");
    mode_builder.add_key("None", 0);
    mode_builder.add_key("Fast", 1);
    mode_builder.add_key("Safe", 2);
    mode_builder.set_is_flag(true);   // 0 / 1 / 2 are pairwise disjoint; the old builder derived this
    builder.add_class_info("Author", "hwshqtb");
    const meta_object_t& meta_object = *builder.build();

    CHECK(builder.build() == &meta_object);   // idempotent
    CHECK(meta_object.class_name() == "DynamicWidget");

    // (2) The method table resolves and invokes through the user thunk.
    CHECK(meta_object.method("setValue(double)").is_valid());
    CHECK(meta_object.signal("valueChanged(double)").is_valid());
    CHECK(meta_object.method("valueChanged(double)").is_signal());
    CHECK(!meta_object.method("setValue(double)").is_signal());
    CHECK(!meta_object.method("noSuchMethod()").is_valid());
    // A run-time built entry carries no access information, so it reports PUBLIC_SCOPE (the
    // documented default of method_access_enum for run-time built tables).
    CHECK(meta_object.method("setValue(double)").access() == method_access_enum::PUBLIC_SCOPE);
    CHECK(meta_object.method("noSuchMethod()").access() == method_access_enum::NONE);

    dynamic_widget_t widget(&meta_object, root);
    // The virtual hook reports the DYNAMIC class — object_t's static table would say "object_t".
    CHECK(widget.meta_object()->class_name() == "DynamicWidget");
    CHECK(widget.meta_object()->method("setValue(double)").is_valid());

    // The argument channel is one any_t per parameter: the value carries its own type, and the
    // run-time built entry matches it against its parameter by name.
    double written = 2.5;
    const any_t invoke_args[] = { any_t(written) };
    CHECK(meta_object.method("setValue(double)").invoke(widget, invoke_args).is_valid());
    CHECK(widget.dynamic_value("value").get<double>() == 2.5);

    // (3) The property table reads and writes through the user thunks.
    const meta_property_t value_property = meta_object.property("value");
    CHECK(value_property.is_valid());
    CHECK(value_property.is_writable());
    CHECK(value_property.is_valid());           // readable: a valid row always has a reader

    CHECK(value_property.read(widget).get<double>() == 2.5);

    CHECK(widget.set_property("value", any_t(4.5)));
    CHECK(widget.dynamic_value("value").get<double>() == 4.5);
    CHECK(widget.property("value").get<double>() == 4.5);   // the value interface also works

    // (4) Enumerations.
    const meta_enum_t mode = meta_object.enumeration("Mode");
    CHECK(mode.is_valid());
    CHECK(mode.name() == "Mode");
    CHECK(mode.key_count() == 3);
    CHECK(mode.key_to_value("Fast") == 1);
    CHECK(mode.value_to_key(2) == "Safe");
    CHECK(mode.is_flag());                     // 0 / 1 / 2 are pairwise disjoint under &

    // (5) Class info.
    CHECK(meta_object.class_info_count() == 1);
    CHECK(meta_object.class_info("Author").value() == "hwshqtb");

    // (6) A dynamic type inherits a static base and sees its properties through the chain.
    CHECK(meta_object.super_class() == &static_base_t::static_meta_object);
    CHECK(meta_object.inherits("object_t"));
    CHECK(meta_object.property("base_value").is_valid());

    // (7) A dynamic object connects to a static one.
    static_listener_t listener;
    listener.set_parent(root);
    CHECK(widget.connect("valueChanged(double)", &listener, "on_dynamic_value(double)").is_valid());
    double emitted = 9.0;
    void* emit_args[2] = { nullptr, &emitted };
    // Emit through the OBJECT's own facade: that is the table the connection was resolved against.
    widget.activate_signal(widget.meta_object()->signal("valueChanged(double)"), emit_args);
    CHECK(listener.hits == 1);
    CHECK(listener.last == 9.0);

    // (8) Validation: two User properties and Required+Hidden still fail at build(). The bindable
    // trait is gone, so there is no "Bindable without a writer" rule left to violate. The property
    // handle now exposes every trait the row stores (set_required / set_hidden / set_final), so the
    // Required+Hidden construction the flat builder could spell is expressible again.
    bool threw_two_user = false;
    try {
        meta_object_builder_t bad("BadUser");
        meta_property_builder_t a = bad.add_property("a", "int");
        a.set_reader(&read_value);
        a.set_user(true);
        meta_property_builder_t b = bad.add_property("b", "int");
        b.set_reader(&read_value);
        b.set_user(true);
        bad.build();
    }
    catch (const std::invalid_argument&) {
        threw_two_user = true;
    }
    CHECK(threw_two_user);

    bool threw_required_hidden = false;
    try {
        meta_object_builder_t bad("BadTraits");
        meta_property_builder_t x = bad.add_property("x", "int");
        x.set_reader(&read_value);
        x.set_required(true);
        x.set_hidden(true);
        bad.build();
    }
    catch (const std::invalid_argument&) {
        threw_required_hidden = true;
    }
    CHECK(threw_required_hidden);

    // M17 stage 3: bindability is value-derived — a property is bindable iff it has a notifier, so
    // is_notifiable() is the whole query. The same writable property answers true only when the
    // builder is given a notifier.
    {
        meta_object_builder_t bindable_ok("BindableOk");
        meta_property_builder_t bindable_value = bindable_ok.add_property("value", "int");
        bindable_value.set_reader(&read_value);
        bindable_value.set_writer(&call_set_value);
        meta_method_builder_t value_changed = bindable_ok.add_signal("valueChanged()");
        value_changed.set_caller(&notify_value);
        bindable_value.set_notifier(value_changed);
        const meta_object_t& built = *bindable_ok.build();
        CHECK(built.property("value").is_notifiable());
        CHECK(built.property("value").is_notifiable());     // the notifier is the bindable fact
    }
    {
        meta_object_builder_t not_notifiable("NotNotifiable");
        meta_property_builder_t plain_value = not_notifiable.add_property("value", "int");
        plain_value.set_reader(&read_value);
        plain_value.set_writer(&call_set_value);
        const meta_object_t& built = *not_notifiable.build();
        CHECK(!built.property("value").is_notifiable());
        CHECK(!built.property("value").is_notifiable());
    }

    // (9) The meta-object outlives the object that used it: it lives in the process-wide arena,
    // which is never destroyed.
    {
        meta_object_builder_t short_lived("ShortLived");
        short_lived.add_method("ping(int)").set_caller(&call_set_value);
        short_lived.add_class_info("Key", "Value");
        const meta_object_t& kept = *short_lived.build();
        {
            dynamic_widget_t temporary(&kept, root);
            CHECK(temporary.meta_object()->class_name() == "ShortLived");
        }                                        // the object is gone here
        CHECK(kept.method("ping(int)").is_valid()); // the meta-object is still usable
        CHECK(kept.class_info("Key").value() == "Value");
    }
    // (10) Parameter splitting honours bracket depth: "map<int,int>" is one parameter, not two.
    {
        meta_object_builder_t nested("NestedParams");
        nested.add_method("configure(std::map<int,int>, const std::string&)").set_caller(&call_set_value);
        const meta_object_t& built = *nested.build();     // the builder's return value is queryable
        // L7: the table key is composed from the IDENTITY parameter spellings the row STORES, so it
        // spells `const std::__cxx11::basic_string<char>&` and agrees with param_type_name(1). The
        // user-text normaliser (normalized_signature) resolves every known alias spelling to that one
        // identity spelling, and meta_object_t::method() is a strict exact lookup, hence the alias and
        // canonical spellings do not name this row while the identity spelling does. The BUILDER's
        // index_of_method() canonicalises its argument the same way, so all three spellings find the
        // row before build().
        CHECK(nested.index_of_method("configure(std::map<int,int>, const std::string&)") == 0);
        CHECK(nested.index_of_method(
                  "configure(std::map<int,int>,const std::basic_string<char>&)") == 0);
        const meta_method_t m = built.method(
            "configure(std::map<int,int>,const std::__cxx11::basic_string<char>&)");
        CHECK(m.is_valid());
        CHECK(m.param_count() == 2);
        CHECK(m.signature() == "configure(std::map<int,int>,const std::__cxx11::basic_string<char>&)");
        CHECK(m.param_type_name(1) == "const std::__cxx11::basic_string<char>&");
        CHECK(!built.method("configure(std::map<int,int>,const std::string&)").is_valid());
    }
    std::printf(g_failures == 0 ? "  dynamic meta-object: all checks passed\n"
                                : "  dynamic meta-object: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}