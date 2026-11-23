// M11 test: dynamic properties (QObject::setProperty's undeclared branch) and class info.

#include <meta>
#include <cstdio>
#include <string>

#include <hwshqtb/metaobject/annotation.h>
#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/event_t.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_property_t.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::metaobject;

namespace m11_dynamic_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

struct [[= make_class_info("Author", "hwshqtb")]] info_base : object_t {
    OBJECT
public:
    [[= make_property("value")]]
    int value() const { return 1; }
};
META_OBJECT(info_base);

struct [[= make_class_info("Version", "1.0")]] info_derived : info_base {
    OBJECT
public:
    // Bare name: build_meta resolves the writer by reflecting set_extra(int) (one parameter of the
    // property type int).
    [[= make_property("extra", "set_extra")]]
    int extra() const { return _extra; }

    void set_extra(int v) { _extra = v; }

    int _extra = 2;
};
META_OBJECT(info_derived);

/// Counts DynamicPropertyChange events reaching this object.
struct change_probe : object_t {
    OBJECT
public:
    int changes = 0;
    bool event(event_t* e) override {
        if (e->type() == event_type_enum::DYNAMIC_PROPERTY_CHANGE) {
            ++changes;
            e->accept();
            return true;
        }
        return object_t::event(e);
    }
};
META_OBJECT(change_probe);

}  // namespace m11_dynamic_test

using namespace m11_dynamic_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    std::printf("== M11: dynamic properties and class info ==\n");

    auto* p = new info_derived;
    p->set_parent(root);

    // (1) An undeclared name becomes a dynamic property.
    CHECK(p->set_property("nope", any_t(42)));
    const any_t got = p->property("nope");
    CHECK(got.is_initialized());
    CHECK(got.get<int>() == 42);
    const auto names = p->dynamic_property_names();
    CHECK(names.size() == 1);
    CHECK(names[0] == "nope");

    // (2) Writing the same name overwrites instead of adding a second entry.
    CHECK(p->set_property("nope", any_t(43)));
    CHECK(p->dynamic_property_names().size() == 1);
    CHECK(p->property("nope").get<int>() == 43);

    // (3) A declared property still goes through its writer, not the dynamic table.
    CHECK(p->set_property("extra", any_t(7)));
    CHECK(p->property("extra").get<int>() == 7);
    CHECK(p->dynamic_property_names().size() == 1);
    // Audit of the migrated annotation: the bare writer name resolved through reflection.
    const meta_property_t extra = p->meta_object()->property("extra");
    CHECK(extra.is_valid());
    CHECK(extra.is_writable());
    CHECK(extra.writer().is_valid());
    CHECK(extra.writer().signature() == "set_extra(int)");

    // (4) A missing dynamic property yields an invalid value.
    CHECK(!p->property("missing").is_initialized());

    // (5) Each dynamic write posts a DynamicPropertyChange event.
    auto* probe = new change_probe;
    probe->set_parent(root);
    probe->set_property("dyn", any_t(1));
    CHECK(probe->changes == 0);                 // posted, not dispatched yet
    event_dispatcher::instance().send_posted_events();
    CHECK(probe->changes == 1);
    probe->set_property("dyn", any_t(1));       // same value still notifies (Qt semantics)
    event_dispatcher::instance().send_posted_events();
    CHECK(probe->changes == 2);

    // (6) Class info: own entry plus the inherited one.
    CHECK(p->meta_object()->class_info_count() == 2);
    const auto author = p->meta_object()->class_info("Author");
    CHECK(author.is_valid());
    CHECK(author.name() == "Author");
    CHECK(author.value() == "hwshqtb");
    const auto version = p->meta_object()->class_info("Version");
    CHECK(version.is_valid());
    CHECK(version.value() == "1.0");

    // (7) By-index and by-name agree.
    const index_t author_index = p->meta_object()->index_of_class_info("Author");
    CHECK(author_index != index_npos);
    CHECK(p->meta_object()->class_info(author_index).name() == "Author");

    // (8) A missing key reports index_npos and an invalid view.
    CHECK(p->meta_object()->index_of_class_info("Missing") == index_npos);
    CHECK(!p->meta_object()->class_info("Missing").is_valid());
    CHECK(p->meta_object()->class_info("Missing").name().empty());

    std::printf(g_failures == 0 ? "  M11: all checks passed\n" : "  M11: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}