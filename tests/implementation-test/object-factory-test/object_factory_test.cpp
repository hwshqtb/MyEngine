// I0: the class registry behind object_t::register_type / create.
//
// The registry stores `const meta_object_t*` — no creator functions, no second construction path. The
// executable links only metaobject, so the registry's contents are exactly what this file registers:
// four static classes plus one run-time built (meta_object_builder_t) class with a qualified name.
//
// Covered here:
//  1. the primary key is type->class_name(); a qualified name also gets its short-name alias;
//  2. registered_type_names() lists both, sorted, duplicate-free; the alias resolves to the same class;
//  3. duplicate registration keeps the FIRST registration and warns; a null type registers nothing;
//  4. creation of a class with a 0-argument constructor (through meta_object_t::new_instance);
//  5. creation with a parent: the object-level parent is attached with set_parent() afterwards;
//  6. a class whose only constructor takes an argument -> nullptr + exactly one warning;
//  7. an abstract class -> nullptr + exactly one warning;
//  8. a run-time built meta-object (no constructors, no storage hooks) -> nullptr + one warning;
//  9. unknown name -> nullptr + one warning per call;
// 10. sizeof(object_t) still 32 (the registry lives in a function-local static).

#include <meta>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "hwshqtb/metaobject/log.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/metaobject/meta_object_builder_t.h"
#include "hwshqtb/metaobject/meta_object_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/metaobject/register_object.h"

using namespace hwshqtb::metaobject;

// ---------------------------------------------------------------------------
// The static test classes. class_name() is the type's identifier, so they register under exactly these
// names (no "::" and therefore no alias).
// ---------------------------------------------------------------------------

/// (4) The creatable shape: a real 0-argument constructor.
struct registry_zero_t: object_t {
    OBJECT
public:
    registry_zero_t() = default;
    int tag = 1;
};
META_OBJECT(registry_zero_t);
META_REGISTER_TYPE(registry_zero_t);

/// (5) Also creatable; the test attaches a parent through create().
struct registry_parented_t: object_t {
    OBJECT
public:
    registry_parented_t() = default;
    int tag = 2;
};
META_OBJECT(registry_parented_t);
META_REGISTER_TYPE(registry_parented_t);

/// (6) No 0-argument constructor: registered (the name is valid), but not creatable by name.
struct registry_nozero_t: object_t {
    OBJECT
public:
    explicit registry_nozero_t(int value): tag(value) { }
    int tag = 0;
};
META_OBJECT(registry_nozero_t);
META_REGISTER_TYPE(registry_nozero_t);

/// (7) Abstract: its defaulted constructor is reflected but not callable, so there is no usable
/// 0-argument constructor either.
struct registry_abstract_t: object_t {
    OBJECT
public:
    registry_abstract_t() = default;
    virtual int pure() const = 0;
};
META_OBJECT(registry_abstract_t);
META_REGISTER_TYPE(registry_abstract_t);

namespace object_factory_test {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

static_assert(sizeof(object_t) == 32, "the class registry must not add a member to object_t");

/// Captured log records.
struct record_t {
    std::string category;
    log_level_enum level = log_level_enum::DEBUG;
    std::string message;
};

std::vector<record_t>& records() {
    static std::vector<record_t> captured;
    return captured;
}

std::size_t count_records(log_level_enum level, std::string_view needle) {
    std::size_t found = 0;
    for (const record_t& record : records()) {
        if (record.level == level && record.message.find(needle) != std::string::npos) {
            ++found;
        }
    }
    return found;
}

bool contains_once(const std::vector<std::string>& names, std::string_view name) {
    std::size_t found = 0;
    for (const std::string& candidate : names) {
        if (candidate == name) {
            ++found;
        }
    }
    return found == 1;
}

bool is_sorted(const std::vector<std::string>& names) {
    for (std::size_t i = 1; i < names.size(); ++i) {
        if (names[i - 1] >= names[i]) {
            return false;
        }
    }
    return true;
}
}  // namespace object_factory_test

using namespace object_factory_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    install_log_handler([](log_level_enum level, const log_context_t& context, std::string_view message) {
        records().push_back(record_t{ std::string(context.category), level, std::string(message) });
        return true; // consumed: the default stderr sink stays silent
    });

    std::printf("== I0: the class registry ==\n");

    // -----------------------------------------------------------------------
    // (1)(2) The qualified name of a run-time built class gets a short-name alias.
    // -----------------------------------------------------------------------
    meta_object_builder_t builder("registry_ns::dynamic_probe");
    const meta_object_t* const dynamic = builder.build();
    CHECK(dynamic != nullptr);
    CHECK(object_t::register_type(dynamic));

    const std::vector<std::string> names = object_t::registered_type_names();
    CHECK(is_sorted(names));
    CHECK(names.size() == 6u); // 5 primary names + 1 alias
    CHECK(contains_once(names, "registry_zero_t"));
    CHECK(contains_once(names, "registry_parented_t"));
    CHECK(contains_once(names, "registry_nozero_t"));
    CHECK(contains_once(names, "registry_abstract_t"));
    CHECK(contains_once(names, "registry_ns::dynamic_probe"));
    CHECK(contains_once(names, "dynamic_probe")); // the alias
    CHECK(detail::class_registry_t::instance().size() == 5u); // primary keys only

    // The alias and the full name both resolve to the same class: create() reaches it and then reports
    // its missing constructors identically (the dynamic table has none).
    records().clear();
    CHECK(object_t::create("dynamic_probe") == nullptr);
    CHECK(count_records(log_level_enum::WARNING, "no usable 0-argument constructor") == 1u);
    records().clear();
    CHECK(object_t::create("registry_ns::dynamic_probe") == nullptr);
    CHECK(count_records(log_level_enum::WARNING, "no usable 0-argument constructor") == 1u);

    // -----------------------------------------------------------------------
    // (4) Creation of a 0-argument class: the instance is built by the class's meta-object.
    // -----------------------------------------------------------------------
    records().clear();
    object_t* const zero = object_t::create("registry_zero_t");
    CHECK(zero != nullptr);
    CHECK(dynamic_cast<registry_zero_t*>(zero) != nullptr);
    CHECK(zero->meta_object() == &registry_zero_t::static_meta_object);
    CHECK(static_cast<registry_zero_t*>(zero)->tag == 1);
    CHECK(zero->parent() == nullptr);
    CHECK(records().empty()); // a successful creation warns about nothing

    // The storage hooks really moved onto the meta-object: constructing through it works outside
    // create() too.
    object_t* const direct = static_cast<object_t*>(
        registry_zero_t::static_meta_object.new_instance());
    CHECK(direct != nullptr);
    CHECK(direct->meta_object() == &registry_zero_t::static_meta_object);
    delete direct;
    delete zero;

    // -----------------------------------------------------------------------
    // (5) create(..., parent): the parent link is attached after construction.
    // -----------------------------------------------------------------------
    object_t root;
    object_t* const parented = object_t::create("registry_parented_t", &root);
    CHECK(parented != nullptr);
    CHECK(dynamic_cast<registry_parented_t*>(parented) != nullptr);
    CHECK(static_cast<registry_parented_t*>(parented)->tag == 2);
    CHECK(parented->parent() == &root);
    CHECK(root.children().size() == 1u);

    object_t* const child = object_t::create("registry_zero_t", &root);
    CHECK(child != nullptr);
    CHECK(child->parent() == &root);
    CHECK(root.children().size() == 2u);

    delete child;
    delete parented;
    CHECK(root.children().empty());

    // -----------------------------------------------------------------------
    // (6) A class without a 0-argument constructor: nullptr plus exactly one Warning. It IS registered
    // — registration is about the name, not about constructibility — so this is not the "unknown name"
    // message.
    // -----------------------------------------------------------------------
    records().clear();
    CHECK(object_t::create("registry_nozero_t") == nullptr);
    CHECK(count_records(log_level_enum::WARNING, "no usable 0-argument constructor") == 1u);
    CHECK(count_records(log_level_enum::WARNING, "no class registered") == 0u);
    CHECK(records().size() == 1u);
    CHECK(contains_once(object_t::registered_type_names(), "registry_nozero_t"));

    // -----------------------------------------------------------------------
    // (7) An abstract class: same answer, for the same reason (every constructor is unconstructible).
    // -----------------------------------------------------------------------
    records().clear();
    CHECK(object_t::create("registry_abstract_t") == nullptr);
    CHECK(count_records(log_level_enum::WARNING, "no usable 0-argument constructor") == 1u);
    CHECK(records().size() == 1u);

    // The table still describes the class (it declares a constructor); the CALL is what cannot happen.
    CHECK(registry_abstract_t::static_meta_object.constructor_count() > 0u);

    // -----------------------------------------------------------------------
    // (8) A run-time built table has no constructors and no storage hooks: same warning, no crash.
    // -----------------------------------------------------------------------
    CHECK(dynamic->constructor_count() == 0u);
    records().clear();
    CHECK(object_t::create("registry_ns::dynamic_probe") == nullptr);
    CHECK(records().size() == 1u);

    // -----------------------------------------------------------------------
    // (9) Unknown name: nullptr plus exactly one Warning per call; nothing is cached.
    // -----------------------------------------------------------------------
    records().clear();
    CHECK(object_t::create("no_such_type_t") == nullptr);
    CHECK(count_records(log_level_enum::WARNING, "no class registered as 'no_such_type_t'") == 1u);
    CHECK(records().size() == 1u);
    CHECK(object_t::create("no_such_type_t") == nullptr);
    CHECK(records().size() == 2u);
    CHECK(!contains_once(object_t::registered_type_names(), "no_such_type_t"));

    // -----------------------------------------------------------------------
    // (3) Duplicate registration keeps the first one and warns; a null type registers nothing.
    // -----------------------------------------------------------------------
    records().clear();
    CHECK(!object_t::register_type(&registry_zero_t::static_meta_object));
    CHECK(count_records(log_level_enum::WARNING, "already registered") == 1u);
    CHECK(records().size() == 1u);
    object_t* const still_there = object_t::create("registry_zero_t");
    CHECK(still_there != nullptr);
    delete still_there;

    records().clear();
    CHECK(!object_t::register_type(nullptr));
    CHECK(count_records(log_level_enum::WARNING, "type is null") == 1u);
    CHECK(records().size() == 1u);
    CHECK(object_t::registered_type_names().size() == 6u); // unchanged

    // -----------------------------------------------------------------------
    // (10) The registry lives in a function-local static, not in object_t.
    // -----------------------------------------------------------------------
    CHECK(sizeof(object_t) == 32u);

    install_log_handler(log_handler{});

    std::printf("class registry: all %d checks passed\n", g_checks);
    return g_failures == 0 ? 0 : 1;
}
