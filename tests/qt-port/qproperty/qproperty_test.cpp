// Qt 6.8.3 port: tst_qproperty
// (qtbase/tests/auto/corelib/kernel/qproperty/tst_qproperty.cpp, 2605 lines).
//
// QProperty<T> / QPropertyData<T> are ported as hwshqtb::metaobject::property_t (a handle over
// any_t); Qt's property
// bindings, evaluation-time dependency tracking and invalidation scheduler are the library's M17
// layer (property_t.h). Qt's change handlers/notifiers/subscriptions are M17 Theme B:
// property_t::on_value_changed() / subscribe() return a move-only
// property_change_handler_t whose lifetime is the registration (the union of Qt's
// QPropertyChangeHandler<Functor> and QPropertyNotifier). One name for the registration: Qt's
// QProperty::addNotifier() and QProperty::onValueChanged() are the same operation and both port to
// on_value_changed(), so there is no add_notifier(). Qt's QObject-bound bindable properties are
// M17 Theme G: property_t::from(object, property) is the one way to reach a property as a bindable
// (the reference backend; QUntypedBindable's "does this name a bindable" is its is_valid()), and the
// class itself wires its bindable member to its notify signal in its constructor. Theme N adds
// set_value_bypassing_bindings() and notify().
// QPropertyAlias is NOT ported (it is deprecated since Qt 6.6);
// QUntypedBindable's read-only form is property_t::as_read_only() (B1); its metatype check is the
// declared-type set_binding() overload (B2); QPropertyBindingPrivate::scheduleNotify() is
// property_binding_t::schedule_notify() (B3). Q_OBJECT_COMPAT_PROPERTY is a property_t member + a
// writer + a notify signal (the compat bullet below gives the mapping). What still has NO
// counterpart: QPropertyObserver's raw observer object, the per-object adaptor store
// (QPropertyAdaptor's observer counts/identity) and thread-affine binding state — marked at their
// sites below.
//
// FUNCTION COUNT: the Qt 6.8.3 file defines 69 `void tst_QProperty::` slots (counted over the whole
// source). All 69 are accounted for below: 64 ported in full, 1 left out of this round
// (propertyAdaptorBinding — see below), 1 needs a design decision (C), 3 are permanently out of
// scope for this model (D).
//
// Ported in full (64):
//   functorBinding, basicDependencies, bindingWithInvalidatedPropertyObserver,
//   dependencyChangeDuringDestruction, recursiveDependency,
//   bindingFunctionDtorCalled, switchBinding, boolProperty, settingPropertyValueDoesRemoveBinding,
//   setBindingFunctor, typeNoOperatorEqual, bindingValueReplacement (Qt's body is commented out),
//   bindablePropertyWithInitialization, bindingWithDeletedDependency,
//   dontTriggerDependenciesIfUnchangedValue, arrowAndStarOperator,
//   changeHandler, propertyChangeHandlerApi, subscribe, changeHandlerThroughBindings,
//   changePropertyFromWithinChangeHandler, changePropertyFromWithinChangeHandler2, notifiedProperty,
//   qobjectObservers, notify, multipleObservers, noDoubleNotification, notifyAfterAllDepsGone,
//   takeBinding (C), stickyBinding (C), replaceBinding (C), uninstalledBindingDoesNotEvaluate (C),
//   bindingError (D), bindingLoop (D), bindingSourceLocation (D), selfBindingShouldNotCrash (D),
//   changePropertyFromWithinChangeHandlerThroughDependency (D),
//   groupedNotifications (E), groupedNotificationConsistency (E), bindingGroupBindingDeleted (E),
//   metaProperty (G), qobjectBindableManualNotify (G/N),
//   qobjectBindableSignalTakingNewValue (G), noFakeDependencies (G),
//   multipleDependencies, bindingAfterUse, avoidDependencyAllocationAfterFirstEval, noDoubleCapture,
//   qobjectBindableReallocatedBindingStorage, realloc,
//   modifyObserverListWhileIterating, derefFromObserver,
//   compatBindings, compatPropertyNoDobuleNotification, compatPropertySignals,
//   propertyUpdateViaSignaledProperty, bindableInterfaceOfCompatPropertyUsesSetter,
//   threadSafety2,
//   testNewStuff, genericPropertyBinding, genericPropertyBindingBool, quntypedBindableApi,
//   readonlyConstQBindable, scheduleNotify.
//
// Left out of this round (1) — a partial port is possible, but its remaining block is theme C:
//   propertyAdaptorBinding (the adaptor's VALUE/notification behaviour is expressible with
//   property_t::from() — a view is the adaptor for a notifiable non-BINDABLE property; its
//   per-object observer counts ride on Qt's per-object QPropertyAdaptorSlotObject binding data,
//   which the port's caller-owned views do not have. It stays a marker instead of a partial port).
//
// Needs a design decision (1) — marked at its site:
//   threadSafety (C: per-object binding storage / thread affinity).
//
// Out of scope for this model (3) — marked at their sites:
//   inheritQUntypedPropertyData (no property-data base to be derived from),
//   bindingGroupMovingBindingData (no per-object binding-data pointer that could move), and
//   qpropertyAlias (QPropertyAlias is deprecated in Qt 6.6; the port's property_alias was not
//   equivalent — it fired on a value change, not on a binding change — and was deleted).
//
// Themes:
//   A recomputation timing (read-time evaluation, with the batched round-end flush); B change
//   handlers / notifiers / subscribe; C binding handles (take / replace / sticky); D binding errors
//   and source locations; E property update groups; G QObject-bound bindable properties,
//   QMetaProperty::bindable(), compat properties (property adaptors' per-object counts stay out);
//   H untyped bindable interface and binding-storage introspection (read-only handles and the
//   declared-type check are in, the storage introspection is not); I QPropertyObserver;
//   J QPropertyAlias (not ported: deprecated in Qt 6.6); K value operators (operator-> / operator*,
//   no implicit conversion);
//   L thread-affine binding storage; N manual notification (setValueBypassingBindings / notify()).
//
// Spellings and mechanisms adapted (no asserted value changes):
//   * QVERIFY/QCOMPARE -> CHECK.
//   * Qt's binding functor is nullary, and so is the port's closure. A closure that reports an error
//     calls set_binding_error() (Qt's currentlyEvaluatingBinding()->setError). QProperty's functor
//     constructor and Qt::makePropertyBinding() have no counterpart, so a property is constructed
//     first and the binding is installed with property_t::set_binding() — the same asserted values,
//     one statement later.
//   * Qt's implicit conversion QProperty<T> -> T, and the arithmetic / logical / comparison
//     expressions built on it (`firstDependency + secondDependency`, `first && second`,
//     `if (property)`), are spelled .value().get<T>(); the erased handle has no operator-> /
//     operator* of its own (Theme K is expressed by reading the value back and dereferencing IT).
//   * Qt recomputes a dirty property when it is read. property_t::value() does the same: it
//     evaluates the binding and consumes its queued scheduler entry, so a read here is exactly the
//     read Qt performs (Theme A). A property that is never read is still recomputed by
//     flush_bindings(), hooked at the end of every event-dispatcher round
//     (binding_flush_guard in event_dispatcher::process_events). No local flush helper is needed.
//   * QPropertyBinding<T>::isNull() -> !property_t::has_binding(): the same fact, inverted.
//   * QString / QSignalSpy assertions -> plain counters. A signal assertion counts the emissions
//     through a functor connection (object_t::connect(signature, fn)); Qt's QSignalSpy(&object,
//     &MyQObject::fooChanged) becomes a counter incremented by that connection.
//   * QObject::destroyed() has a working counterpart: ~object_t emits the inherited
//     object_t::destroyed(object_t*) at Qt's point (after the death watch — a QPointer equivalent —
//     and before the children and the connection set are torn down) and, like Qt's, cannot be
//     blocked. Qt's clearBindingStorage() has no object-owned counterpart: a property's binding data
//     is the property's own core, which dies with its owner BEFORE ~object_t runs, so both orderings
//     have dropped the property's change handlers by the time a destroyed() slot writes a dependency.
//   * Q_OBJECT_BINDABLE_PROPERTY(Class, Type, data, &Class::signal) -> a property_t member plus
//     the property's annotation naming the signal, plus ONE line in the class's constructor that
//     subscribes the member to that signal (the meta layer no longer installs that hook — M17 stage 2
//     deleted the accessor layer). The trigger points are Qt's: every real value change, including an
//     install whose first result differs, plus a manual notify().
//   * Q_NOTIFIED_PROPERTY / Q_OBJECT_*_PROPERTY initialisation arguments are not expressible as
//     reflected properties; where Qt asserts only the resulting values (the two functions ported in
//     full that use them) the bindable's value constructor carries the same initial value.
//   * Qt's change handlers: QProperty::onValueChanged() and QProperty::addNotifier() (the same
//     operation in Qt) -> property_t::on_value_changed(), QProperty::subscribe()
//     -> property_t::subscribe(). Both return the same move-only property_change_handler_t, and
//     `notifier = {}` (Qt's QPropertyNotifier reset) becomes a move-assignment of an empty handle —
//     the same detach-on-reset lifetime rule.
//   * Theme C: QPropertyBinding<T> -> property_binding_t (one erased handle type; takeBinding() and
//     binding() return it, setBinding() accepts it). Qt's Qt::makePropertyBinding(f) has no
//     counterpart as a free function; the handle's own constructor takes the closure and the
//     source location (property_binding_t(f)), which is the same object one statement earlier.
//     Where Qt calls QBindable::setBinding(Qt::makePropertyBinding(f)), the port installs through the
//     property view: property_t::from(object, property).set_binding(f).
//   * Theme D: QPropertyBindingError -> property_binding_t_error_t; .type() -> .kind,
//     .description() -> .message, .hasError() -> .has_error(). Qt's eager propagation evaluates a
//     dependant binding while the dependency is being written; the library evaluates on read
//     (Theme A), so where Qt's loop error appears as a side effect of a write, the port performs
//     the read that triggers the same evaluation. Each such site is marked RE-EXPRESSED.
//   * Theme E: QScopedPropertyUpdateGroup -> property_update_group_t, Qt::beginPropertyUpdateGroup()
//     / Qt::endPropertyUpdateGroup() -> begin_property_update_group_t() /
//     end_property_update_group_t().
//   * Theme G: QUntypedBindable -> property_t (the reference view created by property_t::from());
//     QMetaProperty::bindable(&object) -> property_t::from(object, property);
//     QUntypedBindable::isBindable() (Qt's "the handle names a bindable") -> property_t::is_valid();
//     the QtPrivate::isAnyBindingEvaluating() probe -> is_binding_evaluating(). The old `bindable_xxx()`
//     accessors are gone: where the port needs the erased API (a property_binding_t handle, or
//     property_t::value()) it names the property's data member, which is exactly what the accessor
//     handed out; the property-as-bindable assertions go through property_t::from().
//     A binding installed on a view is recomputed EAGERLY at the end of the notification pass that
//     invalidated it and written back through the property's setter (the reference backend's rule,
//     because the value lives in the object and nobody reads the handle), so these sites need no
//     compensating read: Qt's eager assertion order is preserved.
//   * B1 (read-only): a const QProperty<T>* / const QBindable<T> is property_t::as_read_only(); the
//     flag lives on the handle, so a read-only copy still reports the value and the binding a
//     writable handle installed on the same variable. Qt's warning for a refused write goes through
//     the library's log (one Warning per refusal, category metaobject.connection).
//   * B2 (declared value type): QUntypedPropertyBinding(valueMetaType, closure) is
//     property_t::set_binding(closure, declared_type); a mismatch with the property's type refuses the
//     install (kind_enum::UNKNOWN + message, one Warning) and assigns nothing.
//   * B3 (install notification): QPropertyBindingPrivate::scheduleNotify() is
//     property_binding_t::schedule_notify(); the next install then notifies once even when the value
//     did not change.
//   * B4 (alias): NOT ported. QPropertyAlias<T> is deprecated since Qt 6.6, and the port's former
//     property_alias was not equivalent either — Qt's alias observer fires when the property's
//     BINDING changes, while that handler fired on a value change. The class and the qpropertyAlias()
//     test were deleted.
//   * Q_OBJECT_COMPAT_PROPERTY(setter[, signal[, getter[, default]]]) is a property_t data member + a
//     writer + a notify signal named by the annotation, with the class's constructor subscribing the
//     member to the signal (and, for Qt's getter form, emitting the GETTER's value). The compat
//     property's bindable interface is property_t::from(object, property): a write through the view
//     reaches the setter, and a write to the data member bypasses it (Qt's
//     valueBypassingBindings()).

#include <meta>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <source_location>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <hwshqtb/metaobject/annotation.h>
#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_method_t.h>
#include <hwshqtb/metaobject/meta_property_t.h>
#include <hwshqtb/metaobject/meta_type_t.h>
#include <hwshqtb/metaobject/object_pointer_t.h>
#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/property_t.h>
#include <hwshqtb/metaobject/thread_t.h>

using namespace hwshqtb::metaobject;

namespace qproperty_test {

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

// ---------------------------------------------------------------------------
// Qt's private introspection, expressed through the library's public core accessor:
// QPropertyBindingDataPointer::get(p).observerCount() == the number of dependants that read p,
// QPropertyBindingPrivate::get(...)->dependencyObserverCount == the number of dependencies a
// binding read last time. The core behind property_t keeps both lists as ordinary members
// (core::subscribers / core::dependencies) and property_t::core_handle() hands the core out, so
// the counts are exact. The core type is not reachable by name from the public headers, hence `auto`.
//
// subscribers holds dependency edges AND change handlers: for the functions that use these helpers
// the counted properties have no handlers, so the list length is exactly Qt's observer count.
// ---------------------------------------------------------------------------
std::size_t dependants(const property_t& property) noexcept {
    auto* core = property.core_handle().get();
    return core != nullptr ? core->subscribers.size() : 0;
}

std::size_t dependencies(const property_t& property) noexcept {
    auto* core = property.core_handle().get();
    return core != nullptr ? core->dependencies.size() : 0;
}

// ---------------------------------------------------------------------------
// Qt's test-local types (tst_qproperty.cpp: 22, 1016, 2104, 2123).
// ---------------------------------------------------------------------------

/// tst_qproperty.cpp: DtorCounter (line 22) — counts the destruction of a captured binding object.
struct dtor_counter {
    static inline int counter = 0;
    bool should_increment = false;
    ~dtor_counter() {
        if (should_increment) {
            ++counter;
        }
    }
};

/// tst_qproperty.cpp: Uncomparable (line 1016) — a type with deleted comparison operators.
struct uncomparable {
    int data = -1;
    bool changed_called = false;

    uncomparable(int value = 0) : data(value) { }
    uncomparable(const uncomparable& other) {
        data = other.data;
        changed_called = false;
    }
    uncomparable(uncomparable&& other) {
        data = other.data;
        changed_called = false;
        other.data = -1;
        other.changed_called = false;
    }
    uncomparable& operator=(const uncomparable& other) {
        data = other.data;
        return *this;
    }
    uncomparable& operator=(uncomparable&& other) {
        data = other.data;
        changed_called = false;
        other.data = -1;
        other.changed_called = false;
        return *this;
    }
    bool operator==(const uncomparable&) = delete;
    bool operator!=(const uncomparable&) = delete;

    void changed() {
        changed_called = true;
    }
};

/// tst_qproperty.cpp: CustomType (line 2104).
struct custom_type {
    custom_type() = default;
    custom_type(int value) : value(value) { }
    custom_type(int value, int another_value) : value(value), another_value(another_value) { }
    custom_type(const custom_type&) = default;
    custom_type(custom_type&&) = default;
    ~custom_type() = default;
    custom_type& operator=(const custom_type&) = default;
    custom_type& operator=(custom_type&&) = default;

    bool operator==(const custom_type& other) const {
        return (value == other.value) && (another_value == other.another_value);
    }

    int value = 0;
    int another_value = 0;
};

/// tst_qproperty.cpp: PropertyWithInitializationTester (line 2123), reduced to the data the ported
/// assertions read: Qt's Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS / Q_OBJECT_COMPAT_PROPERTY_WITH_ARGS
/// initialisers become bindable value constructors (same initial values).
struct property_with_initialization_tester {
    property_t prop1_data{ any_t(5) };
    property_t prop2_data{ any_t(custom_type(5)) };
    property_t prop3_data{ any_t(custom_type(10, 20)) };

    int prop1() {
        return prop1_data.value().get<int>();
    }
    custom_type prop2() {
        return prop2_data.value().get<custom_type>();
    }
    custom_type prop3() {
        return prop3_data.value().get<custom_type>();
    }
};

/// tst_qproperty.cpp: ReallocObject (line 1275), reduced to its value graph.
struct realloc_object {
    property_t v{ any_t(0) };
    property_t x{ any_t(0) };
    property_t y{ any_t(0) };
    property_t z{ any_t(0) };

    realloc_object() {
        v.set_binding([this]() {
            return any_t(x.value().get<int>() + y.value().get<int>() + z.value().get<int>());
        });
    }
};

/// tst_qproperty.cpp: BindingLoopTester (line 605), reduced to the two eager bindables the ported
/// assertions read: Qt's Q_OBJECT_COMPAT_PROPERTY + notify() layer is Theme G's compat half (out of
/// this round), and the assertions only look at the two bindings' error states.
struct binding_loop_tester {
    property_t eager_data{ any_t(0) };
    property_t eager_data2{ any_t(0) };

    /// The Qt constructor binds the two properties into a 2-cycle and then writes the source.
    explicit binding_loop_tester(property_t* i) {
        eager_data.set_binding([this, i]() {
            return any_t(eager_data2.value().get<int>() + i->value().get<int>());
        });
        eager_data2.set_binding(
            [this]() { return any_t(eager_data.value().get<int>() + 1); });
        i->set_value(any_t(42));
    }

    /// Qt's second block constructs the tester without the source property and installs bindings
    /// afterwards.
    binding_loop_tester() = default;
};

// ---------------------------------------------------------------------------
// Theme G: the object-bound layer (tst_qproperty.cpp: MyQObject at line 1170,
// ClassWithNotifiedProperty at line 928, FakeDependencyCreator at line 1738).
//
// A Q_OBJECT_BINDABLE_PROPERTY(Class, Type, data, &Class::signal) becomes:
//   * a property_t member (the property data, exactly Qt's storage),
//   * a SIGNAL declaration for the notify signal,
//   * and ONE subscription in the class's constructor: the member's change handler emits the signal
//     (the meta layer no longer installs that hook — M17 stage 2). The handle it returns is a class
//     member, because the handle IS the registration.
// ---------------------------------------------------------------------------

/// tst_qproperty.cpp: MyQObject (line 1170). foo/bar are the BINDABLE ones; read/computed are the
/// plain read-only properties the compat half would need (the assertions only read them).
struct my_qobject : object_t {
    OBJECT
public:
    /// Qt's Q_OBJECT_BINDABLE_PROPERTY notify callbacks, written explicitly (M17 stage 2): each
    /// property's own property member drives its signal.
    my_qobject() {
        _foo_notifier =
            foo_data.on_value_changed([this] { foo_changed(foo_data.value().get<int>()); });
        _bar_notifier = bar_data.on_value_changed([this] { bar_changed(); });
    }

    [[= make_property("foo", "set_foo", "foo_changed")]]
    int foo() const { return foo_data.value().get<int>(); }

    void set_foo(int i) { foo_data.set_value(any_t(i)); }

    [[= make_property("bar", "set_bar", "bar_changed")]]
    int bar() const { return bar_data.value().get<int>(); }

    void set_bar(int i) { bar_data.set_value(any_t(i)); }

    [[= make_property("read")]]
    int read() const { return read_data.value().get<int>(); }

    /// Qt: Q_OBJECT_COMPUTED_PROPERTY(MyQObject, int, computedData, &MyQObject::computed). The
    /// getter is what the assertions read (readData), which is exactly what Qt's computed getter
    /// does.
    [[= make_property("computed")]]
    int computed() const { return read_data.value().get<int>(); }

    /// Qt: Q_OBJECT_COMPAT_PROPERTY(MyQObject, int, compatData, &MyQObject::setCompat) (line 1227).
    /// The property's bindable interface is its SETTER: a write through a property_t view reaches
    /// set_compat(), which clamps, counts and emits compatChanged; a write to the backing member
    /// (compat_data) bypasses all of that — Qt's valueBypassingBindings().
    [[= make_property("compat", "set_compat", "compat_changed")]]
    int compat() const { return compat_data.value().get<int>(); }

    void set_compat(int i) {
        if (compat_data.value().get<int>() == i)
            return;
        ++set_compat_called;
        if (i < 0)
            i = 0;
        // Qt: compatData.setValue(i) + compatData.notify(). The port's set_value() already notifies
        // exactly once, so the explicit notify() is dropped (same single notification).
        compat_data.set_value(any_t(i));
        compat_changed();
    }

    /// Qt's notify signals (the Q_OBJECT_BINDABLE_PROPERTY arguments). fooChanged takes the new value,
    /// barChanged does not.
    SIGNAL(void foo_changed(int new_foo) const)
    SIGNAL(void bar_changed() const)
    SIGNAL(void compat_changed() const)

    /// Qt connects fooHasChanged / barHasChanged in its tests. The subscriptions themselves live in
    /// the constructor; this keeps the connection half (the trigger point the tests call).
    void listen() {
        connect("foo_changed(int) const", [this]() { ++foo_changed_count; });
        connect("bar_changed() const", [this]() { ++bar_changed_count; });
        connect("compat_changed() const", [this]() { ++compat_changed_count; });
    }

    int foo_changed_count = 0;
    int bar_changed_count = 0;
    int compat_changed_count = 0;
    int set_compat_called = 0;

    property_t foo_data{ any_t(0) };
    property_t bar_data{ any_t(0) };
    property_t read_data{ any_t(0) };
    property_t computed_data{ any_t(0) };
    property_t compat_data{ any_t(0) };

private:
    /// What foo_data/bar_data.on_value_changed() returned; destroying them unregisters.
    property_change_handler_t _foo_notifier;
    property_change_handler_t _bar_notifier;
};
META_OBJECT(my_qobject);

/// tst_qproperty.cpp: ClassWithNotifiedProperty (line 928). Qt passes a callback to
/// Q_OBJECT_BINDABLE_PROPERTY; here that callback is the constructor's subscription, and the signal
/// connection records the value.
struct class_with_notified_property : object_t {
    OBJECT
public:
    class_with_notified_property() {
        _notifier = property_data.on_value_changed(
            [this] { property_changed(property_data.value().get<int>()); });
        connect("property_changed(int) const", [this]() { recorded_values.push_back(property()); });
    }

    [[= make_property("property", "set_property", "property_changed")]]
    int property() const { return property_data.value().get<int>(); }

    void set_property(int v) { property_data.set_value(any_t(v)); }

    SIGNAL(void property_changed(int new_value) const)

    /// Qt's ClassWithNotifiedProperty::recordedValues.
    std::vector<int> recorded_values;
    property_t property_data{ any_t(0) };

private:
    property_change_handler_t _notifier;
};
META_OBJECT(class_with_notified_property);

/// tst_qproperty.cpp: FakeDependencyCreator (line 1738): three BINDABLE properties, each with a
/// notifier, plus setters that write and notify manually (Qt's Q_OBJECT_COMPAT_PROPERTY setters).
struct fake_dependency_creator : object_t {
    OBJECT
public:
    /// Qt's three Q_OBJECT_COMPAT_PROPERTY declarations install the notify hooks; here each property's
    /// bindable member is wired to its signal by the constructor.
    fake_dependency_creator() {
        _prop1_notifier = prop1_data.on_value_changed([this] { prop1_changed(); });
        _prop2_notifier = prop2_data.on_value_changed([this] { prop2_changed(); });
        _prop3_notifier = prop3_data.on_value_changed([this] { prop3_changed(); });
    }

    [[= make_property("prop1", "set_prop1", "prop1_changed")]]
    int prop1() const { return prop1_data.value().get<int>(); }

    [[= make_property("prop2", "set_prop2", "prop2_changed")]]
    int prop2() const { return prop2_data.value().get<int>(); }

    [[= make_property("prop3", "set_prop3", "prop3_changed")]]
    int prop3() const { return prop3_data.value().get<int>(); }

    void set_prop1(int val) {
        prop1_data.set_value(any_t(val));
        prop1_data.notify();
    }
    void set_prop2(int val) {
        prop2_data.set_value(any_t(val));
        prop2_data.notify();
    }
    void set_prop3(int val) {
        prop3_data.set_value(any_t(val));
        prop3_data.notify();
    }

    SIGNAL(void prop1_changed() const)
    SIGNAL(void prop2_changed() const)
    SIGNAL(void prop3_changed() const)

    property_t prop1_data{ any_t(0) };
    property_t prop2_data{ any_t(0) };
    property_t prop3_data{ any_t(0) };

private:
    property_change_handler_t _prop1_notifier;
    property_change_handler_t _prop2_notifier;
    property_change_handler_t _prop3_notifier;
};
META_OBJECT(fake_dependency_creator);

/// tst_qproperty.cpp: CompatPropertyTester (line 1594) — the four compat properties. Qt's
/// Q_OBJECT_COMPAT_PROPERTY setter + notify-signal pair becomes a setter that writes the data member
/// plus the constructor's one-line data-member-to-signal subscriptions (the MyQObject pattern).
/// prop3 carries Qt's default argument (1); prop4 carries Qt's getter (0 is reported as 42) AND its
/// default 0.
struct compat_property_tester : object_t {
    OBJECT
public:
    compat_property_tester() {
        _prop1_notifier = prop1_data.on_value_changed([this] { prop1_changed(); });
        _prop2_notifier = prop2_data.on_value_changed(
            [this] { prop2_changed(prop2_data.value().get<int>()); });
        _prop3_notifier = prop3_data.on_value_changed([this] { prop3_changed(); });
        // Qt's Q_OBJECT_COMPAT_PROPERTY_WITH_ARGS(... &CompatPropertyTester::prop4, 0): the signal
        // carries the GETTER's value (the compat property's value), not the raw storage.
        _prop4_notifier = prop4_data.on_value_changed([this] { prop4_changed(prop4()); });
    }

    /// Qt's prop1 declares no NOTIFY signal (line 1597). property_t::from() admits only a notifiable
    /// property, so the port gives it one: the compat property's observers are notified by the data
    /// write either way, and compatPropertyNoDobuleNotification asserts exactly that single
    /// notification.
    [[= make_property("prop1", "set_prop1", "prop1_changed")]]
    int prop1() const { return prop1_data.value().get<int>(); }

    void set_prop1(int i) {
        if (i == prop1_data.value().get<int>())
            return;
        prop1_data.set_value(any_t(i));
    }

    [[= make_property("prop2", "set_prop2", "prop2_changed")]]
    int prop2() const { return prop2_data.value().get<int>(); }

    void set_prop2(int i) {
        if (i == prop2_data.value().get<int>())
            return;
        prop2_data.set_value(any_t(i));
    }

    [[= make_property("prop3", "set_prop3", "prop3_changed")]]
    int prop3() const { return prop3_data.value().get<int>(); }

    void set_prop3(int i) {
        if (i == prop3_data.value().get<int>())
            return;
        prop3_data.set_value(any_t(i));
    }

    [[= make_property("prop4", "set_prop4", "prop4_changed")]]
    int prop4() const {
        const int value = prop4_data.value().get<int>();
        return value == 0 ? 42 : value;
    }

    void set_prop4(int i) {
        if (i == prop4_data.value().get<int>())    // Qt compares against the DATA, not the getter
            return;
        prop4_data.set_value(any_t(i));
    }

    SIGNAL(void prop1_changed() const)
    SIGNAL(void prop2_changed(int value) const)
    SIGNAL(void prop3_changed() const)
    SIGNAL(void prop4_changed(int value) const)

    property_t prop1_data{ any_t(0) };
    property_t prop2_data{ any_t(0) };
    property_t prop3_data{ any_t(1) };      // Qt's Q_OBJECT_COMPAT_PROPERTY_WITH_ARGS default
    property_t prop4_data{ any_t(0) };      // Qt's default 0 (reported as 42 by the getter)

private:
    property_change_handler_t _prop1_notifier;
    property_change_handler_t _prop2_notifier;
    property_change_handler_t _prop3_notifier;
    property_change_handler_t _prop4_notifier;
};
META_OBJECT(compat_property_tester);

/// tst_qproperty.cpp: ChangeDuringDtorTester (line 278) — the object whose compat property is written
/// from its own destroyed() signal. Qt declares
/// Q_PROPERTY(int prop READ prop WRITE setProp BINDABLE bindableProp) with
/// Q_OBJECT_COMPAT_PROPERTY(ChangeDuringDtorTester, int, m_prop, &ChangeDuringDtorTester::setProp), so
/// `bindableProp()` IS the property data member; the port's counterpart is `prop_data`, and
/// `bindableProp().setBinding(...)` / `onValueChanged(...)` are its own set_binding() /
/// on_value_changed() (the compat-property pattern above: one constructor line subscribes the member to
/// the notify signal; Qt's compat property declares no signal, the port's property views need one).
struct change_during_dtor_tester : object_t {
    OBJECT
public:
    change_during_dtor_tester() {
        _prop_notifier = prop_data.on_value_changed([this] { prop_changed(); });
    }

    [[= make_property("prop", "set_prop", "prop_changed")]]
    int prop() const { return prop_data.value().get<int>(); }

    /// Qt's setProp: the compat property's setter writes its data member.
    void set_prop(int i) { prop_data.set_value(any_t(i)); }

    SIGNAL(void prop_changed() const)

    property_t prop_data{ any_t(0) };

private:
    property_change_handler_t _prop_notifier;
};
META_OBJECT(change_during_dtor_tester);

/// tst_qproperty.cpp: TestObject (line 2456) — two compat properties driven through their setters by
/// the two notify signals, plus a plain signalled property.
struct test_object_t : object_t {
    OBJECT
public:
    [[= make_property("signaled", "set_signaled", "signaled_changed")]]
    int signaled() const { return m_signaled; }

    void set_signaled(int new_signaled) {
        if (m_signaled == new_signaled)
            return;
        m_signaled = new_signaled;
        signaled_changed();
    }

    [[= make_property("bindable1", "set_bindable1", "bindable1_changed")]]
    int bindable1() const { return m_bindable1.value().get<int>(); }

    void set_bindable1(int new_bindable1) {
        if (m_bindable1.value().get<int>() == new_bindable1)
            return;
        m_bindable1.set_value(any_t(new_bindable1));
        bindable1_changed();
    }

    [[= make_property("bindable2", "set_bindable2", "bindable2_changed")]]
    int bindable2() const { return m_bindable2.value().get<int>(); }

    void set_bindable2(int new_bindable2) {
        if (m_bindable2.value().get<int>() == new_bindable2)
            return;
        m_bindable2.set_value(any_t(new_bindable2));
        bindable2_changed();
    }

    SIGNAL(void signaled_changed() const)
    SIGNAL(void bindable1_changed() const)
    SIGNAL(void bindable2_changed() const)

    int m_signaled = 0;
    property_t m_bindable1{ any_t(0) };
    property_t m_bindable2{ any_t(0) };
};
META_OBJECT(test_object_t);

/// tst_qproperty.cpp: ThreadSafetyTester (line 1983) for threadSafety2's purposes: the function
/// asserts nothing but the names and the child list after a cross-thread move.
struct thread_safety_object_t : object_t {
    OBJECT
};
META_OBJECT(thread_safety_object_t);

/// tst_qproperty.cpp: QPropertyUsingThread (line 2068). Qt's worker runs an event loop and the test
/// quits it; here nothing is posted to the worker, so run() creates the subtree, moves it back and
/// returns (the loop carries no asserted fact).
struct property_using_thread_t : thread_t {
    OBJECT
public:
    /// The destination thread's identity. object_t::thread()'s type is thread_t*, named through
    /// decltype so the test also pins that type.
    using thread_t_ptr = decltype(std::declval<object_t&>().thread());

    void run() override {
        auto* created = new thread_safety_object_t;
        created->set_object_name("test");
        auto* child = new thread_safety_object_t;
        child->set_object_name("child");
        child->set_parent(created);
        // Qt: scopedObj1->moveToThread(destThread) — the whole subtree follows.
        created->move_to_thread(dest_thread);
        dest->store(created, std::memory_order_release);
    }

    std::atomic<object_t*>* dest = nullptr;
    thread_t_ptr dest_thread = nullptr;
};
META_OBJECT(property_using_thread_t);

// ---------------------------------------------------------------------------
// Ported in full.
// ---------------------------------------------------------------------------

// ported from tst_qproperty.cpp: functorBinding() (line 169)
void functor_binding() {
    // Qt constructs from a functor; there is no such constructor here, so the binding is installed
    // explicitly (same asserted values).
    property_t property{ any_t(0) };
    property.set_binding([]() { return any_t(42); });
    CHECK(property.value().get<int>() == 42);
    property.set_binding([]() { return any_t(100); });
    CHECK(property.value().get<int>() == 100);
    property.set_binding([]() { return any_t(50); });
    CHECK(property.value().get<int>() == 50);
}

// ported from tst_qproperty.cpp: basicDependencies() (line 179)
void basic_dependencies() {
    property_t right{ any_t(100) };
    property_t left{ any_t(0) };
    left.set_binding([&]() { return any_t(right.value().get<int>()); });

    CHECK(left.value().get<int>() == 100);

    right.set_value(any_t(42));

    CHECK(left.value().get<int>() == 42);
}

// ported from tst_qproperty.cpp: bindingWithInvalidatedPropertyObserver() (line 256)
void binding_with_invalidated_property_observer() {
    property_t dynamic_property1{ any_t(false) };
    property_t dynamic_property2{ any_t(false) };
    property_t dynamic_property3{ any_t(false) };
    property_t dynamic_property4{ any_t(false) };
    property_t dynamic_property5{ any_t(false) };
    property_t dynamic_property6{ any_t(false) };
    property_t property_selector{ any_t(false) };
    property_selector.set_binding([&]() {
        return any_t(dynamic_property1.value().get<bool>() && dynamic_property2.value().get<bool>()
                     && dynamic_property3.value().get<bool>() && dynamic_property4.value().get<bool>()
                     && dynamic_property5.value().get<bool>() && dynamic_property6.value().get<bool>());
    });

    dynamic_property1.set_value(any_t(true));
    dynamic_property2.set_value(any_t(true));
    dynamic_property3.set_value(any_t(true));
    dynamic_property4.set_value(any_t(true));
    dynamic_property5.set_value(any_t(true));
    CHECK(property_selector.value().get<bool>() == false);
    dynamic_property6.set_value(any_t(true));
    CHECK(property_selector.value().get<bool>() == true);
}

// ported from tst_qproperty.cpp: recursiveDependency() (line 307)
void recursive_dependency() {
    property_t first{ any_t(1) };

    property_t second{ any_t(0) };
    second.set_binding([&]() { return any_t(first.value().get<int>()); });

    property_t third{ any_t(0) };
    third.set_binding([&]() { return any_t(second.value().get<int>()); });

    CHECK(third.value().get<int>() == 1);

    first.set_value(any_t(2));

    CHECK(third.value().get<int>() == 2);
}

// ported from tst_qproperty.cpp: bindingFunctionDtorCalled() (line 341)
void binding_function_dtor_called() {
    dtor_counter::counter = 0;
    dtor_counter counter;

    {
        property_t property{ any_t(0) };
        property.set_binding([counter]() mutable {
            counter.should_increment = true;
            return any_t(42);
        });
        CHECK(property.value().get<int>() == 42);
    }

    CHECK(dtor_counter::counter == 1);
}

// ported from tst_qproperty.cpp: switchBinding() (line 356)
void switch_binding() {
    property_t first{ any_t(1) };

    property_t property_with_changing_binding{ any_t(0) };
    property_with_changing_binding.set_binding(
        [&]() { return any_t(first.value().get<int>()); });

    CHECK(property_with_changing_binding.value().get<int>() == 1);

    property_t output{ any_t(0) };
    output.set_binding(
        [&]() { return any_t(property_with_changing_binding.value().get<int>()); });
    CHECK(output.value().get<int>() == 1);

    property_t second{ any_t(2) };
    property_with_changing_binding.set_binding(
        [&]() { return any_t(second.value().get<int>()); });
    CHECK(output.value().get<int>() == 2);
}

// ported from tst_qproperty.cpp: boolProperty() (line 391)
void bool_property() {
    property_t first{ any_t(true) };
    property_t second{ any_t(false) };
    property_t all{ any_t(false) };
    all.set_binding(
        [&]() { return any_t(first.value().get<bool>() && second.value().get<bool>()); });

    CHECK(all.value().get<bool>() == false);

    second.set_value(any_t(true));

    CHECK(all.value().get<bool>() == true);
}

// ported from tst_qproperty.cpp: settingPropertyValueDoesRemoveBinding() (line 793)
void setting_property_value_does_remove_binding() {
    property_t source{ any_t(42) };

    property_t property{ any_t(0) };
    property.set_binding([&]() { return any_t(source.value().get<int>()); });

    CHECK(property.value().get<int>() == 42);
    CHECK(property.has_binding());   // Qt: !property.binding().isNull()

    property.set_value(any_t(100));
    CHECK(property.value().get<int>() == 100);
    CHECK(!property.has_binding());  // Qt: property.binding().isNull()

    source.set_value(any_t(1));
    CHECK(property.value().get<int>() == 100);
    CHECK(!property.has_binding());
}

// ported from tst_qproperty.cpp: setBindingFunctor() (line 855)
void set_binding_functor() {
    property_t property{ any_t(0) };
    property_t injected_value{ any_t(100) };
    property.set_binding([&injected_value]() { return any_t(injected_value.value().get<int>()); });
    injected_value.set_value(any_t(200));
    CHECK(property.value().get<int>() == 200);
}

// ported from tst_qproperty.cpp: typeNoOperatorEqual() (line 1014)
void type_no_operator_equal() {
    uncomparable u1 = { 13 };
    uncomparable u2 = { 27 };

    property_t p1{ any_t(uncomparable{}) };
    property_t p2{ any_t(uncomparable{}) };
    p2.set_binding([&]() { return p1.value(); });

    CHECK(p1.value().get<uncomparable>().data == p2.value().get<uncomparable>().data);
    p1.set_value(any_t(u1));
    CHECK(p1.value().get<uncomparable>().data == u1.data);
    CHECK(p1.value().get<uncomparable>().data == p2.value().get<uncomparable>().data);
    p2.set_value(any_t(u2));
    CHECK(p1.value().get<uncomparable>().data == u1.data);
    CHECK(p2.value().get<uncomparable>().data == u2.data);

    property_t p3{ any_t(uncomparable{}) };
    p3.set_binding([&]() { return p1.value(); });
    p1.set_value(any_t(u1));
    CHECK(p1.value().get<uncomparable>().data == p3.value().get<uncomparable>().data);
}

// ported from tst_qproperty.cpp: bindingValueReplacement() (line 1103)
void binding_value_replacement() {
    // Qt 6.8.3 has this function's whole body commented out (lines 1103-1112): nothing is asserted.
    // Ported as the empty function for the inventory's sake.
}

// ported from tst_qproperty.cpp: bindablePropertyWithInitialization() (line 2156)
void bindable_property_with_initialization() {
    property_with_initialization_tester tester;

    CHECK(tester.prop1() == 5);
    CHECK(tester.prop2().value == 5);
    CHECK(tester.prop3().value == 10);
    CHECK(tester.prop3().another_value == 20);
}

// ported from tst_qproperty.cpp: bindingWithDeletedDependency() (line 226)
void binding_with_deleted_dependency() {
    // Qt's QScopedPointer<QProperty<int>>; the owning pointer is what becomes null on reset, while
    // the binding's closure keeps reading the pointer variable itself (Qt checks isNull() the same
    // way, line 237).
    std::unique_ptr<property_t> dynamic_property(new property_t{ any_t(100) });

    property_t static_property{ any_t(1000) };

    property_t binding_returns_dynamic_property{ any_t(false) };

    property_t property_selector{ any_t(0) };
    property_selector.set_binding([&]() {
        if (binding_returns_dynamic_property.value().get<bool>() && dynamic_property != nullptr) {
            return any_t(dynamic_property->value().get<int>());
        }
        return any_t(static_property.value().get<int>());
    });

    CHECK(property_selector.value().get<int>() == static_property.value().get<int>());

    binding_returns_dynamic_property.set_value(any_t(true));

    CHECK(property_selector.value().get<int>() == dynamic_property->value().get<int>());

    // The dependency dies while the binding is installed: its destructor tells the dependant to drop
    // it (Theme M). The dependant keeps the value it last computed and remains readable.
    dynamic_property.reset();

    CHECK(property_selector.value().get<int>() == 100);

    // The surviving dependency still invalidates the binding, which now takes the other branch.
    binding_returns_dynamic_property.set_value(any_t(false));

    CHECK(property_selector.value().get<int>() == static_property.value().get<int>());
}

// ported from tst_qproperty.cpp: dontTriggerDependenciesIfUnchangedValue() (line 566)
void dont_trigger_dependencies_if_unchanged_value() {
    property_t property{ any_t(42) };

    bool triggered = false;
    property_t observer{ any_t(0) };
    observer.set_binding([&]() {
        triggered = true;
        return any_t(property.value().get<int>());
    });

    CHECK(observer.value().get<int>() == 42);
    CHECK(triggered);

    triggered = false;
    property.set_value(any_t(42));      // equal value: no notification reaches the dependant
    CHECK(observer.value().get<int>() == 42);
    CHECK(!triggered);
}

// ported from tst_qproperty.cpp: arrowAndStarOperator() (line 901)
void arrow_and_star_operator() {
    std::string text("Hello");
    property_t pointer{ any_t(&text) };

    CHECK(pointer.value().get<std::string*>()->size() == text.size());
    CHECK(*pointer.value().get<std::string*>() == text);

    // Qt's QTypeTraits::is_dereferenceable_v<Dereferenceable> (line 918) is a trait over a type with
    // operator->; the erased handle has no operator-> of its own, so the value is read back and the
    // VALUE's own operator-> performs the member lookup (the same requirement, stated directly).
    struct dereferenceable {
        std::string text;
        std::string* operator->() { return &text; }
        const std::string* operator->() const { return &text; }
    };
    static_assert(requires(const dereferenceable& object) { object.operator->(); });

    property_t object_pointer_t{ any_t(dereferenceable{ text }) };
    CHECK(object_pointer_t.value().get<dereferenceable>().operator->()->size()
          == text.size());
    CHECK(*pointer.value().get<std::string*>() == text);  // Qt's line 922 repeats **prop, not **prop2

    // Qt's QSharedPointer<QObject> block (lines 924-929): the shared pointer is dereferenceable
    // through its own operator->, which the value read-back forwards to.
    struct named_object {
        std::string name;
    };
    auto object = std::make_shared<named_object>();
    object->name = text;

    property_t shared_pointer{ any_t(std::shared_ptr<named_object>(object)) };
    CHECK(shared_pointer.value().get<std::shared_ptr<named_object>>()->name == text);
    CHECK(shared_pointer.value().get<std::shared_ptr<named_object>>() == object);
}

// ported from tst_qproperty.cpp: changeHandler() (line 467)
void change_handler() {
    property_t test_property{ any_t(0) };
    std::vector<int> recorded_values;

    {
        auto handler = test_property.on_value_changed([&]() {
            recorded_values.push_back(test_property.value().get<int>());
        });

        test_property.set_value(any_t(1));
        test_property.set_value(any_t(2));
    }
    test_property.set_value(any_t(3));

    CHECK(recorded_values.size() == 2);
    CHECK(recorded_values.at(0) == 1);
    CHECK(recorded_values.at(1) == 2);
}

// ported from tst_qproperty.cpp: propertyChangeHandlerApi() (line 487)
void property_change_handler_t_api() {
    int change_handler_call_count = 0;
    property_change_handler_t handler([&change_handler_call_count]() {
        ++change_handler_call_count;
    });

    property_t source1{ any_t(0) };
    property_t source2{ any_t(0) };

    handler.set_source(source1);

    source1.set_value(any_t(100));
    CHECK(change_handler_call_count == 1);

    handler.set_source(source2);
    source1.set_value(any_t(101));
    CHECK(change_handler_call_count == 1);

    source2.set_value(any_t(200));
    CHECK(change_handler_call_count == 2);
}

// ported from tst_qproperty.cpp: subscribe() (line 510)
void subscribe() {
    property_t test_property{ any_t(42) };
    std::vector<int> recorded_values;

    {
        auto handler = test_property.subscribe([&]() {
            recorded_values.push_back(test_property.value().get<int>());
        });

        test_property.set_value(any_t(1));
        test_property.set_value(any_t(2));
    }
    test_property.set_value(any_t(3));

    CHECK(recorded_values.size() == 3);
    CHECK(recorded_values.at(0) == 42);
    CHECK(recorded_values.at(1) == 1);
    CHECK(recorded_values.at(2) == 2);
}

// ported from tst_qproperty.cpp: changeHandlerThroughBindings() (line 531)
void change_handler_through_bindings() {
    property_t trigger{ any_t(false) };
    property_t block_trigger{ any_t(false) };
    property_t condition{ any_t(false) };
    condition.set_binding([&]() {
        // Qt's closure reads both dependencies through their implicit conversion (lines 539-543).
        bool trigger_value = trigger.value().get<bool>();
        bool block_trigger_value = block_trigger.value().get<bool>();
        return any_t(trigger_value && !block_trigger_value);
    });
    bool change_handler_called = false;
    auto handler = condition.on_value_changed([&]() {
        change_handler_called = true;
    });

    CHECK(!condition.value().get<bool>());
    CHECK(!change_handler_called);

    trigger.set_value(any_t(true));

    CHECK(condition.value().get<bool>());
    CHECK(change_handler_called);
    change_handler_called = false;

    trigger.set_value(any_t(false));

    CHECK(!condition.value().get<bool>());
    CHECK(change_handler_called);
    change_handler_called = false;

    block_trigger.set_value(any_t(true));

    CHECK(!condition.value().get<bool>());
    CHECK(!change_handler_called);
}

// ported from tst_qproperty.cpp: changePropertyFromWithinChangeHandler() (line 728)
void change_property_from_within_change_handler() {
    property_t property{ any_t(100) };
    bool reset_property_on_change = false;
    int change_handler_call_count = 0;

    auto handler = property.on_value_changed([&]() {
        ++change_handler_call_count;
        if (reset_property_on_change)
            property.set_value(any_t(100));
    });

    CHECK(property.value().get<int>() == 100);

    reset_property_on_change = true;
    property.set_value(any_t(42));
    CHECK(property.value().get<int>() == 100);
    // changing the property value inside the change handler won't result in the change
    // handler being called again.
    CHECK(change_handler_call_count == 1);
    change_handler_call_count = 0;
}

// ported from tst_qproperty.cpp: changePropertyFromWithinChangeHandler2() (line 776)
void change_property_from_within_change_handler2() {
    property_t property{ any_t(100) };
    int change_handler_call_count = 0;

    auto handler = property.on_value_changed([&]() {
        ++change_handler_call_count;
        property.set_value(any_t(property.value().get<int>() + 1));
    });

    CHECK(property.value().get<int>() == 100);

    property.set_value(any_t(42));
    CHECK(property.value().get<int>() == 43);
    CHECK(!property.has_binding());     // setting the value in the change handler removed the binding
}

// ported from tst_qproperty.cpp: notifiedProperty() (line 939)
// Qt's ClassWithNotifiedProperty derives from QObject and passes a notify callback to its
// Q_OBJECT_BINDABLE_PROPERTY. The test type above does the same through its constructor's
// subscription, so the trigger points are Qt's: every real value change, including a binding install
// whose first result differs, and the manual notify().
void notified_property() {
    class_with_notified_property instance;
    std::array<property_t, 5> other_properties{ property_t{ any_t(0) }, property_t{ any_t(0) },
                                                property_t{ any_t(0) }, property_t{ any_t(0) },
                                                property_t{ any_t(0) } };
    for (int i = 0; i < static_cast<int>(other_properties.size()); ++i) {
        other_properties[i].set_binding([&instance, i]() {
            return any_t(instance.property() + i + 1);
        });
    }

    auto check_dependants = [&] {
        const int val = instance.property();
        for (int i = 0; i < static_cast<int>(other_properties.size()); ++i)
            CHECK(other_properties[i].value().get<int>() == val + i + 1);
    };

    CHECK(instance.recorded_values.empty());
    check_dependants();

    instance.property_data.set_value(any_t(42));    // Qt: instance.property.setValue(42)
    CHECK(instance.recorded_values.size() == 1);
    CHECK(instance.recorded_values.at(0) == 42);
    instance.recorded_values.clear();
    check_dependants();

    instance.property_data.set_value(any_t(42));    // equal value: no callback
    CHECK(instance.recorded_values.empty());
    check_dependants();

    int subscribed_count = 0;
    property_t injected_value{ any_t(100) };
    instance.property_data.set_binding(
        [&injected_value]() { return any_t(injected_value.value().get<int>()); });
    auto subscriber = [&] { ++subscribed_count; };
    std::array<property_change_handler_t, 10> subscribers = {
            instance.property_data.subscribe(subscriber),
            instance.property_data.subscribe(subscriber),
            instance.property_data.subscribe(subscriber),
            instance.property_data.subscribe(subscriber),
            instance.property_data.subscribe(subscriber),
            instance.property_data.subscribe(subscriber),
            instance.property_data.subscribe(subscriber),
            instance.property_data.subscribe(subscriber),
            instance.property_data.subscribe(subscriber),
            instance.property_data.subscribe(subscriber)
    };

    CHECK(subscribed_count == 10);
    subscribed_count = 0;

    CHECK(instance.property() == 100);
    CHECK(instance.recorded_values.size() == 1);
    CHECK(instance.recorded_values.at(0) == 100);
    instance.recorded_values.clear();
    check_dependants();
    CHECK(subscribed_count == 0);

    injected_value.set_value(any_t(200));           // Qt: injectedValue = 200
    CHECK(instance.property() == 200);
    CHECK(instance.recorded_values.size() == 1);
    CHECK(instance.recorded_values.at(0) == 200);
    instance.recorded_values.clear();
    check_dependants();
    CHECK(subscribed_count == 10);
    subscribed_count = 0;

    injected_value.set_value(any_t(400));           // Qt: injectedValue = 400
    CHECK(instance.property() == 400);
    CHECK(instance.recorded_values.size() == 1);
    CHECK(instance.recorded_values.at(0) == 400);
    instance.recorded_values.clear();
    check_dependants();
    CHECK(subscribed_count == 10);
}

// ported from tst_qproperty.cpp: qobjectObservers() (line 1400)
// Qt's MyQObject exposes bindableFoo()/bindableBar() over Q_OBJECT_BINDABLE_PROPERTY members and
// setFoo()/setBar() write them; the assertions are the handler counts across those writes. The
// accessors are gone (M17 stage 2): the property-as-bindable is property_t::from().
void qobject_observers() {
    my_qobject object;
    object.listen();                                // the signal connections (Qt: the declaration)
    const meta_property_t foo_property = object.meta_object()->property("foo");
    CHECK(foo_property.is_valid());
    property_t foo = property_t::from(object, foo_property);      // Qt: object.bindableFoo()
    property_t bar = property_t::from(object, object.meta_object()->property("bar"));
    CHECK(foo.is_valid());
    CHECK(bar.is_valid());
    int on_value_changed_called = 0;
    {
        property_change_handler_t handler = foo.on_value_changed([&on_value_changed_called]() {
            ++on_value_changed_called;
        });
        CHECK(on_value_changed_called == 0);

        object.set_foo(10);                         // object.setFoo(10)
        CHECK(on_value_changed_called == 1);

        // Qt: object.bindableFoo().setBinding(object.bindableBar().makeBinding());
        // makeBinding() has no counterpart: the binding reads the other property's view directly.
        foo.set_binding([&bar]() { return any_t(bar.value().get<int>()); });
        CHECK(on_value_changed_called == 2);

        object.set_bar(42);                         // object.setBar(42)
        CHECK(on_value_changed_called == 3);
    }
    object.set_bar(0);
    CHECK(on_value_changed_called == 3);
}

// ported from tst_qproperty.cpp: notify() (line 2407)
void notify() {
    property_t test_property{ any_t(0) };
    std::vector<int> recorded_values;
    int value = 0;
    property_change_handler_t notifier;

    {
        property_change_handler_t handler = test_property.on_value_changed([&]() {
            recorded_values.push_back(test_property.value().get<int>());
        });
        notifier = test_property.on_value_changed([&]() {
            value = test_property.value().get<int>();
        });

        test_property.set_value(any_t(1));
        test_property.set_value(any_t(2));
    }
    CHECK(value == 2);
    test_property.set_value(any_t(3));
    CHECK(value == 3);
    notifier = {};
    test_property.set_value(any_t(4));
    CHECK(value == 3);

    CHECK(recorded_values.size() == 2);
    CHECK(recorded_values.at(0) == 1);
    CHECK(recorded_values.at(1) == 2);
}

// ported from tst_qproperty.cpp: multipleObservers() (line 870)
void multiple_observers() {
    property_t property{ any_t(0) };
    property.set_value(any_t(5));
    CHECK(property.value().get<int>() == 5);

    int value1 = 1;
    auto change_handler =
        property.on_value_changed([&]() { value1 = property.value().get<int>(); });
    CHECK(value1 == 1);

    int value2 = 2;
    auto subscribe_handler = property.subscribe([&]() { value2 = property.value().get<int>(); });
    CHECK(value2 == 5);

    property.set_value(any_t(6));
    CHECK(property.value().get<int>() == 6);
    CHECK(value1 == 6);
    CHECK(value2 == 6);

    property.set_binding([]() { return any_t(12); });
    CHECK(value1 == 12);
    CHECK(value2 == 12);
    CHECK(property.value().get<int>() == 12);

    // Qt: property.setBinding(QPropertyBinding<int>()) — an empty binding clears the installed one.
    property.clear_binding();
    CHECK(value1 == 12);
    CHECK(value2 == 12);
    CHECK(property.value().get<int>() == 12);

    property.set_value(any_t(22));
    CHECK(value1 == 22);
    CHECK(value2 == 22);
    CHECK(property.value().get<int>() == 22);
}

// ported from tst_qproperty.cpp: noDoubleNotification() (line 2222)
void no_double_notification() {
    /* dependency graph for this test
       x --> y means y depends on x
      a-->b-->d
      \       ^
       \->c--/
    */
    property_t a{ any_t(0) };
    property_t b{ any_t(0) };
    b.set_binding([&]() { return any_t(a.value().get<int>()); });
    property_t c{ any_t(0) };
    c.set_binding([&]() { return any_t(a.value().get<int>()); });
    property_t d{ any_t(0) };
    d.set_binding([&]() { return any_t(b.value().get<int>() + c.value().get<int>()); });
    int n_notifications = 0;
    int expected = 0;
    auto connection = d.subscribe([&]() {
        ++n_notifications;
        CHECK(d.value().get<int>() == expected);
    });
    CHECK(n_notifications == 1);
    expected = 2;
    a.set_value(any_t(1));
    CHECK(n_notifications == 2);
    expected = 4;
    a.set_value(any_t(2));
    CHECK(n_notifications == 3);
}

// ported from tst_qproperty.cpp: notifyAfterAllDepsGone() (line 2490)
void notify_after_all_deps_gone() {
    bool b = true;
    property_t iprop{ any_t(0) };
    property_t jprop{ any_t(42) };
    iprop.set_binding([&]() {
        if (b)
            return any_t(jprop.value().get<int>());
        return any_t(13);
    });
    int change_counter = 0;
    auto keep_alive = iprop.on_value_changed([&]() { ++change_counter; });
    CHECK(iprop.value().get<int>() == 42);
    jprop.set_value(any_t(44));
    CHECK(iprop.value().get<int>() == 44);
    CHECK(change_counter == 1);
    b = false;
    jprop.set_value(any_t(43));
    CHECK(iprop.value().get<int>() == 13);
    CHECK(change_counter == 2);
}

// ---------------------------------------------------------------------------
// Ported in part: value assertions ported, the missing API marked at its site.
// ---------------------------------------------------------------------------

// ported from tst_qproperty.cpp: multipleDependencies() (line 192)
void multiple_dependencies() {
    property_t first_dependency{ any_t(1) };
    property_t second_dependency{ any_t(2) };

    property_t sum{ any_t(0) };
    sum.set_binding([&]() {
        return any_t(first_dependency.value().get<int>() + second_dependency.value().get<int>());
    });

    // Qt's QCOMPARE(QPropertyBindingDataPointer::get(firstDependency).observerCount(), 1) after every
    // step (lines 200-223): one dependant edge per dependency, rewritten (not accumulated) by each
    // re-evaluation.
    CHECK(dependants(first_dependency) == 1);
    CHECK(dependants(second_dependency) == 1);

    CHECK(sum.value().get<int>() == 3);
    CHECK(dependants(first_dependency) == 1);
    CHECK(dependants(second_dependency) == 1);

    first_dependency.set_value(any_t(10));

    CHECK(sum.value().get<int>() == 12);
    CHECK(dependants(first_dependency) == 1);
    CHECK(dependants(second_dependency) == 1);

    second_dependency.set_value(any_t(20));

    CHECK(sum.value().get<int>() == 30);
    CHECK(dependants(first_dependency) == 1);
    CHECK(dependants(second_dependency) == 1);

    first_dependency.set_value(any_t(1));
    second_dependency.set_value(any_t(1));
    CHECK(sum.value().get<int>() == 2);
    CHECK(dependants(first_dependency) == 1);
    CHECK(dependants(second_dependency) == 1);
}

// ported from tst_qproperty.cpp: bindingAfterUse() (line 324)
void binding_after_use() {
    property_t prop_with_binding_later{ any_t(1) };

    property_t prop_that_uses_first_prop{ any_t(0) };
    prop_that_uses_first_prop.set_binding(
        [&]() { return any_t(prop_with_binding_later.value().get<int>()); });

    CHECK(prop_that_uses_first_prop.value().get<int>() == 1);
    // Qt line 332: the property gets its own binding afterwards, but the dependant edge stays.
    CHECK(dependants(prop_with_binding_later) == 1);

    property_t injected_value{ any_t(42) };
    prop_with_binding_later.set_binding(
        [&]() { return any_t(injected_value.value().get<int>()); });

    CHECK(prop_that_uses_first_prop.value().get<int>() == 42);
    CHECK(dependants(prop_with_binding_later) == 1);    // Qt line 338
}

// ported from tst_qproperty.cpp: avoidDependencyAllocationAfterFirstEval() (line 374)
void avoid_dependency_allocation_after_first_eval() {
    property_t first_dependency{ any_t(1) };
    property_t second_dependency{ any_t(10) };

    property_t prop_with_binding{ any_t(0) };
    prop_with_binding.set_binding([&]() {
        return any_t(first_dependency.value().get<int>() + second_dependency.value().get<int>());
    });

    CHECK(prop_with_binding.value().get<int>() == 11);

    // Qt: QVERIFY(QPropertyBindingDataPointer::get(propWithBinding).binding()) — the same fact as
    // has_binding().
    CHECK(prop_with_binding.has_binding());

    // Qt line 384: binding()->dependencyObserverCount == 2u. The read set is sized once and reused by
    // every re-evaluation, so the count is the same after the dependency change (line 388).
    CHECK(dependencies(prop_with_binding) == 2);

    first_dependency.set_value(any_t(100));
    CHECK(prop_with_binding.value().get<int>() == 110);
    CHECK(dependencies(prop_with_binding) == 2);
}

// ported from tst_qproperty.cpp: noDoubleCapture() (line 1578)
void no_double_capture() {
    property_t size{ any_t(3LL) };
    property_t max{ any_t(0) };
    max.set_binding([&size]() -> any_t {
        // each loop run attempts to capture size; the library de-duplicates the recording
        for (int i = 0; i < static_cast<int>(size.value().get<long long>()); ++i) { }
        return any_t(static_cast<int>(size.value().get<long long>()));
    });

    // Qt line 1589: QPropertyBindingPrivate::get(max.binding())->dependencyObserverCount == 1U — the
    // repeated read of `size` in the loop is one dependency, not one per read.
    CHECK(dependencies(max) == 1);

    size.set_value(any_t(4LL));   // should not crash
    CHECK(max.value().get<int>() == 4);
}

// ported from tst_qproperty.cpp: qobjectBindableReallocatedBindingStorage() (line 1284)
void qobject_bindable_reallocated_binding_storage() {
    // Qt's ReallocObject has four Q_OBJECT_BINDABLE_PROPERTY members; the storage reallocation itself
    // is never read (Qt asserts nothing about it): the whole function is `x = 1; QCOMPARE(v.value(),
    // 1)` on a value graph, which is exactly what the port asserts. The port's bindables are
    // standalone objects with no per-object storage to reallocate, so the reallocation trigger stays
    // the one Theme G fact this function does not reproduce — no assertion depends on it.
    realloc_object object;
    object.x.set_value(any_t(1));
    CHECK(object.v.value().get<int>() == 1);
}

// ported from tst_qproperty.cpp: groupedNotifications() (line 2196)
void grouped_notifications() {
    property_t a{ any_t(0) };
    property_t b{ any_t(0) };
    b.set_binding([&]() { return any_t(a.value().get<int>()); });
    property_t c{ any_t(0) };
    c.set_binding([&]() { return any_t(a.value().get<int>()); });
    property_t d{ any_t(0) };
    property_t e{ any_t(0) };
    e.set_binding([&]() {
        return any_t(b.value().get<int>() + c.value().get<int>() + d.value().get<int>());
    });
    int n_notifications = 0;
    int expected = 0;
    auto connection = e.subscribe([&]() {
        ++n_notifications;
        CHECK(e.value().get<int>() == expected);
    });
    CHECK(n_notifications == 1);

    expected = 2;
    {
        const property_update_group_t guard;          // Qt: const QScopedPropertyUpdateGroup guard;
        a.set_value(any_t(1));
        CHECK(b.value().get<int>() == 0);           // still stale inside the group
        CHECK(c.value().get<int>() == 0);
        CHECK(d.value().get<int>() == 0);
        CHECK(n_notifications == 1);
    }
    CHECK(b.value().get<int>() == 1);
    CHECK(c.value().get<int>() == 1);
    CHECK(e.value().get<int>() == 2);
    CHECK(n_notifications == 2);

    expected = 7;
    {
        const property_update_group_t guard;
        a.set_value(any_t(2));
        d.set_value(any_t(3));
        CHECK(b.value().get<int>() == 1);
        CHECK(c.value().get<int>() == 1);
        CHECK(d.value().get<int>() == 3);
        CHECK(n_notifications == 2);
    }
    CHECK(b.value().get<int>() == 2);
    CHECK(c.value().get<int>() == 2);
    CHECK(e.value().get<int>() == 7);
    CHECK(n_notifications == 3);
}

// ported from tst_qproperty.cpp: uninstalledBindingDoesNotEvaluate() (line 2325)
void uninstalled_binding_does_not_evaluate() {
    property_t i{ any_t(0) };
    property_t j{ any_t(0) };
    int binding_evaluation_counter = 0;
    i.set_binding([&]() {
        ++binding_evaluation_counter;
        return any_t(j.value().get<int>());
    });
    CHECK(binding_evaluation_counter == 1);

    // Sanity check: if we force a binding reevaluation,
    j.set_value(any_t(42));
    // the binding function will be called again. The library evaluates on read (Theme A), so read
    // the property here — that is the point where Qt has already re-evaluated it.
    CHECK(i.value().get<int>() == 42);
    CHECK(binding_evaluation_counter == 2);

    // If we keep referencing the binding
    property_binding_t kept_binding = i.binding();   // Qt: auto keptBinding = i.binding();
    // but have it not installed on a property
    i.set_value(any_t(10));
    CHECK(!i.has_binding());
    CHECK(!kept_binding.is_null());
    // then changing a dependency
    j.set_value(any_t(12));
    // does not lead to the binding being reevaluated.
    CHECK(binding_evaluation_counter == 2);
}

// ported from tst_qproperty.cpp: realloc() (line 700)
void realloc() {
    // Qt's first block (lines 702-711) installs four bindings onto prop5 and checks the resulting
    // value. Qt's ReallocTester uses Q_OBJECT_COMPAT_PROPERTY; the port's prop1..prop5 are plain
    // bindables, which is the same value graph. Qt's intent — "triggering a binding-data reallocation
    // does not crash" — has nothing to reallocate here (a bindable owns its state; nothing moves), and
    // Qt asserts nothing about the reallocation, only the value below.
    property_t prop1{ any_t(0) };
    property_t prop2{ any_t(0) };
    property_t prop3{ any_t(0) };
    property_t prop4{ any_t(0) };
    property_t prop5{ any_t(0) };
    prop1.set_binding([&]() { return any_t(prop5.value().get<int>()); });
    prop2.set_binding([&]() { return any_t(prop5.value().get<int>()); });
    prop3.set_binding([&]() { return any_t(prop5.value().get<int>()); });
    prop4.set_binding([&]() { return any_t(prop5.value().get<int>()); });
    prop5.set_binding([]() { return any_t(42); });
    CHECK(prop1.value().get<int>() == 42);

    // Qt's second block (lines 712-729): a change handler keeps working across the (absent)
    // reallocation. All three modification counts are asserted exactly (1, 2, 3), as in Qt.
    property_t bindable_prop1{ any_t(0) };
    property_t bindable_prop2{ any_t(0) };
    property_t bindable_prop3{ any_t(0) };
    property_t bindable_prop4{ any_t(0) };
    property_t bindable_prop5{ any_t(0) };
    int modification_count = 0;
    property_change_handler_t observer([&]() { ++modification_count; });
    observer.set_source(bindable_prop1);                        // Qt: bindableProp1().observe(&observer)
    bindable_prop1.set_value(any_t(12));                        // Qt: tester.setProp1(12)
    CHECK(modification_count == 1);
    CHECK(bindable_prop1.value().get<int>() == 12);

    bindable_prop1.set_binding([&]() { return any_t(bindable_prop5.value().get<int>()); });
    CHECK(modification_count == 2);
    bindable_prop2.set_binding([&]() { return any_t(bindable_prop5.value().get<int>()); });
    bindable_prop3.set_binding([&]() { return any_t(bindable_prop5.value().get<int>()); });
    bindable_prop4.set_binding([&]() { return any_t(bindable_prop5.value().get<int>()); });
    bindable_prop5.set_binding([]() { return any_t(42); });
    CHECK(modification_count == 3);
}

// ---------------------------------------------------------------------------
// Ported in full — the second half: Theme A/H counts, compat properties, and the
// read-only/declared-type/notify-on-install additions. The C design decisions and the D model
// limits are collected at the end of the file.
// ---------------------------------------------------------------------------

// ported from tst_qproperty.cpp: takeBinding() (line 404)
void take_binding() {
    property_binding_t existing_binding;
    CHECK(existing_binding.is_null());              // Qt: QVERIFY(existingBinding.isNull())

    property_t first{ any_t(100) };
    property_t second{ any_t(0) };
    // Qt: QProperty<int> second(Qt::makePropertyBinding(first)) — the binding is installed one
    // statement later (same asserted values).
    second.set_binding([&]() { return any_t(first.value().get<int>()); });

    CHECK(second.value().get<int>() == int(100));

    existing_binding = second.take_binding();
    CHECK(!existing_binding.is_null());

    first.set_value(any_t(10));
    CHECK(second.value().get<int>() == int(100));

    second.set_value(any_t(25));
    CHECK(second.value().get<int>() == int(25));

    second.set_binding(existing_binding);
    CHECK(second.value().get<int>() == int(10));
    CHECK(!existing_binding.is_null());
}

// ported from tst_qproperty.cpp: stickyBinding() (line 428)
void sticky_binding() {
    property_t prop{ any_t(0) };
    property_t prop2{ any_t(2) };
    prop.set_binding([&]() { return any_t(prop2.value().get<int>()); });
    CHECK(prop.value().get<int>() == 2);
    // Qt: QPropertyBindingPrivate::get(prop.binding()) — the handle shares the installed state.
    property_binding_t priv_binding = prop.binding();
    // If we make a binding sticky,
    priv_binding.set_sticky();
    // then writing to the property does not remove it
    prop.set_value(any_t(1));
    CHECK(prop.has_binding());                      // Qt: QVERIFY(prop.hasBinding())
    // but the value still changes.
    CHECK(prop.value().get<int>() == 1);
    // The binding continues to work normally.
    prop2.set_value(any_t(3));
    CHECK(prop.value().get<int>() == 3);
    // If we remove the stickiness
    priv_binding.set_sticky(false);
    // the binding goes away on the next write
    prop.set_value(any_t(42));
    CHECK(!prop.has_binding());
}

// ported from tst_qproperty.cpp: replaceBinding() (line 452)
void replace_binding() {
    property_t first{ any_t(100) };
    property_t second{ any_t(0) };
    second.set_binding([&]() { return any_t(first.value().get<int>()); });   // Qt: QProperty<int> second(Qt::makePropertyBinding(first));

    CHECK(second.value().get<int>() == 100);

    // Qt: auto constantBinding = Qt::makePropertyBinding([]() { return 42; });
    property_binding_t constant_binding([&]() { return 42; });
    // Qt's setBinding(constantBinding) hands back the replaced binding; the erased set_binding()
    // reports acceptance instead, so the replaced handle is taken out first — the same object, one
    // statement earlier.
    property_binding_t old_binding = second.take_binding();
    second.set_binding(constant_binding);
    CHECK(second.value().get<int>() == 42);

    second.set_binding(old_binding);
    CHECK(second.value().get<int>() == 100);
}

// ported from tst_qproperty.cpp: bindingSourceLocation() (line 581)
void binding_source_location() {
    // Qt guards this function with QT_PROPERTY_COLLECT_BINDING_LOCATION (which qproperty.h enables
    // whenever std::source_location is available) and QSKIPs otherwise; the library always records
    // the location, so the enabled branch is ported unconditionally.
    // Qt: auto binding = Qt::makePropertyBinding([]() { return 42; }); — the handle's constructor
    // records the location of the statement that constructs it.
    const std::uint32_t binding_line = std::source_location::current().line() + 1;
    property_binding_t binding([]() { return 42; });
    CHECK(binding.source_location().line() == binding_line);    // Qt: QCOMPARE(...->sourceLocation().line, bindingLine)
}

// ported from tst_qproperty.cpp: bindingError() (line 592)
void binding_error() {
    property_t prop{ any_t(0) };
    prop.set_binding([]() -> any_t {
        // Qt: QPropertyBindingPrivate::currentlyEvaluatingBinding()->setError(std::move(error));
        set_binding_error(property_binding_t_error_t{
            property_binding_t_error_t::kind_enum::UNKNOWN, "my error" });
        return any_t(0);
    });
    CHECK(prop.value().get<int>() == 0);
    CHECK(prop.binding().error().message == "my error");        // Qt: QCOMPARE(error().description(), QString("my error"))
}

// ported from tst_qproperty.cpp: bindingLoop() (line 629)
void binding_loop() {
    property_t first_prop{ any_t(0) };

    property_t second_prop{ any_t(0) };
    second_prop.set_binding([&]() -> any_t {
        return any_t(first_prop.value().get<int>());
    });

    property_t third_prop{ any_t(0) };
    third_prop.set_binding([&]() -> any_t {
        return any_t(second_prop.value().get<int>());
    });

    first_prop.set_binding([&]() -> any_t {
        return any_t(second_prop.value().get<int>() + third_prop.value().get<int>());
    });

    third_prop.set_value(any_t(10));
    // RE-EXPRESSED (Theme A): Qt's eager propagation evaluates firstProp's binding while
    // thirdProp.setValue(10) notifies, and that evaluation re-enters firstProp through secondProp.
    // The library evaluates on read, so this read is where the same evaluation (and the same loop
    // detection) happens. The asserted error is identical.
    (void)first_prop.value();
    CHECK(first_prop.binding().error().kind == property_binding_t_error_t::kind_enum::BINDING_LOOP);

    {
        property_t i{ any_t(0) };
        binding_loop_tester tester(&i);
        // RE-EXPRESSED (Theme A): the constructor's setBinding calls and the source write mark the
        // graph; the round-end flush is what performs the evaluations Qt's eager propagation does
        // immediately, and both bindings of the 2-cycle are then reported.
        flush_bindings();
        CHECK(tester.eager_data.binding().error().kind == property_binding_t_error_t::kind_enum::BINDING_LOOP);
        CHECK(tester.eager_data2.binding().error().kind == property_binding_t_error_t::kind_enum::BINDING_LOOP);
    }
    {
        binding_loop_tester tester;
        auto handler = tester.eager_data.on_value_changed([&]() {
            tester.eager_data.set_binding([]() { return 42; });
        });
        tester.eager_data.set_binding([]() { return 42; });
        CHECK(tester.eager_data.binding().error().kind == property_binding_t_error_t::kind_enum::BINDING_LOOP);
        CHECK(tester.eager_data.binding().error().message == "Binding set during binding evaluation!");
    }
}

// ported from tst_qproperty.cpp: changePropertyFromWithinChangeHandlerThroughDependency() (line 751)
void change_property_from_within_change_handler_through_dependency() {
    property_t source_property{ any_t(100) };
    property_t property{ any_t(0) };
    property.set_binding(
        [&]() { return any_t(source_property.value().get<int>()); });   // Qt: QProperty<int> property(Qt::makePropertyBinding(sourceProperty));
    bool reset_property_on_change = false;
    int change_handler_call_count = 0;

    auto handler = property.on_value_changed([&]() {
        ++change_handler_call_count;
        if (reset_property_on_change)
            source_property.set_value(any_t(100));
    });

    CHECK(property.value().get<int>() == 100);

    reset_property_on_change = true;
    source_property.set_value(any_t(42));
    CHECK(property.value().get<int>() == 100 || property.value().get<int>() == 42);
    CHECK(property.binding().error().kind == property_binding_t_error_t::kind_enum::BINDING_LOOP);
    // changing the property value inside the change handler won't result in the change
    // handler being called again.
    CHECK(change_handler_call_count == 1);
    change_handler_call_count = 0;
}

// ported from tst_qproperty.cpp: genericPropertyBinding() (line 811)
void generic_property_binding_t() {
    property_t property(any_t(0));          // Qt: QProperty<int> property

    // Qt: QUntypedPropertyBinding doubleBinding(QMetaType::fromType<double>(), ...) — the binding
    // declares double and the property is an int, so setBinding() refuses it (B2: Qt's
    // valueMetaType/metatype comparison, checked before the closure runs).
    CHECK(!property.set_binding([]() { return any_t(0.0); }, meta_type_t::from_type<double>()));
    CHECK(!property.has_binding());
    CHECK(property.value().get<int>() == 0);

    // Qt: QUntypedPropertyBinding intBinding(QMetaType::fromType<int>(), ... writes 100 through the
    // void* buffer ...) — the declared type matches, so the install is accepted and the evaluated
    // value is assigned.
    CHECK(property.set_binding([]() { return any_t(100); }, meta_type_t::from_type<int>()));

    CHECK(property.value().get<int>() == 100);
}

// ported from tst_qproperty.cpp: genericPropertyBindingBool() (line 838)
void generic_property_binding_t_bool() {
    // Qt builds a QUntypedPropertyBinding whose closure writes a bool through a raw `void*` buffer.
    // The port's untyped binding is an owning property_t plus a closure returning any_t: the same
    // "type-erased value written by the closure" shape (probe P1), asserted over bool.
    property_t property(any_t(false));

    CHECK(!property.value().get<bool>());

    CHECK(property.set_binding([]() { return any_t(true); }));

    CHECK(property.value().get<bool>());
}

// ported from tst_qproperty.cpp: quntypedBindableApi() (line 1114)
void quntyped_bindable_api() {
    property_t iprop(any_t(0));                 // Qt: QProperty<int> iprop
    // Qt wraps a QProperty in a QUntypedBindable; property_t IS the untyped handle here.
    CHECK(!iprop.has_binding());
    // Qt: QVERIFY(bindable.binding().isNull()) — the core holds no closure to hand out.
    CHECK(!iprop.core_handle()->binding);

    CHECK(iprop.set_binding([]() { return any_t(42); }));
    CHECK(iprop.has_binding());
    CHECK(static_cast<bool>(iprop.core_handle()->binding));    // Qt: !bindable.binding().isNull()
    CHECK(iprop.value().get<int>() == 42);

    // Qt: QUntypedPropertyBinding binding = bindable.takeBinding(); QVERIFY(!bindable.hasBinding());
    // M17 stage 3: take_binding() lives on property_t, so the erased take is the same call Qt makes.
    property_binding_t taken = iprop.take_binding();
    CHECK(!iprop.has_binding());
    CHECK(!taken.is_null());

    // Qt: bindable.setBinding(binding); QCOMPARE(iprop.value(), 42) — the taken binding is reinstalled.
    CHECK(iprop.set_binding(taken));
    CHECK(iprop.value().get<int>() == 42);

    // Qt: QUntypedBindable propLess; QVERIFY(propLess.takeBinding().isNull()).
    property_t prop_less;                       // the default (invalid) handle
    CHECK(prop_less.core_handle() == nullptr);  // nothing to take: Qt's null binding

    // Qt: an invalid bindable refuses setBinding() with a warning and installs nothing.
    property_t invalid_bindable;
    CHECK(!invalid_bindable.set_binding([]() { return any_t(0); }));

    // Qt: QUntypedBindable readOnlyBindable(static_cast<const QProperty<int>*>(&iprop));
    // QVERIFY(readOnlyBindable.isReadOnly()); the install is refused with a warning and the binding
    // that iprop holds stays installed.
    property_t read_only_bindable = iprop.as_read_only();
    CHECK(read_only_bindable.is_read_only());
    CHECK(!read_only_bindable.set_binding([]() { return any_t(7); }));
    CHECK(iprop.has_binding());
    CHECK(iprop.value().get<int>() == 42);

    // Qt: QProperty<float> fprop; bindable.setBinding(Qt::makePropertyBinding(fprop)); — the property
    // expects int, the binding declares float, so the install is refused (B2).
    property_t fprop(any_t(0.5f));
    CHECK(!iprop.set_binding([&fprop]() { return any_t(fprop.value().get<float>()); },
                             meta_type_t::from_type<float>()));
    CHECK(iprop.has_binding());
    CHECK(iprop.value().get<int>() == 42);
}

// ported from tst_qproperty.cpp: readonlyConstQBindable() (line 1150)
void readonly_const_q_bindable() {
    property_t i(any_t(42));
    const property_t bindable_i = i.as_read_only();     // Qt: const QBindable<int> bindableI(...)

    // check that read-only operations work with a const QBindable
    CHECK(bindable_i.is_read_only());
    CHECK(!bindable_i.has_binding());
    // we can still create a binding to a read only bindable through the interface
    property_t j{ any_t(0) };
    // Qt: j.setBinding(bindableI.makeBinding()) — makeBinding() is a closure reading the bindable.
    j.set_binding([&bindable_i]() { return any_t(bindable_i.value().get<int>()); });
    CHECK(j.has_binding());
    CHECK(j.value().get<int>() == bindable_i.value().get<int>());
    int counter = 0;
    auto observer = bindable_i.subscribe([&] { ++counter; });
    CHECK(counter == 1);
    auto observer2 = bindable_i.on_value_changed([&] { ++counter; });
    i.set_value(any_t(0));
    CHECK(counter == 3);
}

// ported from tst_qproperty.cpp: qobjectBindableManualNotify() (line 1231)
void qobject_bindable_manual_notify() {
    // Given an object of type MyQObject,
    my_qobject object;
    object.listen();                                // the signal connections (Qt: the declaration)
    // track its foo property's change count,
    // Qt: QMetaProperty fooProp = object.metaObject()->property(fooIndex); fooProp.bindable(&object).
    property_t foo = property_t::from(object, object.meta_object()->property("foo"));
    CHECK(foo.is_valid());
    int foo_change_count = 0;
    property_change_handler_t change_handler = foo.on_value_changed([&] {
        ++foo_change_count;
    });
    // and how many changed signals it emits (Qt: QSignalSpy fooChangedSpy).
    int foo_changed_spy = 0;
    object.connect("foo_changed(int) const", [&](int value) { ++foo_changed_spy; });

    // If we bypass the bindings system,
    object.foo_data.set_value_bypassing_bindings(42);
    // there is no change.
    CHECK(foo_change_count == 0);
    CHECK(foo_changed_spy == 0);
    // Once we notify manually
    object.foo_data.notify();
    // observers are notified and the signal arrives.
    CHECK(foo_change_count == 1);
    CHECK(foo_changed_spy == 1);

    // If we set a binding. Qt captures a plain `int i = 1` (a non-property): reading it records no
    // dependency, which is exactly what the later "changing a non-property" assertion relies on.
    int i = 1;
    object.foo_data.set_binding([&i]() { return i; });
    // then the value changes
    CHECK(object.foo() == 1);
    // and the change and signal count are incremented.
    CHECK(foo_change_count == 2);
    CHECK(foo_changed_spy == 2);
    // Changing a non-property won't trigger any notification.
    i = 2;
    CHECK(foo_change_count == 2);
    CHECK(foo_changed_spy == 2);
    // Manually triggering the notification
    object.foo_data.notify();
    // increments the change count
    CHECK(foo_change_count == 3);
    CHECK(foo_changed_spy == 3);
    // but doesn't actually cause a binding reevaluation.
    CHECK(object.foo() == 1);
}

// ported from tst_qproperty.cpp: qobjectBindableSignalTakingNewValue() (line 1291)
void qobject_bindable_signal_taking_new_value() {
    // Given an object of type MyQObject,
    my_qobject object;
    object.listen();                                // the signal connections (Qt: the declaration)
    // and tracking the values emitted via its fooChanged signal,
    int new_value = -1;
    object.connect("foo_changed(int) const", [&](int value) {
        new_value = value;
    });

    // when we change the property's value via the property view,
    property_t foo = property_t::from(object, object.meta_object()->property("foo"));
    foo.set_value(any_t(1));
    // we obtain the newly set value.
    CHECK(new_value == 1);

    // The same holds true when we set a binding
    property_t i{ any_t(2) };
    // Qt: object.bindableFoo().setBinding(Qt::makePropertyBinding(i)) — no free counterpart, so the
    // binding is installed on the property view, reading i.
    foo.set_binding([&i]() { return any_t(i.value().get<int>()); });
    CHECK(new_value == 2);
    // and when the binding gets reevaluated to a new value
    i.set_value(any_t(3));
    // The view publishes eagerly at the end of the dependency's notification pass (reference backend),
    // so the signal arrives without anyone reading the handle.
    CHECK(new_value == 3);
}

// ported from tst_qproperty.cpp: testNewStuff() (line 1313)
// The accessors this function used are gone (M17 stage 2). It keeps naming the property DATA MEMBERS,
// which is exactly what the deleted `bindableFoo()` returned: the erased API this function exercises
// (property_binding_t handles, property_t::value()) is the counterpart of the typed one, so the
// reference view is covered by qobjectObservers / qobjectBindableManualNotify /
// qobjectBindableSignalTakingNewValue / metaProperty as well.
void test_new_stuff() {
    // Qt's first block: the READ-ONLY view of the property refuses the install (with a warning) and
    // still reports a binding installed through the writable interface; isReadOnly() distinguishes the
    // two handles. B1 is the read-only flag on the handle.
    my_qobject test_read_only;
    property_t writable_foo = property_t::from(test_read_only, test_read_only.meta_object()->property("foo"));
    property_t read_only_foo = writable_foo.as_read_only();
    CHECK(read_only_foo.is_read_only());
    CHECK(!read_only_foo.set_binding([]() { return any_t(42); }));    // Qt: refused + warning
    CHECK(!read_only_foo.has_binding());

    // Qt reads the same property through the const bindable afterwards and finds a binding: the port's
    // read-only copy shares that core, so the install Qt's first line attempts is done through the
    // writable handle here and the read-only one still reports it.
    CHECK(!writable_foo.is_read_only());
    CHECK(writable_foo.set_binding([]() { return any_t(42); }));
    CHECK(read_only_foo.has_binding());                     // Qt: QVERIFY(bindable.hasBinding())
    CHECK(read_only_foo.value().get<int>() == 42);

    my_qobject object;
    object.listen();                                // the signal connections (Qt: the declarations)
    CHECK(!property_t::from(object, object.meta_object()->property("foo")).is_read_only());

    CHECK(object.foo_changed_count == 0);
    object.set_foo(10);
    CHECK(object.foo_changed_count == 1);
    CHECK(object.foo() == 10);

    CHECK(object.bar_changed_count == 0);
    object.set_bar(42);
    CHECK(object.bar_changed_count == 1);
    CHECK(object.foo_changed_count == 1);
    // Qt: object.fooData.setBinding(f) with f reading object.barData. The closure reads the property.
    object.foo_data.set_binding([&object]() { return any_t(object.bar_data.value().get<int>()); });
    CHECK(object.foo_changed_count == 2);
    CHECK(object.foo_data.value().get<int>() == 42);
    object.set_bar(666);
    CHECK(object.foo_changed_count == 3);
    CHECK(object.bar_changed_count == 2);
    CHECK(object.foo_data.value().get<int>() == 666);
    CHECK(object.foo_changed_count == 3);

    // Qt: object.bindableFoo().setBinding(Qt::makePropertyBinding(f2)) with f2 = barData / 2.
    object.foo_data.set_binding(
        [&object]() { return any_t(object.bar_data.value().get<int>() / 2); });
    CHECK(object.foo_data.has_binding());           // Qt: QVERIFY(object.bindableFoo().hasBinding())
    CHECK(object.foo() == 333);
    // Qt: object.bindableFoo().setBinding(QPropertyBinding<int>()) — an empty binding clears it and
    // hands back the replaced one. The erased set_binding() reports acceptance instead, so the
    // replaced handle is taken out first (the same object, one statement earlier).
    property_binding_t old_binding = object.foo_data.take_binding();
    CHECK(!object.foo_data.has_binding());
    CHECK(!old_binding.is_null());
    CHECK(object.foo() == 333);
    object.set_bar(222);
    CHECK(object.foo() == 333);
    object.foo_data.set_binding(old_binding);
    CHECK(object.foo() == 111);

    // Qt: auto b = object.bindableRead().makeBinding(); object.bindableFoo().setBinding(b). The
    // makeBinding() result is a closure reading the bindable, installed the same way.
    object.foo_data.set_binding([&object]() { return any_t(object.read_data.value().get<int>()); });
    CHECK(object.foo() == 0);
    object.read_data.set_value(any_t(10));
    CHECK(object.foo() == 10);

    CHECK(object.computed() == 10);
    object.read_data.set_value(any_t(42));
    CHECK(object.computed() == 42);

    // Qt: object.bindableBar().setBinding(object.bindableComputed().makeBinding()).
    object.bar_data.set_binding(
        [&object]() { return any_t(object.computed_data.value().get<int>()); });
    CHECK(object.computed() == 42);
    object.read_data.set_value(any_t(111));
    CHECK(object.computed() == 111);

    // Qt: object.bindableComputed().setBinding(object.bindableBar().makeBinding()).
    object.computed_data.set_binding(
        [&object]() { return any_t(object.bar_data.value().get<int>()); });
    CHECK(object.computed() == 111);
    // RE-EXPRESSED: Qt's computedData is a Q_OBJECT_COMPUTED_PROPERTY, whose bindable setter has no
    // storage to write; here the computed property's data member is an ordinary property the getter
    // does not read, so writing it cannot change computed(). The assertion is the same.
    object.computed_data.set_value(any_t(10));
    CHECK(object.computed() == 111);

    CHECK(object.foo_data.value().get<int>() == 111);
    object.foo_data.set_value(any_t(24));
    CHECK(object.foo() == 24);

    // Qt: QtPrivate::isAnyBindingEvaluating().
    const auto is_currently_evaluating_binding = []() { return is_binding_evaluating(); };
    CHECK(!is_currently_evaluating_binding());
    property_t evaluation_detector{ any_t(false) };
    evaluation_detector.set_binding([]() { return any_t(is_binding_evaluating()); });
    CHECK(evaluation_detector.value().get<bool>());
    CHECK(!is_currently_evaluating_binding());
}

// ported from tst_qproperty.cpp: compatBindings() (line 1423)
void compat_bindings() {
    my_qobject object;
    object.listen();                                // Qt's three connect() calls (foo, bar, compat)

    CHECK(object.compat_data.value().get<int>() == 0);   // Qt: QCOMPARE(object.compatData, 0)
    // setting data through the private interface should not call the changed signal or the public setter
    object.compat_data.set_value(any_t(10));
    CHECK(object.compat_changed_count == 0);
    CHECK(object.set_compat_called == 0);
    // going through the public API should emit the signal
    object.set_compat(42);
    CHECK(object.compat_changed_count == 1);
    CHECK(object.set_compat_called == 1);

    // setting the same value again does nothing
    object.set_compat(42);
    CHECK(object.compat_changed_count == 1);
    CHECK(object.set_compat_called == 1);

    object.set_foo(111);
    // just setting the binding. For a compat property, this should already trigger evaluation
    // Qt: object.compatData.setBinding(object.bindableFoo().makeBinding()). The port installs on the
    // compatibility property's VIEW, whose writes go through setCompat() — installing on the backing
    // member would bypass the setter, which is the one Thing Qt's compat layer adds.
    property_t compat = property_t::from(object, object.meta_object()->property("compat"));
    CHECK(compat.set_binding([&object]() { return any_t(object.foo_data.value()); }));
    CHECK(object.compat_data.value().get<int>() == 111); // Qt: compatData.valueBypassingBindings()
    CHECK(object.compat_changed_count == 2);
    CHECK(object.set_compat_called == 2);

    CHECK(object.compat() == 111);
    CHECK(object.compat_changed_count == 2);
    CHECK(object.set_compat_called == 2);

    object.set_foo(666);
    CHECK(object.compat_data.value().get<int>() == 666);
    CHECK(object.compat_changed_count == 3);
    CHECK(object.set_compat_called == 3);

    CHECK(object.compat() == 666);
    CHECK(object.compat_changed_count == 3);
    CHECK(object.set_compat_called == 3);

    object.set_foo(-42);
    CHECK(object.compat_changed_count == 4);
    CHECK(object.set_compat_called == 4);

    CHECK(object.compat() == 0);
    CHECK(object.compat_changed_count == 4);
    CHECK(object.set_compat_called == 4);

    object.set_compat(0);
    CHECK(object.compat() == 0);
    CHECK(object.compat_changed_count == 4);
    CHECK(object.set_compat_called == 4);
}

// ported from tst_qproperty.cpp: metaProperty() (line 1479)
void meta_property() {
    my_qobject object;
    object.listen();                                // the signal connections (Qt: the notify hooks)
    CHECK(object.foo_changed_count == 0);
    object.set_foo(10);
    CHECK(object.foo_changed_count == 1);
    CHECK(object.foo() == 10);

    // Qt: auto f = [&object]() -> int { return object.barData; };
    CHECK(object.bar_changed_count == 0);
    object.set_bar(42);
    CHECK(object.bar_changed_count == 1);
    CHECK(object.foo_changed_count == 1);

    // Qt: int fooIndex = object.metaObject()->indexOfProperty("foo"); QMetaProperty fooProp = ...
    const meta_property_t foo_property = object.meta_object()->property("foo");
    CHECK(foo_property.is_valid());
    // Qt: auto fooBindable = fooProp.bindable(&object); QVERIFY(fooBindable.isValid());
    property_t foo_bindable = property_t::from(object, foo_property);
    CHECK(foo_bindable.is_valid());
    // Qt: QVERIFY(fooBindable.isBindable()) — QUntypedBindable's "does this name a bindable", which is
    // this handle's is_valid(). The meta-level answer is value-derived: bindable iff notifiable
    // (M17 stage 3), and is_notifiable() is that query — QMetaProperty::isBindable() is not ported
    // separately, and `foo` has a notifier.
    CHECK(foo_property.is_notifiable());
    CHECK(!foo_bindable.has_binding());
    // Qt: fooBindable.setBinding(Qt::makePropertyBinding(f)) — makePropertyBinding() has no
    // counterpart, so the binding is installed through the handle and reads the other property's data.
    foo_bindable.set_binding([&object]() { return object.bar_data.value(); });
    CHECK(foo_bindable.has_binding());
    CHECK(object.foo_changed_count == 2);
    CHECK(object.foo_data.value().get<int>() == 42);
    object.set_bar(666);
    // The view publishes eagerly at the end of the dependency's notification pass (reference backend),
    // so the notifier and the property's own data are already up to date here — no read of the handle.
    CHECK(object.foo_changed_count == 3);
    CHECK(object.bar_changed_count == 2);
    CHECK(object.foo_data.value().get<int>() == 666);
    CHECK(object.foo_changed_count == 3);

    // Qt: fooBindable.setBinding(QUntypedPropertyBinding()) — clearing through the handle.
    foo_bindable.clear_binding();
    CHECK(!foo_bindable.has_binding());
    CHECK(object.foo_data.value().get<int>() == 666);

    object.set_bar(0);
    CHECK(object.foo_data.value().get<int>() == 666);
    object.set_foo(1);
    CHECK(object.foo_data.value().get<int>() == 1);
}

// ported from tst_qproperty.cpp: modifyObserverListWhileIterating() (line 1526)
void modify_observer_list_while_iterating() {
    // Qt's DestructingObserver is a raw QPropertyObserver whose callback destroys an observer —
    // itself in the first block, the NEXT one in the second. The library's counterpart is the
    // documented contract of property_change_handler_t: the notification iterates a snapshot and
    // skips a node whose is_bound() turned false, so destroying a handle (even the running one)
    // from inside a callback is safe.
    {
        // observer deletes itself while running the notification
        // while explicitly calling the destructor is rather unusual
        // it is completely plausible for this to happen because the object to which a
        // propertyobserver belongs has been destroyed
        property_t prop{ any_t(0) };
        int counter = 0;
        std::unique_ptr<property_change_handler_t> observer;

        observer = std::make_unique<property_change_handler_t>(
            prop.on_value_changed([&]() {
                ++counter;
                observer.reset();
            }));
        prop.set_value(any_t(42));     // should not crash
        CHECK(counter == 1);
        CHECK(!observer);
    }
    {
        // observer deletes the next observer in the list
        property_t prop{ any_t(0) };
        int counter = 0;
        std::unique_ptr<property_change_handler_t> first;
        std::unique_ptr<property_change_handler_t> second;
        first = std::make_unique<property_change_handler_t>(prop.on_value_changed([&]() {
            ++counter;
            second.reset();
        }));
        second = std::make_unique<property_change_handler_t>(prop.on_value_changed([&]() {
            ++counter;
            first.reset();
        }));
        prop.set_value(any_t(42));     // should not crash
        CHECK(counter == 1);    // only one trigger runs as the other has been deleted
        CHECK(!second);         // the peer was destroyed from the running callback
        CHECK(first != nullptr); // ... and the callback that would have destroyed this one never ran
    }
}

// ported from tst_qproperty.cpp: compatPropertyNoDobuleNotification() (line 1661)
void compat_property_no_dobule_notification() {
    compat_property_tester tester;
    int counter = 0;
    property_t iprop{ any_t(1) };
    // Qt: tester.bindableProp1().setBinding([&]() -> int {return iprop;}).
    property_t prop1 = property_t::from(tester, tester.meta_object()->property("prop1"));
    CHECK(prop1.set_binding([&iprop]() { return iprop.value(); }));
    auto observer = prop1.on_value_changed([&]() { ++counter; });
    iprop.set_value(any_t(2));
    CHECK(counter == 1);            // exactly one notification: the data write, not write + signal
}

// ported from tst_qproperty.cpp: compatPropertySignals() (line 1672)
void compat_property_signals() {
    compat_property_tester tester;

    // Compat property with signal. Signal has parameter.
    // Qt: QProperty<int> prop2Observer; prop2Observer.setBinding(tester.bindableProp2().makeBinding())
    // — the makeBinding() closure reads the property through its bindable; the port's binding reads
    // the property_t view, which is the property's value (the data member).
    property_t prop2_view = property_t::from(tester, tester.meta_object()->property("prop2"));
    property_t prop2_observer{ any_t(0) };
    prop2_observer.set_binding([&prop2_view]() { return prop2_view.value().get<int>(); });

    int prop2_spy = 0;
    int prop2_argument = -1;
    tester.connect("prop2_changed(int) const", [&](int value) {
        ++prop2_spy;
        // Qt: arguments.size() == 1, arguments.at(0).metaType().id() == QMetaType::Int, .toInt() ==
        // The declared signature is (int): the typed connect() matches it, so the callable receives
        // the value directly and no argument arithmetic appears here at all.
        prop2_argument = value;
    });

    tester.set_prop2(10);

    CHECK(prop2_observer.value().get<int>() == 10);
    CHECK(prop2_spy == 1);
    CHECK(prop2_argument == 10);

    // Compat property with signal and default value. Signal has no parameter.
    property_t prop3_view = property_t::from(tester, tester.meta_object()->property("prop3"));
    property_t prop3_observer{ any_t(0) };
    prop3_observer.set_binding([&prop3_view]() { return any_t(prop3_view.value().get<int>()); });
    CHECK(prop3_observer.value().get<int>() == 1);

    int prop3_spy = 0;
    tester.connect("prop3_changed() const", [&]() { ++prop3_spy; });

    tester.set_prop3(5);

    CHECK(prop3_observer.value().get<int>() == 5);
    CHECK(prop3_spy == 1);

    // Compat property with signal, default value, and custom setter. Signal has parameter.
    property_t prop4_view = property_t::from(tester, tester.meta_object()->property("prop4"));
    property_t prop4_observer{ any_t(0) };
    prop4_observer.set_binding([&prop4_view]() { return any_t(prop4_view.value().get<int>()); });
    CHECK(prop4_observer.value().get<int>() == 42);        // the getter maps the default 0 to 42

    // Qt's QSignalSpy takes the one queued emission each time; the port counts cumulatively, so each
    // checkpoint adds exactly one over the previous one.
    int prop4_spy = 0;
    int prop4_argument = -1;
    tester.connect("prop4_changed(int) const", [&](int value) {
        ++prop4_spy;
        prop4_argument = value;
    });

    tester.set_prop4(10);

    CHECK(prop4_observer.value().get<int>() == 10);
    CHECK(prop4_spy == 1);
    CHECK(prop4_argument == 10);

    tester.set_prop4(42);

    CHECK(prop4_observer.value().get<int>() == 42);
    CHECK(prop4_spy == 2);
    CHECK(prop4_argument == 42);

    tester.set_prop4(0);

    CHECK(prop4_observer.value().get<int>() == 42);        // 0 is reported as 42 again: no value change
    CHECK(prop4_spy == 3);
    CHECK(prop4_argument == 42);                // the signal carries the GETTER's value
}

// ported from tst_qproperty.cpp: noFakeDependencies() (line 1770)
void no_fake_dependencies() {
    fake_dependency_creator fdc;                    // the notify subscriptions live in its constructor
    int binding_function_called = 0;
    // Qt: fdc.bindableProp1().setBinding([&]() -> int {++bindingFunctionCalled; return fdc.prop2();});
    // The deleted accessor returned prop1_data, so the binding goes on the property's own backend
    // (the closure returns the erased value directly).
    fdc.prop1_data.set_binding([&]() {
        ++binding_function_called;
        return any_t(fdc.prop2());
    });
    fdc.set_prop2(42);
    CHECK(fdc.prop1() == 42);                       // basic binding works

    int slot_counter = 0;
    // Qt: QObject::connect(&fdc, &FakeDependencyCreator::prop1Changed, &fdc, [&](){ (void) fdc.prop3(); ++slotCounter;});
    fdc.connect("prop1_changed() const", [&]() {
        (void)fdc.prop3();                          // the read the test is about
        ++slot_counter;
    });
    fdc.set_prop2(13);
    CHECK(slot_counter == 1);                       // sanity check
    int old = binding_function_called;
    fdc.set_prop3(100);
    // The read of prop3 made from the signal handler did NOT become a dependency of prop1's binding:
    // the signal is emitted from a change handler, which runs after the evaluation has finished (no
    // binding context is installed then), so writing prop3 cannot re-run prop1's closure.
    CHECK(old == binding_function_called);
}

// ported from tst_qproperty.cpp: threadSafety2() (line 2087)
void thread_safety2() {
    // The worker thread creates an object plus a child, moves the subtree back to the main thread and
    // hands it over; the main thread asserts the names and the child list. Qt asserts nothing about
    // bindings here (its comment about the binding storage travelling is not asserted), so the
    // library's object tree is all this needs.
    //
    // The library's main thread is not started through a thread_t object, but every thread that has
    // data adopts an identity: object_t::thread() of an object created here is the main thread's
    // public identity (unlike Qt, thread_t::current_thread() still answers nullptr for it).
    object_t main_thread_probe;
    auto* main_thread = main_thread_probe.thread();

    std::unique_ptr<object_t> moved_object;
    {
        std::atomic<object_t*> transferred{ nullptr };
        property_using_thread_t worker_thread;
        worker_thread.dest = &transferred;
        worker_thread.dest_thread = main_thread;
        worker_thread.start();
        // Qt: workerThread.quit() + wait(). Nothing is posted to the worker here, so its run()
        // returns on its own and there is no loop to quit. The spin waits for the hand-over (Qt's
        // wait() does the same job); the wait() after it joins the OS thread.
        while (transferred.load(std::memory_order_acquire) == nullptr) {
            std::this_thread::yield();
        }
        moved_object.reset(transferred.load(std::memory_order_acquire));
        worker_thread.wait();
    }

    CHECK(moved_object != nullptr);
    CHECK(moved_object->object_name() == "test");
    CHECK(moved_object->children().size() == 1);
    CHECK(moved_object->children()[0]->object_name() == "child");   // Qt: children().first()
}

// ported from tst_qproperty.cpp: groupedNotificationConsistency() (line 2246)
void grouped_notification_consistency() {
    property_t i{ any_t(0) };
    property_t j{ any_t(0) };
    bool are_equal = true;

    auto observer = i.on_value_changed([&]() {
        are_equal = i.value().get<int>() == j.value().get<int>();   // Qt: areEqual = i == j
    });

    i.set_value(any_t(1));
    j.set_value(any_t(1));
    CHECK(!are_equal);                              // value changed runs before j = 1

    {
        const property_update_group_t guard;
        i.set_value(any_t(2));
        j.set_value(any_t(2));
    }
    CHECK(are_equal);                               // value changed runs after everything has been evaluated
}

// ported from tst_qproperty.cpp: bindingGroupBindingDeleted() (line 2300)
void binding_group_binding_deleted() {
    // Qt's ClassWithNotifiedProperty is a bindable property plus its notify callback; here the
    // callback is the Theme B handle the instance registers (notified_property()).
    struct class_with_notified_property {
        property_t property{ any_t(0) };
        property_change_handler_t notifier;
    };

    auto deleter = std::make_unique<class_with_notified_property>();
    auto to_be_deleted = std::make_unique<class_with_notified_property>();

    bool called_handler = false;
    deleter->property.set_binding([&]() {
        int new_value = to_be_deleted->property.value().get<int>();
        if (new_value == 42)
            to_be_deleted.reset();                  // the dependency dies inside the evaluation
        return any_t(new_value);
    });
    auto handler = to_be_deleted->property.on_value_changed([&]() { called_handler = true; });
    {
        // Qt: Qt::beginPropertyUpdateGroup() + qScopeGuard([](){ Qt::endPropertyUpdateGroup(); });
        begin_property_update_group_t();
        to_be_deleted->property.set_value(any_t(42));
        // ASAN should not complain here
        end_property_update_group_t();
    }
    CHECK(!to_be_deleted);
    // the change notification is sent, even if the binding is deleted during evaluation
    CHECK(called_handler);
}

// ported from tst_qproperty.cpp: bindableInterfaceOfCompatPropertyUsesSetter() (line 2381)
void bindable_interface_of_compat_property_uses_setter() {
    my_qobject obj;
    // Qt: QBindable<int> bindable = obj.bindableCompat() — the compat property's bindable interface.
    property_t bindable = property_t::from(obj, obj.meta_object()->property("compat"));
    CHECK(bindable.is_valid());
    CHECK(obj.set_compat_called == 0);
    CHECK(bindable.set_value(any_t(42)));       // a view write goes through the property's setter
    CHECK(obj.set_compat_called == 1);
}

// ported from tst_qproperty.cpp: selfBindingShouldNotCrash() (line 2390)
void self_binding_should_not_crash() {
    property_t i{ any_t(0) };
    i.set_binding([&]() { return any_t(i.value().get<int>() + 1); });   // Qt: i.setBinding([&](){ return i+1; });
    CHECK(i.binding().error().has_error());                // Qt: QVERIFY(i.binding().error().hasError())
}

// Qt's qpropertyAlias() (tst_qproperty.cpp line 2397) is NOT ported: QPropertyAlias was deprecated
// in Qt 6.6 and the class this port used to expose (property_alias) had different semantics — Qt's
// alias observer fires when the property's BINDING changes, the port's handler fired on a value
// change.

// ported from tst_qproperty.cpp: scheduleNotify() (line 2419)
void schedule_notify() {
    int notifications = 0;
    property_t p{ any_t(0) };
    CHECK(p.value().get<int>() == 0);
    const auto handler = p.on_value_changed([&] { ++notifications; });
    CHECK(notifications == 0);
    // Qt: QPropertyBinding<int> b([]() { return 0; }); QPropertyBindingPrivate::get(b)->scheduleNotify();
    property_binding_t b([]() { return 0; });
    b.schedule_notify();
    CHECK(notifications == 0);
    p.set_binding(b);
    CHECK(notifications == 1);                      // B3: the equal-value install notifies once
    CHECK(p.value().get<int>() == 0);
    CHECK(!b.notify_scheduled());                   // the install consumed the request
}

// ported from tst_qproperty.cpp: propertyUpdateViaSignaledProperty() (line 2524)
void property_update_via_signaled_property() {
    test_object_t o;
    property_t root_trigger{ any_t(0) };
    property_t signal_trigger{ any_t(0) };

    // Qt: o.bindable1Bindable().setBinding([&]() { return rootTrigger.value(); }).
    property_t bindable1 = property_t::from(o, o.meta_object()->property("bindable1"));
    CHECK(bindable1.set_binding(
        [&root_trigger]() { return any_t(root_trigger.value().get<int>()); }));

    o.connect("bindable1_changed() const", [&]() {
        // Signaled changes only once, doesn't actually depend on bindable1.
        // In reality, there could be some complicated calculation behind this that changes
        // on certain checkpoints, but not on every iteration.
        o.set_signaled(40);
    });

    // Qt: o.bindable2Bindable().setBinding([&]() { return signalTrigger.value() - o.bindable1(); }).
    // bindable1() is the getter over the property's bindable member: reading it records an owning
    // dependency, exactly like Qt's QPropertyData read inside a binding.
    property_t bindable2 = property_t::from(o, o.meta_object()->property("bindable2"));
    CHECK(bindable2.set_binding([&o, &signal_trigger]() {
        return any_t(signal_trigger.value().get<int>() - o.bindable1());
    }));

    o.connect("signaled_changed() const", [&]() {
        signal_trigger.set_value(any_t(o.signaled()));
    });

    root_trigger.set_value(any_t(2));
    CHECK(o.bindable1() == 2);
    CHECK(o.bindable2() == 38);
    root_trigger.set_value(any_t(3));
    CHECK(o.bindable1() == 3);
    CHECK(o.bindable2() == 37);
    root_trigger.set_value(any_t(4));
    CHECK(o.bindable1() == 4);
    CHECK(o.bindable2() == 36);
}

// ported from tst_qproperty.cpp: derefFromObserver() (line 2560)
void deref_from_observer() {
    int triggered = 0;
    property_t source{ any_t(11) };

    dtor_counter::counter = 0;
    dtor_counter counter;

    property_t target{ any_t(0) };
    target.set_binding([&triggered, &source, counter]() mutable {
        counter.should_increment = true;
        return any_t(++triggered + source.value().get<int>());
    });
    CHECK(triggered == 1);

    {
        // Qt: propObserverPtr.setBindingToNotify(QPropertyBindingPrivate::get(target.binding())) and a
        // QBindingObserverPtr keep the REPLACED binding object alive past the rebind. In the new model
        // a binding handle IS that kept-alive object: take_binding() hands the detached binding back,
        // holding its moved closure and shared state.
        CHECK(triggered == 1);
        source.set_value(any_t(25));
        CHECK(target.value().get<int>() == 27);     // Theme A: the read performs the recompute Qt does eagerly
        CHECK(triggered == 2);

        property_binding_t kept = target.take_binding();      // the replaced binding
        target.set_binding([]() { return any_t(8); });

        CHECK(target.value().get<int>() == 8);

        // The handle still holds the binding. (Do not also call target.binding(): that copies the
        // closure, and the counter would reach 2.)
        CHECK(dtor_counter::counter == 0);

        kept = property_binding_t();
        CHECK(dtor_counter::counter == 1);
    }

    source.set_value(any_t(26));
    CHECK(triggered == 2);
    CHECK(target.value().get<int>() == 8);
}
// ---------------------------------------------------------------------------
// Still out of scope: one design decision (C) and the model limits (D). Each keeps its marker;
// qpropertyAlias() is a third out-of-scope entry.
// (dependencyChangeDuringDestruction used to be the fourth entry; it is ported in full above now that
// object_t emits destroyed().)
// ---------------------------------------------------------------------------

// ported from tst_qproperty.cpp: inheritQUntypedPropertyData() (line 135)
void inherit_q_untyped_property_data() {
    // OUT OF SCOPE (D):
    // Qt asserts (at compile time and run time) that QUntypedPropertyData is a base of a
    // public/private subclass, of QPropertyData<int> and of QProperty<int> (lines 135-167). The
    // assertion exists so the type-erased QUntypedPropertyData* reaches a property's storage (that is
    // how the interface table and the observer list find it). The rewrite replaced the CRTP storage
    // with property_t (a handle) plus detail::core; the handle derives from nothing and there is no
    // QPropertyData equivalent. A three-line empty marker base would satisfy the static_assert but
    // would be a fake relation (no layout, no dispatch), so the function stays out.
}

// ported from tst_qproperty.cpp: dependencyChangeDuringDestruction() (line 291)
void dependency_change_during_destruction() {
    auto tester = std::make_unique<change_during_dtor_tester>();
    property_t iprop{ any_t(42) };
    // Qt: tester->bindableProp().setBinding(Qt::makePropertyBinding(iprop)) — the compat property's
    // bindable interface is its data member, and a property binding is a closure reading the source.
    tester->prop_data.set_binding([&iprop]() { return any_t(iprop.value().get<int>()); });
    CHECK(tester->prop() == 42);

    // Qt: QObject::connect(tester.get(), &QObject::destroyed, [&](){ iprop = 12; }); the signal
    // signature comes from the handle, so the test does not hardcode the qualified parameter spelling.
    int destroyed_calls = 0;
    object_t* destroyed_arg = nullptr;
    const meta_method_t destroyed_signal = meta_method_t::from_method(&object_t::destroyed);
    CHECK(destroyed_signal.is_valid());
    CHECK(tester->connect(std::string(destroyed_signal.signature()), [&](object_t* object) {
        ++destroyed_calls;
        destroyed_arg = object;
        iprop.set_value(any_t(12));
    }).is_valid());

    bool failed = false;
    auto handler = tester->prop_data.on_value_changed([&]() { failed = true; });

    // Positive control (Theme A): the same write while the object is alive DOES reach the handler, so
    // the Qt assertion below is not vacuous. Qt's eager propagation notifies at the write itself; the
    // port evaluates on read, so the read performs the recompute Qt performs eagerly.
    iprop.set_value(any_t(43));
    CHECK(tester->prop_data.value().get<int>() == 43);
    CHECK(failed);
    failed = false;

    object_t* raw = tester.get();
    tester.reset();

    // Qt's assertion (line 304) is QVERIFY(!failed): the property's onValueChanged handler must not
    // run when the destroyed() slot writes its dependency. The three checks before it are what makes
    // that assertion meaningful — the slot really ran, with the object pointer, and its write really
    // happened.
    CHECK(destroyed_calls == 1);
    CHECK(destroyed_arg == raw);
    CHECK(iprop.value().get<int>() == 12);
    // Theme A: the round-end flush must not resurrect a handler of the property that died with the
    // object (Qt's clearBindingStorage() has already dropped it in ~QObject; here the member's core
    // dies before ~object_t runs).
    flush_bindings();
    CHECK(!failed);
}

// ported from tst_qproperty.cpp: propertyAdaptorBinding() (line 1823)
void property_adaptor_binding() {
    // LEFT OUT OF THIS ROUND:
    // Qt creates a QBindable adaptor over a non-BINDABLE property (QBindable<int>(&object, "foo")),
    // drives it through bindings, update groups and multiple objects, and checks its validity
    // diagnostics (lines 1823-1980). Most of the behaviour is expressible with the library's
    // reference view (property_t::from() IS the adaptor for a notifiable property; a property with no
    // notifier is refused, Qt's "has no notify signal"), but two blocks are not:
    //  - the observer counts (data.observerCount() 0 -> 1 -> 2, "binding = {} keeps 2", addNotifier ->
    //    3): they are entries in Qt's PER-OBJECT adaptor binding data (QPropertyAdaptorSlotObject),
    //    and a port view is a caller-owned core whose dependants are notifier connections, not entries
    //    in the object;
    //  - the interop with BINDABLE properties (QBindable<int>(&bindableObject, "foo").hasBinding()
    //    seeing fooData's binding): that is the meta-property <-> binding-storage identity decision,
    //    not a binding tweak.
    // The function stays out; only the verdict is recorded here.
}

// ported from tst_qproperty.cpp: threadSafety() (line 2007)
void thread_safety() {
    // DESIGN DECISION (C):
    // Qt moves QObjects to a worker thread and checks that each object's binding storage belongs to
    // the thread that owns it (qGetBindingStorage(...)->status(...), lines 2007-2066), including after
    // moveToThread and while the object lives in another thread. The model has no per-object binding
    // storage: the per-thread part is the scheduler (detail::thread_data_t), the per-variable part is a
    // heap core with no affinity, and move_to_thread moves the object/subtree, not binding data.
    // The "bindings work on the thread that uses them" half IS expressible (a bindable created and
    // bound inside run() works; each thread has its own scheduler); the status/ownership assertions
    // need a thread-affinity subsystem (a binding_status per variable, a query, and a move that
    // retargets it) — a new concept, so the function stays C.
}

// ported from tst_qproperty.cpp: bindingGroupMovingBindingData() (line 2268)
void binding_group_moving_binding_data() {
    // OUT OF SCOPE (D):
    // Qt takes QObjectPrivate::bindingStorage.bindingData(&tester->property), opens an update group,
    // writes, checks the pointer is unchanged, reads QPropertyBindingDataPointer::proxyData(),
    // forces the per-object hash map to reallocate with ten dummy entries, then asserts
    // bindingData(property) != old, proxyData->originalBindingData == new, and after tester.reset()
    // proxyData->originalBindingData == nullptr. There is no per-object hash map, no binding-data
    // pointer and no proxy: the deferred-change queue stores void* = detail::core*, and a core is a
    // stable heap object behind shared_ptr that never moves, so the pointer-identity assertions are
    // structurally unrepresentable. The safety fact behind them (a deferred change for a destroyed
    // property must not be delivered) is covered by bindingGroupBindingDeleted, ported and passing.
}

}  // namespace qproperty_test

using namespace qproperty_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    std::printf("== Qt port: qproperty ==\n");

    inherit_q_untyped_property_data();
    functor_binding();
    basic_dependencies();
    multiple_dependencies();
    binding_with_deleted_dependency();
    binding_with_invalidated_property_observer();
    dependency_change_during_destruction();
    recursive_dependency();
    binding_after_use();
    binding_function_dtor_called();
    switch_binding();
    avoid_dependency_allocation_after_first_eval();
    bool_property();
    take_binding();
    sticky_binding();
    replace_binding();
    change_handler();
    property_change_handler_t_api();
    subscribe();
    change_handler_through_bindings();
    dont_trigger_dependencies_if_unchanged_value();
    binding_source_location();
    binding_error();
    binding_loop();
    realloc();
    change_property_from_within_change_handler();
    change_property_from_within_change_handler_through_dependency();
    change_property_from_within_change_handler2();
    setting_property_value_does_remove_binding();
    generic_property_binding_t();
    generic_property_binding_t_bool();
    set_binding_functor();
    multiple_observers();
    arrow_and_star_operator();
    notified_property();
    type_no_operator_equal();
    binding_value_replacement();
    quntyped_bindable_api();
    readonly_const_q_bindable();
    qobject_bindable_manual_notify();
    qobject_bindable_reallocated_binding_storage();
    qobject_bindable_signal_taking_new_value();
    test_new_stuff();
    qobject_observers();
    compat_bindings();
    meta_property();
    modify_observer_list_while_iterating();
    no_double_capture();
    compat_property_no_dobule_notification();
    compat_property_signals();
    no_fake_dependencies();
    property_adaptor_binding();
    thread_safety();
    thread_safety2();
    bindable_property_with_initialization();
    no_double_notification();
    grouped_notifications();
    grouped_notification_consistency();
    binding_group_moving_binding_data();
    binding_group_binding_deleted();
    uninstalled_binding_does_not_evaluate();
    notify();
    bindable_interface_of_compat_property_uses_setter();
    self_binding_should_not_crash();
    schedule_notify();
    notify_after_all_deps_gone();
    property_update_via_signaled_property();
    deref_from_observer();

    if (g_failures == 0) {
        std::printf("  qproperty: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("  qproperty: %d of %d check(s) failed\n", g_failures, g_checks);
    return 1;
}
