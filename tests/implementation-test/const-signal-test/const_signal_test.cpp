// Const signals: Qt forbids them (emitting changes connection bookkeeping), so a test here checks
// whether this library's emitter really works from a const member function and from a const object.

#include <meta>
#include <cstdio>
#include <string>

#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_info_t.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/signal_spy_t.h>

using namespace hwshqtb::metaobject;

namespace const_signal_test {

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

/// A signal declared const: the emitter body must work without mutable access.
struct const_signal_t : object_t {
    OBJECT
public:
    int stored = 0;

    int value() const {
        return stored;
    }

    /// Emits the const signal from inside another const member function.
    void emit_from_const(int v) const {
        changed(v);
    }

    SIGNAL(void changed(int v) const)

    /// No parameters at all.
    SIGNAL(void ticked() const)

    /// Two parameters of different types.
    SIGNAL(void moved_by(int dx, double dy) const)

    /// A parameter that is a const reference: the emitter's normalised signature must keep the
    /// cv/ref spelling, i.e. produce exactly the IDENTITY table key for std::string that
    /// build_method_infos stores (normalized_signature must agree with it).
    SIGNAL(void text_changed(const std::string& text) const)
};
META_OBJECT(const_signal_t);

/// Same signal without const, used to prove the const one behaves identically.
struct plain_signal_t : object_t {
    OBJECT
public:
    SIGNAL(void changed(int v))
};
META_OBJECT(plain_signal_t);

struct listener_t : object_t {
    OBJECT
public:
    int hits = 0;
    int last = -1;
    int ticks = 0;
    int last_dx = 0;
    double last_dy = 0.0;
    std::string last_text;

    void on_changed(int v) {
        ++hits;
        last = v;
    }

    void on_ticked() {
        ++ticks;
    }

    void on_moved(int dx, double dy) {
        last_dx = dx;
        last_dy = dy;
    }

    void on_text(const std::string& s) {
        last_text = s;
    }
};
META_OBJECT(listener_t);

/// The cvref regression fixture: ONE class declaring BOTH overloads. Before the signature text
/// carried the member's cvref suffix, `f()` and `f() const` produced the same method_infos key and
/// building the table failed in chd_hash — exactly the defect this test pins down. The two bodies
/// return different values, so invoke() also proves the right implementation is reached.
struct overloaded_t : object_t {
    OBJECT
public:
    int f() {
        return 1;
    }

    int f() const {
        return 2;
    }
};
META_OBJECT(overloaded_t);

/// The strictness fixture: the table holds only the CONST overload of ping(). A lookup for "ping()"
/// must miss — there is deliberately no ignore-cvref fallback that would quietly find this entry.
struct const_only_t : object_t {
    OBJECT
public:
    SIGNAL(void ping() const)

    void slot() {
    }
};
META_OBJECT(const_only_t);

/// Receiver for the strictness fixture: a slot that counts the deliveries of ping().
struct ping_receiver_t : object_t {
    OBJECT
public:
    int hits = 0;

    void on_ping() {
        ++hits;
    }
};
META_OBJECT(ping_receiver_t);

/// The "a slot is just a method" fixture: the sender side is a signal, the receiver side is an
/// ordinary public method with no marker of any kind. Both are reflected exactly like every other
/// member, and the ordinary method is a valid connection target — there is no separate slot kind to
/// mark it with.
struct connection_source_t : object_t {
    OBJECT
public:
    SIGNAL(void some_signal(int v))
};
META_OBJECT(connection_source_t);

struct ordinary_target_t : object_t {
    OBJECT
public:
    int hits = 0;
    int last = -1;

    /// An ordinary non-signal method: not marked as a slot anywhere, still a connection target.
    void ordinary_method(int v) {
        ++hits;
        last = v;
    }
};
META_OBJECT(ordinary_target_t);

/// The normalization-parity fixture. The subject of the invariant is not any one member here but the
/// AGREEMENT between the two spellers: the compile-time key (signature_of, reflection-based) and the
/// run-time key (normalized_signature, user-text based) must be the SAME STRING for the same
/// declaration written the way a user writes it. Each member below pins one branch of C++'s
/// top-level-cv rule for a function parameter:
///   f(const int)   — by value: the top-level cv belongs to the VALUE and is dropped from the type;
///   g(int* const)  — pointer: the top-level cv trails the declarator and is dropped, the * stays;
///   h(const int&)  — reference: the cv belongs to the REFERENT and stays (a reference has no
///                    top-level cv of its own);
///   k(const int*)  — pointer TO const: the cv belongs to the POINTEE and stays;
///   i() const      — member cvref: part of the key, and NOT what "i()" names;
///   j() &          — member ref-qualifier: part of the key too.
/// Without the drop, normalized_signature() answers "f(const int)" and "g(int*const)" where the
/// stored key is "f(int)" and "g(int*)" — a lookup that can never hit.
struct normalization_parity_t : object_t {
    OBJECT
public:
    void f(const int) {
    }

    void g(int* const) {
    }

    void h(const int&) {
    }

    int k(const int*) {
        return 0;
    }

    int i() const {
        return 0;
    }

    void j() & {
    }
};
META_OBJECT(normalization_parity_t);

}  // namespace const_signal_test

using namespace const_signal_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    std::printf("== const signals ==\n");

    // (1) The const signal is visible in the meta-info under the same signature as a plain one.
    CHECK(const_signal_t::static_meta_object.signal("changed(int) const").is_valid());
    CHECK(plain_signal_t::static_meta_object.signal("changed(int)").is_valid());

    const_signal_t sender;
    sender.set_parent(root);
    listener_t listener;
    listener.set_parent(root);

    const auto conn = sender.connect("changed(int) const", &listener, "on_changed(int)");
    CHECK(conn.is_valid());

    // (2) Emitting through a non-const object.
    sender.changed(7);
    CHECK(listener.hits == 1);
    CHECK(listener.last == 7);

    // (3) The point of the test: emitting through a CONST reference.
    const const_signal_t& cref = sender;
    cref.changed(8);
    CHECK(listener.hits == 2);
    CHECK(listener.last == 8);

    // (4) Emitting from inside another const member function, through a const reference only.
    const const_signal_t& frozen_view = sender;
    frozen_view.emit_from_const(10);
    CHECK(listener.hits == 3);
    CHECK(listener.last == 10);

    // (5) Argument values are carried unchanged, one dispatch per emit.
    cref.changed(-5);
    CHECK(listener.hits == 4);
    CHECK(listener.last == -5);

    // (6) A const signal with no parameters.
    CHECK(sender.meta_object()->signal("ticked() const").is_valid());
    CHECK(sender.connect("ticked() const", &listener, "on_ticked()").is_valid());
    sender.ticked();
    CHECK(listener.ticks == 1);
    cref.ticked();                     // through a const reference
    CHECK(listener.ticks == 2);

    // (7) A const signal with two parameters of different types.
    CHECK(sender.meta_object()->signal("moved_by(int,double) const").is_valid());
    CHECK(sender.connect("moved_by(int,double) const", &listener, "on_moved(int,double)").is_valid());
    sender.moved_by(3, 1.25);
    CHECK(listener.last_dx == 3);
    CHECK(listener.last_dy == 1.25);
    cref.moved_by(-2, -0.5);
    CHECK(listener.last_dx == -2);
    CHECK(listener.last_dy == -0.5);

    // (8) A const signal over a Queued connection is dispatched by the event loop, not inline.
    //     Uses a fresh listener: the Direct connection created in (2) would otherwise increment
    //     inline and mask the queued behaviour.
    listener_t queued_listener;
    queued_listener.set_parent(root);
    CHECK(sender.connect("changed(int) const", &queued_listener, "on_changed(int)",
                         connection_type_flag::QUEUED).is_valid());
    cref.changed(11);
    CHECK(queued_listener.hits == 0);                  // queued: not dispatched yet
    event_dispatcher::instance().send_posted_events();
    CHECK(queued_listener.hits == 1);
    CHECK(queued_listener.last == 11);

    // (9) Disconnecting a const signal's connection stops delivery to that receiver.
    listener_t dropped_listener;
    dropped_listener.set_parent(root);
    const auto dropped_conn = sender.connect("changed(int) const", &dropped_listener, "on_changed(int)");
    CHECK(dropped_conn.is_valid());
    cref.changed(12);
    CHECK(dropped_listener.hits == 1);
    sender.disconnect(dropped_conn);
    cref.changed(13);
    CHECK(dropped_listener.hits == 1);                 // unchanged after disconnect

    // (10) A const-reference parameter keeps its cv/ref in the normalised signature: the emitter
    //      builds its key with normalized_signature(), which must agree with the spelling
    //      build_method_infos stores. That stored spelling is the ORIGINAL one (std::basic_string<char>
    //      is only an alias a caller may write), so the expectation comes from the library — never
    //      from a std::__cxx11 literal, which a standard library without that namespace would break.
    const std::string std_string_name(meta_info_t::original_name<std::string>());
    CHECK(sender.meta_object()->normalized_signature("text_changed(const std::string&)")
          == "text_changed(const " + std_string_name + "&)");
    // The user text goes through normalized_signature(), exactly as the API contract says: signal()
    // and connect() compare the key byte for byte against the stored identity spelling.
    CHECK(sender.meta_object()->signal(
              sender.meta_object()->normalized_signature("text_changed(const std::string&) const")).is_valid());
    CHECK(sender.connect(
              sender.meta_object()->normalized_signature("text_changed(const std::string&) const"),
              &listener,
              listener.meta_object()->normalized_signature("on_text(const std::string&)")).is_valid());
    sender.text_changed(std::string("hello"));
    CHECK(listener.last_text == "hello");
    cref.text_changed(std::string("again"));    // through a const reference
    CHECK(listener.last_text == "again");

    // -----------------------------------------------------------------------
    // (11) THE REGRESSION: f() and f() const are two distinct entries.
    //
    // overloaded_t declaring both is itself the first half of the proof — before the signature text
    // carried the cvref suffix, both declarations produced the key "f()" and build_meta<T>() failed
    // while building the perfect hash. The checks below pin down that the two entries survive as
    // different rows with the right qualifier, and that invoke() reaches the right body.
    // -----------------------------------------------------------------------
    {
        const meta_object_t& mo = overloaded_t::static_meta_object;
        const index_t plain_index = mo.index_of_method("f()");
        const index_t const_index = mo.index_of_method("f() const");
        CHECK(plain_index != index_npos);
        CHECK(const_index != index_npos);
        CHECK(plain_index != const_index);

        // The qualifier column stays: it is the structured form of the same fact.
        CHECK(mo.method(plain_index).qualifier() == qualifier_flag::NONE);
        CHECK(mo.method(const_index).qualifier() == qualifier_flag::CONST);

        // The stored keys are the canonical spellings too.
        CHECK(mo.method(plain_index).signature() == "f()");
        CHECK(mo.method(const_index).signature() == "f() const");

        // Each lookup reaches its own implementation: the bodies return different values.
        overloaded_t object;
        int plain_result = 0;
        int const_result = 0;
        CHECK(mo.invoke(object, "f()", plain_result));
        CHECK(mo.invoke(object, "f() const", const_result));
        CHECK(plain_result == 1);
        CHECK(const_result == 2);
    }

    // -----------------------------------------------------------------------
    // (12) normalized_signature() canonicalises the trailing cvref suffix.
    // -----------------------------------------------------------------------
    {
        const meta_object_t& mo = overloaded_t::static_meta_object;
        CHECK(mo.normalized_signature("f(int)") == "f(int)");
        CHECK(mo.normalized_signature("f(int)const") == "f(int) const");
        CHECK(mo.normalized_signature("f(int)const&") == "f(int) const &");
        CHECK(mo.normalized_signature("f(int) const&") == "f(int) const &");
        CHECK(mo.normalized_signature("f( int ) const &") == "f(int) const &");
        CHECK(mo.normalized_signature("f(int)&&") == "f(int) &&");
        CHECK(mo.normalized_signature("f(int) volatile const") == "f(int) const volatile");
        CHECK(mo.normalized_signature("f(int) const volatile &") == "f(int) const volatile &");
        // The member cvref and the parameter cvref normalise together, and the alias spelling is
        // resolved to the IDENTITY spelling the tables store.
        CHECK(mo.normalized_signature("f(const std::string&)const")
              == "f(const " + std_string_name + "&) const");
    }

    // -----------------------------------------------------------------------
    // (13) LOOKUP IS STRICT: "ping()" does not name "ping() const". No fallback, no silent match.
    // -----------------------------------------------------------------------
    {
        const meta_object_t& mo = const_only_t::static_meta_object;
        CHECK(mo.index_of_method("ping()") == index_npos);
        CHECK(!mo.method("ping()").is_valid());
        CHECK(!mo.signal("ping()").is_valid());
        CHECK(mo.index_of_method("ping() const") != index_npos);
        CHECK(mo.signal("ping() const").is_valid());

        // The miss is observable through the connection path as well: an invalid handle, while the
        // canonical spelling connects and delivers.
        const_only_t strict_sender;
        strict_sender.set_parent(root);
        ping_receiver_t receiver;
        receiver.set_parent(root);

        signal_spy_t strict_spy(&strict_sender, "ping()");
        CHECK(!strict_spy.is_valid());                 // the no-cvref spelling records nothing
        CHECK(strict_spy.count() == 0);
        CHECK(!strict_sender.connect("ping()", &receiver, "on_ping()").is_valid());

        CHECK(strict_sender.connect("ping() const", &receiver, "on_ping()").is_valid());
        strict_sender.ping();
        CHECK(receiver.hits == 1);
    }

    // -----------------------------------------------------------------------
    // (14) A slot is just a method. There is no slot kind, so ANY reflected method can be the
    //      target of a connection: ordinary_method() is declared as a plain public method (no
    //      marker, no `slots:` block) and still receives the delivery.
    // -----------------------------------------------------------------------
    {
        const meta_object_t& target_meta = ordinary_target_t::static_meta_object;
        const meta_method_t target = target_meta.method("ordinary_method(int)");
        CHECK(target.is_valid());
        CHECK(target.method_kind() == method_kind_enum::PLAIN_METHOD);
        CHECK(!target.is_signal());

        connection_source_t source;
        source.set_parent(root);
        ordinary_target_t receiver;
        receiver.set_parent(root);

        const auto ordinary_conn = source.connect("some_signal(int)", &receiver,
                                                  "ordinary_method(int)");
        CHECK(ordinary_conn.is_valid());

        source.some_signal(42);
        CHECK(receiver.hits == 1);
        CHECK(receiver.last == 42);

        // A signal is still a signal, and the two queries agree.
        const meta_method_t emitted = source.meta_object()->signal("some_signal(int)");
        CHECK(emitted.is_valid());
        CHECK(emitted.method_kind() == method_kind_enum::SIGNAL);
        CHECK(emitted.is_signal());
        CHECK(emitted.is_signal() == (emitted.method_kind() == method_kind_enum::SIGNAL));
    }

    // -----------------------------------------------------------------------
    // (15) THE NORMALIZATION PARITY INVARIANT: the compile-time key and the run-time key are the same
    //      string for the same declaration.
    //
    //      Every case below would have failed before the TOP-LEVEL cv of a parameter was stripped on
    //      the user-text side: the reflection already drops it (a parameter's type has no top-level
    //      cv), so "f(const int)" normalised to a spelling no row carries. The invariant is stated
    //      as equality with signature_of, so neither side can drift without this failing.
    // -----------------------------------------------------------------------
    {
        const meta_object_t& mo = normalization_parity_t::static_meta_object;

        // By value: the top-level cv is dropped. "f(const int)" IS "f(int)".
        CHECK(mo.normalized_signature("f(const int)") == "f(int)");
        CHECK(std::string(meta_info_t::signature_of(^^normalization_parity_t::f))
              == mo.normalized_signature("f(const int)"));
        CHECK(mo.index_of_method(mo.normalized_signature("f(const int)")) != index_npos);
        // The unqualified spelling still names the same row, of course.
        CHECK(mo.index_of_method("f(int)") != index_npos);

        // Pointer: the pointer's own cv trails the declarator and goes; the '*' stays, and the spacing
        // is the normal form ("int*"), not the collapsed "int*const" the raw text would produce.
        CHECK(mo.normalized_signature("g(int* const)") == "g(int*)");
        CHECK(std::string(meta_info_t::signature_of(^^normalization_parity_t::g))
              == mo.normalized_signature("g(int* const)"));
        CHECK(mo.index_of_method(mo.normalized_signature("g(int* const)")) != index_npos);

        // Reference: the cv belongs to the REFERENT, so it is kept — stripping it would name a
        // different function.
        CHECK(mo.normalized_signature("h(const int&)") == "h(const int&)");
        CHECK(std::string(meta_info_t::signature_of(^^normalization_parity_t::h))
              == mo.normalized_signature("h(const int&)"));
        CHECK(mo.index_of_method(mo.normalized_signature("h(const int&)")) != index_npos);

        // Pointer TO const: the cv belongs to the POINTEE, so the leading const is kept while a
        // TRAILING one would not be. Both facts in one pair.
        CHECK(mo.normalized_signature("k(const int*)") == "k(const int*)");
        CHECK(std::string(meta_info_t::signature_of(^^normalization_parity_t::k))
              == mo.normalized_signature("k(const int*)"));
        CHECK(mo.index_of_method(mo.normalized_signature("k(const int*)")) != index_npos);

        // The member cvref: part of the key, and the whole point is that "i()" does NOT name it.
        CHECK(mo.normalized_signature("i() const") == "i() const");
        CHECK(std::string(meta_info_t::signature_of(^^normalization_parity_t::i))
              == mo.normalized_signature("i() const"));
        CHECK(mo.normalized_signature("i()") != "i() const");
        CHECK(mo.index_of_method(mo.normalized_signature("i()")) == index_npos);
        CHECK(mo.index_of_method(mo.normalized_signature("i() const")) != index_npos);

        // The member ref-qualifier is part of the key too.
        CHECK(mo.normalized_signature("j() &") == "j() &");
        CHECK(std::string(meta_info_t::signature_of(^^normalization_parity_t::j))
              == mo.normalized_signature("j() &"));
        CHECK(mo.index_of_method(mo.normalized_signature("j() &")) != index_npos);
    }

    std::printf(g_failures == 0 ? "  const signals: all %d checks passed\n"
                                : "  const signals: %d of %d check(s) failed\n",
                g_failures == 0 ? g_checks : g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}