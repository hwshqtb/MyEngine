// Negative test: a GADGET must not sit under an OBJECT class.
//
// A GADGET is a value type: it declares only `static_meta_object`, never the virtual `meta_object()`,
// so there is no polymorphic accessor that could name the right table at run time. Inheriting an
// OBJECT class would give the gadget an INHERITED meta_object() that reports the BASE's table while
// `T::static_meta_object` is the gadget's own — two answers for one object. The consteval constructor
// must reject the shape. This file is expected to FAIL to compile;
// tests/implementation-test/negative/CMakeLists.txt asserts that.
#include <meta>

#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/macros.h>

using namespace hwshqtb::metaobject;

struct bad_gadget_over_object: object_t {
    GADGET
};
META_OBJECT(bad_gadget_over_object);

int main() {
    (void)bad_gadget_over_object::static_meta_object.class_name();
}
