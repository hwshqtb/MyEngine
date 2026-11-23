// Qt 6.8.3 port: tst_qmetaenum
// (qtbase/tests/auto/corelib/kernel/qmetaenum/tst_qmetaenum.cpp).
//
// QMetaEnum is ported as hwshqtb::metaobject::meta_enum_t. Qt obtains the handle from a type with
// QMetaEnum::fromType<E>(); here the class-scope enum is named explicitly:
//     meta_enum_t::from_type<tst_meta_enum_t>("SuperEnum")
// (a type may declare several enums, so the name is part of the API).
//
// QVERIFY/QCOMPARE become CHECK. The data-driven valuesToKeys() is expanded into one block per Qt
// data row. The Qt test functions are fromType(), valuesToKeys_data() / valuesToKeys() and
// defaultConstructed(); the file-scope IsQEnumHelper static_asserts have no library analogue.
//
// Three points are deliberately NOT ported — the project owner ruled they are not differences:
//   * fromType(): !isFlag() — is_flag is derived strictly from the enumerator values; declaring a
//     flag is deliberately not supported (it would let an enum that cannot be used as a flag be used
//     as one).
//   * fromType(): enumName() / enclosingMetaObject() — not public API in this library.
//   * fromType(): the Flags handle — Q_DECLARE_FLAGS alias follows from the is_flag ruling.

#include <meta>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>

#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_enum_t.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::metaobject;

namespace qmetaenum_test {

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

// tst_QMetaEnum's class (a QObject subclass in Qt). OBJECT declares the static meta-object that
// meta_enum_t::from_type<T>() reads; the class is never instantiated, so no object/app is needed.
struct tst_meta_enum_t : object_t {
    OBJECT
public:
    enum SuperEnum { SuperValue1 = 1, SuperValue2 = INT_MIN };
    enum Flag { Flag1 = 1, Flag2 = INT_MIN };
    // Q_DECLARE_FLAGS(Flags, Flag) / Q_FLAG(Flags) have no library equivalent: flag_t<Flag> is the
    // value layer, and no alias enum is registered in the meta-object (see from_type below).

    // Stand-in for Qt::WindowFlags, the flag enum the valuesToKeys() rows are written against.
    // Qt::WindowFlags is declared in the Qt namespace (Q_FLAG_NS); the library covers only enums
    // declared inside a type, so the rows are reproduced with the same enumerator names and values.
    // Only the enumerators the rows exercise are declared; the values are Qt's.
    enum WindowType {
        Window = 0x00000001,
        Dialog = 0x00000002 | Window,
        FramelessWindowHint = 0x00000800,
        Tool = 0x0000000b, // Qt: Popup | Dialog, i.e. 0x00000009 | 0x00000003
        WindowMinimizeButtonHint = 0x00008000,
        WindowMaximizeButtonHint = 0x00010000,
        WindowMinMaxButtonsHint = WindowMinimizeButtonHint | WindowMaximizeButtonHint,
        WindowStaysOnTopHint = 0x00040000,
    };
};
META_OBJECT(tst_meta_enum_t);

// QMetaEnum::keysToValue(keys, &ok) shape: -1 with ok == false on a miss.
std::int64_t keys_to_value(const meta_enum_t& me, std::string_view keys, bool& ok) {
    if (const std::optional<std::int64_t> value = me.keys_to_value(keys)) {
        ok = true;
        return *value;
    }
    ok = false;
    return -1;
}

// ported from tst_qmetaenum.cpp: fromType()
void from_type() {
    meta_enum_t meta = meta_enum_t::from_type<tst_meta_enum_t>("SuperEnum");
    CHECK(meta.is_valid());
    // Not ported — not a difference: this library derives is_flag strictly from the enumerator
    // values (pairwise disjoint under &). Declaring a flag is deliberately not supported: it would
    // let an enum that cannot be used as a flag be used as one.
    CHECK(meta.name() == "SuperEnum");
    // Not ported — not a difference: enum_name() is not public API in this library; name() above is
    // the only enum name the handle exposes.
    // Not ported — not a difference: enclosing_meta_object() is not public API in this library.
    CHECK(meta.key_count() == 2);

    // Not ported — not a difference: the Q_DECLARE_FLAGS(Flags, Flag) alias follows from the
    // is_flag ruling (flag-ness is a value property, not a declaration); there is no "Flags" entry
    // to look up, and its isFlag/name/enumName/enclosingMetaObject/keyCount checks do not apply.
}

// ported from tst_qmetaenum.cpp: valuesToKeys_data() + valuesToKeys()
void values_to_keys() {
    // ---- Row "Window": QMetaEnum::fromType<Qt::WindowFlags>(), Qt::Window, "Window" ----
    {
        const meta_enum_t me = meta_enum_t::from_type<tst_meta_enum_t>("WindowType");
        const std::uint64_t flags = static_cast<std::uint64_t>(tst_meta_enum_t::Window);
        CHECK(me.value_to_keys(static_cast<std::int64_t>(flags)) == "Window");
        bool ok = false;
        const std::int64_t value = keys_to_value(me, "Window", ok);
        CHECK(static_cast<std::uint32_t>(value) == flags);
        CHECK(ok);
    }

    // ---- Row "Frameless_Dialog": Qt::Dialog | Qt::FramelessWindowHint ----
    {
        const meta_enum_t me = meta_enum_t::from_type<tst_meta_enum_t>("WindowType");
        const std::uint64_t flags =
            static_cast<std::uint64_t>(tst_meta_enum_t::Dialog)
            | static_cast<std::uint64_t>(tst_meta_enum_t::FramelessWindowHint);
        CHECK(me.value_to_keys(static_cast<std::int64_t>(flags)) == "Dialog|FramelessWindowHint");
        bool ok = false;
        const std::int64_t value = keys_to_value(me, "Dialog|FramelessWindowHint", ok);
        CHECK(static_cast<std::uint32_t>(value) == flags);
        CHECK(ok);
    }

    // ---- Row "Tool_MinMax_StaysOnTop": Qt::Tool | Qt::WindowMinMaxButtonsHint |
    //      Qt::WindowStaysOnTopHint ----
    {
        const meta_enum_t me = meta_enum_t::from_type<tst_meta_enum_t>("WindowType");
        const std::uint64_t flags =
            static_cast<std::uint64_t>(tst_meta_enum_t::Tool)
            | static_cast<std::uint64_t>(tst_meta_enum_t::WindowMinMaxButtonsHint)
            | static_cast<std::uint64_t>(tst_meta_enum_t::WindowStaysOnTopHint);
        CHECK(me.value_to_keys(static_cast<std::int64_t>(flags))
              == "Tool|WindowMinMaxButtonsHint|WindowStaysOnTopHint");
        bool ok = false;
        const std::int64_t value = keys_to_value(me, "Tool|WindowMinMaxButtonsHint|WindowStaysOnTopHint", ok);
        CHECK(static_cast<std::uint32_t>(value) == flags);
        CHECK(ok);
    }

    // ---- Row "INT_MIN": QMetaEnum::fromType<Flags>(), quint64(uint(Flag2)), "Flag2" ----
    {
        const meta_enum_t me = meta_enum_t::from_type<tst_meta_enum_t>("Flag");
        const std::uint64_t flags =
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(tst_meta_enum_t::Flag2));
        // value_to_keys() compares at the enum's underlying width (32 bits here), so the positive
        // 0x80000000 bit pattern matches the sign-extended INT_MIN enumerator.
        CHECK(me.value_to_keys(static_cast<std::int64_t>(flags)) == "Flag2");
        bool ok = false;
        const std::int64_t value = keys_to_value(me, "Flag2", ok);
        CHECK(static_cast<std::uint32_t>(value) == flags);
        CHECK(ok);
    }
}

// ported from tst_qmetaenum.cpp: defaultConstructed()
void default_constructed() {
    const meta_enum_t e;
    CHECK(!e.is_valid());
    CHECK(!e.is_scoped());
    CHECK(!e.is_flag());
    CHECK(e.name().empty());
}

// Not ported — no library analogue: the six file-scope QtPrivate::IsQEnumHelper<> static_asserts
// (tst_qmetaenum.cpp:99-105). The library exposes no public "is this type registered as an enum?"
// trait, and Qt::WindowFlags / Qt::Orientation / QObject do not exist in this port.
// (Q_DECLARE_METATYPE(Qt::WindowFlags) at line 46 is likewise a Qt type-system registration with no
// counterpart needed here: this library produces a type's interface on demand.)

}  // namespace qmetaenum_test

using namespace qmetaenum_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    std::printf("== Qt port: qmetaenum ==\n");

    from_type();
    values_to_keys();
    default_constructed();

    if (g_failures == 0) {
        std::printf("  qmetaenum: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("  qmetaenum: %d of %d check(s) failed\n", g_failures, g_checks);
    return 1;
}
