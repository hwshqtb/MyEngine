// Negative test: a class with a NON-HIDDEN member that declares an EXPLICIT OBJECT parameter
// ("deducing-this": `void f(this X&, int)`) must NOT compile.
//
// The meta-object does not describe such a member, and the omission is made LOUD instead of silent by
// detail::reject_explicit_object_members<T>(), which build_meta<T>() calls before it collects
// anything: the class fails to build with a diagnostic that names the offending member and explains
// why (an explicit object parameter is NOT an implicit one — `f(this X const x)` and `f() const` are
// different declarations — so keying one as the other would describe a call nobody can make).
//
// The conditional half of the rule — a [[= make_hidden()]] deducing-this member, which is the author's
// opt-out and must compile fine — lives in the POSITIVE suite instead
// (tests/implementation-test/m28-static-method-test, the test that owns method collection).
// This file is expected to FAIL to compile; tests/implementation-test/negative/CMakeLists.txt
// asserts that.
#include <meta>

#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/macros.h>

using namespace hwshqtb::metaobject;

struct bad_deducing_this_t : object_t {
    OBJECT
public:
    // NOT hidden, so the meta-object refuses the class: this is the rejected declaration.
    void f(this bad_deducing_this_t& self, int v) {
        self.value = v;
    }

    // An ordinary member in the same class: the diagnostic must be about `f`, not about "the class".
    void g(int v) {
        value = v;
    }

    int value = 0;
};
META_OBJECT(bad_deducing_this_t);

int main() {
    (void)bad_deducing_this_t::static_meta_object.class_name();
}
