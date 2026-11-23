// Negative test: a bare accessor name that matches TWO overloads at the same matching level is a
// compile-time error. build_property_infos() throws std::meta::exception naming the property and the
// candidates: neither `set_x(const double&)` nor `set_x(double&&)` IS the property type `double`,
// both have the property's bare type `double`, so neither can be preferred and the annotation has no
// way to say which one it means. Silently picking one would be a coin flip, so the build fails.
// This file is expected to FAIL to compile; tests/implementation-test/negative/CMakeLists.txt
// asserts that.
#include <meta>

#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/macros.h>

using namespace hwshqtb::metaobject;

struct bad_ambiguous_accessor : object_t {
    OBJECT
public:
    // Both overloads accept a double; the bare name "set_x" cannot choose between them.
    [[= make_property("x", "set_x")]]
    double x() const { return 0.0; }

    void set_x(const double&) { }
    void set_x(double&&) { }
};
META_OBJECT(bad_ambiguous_accessor);

int main() {
    (void)bad_ambiguous_accessor::static_meta_object.class_name();
}
