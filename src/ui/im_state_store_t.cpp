/// @file im_state_store_t.cpp
/// @brief im_state_store_t: the USER-linked property snapshot / restore (see im_state_store_t.h).

#include "hwshqtb/ui/im_state_store_t.h"

#include <cstddef>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include "hwshqtb/metaobject/meta_method_t.h"
#include "hwshqtb/metaobject/meta_object_t.h"
#include "hwshqtb/metaobject/meta_property_t.h"
#include "hwshqtb/metaobject/signal_blocker_t.h"

namespace hwshqtb::ui {
    namespace {
        /// @brief The writable property whose notifier is the class's USER event, or an invalid handle.
        /// @param meta The control's meta-object (the chain head).
        /// @return The linked state property, or an invalid handle when the class has no USER property
        ///         or no writable property notifies it.
        /// @note The link is the NAME in the metadata: the USER property is the event, and the state
        ///       property's notifier is the signal of that event. A command event without a value
        ///       (push_button's clicked) has no such property, so it has no state.
        /// @note Property order is parents first; if several writable properties link the same USER
        ///       event, the FIRST one is the state. Normal modeling declares exactly one.
        ::hwshqtb::metaobject::meta_property_t linked_state_property(
            const ::hwshqtb::metaobject::meta_object_t& meta) {
            const ::hwshqtb::metaobject::meta_property_t user = meta.user_property();
            if (!user.is_valid()) {
                return {};
            }
            for (std::size_t index = 0; index < meta.property_count(); ++index) {
                const ::hwshqtb::metaobject::meta_property_t property =
                    meta.property(static_cast<::hwshqtb::metaobject::index_t>(index));
                if (!property.is_valid() || property.is_user() || !property.is_writable()) {
                    continue;
                }
                const ::hwshqtb::metaobject::meta_method_t notifier = property.notifier();
                if (notifier.is_valid() && notifier.is_signal()
                    && notifier.name() == user.name()) {
                    return property;
                }
            }
            return {};
        }
    }

    void im_state_store_t::snapshot(const ::hwshqtb::metaobject::object_t& object,
                                    std::string_view id) {
        if (id.empty() || !object.is_widget()) {
            return;
        }
        const ::hwshqtb::metaobject::meta_object_t* const meta = object.meta_object();
        if (meta == nullptr) {
            return;
        }
        const ::hwshqtb::metaobject::meta_property_t state = linked_state_property(*meta);
        if (!state.is_valid()) {
            // No USER event, or a command event with no value: this class has no state at all.
            const auto found = _entries.find(id);
            if (found != _entries.end()) {
                _entries.erase(found);
            }
            return;
        }
        value_t value;
        value.name = std::string(state.name());
        try {
            ::hwshqtb::metaobject::any_t read = state.read(object);
            if (state.type(object).is<std::string_view>()) {
                value.text = std::string(read.get<std::string_view>());
                value.text_value = true;
            }
            else {
                value.value = std::move(read);
            }
        }
        catch (const std::exception&) {
            return; // a transient read failure must not throw away the earlier value
        }
        snapshot_t next;
        next.type_name = std::string(meta->qualified_class_name());
        next.value = std::move(value);
        _entries.insert_or_assign(std::string(id), std::move(next));
    }

    bool im_state_store_t::restore(::hwshqtb::metaobject::object_t& object, std::string_view id) {
        const auto found = _entries.find(id);
        if (found == _entries.end()) {
            return false;
        }
        const ::hwshqtb::metaobject::meta_object_t* const meta = object.meta_object();
        if (meta == nullptr || found->second.type_name != meta->qualified_class_name()) {
            _entries.erase(found); // the id now names another class: the old value is not its state
            return false;
        }
        const value_t& value = found->second.value;
        const ::hwshqtb::metaobject::meta_property_t property = meta->property(value.name);
        if (property.is_valid() && property.is_writable()) {
            // Restoring is not a change the caller made: a fresh control and a reused one must emit
            // the same signals, so the write is silent.
            const ::hwshqtb::metaobject::signal_blocker_t blocked(&object);
            try {
                if (value.text_value) {
                    const std::string_view text{ value.text };
                    (void)property.write(object, ::hwshqtb::metaobject::any_t{ text });
                }
                else if (value.value.is_initialized()) {
                    (void)property.write(object, value.value);
                }
            }
            catch (const std::exception&) {
                // The linked property no longer accepts the stored value: nothing else to restore.
            }
        }
        return true;
    }

    bool im_state_store_t::contains(std::string_view id) const noexcept {
        return _entries.find(id) != _entries.end();
    }

    bool im_state_store_t::erase(std::string_view id) {
        const auto found = _entries.find(id);
        if (found == _entries.end()) {
            return false;
        }
        _entries.erase(found);
        return true;
    }

    void im_state_store_t::clear() {
        _entries.clear();
    }

    std::size_t im_state_store_t::size() const noexcept {
        return _entries.size();
    }
}
