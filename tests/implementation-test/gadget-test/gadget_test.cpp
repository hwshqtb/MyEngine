// GADGET: the value-type meta-object (Q_GADGET equivalent).
//
// OBJECT declares a static table PLUS a virtual accessor. That is right for an object_t — the
// accessor is the one virtual function dynamic_base_t overrides — but wrong for a plain value type:
// a vtable inflates sizeof, makes the type non-trivially-copyable, and rules out unions / by-value
// ABI passing. GADGET declares the same static table WITHOUT the virtual function and WITHOUT the
// accessor; META_OBJECT(TYPE) defines it exactly as before.
//
// What this test pins down:
//  1. class name: TYPE::static_meta_object.class_name() == "point_t";
//  2. properties: both resolve (name / type / writer), and read() on a gadget returns the value
//     stored by the setter;
//  3. methods: "length_squared() const" resolves and invoke() on a point_t returns the right result;
//  4. enumerations: "axis" resolves (a gadget keeps property / method / enum / class-info support);
//  5. new_instance(): a gadget is constructible — its constructor entry carries the same construct
//     thunk as an object_t class, the thunk writes the new point_t as a void*, and the caller casts
//     it back to the class it named;
//  6. sizeof(point_t) == 2 * sizeof(int) — no vtable;
//  7. std::is_trivially_copyable_v<point_t> is true;
//  8. there is no meta_object() member at all.
//
// Assertions 6-8 are the point of the exercise: they are static_asserts ON PURPOSE, so a GADGET
// that accidentally grew a vtable or an accessor fails the build instead of a runtime check.
//
// any_t also holds a point_t: a gadget is registered through the ordinary object-tree registry
// (here the application root), exactly like any other value type.

#include <meta>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <hwshqtb/metaobject/annotation.h>
#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_enum_t.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/meta_property_t.h>
#include <hwshqtb/metaobject/type_interface_t.h>

using namespace hwshqtb::metaobject;

// ---------------------------------------------------------------------------
// The value type under test. Deliberately at global scope: a type declared inside an anonymous
// namespace has no stable global name, so the canonical namer reports it as an error. Global scope
// gives it the plain, stable name "point_t". Only this translation unit uses it anyway.
//
// Note the order inside the class: GADGET opens with `public:` and closes with `private:`, exactly
// like OBJECT, so the following `public:` restores the intended access for the reflected members.
// ---------------------------------------------------------------------------
struct point_t {
    GADGET

public:
    // Accessors are named by BARE NAME; build_meta resolves them by reflecting these declarations
    // (writer: one parameter of the property type — int here).
    [[= make_property("x", "set_x")]]
    int x() const { return _x; }
    void set_x(int v) { _x = v; }

    [[= make_property("y", "set_y")]]
    int y() const { return _y; }
    void set_y(int v) { _y = v; }

    int length_squared() const { return _x * _x + _y * _y; }

    enum class axis { horizontal, vertical };

private:
    int _x = 0, _y = 0;
};

// No metatype declaration is needed: the type's interface is produced on demand from its reflection
// (the identity name is the canonical namer's spelling), so META_OBJECT may follow immediately.
META_OBJECT(point_t);

// ---------------------------------------------------------------------------
// Assertions 6-8: the actual point of GADGET. These are compile-time on purpose — if any of them
// fails, the design regressed and the build must fail loudly, not a run-time check.
//
// Assertion 8 is written as a concept, not as a bare
//     static_assert(!requires(const point_t& p) { p.meta_object(); });
// A requires-expression OUTSIDE a template is not a "substitution" context: its requirements are
// checked at definition time, so an invalid member access there is ill-formed (GCC 16.2 says
// "'const struct point_t' has no member named 'meta_object'") instead of yielding false. The
// dependent form is the standard way to ask the question, and it is exactly the same check.
// ---------------------------------------------------------------------------
template <typename T>
concept has_meta_object_member = requires(const T& p) { p.meta_object(); };

static_assert(sizeof(point_t) == 2 * sizeof(int),
    "GADGET must not add a vtable or any data member: sizeof(point_t) must stay 2 * sizeof(int)");
static_assert(std::is_trivially_copyable_v<point_t>,
    "GADGET must keep the value type trivially copyable");
static_assert(!std::is_polymorphic_v<point_t>,
    "GADGET must not introduce a virtual function (no vtable)");
static_assert(!has_meta_object_member<point_t>,
    "GADGET must not declare a meta_object() member: the table is reached as TYPE::static_meta_object");

namespace gadget_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// ---------------------------------------------------------------------------
// Log capture: construction failures are reported through log_connection; a successful construction
// records nothing. Keep the default stderr sink silent and assert on the record count.
// ---------------------------------------------------------------------------
struct record_t {
    std::string category;
    log_level_enum level = log_level_enum::DEBUG;
    std::string message;
};

std::vector<record_t>& records() {
    static std::vector<record_t> captured;
    return captured;
}

}  // namespace gadget_test

using namespace gadget_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    // No registration call: point_t's interface is produced on demand from its reflection (above)
    // and the builtins are seeded lazily in the one process-wide registry.
    core_application app(argc, argv);

    install_log_handler([](log_level_enum level, const log_context_t& context, std::string_view message) {
        records().push_back(record_t{ std::string(context.category), level, std::string(message) });
        return true;  // consumed: the default stderr sink stays silent
    });

    std::printf("== GADGET: value-type meta-object ==\n");

    const meta_object_t& meta = point_t::static_meta_object;

    // -----------------------------------------------------------------------
    // (1) Identity: the static table carries the class name.
    // -----------------------------------------------------------------------
    CHECK(meta.class_name() == "point_t");
    CHECK(!meta.has_super_class());

    // -----------------------------------------------------------------------
    // (2) Properties: both resolve, and read() runs the getter on a point_t.
    //     A GADGET has no tree, so the property type is resolved through the global registry; the
    //     value itself is read by the very same thunk an object_t property uses.
    // -----------------------------------------------------------------------
    const meta_property_t px = meta.property("x");
    const meta_property_t py = meta.property("y");
    CHECK(px.is_valid());
    CHECK(py.is_valid());
    CHECK(meta.property_count() == 2u);
    CHECK(px.name() == "x");
    CHECK(py.name() == "y");
    CHECK(px.type_name() == "int");
    CHECK(py.type_name() == "int");
    CHECK(meta.index_of_property("x") != index_npos);
    CHECK(meta.index_of_property("y") != index_npos);

    // The writer resolves the named setter and is an ordinary named method handle.
    CHECK(px.writer().is_valid());
    CHECK(px.writer().name() == "set_x");
    CHECK(py.writer().is_valid());
    CHECK(py.writer().name() == "set_y");
    CHECK(px.is_writable());
    CHECK(!px.is_resettable());
    // Audit of the migrated annotations: every accessor the bare names declare resolves through the
    // reflection matching (a writer here; no notifier / reseter is declared).
    CHECK(!px.is_notifiable());
    CHECK(py.is_writable());
    CHECK(!py.is_resettable());
    CHECK(px.writer().signature() == "set_x(int)");
    CHECK(py.writer().signature() == "set_y(int)");

    point_t p;
    p.set_x(3);
    p.set_y(4);

    // read(p) — the gadget overload of meta_property_t::read.
    const any_t value_x = px.read(p);
    const any_t value_y = py.read(p);
    CHECK(value_x.is_initialized());
    CHECK(value_y.is_initialized());
    CHECK(value_x.is<int>());
    CHECK(value_y.is<int>());
    CHECK(value_x.get<int>() == 3);
    CHECK(value_y.get<int>() == 4);

    // The values follow the object: change it, read again.
    p.set_x(9);
    CHECK(px.read(p).get<int>() == 9);

    // The gadget write() overload runs the named setter through the same thunk.
    CHECK(px.write(p, any_t(11)));
    CHECK(p.x() == 11);
    CHECK(px.read(p).get<int>() == 11);
    p.set_x(3);

    // -----------------------------------------------------------------------
    // (3) Methods: "length_squared() const" resolves and invoke on a point_t returns the right result.
    // -----------------------------------------------------------------------
    const meta_method_t length = meta.method("length_squared() const");
    CHECK(length.is_valid());
    // "resolved to a table entry": the handle has a non-empty signature. (has_name() used to spell
    // this; the predicate is signature().empty() now — is_valid() alone would not do, because a
    // deleted / abstract constructor is in the table yet not callable.)
    CHECK(!length.signature().empty());
    CHECK(length.access() == method_access_enum::PUBLIC_SCOPE);   // the only specifier it declares
    CHECK(length.name() == "length_squared");
    CHECK(length.signature() == "length_squared() const");
    CHECK(length.param_count() == 0u);
    CHECK(length.return_type_name() == "int");

    int result = 0;
    CHECK(meta.invoke(p, "length_squared() const", result));
    CHECK(result == 25);                       // 3 * 3 + 4 * 4
    CHECK(length.invoke(p).is_valid());        // the same entry through the handle, result ignored

    // The setter is a reflected method too, and the SIGNAL machinery is not involved at all.
    // A PRVALUE argument: with the object first, an lvalue in the third position is the out slot, so
    // a single by-value argument is spelled as a value (the erased form is the lvalue path).
    CHECK(meta.invoke(p, "set_x(int)", 7).is_valid());
    CHECK(p.x() == 7);
    p.set_x(3);

    // -----------------------------------------------------------------------
    // (4) Enumerations: enums declared inside a gadget are in the table.
    // -----------------------------------------------------------------------
    const meta_enum_t axis = meta.enumeration("axis");
    CHECK(axis.is_valid());
    CHECK(axis.name() == "axis");
    CHECK(axis.scope() == "point_t");
    CHECK(axis.is_scoped());
    CHECK(axis.key_count() == 2u);
    CHECK(axis.key(0) == "horizontal");
    CHECK(axis.key(1) == "vertical");
    CHECK(axis.key_to_value("vertical") == std::optional<std::int64_t>(1));
    CHECK(axis.value_to_key(0) == "horizontal");

    // -----------------------------------------------------------------------
    // (5) new_instance(): a gadget is constructible. Its constructor entry carries the same construct
    //     thunk as an object_t class; the thunk writes the new point_t as a void*, and the caller
    //     casts it back to the class it named. A successful construction logs nothing.
    // -----------------------------------------------------------------------
    {
        const std::size_t before = records().size();
        auto* built = static_cast<point_t*>(meta.new_instance());
        CHECK(built != nullptr);
        CHECK(records().size() == before);            // a successful construction logs nothing
        if (built != nullptr) {
            CHECK(built->x() == 0);                   // the constructed values (point_t's defaults)
            CHECK(built->y() == 0);
            CHECK(built->length_squared() == 0);      // and its methods work on the new value
            delete built;                             // ordinary delete: the storage came from new
        }

        // The any_t entry point: the out slot is an alias to the caller's pointer variable, and the
        // constructor thunk writes the new instance's address through it.
        const std::size_t before_aliased = records().size();
        point_t* aliased_built = nullptr;
        any_t aliased_out(std::ref(aliased_built));
        CHECK(meta.new_instance(std::span<const any_t>{}, aliased_out));
        CHECK(aliased_built != nullptr);
        CHECK(records().size() == before_aliased);
        delete aliased_built;

        // The forwarding overload boxes the empty argument list and delegates to the any_t form.
        const std::size_t before_typed = records().size();
        auto* typed_built = static_cast<point_t*>(meta.new_instance());
        CHECK(typed_built != nullptr);
        CHECK(records().size() == before_typed);
        if (typed_built != nullptr) {
            CHECK(typed_built->x() == 0);
            CHECK(typed_built->y() == 0);
            delete typed_built;
        }

        // A constructor entry really is in the table (the table describes the type) and the call
        // builds the value that was named.
        CHECK(meta.index_of_constructor("point_t()") != index_npos);

        // The pre-existing point_t was not touched by the construction.
        CHECK(p.x() == 3);
    }

    // -----------------------------------------------------------------------
    // (6 + 7) The static values, printed so the verification log carries them.
    // -----------------------------------------------------------------------
    std::printf("  sizeof(point_t) = %zu bytes (2 * sizeof(int) = %zu)\n",
        sizeof(point_t), 2 * sizeof(int));
    std::printf("  std::is_trivially_copyable_v<point_t> = %s\n",
        std::is_trivially_copyable_v<point_t> ? "true" : "false");
    std::printf("  std::is_polymorphic_v<point_t> = %s\n",
        std::is_polymorphic_v<point_t> ? "true" : "false");
    std::printf("  has meta_object() member = %s\n",
        has_meta_object_member<point_t> ? "yes" : "no");

    // -----------------------------------------------------------------------
    // any_t holds a gadget: point_t is registered through the ordinary object-tree registry (the
    // application root), exactly like every other value type.
    // -----------------------------------------------------------------------
    point_t stored;
    stored.set_x(5);
    stored.set_y(6);
    any_t held = stored;
    CHECK(held.is_initialized());
    CHECK(held.is<point_t>());
    CHECK(held.type().name() == "point_t");
    CHECK(bool(held.type().tag() & type_tag_flag::IS_TRIVIALLY_COPYABLE));

    const point_t copy = held.get<point_t>();
    CHECK(copy.x() == 5);
    CHECK(copy.y() == 6);
    CHECK(copy.length_squared() == 61);

    install_log_handler({ });
    std::printf(g_failures == 0 ? "  gadget: all checks passed\n"
                                : "  gadget: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
