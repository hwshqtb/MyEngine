// Qt 6.8.3 port: tst_qsignalblocker
// (qtbase/tests/auto/corelib/kernel/qsignalblocker/tst_qsignalblocker.cpp).
//
// QSignalBlocker is ported as hwshqtb::metaobject::signal_blocker_t. All three Qt test functions are
// ported in full: signalBlocking(), moveAssignment() and dismiss(). QVERIFY/QCOMPARE become CHECK.
//
// Qt's QObject::blockSignals() is a boolean, while object_t::block_signals() maintains a nesting
// counter; signal_blocker_t tracks whether *it* owns one level (_blocked), which is what makes the
// move-assignment cases below counter-correct. See signal_blocker_t.h for the exact sequence.

#include <meta>
#include <cstdio>
#include <utility>

#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/signal_blocker_t.h>

using namespace hwshqtb::metaobject;

namespace qsignalblocker_test {

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

// ported from tst_qsignalblocker.cpp: signalBlocking()
void signal_blocking() {
    object_t o;

    CHECK(!o.signals_blocked());

    {
        signal_blocker_t blocker(&o);
        CHECK(o.signals_blocked());

        o.block_signals(false);
        CHECK(!o.signals_blocked());

        o.block_signals(true);
        CHECK(o.signals_blocked());

        blocker.unblock();
        CHECK(!o.signals_blocked());

        blocker.reblock();
        CHECK(o.signals_blocked());
    }

    CHECK(!o.signals_blocked());
}

// ported from tst_qsignalblocker.cpp: moveAssignment()
void move_assignment() {
    object_t o1, o2;

    // move-assignment: both block other objects
    {
        signal_blocker_t b(&o1);
        CHECK(o1.signals_blocked());

        CHECK(!o2.signals_blocked());
        b = signal_blocker_t(&o2);
        CHECK(!o1.signals_blocked());
        CHECK(o2.signals_blocked());
    }

    CHECK(!o1.signals_blocked());
    CHECK(!o2.signals_blocked());

    // move-assignment: from inert other
    {
        signal_blocker_t b(&o1);
        CHECK(o1.signals_blocked());
        b = signal_blocker_t(nullptr);
    }

    CHECK(!o1.signals_blocked());
    CHECK(!o2.signals_blocked());

    // move-assignment: to inert *this
    {
        signal_blocker_t b(nullptr);
        CHECK(!o1.signals_blocked());
        {
            signal_blocker_t inner(&o1);
            CHECK(o1.signals_blocked());
            b = std::move(inner);
        }
        CHECK(o1.signals_blocked());
    }

    CHECK(!o1.signals_blocked());
    CHECK(!o2.signals_blocked());

    // move-assignment: both block the same object, neither is unblocked
    {
        signal_blocker_t b(&o1);
        CHECK(o1.signals_blocked());
        {
            b.unblock(); // make sure b._blocked = false, so the assignment below releases nothing
            CHECK(!o1.signals_blocked());
            signal_blocker_t inner(&o1);
            CHECK(o1.signals_blocked());
            b.reblock();
            CHECK(o1.signals_blocked());
            b = std::move(inner);
        }
        CHECK(o1.signals_blocked());
    }

    CHECK(!o1.signals_blocked());
    CHECK(!o2.signals_blocked());

    // move-assignment: both block the same object, but *this is unblocked
    {
        signal_blocker_t b(&o1);
        CHECK(o1.signals_blocked());
        b.unblock();
        CHECK(!o1.signals_blocked());
        b = signal_blocker_t(&o1);
        CHECK(o1.signals_blocked());
    }

    CHECK(!o1.signals_blocked());
    CHECK(!o2.signals_blocked());

    // move-assignment: both block the same object, but other is unblocked
    {
        signal_blocker_t b(&o1);
        {
            CHECK(o1.signals_blocked());
            signal_blocker_t inner(&o1);
            CHECK(o1.signals_blocked());
            inner.unblock();
            CHECK(o1.signals_blocked());
            b = std::move(inner);
            CHECK(!o1.signals_blocked());
        }
        CHECK(!o1.signals_blocked());
    }

    CHECK(!o1.signals_blocked());
    CHECK(!o2.signals_blocked());
}

// ported from tst_qsignalblocker.cpp: dismiss()
void dismiss() {
    object_t obj;
    {
        signal_blocker_t blocker(&obj);
        blocker.dismiss();
    }
    CHECK(obj.signals_blocked());
}

}  // namespace qsignalblocker_test

using namespace qsignalblocker_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    std::printf("== Qt port: qsignalblocker ==\n");

    signal_blocking();
    move_assignment();
    dismiss();

    if (g_failures == 0) {
        std::printf("  qsignalblocker: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("  qsignalblocker: %d of %d check(s) failed\n", g_failures, g_checks);
    return 1;
}
