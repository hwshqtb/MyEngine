// Negative test: a non-OBJECT class must not sit between two OBJECT classes.
//
// `middle_t` inherits meta_object() from object_t but never declares it (it did not expand OBJECT),
// so it has no table of its own. `bad_leaf_t` DOES declare OBJECT, and its direct base declares none:
// linking across `middle_t` would make super_class() name an ancestor while a real class of the chain
// is skipped, so the consteval constructor must reject this rather than silently skip it.
// This file is expected to FAIL to compile; tests/implementation-test/negative/CMakeLists.txt asserts
// that.
#include <meta>

#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/macros.h>

using namespace hwshqtb::metaobject;

struct middle_t: object_t { };

struct bad_leaf_t: middle_t {
    OBJECT
};
META_OBJECT(bad_leaf_t);

int main() {
    (void)bad_leaf_t::static_meta_object.class_name();
}
