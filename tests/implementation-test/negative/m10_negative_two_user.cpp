// Negative test: two properties marked User in one class must be rejected at compile time.
// This file is expected to FAIL to compile; tests/negative/CMakeLists.txt asserts that.
#include <meta>

#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/macros.h>

using namespace hwshqtb::metaobject;

struct bad_two_user : object_t {
    OBJECT
public:
    [[= make_property("a", "", "", "", property_trait_flag::USER)]]
    int a() const { return 0; }

    [[= make_property("b", "", "", "", property_trait_flag::USER)]]
    int b() const { return 0; }
};
META_OBJECT(bad_two_user);

int main() {
    (void)bad_two_user::static_meta_object.class_name();
}