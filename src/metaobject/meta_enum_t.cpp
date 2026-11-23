#include <hwshqtb/metaobject/meta_enum_t.h>

#include <hwshqtb/metaobject/meta_info_t.h>
#include <hwshqtb/metaobject/meta_object_t.h>

#include <span>
#include <vector>

namespace hwshqtb::metaobject {
    namespace {
        /// @brief Get the enumerator table of an enum.
        /// @param info The enum info.
        /// @return Span over the enumerators.
        /// @note noexcept.
        std::span<const detail::enum_entry_t> entries_of(const detail::enum_info_t& info) noexcept {
            return info.entries;
        }
    }

    const detail::enum_info_t* meta_enum_t::aimed_info() const noexcept {
        // The slot guard is meta_object_t::row_at, shared with the method and class-info handles.
        return meta_object_t::row_at(_owner, _index, &meta_info_t::enum_infos);
    }

    meta_enum_t::meta_enum_t(const meta_object_t* owner, index_t index) noexcept
        :
        _owner(owner),
        _index(index) { }

    meta_enum_t meta_enum_t::enumeration_of(const meta_object_t& meta, std::string_view name) {
        return meta.enumeration(name);
    }

    bool meta_enum_t::is_valid() const noexcept {
        return aimed_info() != nullptr;
    }

    std::string_view meta_enum_t::name() const noexcept {
        const detail::enum_info_t* info = aimed_info();
        return (info != nullptr) ? static_cast<std::string_view>(info->name) : std::string_view{};
    }

    std::string_view meta_enum_t::scope() const noexcept {
        const detail::enum_info_t* info = aimed_info();
        return (info != nullptr) ? static_cast<std::string_view>(info->scope) : std::string_view{};
    }

    bool meta_enum_t::is_scoped() const noexcept {
        const detail::enum_info_t* info = aimed_info();
        return (info != nullptr) && info->is_scoped;
    }

    bool meta_enum_t::is_flag() const noexcept {
        const detail::enum_info_t* info = aimed_info();
        return (info != nullptr) && info->is_flag;
    }

    std::size_t meta_enum_t::key_count() const noexcept {
        const detail::enum_info_t* info = aimed_info();
        return (info != nullptr) ? entries_of(*info).size() : 0;
    }

    std::string_view meta_enum_t::key(std::size_t index) const noexcept {
        const detail::enum_info_t* info = aimed_info();
        if (info == nullptr) {
            return {};
        }
        const std::span<const detail::enum_entry_t> entries = entries_of(*info);
        if (index >= entries.size()) {
            return {};
        }
        return static_cast<std::string_view>(entries[index].name);
    }

    std::int64_t meta_enum_t::value(std::size_t index) const noexcept {
        const detail::enum_info_t* info = aimed_info();
        if (info == nullptr) {
            return 0;
        }
        const std::span<const detail::enum_entry_t> entries = entries_of(*info);
        return (index < entries.size()) ? entries[index].value : 0;
    }

    std::string_view meta_enum_t::value_to_key(std::int64_t value) const noexcept {
        const detail::enum_info_t* info = aimed_info();
        if (info == nullptr) {
            return {};
        }
        for (const auto& entry : entries_of(*info)) {
            if (entry.value == value) {
                return static_cast<std::string_view>(entry.name);
            }
        }
        return {};
    }

    std::optional<std::int64_t> meta_enum_t::key_to_value(std::string_view key) const noexcept {
        const detail::enum_info_t* info = aimed_info();
        if (info == nullptr) {
            return std::nullopt;
        }
        for (const auto& entry : entries_of(*info)) {
            if (static_cast<std::string_view>(entry.name) == key) {
                return entry.value;
            }
        }
        return std::nullopt;
    }

    std::string meta_enum_t::value_to_keys(std::int64_t value) const {
        const detail::enum_info_t* info = aimed_info();
        if (info == nullptr) {
            return {};
        }
        const std::span<const detail::enum_entry_t> entries = entries_of(*info);

        // Compare at the enum's own width: the table widens every enumerator to int64, so an
        // int-backed INT_MIN is stored sign-extended (0xFFFFFFFF80000000) while the query may be the
        // positive 0x80000000 bit pattern. Masking both sides makes the two agree.
        const std::size_t width = info->underlying_size;
        const std::uint64_t mask = (width >= sizeof(std::uint64_t))
            ? ~std::uint64_t{ 0 }
            : (std::uint64_t{ 1 } << (width * 8)) - 1;
        const std::uint64_t query = static_cast<std::uint64_t>(value) & mask;

        // Qt's algorithm: visit the enumerators in REVERSE declaration order, so a combined value
        // (Dialog = Window|0x2) is seen before its constituents, and clear the bits of every match
        // so those bits cannot be emitted a second time by a contained enumerator.
        std::uint64_t remaining = query;
        std::vector<std::string_view> parts;
        parts.reserve(entries.size());
        for (std::size_t i = entries.size(); i-- > 0;) {
            const std::uint64_t key = static_cast<std::uint64_t>(entries[i].value) & mask;
            // Qt's degenerate clause compares against the ORIGINAL query, not the running
            // remainder: an exact key wins even after earlier entries consumed bits, and a
            // zero-valued enumerator is emitted for a zero query (but never for a non-zero query
            // whose bits have all been consumed).
            if ((key != 0 && (remaining & key) == key) || (key == query)) {
                remaining &= ~key;
                parts.push_back(static_cast<std::string_view>(entries[i].name));
            }
        }

        // parts is in reverse declaration order; join backwards to keep declaration order.
        std::string out;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            if (!out.empty()) {
                out.push_back('|');
            }
            out.append(*it);
        }
        return out;
    }

    std::optional<std::int64_t> meta_enum_t::keys_to_value(std::string_view keys) const noexcept {
        const detail::enum_info_t* info = aimed_info();
        if (info == nullptr || keys.empty()) {
            return std::nullopt;
        }
        std::int64_t result = 0;
        std::size_t begin = 0;
        while (true) {
            const std::size_t sep = keys.find('|', begin);
            const std::string_view segment = (sep == std::string_view::npos)
                ? keys.substr(begin)
                : keys.substr(begin, sep - begin);
            if (segment.empty()) {
                return std::nullopt; // empty segment (leading/trailing '|', or two in a row)
            }
            const std::optional<std::int64_t> hit = key_to_value(segment);
            if (!hit.has_value()) {
                return std::nullopt;
            }
            result |= *hit;
            if (sep == std::string_view::npos) {
                break;
            }
            begin = sep + 1;
        }
        return result;
    }
}
