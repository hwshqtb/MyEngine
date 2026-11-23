// M18: constructors in the meta-object — QMetaObject::constructor / newInstance equivalent.
//
// Constructors follow exactly the same rule as ordinary methods: reflected by default, excluded by
// [[= make_hidden()]] — and make_hidden() is the ONLY exclusion. The table describes what the class
// DECLARES: the copy constructor, the move constructor, a deleted constructor, a private constructor
// and the constructors of an abstract class are all in it. Whether one can actually be CALLED is a
// separate question, answered at compile time (std::is_constructible_v on the thunk's argument list)
// and reported at run time: new_instance() answers nullptr and logs a Critical before it allocates
// anything. Hiding an entry would make the table lie about the class.
//
// Constructors have their OWN table (meta_info_t::constructor_infos) and their own handle,
// meta_constructor_t: they are not methods. method_count() / method(i) / method(signature) /
// index_of_method() never see a constructor, and constructor_count() / constructor(i) /
// index_of_constructor() cover this class's own constructors only (constructors are not inherited).
//
// What this test pins down:
//  1. constructor_count() counts the reflected constructors (default + (int,int) + copy + move) and
//     nothing else, and method_count() no longer counts them;
//  2. index_of_constructor / constructor(i) agree on the signature and on the entry;
//  3. new_instance(args) builds a real object from the passed values, and
//     new_instance(args, any_t&) writes the new instance's pointer through the caller's out alias;
//  4. an argument list that fits no constructor answers nullptr plus a Warning through
//     log_connection;
//  5. a throwing constructor answers nullptr, logs a Critical from its thunk and another from
//     new_instance, and leaves no object behind;
//  6. a constructor marked make_hidden() does not appear;
//  7. the copy and the move constructor appear, and new_instance really copies / moves through them
//     (the argument's recorded cvref selects which one);
//  8. a constructor handle is meta_constructor_t, never meta_method_t: the method space cannot name
//     a constructor at all (method(signature) invalid, index_of_method(signature) == index_npos);
//  9. non-trivial parameters: the constructor is selected by the BARE parameter type names, so a
//     reference parameter is as reachable as a by-value one;
// 10. ACCESS IS ONLY A HINT. A class with a private constructor and a private method is fully
//     usable through its meta-object: OBJECT's friend declarations give the table's thunks (static
//     members of meta_info_t) the class's own access, so a private AND a protected constructor are
//     in the table, are VALID (there is a real thunk: caller != nullptr), are built by new_instance()
//     like public ones, and report their specifier through access(). A private method is invocable
//     through invoke(). What still cannot be called is a deleted constructor and every
//     constructor of an abstract class: they stay in the table, but no thunk can be generated for
//     them, so is_valid() is false and new_instance() reports nullptr plus a Critical. That is the
//     failure taxonomy: unknown signature = Warning (a typo), known but not callable = Critical (a
//     defect of the class), constructor threw = Critical (a runtime failure).

#include <meta>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <hwshqtb/metaobject/annotation.h>
#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_constructor_t.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::metaobject;

// ---------------------------------------------------------------------------
// The test classes live at global scope, NOT in an anonymous namespace: a type declared inside an
// anonymous namespace has no stable global name, so the canonical namer reports it as an error (its
// identity could never be a table key). Global scope gives it the plain, stable name "x_t", which is
// what the reference-parameter signatures below spell ("x_t(const x_t&)"). They are only used by this
// translation unit anyway.
//
// A class with four reflected constructors: the default one, (int,int), the copy and the move.
//
// Two exclusions pin the rule down: the (int) overload carries make_hidden() and must not appear;
// nothing else is excluded, so the copy / move constructors DO appear even though — or rather
// because — the table describes the class. object_t is neither copyable nor movable, so the two of
// them build the base fresh and really copy / move the pair of ints; the move leaves a -1 marker on
// the source, which is how the test sees that it ran.
// ---------------------------------------------------------------------------
struct x_t : object_t {
    OBJECT
public:
    int a = 0;
    int b = 0;

    x_t() = default;
    x_t(int x, int y) : a(x), b(y) { }

    [[= make_hidden()]]
    x_t(int only) : a(only) { }

    x_t(const x_t& other) : object_t(), a(other.a), b(other.b) { }
    x_t(x_t&& other) : object_t(), a(other.a), b(other.b) {
        other.a = -1;
        other.b = -1;
    }

    int add(int lhs, int rhs) const { return lhs + rhs; }
};
META_OBJECT(x_t);

// ---------------------------------------------------------------------------
// A class whose only constructor throws on demand.
// The counters are the leak check: a constructor that throws leaves neither a completed object nor
// a destroyed one, and new_instance() releases the storage it allocated.
// ---------------------------------------------------------------------------
struct throwing_t : object_t {
    OBJECT
public:
    static inline int attempts = 0;
    static inline int completed = 0;
    static inline int destroyed = 0;

    explicit throwing_t(int value) {
        ++attempts;
        if (value < 0) {
            throw std::runtime_error("boom");
        }
        ++completed;
    }

    ~throwing_t() override {
        ++destroyed;
    }
};
META_OBJECT(throwing_t);

// ---------------------------------------------------------------------------
// A class whose constructors take non-trivial parameters: a reference parameter and a by-value
// std::string. They cover the two halves of the typed-overload rule: a by-value parameter is spelled
// the same way at the call site (so the typed form finds it), a reference parameter is not (so only
// the signature form can address it) — exactly the limitation check_args<Ts...>() already has.
// ---------------------------------------------------------------------------
struct ref_t : object_t {
    OBJECT
public:
    std::string text;
    int count = 0;

    explicit ref_t(const std::string& value) : text(value) { }
    ref_t(std::string value, int n) : text(std::move(value)), count(n) { }
};
META_OBJECT(ref_t);

// ---------------------------------------------------------------------------
// (10a) A class holding a std::unique_ptr: its copy constructor is implicitly deleted. The copy
// constructor is still a declaration the class has, so it is still reflected.
// ---------------------------------------------------------------------------
struct unique_owning_t : object_t {
    OBJECT
public:
    std::unique_ptr<int> owned = std::make_unique<int>(7);
    int tag = 0;

    unique_owning_t() = default;
};
META_OBJECT(unique_owning_t);

// ---------------------------------------------------------------------------
// (10b) A class with a private constructor and a private method. The table holds both, and since
// the friend declarations OBJECT adds give meta_info_t's thunks the class's own access, both are
// reachable through the meta-object: the constructor through new_instance(), the method through
// invoke(). access() reports the specifier, which is a hint and nothing more.
// ---------------------------------------------------------------------------
struct private_ctor_t : object_t {
    OBJECT
public:
    int value = 0;
    static private_ctor_t* create(int value) { return new private_ctor_t(value); }

private:
    explicit private_ctor_t(int value) : value(value) { }
    int secret(int x) const { return value * 10 + x; }
};
META_OBJECT(private_ctor_t);

// ---------------------------------------------------------------------------
// (10b2) The same rule for a PROTECTED constructor. There is no public factory: the only way to an
// instance is the meta-object (or a derived class), which is exactly what the test exercises.
// ---------------------------------------------------------------------------
struct protected_ctor_t : object_t {
    OBJECT
public:
    int value = 0;

protected:
    explicit protected_ctor_t(int value) : value(value) { }
};
META_OBJECT(protected_ctor_t);

// ---------------------------------------------------------------------------
// (10c) An abstract class: it declares constructors, and none of them can be called — the type
// cannot be instantiated at all.
// ---------------------------------------------------------------------------
struct abstract_ctor_t : object_t {
    OBJECT
public:
    abstract_ctor_t() = default;
    int seed = 5;

    virtual int pure() const = 0;
};
META_OBJECT(abstract_ctor_t);

namespace constructor_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// ---------------------------------------------------------------------------
// Log capture: every record the library emits while the test runs.
// ---------------------------------------------------------------------------
struct record_t {
    std::string category;
    log_level_enum level = log_level_enum::DEBUG;
    std::string message;
};

std::vector<record_t>& records() {
    static std::vector<record_t> captured;
    return captured;
}

std::size_t count_records(log_level_enum level, std::string_view needle) {
    std::size_t found = 0;
    for (const record_t& record : records()) {
        if (record.level == level && record.message.find(needle) != std::string::npos) {
            ++found;
        }
    }
    return found;
}

}  // namespace constructor_test

using namespace constructor_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    install_log_handler([](log_level_enum level, const log_context_t& context, std::string_view message) {
        records().push_back(record_t{ std::string(context.category), level, std::string(message) });
        return true;  // consumed: the default stderr sink stays silent
    });

    std::printf("== M18: constructors ==\n");

    const meta_object_t& meta = x_t::static_meta_object;

    // -----------------------------------------------------------------------
    // (1) constructor_count(): the default constructor, (int,int), the copy and the move
    //     constructor are reflected; the hidden one and nothing else is left out.
    //
    // The count is THIS class's own constructors, like Qt's QMetaObject::constructorCount():
    // constructors are not inherited, so a base's rows are not part of the view. x_t contributes
    // exactly four.
    // -----------------------------------------------------------------------
    CHECK(meta.constructor_count() == 4);

    // Constructors are NOT in the method table any more, so only x_t's own two methods
    // (meta_object from OBJECT, add) are added; the four constructors count separately above.
    CHECK(meta.method_count() == object_t::static_meta_object.method_count() + 2);
    CHECK(meta.method_count() >= meta.constructor_count());

    // -----------------------------------------------------------------------
    // (2) index_of_constructor / constructor(i) describe the same entry.
    // -----------------------------------------------------------------------
    const index_t pair_index = meta.index_of_constructor("x_t(int,int)");
    CHECK(pair_index != index_npos);
    CHECK(pair_index < meta.constructor_count());
    CHECK(meta.constructor(pair_index).is_valid());
    CHECK(meta.constructor(pair_index).signature() == "x_t(int,int)");
    CHECK(meta.constructor(pair_index).param_count() == 2u);
    CHECK(meta.constructor(pair_index).param_type_name(0) == "int");

    // Every constructor of the own-only view IS a table entry: signature() is non-empty and the
    // handle names its own slot. x_t's four constructors are all callable, so is_valid() is true;
    // a deleted / abstract one stays in this table with a false verdict (covered in (10)).
    bool saw_default = false;
    bool saw_pair = false;
    bool saw_copy = false;
    bool saw_move = false;
    for (index_t i = 0; i < meta.constructor_count(); ++i) {
        const meta_constructor_t ctor = meta.constructor(i);
        CHECK(!ctor.signature().empty());
        CHECK(ctor.is_valid());
        CHECK(ctor.index() == i);                    // the constructor coordinate, its own space
        // The split: no method-space lookup can name a constructor.
        CHECK(meta.index_of_method(ctor.signature()) == index_npos);
        CHECK(!meta.method(ctor.signature()).is_valid());
        if (ctor.signature() == "x_t()") {
            saw_default = true;
        }
        if (ctor.signature() == "x_t(int,int)") {
            saw_pair = true;
        }
        if (ctor.signature() == "x_t(const x_t&)") {
            saw_copy = true;
        }
        if (ctor.signature() == "x_t(x_t&&)") {
            saw_move = true;
        }
    }
    CHECK(saw_default);
    CHECK(saw_pair);
    CHECK(saw_copy);
    CHECK(saw_move);

    // Out of range answers an invalid handle, not a crash.
    CHECK(!meta.constructor(meta.constructor_count()).is_valid());

    // -----------------------------------------------------------------------
    // (3) new_instance(args) builds a real object: the arguments are boxed as one any_t each and the
    //     constructor is selected by that list (name + cvref, never an implicit conversion).
    // -----------------------------------------------------------------------
    int first = 3;
    int second = 4;
    object_t* made = static_cast<object_t*>(meta.new_instance(first, second));
    CHECK(made != nullptr);
    if (made != nullptr) {
        auto* typed = static_cast<x_t*>(made);
        CHECK(typed->a == 3);
        CHECK(typed->b == 4);
        CHECK(typed->add(typed->a, typed->b) == 7);   // the new object's methods work
        delete typed;                                  // ordinary delete: the storage came from new
    }

    // The default constructor needs no arguments at all.
    object_t* default_made = static_cast<object_t*>(meta.new_instance());
    CHECK(default_made != nullptr);
    delete default_made;

    // The same call through the any_t entry point: the out slot is an ALIAS to the caller's pointer
    // variable, and the constructing thunk writes the new instance's address through it.
    x_t* aliased_made = nullptr;
    any_t aliased_out(std::ref(aliased_made));
    CHECK(meta.new_instance(std::span<const any_t>{}, aliased_out));
    CHECK(aliased_made != nullptr);
    delete aliased_made;

    // The typed overload boxes its arguments and delegates to the any_t form.
    int seven = 7;
    int eight = 8;
    object_t* typed_made = static_cast<object_t*>(meta.new_instance(seven, eight));
    CHECK(typed_made != nullptr);
    if (typed_made != nullptr) {
        auto* typed = static_cast<x_t*>(typed_made);
        CHECK(typed->a == 7);
        CHECK(typed->b == 8);
        delete typed;
    }

    // An argument list that fits no constructor is rejected, not guessed: x_t(int) is hidden, so a
    // single int selects nothing (nullptr plus a Warning naming the list it looked for).
    CHECK(meta.new_instance(first) == nullptr);

    // -----------------------------------------------------------------------
    // (4) Unknown signature: nullptr plus a Warning through log_connection (the caller may have
    //     mistyped it — the table has nothing to say about that).
    // -----------------------------------------------------------------------
    {
        const std::size_t before = records().size();
        CHECK(meta.new_instance(1.0, 2.0) == nullptr);
        CHECK(records().size() == before + 1);
        CHECK(records().back().level == log_level_enum::WARNING);
        CHECK(records().back().category == "metaobject.connection");
        CHECK(records().back().message.find("x_t(double,double)") != std::string::npos);
    }

    // -----------------------------------------------------------------------
    // (5) A throwing constructor: nullptr, two Criticals, and no object left behind.
    // -----------------------------------------------------------------------
    {
        const std::size_t before = records().size();
        int negative = -1;
        CHECK(throwing_t::static_meta_object.new_instance(negative) == nullptr);

        // The thunk logs the exception at Critical, new_instance logs the failure at Critical: two
        // records, not one. (A runtime failure is not a caller mistake, so neither is a Warning.)
        CHECK(records().size() == before + 2);
        CHECK(count_records(log_level_enum::CRITICAL, "constructor of throwing_t") == 1);
        CHECK(count_records(log_level_enum::CRITICAL, "construction through 'throwing_t(int)'") == 1);
        CHECK(count_records(log_level_enum::WARNING, "throwing_t") == 0);

        // No object was constructed and none was destroyed: the storage was released again, and no
        // half-built throwing_t exists.
        CHECK(throwing_t::attempts == 1);
        CHECK(throwing_t::completed == 0);
        CHECK(throwing_t::destroyed == 0);
    }

    // A successful construction through the same entry pairs up with its destruction.
    {
        int positive = 5;
        object_t* good = static_cast<object_t*>(
            throwing_t::static_meta_object.new_instance(positive));
        CHECK(good != nullptr);
        CHECK(throwing_t::completed == 1);
        CHECK(throwing_t::destroyed == 0);
        delete good;
        CHECK(throwing_t::destroyed == 1);
    }

    // -----------------------------------------------------------------------
    // (6) A hidden constructor is absent from the constructor table, and no constructor is
    //     reachable through the method space in the first place.
    // -----------------------------------------------------------------------
    CHECK(meta.index_of_constructor("x_t(int)") == index_npos);
    CHECK(meta.index_of_method("x_t(int)") == index_npos);
    CHECK(!meta.method("x_t(int)").is_valid());
    for (index_t i = 0; i < meta.constructor_count(); ++i) {
        const meta_constructor_t ctor = meta.constructor(i);
        CHECK(ctor.signature() != "x_t(int)");   // the hidden (int) overload
    }

    // -----------------------------------------------------------------------
    // (7) The copy and the move constructor are ordinary constructors: they appear, and new_instance
    //     builds a real copy / a real move through them.
    // -----------------------------------------------------------------------
    const index_t copy_index = meta.index_of_constructor("x_t(const x_t&)");
    const index_t move_index = meta.index_of_constructor("x_t(x_t&&)");
    CHECK(copy_index != index_npos);
    CHECK(move_index != index_npos);
    CHECK(meta.constructor(copy_index).is_valid());
    CHECK(meta.constructor(copy_index).param_count() == 1u);
    CHECK(meta.constructor(move_index).is_valid());
    // The copy / move constructors are constructors in the table ABOVE, and absent from the method
    // space: neither the exact lookup nor the signature form resolves them as methods.
    CHECK(meta.index_of_method("x_t(const x_t&)") == index_npos);
    CHECK(meta.index_of_method("x_t(x_t&&)") == index_npos);
    CHECK(!meta.method("x_t(const x_t&)").is_valid());
    CHECK(!meta.method("x_t(x_t&&)").is_valid());

    x_t original;
    original.a = 11;
    original.b = 22;
    // An ALIAS argument keeps the caller's object as the source (std::ref records LVALUE_REF, which is
    // what makes the copy constructor the exact match over the move one).
    const any_t copy_args[] = { any_t(std::ref(original)) };
    x_t* copied = nullptr;
    any_t copied_out(std::ref(copied));
    CHECK(meta.new_instance(copy_args, copied_out));
    CHECK(copied != nullptr);
    if (copied != nullptr) {
        CHECK(copied != &original);
        CHECK(copied->a == 11);
        CHECK(copied->b == 22);
        CHECK(original.a == 11);                   // a copy leaves the source alone
        CHECK(original.b == 22);
        delete copied;
    }

    x_t movable;
    movable.a = 33;
    movable.b = 44;
    // An rvalue argument (moved into the any_t, whose recorded cvref is then RVALUE_REF) selects
    // x_t(x_t&&): the exact reference-kind pass prefers it over `const x_t&`.
    const any_t move_args[] = { any_t(std::move(movable)) };
    x_t* moved = nullptr;
    any_t moved_out(std::ref(moved));
    CHECK(meta.new_instance(move_args, moved_out));
    CHECK(moved != nullptr);
    if (moved != nullptr) {
        CHECK(moved->a == 33);
        CHECK(moved->b == 44);
        delete moved;
    }
    CHECK(movable.a == -1);                        // the move constructor really ran
    CHECK(movable.b == -1);

    // -----------------------------------------------------------------------
    // (8) The two handle types and the two index spaces. A constructor handle answers the
    //     constructor questions; the method space cannot name it at all.
    // -----------------------------------------------------------------------
    const meta_constructor_t pair_ctor = meta.constructor(pair_index);
    const meta_method_t ordinary = meta.method("add(int,int) const");
    CHECK(ordinary.is_valid());
    CHECK(ordinary.method_kind() == method_kind_enum::PLAIN_METHOD);
    CHECK(ordinary.access() == method_access_enum::PUBLIC_SCOPE);
    CHECK(pair_ctor.is_valid());
    CHECK(pair_ctor.index() == pair_index);
    CHECK(pair_ctor.owner() == &meta);
    CHECK(pair_ctor.name() == "x_t");
    CHECK(pair_ctor.qualifier() == qualifier_flag::NONE);   // no cvref suffix on a constructor
    CHECK(pair_ctor.access() == method_access_enum::PUBLIC_SCOPE);
    // The structural split: the constructor coordinate is NOT a method index and the signature is
    // not a method key.
    CHECK(meta.index_of_method(pair_ctor.signature()) == index_npos);
    CHECK(!meta.method(pair_ctor.signature()).is_valid());
    CHECK(!meta_constructor_t{}.is_valid());                 // invalid constructor handle
    CHECK(meta_constructor_t{}.access() == method_access_enum::NONE);  // no declaration, no specifier
    CHECK(meta_method_t{}.access() == method_access_enum::NONE);  // no declaration, no specifier

    // -----------------------------------------------------------------------
    // (9) Non-trivial parameters. The constructor is selected by the bare parameter type names, so
    //     BOTH the reference and the by-value constructor are reachable through the argument list —
    //     the old "the typed form decays and misses a reference spelling" limitation went away with
    //     the erased signature overload it belonged to.
    // -----------------------------------------------------------------------
    {
        const meta_object_t& ref_meta = ref_t::static_meta_object;

        // Find the constructor by name instead of hard-coding a spelling: the exact parameter
        // spelling ("const std::basic_string<char>&") is a compiler detail. The copy constructor is
        // in the table now, and its single parameter is spelled "...&" as well, so it has to be
        // skipped explicitly (its parameter names ref_t itself).
        std::string_view ref_signature;
        std::string_view by_value_signature;
        for (index_t i = 0; i < ref_meta.constructor_count(); ++i) {
            const meta_constructor_t ctor = ref_meta.constructor(i);
            if (ctor.name() != "ref_t") {
                continue;                                  // object_t's own constructors
            }
            if (ctor.param_count() == 1
                && ctor.param_type_name(0).find('&') != std::string_view::npos
                && ctor.param_type_name(0).find("ref_t") == std::string_view::npos) {
                ref_signature = ctor.signature();
            }
            else if (ctor.param_count() == 2) {
                by_value_signature = ctor.signature();
            }
        }
        CHECK(!ref_signature.empty());
        CHECK(!by_value_signature.empty());

        const std::string value = "hello";
        const any_t ref_args[] = { any_t(std::cref(value)) };
        ref_t* ref_made = nullptr;
        any_t ref_out(std::ref(ref_made));
        CHECK(ref_meta.new_instance(ref_args, ref_out));
        CHECK(ref_made != nullptr);
        if (ref_made != nullptr) {
            CHECK(ref_made->text == "hello");
            delete ref_made;
        }

        // The forwarding form reaches the reference parameter too: the match is by NAME, and the
        // argument's recorded cvref (LVALUE_REF, const) fits `const std::string&`.
        object_t* typed_ref_made = static_cast<object_t*>(ref_meta.new_instance(value));
        CHECK(typed_ref_made != nullptr);
        if (typed_ref_made != nullptr) {
            CHECK(static_cast<ref_t*>(typed_ref_made)->text == "hello");
            delete typed_ref_made;
        }

        // A by-value parameter matches the same way, in the two-argument overload.
        object_t* typed_ref = static_cast<object_t*>(
            ref_meta.new_instance(std::string("world"), 9));
        CHECK(typed_ref != nullptr);
        if (typed_ref != nullptr) {
            CHECK(static_cast<ref_t*>(typed_ref)->text == "world");
            CHECK(static_cast<ref_t*>(typed_ref)->count == 9);
            delete typed_ref;
        }
    }

    // -----------------------------------------------------------------------
    // (10) Access is only a hint. What cannot be called is decided by the THUNK, not by the
    //      access specifier: a deleted constructor and every constructor of an abstract class stay in
    //      the table without one (is_valid() == false, new_instance() reports a Critical); a private
    //      or a protected constructor has a real thunk and is built like a public one, with access()
    //      reporting the specifier.
    // -----------------------------------------------------------------------
    {
        // (a) Implicitly deleted copy constructor: the class holds a std::unique_ptr.
        const meta_object_t& owning_meta = unique_owning_t::static_meta_object;
        const index_t owning_copy =
            owning_meta.index_of_constructor("unique_owning_t(const unique_owning_t&)");
        CHECK(owning_copy != index_npos);
        CHECK(!owning_meta.constructor(owning_copy).is_valid());
        CHECK(owning_meta.constructor(owning_copy).index() == owning_copy);   // still a row
        CHECK(owning_meta.constructor(owning_copy).param_count() == 1u);
        // It is declared public, so the specifier column still reports PUBLIC_SCOPE: being
        // uncallable is the thunk's verdict, not the access one.
        CHECK(owning_meta.constructor(owning_copy).access() == method_access_enum::PUBLIC_SCOPE);

        unique_owning_t owning_source;
        owning_source.tag = 7;
        const std::size_t before_copy = records().size();
        // The copy constructor IS the exact match for an lvalue argument, so the list resolves to it
        // and the verdict is the entry's null caller: nothing is built.
        const any_t owning_args[] = { any_t(std::ref(owning_source)) };
        unique_owning_t* owning_made = nullptr;
        any_t owning_out(std::ref(owning_made));
        CHECK(!owning_meta.new_instance(owning_args, owning_out));
        CHECK(owning_made == nullptr);
        // Exactly one record: new_instance() decides before it calls the entry's caller, so the
        // thunk's own fallback message never fires a second time.
        CHECK(records().size() == before_copy + 1);
        CHECK(records().back().level == log_level_enum::CRITICAL);
        CHECK(records().back().category == "metaobject.connection");
        CHECK(records().back().message.find("is not callable") != std::string::npos);
        CHECK(records().back().message.find("unique_owning_t(const unique_owning_t&)")
              != std::string::npos);
    }
    {
        // (b) Private constructor: IN the table, VALID, and built through new_instance() like a public
        //     one; access() reports the specifier.
        const meta_object_t& private_meta = private_ctor_t::static_meta_object;
        const index_t private_index = private_meta.index_of_constructor("private_ctor_t(int)");
        CHECK(private_index != index_npos);
        CHECK(private_meta.constructor(private_index).is_valid());
        CHECK(private_meta.constructor(private_index).access() == method_access_enum::PRIVATE_SCOPE);
        CHECK(private_meta.constructor(private_index).name() == "private_ctor_t");

        // Actually construct one through the meta-object and read the value back.
        int value = 9;
        const std::size_t before_private = records().size();
        object_t* made = static_cast<object_t*>(private_meta.new_instance(value));
        CHECK(made != nullptr);
        if (made != nullptr) {
            CHECK(static_cast<private_ctor_t*>(made)->value == 9);
            delete made;
        }
        // Nothing was logged: the call succeeded (no Critical "is not callable" any more).
        CHECK(records().size() == before_private);

        // The factory still works, of course.
        private_ctor_t* created = private_ctor_t::create(9);
        CHECK(created != nullptr);
        if (created != nullptr) {
            CHECK(created->value == 9);
            delete created;
        }

        // N3 blast radius, pinned here instead of left implicit: this class declares
        // `static private_ctor_t* create(int)`, so its METHOD table gained that row when static
        // member functions started being collected. It is a method row of the STATIC kind (a plain
        // function keyed as "create(int)", no receiver, no cvref) — never a constructor row, which is
        // what keeps the two tables apart even for a class whose only factory is static.
        const meta_method_t create_row = private_meta.method("create(int)");
        CHECK(create_row.is_valid());
        CHECK(create_row.method_kind() == method_kind_enum::STATIC_METHOD);
        CHECK(!create_row.has_receiver());
        CHECK(private_meta.index_of_constructor("create(int)") == index_npos);

        // A private METHOD is reachable the same way: the "access is only a hint" contract.
        const meta_method_t secret = private_meta.method("secret(int) const");
        CHECK(secret.is_valid());
        CHECK(!secret.signature().empty());
        CHECK(secret.access() == method_access_enum::PRIVATE_SCOPE);
        CHECK(secret.method_kind() == method_kind_enum::PLAIN_METHOD);
        created = private_ctor_t::create(9);
        CHECK(created != nullptr);
        if (created != nullptr) {
            int out = 0;
            CHECK(secret.invoke(*created, out, 4));
            CHECK(out == 94);                        // 9 * 10 + 4
            CHECK(secret.invoke(*created, 5).is_valid());       // the void form reaches it too
            delete created;
        }
    }
    {
        // (b2) Protected constructor: the same rule, and the only way to an instance here (there is
        //      no public factory).
        const meta_object_t& protected_meta = protected_ctor_t::static_meta_object;
        const index_t protected_index =
            protected_meta.index_of_constructor("protected_ctor_t(int)");
        CHECK(protected_index != index_npos);
        CHECK(protected_meta.constructor(protected_index).is_valid());
        CHECK(protected_meta.constructor(protected_index).access()
              == method_access_enum::PROTECTED_SCOPE);

        int value = 12;
        const std::size_t before_protected = records().size();
        object_t* made = static_cast<object_t*>(protected_meta.new_instance(value));
        CHECK(made != nullptr);
        if (made != nullptr) {
            CHECK(static_cast<protected_ctor_t*>(made)->value == 12);
            delete made;
        }
        CHECK(records().size() == before_protected);
    }
    {
        // (c) Abstract class: it declares its constructors, none of them can be called.
        const meta_object_t& abstract_meta = abstract_ctor_t::static_meta_object;
        CHECK(abstract_meta.index_of_constructor("abstract_ctor_t()") != index_npos);
        CHECK(abstract_meta.constructor_count() >= 1);
        CHECK(!abstract_meta.constructor(abstract_meta.index_of_constructor("abstract_ctor_t()"))
                   .is_valid());
        CHECK(abstract_meta.constructor(abstract_meta.index_of_constructor("abstract_ctor_t()"))
                  .access() == method_access_enum::PUBLIC_SCOPE);

        const std::size_t before_abstract = records().size();
        CHECK(abstract_meta.new_instance() == nullptr);
        CHECK(records().size() == before_abstract + 1);
        CHECK(records().back().level == log_level_enum::CRITICAL);
        CHECK(records().back().message.find("is not callable") != std::string::npos);
        CHECK(records().back().message.find("abstract_ctor_t()") != std::string::npos);
    }

    // An invalid handle has no declared access at all.
    CHECK(meta_method_t{}.access() == method_access_enum::NONE);

    install_log_handler({ });
    std::printf(g_failures == 0 ? "  constructors: all checks passed\n"
                                : "  constructors: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
