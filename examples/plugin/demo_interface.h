#ifndef DEMO_PLUGIN__DEMO_INTERFACE_H
#define DEMO_PLUGIN__DEMO_INTERFACE_H

/// @file demo_interface.h
/// @brief The interface the demo plugin implements, shared verbatim by the plugin and the host
///        (M16 Phase D).
///
/// An interface is a plain abstract class, NOT an object_t (M16 Phase A). Both the plugin and the
/// end-to-end test include this one header, so the vtable the host calls through is exactly the
/// layout the plugin compiled: there is no second, hand-maintained copy of the interface.

#include <string>
#include <string_view>

#include <hwshqtb/metaobject/meta_info_t.h>
#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/macros.h>

/// @brief The observable behaviour that lets the host prove it reached the plugin's own code.
///
/// init_count() is the identity probe: it reads a counter that lives inside the plugin DLL and
/// counts how many demo_plugin_object instances the plugin has constructed. A host-side copy of the
/// interface (or a stubbed implementation) cannot produce that number.
///
/// The IID is bound on the type itself with make_interface, so the plugin and the host read it from
/// this one definition: an interface type carries the annotation on its own definition, and a class
/// implements it by inheriting it publicly (the plugin's object does exactly that — there is no
/// INTERFACES list to keep in sync).
struct [[= hwshqtb::metaobject::make_interface("com.example.IGreeter/1.0")]] IGreeter {
    /// @brief Plugin-specific magic number: "M16P" in ASCII.
    static constexpr int magic_number = 0x4D313650;

    virtual ~IGreeter() = default;

    /// @brief Return magic_number.
    virtual int magic() const = 0;

    /// @brief Return the plugin's greeting string.
    virtual std::string greet() const = 0;

    /// @brief How many plugin objects the plugin DLL has constructed so far.
    virtual int init_count() const = 0;

    /// @brief Build an any_t inside the plugin, for the shared-registry check.
    /// @return An owning std::string value ("demo_plugin").
    virtual hwshqtb::metaobject::any_t make_any() const = 0;

    /// @brief Build an any_t holding a USER-declared type inside the plugin (the cross-module id probe).
    /// @return An owning demo_payload_t value ({ 7 }).
    /// @note std::basic_string is a BUILTIN interface; this one proves the name-based id dedup for a
    ///       type only the demo declares.
    virtual hwshqtb::metaobject::any_t make_payload_any() const = 0;

    /// @brief Install the plugin's own binding on its internal bindable (M17).
    ///
    /// Both the bindable and the closure live in the DLL, so this is the plain "the plugin owns a
    /// bound property" case. The cross-module regression test reads the result through bound_value()
    /// from the host, and ALSO installs a host-side binding whose closure calls bound_value(): that
    /// second form is the one that crosses the module boundary (see tests/m16-plugin-test).
    virtual void install_binding() = 0;

    /// @brief Read the current value of the plugin-owned bindable.
    /// @return The cached value; the read happens in DLL code.
    virtual int bound_value() const = 0;

    /// @brief How many times the plugin's binding closure has been evaluated so far.
    /// @return The DLL-owned evaluation counter (an identity probe like init_count()).
    /// @note This is the observable form of "was the queued recompute consumed / did the round
    ///       evaluate the binding again": the counter only moves when the closure runs.
    virtual int bound_evaluations() const = 0;

    /// @brief A plain source value the plugin's bindable can depend on.
    /// @return The plugin's source value (initially 1).
    virtual int source_value() const = 0;

    /// @brief Write the source value and invalidate the plugin-owned bindable.
    /// @param v The new source value.
    /// @note The bound value is not recomputed here: invalidate() marks the binding dirty and queues
    ///       it, so the next bound_value() read (or the end-of-round scheduler flush) recomputes it.
    virtual void set_source_value(int v) = 0;
};

/// @brief A USER-declared metatype shared verbatim by the plugin and the host (M16-D, phase 1).
///
/// The plugin stores one in an any_t inside the DLL; the host compares the id its own interface copy
/// gets. The two type_interface_t objects are distinct (one per PE module), so the ids can only agree
/// because the one process-wide registry dedups by identity NAME — the Qt registerCustomType pattern
/// this phase switched the type system to. The interface is produced on demand from the type's own
/// reflection in each module, so both sides spell the name identically (the canonical namer's output).
struct demo_payload_t {
    int value = 0;
};

/// @brief The IID the host passes to meta_object_t::cast(object, iid); it must match the
///        make_interface annotation on IGreeter (cast<IGreeter>(object) reads that annotation
///        directly, so the two never drift).
inline constexpr const char* demo_plugin_iid = "com.example.IGreeter/1.0";

/// @brief The plugin's greeting, kept in the shared contract so plugin and test cannot drift apart.
inline constexpr std::string_view demo_plugin_greeting = "hello from demo_plugin/1.0";

#endif
