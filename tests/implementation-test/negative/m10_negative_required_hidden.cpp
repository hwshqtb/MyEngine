// Negative test: a property marked both Required and Hidden must be rejected at compile time.
// This file is expected to FAIL to compile; tests/negative/CMakeLists.txt asserts that.
#include <meta>

#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/macros.h>

using namespace hwshqtb::metaobject;

struct bad_required_hidden : object_t {
    OBJECT
public:
    [[= make_property("x", "", "", "", property_trait_t{ property_trait_flag::REQUIRED } | property_trait_flag::HIDDEN)]]
    int x() const { return 0; }
};
META_OBJECT(bad_required_hidden);

int main() {
    (void)bad_required_hidden::static_meta_object.class_name();
}