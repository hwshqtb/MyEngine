/// @file factory_loader_t.cpp
/// @brief Scan directories for plugins and index them by their metadata key.

#include <hwshqtb/metaobject/factory_loader_t.h>

#include <hwshqtb/metaobject/library_t.h>
#include <hwshqtb/metaobject/log.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hwshqtb::metaobject {
    namespace {
        /// @brief The metadata key that indexes a plugin in the factory.
        constexpr const char* key_entry = "key";

        /// @brief A stable spelling of @p path, used to remember what has been scanned already.
        std::string normalize(const std::filesystem::path& path) {
            std::error_code error;
            std::filesystem::path absolute = std::filesystem::absolute(path, error);
            if (error) {
                absolute = path;
            }
            return absolute.lexically_normal().string();
        }
    }

    void factory_loader_t::add_search_path(std::string_view dir) {
        _search_paths.emplace_back(dir);
    }

    void factory_loader_t::scan() {
        for (const std::string& directory : _search_paths) {
            scan_directory(directory);
        }
    }

    void factory_loader_t::scan_directory(const std::string& directory) {
        std::error_code error;
        std::filesystem::directory_iterator iterator(directory, error);
        if (error) {
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING,
                "factory_loader_t: cannot scan '" + directory + "': " + error.message());
            return;
        }

        // Collect first, then sort: the directory order is unspecified, and first-scanned-wins must
        // be deterministic for two scans / two machines to agree.
        std::vector<std::filesystem::path> candidates;
        const std::filesystem::directory_iterator end;
        while (iterator != end) {
            const std::filesystem::directory_entry& entry = *iterator;
            std::error_code type_error;
            if (entry.is_regular_file(type_error) && !type_error &&
                library_t::is_library(entry.path().string())) {
                candidates.push_back(entry.path());
            }
            iterator.increment(error);
            if (error) {
                HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING,
                    "factory_loader_t: stopped scanning '" + directory + "': " + error.message());
                break;
            }
        }

        std::sort(candidates.begin(), candidates.end());
        for (const std::filesystem::path& candidate : candidates) {
            scan_plugin(normalize(candidate));
        }
    }

    void factory_loader_t::scan_plugin(std::string path) {
        if (std::find(_scanned_paths.begin(), _scanned_paths.end(), path) != _scanned_paths.end()) {
            return;  // scan() is idempotent: this file has already been attempted
        }
        _scanned_paths.push_back(path);

        auto loader = std::make_unique<plugin_loader_t>(path);
        if (!loader->load()) {
            // The loader already logged the reason at Critical; the scan only reports the skip.
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING,
                "factory_loader_t: skipping '" + path + "': plugin load failed");
            return;  // the unique_ptr destroys the (still unloaded) loader
        }

        const std::string key(loader->metadata_value(key_entry));
        if (key.empty()) {
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING,
                "factory_loader_t: skipping '" + path + "': no 'key=' metadata");
            return;
        }
        if (_index.find(key) != _index.end()) {
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING,
                "factory_loader_t: skipping '" + path + "': duplicate key '" + key +
                    "' (the first scanned plugin wins)");
            return;
        }

        plugin_loader_t* registered = loader.get();
        _index.emplace(key, registered);
        _loaders.push_back(std::move(loader));
    }

    std::vector<std::string> factory_loader_t::keys() const {
        std::vector<std::string> result;
        result.reserve(_index.size());
        for (const auto& entry : _index) {
            result.push_back(entry.first);
        }
        return result;
    }

    const plugin_loader_t* factory_loader_t::find(std::string_view key) const {
        const auto entry = _index.find(key);
        return (entry != _index.end()) ? entry->second : nullptr;
    }

    object_t* factory_loader_t::create(std::string_view key) {
        const auto entry = _index.find(key);
        if (entry == _index.end()) {
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING,
                "factory_loader_t: no plugin registered for key '" + std::string(key) + "'");
            return nullptr;
        }

        plugin_loader_t* loader = entry->second;
        object_t* object = loader->instance();
        if (object == nullptr) {
            // Metadata-only plugins are legitimate: they loaded, they just have no main object.
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING,
                "factory_loader_t: plugin '" + std::string(loader->path()) + "' has no instance");
            return nullptr;
        }

        // Count the object against the loader so unload() is refused while it lives.
        loader->register_object();
        return object;
    }
}
