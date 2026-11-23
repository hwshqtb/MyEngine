// M28: STATIC member functions are rows of the method table.
//
// Phase N3 gave the method table its third kind of row. This file pins every part of it:
//
//  (1) COLLECTION: build_method_infos() collects statics (detail::get_functions<T>(true)) while the
//      PROPERTY accessor scan keeps the default (statics excluded). The table therefore holds the
//      static rows — method_count() grows by exactly the number of static member functions the class
//      declares — and a static getter-shaped function is NOT an accessor candidate.
//  (2) CLASSIFICATION AND KEY: a static row's kind is method_kind_enum::STATIC_METHOD, has_receiver() is
//      false, its cvref column is None, and its signature is the BARE "f(int)" a user writes — never
//      "f(int) const" (a static member has no implicit object to qualify).
//  (3) INVOCATION WITH NO OBJECT: the static thunk runs a plain function pointer, so the row is called
//      through invoke_static() with obj == nullptr. Value returns come back through the same
//      return-slot protocol as an instance call (typed Ret&, any_t, erased span, erased any_t&), and a
//      void static writes nothing. The object layer's call_static / invoke_static families agree with
//      the handle, because they resolve and then delegate to it.
//  (4) THE OVERLOAD PAIR that makes staticness visible without being able to disambiguate it:
//      `void f(int)` (instance) and `static void f(double)` (static) are legal overloads; each
//      resolves to its own row and each is called through the form that matches its kind.
//  (5) MISUSE, one Critical each way: invoke(obj, ...) on a static row and invoke_static(...) on an
//      instance row are both refused, with the message naming the form that WOULD work, and nothing
//      runs.
//  (6) PROPERTY REGRESSION GUARD: a property whose writer name is carried by a STATIC
//      accessor-shaped function must bind NO writer (the observed outcome: the property is read-only)
//      and must never bind that static function. A positive control (an instance writer) proves the
//      scan still binds ordinary accessors.
//  (7) METHOD_COUNT(): the static rows are counted, and the number is asserted against
//      object_t::static_meta_object's own count plus the class's declared members, so the growth is a
//      stated fact rather than an accident.

#include <meta>

#include <array>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <hwshqtb/metaobject/annotation.h>
#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/meta_property_t.h>
#include <hwshqtb/metaobject/meta_type_t.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::metaobject;

namespace m28_static_method_test {

int g_checks = 0;
int g_failures = 0;

/// @brief Record one check; a failure prints its line and the condition.
void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// @brief One captured log record (category name, level, text).
struct log_record_t {
    std::string category;
    log_level_enum level;
    std::string message;
};

/// @brief Every record the installed handler received during the whole run.
std::vector<log_record_t> g_records;

/// @brief Whether @p haystack contains @p needle.
bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

/// @brief The int an any_t holds, or a sentinel no assertion expects.
/// @note Used instead of any_t::get<int>() directly, so an invalid or differently typed result
///       reports a failed CHECK instead of throwing out of the test.
int held_int(const any_t& value) {
    if (value.is_valid() && value.is_initialized() && value.type().is<int>()) {
        return value.get<int>();
    }
    return -9999;
}

}  // namespace m28_static_method_test

using namespace m28_static_method_test;

// ---------------------------------------------------------------------------
// The fixtures. Deliberately at global scope: a type declared inside an anonymous namespace has no
// stable name, and the table build keys types by name.
// ---------------------------------------------------------------------------

/// The main fixture: two static member functions (one value-returning, one void), an instance method
/// and a static overload of it, plus a static DATA member (which must not become a row at all).
struct static_fixture_t : object_t {
    OBJECT

public:
    /// A static function with a value return. The counter is how "nothing ran" is asserted later.
    static int make(int v) {
        ++make_calls;
        return v * 2;
    }

    /// A static function with a void return.
    static void reset_marker(int v) {
        ++reset_calls;
        marker = v;
    }

    /// The instance half of the overload pair: f(int) is called ON an object.
    void f(int v) {
        ++f_int_calls;
        last_int = v;
    }

    /// The static half: f(double) is called with NO object. Legal overload: the parameter lists
    /// differ, so staticness is not what tells them apart (C++ forbids a static / non-static pair
    /// with the SAME parameter list) — the ARGUMENT TYPE is.
    static void f(double v) {
        ++f_double_calls;
        last_double = v;
    }

    /// A static DATA member. It is a variable, not a function, so is_aimed_function() rejects it and
    /// it never becomes a method row (static data members are deliberately not collected).
    static inline int make_calls = 0;
    static inline int reset_calls = 0;
    static inline int f_int_calls = 0;
    static inline int f_double_calls = 0;
    static inline int marker = 0;
    static inline int last_int = 0;
    static inline double last_double = 0.0;
};

META_OBJECT(static_fixture_t);

/// The property fixture. `v` names a writer `set_v`; the ONLY `set_v` the class declares is STATIC
/// and accessor-shaped (one int parameter, the property's exact type). If the accessor scan ever
/// collected statics, that static would bind as the writer; with the split it must not, and the
/// property must therefore be READ-ONLY. `w` is the positive control: an instance writer still binds.
struct static_property_t : object_t {
    OBJECT

public:
    [[= make_property("v", "set_v")]]
    int get_v() const {
        return value;
    }

    /// The static accessor-shaped function: same name as the annotation's writer, same parameter type
    /// as the property's. It is a METHOD row (it is a reflected member function) and must never be an
    /// accessor.
    static void set_v(int v) {
        ++static_set_v_calls;
        static_v = v;
    }

    /// A static overload of the reader's name (legal: different parameter list). It is NOT the
    /// property's reader — the reader is the ANNOTATED declaration, and it must answer the instance
    /// value.
    static int get_v(int seed) {
        ++static_get_v_calls;
        return seed + 1000;
    }

    [[= make_property("w", "set_w")]]
    int get_w() const {
        return value + 1;
    }

    void set_w(int v) {
        value = v - 1;
        ++instance_set_w_calls;
    }

    static inline int static_set_v_calls = 0;
    static inline int static_get_v_calls = 0;
    static inline int instance_set_w_calls = 0;
    static inline int static_v = 0;
    static inline int value = 7;
};

META_OBJECT(static_property_t);

/// The HIDING half of the explicit-object-parameter rule (N4 rollback).
///
/// A deducing-this member (`void h(this T& self, int)`) is NOT modelled by the meta-object: its
/// explicit object parameter is not an implicit one, so it must never become a row. An unhidden one
/// is a compile error naming the member (tests/implementation-test/negative/
/// m29_negative_deducing_this.cpp is the WILL_FAIL case). But [[= make_hidden()]] is this library's
/// "keep this out of the reflection" annotation, and the author's opt-out wins: this class MUST
/// compile, the hidden member MUST simply not appear in the table, and nothing may be logged about it.
/// `visible(int)` is the positive control: the table still collects ordinary members of the same class.
struct hidden_deducing_this_t : object_t {
    OBJECT

public:
    /// Hidden, so the class builds and the member is not collected (no diagnostic either).
    [[= make_hidden()]]
    void hidden_deducing(this hidden_deducing_this_t& self, int v) {
        self.value = v;
    }

    /// The positive control: collected exactly like any other ordinary method.
    void visible(int v) {
        value = v;
    }

    int value = 0;
};

META_OBJECT(hidden_deducing_this_t);

int main(int argc, char** argv) {
    core_application app(argc, argv);

    install_log_handler([](log_level_enum level, const log_context_t& context, std::string_view message) {
        g_records.push_back(log_record_t{ std::string(context.category), level, std::string(message) });
        return true;  // consumed: the default stderr sink stays silent
    });

    std::printf("== M28: static member functions as method rows ==\n");

    const meta_object_t& meta = static_fixture_t::static_meta_object;

    // -----------------------------------------------------------------------
    // (1) COLLECTION and CLASSIFICATION.
    //
    // method_count() is the chain-wide count: object_t's own table plus this class's rows. This
    // class declares FIVE reflected members of its own — the OBJECT accessor meta_object() const,
    // the two statics make(int) / reset_marker(int), and the overload pair f(int) / f(double) — so
    // the count is object_t's plus five. The static DATA members contribute nothing: they are
    // variables, and is_aimed_function() only ever answers for a function.
    // -----------------------------------------------------------------------
    const std::size_t base_count = object_t::static_meta_object.method_count();
    std::printf("  method_count: object_t=%zu static_fixture_t=%zu (delta=%zu)\n",
        base_count, meta.method_count(), meta.method_count() - base_count);
    CHECK(meta.method_count() == base_count + 5);
    CHECK(static_property_t::static_meta_object.method_count()
          == object_t::static_meta_object.method_count() + 6);

    const index_t make_index = meta.index_of_method("make(int)");
    CHECK(make_index != index_npos);
    CHECK(static_cast<std::size_t>(make_index) >= base_count);   // its own row, after the base's

    const meta_method_t make_row = meta.method("make(int)");
    CHECK(make_row.is_valid());
    CHECK(!make_row.signature().empty());
    CHECK(make_row.name() == "make");
    CHECK(make_row.signature() == "make(int)");
    CHECK(make_row.param_count() == 1u);
    CHECK(make_row.param_type_name(0) == "int");
    CHECK(make_row.return_type_name() == "int");
    CHECK(make_row.method_kind() == method_kind_enum::STATIC_METHOD);
    CHECK(!make_row.has_receiver());
    CHECK(make_row.qualifier() == qualifier_flag::NONE);
    CHECK(make_row.access() == method_access_enum::PUBLIC_SCOPE);

    // The KEY has no cvref suffix: a static member has no implicit object to qualify. If the suffix
    // leaked in, this lookup would hit where it must not.
    CHECK(meta.index_of_method("make(int) const") == index_npos);
    CHECK(!meta.method("make(int) const").is_valid());

    // A static DATA member is not a method row, under any key.
    CHECK(meta.index_of_method("make_calls") == index_npos);
    CHECK(!meta.method("make_calls").is_valid());

    // The void static is a row too, and its return column says void.
    const meta_method_t reset_row = meta.method("reset_marker(int)");
    CHECK(reset_row.is_valid());
    CHECK(reset_row.method_kind() == method_kind_enum::STATIC_METHOD);
    CHECK(reset_row.return_type_name() == "void");

    // (2) THE OVERLOAD PAIR: the two `f` rows, told apart by the argument type, each with its own
    //     receiver fact.
    const meta_method_t f_int = meta.method("f(int)");
    const meta_method_t f_double = meta.method("f(double)");
    CHECK(f_int.is_valid());
    CHECK(f_double.is_valid());
    CHECK(f_int.has_receiver());
    CHECK(f_int.method_kind() == method_kind_enum::PLAIN_METHOD);
    CHECK(!f_double.has_receiver());
    CHECK(f_double.method_kind() == method_kind_enum::STATIC_METHOD);
    CHECK(f_int.signature() == "f(int)");
    CHECK(f_double.signature() == "f(double)");

    // -----------------------------------------------------------------------
    // (3) INVOCATION WITH NO OBJECT: the value-returning static, through every shape.
    // -----------------------------------------------------------------------
    const int make_before = static_fixture_t::make_calls;
    {
        const any_t result = make_row.invoke_static(5);
        CHECK(result.is_valid());
        CHECK(result.is_initialized());
        CHECK(result.is<int>());
        CHECK(held_int(result) == 10);
        CHECK(static_fixture_t::make_calls == make_before + 1);
    }
    {
        // The typed `Ret&` form: the caller's own variable is the slot.
        int out = 0;
        CHECK(make_row.invoke_static(out, 4));
        CHECK(out == 8);
    }
    {
        // The handle's signature-based form and the table's own resolver agree on the row.
        const any_t by_signature = meta.invoke_static("make(int)", 3);
        CHECK(held_int(by_signature) == 6);
    }
    {
        // The OBJECT LAYER: call_static<"name"> and call_static(name) resolve, then delegate to the
        // handle's invoke_static — so they must agree with each other and with the handle.
        const any_t by_ct_name = meta.call_static<"make">(5);
        const any_t by_rt_name = meta.call_static("make", 5);
        CHECK(held_int(by_ct_name) == 10);
        CHECK(held_int(by_rt_name) == 10);
        int out = 0;
        CHECK(meta.call_static("make", out, 6));
        CHECK(out == 12);
        CHECK(meta.call_static<"make">(out, 7));
        CHECK(out == 14);
    }
    {
        // The void static: the success shape is the DECLARED-ONLY void any_t, and nothing is written
        // through a return slot.
        const int reset_before = static_fixture_t::reset_calls;
        const any_t void_result = reset_row.invoke_static(7);
        CHECK(void_result.is_valid());
        CHECK(!void_result.is_initialized());
        CHECK(static_fixture_t::marker == 7);
        CHECK(static_fixture_t::reset_calls == reset_before + 1);
        const any_t via_layer = meta.call_static<"reset_marker">(8);
        CHECK(via_layer.is_valid());
        CHECK(!via_layer.is_initialized());
        CHECK(static_fixture_t::marker == 8);
    }
    {
        // The ERASED channel: one any_t per parameter, no object anywhere.
        std::array<any_t, 1> boxed{ any_t(6) };
        const any_t result = make_row.invoke_static(std::span<const any_t>(boxed));
        CHECK(held_int(result) == 12);
        const any_t by_layer = meta.call_static("make", std::span<const any_t>(boxed));
        CHECK(held_int(by_layer) == 12);
        const any_t by_signature = meta.invoke_static("make(int)", std::span<const any_t>(boxed));
        CHECK(held_int(by_signature) == 12);

        // The erased `any_t&` slot: declared, then filled by the call.
        any_t slot(meta_type_t::from_type<int>());
        CHECK(make_row.invoke_static(slot, std::span<const any_t>(boxed)));
        CHECK(slot.is_initialized());
        CHECK(held_int(slot) == 12);
    }
    {
        // Connection types: Direct runs; Queued is REFUSED with one Warning, because a static call has
        // no receiver and therefore no thread affinity to post an event to. Auto resolves to Direct.
        const any_t direct = make_row.invoke_static(connection_type_flag::DIRECT, 2);
        CHECK(held_int(direct) == 4);
        const std::size_t before_queued = g_records.size();
        const any_t queued = make_row.invoke_static(connection_type_flag::QUEUED, 2);
        CHECK(!queued.is_valid());
        CHECK(g_records.size() == before_queued + 1);
        CHECK(g_records.back().level == log_level_enum::WARNING);
        CHECK(contains(g_records.back().message, "no thread affinity"));
    }

    // -----------------------------------------------------------------------
    // (4) THE OVERLOAD PAIR IN ACTION: each row is called through the form its kind requires.
    // -----------------------------------------------------------------------
    static_fixture_t obj;
    obj.set_object_name("m28");
    {
        const int int_before = static_fixture_t::f_int_calls;
        const int double_before = static_fixture_t::f_double_calls;
        const any_t instance_call = meta.call(obj, "f", 3);
        CHECK(instance_call.is_valid());
        CHECK(static_fixture_t::f_int_calls == int_before + 1);
        CHECK(static_fixture_t::f_double_calls == double_before);   // the static row did NOT run
        CHECK(static_fixture_t::last_int == 3);

        const any_t static_call = meta.call_static("f", 2.5);
        CHECK(static_call.is_valid());
        CHECK(static_fixture_t::f_double_calls == double_before + 1);
        CHECK(static_fixture_t::last_double == 2.5);

        // The same two rows by SIGNATURE.
        CHECK(meta.invoke(obj, "f(int)", 4).is_valid());
        CHECK(static_fixture_t::last_int == 4);
        CHECK(meta.invoke_static("f(double)", 4.5).is_valid());
        CHECK(static_fixture_t::last_double == 4.5);

        // Resolution agreed with the rows: neither call was an ambiguity.
        CHECK(meta.call_static<"f">(6.5).is_valid());
        CHECK(static_fixture_t::last_double == 6.5);
    }

    // -----------------------------------------------------------------------
    // (4b) A static row stays a CANDIDATE while a table is searched with a CONST object.
    //
    // In C++ a static member is callable through a const object (`obj.static_fn()` is legal), so the
    // object-cvref block must be skipped for a receiver-less row — what the matcher's has_receiver()
    // condition says. Consequence, and the reason this block exists: a const-object lookup of a name
    // that only the STATIC row accepts must RESOLVE (then be refused by the misuse rule, one
    // Critical), not fall through to a resolution miss (one Warning). Applying the object rule to
    // every row would turn this into a miss.
    // -----------------------------------------------------------------------
    {
        const static_fixture_t const_obj;
        const int double_before = static_fixture_t::f_double_calls;
        const std::size_t before = g_records.size();
        const any_t refused = meta.call(const_obj, "f", 2.5);
        CHECK(!refused.is_valid());
        CHECK(static_fixture_t::f_double_calls == double_before);   // refused before anything ran
        CHECK(g_records.size() == before + 1);
        CHECK(g_records.back().level == log_level_enum::CRITICAL);       // RESOLVED, then refused...
        CHECK(contains(g_records.back().message, "invoke_static")); // ...by the receiver-mode rule
    }

    // -----------------------------------------------------------------------
    // (5) MISUSE: exactly one Critical each way, nothing invoked.
    // -----------------------------------------------------------------------
    {
        // invoke(obj, ...) on the STATIC row: refused, message points at invoke_static.
        const int before_calls = static_fixture_t::make_calls;
        const std::size_t before = g_records.size();
        const any_t refused = make_row.invoke(obj, 5);
        CHECK(!refused.is_valid());
        CHECK(static_fixture_t::make_calls == before_calls);   // nothing ran
        CHECK(g_records.size() == before + 1);                 // exactly ONE record
        CHECK(g_records.back().level == log_level_enum::CRITICAL);
        CHECK(g_records.back().category == "metaobject.connection");
        CHECK(contains(g_records.back().message, "make(int)"));
        CHECK(contains(g_records.back().message, "static member function"));
        CHECK(contains(g_records.back().message, "invoke_static"));

        // The erased channel refuses it the same way (one choke point, two channels).
        std::array<any_t, 1> boxed{ any_t(5) };
        const std::size_t before_erased = g_records.size();
        const any_t refused_erased = make_row.invoke(obj, std::span<const any_t>(boxed));
        CHECK(!refused_erased.is_valid());
        CHECK(static_fixture_t::make_calls == before_calls);
        CHECK(g_records.size() == before_erased + 1);
        CHECK(g_records.back().level == log_level_enum::CRITICAL);
        CHECK(contains(g_records.back().message, "invoke_static"));

        // And the object layer's instance entry reaches the same refusal.
        const std::size_t before_layer = g_records.size();
        const any_t refused_layer = meta.invoke(obj, "make(int)", 5);
        CHECK(!refused_layer.is_valid());
        CHECK(g_records.size() == before_layer + 1);
        CHECK(g_records.back().level == log_level_enum::CRITICAL);
        CHECK(contains(g_records.back().message, "invoke_static"));
    }
    {
        // invoke_static(...) on the INSTANCE row: refused, message points at invoke(obj, ...).
        const int before_calls = static_fixture_t::f_int_calls;
        const std::size_t before = g_records.size();
        const any_t refused = f_int.invoke_static(3);
        CHECK(!refused.is_valid());
        CHECK(static_fixture_t::f_int_calls == before_calls);   // nothing ran
        CHECK(g_records.size() == before + 1);                  // exactly ONE record
        CHECK(g_records.back().level == log_level_enum::CRITICAL);
        CHECK(contains(g_records.back().message, "f(int)"));
        CHECK(contains(g_records.back().message, "receiver"));
        CHECK(contains(g_records.back().message, "invoke(obj"));

        // The typed Ret& form refuses before it touches the caller's slot.
        int out = -1;
        const std::size_t before_out = g_records.size();
        CHECK(!f_int.invoke_static(out, 3));
        CHECK(out == -1);
        CHECK(g_records.size() == before_out + 1);
        CHECK(g_records.back().level == log_level_enum::CRITICAL);

        // The erased and the object-layer forms do the same.
        std::array<any_t, 1> boxed{ any_t(3) };
        const std::size_t before_erased = g_records.size();
        CHECK(!f_int.invoke_static(std::span<const any_t>(boxed)).is_valid());
        CHECK(g_records.size() == before_erased + 1);
        CHECK(g_records.back().level == log_level_enum::CRITICAL);

        const std::size_t before_layer = g_records.size();
        const any_t refused_layer = meta.call_static("f", 3);
        CHECK(!refused_layer.is_valid());
        CHECK(g_records.size() == before_layer + 1);
        CHECK(g_records.back().level == log_level_enum::CRITICAL);
        CHECK(contains(g_records.back().message, "invoke(obj"));   // f(int) is the 3-arg int row
    }

    // -----------------------------------------------------------------------
    // (6) PROPERTY REGRESSION GUARD.
    //
    // static_property_t names the writer "set_v", and the only set_v it declares is STATIC and
    // accessor-shaped (one int, the property's exact type). The property scan keeps the default
    // get_functions<T>() (statics excluded), so there is NO writer candidate: the property binds no
    // writer and is read-only. That is the OBSERVED outcome, and it is the one asserted here. If the
    // scan had been widened, pick_overload() would have selected the static set_v(int), the row's
    // signature is in the table, and the property would have reported itself writable and then run a
    // static thunk with the object's address (ignored) — a silent no-op write.
    // -----------------------------------------------------------------------
    {
        const meta_object_t& props = static_property_t::static_meta_object;
        const meta_property_t v = props.property("v");
        CHECK(v.is_valid());
        CHECK(v.type_name() == "int");
        CHECK(!v.is_writable());                 // the observed outcome: no writer binds
        CHECK(!v.writer().is_valid());           // ... and no handle names the static function
        CHECK(v.writer().signature().empty());

        // The static accessor-shaped functions ARE method rows — they are not accessors, but they are
        // members the table describes.
        const meta_method_t static_set_v = props.method("set_v(int)");
        CHECK(static_set_v.is_valid());
        CHECK(!static_set_v.has_receiver());
        CHECK(static_set_v.method_kind() == method_kind_enum::STATIC_METHOD);
        const meta_method_t static_get_v = props.method("get_v(int)");
        CHECK(static_get_v.is_valid());
        CHECK(!static_get_v.has_receiver());

        // The reader is the ANNOTATED instance getter: it answers the object's value, and the static
        // get_v(int) is never involved (it is not even callable with zero arguments).
        static_property_t object;
        object.value = 7;
        const any_t read_back = v.read(object);
        CHECK(read_back.is_initialized());
        CHECK(held_int(read_back) == 7);
        CHECK(static_property_t::static_get_v_calls == 0);
        object.value = 42;
        CHECK(held_int(v.read(object)) == 42);
        CHECK(static_property_t::static_get_v_calls == 0);
        CHECK(static_property_t::static_set_v_calls == 0);

        // A refused write cannot reach the static function either.
        CHECK(!v.write(object, any_t(5)));
        CHECK(object.value == 42);
        CHECK(static_property_t::static_set_v_calls == 0);
        CHECK(static_property_t::static_v == 0);

        // POSITIVE CONTROL: the instance writer still binds, so the split did not break the scan.
        const meta_property_t w = props.property("w");
        CHECK(w.is_valid());
        CHECK(w.is_writable());
        CHECK(w.writer().is_valid());
        CHECK(w.writer().name() == "set_w");
        CHECK(w.writer().signature() == "set_w(int)");
        CHECK(w.write(object, any_t(11)));
        CHECK(object.value == 10);
        CHECK(held_int(w.read(object)) == 11);
    }

    // -----------------------------------------------------------------------
    // (7) An invalid handle answers the invalid any_t and logs nothing: invoke_static() on a handle
    //     that names no row is a no-op, exactly like invoke().
    // -----------------------------------------------------------------------
    {
        const meta_method_t invalid;
        const std::size_t before = g_records.size();
        CHECK(!invalid.invoke_static(1).is_valid());
        int out = 0;
        CHECK(!invalid.invoke_static(out, 1));
        CHECK(out == 0);
        CHECK(g_records.size() == before);   // nothing to report: an invalid handle is not a misuse
    }

    // -----------------------------------------------------------------------
    // (8) THE HIDDEN EXPLICIT-OBJECT MEMBER: hidden_deducing_this_t compiles (the author opted out
    //     with [[= make_hidden()]]), and the member is simply not collected. This is the conditional
    //     half of the N4 rule — the unhidden half is the WILL_FAIL negative case — and the point is
    //     that the opt-out produces NO diagnostic and NO row, while the class's ordinary members are
    //     unaffected.
    // -----------------------------------------------------------------------
    {
        const meta_object_t& hidden_meta = hidden_deducing_this_t::static_meta_object;
        const std::size_t hidden_base = object_t::static_meta_object.method_count();
        std::printf("  method_count: object_t=%zu hidden_deducing_this_t=%zu (delta=%zu)\n",
            hidden_base, hidden_meta.method_count(), hidden_meta.method_count() - hidden_base);
        // Its own rows are exactly the OBJECT accessor meta_object() const and visible(int): the
        // hidden deducing-this member contributes NOTHING, under either spelling of its key.
        CHECK(hidden_meta.method_count() == hidden_base + 2);
        CHECK(hidden_meta.index_of_method("visible(int)") != index_npos);
        CHECK(hidden_meta.method("visible(int)").is_valid());
        CHECK(hidden_meta.index_of_method("hidden_deducing(int)") == index_npos);
        CHECK(hidden_meta.index_of_method("hidden_deducing(hidden_deducing_this_t&,int)") == index_npos);
        CHECK(!hidden_meta.method("hidden_deducing(int)").is_valid());
        CHECK(!hidden_meta.method("hidden_deducing(hidden_deducing_this_t&,int)").is_valid());
        // The member is still callable as an ordinary C++ function through the type itself.
        hidden_deducing_this_t object;
        object.hidden_deducing(5);
        CHECK(object.value == 5);
        // Nothing was logged about the hidden member (there is no diagnostic to emit): whatever the
        // earlier sections recorded, none of it may mention this member.
        bool mentioned = false;
        for (const log_record_t& record : g_records) {
            if (contains(record.message, "hidden_deducing")) {
                mentioned = true;
                break;
            }
        }
        CHECK(!mentioned);
    }

    // -----------------------------------------------------------------------
    // Summary.
    // -----------------------------------------------------------------------
    if (g_failures == 0) {
        std::printf("  m28: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("  m28: %d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
}
