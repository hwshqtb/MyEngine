#ifndef HWSHQTB__METAOBJECT__LIBRARY_T_H
#define HWSHQTB__METAOBJECT__LIBRARY_T_H

/// @file library_t.h
/// @brief A generic dynamically loaded shared library (the QLibrary counterpart).

#include <string>
#include <string_view>

#include "export.h"

namespace hwshqtb::metaobject {
    /// @brief Owns one dynamically loaded shared library and resolves symbols from it.
    ///
    /// Wraps LoadLibraryW / GetProcAddress / FreeLibrary on Windows and dlopen / dlsym / dlclose on
    /// POSIX, keeps the text of the last failure for diagnostics, and knows which file names name a
    /// shared library (is_library()). plugin_loader_t builds the plugin protocol on top of it.
    ///
    /// @note resolve() never loads the library implicitly: a caller can tell "cannot open the
    ///       library" apart from "the library is open but does not export this symbol".
    ///
    /// Lifetime rules:
    ///  - the destructor unloads an open handle, so the library never outlives its wrapper;
    ///  - the class is not copyable: two wrappers for the same file would each hold a reference to
    ///    the same OS image and their "already loaded" shortcut and error state could disagree;
    ///  - the class is not thread-safe: loading, resolving and unloading one library from several
    ///    threads needs external synchronisation.
    class HWSHQTB_EXPORT library_t {
    public:
        /// @brief Construct an empty wrapper; set_path() names the library, load() opens it.
        /// @note noexcept.
        library_t() noexcept = default;

        /// @brief Construct a wrapper for @p path; nothing is opened until load().
        /// @param path The shared library file; stored as-is and handed to the OS loader.
        explicit library_t(std::string path);

        /// @brief Unload the library (calls unload()).
        /// @note Never throws.
        ~library_t();

        library_t(const library_t&) = delete;
        library_t& operator=(const library_t&) = delete;

        /// @brief Name the library to open.
        /// @param path The shared library file; stored as-is.
        /// @note An open library is unloaded first, so path() always describes the library that
        ///       is_loaded() refers to.
        void set_path(std::string path);

        /// @brief The file name passed to the constructor or set_path().
        /// @note noexcept.
        std::string_view path() const noexcept;

        /// @brief Open the library.
        /// @return true when the library is open afterwards — either a successful load, or an
        ///         already open library (load() is idempotent).
        /// @note On failure the OS error text is stored in error_string(), a Critical is logged,
        ///       and the wrapper stays unloaded (no half-open handle).
        bool load();

        /// @brief Close the library.
        /// @note A no-op when the wrapper is not loaded. Never throws.
        void unload();

        /// @brief Whether the library is currently open.
        /// @note noexcept.
        bool is_loaded() const noexcept;

        /// @brief Resolve one exported symbol.
        /// @param symbol The exact, case-sensitive symbol name.
        /// @return The symbol address, or nullptr when the library is not loaded or does not
        ///         export the symbol; both cases log a Critical and update error_string().
        /// @note Does **not** load the library implicitly (unlike QLibrary::resolve()).
        void* resolve(std::string_view symbol) const;

        /// @brief Resolve one symbol and cast it to a function-pointer type.
        /// @tparam Fn A function-pointer type, for example <tt>int (*)(int)</tt>.
        /// @return The typed address, or nullptr when resolve() returns nullptr.
        template <typename Fn>
        Fn resolve_as(std::string_view symbol) const {
            return reinterpret_cast<Fn>(resolve(symbol));
        }

        /// @brief The text of the last failure.
        /// @return The OS error ("GetLastError()=126 (...)" on Windows, dlerror()'s text on
        ///         POSIX), or an explanatory message when the OS supplied none (empty path,
        ///         resolve() on an unloaded library); empty after a successful load().
        std::string error_string() const;

        /// @brief Whether @p path names a shared library by its extension.
        /// @param path A file name or a full path; only the final component is examined.
        /// @return true for <c>.dll</c>, <c>.so</c>, <c>.so.&lt;digits&gt;</c> and <c>.dylib</c>
        ///         (case-insensitive); false otherwise.
        /// @note Static and filesystem-free: the answer depends on the spelling alone and cannot
        ///       throw.
        static bool is_library(std::string_view path);

    private:
        /// @brief The file name handed to the OS loader.
        std::string _path;

        /// @brief The OS handle (HMODULE / dlopen result); nullptr when not loaded.
        void* _handle = nullptr;

        /// @brief Text of the last failure; mutable because the const resolve() records errors.
        mutable std::string _error;
    };
}

#endif
