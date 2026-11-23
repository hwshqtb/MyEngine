// Qt 6.8.3 port: tst_qmetaproperty
// (qtbase/tests/auto/corelib/kernel/qmetaproperty/tst_qmetaproperty.cpp).
//
// QMetaProperty is ported as hwshqtb::metaobject::meta_property_t, QObject as object_t, QMetaObject
// as meta_object_t, QMetaObject::indexOfProperty as index_of_property(), QVariant as any_t, QMetaType
// as meta_type_t, Q_GADGET as GADGET, QMetaType::fromName as meta_type_t::resolve_global(),
// QMetaType::registerConverter as register_conversion<From, To>().
//
// QVERIFY/QCOMPARE become CHECK. The Qt file has 10 test functions:
//
//   ported in full:
//     isFinal(), qflags(), gadget(), mapProperty(), notifySignalIndex()
//   ported in part — the missing assertions are ACCEPTED DIFFERENCES (owner-approved; the library's
//   position is stated at each spot):
//     hasStdCppSet(), isConstant(), conversion()
//   not ported — accepted difference (the reason is at the spot where Qt's function would be):
//     readAndWriteWithLazyRegistration()
//   ported in part — one row stays out under ruling 11 (namespace-scope enums are out of scope):
//     enumsFlags()
//
// Spellings adapted to the library (nothing that is asserted changes meaning):
//   * QString -> std::string, QMap<int, int> -> std::map<int, int>.
//   * Qt's MEMBER properties are real data members. A data-member property is writable only when its
//     annotation names a writer (this library does not synthesise a writer for a member, unlike
//     Q_PROPERTY's MEMBER), so the two members the test writes through (value7, custom) name one;
//     `map` stays a plain MEMBER and is read-only, which is all mapProperty() asks.
//   * Qt's property-write failures return false; this library throws bad_any_cast_t (its documented
//     failure channel), so the // QVERIFY(!write(...)) sites assert that exception instead.
//   * Qt's camelCase names -> the library's snake_case (setValue -> set_value, baseSignal ->
//     base_signal, propWithInheritedSig -> prop_with_inherited_sig).

#include <meta>

#include <cstdint>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <hwshqtb/metaobject/annotation.h>
#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/flag_t.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_info_t.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>
#include <hwshqtb/metaobject/meta_property_t.h>
#include <hwshqtb/metaobject/meta_type_t.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/type_interface_t.h>

using namespace hwshqtb::metaobject;

// Qt's QMap<int, int> stand-in. The alias also keeps a comma out of a CHECK macro argument.
using int_map_t = std::map<int, int>;

// Qt's CustomType: constructed from QString and convertible back to it. std::string is the QString
// stand-in, so the two converters registered in main() are custom_type <-> std::string.
struct custom_type {
    int padding = 0;
    std::string str;
    custom_type(const std::string& s = { }):
        str(s) { }
    operator std::string() const {
        return str;
    }
    friend bool operator!=(const custom_type& a, const custom_type& b) {
        return a.str != b.str;
    }
};

// Qt's MyGadget (Q_GADGET): a value type, not an object_t, reached as static_meta_object.
struct my_gadget {
    GADGET

public:
    // Qt: Q_PROPERTY(QString value READ getValue WRITE setValue RESET resetValue)
    // Accessors are named by BARE NAME; build_meta resolves them by reflection — set_value's one
    // parameter is std::string, i.e. the property type, and reset_value takes none.
    [[= make_property("value", "set_value", "", "reset_value")]]
    std::string value() const {
        return m_value;
    }
    void set_value(std::string v) {
        m_value = std::move(v);
    }
    void reset_value() {
        m_value = "reset";
    }
    std::string m_value;
};
META_OBJECT(my_gadget);

// Qt's `class Base : public QObject { signals: void baseSignal(int); }`.
struct base_t: object_t {
    OBJECT

public:
    SIGNAL(void base_signal(int) const)
};
META_OBJECT(base_t);

// Qt's tst_QMetaProperty. Some Q_PROPERTY entries exist only to serve an assertion that is marked at
// its site (an accepted difference, or the ruling-11 omission).
class tst_meta_property: public base_t {
    OBJECT

public:
    enum EnumType { EnumType1, EnumType2 };
    // Qt::AlignmentFlag stand-in (the library reflects only enums declared inside a type).
    enum alignment_flag { align_left = 0x1, align_right = 0x2, align_hcenter = 0x4 };

    // Qt: Q_PROPERTY(EnumType value WRITE setValue READ getValue) — hasStdCppSet()
    [[= make_property("value", "set_value")]]
    EnumType value() const {
        return EnumType1;
    }
    void set_value(EnumType) { }

    // Qt: Q_PROPERTY(EnumType value2 WRITE set_value READ get_value) — hasStdCppSet()
    [[= make_property("value2", "set_value2")]]
    EnumType value2() const {
        return EnumType1;
    }
    void set_value2(EnumType) { }

    // Qt: Q_PROPERTY(int value8 READ value8) — isConstant()
    [[= make_property("value8")]]
    int value8() const {
        return 1;
    }

    // Qt: Q_PROPERTY(int value9 READ value9 CONSTANT) — isConstant() and isFinal()
    [[= make_property("value9")]]
    int value9() const {
        return 1;
    }

    // Qt: Q_PROPERTY(int value10 READ value10 FINAL) — isFinal()
    [[= make_property("value10", "", "", "", property_trait_flag::FINAL)]]
    int value10() const {
        return 1;
    }

    // Qt: Q_PROPERTY(QString value7 MEMBER value7 RESET resetValue7) — conversion()
    // MEMBER + RESET; the writer is added because this library has no synthesised MEMBER writer.
    [[= make_property("value7", "set_value7", "", "reset_value7")]]
    std::string value7;

    // Qt: Q_PROPERTY(CustomType custom MEMBER custom) — conversion()
    [[= make_property("custom", "set_custom")]]
    custom_type custom;

    // Qt: Q_PROPERTY(QMap<int, int> map MEMBER map) — mapProperty()
    [[= make_property("map")]]
    std::map<int, int> map;

    // Qt: Q_PROPERTY(int propWithInheritedSig READ propWithInheritedSig NOTIFY baseSignal)
    // The notifier is declared by the BASE class: notifySignalIndex() exercises the inherited case,
    // which the compile-time pass deliberately leaves alone (it scans the declaring class only) and
    // meta_property_t::notifier() resolves at run time against the meta-object chain.
    [[= make_property("propWithInheritedSig", "", "base_signal")]]
    int prop_with_inherited_sig() const {
        return 0;
    }

    // Qt: Q_PROPERTY(QFlags<Qt::AlignmentFlag> qflags_value MEMBER qflags_value) — qflags()
    [[= make_property("qflags_value")]]
    flag_t<alignment_flag> qflags_value{};

    void set_value7(std::string v) {
        value7 = std::move(v);
    }
    void reset_value7() {
        value7 = "reset";
    }
    void set_custom(custom_type c) {
        custom = std::move(c);
    }
};

// ---------------------------------------------------------------------------
// No metatype declarations are needed any more: every type's interface is produced on demand from its
// own reflection, and its identity name is the canonical namer's spelling — the very spelling the
// reflection-built table stores, so the two agree by construction.
// ---------------------------------------------------------------------------

META_OBJECT(tst_meta_property);

// Qt's EnumFlagsTester — enumsFlags().
enum free_enum { free_enum_value1, free_enum_value2 };

struct enum_flags_tester: object_t {
    OBJECT

public:
    enum TestEnum { e1, e2 };
    // Qt: Q_DECLARE_FLAGS(TestFlags, TestFlag) — flag_t<TestFlag> is the library's value layer.
    enum TestFlag { flag1 = 0x1, flag2 = 0x2 };

    [[= make_property("enumProperty", "set_enum_property")]]
    TestEnum enum_property() const {
        return m_enum;
    }
    void set_enum_property(TestEnum e) {
        m_enum = e;
    }

    [[= make_property("flagProperty", "set_flag_property")]]
    flag_t<TestFlag> flag_property() const {
        return m_flags;
    }
    void set_flag_property(flag_t<TestFlag> f) {
        m_flags = f;
    }

    [[= make_property("freeEnumProperty", "set_free_enum_property")]]
    free_enum free_enum_property() const {
        return m_free_enum;
    }
    void set_free_enum_property(free_enum e) {
        m_free_enum = e;
    }

    // Extension (not a Qt assertion): a narrow underlying type, so the structural conversion's width
    // rule can be exercised — an int outside the enum's storage width must be rejected, not truncated.
    enum struct narrow_enum: std::uint8_t { narrow_zero = 0, narrow_max = 255 };

    [[= make_property("narrowEnumProperty", "set_narrow_enum_property")]]
    narrow_enum narrow_enum_property() const {
        return m_narrow;
    }
    void set_narrow_enum_property(narrow_enum e) {
        m_narrow = e;
    }

private:
    TestEnum m_enum = e1;
    flag_t<TestFlag> m_flags;
    free_enum m_free_enum = free_enum_value1;
    narrow_enum m_narrow = narrow_enum::narrow_zero;
};

// No declarations to make here either: enum_flags_tester's table produces every type interface it
// needs from the reflections themselves.
META_OBJECT(enum_flags_tester);

namespace qmetaproperty_test {

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

/// Whether @p f throws bad_any_cast_t — the library's documented channel for "the value does not fit
/// the property" (Qt returns false from QMetaProperty::write() instead).
template <typename F>
bool throws_bad_any_cast(F&& f) {
    try {
        f();
        return false;
    }
    catch (const bad_any_cast_t&) {
        return true;
    }
    catch (...) {
        return false;
    }
}

// ported from tst_qmetaproperty.cpp: hasStdCppSet()
void has_std_cpp_set(tst_meta_property& self) {
    const meta_object_t* mo = self.meta_object();

    meta_property_t prop = mo->property(mo->index_of_property("value"));
    CHECK(prop.is_valid());
    // ACCEPTED DIFFERENCE (owner-approved; Qt line 85, QVERIFY(prop.hasStdCppSet())): moc bookkeeping
    // only — Qt reports whether the writer was written as the conventional set<Property> name. This
    // library resolves the writer from the annotation's explicit name, so there is no
    // convention-versus-explicit distinction to expose; the query is not part of its meta API.

    prop = mo->property(mo->index_of_property("value2"));
    CHECK(prop.is_valid());
    // ACCEPTED DIFFERENCE (owner-approved; Qt line 89, QVERIFY(!prop.hasStdCppSet())): same reason —
    // the writer is whatever name the annotation gives, not a naming convention.
}

// ported from tst_qmetaproperty.cpp: isConstant()
void is_constant(tst_meta_property& self) {
    const meta_object_t* mo = self.meta_object();

    meta_property_t prop = mo->property(mo->index_of_property("value8"));
    CHECK(prop.is_valid());
    // ACCEPTED DIFFERENCE (owner-approved; Qt line 98, QVERIFY(!prop.isConstant()) for a read-only
    // property WITHOUT the CONSTANT attribute): this library derives constness from the value facts —
    // "no writer and no notifier" (meta_property_t.h) — because a declaration-based CONSTANT could lie
    // (marked constant while a writer still exists). Value-derived is the design.

    prop = mo->property(mo->index_of_property("value9"));
    CHECK(prop.is_valid());
    CHECK(prop.is_constant());
}

// ported from tst_qmetaproperty.cpp: isFinal()
void is_final(tst_meta_property& self) {
    const meta_object_t* mo = self.meta_object();

    meta_property_t prop = mo->property(mo->index_of_property("value10"));
    CHECK(prop.is_valid());
    CHECK(prop.is_final());

    prop = mo->property(mo->index_of_property("value9"));
    CHECK(prop.is_valid());
    CHECK(!prop.is_final());
}

// ported from tst_qmetaproperty.cpp: qflags()
void qflags(tst_meta_property& self) {
    const meta_object_t* mo = self.meta_object();

    meta_property_t prop = mo->property(mo->index_of_property("qflags_value"));
    // Qt: QVERIFY(prop.isEnumType()) — a flag_t<E> property names E, so the recorded enum column is
    // set (Qt reports QFlags<E> as an enum type for the same reason).
    CHECK(prop.is_enum_type());
    // Qt: QVERIFY(prop.isFlagType()) — the wrapper fact, kept apart from "is an enum" (see qflags()).
    CHECK(prop.is_flag_type());
    // The enum behind the wrapper is reachable, which is what makes the two facts above meaningful.
    CHECK(prop.enumerator().is_valid());
}

// ported from tst_qmetaproperty.cpp: gadget()
void gadget() {
    const meta_object_t& mo = my_gadget::static_meta_object;
    meta_property_t value_prop = mo.property(mo.index_of_property("value"));
    CHECK(value_prop.is_valid());
    // Qt: QCOMPARE(valueProp.metaType(), QMetaType::fromType<QString>()). The property type is the
    // ORIGINAL spelling of QString's stand-in (the identity name the registry stores for std::string);
    // std::basic_string<char> is only the canonical alias a caller writes.
    CHECK(value_prop.type_name() == meta_info_t::registered_type_name<std::string>());
    CHECK(meta_type_t::resolve_global(value_prop.type_name()).is<std::string>());
    {
        my_gadget g;
        std::string hello = "hello";
        CHECK(value_prop.write(g, any_t(hello)));
        CHECK(g.m_value == "hello");
        CHECK(value_prop.read(g).get<std::string>() == hello);
        CHECK(value_prop.reset(g));
        CHECK(value_prop.read(g).get<std::string>() == "reset");
    }
}

// ACCEPTED DIFFERENCE (owner-approved; Qt lines 285-299) — readAndWriteWithLazyRegistration() is not
// ported.
// The registry is read-only during use by design; implicit / lazy registration was deliberately
// removed (meta_type_t.h: "types must be registered explicitly at start-up"). The derived-pointer ->
// base-pointer conversion the Qt test also exercises belongs to the same Qt machinery and is absent
// for the same reason: an object pointer in an any_t is stored under the single guarded
// object_pointer_t<object_t> entry, with no T* -> base-T* converter.

// ported from tst_qmetaproperty.cpp: mapProperty()
void map_property(tst_meta_property& self) {
    self.map.insert({ 5, 9 });
    any_t v1 = any_t(self.map); // Qt: QVariant v1 = QVariant::fromValue(map) (unused, as in Qt)
    (void)v1;
    any_t v = self.property("map");
    CHECK(v.is_initialized());
    CHECK(self.map == v.get<int_map_t>());
}

// ported from tst_qmetaproperty.cpp: conversion()
void conversion(tst_meta_property& self) {
    // Qt: QMetaType::registerConverter<QString, CustomType>() and
    //     QMetaType::registerConverter<CustomType, QString>() — done in main() for
    //     custom_type <-> std::string.
    std::string hello = "Hello";

    // Write to a QString property using a CustomType in a QVariant.
    const meta_property_t value7_p = self.meta_object()->property("value7");
    CHECK(value7_p.is_valid());
    CHECK(value7_p.write(self, any_t(custom_type(hello))));
    CHECK(self.value7 == hello);

    // Write to a CustomType property using a QString in a QVariant.
    const meta_property_t custom_p = self.meta_object()->property("custom");
    CHECK(custom_p.is_valid());
    CHECK(custom_p.write(self, any_t(hello)));
    CHECK(self.custom.str == hello);

    // Something that cannot be converted should fail. Qt returns false; the library throws
    // bad_any_cast_t (the documented failure channel), which is the same outcome.
    CHECK(throws_bad_any_cast([&] { (void)custom_p.write(self, any_t(45)); }));
    CHECK(throws_bad_any_cast([&] { (void)custom_p.write(self, any_t(&self)); }));
    CHECK(throws_bad_any_cast([&] { (void)value7_p.write(self, any_t(&self)); }));
    CHECK(throws_bad_any_cast([&] { (void)value7_p.write(self, any_t(static_cast<object_t*>(&self))); }));

    // none of this should have changed the values
    CHECK(self.value7 == hello);
    CHECK(self.custom.str == hello);

    // ACCEPTED DIFFERENCE (owner-approved; Qt lines 341-345: writing an EMPTY QVariant, which Qt turns
    // into "default object" or "reset resetable"): this library throws bad_any_uninitialized_t on an
    // uninitialised any_t by design — Qt's reset-on-empty would silently mask a "forgot to set the
    // value" caller bug.
}

// ported from tst_qmetaproperty.cpp: enumsFlags()
void enums_flags() {
    enum_flags_tester t;
    const meta_object_t* mo = t.meta_object();

    const index_t enum_index = mo->index_of_property("enumProperty");
    CHECK(enum_index != index_npos);
    const meta_property_t enum_property = mo->property(enum_index);
    // Qt: enumProperty.metaType().flags().testFlag(QMetaType::IsEnumeration)
    CHECK(bool(enum_property.type(t).tag() & type_tag_flag::IS_ENUM));
    // Qt: QVERIFY(enumProperty.write(&t, QVariant(int(e2)))) + QCOMPARE(t.enumProperty(), e2). The
    // structural integer -> enum conversion (meta_type_t::convert) makes the write work without a
    // registered converter: C++ itself allows an enum to be initialised from an integer.
    CHECK(enum_property.write(t, any_t(static_cast<int>(enum_flags_tester::e2))));
    CHECK(t.enum_property() == enum_flags_tester::e2);
    // Qt: QVERIFY(enumProperty.enumerator().isValid()) — OK: Q_ENUM.
    CHECK(enum_property.enumerator().is_valid());

    const index_t flags_index = mo->index_of_property("flagProperty");
    CHECK(flags_index != index_npos);
    const meta_property_t flags_property = mo->property(flags_index);
    // Qt asserts the single QMetaType::IsEnumeration flag here; this library keeps "is an enum"
    // (IS_ENUM) and "wraps an enum" (IS_FLAGS) apart, and a flag_t<E> property is the wrapper — either
    // side answers Qt's question.
    CHECK(bool(flags_property.type(t).tag()
               & (type_tag_t(type_tag_flag::IS_ENUM) | type_tag_flag::IS_FLAGS)));
    // Qt: QVERIFY(flagsProperty.write(&t, QVariant(int(flag2)))) + QCOMPARE(t.flagProperty(), flag2):
    // the same structural integer -> flags conversion, reading/writing the wrapper's `data`.
    CHECK(flags_property.write(t, any_t(static_cast<int>(enum_flags_tester::flag2))));
    CHECK(t.flag_property() == enum_flags_tester::flag2);
    // Extension (not a Qt assertion): a multi-bit value round-trips through the wrapper's `data` — the
    // conversion reads the bit set, never the wrapper's operator bool.
    CHECK(flags_property.write(t, any_t(3)));
    CHECK(t.flag_property().data == 3);
    // Qt: QVERIFY(!flagsProperty.enumerator().isValid()); // Not using Q_FLAG.
    // Deliberate consequence of the approved design: the flags wrapper always records its enum, so the
    // handle is valid here even though the port has no Q_FLAG declaration to apply (Qt returns invalid
    // only because Q_FLAG was not used).
    CHECK(flags_property.enumerator().is_valid());

    const index_t free_enum_index = mo->index_of_property("freeEnumProperty");
    CHECK(free_enum_index != index_npos);
    const meta_property_t free_enum_property = mo->property(free_enum_index);
    CHECK(bool(free_enum_property.type(t).tag() & type_tag_flag::IS_ENUM));
    // Qt: QVariant(FreeEnumValue2) — a value of the enum's own type, so no conversion is needed.
    CHECK(free_enum_property.write(t, any_t(free_enum_value2)));
    CHECK(t.free_enum_property() == free_enum_value2);
    // Qt: QVERIFY(!freeEnumProperty.enumerator().isValid()) — Not using Q_ENUM. The enum is at
    // namespace scope, and this library reflects only enums declared inside a type, so no enumerator
    // is found (same answer as Qt).
    CHECK(!free_enum_property.enumerator().is_valid());

    // NOT PORTED (ruling 11): Qt's remaining five properties (namespacedEnumProperty,
    // qtNamespacedEnumProperty, qtNamespacedFlagProperty, sepEnum, sepFlags; lines 380-418) are
    // Q_ENUM_NS / Q_FLAG declarations of NAMESPACE-scope enums. This library reflects only enums
    // declared inside a type (documented in meta_enum_t.h), so those rows stay out.
}

// Extension (not a Qt test function): the structural conversion's width rule. An integer that does not
// fit the target enum's underlying width is rejected without truncation; an in-width one is accepted.
void width_rule() {
    enum_flags_tester t;
    const meta_property_t narrow = t.meta_object()->property("narrowEnumProperty");
    CHECK(narrow.is_enum_type());

    // narrow_enum's underlying type is std::uint8_t: 300 and -1 are outside its width.
    CHECK(throws_bad_any_cast([&] { (void)narrow.write(t, any_t(300)); }));
    CHECK(t.narrow_enum_property() == enum_flags_tester::narrow_enum::narrow_zero);
    CHECK(throws_bad_any_cast([&] { (void)narrow.write(t, any_t(-1)); }));
    CHECK(t.narrow_enum_property() == enum_flags_tester::narrow_enum::narrow_zero);

    // 200 fits: it is stored, and the value is the enumerator pattern, not a truncation.
    CHECK(narrow.write(t, any_t(200)));
    CHECK(t.narrow_enum_property() == static_cast<enum_flags_tester::narrow_enum>(200));

    // Widening always fits: an enum value read as an integer (the other side of the structural pair).
    CHECK(any_t(enum_flags_tester::e2).as<int>() == 1);

    // Extension: an ill-formed USER conversion pair is refused at compile time (returns false) instead
    // of instantiating a converter that cannot compile. Integer <-> enum does not use this path at all
    // (it is the structural rule above).
    CHECK((!register_conversion<int, enum_flags_tester::TestEnum>()));
}

// ported from tst_qmetaproperty.cpp: notifySignalIndex()
void notify_signal_index(tst_meta_property& self) {
    const meta_object_t* mo = self.meta_object();
    const index_t prop_index = mo->index_of_property("propWithInheritedSig");
    const meta_property_t prop_with_inherited_sig = mo->property(prop_index);
    CHECK(prop_with_inherited_sig.is_valid());
    CHECK(prop_with_inherited_sig.is_notifiable());
    // Qt: QVERIFY(propWithInheritedSig.notifySignalIndex() != -1). The library exposes the method
    // handle, not a property->method index, so the same fact is asserted through the chain lookup.
    CHECK(mo->index_of_signal("base_signal(int) const") != index_npos);
    const meta_method_t notify_signal = prop_with_inherited_sig.notifier();
    CHECK(notify_signal.is_valid());
    CHECK(notify_signal.name() == "base_signal");
    CHECK(notify_signal.param_count() == 1);
    // The handle points at the DECLARING class's table (base_t), which is where the run-time chain
    // walk found it — this is the notifySignalIndex() fact expressed through the method handle.
    CHECK(notify_signal.signature() == "base_signal(int) const");
    CHECK(notify_signal.param_type_name(0) == "int");
    CHECK(notify_signal.is_signal());
    CHECK(prop_with_inherited_sig.writer().is_valid() == false);   // no writer was declared
}

// Extension (not a Qt test function): AUDIT of the bare-name annotations. Every accessor the
// annotations name must actually resolve — a bare name carries no parameter spelling, so a renamed
// function or a parameter type that no longer matches the property type would silently turn the
// property read-only. This is the regression net for the migration back from complete signatures.
void accessor_audit() {
    const meta_object_t& gadget_meta = my_gadget::static_meta_object;
    const meta_property_t gadget_value = gadget_meta.property("value");
    // The stored writer signature keeps the ORIGINAL spelling of the property type; std::string is
    // only the alias a caller writes, so the expectation comes from the library.
    const std::string std_string_name(meta_info_t::original_name<std::string>());
    CHECK(gadget_value.is_writable());
    CHECK(gadget_value.is_resettable());
    CHECK(gadget_value.writer().signature() == "set_value(" + std_string_name + ")");
    CHECK(gadget_value.reseter().signature() == "reset_value()");

    tst_meta_property self;
    const meta_object_t* mo = self.meta_object();
    const auto writable = [&](const char* name) {
        const meta_property_t prop = mo->property(name);
        return prop.is_valid() && prop.is_writable() && prop.writer().is_valid();
    };
    CHECK(writable("value"));
    CHECK(writable("value2"));
    CHECK(writable("value7"));
    CHECK(writable("custom"));
    CHECK(!writable("value8"));                 // read-only by declaration: no writer named
    const meta_property_t value7 = mo->property("value7");
    CHECK(value7.is_resettable());
    CHECK(value7.reseter().signature() == "reset_value7()");

    // The inherited notifier is NOT resolved at compile time (the scan covers the declaring class
    // only); notifier() must find it through the run-time chain walk.
    const meta_property_t inherited = mo->property("propWithInheritedSig");
    CHECK(inherited.is_notifiable());
    CHECK(inherited.notifier().signature() == "base_signal(int) const");

    enum_flags_tester tester;
    const meta_object_t* tester_meta = tester.meta_object();
    for (const char* name : { "enumProperty", "flagProperty", "freeEnumProperty", "narrowEnumProperty" }) {
        const meta_property_t prop = tester_meta->property(name);
        CHECK(prop.is_valid());
        CHECK(prop.is_writable());
        CHECK(prop.writer().is_valid());
    }
}

}  // namespace qmetaproperty_test

using namespace qmetaproperty_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    // conversion(): Qt's QMetaType::registerConverter calls. Registered once in the process-wide
    // conversion map; the property write path resolves both endpoints through the same registry, so
    // both see this converter.
    register_conversion<custom_type, std::string>(); // Qt: <CustomType, QString>
    register_conversion<std::string, custom_type>(); // Qt: <QString, CustomType>

    // The property type NAMES this test resolves through the reflected tables. std::string and int
    // come from core_application's constructor; custom_type from the conversions above (they register
    // their endpoints); std::map<int,int> from the any_t(self.map) in mapProperty(). The enum / flag
    // types are only used here, so they are registered explicitly.
    (void)meta_type_t::register_type<tst_meta_property::EnumType>();
    (void)meta_type_t::register_type<enum_flags_tester::TestEnum>();
    (void)meta_type_t::register_type<hwshqtb::metaobject::flag_t<enum_flags_tester::TestFlag>>();
    (void)meta_type_t::register_type<enum_flags_tester::narrow_enum>();
    (void)meta_type_t::register_type<free_enum>();

    std::printf("== Qt port: qmetaproperty ==\n");

    tst_meta_property self;

    has_std_cpp_set(self);
    is_constant(self);
    is_final(self);
    qflags(self);
    gadget();
    map_property(self);
    conversion(self);
    enums_flags();
    width_rule();
    notify_signal_index(self);
    accessor_audit();

    if (g_failures == 0) {
        std::printf("  qmetaproperty: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("  qmetaproperty: %d of %d check(s) failed\n", g_failures, g_checks);
    return 1;
}
