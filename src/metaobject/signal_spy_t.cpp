/// @file signal_spy_t.cpp
/// @brief Records the emissions of one signal (the QSignalSpy equivalent).

#include "hwshqtb/metaobject/signal_spy_t.h"

namespace hwshqtb::metaobject {
    signal_spy_t::signal_spy_t(object_t* obj, std::string_view signal_signature):
        _records(std::make_shared<std::vector<std::vector<any_t>>>()) {
        if (obj == nullptr) {
            return;
        }
        const meta_method_t sig = obj->meta_object()->signal(signal_signature);
        if (!sig.is_valid()) {
            return;
        }
        auto records = _records;
        object_t* sender = obj;
        _handle = obj->variadic_connect(signal_signature, [records, sig, sender](std::span<any_t> args) {
            std::vector<any_t> entry;
            const std::size_t count = sig.param_count();
            // The spy STORES what it saw, and a Direct dispatch hands out non-owning aliases of the
            // emitter's arguments: copying an any_t keeps it an alias, so storing the span directly
            // would dangle the moment the emission ended. copy_args() deep-copies by DECLARED type,
            // which is what makes a recorded row outlive the call.
            std::vector<void*> value_addresses(args.size() + 1, nullptr);
            for (std::size_t index = 0; index < args.size(); ++index) {
                value_addresses[index + 1] = const_cast<void*>(args[index].operator&());
            }
            if (detail::copy_args(sig, *sender, value_addresses.data(), count, entry) != count) {
                // The spy's contract is "one row per emission": a value that cannot be copied (or
                // whose declared type does not resolve) still records the emission, with no
                // arguments.
                entry.clear();
            }
            records->push_back(std::move(entry));
        });
    }

    signal_spy_t::~signal_spy_t() {
        if (_handle.is_valid()) {
            _handle.disconnect();
        }
    }

    bool signal_spy_t::is_valid() const noexcept {
        return _handle.is_valid();
    }

    std::size_t signal_spy_t::count() const noexcept {
        return (_records != nullptr) ? _records->size() : 0;
    }

    std::vector<std::vector<any_t>> signal_spy_t::take_all() {
        std::vector<std::vector<any_t>> result;
        if (_records != nullptr) {
            result = std::move(*_records);
            _records->clear();
        }
        return result;
    }
}