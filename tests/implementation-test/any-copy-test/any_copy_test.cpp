// T1: regression test for the any_t throwing-copy fix, which had none.
//
// WHAT THE FIX WAS. any_t's copy constructor used to have its own hand-written body. When the
// source owns a constructed value, the copy allocates the destination slot and calls the type's
// copy constructor through meta_type_t::copy_construct(); if THAT COPY CONSTRUCTOR THROWS (rather
// than the type being non-copyable, which makes copy_construct() return false), the exception left
// the old constructor before the object was complete, so ~any_t never ran and the destination slot
// leaked. The copy constructor now delegates to the copy assignment, which owns exactly one copy
// rule; on the throw the delegated-to object's destructor runs during unwinding and releases the
// slot.
//
// WHAT IS NOT OBSERVABLE. The leak itself cannot be seen from a test binary: the slot comes from
// type_interface_t::allocate() INSIDE libmetaobject.dll (::operator new in the DLL's CRT), so
// overriding operator new in this executable would not intercept it. There is also no allocation
// counter on the public API. This test therefore does NOT try to count bytes; it pins the STATE
// INVARIANTS the fixed control flow relies on, which a regression to a hand-written constructor (or
// a "clear the slot" deviating assignment) would break:
//   1. the exception from the underlying copy constructor propagates out unchanged;
//   2. after a throwing copy the destination is never "half-owned": it is either declared-only
//      (owns exactly its own reserved slot, is_initialized() == false) or has no storage at all --
//      never a constructed flag over an unconstructed object, and never a shared/borrowed slot;
//   3. the source is unchanged (still initialized, value readable);
//   4. repeating the throwing copy N times leaves the destination in the same state (no
//      accumulation of slots and no drift into the initialized state).
// The two failure channels are pinned separately because they end in DIFFERENT states by design:
//   - a throwing copy constructor propagates; the assignment has already reserved the destination
//     slot, so the destination is declared-only and operator&() answers that slot (the destructor
//     of a copy-constructed any_t releases it while unwinding -- that is the leak fix);
//   - a NON-copy-constructible type makes copy_construct() return false; the assignment releases
//     the slot before throwing bad_any_unsupported_t, so operator&() throws: this is the only
//     channel that ends with "no storage at all".
// Note on wording: the assertion "operator&() throws" does NOT hold for the
// throwing-copy channel, because a declared-only any_t deliberately owns an addressable slot (see
// any_t.h). The contract this test pins is "declared-only OR
// no storage, never half-owned", which is the strongest true statement; the contrast case below
// covers the other branch.

#include <meta>

#include <cstdio>
#include <stdexcept>
#include <utility>

#include <hwshqtb/metaobject/any_t.h>
#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/meta_type_t.h>

using namespace hwshqtb::metaobject;

namespace any_copy_exception_test {

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

/// A copyable-looking type whose copy constructor always throws. `std::is_copy_constructible_v` is
/// true, so the type interface installs a copy thunk and meta_type_t::copy_construct() calls it --
/// the exception escapes it. The move constructor is defaulted (and not throwing), so the SOURCE
/// any_t can be built from an rvalue without going through the throwing copy.
struct throwing_copy_t {
    int value = 0;
    static inline int copy_attempts = 0;

    explicit throwing_copy_t(int v): value(v) { }
    throwing_copy_t(throwing_copy_t&&) noexcept = default;
    throwing_copy_t& operator=(const throwing_copy_t&) = default;
    throwing_copy_t& operator=(throwing_copy_t&&) noexcept = default;
    throwing_copy_t(const throwing_copy_t& other): value(other.value) {
        ++copy_attempts;
        throw std::runtime_error("throwing_copy_t: copy constructor");
    }
};

/// The OTHER failure channel: no copy thunk at all, so copy_construct() returns false and the
/// assignment releases the slot before throwing bad_any_unsupported_t.
struct move_only_t {
    int value = 0;

    explicit move_only_t(int v): value(v) { }
    move_only_t(move_only_t&&) noexcept = default;
    move_only_t& operator=(move_only_t&&) noexcept = default;
    move_only_t(const move_only_t&) = delete;
    move_only_t& operator=(const move_only_t&) = delete;
};

}  // namespace any_copy_exception_test

using namespace any_copy_exception_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);

    std::printf("== T1: any_t copy with a throwing copy constructor ==\n");

    throwing_copy_t::copy_attempts = 0;

    // The source owns a constructed value; building it moved, so no copy has happened yet.
    any_t source(throwing_copy_t{21});
    CHECK(source.is_initialized());
    CHECK(source.type().is<throwing_copy_t>());
    CHECK(source.get<throwing_copy_t&>().value == 21);
    CHECK(throwing_copy_t::copy_attempts == 0);

    // (1) Copy CONSTRUCTION propagates the exception. This is the delegating-constructor path whose
    //     unwinding destructor releases the destination slot (the leak fix); the slot is released
    //     before any test could see it.
    {
        bool threw = false;
        try {
            any_t copy(source);
            (void)copy;
        }
        catch (const std::runtime_error&) {
            threw = true;
        }
        CHECK(threw);
        CHECK(throwing_copy_t::copy_attempts == 1);
    }

    // (2) The source is unchanged by the failed copy: same state, same value.
    CHECK(source.is_initialized());
    CHECK(source.type().is<throwing_copy_t>());
    CHECK(source.get<throwing_copy_t&>().value == 21);
    CHECK(static_cast<throwing_copy_t*>(&source)->value == 21);

    // (3) Copy ASSIGNMENT: the destination is observable, so the state after the throw is pinned
    //     here. It must be declared-only -- the type was taken over, nothing was constructed, and
    //     the reserved slot is the destination's OWN (not the source's, not a shared one).
    any_t destination;
    CHECK(!destination.is_initialized());
    {
        bool threw = false;
        try {
            destination = source;
        }
        catch (const std::runtime_error&) {
            threw = true;
        }
        CHECK(threw);
        CHECK(throwing_copy_t::copy_attempts == 2);
    }
    CHECK(!destination.is_initialized());
    CHECK(destination.type().is<throwing_copy_t>());
    {
        bool uninitialized_access = false;
        try {
            (void)destination.get<throwing_copy_t&>();
        }
        catch (const bad_any_uninitialized_t&) {
            uninitialized_access = true;
        }
        CHECK(uninitialized_access);
    }
    // Declared-only: the slot is owned and addressable (this is NOT the "no storage" state), but
    // it holds no object. The two addresses must differ: the destination never borrows the source's
    // storage.
    void* destination_slot = nullptr;
    bool destination_has_slot = false;
    try {
        destination_slot = &destination;
        destination_has_slot = true;
    }
    catch (const bad_any_unsupported_t&) {
    }
    CHECK(destination_has_slot);
    CHECK(destination_slot != nullptr);
    CHECK(destination_slot != static_cast<void*>(&source));
    {
        const any_t& const_destination = destination;
        const void* const_slot = nullptr;
        bool const_has_slot = false;
        try {
            const_slot = &const_destination;
            const_has_slot = true;
        }
        catch (const bad_any_unsupported_t&) {
        }
        CHECK(const_has_slot);
        CHECK(const_slot == destination_slot);
    }

    // (4) Repeating the throwing copy N times leaves the destination in the SAME state every time:
    //     no accumulation (the slot is released and re-reserved by each assignment's reset(), and
    //     the failed copy's slot is released by the assignment itself before the exception leaves
    //     the copy-construction path), and no drift into is_initialized().
    {
        int repeated = 0;
        for (int i = 0; i < 5; ++i) {
            try {
                destination = source;
            }
            catch (const std::runtime_error&) {
                ++repeated;
            }
            CHECK(!destination.is_initialized());
            CHECK(destination.type().is<throwing_copy_t>());
            void* slot = nullptr;
            bool has_slot = false;
            try {
                slot = &destination;
                has_slot = true;
            }
            catch (const bad_any_unsupported_t&) {
            }
            CHECK(has_slot);
            CHECK(slot != nullptr);
            CHECK(slot != static_cast<void*>(&source));
        }
        CHECK(repeated == 5);
        CHECK(throwing_copy_t::copy_attempts == 2 + 5);
    }

    // The source is still untouched after the repetition.
    CHECK(source.is_initialized());
    CHECK(source.get<throwing_copy_t&>().value == 21);

    // (5) Contrast -- the OTHER channel ends with NO storage. A non-copy-constructible type makes
    //     copy_construct() return false; the assignment releases the slot and throws
    //     bad_any_unsupported_t, so operator&() refuses. This is what "never half-owned" means for
    //     this channel, and it shows the throwing channel above is deliberately the other shape.
    {
        any_t move_only_source(move_only_t{ 7 });
        CHECK(move_only_source.is_initialized());
        CHECK(move_only_source.get<move_only_t&>().value == 7);

        any_t move_only_destination;
        bool unsupported = false;
        try {
            move_only_destination = move_only_source;
        }
        catch (const bad_any_unsupported_t&) {
            unsupported = true;
        }
        CHECK(unsupported);
        CHECK(!move_only_destination.is_initialized());
        bool no_storage = false;
        try {
            (void)&move_only_destination;
        }
        catch (const bad_any_unsupported_t&) {
            no_storage = true;
        }
        CHECK(no_storage);
        CHECK(move_only_source.is_initialized());
        CHECK(move_only_source.get<move_only_t&>().value == 7);
    }

    // (6) THE INVALID STATE — the corrected default. A default-constructed any_t declares NO type:
    //     it is the std::type_index analogue ("no type"), not a void variable. The old default was
    //     meta_type_t's void interface, so every expectation below is new, and each one fails if the
    //     default constructor is reverted to `declared<void>()`.
    {
        any_t invalid;
        CHECK(!invalid.is_valid());
        CHECK(!invalid.is_initialized());

        // type() is a QUERY, not a value access: it answers the invalid handle and does not throw.
        meta_type_t reported;
        bool type_threw = false;
        try {
            reported = invalid.type();
        }
        catch (...) {
            type_threw = true;
        }
        CHECK(!type_threw);
        CHECK(!reported.is_valid());
        CHECK(!reported.is_void());   // "no type" is not void
        CHECK(reported.id() == index_npos);

        // Every operation that needs the value reports "there is no type" — bad_meta_type_t, never
        // "the type is known but not built" (bad_any_uninitialized_t).
        bool get_threw = false;
        try {
            (void)invalid.get<int>();
        }
        catch (const bad_meta_type_t&) {
            get_threw = true;
        }
        CHECK(get_threw);

        const any_t& const_invalid = invalid;
        bool const_get_threw = false;
        try {
            (void)const_invalid.get<int>();
        }
        catch (const bad_meta_type_t&) {
            const_get_threw = true;
        }
        CHECK(const_get_threw);

        bool address_threw = false;
        try {
            (void)&invalid;
        }
        catch (const bad_meta_type_t&) {
            address_threw = true;
        }
        CHECK(address_threw);

        bool const_address_threw = false;
        try {
            (void)&const_invalid;
        }
        catch (const bad_meta_type_t&) {
            const_address_threw = true;
        }
        CHECK(const_address_threw);

        bool serialize_threw = false;
        try {
            (void)invalid.serialize();
        }
        catch (const bad_meta_type_t&) {
            serialize_threw = true;
        }
        CHECK(serialize_threw);

        bool deserialize_threw = false;
        try {
            (void)invalid.deserialize("0");
        }
        catch (const bad_meta_type_t&) {
            deserialize_threw = true;
        }
        CHECK(deserialize_threw);

        bool as_threw = false;
        try {
            (void)invalid.as<int>();
        }
        catch (const bad_meta_type_t&) {
            as_threw = true;
        }
        CHECK(as_threw);

        // The equality rule reads the handle type() answers. any_t has no operator== of its own (see
        // the header: equality is the type-erased detail::equal_any gate in property_t.cpp, which is
        // noexcept and answers false), so "compares equal to nothing" is pinned on the handle: an
        // invalid handle equals no type at all — in particular not void.
        CHECK(!(invalid.type() == meta_type_t::from_type<void>()));
        CHECK(!invalid.type().is<void>());
        CHECK(!invalid.is<int>());   // pure query: false, no throw

        // Copy, move and assignment keep the invalid state invalid: there is no type to copy and
        // nothing to move.
        any_t copied(invalid);
        CHECK(!copied.is_valid());
        CHECK(!copied.is_initialized());
        CHECK(!copied.type().is_valid());

        any_t moved(std::move(copied));
        CHECK(!moved.is_valid());
        CHECK(!moved.is_initialized());

        any_t assigned;
        assigned = invalid;
        CHECK(!assigned.is_valid());
        CHECK(!assigned.is_initialized());
    }

    // (7) The DECLARED-ONLY VOID variable is the OTHER state, and it is VALID: void is a real type,
    //     so its value access fails as "not built yet" (bad_any_uninitialized_t), never as "no type".
    //     This is the pair the correction exists to separate.
    {
        any_t void_variable(meta_type_t::from_type<void>());
        CHECK(void_variable.is_valid());
        CHECK(!void_variable.is_initialized());
        CHECK(void_variable.type().is_void());
        CHECK(void_variable.type().is_valid());
        CHECK(void_variable.type() == meta_type_t::from_type<void>());

        bool uninitialized = false;
        try {
            (void)void_variable.get<int>();
        }
        catch (const bad_any_uninitialized_t&) {
            uninitialized = true;
        }
        CHECK(uninitialized);

        // It has no storage either, but for the other reason — it declares a type.
        bool no_storage = false;
        try {
            (void)&void_variable;
        }
        catch (const bad_any_unsupported_t&) {
            no_storage = true;
        }
        CHECK(no_storage);

        // invalid != void, in both directions.
        any_t invalid;
        CHECK(invalid.type() != void_variable.type());
        CHECK(!invalid.is_valid());
        CHECK(void_variable.is_valid());
    }

    // (8) The handle-level statement of the same correction: a default-constructed meta_type_t is the
    //     INVALID handle, and void is reachable only through from_type<void>() / a successful lookup.
    {
        CHECK(!meta_type_t{}.is_valid());
        CHECK(!meta_type_t{}.is_void());
        CHECK(meta_type_t{}.name().empty());
        CHECK(meta_type_t{}.id() == index_npos);
        CHECK(meta_type_t::from_type<void>().is_valid());
        CHECK(meta_type_t::from_type<void>().is_void());
        // invalid is not void: "no type" never names the void type. (Two invalid handles are not
        // "the same type" either — == answers "do they name one type", and neither does.)
        CHECK(!(meta_type_t{} == meta_type_t::from_type<void>()));
        CHECK(meta_type_t::resolve_global("void").is_void());   // void stays reachable by name
    }

    std::printf(g_failures == 0 ? "  T1 any_t throwing copy: all %d checks passed\n"
                                : "  T1 any_t throwing copy: %d of %d check(s) failed\n",
                g_failures == 0 ? g_checks : g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
