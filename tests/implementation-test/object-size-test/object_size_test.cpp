// Stage S: object_t slimming — 32-byte layout plus lazy, on-demand state.
//
// The acceptance criteria as executable checks:
//  1. the layout: sizeof(object_t) == 32 and a derived class with no data members adds nothing;
//  2. the lazy state block: a bare object allocates NO detail::object_data_t, and each state-using
//     accessor allocates exactly one block (per object that needs one);
//  3. the accessor contract without a block: parent() / children() / object_name() / root() /
//     find_child() / signal emission all work and allocate nothing;
//  4. object_pointer_t on a bare object still works (it triggers the death-watch allocation) and the
//     guard expires when the object dies;
//  5. the whole-set size drop: 288 -> 32 bytes, i.e. exactly the 256 bytes stage S removes.
//
// The counters are the library's own (detail::object_data_live_count /
// object_data_total_allocations), incremented in object_t::ensure_data() and decremented in
// ~object_t, so they observe the REAL allocation sites instead of a test-local operator new.

#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/object_pointer_t.h>
#include <hwshqtb/metaobject/object_t.h>

#include <cstddef>
#include <cstdio>
#include <functional>
#include <type_traits>

using namespace hwshqtb::metaobject;

namespace object_size_test {
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

    std::size_t live() noexcept {
        return detail::object_data_live_count();
    }

    std::size_t total() noexcept {
        return detail::object_data_total_allocations();
    }
}  // namespace object_size_test

using namespace object_size_test;  // the file-local test namespace stays the file's own vocabulary

// ---- (1) layout: the compile-time contract ---------------------------------------------------
//
// The pre-stage-S size was 288 bytes: vtable 8 + _root 8 + _parent 8 +
// _thread 8 + six vectors*24 (_children, _connections, _incoming, _context_connections,
// _event_filters, _installed_filters) + _signal_block_count 4 (+4 pad) + _watch 16 +
// _dynamic_properties 48 + _delete_later_posted 1 (+7 pad) + _name 32. Stage S keeps the vtable, the
// thread affinity, the data pointer and the block counter inline and moves everything else into
// detail::object_data_t.
constexpr std::size_t object_t_size_before_stage_s = 288;
constexpr std::size_t object_t_size_target = 32;

static_assert(sizeof(object_t) == object_t_size_target,
    "object_t must be vtable + thread + data pointer + a 16-bit block counter = 32 bytes");
static_assert(sizeof(detail::object_data_t) > 100,
    "the lazy block must be large enough to hold every list / string / map of the old layout");
static_assert(object_t_size_before_stage_s - sizeof(object_t) == 256,
    "stage S must remove exactly the 256 bytes it moved out of object_t");

// Copy and move stay disabled: an object is a tree node identified by its address.
static_assert(!std::is_copy_constructible_v<object_t>);
static_assert(!std::is_copy_assignable_v<object_t>);
static_assert(!std::is_move_constructible_v<object_t>);
static_assert(!std::is_move_assignable_v<object_t>);

/// @brief A class with NO data members: it must not add a byte to object_t.
/// @note It still declares a signal, which is the point: an emitter adds a function, not state.
struct probe_t : object_t {
    OBJECT
public:
    SIGNAL(void pinged(int v) const)
};
META_OBJECT(probe_t);

// A derived class with no data members has exactly the base's size (the vtable pointer is inherited,
// not duplicated) — the old layout added object_t's 288 bytes to every control/widget.
static_assert(sizeof(probe_t) == sizeof(object_t),
    "a member-less class must not grow object_t");

/// @brief A receiver for the signature-based connect() check; its own members are irrelevant here.
struct receiver_t : object_t {
    OBJECT
public:
    int hits = 0;
    int last = 0;

    void on_pinged(int v) {
        ++hits;
        last = v;
    }
};
META_OBJECT(receiver_t);

int main(int argc, char** argv) {
    core_application app(argc, argv);

    std::printf("== Stage S: object_t slimming ==\n");
    std::printf("  sizeof(object_t) = %zu, sizeof(detail::object_data_t) = %zu\n",
        sizeof(object_t), sizeof(detail::object_data_t));

    // The whole suite runs against a clean counter: nothing in the library allocates a block by
    // itself (the application object is a bare object too).
    const std::size_t live_base = live();
    CHECK(live_base == 0);

    // ---- (2) a bare object: no allocation, full accessor contract ----------------------------
    {
        const std::size_t total_before = total();
        object_t bare;
        CHECK(live() == live_base);           // constructing a bare object allocates nothing
        CHECK(total() == total_before);

        CHECK(bare.parent() == nullptr);      // no block -> no parent
        CHECK(bare.children().empty());       // no block -> empty span over the shared static vector
        CHECK(bare.children().data() == nullptr);
        CHECK(bare.object_name().empty());    // no block -> shared static empty string
        CHECK(bare.root() == &bare);          // the root is computed, not stored
        CHECK(bare.thread() != nullptr);      // the affinity is inline and adopted (never null)
        CHECK(!bare.signals_blocked());       // the counter is inline

        // Detaching an object that was never attached must not allocate either.
        bare.set_parent(nullptr);
        CHECK(total() == total_before);

        // The header templates read the child list without a block.
        CHECK(bare.find_child("nope") == nullptr);
        CHECK(bare.find_child<probe_t>() == nullptr);
        CHECK(bare.find_children<object_t>().empty());

        // '' is the name a bare object already has: no allocation, no signal.
        bare.set_object_name("");
        CHECK(total() == total_before);
        CHECK(bare.object_name().empty());

        // Blocking is inline state (16-bit counter), never data.
        CHECK(!bare.block_signals(true));
        CHECK(bare.signals_blocked());
        CHECK(bare.block_signals(false));
        CHECK(!bare.signals_blocked());
        CHECK(total() == total_before);
    }
    CHECK(live() == live_base);               // destruction released the (nonexistent) block

    // ---- (3) signal emission without a block: nothing to notify, no allocation ----------------
    {
        const std::size_t total_before = total();
        probe_t bare_sender;
        bare_sender.pinged(7);                // no connection table -> returns before dispatching
        CHECK(total() == total_before);
        CHECK(live() == live_base);
    }
    CHECK(live() == live_base);

    // ---- (4) set_object_name(): exactly one block, created on the first non-empty name ---------
    {
        const std::size_t total_before = total();
        object_t named;
        CHECK(live() == live_base);

        named.set_object_name("x");
        CHECK(live() == live_base + 1);       // exactly one block for this object
        CHECK(total() == total_before + 1);
        CHECK(named.object_name() == "x");

        named.set_object_name("x");           // unchanged value: no new block
        CHECK(total() == total_before + 1);
        named.set_object_name("y");           // rename: reuses the block
        CHECK(total() == total_before + 1);
        CHECK(named.object_name() == "y");
        CHECK(live() == live_base + 1);
    }
    CHECK(live() == live_base);               // the block is freed with the object

    // ---- (5) connect(): one block per participating object, emission unchanged -----------------
    {
        const std::size_t total_before = total();
        const std::size_t live_before = live();
        probe_t sender;
        bool called = false;
        int seen = 0;

        // A functor connection has no receiver object, so only the sender needs a block — the
        // "exactly one" case.
        const connection_handle_t handle = sender.connect("pinged(int) const",
            std::function<void(int)>([&](int value) {
                called = true;
                seen = value;
            }));
        CHECK(handle.is_valid());
        CHECK(live() == live_before + 1);     // exactly one block: the sender's
        CHECK(total() == total_before + 1);

        sender.pinged(42);                    // the dispatch path still works
        CHECK(called);
        CHECK(seen == 42);

        // A slot connection needs a block on each side (the sender's table and the receiver's
        // incoming list); still exactly one block per object, never two.
        receiver_t receiver;
        const connection_handle_t slot_handle =
            sender.connect("pinged(int) const", &receiver, "on_pinged(int)");
        CHECK(slot_handle.is_valid());
        CHECK(live() == live_before + 2);     // sender + receiver, one each
        CHECK(total() == total_before + 2);
        sender.pinged(11);
        CHECK(receiver.hits == 1);
        CHECK(receiver.last == 11);

        // Emission allocates nothing beyond the dispatch itself (Direct: no queue, no copy).
        sender.pinged(12);
        CHECK(receiver.hits == 2);

        CHECK(sender.disconnect(slot_handle)); // unlinking reuses the blocks
        CHECK(live() == live_before + 2);
    }
    CHECK(live() == live_base);

    // ---- (6) set_parent(): a block on each side, root() computed on demand --------------------
    {
        const std::size_t total_before = total();
        const std::size_t live_before = live();
        object_t root_object;
        object_t child;
        object_t grandchild;

        child.set_parent(&root_object);
        CHECK(child.parent() == &root_object);
        CHECK(root_object.children().size() == 1);
        CHECK(root_object.children()[0] == &child);
        CHECK(live() == live_before + 2);     // the child stores its parent, the parent its child list
        CHECK(total() == total_before + 2);

        grandchild.set_parent(&child);
        CHECK(live() == live_before + 3);
        CHECK(grandchild.root() == &root_object);   // O(depth) walk, no cached root field
        CHECK(child.root() == &root_object);
        CHECK(root_object.root() == &root_object);

        // Re-parenting within the same tree reuses both blocks.
        const std::size_t total_before_reparent = total();
        grandchild.set_parent(&root_object);
        CHECK(grandchild.parent() == &root_object);
        CHECK(total() == total_before_reparent);
        CHECK(grandchild.root() == &root_object);
        CHECK(child.children().empty());

        // Detaching clears the link but keeps the block (the object still owns its state).
        child.set_parent(nullptr);
        CHECK(child.parent() == nullptr);
        CHECK(child.root() == &child);
        CHECK(total() == total_before_reparent);
        CHECK(live() == live_before + 3);
    }
    CHECK(live() == live_base);

    // ---- (7) event filters: both sides get a block, removal does not allocate -----------------
    {
        const std::size_t live_before = live();
        object_t target;
        object_t filter;
        target.install_event_filter(&filter);
        CHECK(live() == live_before + 2);     // the relation lives in both objects' blocks
        target.install_event_filter(&filter); // idempotent: a second install adds no node and no block
        CHECK(live() == live_before + 2);
        const std::size_t total_before_remove = total();
        target.remove_event_filter(&filter);
        CHECK(total() == total_before_remove);
        CHECK(live() == live_before + 2);
    }
    CHECK(live() == live_base);

    // ---- (8) object_pointer_t on a bare object: the guard works and expires ----------------------
    {
        const std::size_t live_before = live();
        object_pointer_t<object_t> guard;
        {
            object_t target;
            guard = &target;                  // first watch() use allocates the token (+ the block)
            CHECK(static_cast<bool>(guard));
            CHECK(guard.data() == &target);
            CHECK(live() == live_before + 1); // one block, which now also holds the death watch
            CHECK(&target.watch() == &target.watch());  // the same token reference every time

            target.set_object_name("guarded");          // reuses the same block
            CHECK(live() == live_before + 1);
        }
        CHECK(live() == live_before);         // block released with the object
        CHECK(guard.data() == nullptr);       // and the guard reads as null (QPointer semantics)
        CHECK(!static_cast<bool>(guard));
    }
    CHECK(live() == live_base);

    // ---- (9) watch() on a const object: lazy creation still works -----------------------------
    {
        const std::size_t live_before = live();
        const object_t constant_object;
        CHECK(constant_object.watch() != nullptr);  // const accessor, lazy token
        CHECK(live() == live_before + 1);
        CHECK(constant_object.object_name().empty());
    }
    CHECK(live() == live_base);

    // ---- (10) nothing leaked ------------------------------------------------------------------
    CHECK(live() == live_base);
    CHECK(live_base == 0);

    std::printf(g_failures == 0 ? "  object size: all %d checks passed\n"
                                : "  object size: %d of %d check(s) failed\n",
                g_failures == 0 ? g_checks : g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
