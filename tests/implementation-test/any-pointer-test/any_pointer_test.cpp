// any_t object-pointer storage test (QPointer-in-QVariant; qPointerFromVariant equivalent).
//
// Every object pointer is stored under the single object_pointer_t<object_t> builtin, so a derived
// type never has to be passed to meta_type_t::register_type<Derived>() — that is the point of this test. The
// stored value is a live guard (it shares the target's death token), and pointer_from_any<T>()
// recovers it, down-casting with dynamic_cast so a mismatched target yields an empty guard.

#include <meta>
#include <cstdio>
#include <string>

#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_info_t.h>
#include <hwshqtb/metaobject/meta_type_t.h>
#include <hwshqtb/metaobject/object_pointer_t.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::metaobject;

namespace any_pointer_test {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// Declares a meta-object (META_OBJECT) but is deliberately never passed to meta_type_t::register_type<widget_t>():
/// storing it in an any_t must not need that.
struct widget_t : object_t {
    OBJECT
public:
    int value = 0;
};
META_OBJECT(widget_t);

/// Multiple inheritance with object_t NOT first: the object_t sub-object sits at a non-zero offset,
/// so storing the pointer has to apply the base adjustment and recovering it has to undo it.
struct iface_t {
    virtual ~iface_t() = default;
    virtual int identity() const {
        return 7;
    }
};

struct multi_t : iface_t, object_t {
    int payload = 0;
};

/// A second, unrelated object_t-derived type, for the type-mismatch case.
struct other_t : object_t { };

/// Compile-time probes for the const-target rules: the object-pointer overloads require a non-const
/// T, so a const pointer still goes through the generic value path and a const guard has no viable
/// overload at all (same constraint as the constructors).
template <typename T>
concept pointer_assignable_to_any = requires(any_t& target, T* pointer) { target = pointer; };

template <typename T>
concept guard_assignable_to_any = requires(any_t& target, const object_pointer_t<T>& guard) { target = guard; };

}  // namespace any_pointer_test

using namespace any_pointer_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    std::printf("== any_t: object pointers ==\n");

    // (8) The object-pointer entry is ONE interface produced on demand at compile time (the
    //     Q_DECLARE_METATYPE model's shared entry), so storing an object pointer works WITHOUT any
    //     bootstrap call. The previous "throws bad_meta_type_t before register_builtin_types()"
    //     behaviour is gone — pinned here.
    {
        object_t stand_alone; // a bare tree: no registry setup at all
        any_t stored(&stand_alone);
        CHECK(stored.is_initialized());
        CHECK(stored.type().is_object_pointer_trait());
        CHECK(pointer_from_any<object_t>(stored).data() == &stand_alone);

        object_pointer_t<object_t> guard(&stand_alone);
        any_t stored_guard(guard);
        CHECK(stored_guard.is_initialized());
        CHECK(pointer_from_any<object_t>(stored_guard).data() == &stand_alone);
    }

    // (1) A META_OBJECT-derived type that was never declared or registered can be stored, and
    // (2) the variable reports the one shared object-pointer entry.
    {
        CHECK(!meta_type_t::resolve_global(meta_info_t::registered_type_name<widget_t>()).is_valid());
        CHECK(meta_type_t::object_pointer_t_type().is_valid());
        CHECK(meta_type_t::object_pointer_t_type().is_object_pointer_trait());

        widget_t widget;
        any_t stored(&widget);
        CHECK(stored.is_initialized());
        CHECK(stored.type().is_object_pointer_trait());
        CHECK(stored.type().id() == meta_type_t::object_pointer_t_type().id());
    }

    // (3) Multiple inheritance: object_t is not at offset 0; the recovered pointer equals the
    //     original one, and (4) it is empty once the target is destroyed.
    {
        auto* multi = new multi_t;
        // The object_t sub-object really is not at offset 0 (iface_t's vptr comes first).
        CHECK(static_cast<void*>(static_cast<object_t*>(multi))
              != static_cast<void*>(static_cast<iface_t*>(multi)));

        any_t stored(multi);
        object_pointer_t<multi_t> back = pointer_from_any<multi_t>(stored);
        CHECK(back.data() == multi);
        CHECK(back->payload == 0);
        CHECK(back->identity() == 7);

        delete multi;
        CHECK(pointer_from_any<multi_t>(stored).data() == nullptr);
        CHECK(pointer_from_any<object_t>(stored).data() == nullptr);
    }

    // (5) A different object_t-derived type cannot be recovered.
    {
        widget_t widget;
        any_t stored(&widget);
        CHECK(pointer_from_any<other_t>(stored).data() == nullptr);
        CHECK(pointer_from_any<object_t>(stored).data() == &widget); // the base is still fine
    }

    // (6) Copying the variable keeps liveness in both copies (the stored guard shares the token).
    {
        auto* widget = new widget_t;
        any_t first(widget);
        any_t second = first;
        CHECK(pointer_from_any<widget_t>(first).data() == widget);
        CHECK(pointer_from_any<widget_t>(second).data() == widget);
        delete widget;
        CHECK(pointer_from_any<widget_t>(first).data() == nullptr);
        CHECK(pointer_from_any<widget_t>(second).data() == nullptr);
    }

    // (7) The identity form hands the same guard back for generic code.
    {
        widget_t widget;
        object_pointer_t<widget_t> guard(&widget);
        object_pointer_t<widget_t> same = pointer_from_any(guard);
        CHECK(same.data() == &widget);
        CHECK(same == guard);
    }

    // (10) Assignment has the same semantics as construction: any_t& operator=(T*) and
    //      operator=(const object_pointer_t<T>&) store the same live guard.
    {
        // An unregistered derived pointer; the assignment carries the live token, so destroying the
        // target afterwards empties the recovered guard.
        widget_t* widget = new widget_t;
        any_t a;
        a = widget;
        CHECK(a.type().is_object_pointer_trait());
        CHECK(pointer_from_any<widget_t>(a).data() == widget);
        delete widget;
        CHECK(pointer_from_any<widget_t>(a).data() == nullptr);
    }
    {
        // Guard lvalue, guard rvalue and a null guard.
        widget_t widget;
        object_pointer_t<widget_t> guard(&widget);
        any_t a;
        a = guard;
        CHECK(pointer_from_any<widget_t>(a).data() == &widget);
        a = object_pointer_t<widget_t>();
        CHECK(pointer_from_any<widget_t>(a).data() == nullptr);
        a = object_pointer_t<widget_t>(&widget);
        CHECK(pointer_from_any<widget_t>(a).data() == &widget);
    }
    {
        // The type switches between the shared object-pointer entry and an ordinary value.
        widget_t widget;
        any_t a;
        a = &widget;
        CHECK(a.type().is_object_pointer_trait());
        CHECK(pointer_from_any<widget_t>(a).data() == &widget);
        a = 42;
        CHECK(!a.type().is_object_pointer_trait());
        CHECK(a.get<int>() == 42);
        a = &widget;
        CHECK(a.type().is_object_pointer_trait());
        CHECK(pointer_from_any<widget_t>(a).data() == &widget);
    }
    {
        // A concrete derived guard type is itself unregistered; assigning it must still work, that is,
        // it must not take the old "the concrete type must be registered" path.
        CHECK(!meta_type_t::resolve_global(meta_info_t::registered_type_name<object_pointer_t<widget_t>>()).is_valid());
        widget_t widget;
        object_pointer_t<widget_t> guard(&widget);
        any_t a;
        a = guard;
        CHECK(a.type().is_object_pointer_trait());
        CHECK(pointer_from_any<widget_t>(a).data() == &widget);
    }

    // (11) Const-qualified targets keep the old behaviour (compile-time checks only, like the
    //      constructors): a const pointer still goes through the generic value path, while a const
    //      guard has no viable overload.
    {
        static_assert(pointer_assignable_to_any<const widget_t>);
        static_assert(!guard_assignable_to_any<const widget_t>);
    }

    // Empty / non-pointer variables yield empty guards.
    {
        any_t empty;
        CHECK(pointer_from_any<widget_t>(empty).data() == nullptr);
        any_t number(42);
        CHECK(pointer_from_any<widget_t>(number).data() == nullptr);
        object_pointer_t<widget_t> none;
        any_t null_guard(none);
        CHECK(pointer_from_any<widget_t>(null_guard).data() == nullptr);
    }

    // (9) Ordinary values are unchanged.
    {
        any_t number(42);
        CHECK(number.get<int>() == 42);
        any_t text(std::string("value"));
        CHECK(text.get<std::string>() == "value");
    }

    if (g_failures == 0) {
        std::printf("  any_pointer: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("  any_pointer: %d of %d check(s) failed\n", g_failures, g_checks);
    return 1;
}
