#ifndef HWSHQTB__METAOBJECT__FACTORY_LOADER_T_H
#define HWSHQTB__METAOBJECT__FACTORY_LOADER_T_H

/// @file factory_loader_t.h
/// @brief Scan directories for plugins and index them by their metadata key.

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "export.h"
#include "plugin_loader_t.h"

namespace hwshqtb::metaobject {
    class object_t;

    /// @brief Discovers plugin libraries in a set of directories and indexes them by key
    ///        (the QFactoryLoader equivalent).
    ///
    /// The two-step model mirrors Qt's loader pair:
    ///  - scan() opens each candidate library, reads its metadata and indexes it by the value of its
    ///    <c>key=</c> entry. It never calls the plugin's instance entry point, so a designer can list
    ///    the available components without constructing any of them;
    ///  - create(key) asks the indexed plugin for its main object and registers it as live, so the
    ///    plugin's library cannot be unloaded while that object exists.
    ///
    /// First-scanned wins: when two libraries declare the same key, the first one (in sorted path
    /// order inside one directory) keeps the index entry and the duplicate is skipped with a Warning,
    /// exactly like QFactoryLoader.
    ///
    /// The class owns every plugin_loader_t it creates; destroying the factory unloads the libraries
    /// whose live-object count is zero.
    class HWSHQTB_EXPORT factory_loader_t {
    public:
        /// @brief Add one directory to scan.
        /// @param dir The directory; it does not have to exist yet, scan() reports it instead.
        /// @note Duplicate directories are harmless: already-scanned files are remembered by path.
        void add_search_path(std::string_view dir);

        /// @brief Scan every added directory for plugin libraries and index them by their key.
        ///
        /// Every regular file whose name library_t::is_library() accepts (<c>.dll</c>, <c>.so</c>,
        /// <c>.so.&lt;digits&gt;</c>, <c>.dylib</c>) becomes a plugin_loader_t whose load() is
        /// attempted. A library that fails to load, declares no @c key= or repeats a key already
        /// seen is skipped with a Warning; the scan itself never throws and never aborts because
        /// of one bad candidate.
        ///
        /// Idempotent: a path that was already attempted is not loaded again on a later call.
        void scan();

        /// @brief The keys of every indexed plugin.
        /// @return The keys in ascending order (the index is ordered), empty when nothing is loaded.
        std::vector<std::string> keys() const;

        /// @brief Look up the loader registered for @p key.
        /// @return The loader, or nullptr when no plugin declares that key.
        const plugin_loader_t* find(std::string_view key) const;

        /// @brief Create the main object of the plugin registered for @p key.
        /// @param key The metadata key of the plugin.
        /// @return The plugin's main object, or nullptr when the key is unknown or the plugin has no
        ///         main object (a metadata-only plugin).
        /// @note Calls plugin_loader_t::instance() and, on success, plugin_loader_t::register_object():
        ///       the caller must unregister (or keep the object alive) before the plugin can unload.
        object_t* create(std::string_view key);

    private:
        /// @brief Heterogeneous key comparator: both sides collapse onto std::string_view, so a
        ///        lookup with std::string_view needs no temporary std::string.
        struct key_less_t {
            using is_transparent = void;

            bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
                return lhs < rhs;
            }
        };

        /// @brief Scan one directory (called by scan()); never throws.
        /// @param directory The directory path as it was added.
        void scan_directory(const std::string& directory);

        /// @brief Load and index one candidate library (called by scan_directory()).
        /// @param path The normalised file path; scan() remembers it, so it is attempted once.
        void scan_plugin(std::string path);

        /// @brief Directories added through add_search_path().
        std::vector<std::string> _search_paths;

        /// @brief Normalised paths already attempted, so a second scan() does not load them twice.
        std::vector<std::string> _scanned_paths;

        /// @brief The loaders this factory owns.
        std::vector<std::unique_ptr<plugin_loader_t>> _loaders;

        /// @brief key -> loader index; ordered so that keys() is deterministic. The transparent
        ///        comparator lets find()/create() look up a std::string_view without allocating.
        std::map<std::string, plugin_loader_t*, key_less_t> _index;
    };
}

#endif
