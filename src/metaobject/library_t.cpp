/// @file library_t.cpp
/// @brief A generic dynamically loaded shared library (the QLibrary counterpart).

#include <hwshqtb/metaobject/library_t.h>

#include <hwshqtb/metaobject/log.h>

#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

namespace hwshqtb::metaobject {
    namespace {
        /// @brief An open shared library; nullptr means "not loaded".
        using library_handle = void*;

        /// @brief The final path component: everything after the last '/' or '\\'.
        std::string_view file_name(std::string_view path) noexcept {
            const std::size_t separator = path.find_last_of("/\\");
            return (separator == std::string_view::npos) ? path : path.substr(separator + 1);
        }

        /// @brief An ASCII lower-case copy of @p text (the extension test is locale-independent).
        std::string lower(std::string_view text) {
            std::string result(text);
            for (char& c : result) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            return result;
        }

#if defined(_WIN32)
        /// @brief Convert a UTF-8 path to the UTF-16 form LoadLibraryW expects.
        std::wstring widen(std::string_view text) {
            if (text.empty()) {
                return std::wstring();
            }
            const int length = ::MultiByteToWideChar(CP_UTF8,
                                                     0,
                                                     text.data(),
                                                     static_cast<int>(text.size()),
                                                     nullptr,
                                                     0);
            if (length <= 0) {
                return std::wstring();
            }
            std::wstring wide(static_cast<std::size_t>(length), L'\0');
            ::MultiByteToWideChar(CP_UTF8,
                                  0,
                                  text.data(),
                                  static_cast<int>(text.size()),
                                  wide.data(),
                                  length);
            return wide;
        }

        /// @brief The last OS error as readable text (GetLastError()).
        std::string last_os_error() {
            const DWORD code = ::GetLastError();
            return "GetLastError()=" + std::to_string(static_cast<unsigned long>(code)) + " (" +
                   std::system_category().message(static_cast<int>(code)) + ")";
        }

        /// @brief Open a shared library; nullptr on failure.
        library_handle open_library(const std::string& path) {
            return static_cast<library_handle>(::LoadLibraryW(widen(path).c_str()));
        }

        /// @brief Resolve one symbol; nullptr when the library does not export it.
        library_handle find_symbol(library_handle handle, const std::string& symbol) {
            return reinterpret_cast<library_handle>(
                ::GetProcAddress(static_cast<HMODULE>(handle), symbol.c_str()));
        }

        /// @brief Close a shared library.
        void close_library(library_handle handle) noexcept {
            ::FreeLibrary(static_cast<HMODULE>(handle));
        }
#else
        /// @brief The last OS error as readable text (dlerror()).
        std::string last_os_error() {
            const char* error = ::dlerror();
            return (error != nullptr) ? std::string(error) : std::string("no dlerror() detail");
        }

        /// @brief Open a shared library; nullptr on failure.
        library_handle open_library(const std::string& path) {
            return ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        }

        /// @brief Resolve one symbol; nullptr when the library does not export it.
        library_handle find_symbol(library_handle handle, const std::string& symbol) {
            ::dlerror();  // clear a stale error so last_os_error() describes this lookup
            return ::dlsym(handle, symbol.c_str());
        }

        /// @brief Close a shared library.
        void close_library(library_handle handle) noexcept {
            ::dlclose(handle);
        }
#endif
    }

    library_t::library_t(std::string path):
        _path(std::move(path)) { }

    library_t::~library_t() {
        // The destructor must not throw, and unloading cannot fail in a way worth reporting.
        try {
            unload();
        }
        catch (...) {
        }
    }

    void library_t::set_path(std::string path) {
        unload();  // path() must not keep naming a library whose handle stays open
        _path = std::move(path);
    }

    std::string_view library_t::path() const noexcept {
        return _path;
    }

    bool library_t::load() {
        if (_handle != nullptr) {
            return true;  // already loaded: load() is idempotent
        }
        if (_path.empty()) {
            _error = "empty library path";
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::CRITICAL, "library_t: cannot load a library with an empty path");
            return false;
        }

        const library_handle handle = open_library(_path);
        if (handle == nullptr) {
            // Capture the OS error before logging: the log call may itself call into the OS.
            _error = last_os_error();
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::CRITICAL, "library_t: cannot load '" + _path + "': " + _error);
            return false;
        }

        _handle = handle;
        _error.clear();  // a successful load leaves no failure behind
        return true;
    }

    void library_t::unload() {
        if (_handle == nullptr) {
            return;  // not loaded: nothing to do
        }
        close_library(_handle);
        _handle = nullptr;
    }

    bool library_t::is_loaded() const noexcept {
        return _handle != nullptr;
    }

    void* library_t::resolve(std::string_view symbol) const {
        if (_handle == nullptr) {
            _error = "the library is not loaded";
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::CRITICAL,
                "library_t: cannot resolve '" + std::string(symbol) + "' in '" + _path +
                    "': the library is not loaded");
            return nullptr;  // deliberately no implicit load(), unlike QLibrary::resolve()
        }

        // Symbol names are NUL-terminated byte strings on both platforms; the caller's view is not.
        const std::string name(symbol);
        const library_handle address = find_symbol(_handle, name);
        if (address == nullptr) {
            // Capture the OS error before logging: the log call may itself call into the OS.
            _error = last_os_error();
            HWSHQTB_LOG_RAW(log_types(), log_level_enum::CRITICAL,
                "library_t: '" + _path + "': cannot resolve '" + name + "': " + _error);
            return nullptr;
        }
        return address;
    }

    std::string library_t::error_string() const {
        return _error;
    }

    bool library_t::is_library(std::string_view path) {
        // Only the final component matters and nothing is touched on disk: the answer is a
        // property of the spelling, so factory_loader_t can ask before it stats a file.
        const std::string name = lower(file_name(path));
        if (name.empty()) {
            return false;
        }
        if (name.ends_with(".dll") || name.ends_with(".dylib")) {
            return true;
        }

        // ".so" or ".so.<digits>": the version suffix is digits only, so "x.so.1" is a library
        // and "x.so.backup" is not.
        const std::size_t so = name.rfind(".so");
        if (so == std::string::npos) {
            return false;
        }
        if (so + 3 == name.size()) {
            return true;  // libb.so
        }
        if (name[so + 3] != '.') {
            return false;  // ".so" is not the start of the extension
        }

        std::string_view version(name);
        version.remove_prefix(so + 4);  // skip ".so."
        if (version.empty()) {
            return false;  // "x.so."
        }
        for (const char c : version) {
            if (c < '0' || c > '9') {
                return false;
            }
        }
        return true;
    }
}
