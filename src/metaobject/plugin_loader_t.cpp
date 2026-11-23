/// @file plugin_loader_t.cpp
/// @brief Host-side loader for one plugin shared library.

#include <hwshqtb/metaobject/plugin_loader_t.h>

#include <hwshqtb/metaobject/log.h>

#include <string>
#include <utility>

namespace hwshqtb::metaobject {
    namespace {
        /// @brief The fixed entry-point names shared by host and plugin (C linkage, no mangling).
        constexpr const char* instance_entry_name = "metaobject_plugin_instance";
        constexpr const char* metadata_entry_name = "metaobject_plugin_metadata";

        /// @brief metaobject_plugin_instance: returns the plugin's main object.
        using instance_entry_t = object_t* (*)();
        /// @brief metaobject_plugin_metadata: returns the plugin's "key=value" text.
        using metadata_entry_t = const char* (*)();

        /// @brief One of the ASCII characters trimmed from metadata keys and values.
        constexpr bool is_space(char c) noexcept {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
        }

        /// @brief Trim leading and trailing ASCII whitespace.
        constexpr std::string_view trim(std::string_view text) noexcept {
            while (!text.empty() && is_space(text.front())) {
                text.remove_prefix(1);
            }
            while (!text.empty() && is_space(text.back())) {
                text.remove_suffix(1);
            }
            return text;
        }
    }

    plugin_loader_t::plugin_loader_t(std::string path):
        _library(std::move(path)) { }

    plugin_loader_t::~plugin_loader_t() {
        // The destructor must not throw: a refused (or failing) unload leaves the library loaded,
        // which is the same outcome the live-object rule asks for.
        try {
            unload();
        }
        catch (...) {
        }
    }

    bool plugin_loader_t::load() {
        if (_library.is_loaded()) {
            return true;  // already loaded: load() is idempotent
        }
        if (!_library.load()) {
            return false;  // library_t logged the Critical reason (empty path or the OS error)
        }

        // Both symbols are mandatory; a library exporting only one of them is not a plugin. The
        // error text is read into a string before logging, because logging may itself call into the
        // OS and replace the OS's last error.
        const std::string path(_library.path());
        const instance_entry_t instance_entry = _library.resolve_as<instance_entry_t>(instance_entry_name);
        if (instance_entry == nullptr) {
            const std::string error = _library.error_string();
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::CRITICAL,
                "plugin_loader_t: '" + path + "': missing entry point '" + instance_entry_name + "': " + error);
            _library.unload();
            return false;
        }

        const metadata_entry_t metadata_entry = _library.resolve_as<metadata_entry_t>(metadata_entry_name);
        if (metadata_entry == nullptr) {
            const std::string error = _library.error_string();
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::CRITICAL,
                "plugin_loader_t: '" + path + "': missing entry point '" + metadata_entry_name + "': " + error);
            _library.unload();
            return false;
        }

        _instance_entry = instance_entry;
        _metadata_entry = metadata_entry;
        _instance = nullptr;
        _instance_resolved = false;
        return true;
    }

    void plugin_loader_t::unload() {
        if (!_library.is_loaded()) {
            return;  // not loaded: nothing to do
        }
        if (_live_objects != 0) {
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::CRITICAL,
                "plugin_loader_t: refusing to unload '" + std::string(_library.path()) + "': " +
                    std::to_string(_live_objects) + " live object(s) remain");
            return;  // keep the library loaded: its static tables may still be referenced
        }

        _library.unload();
        _instance_entry = nullptr;
        _metadata_entry = nullptr;
        _instance = nullptr;
        _instance_resolved = false;
    }

    bool plugin_loader_t::is_loaded() const noexcept {
        return _library.is_loaded();
    }

    std::string_view plugin_loader_t::path() const noexcept {
        return _library.path();
    }

    std::string plugin_loader_t::error_string() const {
        return _library.error_string();
    }

    object_t* plugin_loader_t::instance() const noexcept {
        if (_instance_entry == nullptr) {
            return nullptr;
        }
        if (!_instance_resolved) {
            // Cached even when the answer is nullptr: a metadata-only plugin must not have its entry
            // point called again on every query.
            _instance = _instance_entry();
            _instance_resolved = true;
        }
        return _instance;
    }

    std::string_view plugin_loader_t::metadata() const noexcept {
        if (_metadata_entry == nullptr) {
            return { };
        }
        const char* text = _metadata_entry();
        return (text != nullptr) ? std::string_view(text) : std::string_view();
    }

    std::string plugin_loader_t::metadata_value(std::string_view key) const {
        const std::string_view text = metadata();
        if (text.empty()) {
            return { };
        }
        return parse_metadata_value(text, key);
    }

    std::string plugin_loader_t::parse_metadata_value(std::string_view metadata, std::string_view key) {
        while (!metadata.empty()) {
            const std::size_t newline = metadata.find('\n');
            std::string_view line = metadata.substr(0, newline);
            metadata.remove_prefix((newline == std::string_view::npos) ? metadata.size() : newline + 1);

            line = trim(line);
            if (line.empty() || line.front() == '#') {
                continue;  // blank line or comment
            }
            const std::size_t equals = line.find('=');
            if (equals == std::string_view::npos) {
                continue;  // not "key=value"; it cannot carry a key
            }
            const std::string_view name = trim(line.substr(0, equals));
            if (name != key) {
                continue;
            }
            return std::string(trim(line.substr(equals + 1)));  // split at the FIRST '='
        }
        return { };
    }

    void plugin_loader_t::register_object() noexcept {
        ++_live_objects;
    }

    void plugin_loader_t::unregister_object() noexcept {
        if (_live_objects != 0) {
            --_live_objects;
        }
    }

    std::size_t plugin_loader_t::live_object_count() const noexcept {
        return _live_objects;
    }
}
