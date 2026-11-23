// M8 test: object_t::sender() for direct and queued dispatch.

#include <meta>
#include <cstdio>

#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/core_application.h>
#include <hwshqtb/metaobject/event_dispatcher.h>
#include <hwshqtb/metaobject/macros.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::metaobject;

namespace m8_sender_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

struct emitter_t : object_t {
    OBJECT
public:
    SIGNAL(void ping(int value) const)
};
META_OBJECT(emitter_t);

struct receiver_t : object_t {
    OBJECT
public:
    object_t* last_sender = nullptr;
    int last_value = 0;
    int calls = 0;

    void on_ping(int v) {
        last_sender = sender();
        last_value = v;
        ++calls;
    }
};
META_OBJECT(receiver_t);

}  // namespace m8_sender_test

using namespace m8_sender_test;  // the file-local test namespace stays the file's own vocabulary

int main(int argc, char** argv) {
    core_application app(argc, argv);
    core_application* root = core_application::instance();

    std::printf("== M8: sender() ==\n");

    auto* e = new emitter_t;
    e->set_parent(root);
    auto* r = new receiver_t;
    r->set_parent(root);

    // (1) Outside any dispatch: sender() is nullptr.
    CHECK(r->sender() == nullptr);
    CHECK(e->sender() == nullptr);

    // (2) Direct dispatch: the slot sees the emitter as the sender.
    auto direct = e->connect("ping(int) const", r, "on_ping(int)");
    CHECK(direct.is_valid());
    e->ping(11);
    CHECK(r->calls == 1);
    CHECK(r->last_value == 11);
    CHECK(r->last_sender == e);

    // (3) After direct dispatch, the frame is popped again.
    CHECK(r->sender() == nullptr);

    // (4) Queued dispatch: the sender survives the queue.
    CHECK(direct.disconnect());
    auto queued = e->connect("ping(int) const", r, "on_ping(int)",
                             connection_type_t{connection_type_flag::QUEUED});
    CHECK(queued.is_valid());
    e->ping(22);
    CHECK(r->calls == 1);                 // not dispatched yet
    event_dispatcher::instance().send_posted_events();
    CHECK(r->calls == 2);
    CHECK(r->last_value == 22);
    CHECK(r->last_sender == e);
    CHECK(r->sender() == nullptr);        // popped again

    std::printf(g_failures == 0 ? "  M8 sender: all checks passed\n"
                                : "  M8 sender: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}