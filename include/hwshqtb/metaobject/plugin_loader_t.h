#ifndef HWSHQTB__METAOBJECT__PLUGIN_LOADER_T_H
#define HWSHQTB__METAOBJECT__PLUGIN_LOADER_T_H

/// @file plugin_loader_t.h
/// @brief Host-side loader for one plugin shared library.

#include <cstddef>
#include <string>
#include <string_view>

#include "export.h"
#include "library_t.h"

namespace hwshqtb::metaobject {
    class object_t;

    /// @brief Loads exactly one plugin shared library (DLL / shared object) and exposes its main
    ///        object and metadata (the host half of Qt's qt_plugin_instance convention).
    ///
    /// A plugin is an ordinary shared library that exports two fixed C-linkage entry points:
    /// @code
    /// extern "C" {
    ///     object_t* metaobject_plugin_instance();     // the plugin's main object (may be nullptr)
    ///     const char* metaobject_plugin_metadata();   // "key=value" per line, static storage
    /// }
    /// @endcode
    /// The names are fixed so that nothing depends on C++ name mangling or on a compiler-agnostic
    /// registration macro. Both must be present; a library that exports only one of them is not a
    /// metaobject plugin and load() refuses it.
    ///
    /// Lifetime rules:
    ///  - the loader owns a library_t, which owns the OS handle and closes it in unload() /
    ///    ~plugin_loader_t();
    ///  - objects created through the plugin are counted by register_object(); unload() refuses to
    ///    close the library while that count is non-zero, because the plugin's static meta tables and
    ///    every caller pointer inside them would dangle afterwards.
    ///
    /// The class is deliberately not copyable: two loaders for the same path would each hold an OS
    /// handle and their own live-object counter, so the unload-safety rule could be bypassed.
    class HWSHQTB_EXPORT plugin_loader_t {
    public:
        /// @brief Construct a loader for @p path; nothing is opened until load().
        /// @param path The shared library file; stored as-is and handed to the OS loader.
        explicit plugin_loader_t(std::string path);

        /// @brief Unload the library (calls unload()).
        /// @note Never throws. If objects created through the plugin are still alive, the library
        ///       stays loaded; the process reclaims it at exit.
        ~plugin_loader_t();

        plugin_loader_t(const plugin_loader_t&) = delete;
        plugin_loader_t& operator=(const plugin_loader_t&) = delete;

        /// @brief Open the library and resolve both entry points.
        /// @return true on success; false when the library cannot be opened or an entry point is
        ///         missing (a Critical log names the reason in both cases).
        /// @note Idempotent: a second call on a loaded loader returns true without doing anything.
        bool load();

        /// @brief Close the library.
        /// @note Refused (with a Critical log naming the count) when live_object_count() is
        ///       non-zero; the library then stays loaded. A no-op on a loader that is not loaded.
        /// @note The cached instance pointer and the metadata view are invalidated on success.
        void unload();

        /// @brief Whether the library is currently open.
        /// @note noexcept.
        bool is_loaded() const noexcept;

        /// @brief The path this loader was constructed with.
        /// @note noexcept.
        std::string_view path() const noexcept;

        /// @brief The text of the last library failure.
        /// @return Forwarded from library_t: the OS error text of a failed load or of a missing
        ///         entry point; empty after a successful load().
        std::string error_string() const;

        /// @brief The plugin's main object.
        /// @return The pointer returned by the plugin's instance entry point, cached after the first
        ///         call; nullptr when the library is not loaded **or** when the plugin legitimately
        ///         has no main object (a metadata-only plugin).
        /// @note noexcept. The object is owned by the plugin, not by the loader.
        object_t* instance() const noexcept;

        /// @brief The plugin's raw metadata text ("key=value" lines).
        /// @return A view borrowed from the plugin's static storage; empty when the library is not
        ///         loaded or the entry point returns nullptr.
        /// @note noexcept. The view is valid only while the library stays loaded.
        std::string_view metadata() const noexcept;

        /// @brief One metadata value by key.
        /// @param key The key to look up, compared exactly against the trimmed left-hand side.
        /// @return The trimmed value of the first matching line; an empty string when the key is
        ///         absent, the metadata is empty, or the library is not loaded.
        /// @note Parsing happens on demand, line by line; no map is built.
        std::string metadata_value(std::string_view key) const;

        /// @brief Parse one "key=value" value out of a metadata block.
        /// @param metadata The whole block; one entry per line.
        /// @param key The key to look up.
        /// @return The first matching value, trimmed; an empty string when the key is absent.
        ///
        /// Rules: lines are split at '\n'; blank lines and lines whose first non-space character is
        /// '#' are skipped; each remaining line is split at the **first** '=', both sides are trimmed
        /// of ASCII whitespace, and a line without '=' is skipped (it cannot carry a key). A value may
        /// therefore contain '='. CRLF input needs no special handling because a trailing carriage
        /// return is trimmed.
        static std::string parse_metadata_value(std::string_view metadata, std::string_view key);

        /// @brief Register an object created through this plugin so unload() is refused while it lives.
        /// @note noexcept. The caller promises to call unregister_object() before destroying it.
        void register_object() noexcept;

        /// @brief Release one register_object() record (a no-op when the counter is already zero).
        /// @note noexcept.
        void unregister_object() noexcept;

        /// @brief How many objects created through this plugin are currently registered.
        /// @note noexcept.
        std::size_t live_object_count() const noexcept;

    private:
        /// @brief The OS-level wrapper that opens the library and resolves its symbols.
        library_t _library;

        /// @brief metaobject_plugin_instance, resolved by load().
        object_t* (*_instance_entry)() = nullptr;

        /// @brief metaobject_plugin_metadata, resolved by load().
        const char* (*_metadata_entry)() = nullptr;

        /// @brief Cached result of the instance entry point (nullptr is a valid, cached result).
        /// @note mutable: instance() is const but caches the plugin's answer.
        mutable object_t* _instance = nullptr;

        /// @brief Whether the instance entry point has been called already.
        mutable bool _instance_resolved = false;

        /// @brief Objects created through this plugin and not released yet.
        std::size_t _live_objects = 0;
    };
}

#endif
