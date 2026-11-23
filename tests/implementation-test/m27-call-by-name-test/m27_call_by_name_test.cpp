// M27: the `call` family — resolve a member by NAME, then invoke.
//
// `invoke` is SIGNATURE-based (the caller names the row); `call` is NAME-based (the table picks the
// row). This file pins the name-based half and, above all, the DELEGATION rule: `call` resolves once
// with the matching resolve_method() form and then hands the call to that handle's invoke(), so the two
// layers cannot disagree and a failure is diagnosed once.
//
// Every form takes the OBJECT FIRST — (obj, name | signature, [Ret&], [connection_type_t],
// args...) — the QMetaObject::invokeMethod order, so the out slot never competes with the name.
//
// What is covered:
//   * the runtime-name form: add -> value back (5), set -> valid + declared-only void, over(1) /
//     over(1.5) / over(std::string) pick three DIFFERENT rows (asserted through both the return value
//     and a distinguishing side effect);
//   * lvalue arguments: an `int` lvalue against a by-value parameter and a `const std::string` lvalue
//     against `const std::string&` both resolve (the accommodating pass) — a resolution that looked
//     the name up by argument SPELLING would miss them, which is what the mutation test exploits;
//   * the compile-time-name forms answer the same rows for the same name/arguments, and refuse a
//     NON-receiver object by constraint (the static_assert path is the compile-time resolver's);
//   * the `Ret&` forms fill the out slot; a WRONG Ret is refused (false, slot untouched) and reports
//     itself: exactly ONE Critical naming the row, its actual return type and the expected spelling —
//     on the runtime-name and the compile-time-name form alike (the check is the handle's);
//   * connection types follow the invoke family's rule (the same resolve_invoke decision): Queued is
//     postable and answers a DECLARED-ONLY result whose value arrives when the event runs (the event
//     holds the returned any_t's storage, so the variable must not be moved or copied before then);
//     Queued + `Ret&` is postable too and fills the caller's variable on the same terms, and
//     BlockingQueued on the calling thread is skipped with one Warning (it would wait for the loop it
//     is blocking);
//   * a SIGNAL can be called by name and the connected receiver runs;
//   * a missing name -> invalid any_t / false with exactly ONE Warning; an ambiguous name
//     (h() and h() const, non-const object) -> invalid + exactly ONE Critical;
//   * the erased-argument form (one any_t per parameter) agrees with the typed form on the same call,
//     including the `any_t&` return slot;
//   * the erased form's TAGGED twins (call with a connection type) answer the same row as the untagged
//     ones: Direct hands the value back, Queued answers a DECLARED-ONLY result that the event fills,
//     and the `any_t&` slot is filled identically by both shapes;
//   * `any_t` is excluded from the typed `Ret&` forms, so an `any_t` out slot is always the erased
//     overload (the two would otherwise be indistinguishable: both put the slot third);
//   * the whole erased family — the two tagged `call` twins and the four erased `invoke` forms — is
//     pinned at compile time (any_t without an out slot, bool with one).
//
// The fixture: callee_t (add(int,int)->int, set(int)->void, over(int)/over(double)/
// over(const std::string&), SIGNAL(void pinged(int) const)), receiver_t, and ambiguous_t (h() and
// h() const, so a non-const object is ambiguous).

#include <meta>

#include <array>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/meta_type_t.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::metaobject;

namespace m27_call_by_name_test {

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

/// @brief One captured log record (category name, level, text).
struct log_record_t {
    std::string category;
    log_level_enum level;
    std::string message;
};

/// @brief Every record the installed handler received during the current section.
std::vector<log_record_t> g_records;

/// @brief Whether @p haystack contains @p needle.
bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

/// @brief The int an any_t holds, or a sentinel no assertion expects.
/// @note Used instead of any_t::get<int>() directly, so a WRONG overload selection — an invalid or
///       differently typed result — reports a failed CHECK instead of throwing out of the test.
int held_int(const any_t& value) {
    if (value.is_valid() && value.is_initialized() && value.type().is<int>()) {
        return value.get<int>();
    }
    return -9999;
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// The callee: one value row, one void row, a three-way overload set, and a signal.
struct callee_t : object_t {
    OBJECT
public:
    int add(int a, int b) {
        ++add_calls;
        return a + b;
    }

    void set(int v) {
        value = v;
        ++set_calls;
    }

    /// The three `over` rows answer different values AND leave a different side effect, so a wrong
    /// overload selection is visible even if the value were somehow right.
    int over(int v) {
        last_over = 1;
        return 100 + v;
    }

    int over(double v) {
        last_over = 2;
        return 200 + static_cast<int>(v);
    }

    int over(const std::string& text) {
        last_over = 3;
        return 300 + static_cast<int>(text.size());
    }

    SIGNAL(void pinged(int) const)

    int value = 0;
    int set_calls = 0;
    int add_calls = 0;
    int last_over = 0;
};
META_OBJECT(callee_t);

/// The receiver of the signal called by name.
struct receiver_t : object_t {
    OBJECT
public:
    void on_pinged(int v) {
        ++hits;
        last = v;
    }

    int hits = 0;
    int last = 0;
};
META_OBJECT(receiver_t);

/// h() and h() const both match a NON-const object at the same level: an ambiguity.
struct ambiguous_t : object_t {
    OBJECT
public:
    int h() { return 1; }
    int h() const { return 2; }
};
META_OBJECT(ambiguous_t);

} // namespace m27_call_by_name_test

int main(int argc, char** argv) {
    using namespace m27_call_by_name_test;

    core_application app(argc, argv);

    install_log_handler([](log_level_enum level, const log_context_t& context, std::string_view message) {
        g_records.push_back(log_record_t{ std::string(context.category), level, std::string(message) });
        return true;   // consumed: the default sink stays silent
    });

    std::printf("== M27: call by name (resolve once, then delegate to the handle) ==\n");

    const meta_object_t& meta = callee_t::static_meta_object;
    callee_t obj;

    // -----------------------------------------------------------------------------------------
    // (1) The runtime-name form, value row: `add` resolves to add(int,int) and hands back 5.
    //     The same call through invoke(), by signature, answers the same value — `call` is a front
    //     end, not a second implementation.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (1) runtime name: add -> value back --\n");
    g_records.clear();
    {
        const any_t result = meta.call(obj, "add", 2, 3);
        CHECK(result.is_valid());
        CHECK(result.is_initialized());
        CHECK(held_int(result) == 5);
        CHECK(result.type().is<int>());
        CHECK(g_records.empty());

        const any_t by_signature = meta.invoke(obj, "add(int,int)", 2, 3);
        CHECK(by_signature.is_valid());
        CHECK(held_int(by_signature) == held_int(result));
    }

    // -----------------------------------------------------------------------------------------
    // (2) The void row: `set` answers a DECLARED-ONLY void any_t (valid, not initialized) and the
    //     body ran (value + counter).
    // -----------------------------------------------------------------------------------------
    std::printf("-- (2) runtime name: set -> valid, declared-only void --\n");
    g_records.clear();
    {
        const any_t result = meta.call(obj, "set", 7);
        CHECK(result.is_valid());
        CHECK(!result.is_initialized());
        CHECK(result.type().is<void>());
        CHECK(obj.value == 7);
        CHECK(obj.set_calls == 1);
        CHECK(g_records.empty());
    }

    // -----------------------------------------------------------------------------------------
    // (3) The three-way overload set: the NAME alone does not decide, the argument identities do.
    //     int -> over(int), double -> over(double), std::string -> over(const std::string&). The
    //     side effect tells the rows apart even where the value could not.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (3) runtime name: over() picks the row by the arguments --\n");
    g_records.clear();
    {
        const any_t as_int = meta.call(obj, "over", 1);
        CHECK(as_int.is_valid());
        CHECK(held_int(as_int) == 101);
        CHECK(obj.last_over == 1);

        const any_t as_double = meta.call(obj, "over", 1.5);
        CHECK(as_double.is_valid());
        CHECK(held_int(as_double) == 201);
        CHECK(obj.last_over == 2);

        const any_t as_text = meta.call(obj, "over", std::string("abc"));
        CHECK(as_text.is_valid());
        CHECK(held_int(as_text) == 303);
        CHECK(obj.last_over == 3);

        // The same name with the wrong argument list resolves to nothing (no implicit conversion).
        g_records.clear();
        CHECK(!meta.call(obj, "over", 'x').is_valid());
        CHECK(g_records.size() == 1);
        g_records.clear();
    }

    // -----------------------------------------------------------------------------------------
    // (4) LVALUE arguments: an int lvalue against a by-value parameter, and a const std::string
    //     lvalue against const std::string&, both resolve through the accommodating pass. These are
    //     exactly the calls a signature lookup keyed on the argument SPELLINGS cannot answer, so the
    //     mutation test (resolve through the signature instead) trips on them.
    //     The object-first order makes position 3 the OUT SLOT, so a lone lvalue argument has to
    //     travel where no reference can bind (`add`'s second argument here) or through the erased
    //     form (`over`, whose only parameter would otherwise be read as the return slot).
    // -----------------------------------------------------------------------------------------
    std::printf("-- (4) lvalue arguments resolve (accommodating pass) --\n");
    g_records.clear();
    {
        int b = 3;
        const any_t sum = meta.call(obj, "add", 2, b);       // b is the LVALUE argument
        CHECK(sum.is_valid());
        CHECK(held_int(sum) == 5);

        // The erased form records the same (bare type, cvref) identity the typed form builds, so the
        // accommodating pass still resolves the const std::string& row from a const lvalue.
        const std::string text = "abcd";
        const std::array<any_t, 1> boxed_text{ any_t{ text } };
        const any_t sized = meta.call(obj, "over", std::span<const any_t>(boxed_text));
        CHECK(sized.is_valid());
        CHECK(held_int(sized) == 304);
        CHECK(obj.last_over == 3);

        // The const-object rule: add(int,int) is non-const, so a const object cannot call it.
        g_records.clear();
        CHECK(!meta.call(std::as_const(obj), "add", 2, 3).is_valid());
        CHECK(g_records.size() == 1);
        CHECK(g_records.size() == 1 && g_records[0].level == log_level_enum::WARNING);
        g_records.clear();
    }

    // -----------------------------------------------------------------------------------------
    // (5) The COMPILE-TIME name forms answer the same rows as the runtime-name forms for the same
    //     name and arguments.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (5) compile-time name: same rows as the runtime name --\n");
    g_records.clear();
    {
        const any_t ct_add = meta.call<"add">(obj, 2, 3);
        CHECK(ct_add.is_valid());
        CHECK(held_int(ct_add) == 5);
        CHECK(held_int(ct_add) == held_int(meta.call(obj, "add", 2, 3)));

        const any_t ct_set = meta.call<"set">(obj, 8);
        CHECK(ct_set.is_valid());
        CHECK(!ct_set.is_initialized());
        CHECK(obj.value == 8);

        const any_t ct_int = meta.call<"over">(obj, 1);
        CHECK(held_int(ct_int) == held_int(meta.call(obj, "over", 1)));
        CHECK(obj.last_over == 1);

        const any_t ct_double = meta.call<"over">(obj, 1.5);
        CHECK(held_int(ct_double) == held_int(meta.call(obj, "over", 1.5)));
        CHECK(obj.last_over == 2);

        const any_t ct_text = meta.call<"over">(obj, std::string("abc"));
        CHECK(held_int(ct_text) == held_int(meta.call(obj, "over", std::string("abc"))));
        CHECK(obj.last_over == 3);
        CHECK(g_records.empty());
    }

    // -----------------------------------------------------------------------------------------
    // (6) The Ret& forms: the out slot is filled by the handle's invoke(obj, ret, ...), which also
    //     verifies the return type. A WRONG Ret is false and leaves the slot untouched — and it
    //     REPORTS itself:
    //     exactly ONE Critical naming the signature, the actual return type and the expected spelling.
    //     Both the runtime-name and the compile-time-name form share that check (it lives in the
    //     handle), so each answers the same single record.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (6) Ret& forms: fill, and report a wrong Ret once --\n");
    g_records.clear();
    {
        int out = -1;
        CHECK(meta.call(obj, "add", out, 2, 3));
        CHECK(out == 5);
        CHECK(g_records.empty());

        int ct_out = -1;
        CHECK(meta.call<"add">(obj, ct_out, 4, 5));
        CHECK(ct_out == 9);
        CHECK(g_records.empty());

        // A WRONG Ret (runtime name): refused before anything runs, slot untouched, one Critical.
        double wrong = -1.5;
        CHECK(!meta.call(obj, "add", wrong, 2, 3));
        CHECK(wrong == -1.5);
        CHECK(g_records.size() == 1);
        if (g_records.size() == 1) {
            const log_record_t& record = g_records[0];
            std::printf("  log: [%s] %s\n", record.category.c_str(), record.message.c_str());
            CHECK(record.level == log_level_enum::CRITICAL);
            CHECK(record.category == "metaobject.connection");
            CHECK(contains(record.message, "add(int,int)"));
            CHECK(contains(record.message, "'int'"));       // the row's actual return type
            CHECK(contains(record.message, "'double'"));    // the caller's expected spelling
        }

        // The compile-time name form answers the same one record: the check is the handle's, and both
        // name forms hand the call to it.
        g_records.clear();
        double ct_wrong = -1.5;
        CHECK(!meta.call<"add">(obj, ct_wrong, 2, 3));
        CHECK(ct_wrong == -1.5);
        CHECK(g_records.size() == 1);
        if (g_records.size() == 1) {
            const log_record_t& record = g_records[0];
            CHECK(record.level == log_level_enum::CRITICAL);
            CHECK(record.category == "metaobject.connection");
            CHECK(contains(record.message, "add(int,int)"));
            CHECK(contains(record.message, "'int'"));
            CHECK(contains(record.message, "'double'"));
        }
    }

    // -----------------------------------------------------------------------------------------
    // (7) Connection types: Queued is postable and the result is DECLARED-ONLY on return — the value
    //     arrives when the event runs, because the event holds the returned variable's storage. The
    //     invoke family (by signature) answers the same shape for the same call.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (7) connection type: Queued is postable, value arrives on processing --\n");
    g_records.clear();
    {
        const any_t posted = meta.call(obj, "set", connection_type_flag::QUEUED, 42);
        CHECK(posted.is_valid());
        CHECK(!posted.is_initialized());
        CHECK(posted.type().is<void>());
        CHECK(obj.value == 8);            // posted, not run

        // The returned any_t is the event's slot: it must stay HERE and unmoved until the queue runs
        // (the whole point of the lifetime contract below).
        const int adds_before = obj.add_calls;
        any_t posted_value = meta.call(obj, "add", connection_type_flag::QUEUED, 2, 3);
        CHECK(posted_value.is_valid());
        CHECK(!posted_value.is_initialized());   // declared only: nothing has run yet
        CHECK(posted_value.type().is<int>());
        CHECK(obj.add_calls == adds_before);     // posted, not run

        const any_t via_invoke = meta.invoke(obj, "add(int,int)", connection_type_flag::QUEUED, 2, 3);
        CHECK(via_invoke.is_valid());
        CHECK(!via_invoke.is_initialized());
        CHECK(via_invoke.type() == posted_value.type());

        // Drain: the posted calls run now. `set` proves the body ran; `add` now has a slot to write.
        event_dispatcher::instance().send_posted_events();
        CHECK(obj.value == 42);
        CHECK(obj.set_calls == 3);     // (2)'s Direct call, (5)'s compile-time one, and this posted one
        CHECK(posted_value.get<int>() == 5);     // the event filled the variable we still hold
        CHECK(obj.add_calls == adds_before + 2); // this posted add and the invoke()-form one
        CHECK(g_records.empty());
    }

    // Queued + Ret& has a return channel too — the caller's variable — so it is POSTED and the slot is
    // untouched until the event runs. No Warning: the old "cannot deliver the return value" refusal is
    // gone.
    std::printf("-- (7b) connection type: Queued + Ret& delivers on processing --\n");
    g_records.clear();
    {
        const int adds_before = obj.add_calls;
        int qout = -1;
        CHECK(meta.call(obj, "add", qout, connection_type_flag::QUEUED, 2, 3));
        CHECK(qout == -1);                 // the event holds the ADDRESS; nothing has run yet
        CHECK(obj.add_calls == adds_before);
        CHECK(g_records.empty());          // not a refusal any more: zero records
        event_dispatcher::instance().send_posted_events();
        CHECK(qout == 5);
        CHECK(obj.add_calls == adds_before + 1);
        CHECK(g_records.empty());
    }

    // BlockingQueued on the CALLING thread would wait for a loop this call is blocking: the handle
    // skips it and answers the invalid any_t, with one Warning. The invoke family's decision is the
    // same one (resolve_invoke), so both agree.
    std::printf("-- (7c) connection type: BlockingQueued on this thread is skipped --\n");
    g_records.clear();
    {
        const any_t blocked = meta.call(obj, "add", connection_type_flag::BLOCKING_QUEUED, 2, 3);
        CHECK(!blocked.is_valid());
        CHECK(g_records.size() == 1);
        if (g_records.size() == 1) {
            std::printf("  log: [%s] %s\n", g_records[0].category.c_str(),
                g_records[0].message.c_str());
            CHECK(g_records[0].level == log_level_enum::WARNING);
            CHECK(contains(g_records[0].message, "deadlock"));
        }

        g_records.clear();
        CHECK(!meta.invoke(obj, "add(int,int)", connection_type_flag::BLOCKING_QUEUED, 2, 3).is_valid());
        CHECK(g_records.size() == 1);
    }

    // -----------------------------------------------------------------------------------------
    // (8) A SIGNAL can be called by name: the row resolves, the thunk runs the signal body, and the
    //     connected receiver is dispatched. The runtime-name and compile-time-name forms both reach
    //     the same signal row.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (8) a signal called by name reaches the receiver --\n");
    g_records.clear();
    {
        receiver_t receiver;
        const connection_handle_t connection =
            obj.connect("pinged(int) const", &receiver, "on_pinged(int)");
        CHECK(connection.is_valid());

        const any_t emitted = meta.call(obj, "pinged", 42);
        CHECK(emitted.is_valid());
        CHECK(!emitted.is_initialized());        // void row
        CHECK(receiver.hits == 1);
        CHECK(receiver.last == 42);

        const any_t emitted_ct = meta.call<"pinged">(obj, 43);
        CHECK(emitted_ct.is_valid());
        CHECK(receiver.hits == 2);
        CHECK(receiver.last == 43);
        CHECK(g_records.empty());
    }

    // -----------------------------------------------------------------------------------------
    // (9) A missing name: the INVALID any_t, and exactly ONE Warning — `call` does not log on top of
    //     the resolver. The Ret form answers false on the same single record.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (9) a missing name: invalid handle, one Warning --\n");
    g_records.clear();
    {
        const any_t missing = meta.call(obj, "nope", 1);
        CHECK(!missing.is_valid());
        CHECK(g_records.size() == 1);
        if (g_records.size() == 1) {
            const log_record_t& record = g_records[0];
            std::printf("  log: [%s] %s\n", record.category.c_str(), record.message.c_str());
            CHECK(record.level == log_level_enum::WARNING);
            CHECK(record.category == "metaobject.connection");
            CHECK(contains(record.message, "nope"));
            CHECK(contains(record.message, "callee_t"));
        }

        g_records.clear();
        int missing_out = -1;
        CHECK(!meta.call(obj, "nope", missing_out, 1));
        CHECK(missing_out == -1);
        CHECK(g_records.size() == 1);
        CHECK(g_records.size() == 1 && g_records[0].level == log_level_enum::WARNING);
    }

    // -----------------------------------------------------------------------------------------
    // (10) An ambiguous name: the INVALID any_t and exactly ONE Critical. The Ret form answers false
    //      on the same single record.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (10) an ambiguity: invalid handle, one Critical --\n");
    g_records.clear();
    {
        ambiguous_t ambiguous;
        const any_t ambiguous_result = ambiguous_t::static_meta_object.call(ambiguous, "h");
        CHECK(!ambiguous_result.is_valid());
        CHECK(g_records.size() == 1);
        if (g_records.size() == 1) {
            const log_record_t& record = g_records[0];
            std::printf("  log: [%s] %s\n", record.category.c_str(), record.message.c_str());
            CHECK(record.level == log_level_enum::CRITICAL);
            CHECK(record.category == "metaobject.connection");
            CHECK(contains(record.message, "ambiguous"));
            CHECK(contains(record.message, "h() const"));
            CHECK(contains(record.message, "ambiguous_t"));
        }

        g_records.clear();
        int ambiguous_out = -1;
        CHECK(!ambiguous_t::static_meta_object.call(ambiguous, "h", ambiguous_out));
        CHECK(ambiguous_out == -1);
        CHECK(g_records.size() == 1);
        CHECK(g_records.size() == 1 && g_records[0].level == log_level_enum::CRITICAL);
    }

    // -----------------------------------------------------------------------------------------
    // (11) The erased-argument form agrees with the typed form on the same call, and the erased
    //      `any_t&` return slot is the one the typed forms deliberately exclude (Ret = any_t).
    // -----------------------------------------------------------------------------------------
    std::printf("-- (11) erased arguments agree with the typed form --\n");
    g_records.clear();
    {
        const std::array<any_t, 2> boxed{ any_t{ 2 }, any_t{ 3 } };
        const std::span<const any_t> args(boxed);

        const any_t erased = meta.call(obj, "add", args);
        CHECK(erased.is_valid());
        CHECK(erased.is_initialized());
        CHECK(held_int(erased) == 5);
        CHECK(held_int(erased) == held_int(meta.call(obj, "add", 2, 3)));

        // The same identities pick the same `over` row, including the const std::string& one.
        const std::array<any_t, 1> boxed_text{ any_t{ std::string("abc") } };
        const any_t erased_text = meta.call(obj, "over", std::span<const any_t>(boxed_text));
        CHECK(erased_text.is_valid());
        CHECK(held_int(erased_text) == 303);
        CHECK(obj.last_over == 3);

        // The erased return slot: declared only, of the row's return type.
        any_t slot(meta_type_t::from_type<int>());
        CHECK(!slot.is_initialized());
        CHECK(meta.call(obj, "add", slot, args));
        CHECK(slot.is_initialized());
        CHECK(held_int(slot) == 5);

        // A missing name through the erased form still logs exactly once.
        g_records.clear();
        CHECK(!meta.call(obj, "nope", args).is_valid());
        CHECK(g_records.size() == 1);
        CHECK(g_records.size() == 1 && g_records[0].level == log_level_enum::WARNING);
    }

    // -----------------------------------------------------------------------------------------
    // (12) The erased `call` family gains its TAGGED twins. Same order as every other form —
    //      obj, name, [out], connectiontype, args — and the same delegation: resolve_method() once (with
    //      the boxed identities standing in for `Ts...`), then the handle's matching erased invoke().
    //      Direct hands the value back; Queued answers the DECLARED-ONLY slot whose value the event
    //      writes; the `any_t&` slot is filled identically by the tagged and the untagged shape.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (12) erased call + connection type: Direct / Queued / out slot --\n");
    g_records.clear();
    {
        const std::array<any_t, 2> boxed{ any_t{ 2 }, any_t{ 3 } };
        const std::span<const any_t> args(boxed);

        // (i) Direct, no out slot: the value comes back — and it is the value the untagged twin and
        //     the typed call answer for the same call, because all three resolve the same row.
        const any_t direct = meta.call(obj, "add", connection_type_flag::DIRECT, args);
        CHECK(direct.is_valid());
        CHECK(direct.is_initialized());
        CHECK(held_int(direct) == 5);
        CHECK(held_int(direct) == held_int(meta.call(obj, "add", args)));
        CHECK(held_int(direct) == held_int(meta.call(obj, "add", 2, 3)));
        CHECK(g_records.empty());

        // (ii) Queued, no out slot: POSTED, so the answer is DECLARED ONLY — and it must not be moved
        //      or copied before the queue is drained, because the event holds THIS variable's storage.
        const int adds_before = obj.add_calls;
        any_t queued = meta.call(obj, "add", connection_type_flag::QUEUED, args);
        CHECK(queued.is_valid());
        CHECK(!queued.is_initialized());          // nothing has run yet
        CHECK(queued.type().is<int>());           // ... but the row's return type is declared
        CHECK(obj.add_calls == adds_before);      // posted, not run
        event_dispatcher::instance().send_posted_events();
        CHECK(queued.is_initialized());           // the event marked it once it wrote
        CHECK(held_int(queued) == 5);
        CHECK(obj.add_calls == adds_before + 1);  // the body ran exactly once, on the queue
        CHECK(g_records.empty());

        // (iii) The erased out slot, both shapes: untagged is Direct, tagged honours the type, and the
        //       caller's DECLARED-ONLY any_t receives the same value.
        any_t direct_out(meta_type_t::from_type<int>());
        CHECK(!direct_out.is_initialized());
        CHECK(meta.call(obj, "add", direct_out, args));
        CHECK(direct_out.is_initialized());
        CHECK(held_int(direct_out) == 5);

        any_t tagged_out(meta_type_t::from_type<int>());
        CHECK(meta.call(obj, "add", tagged_out, connection_type_flag::DIRECT, args));
        CHECK(tagged_out.is_initialized());
        CHECK(held_int(tagged_out) == 5);
        CHECK(held_int(tagged_out) == held_int(direct_out));   // the twin answers the same call
        CHECK(tagged_out.type() == direct_out.type());

        // A QUEUED out slot: untouched until the event runs, then holding the result (the EVENT marks
        // it, the only place that knows when it wrote) — the same contract as the typed Queued form.
        const int adds_before_queued_out = obj.add_calls;
        any_t queued_out(meta_type_t::from_type<int>());
        CHECK(meta.call(obj, "add", queued_out, connection_type_flag::QUEUED, args));
        CHECK(!queued_out.is_initialized());
        CHECK(obj.add_calls == adds_before_queued_out);        // posted, not run
        event_dispatcher::instance().send_posted_events();
        CHECK(queued_out.is_initialized());
        CHECK(held_int(queued_out) == 5);
        CHECK(obj.add_calls == adds_before_queued_out + 1);
        CHECK(g_records.empty());

        // (iv) A miss is the RESOLVER's one Warning on the tagged forms too: `call` still adds no
        //      diagnostic of its own, and the caller's slot stays declared only.
        g_records.clear();
        any_t miss_out(meta_type_t::from_type<int>());
        CHECK(!meta.call(obj, "nope", connection_type_flag::DIRECT, args).is_valid());
        CHECK(!meta.call(obj, "nope", miss_out, connection_type_flag::DIRECT, args));
        CHECK(!miss_out.is_initialized());
        CHECK(g_records.size() == 2);          // one Warning per call, and nothing else
        CHECK(g_records.size() == 2 && g_records[0].level == log_level_enum::WARNING
              && g_records[1].level == log_level_enum::WARNING);
        CHECK(g_records.size() == 2 && contains(g_records[0].message, "nope")
              && contains(g_records[1].message, "nope"));
        g_records.clear();

        // (v) A value-returning argument MISMATCH does not throw through this entry point: the boxed
        //     identities simply resolve no row (one Warning), unlike the erased `invoke` forms, whose
        //     argument match is the handle's throw. Asserted here so the two channels stay distinct.
        const std::array<any_t, 2> wrong{ any_t{ 2 }, any_t{ std::string("three") } };
        CHECK(!meta.call(obj, "add", connection_type_flag::DIRECT,
            std::span<const any_t>(wrong)).is_valid());
        CHECK(g_records.size() == 1);
        CHECK(g_records.size() == 1 && g_records[0].level == log_level_enum::WARNING);
        g_records.clear();
    }

    // -----------------------------------------------------------------------------------------
    // (13) The whole erased family is pinned at COMPILE time: the two tagged `call` twins and the four
    //      erased `invoke` forms. The order is obj, name|signature, [out], connectiontype, args, and
    //      the return type is any_t without an out slot and bool with one — the same shape the typed
    //      siblings declare, so a reorder or a flip of the two would break THIS file.
    // -----------------------------------------------------------------------------------------
    std::printf("-- (13) erased family: decltype pinned (any_t / bool) --\n");
    {
        const std::array<any_t, 2> boxed{ any_t{ 2 }, any_t{ 3 } };
        const std::span<const any_t> args(boxed);
        any_t any_out(meta_type_t::from_type<int>());

        // call: the two forms that exist, plus the two tagged twins.
        static_assert(std::is_same_v<decltype(meta.call(obj, "add", args)), any_t>);
        static_assert(std::is_same_v<decltype(meta.call(
            obj, "add", connection_type_flag::DIRECT, args)), any_t>);
        static_assert(std::is_same_v<decltype(meta.call(obj, "add", any_out, args)), bool>);
        static_assert(std::is_same_v<decltype(meta.call(
            obj, "add", any_out, connection_type_flag::DIRECT, args)), bool>);

        // invoke: all four erased forms.
        static_assert(std::is_same_v<decltype(meta.invoke(obj, "add(int,int)", args)), any_t>);
        static_assert(std::is_same_v<decltype(meta.invoke(
            obj, "add(int,int)", connection_type_flag::DIRECT, args)), any_t>);
        static_assert(std::is_same_v<decltype(meta.invoke(
            obj, "add(int,int)", any_out, args)), bool>);
        static_assert(std::is_same_v<decltype(meta.invoke(
            obj, "add(int,int)", any_out, connection_type_flag::DIRECT, args)), bool>);

        // The erased forms own anything CONVERTIBLE to the span, not just the exact type: an
        // std::array / std::vector of any_t is an argument LIST, never one boxed argument.
        static_assert(std::is_same_v<decltype(meta.call(obj, "add", boxed)), any_t>);
        static_assert(std::is_same_v<decltype(meta.invoke(obj, "add(int,int)", boxed)), any_t>);
        // ... and a std::vector<any_t> lvalue too (the converting constructor of std::span).
        std::vector<any_t> vector_args{ any_t{ 2 }, any_t{ 3 } };
        static_assert(std::is_same_v<decltype(meta.call(obj, "add", vector_args)), any_t>);
        static_assert(std::is_same_v<decltype(meta.invoke(
            obj, "add(int,int)", vector_args)), any_t>);
        // The connection_type_t wrapper is accepted where the enum is (same tag, either spelling).
        static_assert(std::is_same_v<decltype(meta.call(
            obj, "add", connection_type_t{ connection_type_flag::DIRECT }, args)), any_t>);

        // The erased `invoke` family resolves and delegates exactly like the typed one: same row, same
        // value, same out slot — this is the "front end, not a second implementation" claim, checked.
        const any_t erased_by_signature = meta.invoke(obj, "add(int,int)", args);
        CHECK(erased_by_signature.is_valid());
        CHECK(held_int(erased_by_signature) == 5);
        any_t signature_out(meta_type_t::from_type<int>());
        CHECK(meta.invoke(obj, "add(int,int)", signature_out, args));
        CHECK(held_int(signature_out) == 5);
        CHECK(g_records.empty());
    }

    install_log_handler(log_handler{ });

    if (g_failures == 0) {
        std::printf("  M27 call by name: all %d checks passed\n", g_checks);
    }
    else {
        std::printf("  M27 call by name: %d check(s) failed\n", g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
