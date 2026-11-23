/// @file meta_property_builder_t.cpp
/// @brief The mutable handle of one run-time built property (QMetaPropertyBuilder equivalent).

#include "hwshqtb/metaobject/meta_property_builder_t.h"

#include "hwshqtb/metaobject/meta_object_builder_t.h"

#include <stdexcept>
#include <string>

namespace hwshqtb::metaobject {
    namespace {
        /// @brief Refuse a capability that has no thunk behind it.
        /// @param property The property name.
        /// @param capability The capability being switched on ("read", "write", "reset").
        /// @param setter The setter that records the thunk behind it.
        /// @return Never returns; the message names the missing thunk and the setter that records it.
        /// @note The table derives every capability from its thunk (meta_property_t::is_writable() is
        ///       "there is a writer"), so enabling one without the thunk would produce a row that
        ///       reports a capability it cannot honour. Refusing is the only honest answer.
        [[noreturn]] void missing_thunk(const std::string& property, const char* capability, const char* setter) {
            throw std::invalid_argument(std::string("meta_property_builder_t: property '") + property
                + "' has no thunk for the " + capability + " capability; record one with " + setter
                + "() before enabling it");
        }
    }

    meta_property_builder_t::meta_property_builder_t(meta_object_builder_t* owner, index_t index) noexcept:
        _owner(owner),
        _index(index) { }

    index_t meta_property_builder_t::index() const noexcept {
        return _index;
    }

    std::string_view meta_property_builder_t::name() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        return (row != nullptr) ? std::string_view(row->name) : std::string_view{};
    }

    std::string_view meta_property_builder_t::type() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        return (row != nullptr) ? std::string_view(row->type) : std::string_view{};
    }

    bool meta_property_builder_t::is_readable() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        return row != nullptr && row->reader != nullptr;
    }

    bool meta_property_builder_t::is_writable() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        return row != nullptr && row->writer != nullptr;
    }

    bool meta_property_builder_t::is_resettable() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        return row != nullptr && row->reseter != nullptr;
    }

    bool meta_property_builder_t::is_user() const noexcept {
        return trait(property_trait_flag::USER);
    }

    bool meta_property_builder_t::is_required() const noexcept {
        return trait(property_trait_flag::REQUIRED);
    }

    bool meta_property_builder_t::is_hidden() const noexcept {
        return trait(property_trait_flag::HIDDEN);
    }

    bool meta_property_builder_t::is_final() const noexcept {
        return trait(property_trait_flag::FINAL);
    }

    bool meta_property_builder_t::trait(property_trait_flag flag) const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        return row != nullptr && bool(row->traits & flag);
    }

    bool meta_property_builder_t::is_constant() const noexcept {
        const auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        return row != nullptr && row->writer == nullptr && row->notifier == index_npos;
    }

    void meta_property_builder_t::set_readable(bool enabled) {
        auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        if (row == nullptr) {
            throw std::invalid_argument(detail::stale_row_message("meta_property_builder_t", name(), "remove_property"));
        }
        if (enabled && row->reader == nullptr) {
            missing_thunk(row->name, "read", "set_reader");
        }
        if (!enabled) {
            row->reader = nullptr;
        }
    }

    void meta_property_builder_t::set_writable(bool enabled) {
        auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        if (row == nullptr) {
            throw std::invalid_argument(detail::stale_row_message("meta_property_builder_t", name(), "remove_property"));
        }
        if (enabled && row->writer == nullptr) {
            missing_thunk(row->name, "write", "set_writer");
        }
        if (!enabled) {
            row->writer = nullptr;
        }
    }

    void meta_property_builder_t::set_resettable(bool enabled) {
        auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        if (row == nullptr) {
            throw std::invalid_argument(detail::stale_row_message("meta_property_builder_t", name(), "remove_property"));
        }
        if (enabled && row->reseter == nullptr) {
            missing_thunk(row->name, "reset", "set_reseter");
        }
        if (!enabled) {
            row->reseter = nullptr;
        }
    }

    void meta_property_builder_t::set_user(bool enabled) {
        set_trait(property_trait_flag::USER, enabled);
    }

    void meta_property_builder_t::set_required(bool enabled) {
        set_trait(property_trait_flag::REQUIRED, enabled);
    }

    void meta_property_builder_t::set_hidden(bool enabled) {
        set_trait(property_trait_flag::HIDDEN, enabled);
    }

    void meta_property_builder_t::set_final(bool enabled) {
        set_trait(property_trait_flag::FINAL, enabled);
    }

    void meta_property_builder_t::set_trait(property_trait_flag flag, bool enabled) {
        auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        if (row == nullptr) {
            throw std::invalid_argument(detail::stale_row_message("meta_property_builder_t", name(), "remove_property"));
        }
        if (enabled) {
            row->traits = row->traits | flag;
        }
        else {
            // flag_t defines `&` and `~` between flags, so clearing one bit keeps the others intact.
            row->traits = row->traits & ~property_trait_t{ flag };
        }
    }

    void meta_property_builder_t::set_constant(bool enabled) {
        auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        if (row == nullptr) {
            throw std::invalid_argument(detail::stale_row_message("meta_property_builder_t", name(), "remove_property"));
        }
        if (enabled) {
            // Constant is derived: no writer and no notifier. Clearing both is what makes it so.
            row->writer = nullptr;
            row->notifier = index_npos;
            return;
        }
        if (row->writer == nullptr && row->notifier == index_npos) {
            throw std::invalid_argument("meta_property_builder_t: property '" + row->name
                + "' is constant because it has neither a writer nor a notifier; a setter cannot invent "
                  "one, so record a writer or a notifier first");
        }
    }

    void meta_property_builder_t::set_reader(detail::generic_call caller) {
        auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        if (row == nullptr) {
            throw std::invalid_argument(detail::stale_row_message("meta_property_builder_t", name(), "remove_property"));
        }
        row->reader = caller;
    }

    void meta_property_builder_t::set_writer(detail::generic_call caller) {
        auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        if (row == nullptr) {
            throw std::invalid_argument(detail::stale_row_message("meta_property_builder_t", name(), "remove_property"));
        }
        row->writer = caller;
    }

    void meta_property_builder_t::set_reseter(detail::generic_call caller) {
        auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        if (row == nullptr) {
            throw std::invalid_argument(detail::stale_row_message("meta_property_builder_t", name(), "remove_property"));
        }
        row->reseter = caller;
    }

    void meta_property_builder_t::set_notifier(meta_method_builder_t method) {
        auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        if (row == nullptr) {
            throw std::invalid_argument(detail::stale_row_message("meta_property_builder_t", name(), "remove_property"));
        }
        // The notifier is stored as the row index and resolved to the final table slot at build()
        // time, so a method removed in between cannot leave the property pointing at the wrong entry.
        row->notifier = method.index();
    }

    void meta_property_builder_t::set_enum(std::string_view enum_name, bool is_flags) {
        auto* row = (_owner != nullptr) ? _owner->property_row(_index) : nullptr;
        if (row == nullptr) {
            throw std::invalid_argument(detail::stale_row_message("meta_property_builder_t", name(), "remove_property"));
        }
        row->enum_name.assign(enum_name);
        row->is_flags = is_flags;
    }
}
