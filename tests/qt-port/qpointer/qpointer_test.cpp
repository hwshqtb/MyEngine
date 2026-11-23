// Qt 6.8.3 port: tst_qpointer
// (qtbase/tests/auto/corelib/kernel/qpointer/tst_qpointer.cpp).
//
// QPointer is ported as hwshqtb::metaobject::object_pointer_t, QObject as object_t. The guard does not
// go through the destroyed() signal at all: each object_t owns a detail::death_watch_t (a shared_ptr)
// and object_pointer_t holds a weak_ptr to it plus the raw pointer; ~object_t marks the watch dead
// before it does anything else, so the pointer turns null for every guard exactly once.
//
// QVERIFY/QCOMPARE become CHECK. The Qt file has 16 test functions; all 16 are ported here.
//
// Spellings adapted to the library (nothing is asserted differently):
//   * QPointer::get() and static_cast<T*>(p) -> p.data(): object_pointer_t has no implicit conversion to
//     T*, so a bare pointer value is read through the documented accessor. Comparisons are spelled as
//     in Qt: operator== / operator!= exist on object_pointer_t (operator!= is synthesised by C++20
//     rewriting), so QCOMPARE(p, QPointer(q)) / QCOMPARE(p, raw) / QVERIFY(p == 0) are ported as-is.
//   * QPointer::isNull() -> !p: operator bool is documented as "the target is alive", the exact
//     inverse predicate. object_pointer_t deliberately has no isNull() name.
//   * QFile -> file_t below: like QFile, just a concrete object_t subclass; QFile itself is a Qt class
//     with no counterpart.
//   * QObject's parent-child links: Qt takes the parent in the constructor, here it is set_parent().
//   * qobject_cast<T*>(p) -> dynamic_cast<T*>(p.data()): qobject_cast is Qt's RTTI cast; dynamic_cast
//     is the C++ equivalent (the checks it appears in only assert that casting the guard yields null /
//     the parent while the object is being destroyed).
//   * QTestPrivate::testEqualityOperatorsCompile<>() and QT_TEST_EQUALITY_OPS -> the two local helpers
//     below, which perform the same checks: every cvref combination at compile time, both operand
//     orders and both operators at run time (Qt's helpers are private testlib details).
//   * QVariant::fromValue(QPointer) -> any_t(tracking); qPointerFromVariant<T>(v) ->
//     pointer_from_any<T>(v). The any_t stores the guard under the single object_pointer_t<object_t>
//     entry, so (as in Qt, where QVariant needs QPointer<T> to be metatype-known) the derived type
//     still needs no registration of its own.
//   * QFile/QIODevice -> file_t/device_t below: the same two levels of the QObject hierarchy, so the
//     three successful recoveries in qvariantCast() still go through exact / intermediate / base.
//   * QThread/QThreadPool/QRunnable -> thread_t/thread_pool_t/runnable_t below. Qt's test asserts
//     nothing: its point is that the guards are created on pool threads while the guarded objects are
//     deleted on the owner thread, so two checks are added to keep it from passing vacuously: all 600
//     deferred deletes really run, and they run on the owner thread.

#include <meta>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <utility>

#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/object_pointer_t.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/runnable_t.h>
#include <hwshqtb/metaobject/thread_pool_t.h>
#include <hwshqtb/metaobject/thread_t.h>

using namespace hwshqtb::metaobject;

namespace qpointer_test {

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

/// Poll @p predicate until it holds or the timeout expires.
template <typename predicate_t>
bool wait_until(predicate_t predicate, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

// Stand-ins for Qt's QFile and its QIODevice base: the test only needs a concrete QObject subclass and
// a second level of the hierarchy (qvariantCast() recovers the guard as base / intermediate / exact).
struct device_t : object_t { };
struct file_t : device_t { };

// tst_QPointer's own class (Qt: `class tst_QPointer : public QObject`). The ported functions only
// need its identity and me().
struct tst_qpointer : object_t {
    tst_qpointer* me() const {
        return const_cast<tst_qpointer*>(this);
    }
};

// ---- threadSafety() support ------------------------------------------------

// The guarded objects: count their destruction and record the thread it happened on. Qt's test uses
// plain QObjects; the counter is what makes the two failure modes (deletes never dispatched / deletes
// dispatched on a worker) observable instead of vacuous.
struct counted_object_t : object_t {
    ~counted_object_t() override;
};

std::atomic<int> g_destroyed{ 0 };
std::atomic<bool> g_wrong_thread{ false };
std::atomic<std::thread::id> g_owner_thread{ };

counted_object_t::~counted_object_t() {
    ++g_destroyed;
    if (std::this_thread::get_id() != g_owner_thread.load()) {
        g_wrong_thread = true;
    }
}

// The owner thread of the Qt test: records its own id before running its event loop.
struct owner_thread_t : thread_t {
    std::atomic<bool> id_recorded{ false };
    std::thread::id thread_id{};

    void run() override {
        thread_id = std::this_thread::get_id();
        id_recorded.store(true, std::memory_order_release);
        thread_t::run();
    }
};

// Qt's TestRunnable: an object_t (so it has an affinity) that is also a runnable_t. The pool deletes
// it on a worker thread after run(), because auto_delete() defaults to true.
class test_runnable_t: public object_t, public runnable_t {
    OBJECT

public:
    void run() override;
};
META_OBJECT(test_runnable_t);

void test_runnable_t::run() {
    object_pointer_t<object_t> obj1 = new counted_object_t;
    object_pointer_t<object_t> obj2 = new counted_object_t;
    obj1->move_to_thread(thread()); // the runnable's owner thread, not the worker's
    obj1->delete_later();
    obj2->move_to_thread(thread());
    obj2->delete_later();
}

// QTestPrivate::testEqualityOperatorsCompile<LeftType, RightType>()
// (testlib/qcomparisontesthelper_p.h): every cvref combination of both operands, in both orders and
// for both operators, must be well-formed and yield bool.
template <typename left_t, typename right_t = left_t>
void test_equality_operators_compile() {
#define QP_CHECK_EQUALITY_OPERATOR(Left, Right)                                                    \
    do {                                                                                           \
        static_assert(std::is_convertible_v<                                                       \
                      decltype(std::declval<Left>() == std::declval<Right>()), bool>);             \
        static_assert(std::is_convertible_v<                                                       \
                      decltype(std::declval<Left>() != std::declval<Right>()), bool>);             \
        if constexpr (!std::is_same_v<Left, Right>) {                                              \
            static_assert(std::is_convertible_v<                                                   \
                          decltype(std::declval<Right>() == std::declval<Left>()), bool>);         \
            static_assert(std::is_convertible_v<                                                   \
                          decltype(std::declval<Right>() != std::declval<Left>()), bool>);         \
        }                                                                                          \
    } while (false)

#define QP_FOR_EACH_CVREF(Func, Left, Right)                                                       \
    Func(Left&, Right&);                                                                           \
    Func(Left&, const Right&);                                                                     \
    Func(Left&, Right&&);                                                                          \
    Func(Left&, const Right&&);                                                                    \
    Func(const Left&, Right&);                                                                     \
    Func(const Left&, const Right&);                                                               \
    Func(const Left&, Right&&);                                                                    \
    Func(const Left&, const Right&&);                                                              \
    Func(Left&&, Right&);                                                                          \
    Func(Left&&, const Right&);                                                                    \
    Func(Left&&, Right&&);                                                                         \
    Func(Left&&, const Right&&);                                                                   \
    Func(const Left&&, Right&);                                                                    \
    Func(const Left&&, const Right&);                                                              \
    Func(const Left&&, Right&&);                                                                   \
    Func(const Left&&, const Right&&)

    QP_FOR_EACH_CVREF(QP_CHECK_EQUALITY_OPERATOR, left_t, right_t);

#undef QP_FOR_EACH_CVREF
#undef QP_CHECK_EQUALITY_OPERATOR
}

// QTestPrivate::testEqualityOperators(lhs, rhs, expectedEqual): both operand orders and both
// operators, each in the four value-category forms of Qt's CHECK_RUNTIME_LR. (The constness matrix is
// covered at compile time by test_equality_operators_compile() above.) The operators take their
// operands by value or by const reference, so std::move() below is only a value-category cast.
template <typename left_t, typename right_t>
void test_equality_operators(left_t lhs, right_t rhs, bool expected_equal) {
    CHECK((lhs == rhs) == expected_equal);
    CHECK((std::move(lhs) == rhs) == expected_equal);
    CHECK((lhs == std::move(rhs)) == expected_equal);
    CHECK((std::move(lhs) == std::move(rhs)) == expected_equal);

    CHECK((lhs != rhs) == !expected_equal);
    CHECK((std::move(lhs) != rhs) == !expected_equal);
    CHECK((lhs != std::move(rhs)) == !expected_equal);
    CHECK((std::move(lhs) != std::move(rhs)) == !expected_equal);

    CHECK((rhs == lhs) == expected_equal);
    CHECK((std::move(rhs) == lhs) == expected_equal);
    CHECK((rhs == std::move(lhs)) == expected_equal);
    CHECK((std::move(rhs) == std::move(lhs)) == expected_equal);

    CHECK((rhs != lhs) == !expected_equal);
    CHECK((std::move(rhs) != lhs) == !expected_equal);
    CHECK((rhs != std::move(lhs)) == !expected_equal);
    CHECK((std::move(rhs) != std::move(lhs)) == !expected_equal);
}

// ChildObject in the Qt test: guards its parent and checks the guard from its destructor, i.e. while
// the parent's destructor is deleting its children.
struct child_object : object_t {
    object_pointer_t<object_t> guarded_pointer;

    explicit child_object(object_t* parent):
        guarded_pointer(parent) {
        set_parent(parent);
    }

    ~child_object() override;
};

child_object::~child_object() {
    // Qt: QCOMPARE(static_cast<QObject *>(guardedPointer), static_cast<QObject *>(0));
    // The parent's ~object_t has already marked the watch dead before deleting its children, so the
    // guard is null here.
    CHECK(guarded_pointer == nullptr);
    // Qt: QCOMPARE(qobject_cast<QObject *>(guardedPointer), static_cast<QObject *>(0));
    CHECK(dynamic_cast<object_t*>(guarded_pointer.data()) == nullptr);
}

class derived_child;

// DerivedParent / DerivedChild in the Qt test: here the parent's own destructor deletes the child,
// so the child's destructor runs *before* ~object_t of the parent has marked the watch dead — the
// guard must still point at the parent.
class derived_parent : public object_t {
    derived_child* child_ = nullptr;

public:
    derived_parent();
    ~derived_parent() override;
};

class derived_child : public object_t {
    derived_parent* parent_pointer_ = nullptr;
    object_pointer_t<derived_parent> guarded_parent_pointer_;

public:
    explicit derived_child(derived_parent* parent):
        parent_pointer_(parent),
        guarded_parent_pointer_(parent) {
        set_parent(parent);
    }

    ~derived_child() override;
};

derived_parent::derived_parent() {
    child_ = new derived_child(this);
}

derived_parent::~derived_parent() {
    delete child_;
}

derived_child::~derived_child() {
    // Qt: QCOMPARE(static_cast<DerivedParent *>(guardedParentPointer), parentPointer);
    CHECK(guarded_parent_pointer_ == parent_pointer_);
    // Qt: QCOMPARE(qobject_cast<DerivedParent *>(guardedParentPointer), parentPointer);
    CHECK(dynamic_cast<derived_parent*>(guarded_parent_pointer_.data()) == parent_pointer_);
}

// ported from tst_qpointer.cpp: constructors()
void constructors() {
    struct derived_t : object_t { };
    derived_t derived;
    tst_qpointer self;

    object_pointer_t<object_t> p1;
    object_pointer_t<object_t> p2(&self);
    object_pointer_t<object_t> p3(p2);
    object_pointer_t<object_t> p4 = &derived;
    CHECK(p1 == object_pointer_t<object_t>());
    CHECK(p2 == object_pointer_t<object_t>(&self));
    CHECK(p3 == object_pointer_t<object_t>(&self));
    CHECK(p4 == &derived);
}

// ported from tst_qpointer.cpp: ctad()
void ctad() {
    {
        object_t o;
        [[maybe_unused]] object_pointer_t po = &o;
        static_assert(std::is_same_v<decltype(po), object_pointer_t<object_t>>);
        [[maybe_unused]] object_pointer_t poc = po;
        static_assert(std::is_same_v<decltype(poc), object_pointer_t<object_t>>);
        [[maybe_unused]] object_pointer_t pom = std::move(po);
        static_assert(std::is_same_v<decltype(pom), object_pointer_t<object_t>>);
    }
    {
        const object_t co;
        [[maybe_unused]] object_pointer_t pco = &co;
        static_assert(std::is_same_v<decltype(pco), object_pointer_t<const object_t>>);
        [[maybe_unused]] object_pointer_t pcoc = pco;
        static_assert(std::is_same_v<decltype(pcoc), object_pointer_t<const object_t>>);
        [[maybe_unused]] object_pointer_t pcom = std::move(pco);
        static_assert(std::is_same_v<decltype(pcom), object_pointer_t<const object_t>>);
    }
    {
        file_t f;
        [[maybe_unused]] object_pointer_t pf = &f;
        static_assert(std::is_same_v<decltype(pf), object_pointer_t<file_t>>);
        [[maybe_unused]] object_pointer_t pfc = pf;
        static_assert(std::is_same_v<decltype(pfc), object_pointer_t<file_t>>);
        [[maybe_unused]] object_pointer_t pfm = std::move(pf);
        static_assert(std::is_same_v<decltype(pfm), object_pointer_t<file_t>>);
    }
    {
        const file_t cf;
        [[maybe_unused]] object_pointer_t pcf = &cf;
        static_assert(std::is_same_v<decltype(pcf), object_pointer_t<const file_t>>);
        [[maybe_unused]] object_pointer_t pcfc = pcf;
        static_assert(std::is_same_v<decltype(pcfc), object_pointer_t<const file_t>>);
        [[maybe_unused]] object_pointer_t pcfm = std::move(pcf);
        static_assert(std::is_same_v<decltype(pcfm), object_pointer_t<const file_t>>);
    }
}

// ported from tst_qpointer.cpp: conversion()
void conversion() {
    // copy-conversion:
    {
        file_t file;
        object_pointer_t<file_t> pf = &file;
        CHECK(pf == &file);
        object_pointer_t<const object_t> pio = pf;
        CHECK(pio == &file);
        CHECK(pio.data() == &file);
        CHECK(pio == pf);
        CHECK(pio.data() == pf.data());

        // reset
        pio = nullptr;
        CHECK(pio == nullptr);
        CHECK(pio.data() == nullptr);

        // copy-assignment
        CHECK(pf == &file);
        pio = pf;
        CHECK(pio == &file);
        CHECK(pio.data() == &file);
        CHECK(pio == pf);
        CHECK(pio.data() == pf.data());
    }
    // move-conversion:
    {
        file_t file;
        object_pointer_t<file_t> pf = &file;
        CHECK(pf == &file);
        object_pointer_t<const object_t> pio = std::move(pf);
        CHECK(pf == nullptr);
        CHECK(pio == &file);
        CHECK(pio.data() == &file);

        // reset
        pio = nullptr;
        CHECK(pio == nullptr);
        CHECK(pio.data() == nullptr);

        // move-assignment
        pio = object_pointer_t<file_t>(&file);
        CHECK(pio == &file);
        CHECK(pio.data() == &file);
    }
}

// ported from tst_qpointer.cpp: destructor()
void destructor() {
    // Make two guards to the same object.
    object_t* object = new object_t;
    object_pointer_t<object_t> p1 = object;
    object_pointer_t<object_t> p2 = object;
    // Check that they point to the correct object.
    CHECK(p1 == object_pointer_t<object_t>(object));
    CHECK(p2 == object_pointer_t<object_t>(object));
    CHECK(p1 == p2);
    // Destroy the guarded object.
    delete object;
    // Check that both pointers were zeroed.
    CHECK(p1 == object_pointer_t<object_t>());
    CHECK(p2 == object_pointer_t<object_t>());
    CHECK(p1 == p2);
}

// ported from tst_qpointer.cpp: assignment_operators()
void assignment_operators() {
    tst_qpointer self;
    object_pointer_t<object_t> p1;
    object_pointer_t<object_t> p2;

    // Test assignment with an object_t-derived object pointer.
    p1 = &self;
    p2 = p1;
    CHECK(p1 == object_pointer_t<object_t>(&self));
    CHECK(p2 == object_pointer_t<object_t>(&self));
    CHECK(p1 == object_pointer_t<object_t>(p2));

    // Test assignment with a null pointer.
    p1 = nullptr;
    p2 = p1;
    CHECK(p1 == object_pointer_t<object_t>());
    CHECK(p2 == object_pointer_t<object_t>());
    CHECK(p1 == object_pointer_t<object_t>(p2));

    // Test assignment with a real object_t pointer.
    object_t* object = new object_t;

    p1 = object;
    p2 = p1;
    CHECK(p1 == object_pointer_t<object_t>(object));
    CHECK(p2 == object_pointer_t<object_t>(object));
    CHECK(p1 == object_pointer_t<object_t>(p2));

    // Test assignment with the same pointer that's already guarded.
    p1 = object;
    p2 = p1;
    CHECK(p1 == object_pointer_t<object_t>(object));
    CHECK(p2 == object_pointer_t<object_t>(object));
    CHECK(p1 == object_pointer_t<object_t>(p2));

    // Cleanup.
    delete object;
}

// ported from tst_qpointer.cpp: compareCompiles()
void compare_compiles() {
    test_equality_operators_compile<object_pointer_t<object_t>>();
    test_equality_operators_compile<object_pointer_t<object_t>, object_t*>();
    // Qt compares against QWidget*, a QObject-derived class; file_t is the object_t-derived stand-in.
    test_equality_operators_compile<object_pointer_t<object_t>, file_t*>();
    test_equality_operators_compile<object_pointer_t<object_t>, object_pointer_t<file_t>>();
    test_equality_operators_compile<object_pointer_t<object_t>, std::nullptr_t>();
}

// ported from tst_qpointer.cpp: equality_operators()
void equality_operators() {
    tst_qpointer self;
    object_pointer_t<object_t> p1;
    object_pointer_t<object_t> p2;

    test_equality_operators(p1, p2, true);

    object_t* object = nullptr;
    file_t* file = nullptr; // Qt: QWidget *widget = nullptr;

    p1 = object;
    test_equality_operators(p1, p2, true);
    test_equality_operators(p1, object, true);
    p2 = object;
    test_equality_operators(p2, p1, true);
    test_equality_operators(p2, object, true);
    p1 = &self;
    test_equality_operators(p1, p2, false);
    p2 = p1;
    test_equality_operators(p1, p2, true);

    // compare to zero
    p1 = nullptr;
    CHECK(p1 == 0);
    CHECK(0 == p1);
    CHECK(p2 != 0);
    CHECK(0 != p2);
    test_equality_operators(p1, nullptr, true);
    test_equality_operators(p2, nullptr, false);
    test_equality_operators(p1, object, true);
    test_equality_operators(p2, object, false);
    test_equality_operators(p1, file, true);
    test_equality_operators(p2, file, false);
}

// ported from tst_qpointer.cpp: swap()
void swap() {
    object_pointer_t<object_t> c1, c2;
    {
        object_t o;
        c1 = &o;
        CHECK(!c2); // Qt: QVERIFY(c2.isNull())
        CHECK(c1.data() == &o);
        c1.swap(c2);
        CHECK(!c1); // Qt: QVERIFY(c1.isNull())
        CHECK(c2.data() == &o);
    }
    CHECK(!c1); // Qt: QVERIFY(c1.isNull())
    CHECK(!c2); // Qt: QVERIFY(c2.isNull())
}

// ported from tst_qpointer.cpp: isNull()
void is_null() {
    tst_qpointer self;
    object_pointer_t<object_t> p1;
    // QVERIFY(p1.isNull()): object_pointer_t has no isNull(); operator bool is documented as the
    // "target still alive" predicate, so its negation is the same test.
    CHECK(!p1);
    p1 = &self;
    CHECK(static_cast<bool>(p1));
    p1 = nullptr;
    CHECK(!p1);
}

// ported from tst_qpointer.cpp: dereference_operators()
void dereference_operators() {
    tst_qpointer self;
    object_pointer_t<tst_qpointer> p1 = &self;
    object_pointer_t<tst_qpointer> p2;

    // operator->() -- only makes sense if not null.
    object_t* object = p1->me();
    CHECK(object == &self);

    // operator*() -- only makes sense if not null.
    object_t& ref = *p1;
    CHECK(&ref == &self);

    // Qt's operator T*(): object_pointer_t has no conversion to T*; data() is the documented accessor
    // for the same pointer value.
    CHECK(p1.data() == &self);
    CHECK(p2.data() == nullptr);

    // data()
    CHECK(p1.data() == &self);
    CHECK(p2.data() == nullptr);
}

// ported from tst_qpointer.cpp: disconnect()
void disconnect() {
    // Verify that the pointer remains guarded when all signals are disconnected.
    object_pointer_t<object_t> p1 = new object_t;
    CHECK(static_cast<bool>(p1));
    // object_t has no argument-less disconnect() (QObject::disconnect() cuts every connection where
    // the object is the sender). The nearest call — disconnect every connection from this object to
    // itself — is, like Qt's call on this freshly created object, a no-op.
    (void)p1->disconnect(p1.data());
    CHECK(static_cast<bool>(p1));
    delete p1.data();
    CHECK(!p1);
}

// ported from tst_qpointer.cpp: castDuringDestruction()
void cast_during_destruction() {
    {
        object_t* parent_object = new object_t;
        (void)new child_object(parent_object);
        delete parent_object;
    }

    // Not ported — this project has no widget module: the ChildWidget block of the Qt test (a QWidget
    // child whose destructor checks that the guard to its parent widget still points at the parent).
    // QWidget does not exist here; the plain-object block above covers the same destruction-order
    // property.

    {
        delete new derived_parent;
    }
}

// ported from tst_qpointer.cpp: qvariantCast()
void qvariant_cast() {
    file_t file;
    object_pointer_t<file_t> tracking = &file;
    tracking->set_object_name("A test name");
    any_t v(tracking); // Qt: QVariant v = QVariant::fromValue(tracking);

    {
        object_pointer_t<object_t> other = pointer_from_any<object_t>(v);
        // Qt dereferences the recovered pointer directly; the null check keeps a regression from
        // crashing the whole test instead of reporting one failed assertion.
        CHECK(other.data() != nullptr);
        CHECK(other->object_name() == "A test name");
    }
    {
        object_pointer_t<device_t> other = pointer_from_any<device_t>(v); // Qt: QIODevice
        CHECK(other.data() != nullptr);
        CHECK(other->object_name() == "A test name");
    }
    {
        object_pointer_t<file_t> other = pointer_from_any<file_t>(v);
        CHECK(other.data() != nullptr);
        CHECK(other->object_name() == "A test name");
    }
    {
        object_pointer_t<tst_qpointer> other = pointer_from_any<tst_qpointer>(v); // Qt: QThread
        CHECK(!other);
    }
    {
        object_pointer_t<file_t> to_be_deleted = new file_t;
        any_t deleted_variant(to_be_deleted); // Qt: QVariant::fromValue(toBeDeleted)
        delete to_be_deleted.data();
        object_pointer_t<object_t> deleted = pointer_from_any<object_t>(deleted_variant);
        CHECK(!deleted);
    }

    // Qt: "Intentionally does not compile." — QPointer<int> sop = qPointerFromVariant<int>(v);
    // Here that follows from the constraint: pointer_from_any<T> requires T to derive from object_t.
    static_assert(!std::derived_from<int, object_t>);
}

// ported from tst_qpointer.cpp: threadSafety()
void thread_safety() {
    owner_thread_t owner;
    owner.start();
    CHECK(wait_until([&] { return owner.id_recorded.load(std::memory_order_acquire); }));
    g_owner_thread.store(owner.thread_id);
    g_destroyed.store(0);
    g_wrong_thread.store(false);

    thread_pool_t pool;
    pool.set_max_thread_count(4);
    for (int i = 0; i < 300; ++i) {
        object_pointer_t<test_runnable_t> task = new test_runnable_t;
        // Qt: task->setAutoDelete(true) — runnable_t's default, so there is nothing to set.
        task->move_to_thread(&owner);
        pool.start(task.data());
    }
    pool.wait_for_done();

    // Qt's function asserts nothing: it is a stress test of "guards are created on pool threads, the
    // objects die on the owner thread". Two checks keep it from passing vacuously:
    // (1) all 600 delete_later() events really are dispatched by the owner loop;
    CHECK(wait_until([&] { return g_destroyed.load() == 600; }, std::chrono::seconds(10)));
    CHECK(g_destroyed.load() == 600);
    // (2) and they are dispatched ON the owner thread, while the guards live on this thread.
    CHECK(!g_wrong_thread.load());
    std::printf("  threadSafety: %d deferred deletes, on the owner thread: %s\n",
        g_destroyed.load(), g_wrong_thread.load() ? "no" : "yes");

    owner.quit();
    owner.wait();
}

// ported from tst_qpointer.cpp: constPointer()
void const_pointer() {
    // Compile-time test that object_pointer_t<const T> works.
    object_pointer_t<const file_t> fp = new file_t;
    delete fp.data();
}

// ported from tst_qpointer.cpp: constQPointer()
void const_qpointer() {
    // Check that const object_pointer_ts work. It's a bit weird to mark a pointer const if its value
    // can change, but the shallow-const principle in C/C++ allows this, and people use it, so
    // document it with a test.
    tst_qpointer self;
    object_t* o = new object_t;
    o->set_parent(&self); // Qt: QObject *o = new QObject(this);
    const object_pointer_t<object_t> p = o;
    delete o;
    CHECK(!p);
}

// Extension (not a Qt test function): proves that the converting constructor/assignment and swap()
// carry the death-watch registration, not just the raw address — every guard below must null when
// the object it now guards dies.
void guard_registration() {
    // (a) copy-conversion outlives the object.
    {
        file_t* file = new file_t;
        object_pointer_t<file_t> pf = file;
        object_pointer_t<object_t> po = pf;
        object_pointer_t<const object_t> pco = pf;
        CHECK(po.data() == file);
        CHECK(pco.data() == file);
        delete file;
        CHECK(po.data() == nullptr);
        CHECK(pco.data() == nullptr);
        CHECK(pf.data() == nullptr);
        CHECK(po == nullptr);
        CHECK(pco == nullptr);
    }
    // (b) move-conversion clears the source and stays a live guard.
    {
        file_t* file = new file_t;
        object_pointer_t<file_t> pf = file;
        object_pointer_t<object_t> po = std::move(pf);
        CHECK(pf.data() == nullptr);
        CHECK(pf == nullptr);
        CHECK(po.data() == file);
        delete file;
        CHECK(po.data() == nullptr);
    }
    // (c) converting assignment (copy and move) is a live guard.
    {
        file_t* file = new file_t;
        object_pointer_t<file_t> pf = file;
        object_pointer_t<object_t> po;
        po = pf;
        CHECK(po.data() == file);
        delete file;
        CHECK(po.data() == nullptr);
        CHECK(pf.data() == nullptr);
    }
    {
        file_t* file = new file_t;
        object_pointer_t<object_t> po;
        po = object_pointer_t<file_t>(file);
        CHECK(po.data() == file);
        delete file;
        CHECK(po.data() == nullptr);
    }
    // (d) converting an already-dead guard stays null; it must not resurrect the pointer.
    {
        object_t* object = new object_t;
        object_pointer_t<object_t> po = object;
        delete object;
        object_pointer_t<const object_t> dead = po;
        CHECK(dead.data() == nullptr);
        CHECK(dead == nullptr);
    }
    // (e) swap() exchanges the registrations: destroying either object nulls exactly the guard that
    //     holds it afterwards.
    {
        object_t* a = new object_t;
        object_t* b = new object_t;
        object_pointer_t<object_t> pa = a;
        object_pointer_t<object_t> pb = b;
        pa.swap(pb);
        CHECK(pa.data() == b);
        CHECK(pb.data() == a);
        delete a;
        CHECK(pb.data() == nullptr); // pb took over a's registration
        CHECK(pa.data() == b);       // pa still guards the live b
        delete b;
        CHECK(pa.data() == nullptr); // pa took over b's registration
        CHECK(pb.data() == nullptr);
    }
    // (f) comparisons use the guarded pointers, never the death tokens: two guards to the same object
    //     are equal, a guard to one live object never equals a guard to another, and two guards whose
    //     objects are both gone are equal (both are null) even though their tokens differ.
    {
        object_t* a = new object_t;
        object_t* b = new object_t;
        object_pointer_t<object_t> a1 = a;
        object_pointer_t<object_t> a2 = a;
        object_pointer_t<object_t> b1 = b;
        CHECK(a1 == a2);
        CHECK(a1 != b1);
        CHECK(a2 == a);
        delete a;
        delete b;
        CHECK(a1 == a2);
        CHECK(a1 == b1); // both tokens are dead: the comparison sees two null pointers
    }
}

}  // namespace qpointer_test

using namespace qpointer_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    // The object-pointer storage (qvariantCast()) uses the process-wide type registry, so unlike the
    // rest of the port this test now has an application instance, as Qt's QVariant does implicitly.
    core_application app(argc, argv);

    std::printf("== Qt port: qpointer ==\n");

    constructors();
    ctad();
    conversion();
    destructor();
    assignment_operators();
    compare_compiles();
    equality_operators();
    swap();
    is_null();
    dereference_operators();
    disconnect();
    cast_during_destruction();
    qvariant_cast();
    thread_safety();
    const_pointer();
    const_qpointer();
    guard_registration();

    if (g_failures == 0) {
        std::printf("  qpointer: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("  qpointer: %d of %d check(s) failed\n", g_failures, g_checks);
    return 1;
}
