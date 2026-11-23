/// @file thread_data_t.cpp
/// @brief Per-thread bookkeeping (the thread_tData equivalent).

#include "hwshqtb/metaobject/thread_data_t.h"

#include "hwshqtb/metaobject/event_dispatcher.h"
#include "hwshqtb/metaobject/thread_t.h"

namespace hwshqtb::metaobject::detail {
    HWSHQTB_EXPORT thread_data_t& thread_data_t::current() noexcept {
        static thread_local thread_data_t data;
        // The dispatcher must be created on the thread that OWNS this thread_data_t, never by a
        // foreign thread that happens to look at it (cross-thread posting does exactly that).
        if (data.dispatcher == nullptr) {
            data.dispatcher = &event_dispatcher::instance();
        }
        // Adopt this thread's public identity. `data` is already the registered thread-local slot and
        // `adopted` is set BEFORE the construction below: thread_t's base object_t constructor calls
        // current() again, so with either fact missing that call would adopt a second identity and
        // recurse forever. The adopted object lives for the process lifetime (never destroyed).
        static thread_local bool adopted = false;
        if (!adopted) {
            adopted = true;
            data.owner = new thread_t(&data);
        }
        return data;
    }
}