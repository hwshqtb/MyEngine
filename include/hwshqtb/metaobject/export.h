#ifndef HWSHQTB__METAOBJECT__EXPORT_H
#define HWSHQTB__METAOBJECT__EXPORT_H

/// @file export.h
/// @brief Visibility marker for the public API of the shared library.

/// @brief Mark a public API entity as exported from the shared library.
///
/// Everything that is not annotated is hidden when the library is built with -fvisibility=hidden.
/// The annotated set is **the public API contract**: a name a consumer may use has to be marked here,
/// and an unmarked one stays outside the surface even when a header declares it.
///
/// The marker depends on the build shape:
///   - static library: HWSHQTB_BUILDING is not defined, so the marker expands to nothing — a static
///     library has no export/import distinction;
///   - building the shared library: HWSHQTB_BUILDING is defined, so the marker expands to
///     __declspec(dllexport) on PE targets and __attribute__((visibility("default"))) elsewhere;
///   - consumer of the shared library: HWSHQTB_BUILDING is not defined, so the marker expands to
///     nothing — deliberately so:
///       - PE targets ignore __attribute__((visibility)), so the DLL side needs __declspec(dllexport);
///       - consumers do NOT use dllimport, because META_OBJECT defines each class's
///         static_meta_object in the translation unit that owns the class (the facade owns its
///         table by value, so there is no shared mutable state to import). Marking the class
///         dllimport would turn a consumer-defined variable into an import the DLL does not export.
///
/// Annotate the CLASS, not its out-of-line member definitions: GCC/MinGW propagates a class-level
/// dllexport to every member function and static data member defined out of line, so src/*.cpp
/// repeats the marker only on functions that are not members of an annotated class (the registry
/// helpers in src/type_registry.cpp and detail::thread_data_t::current()).
///
/// @note The type_interface templates (type_interface_t.h) are deliberately NOT annotated: each module
///       keeps its own constant-initialised copy, and exporting them would force dllimport — which
///       makes their address unusable in a constant expression (Qt documents the same rule for
///       QMetaTypeInterface).
#if defined(HWSHQTB_BUILDING) && (defined(_WIN32) || defined(__CYGWIN__))
#    define HWSHQTB_EXPORT __declspec(dllexport)
#elif defined(HWSHQTB_BUILDING)
#    define HWSHQTB_EXPORT __attribute__((visibility("default")))
#else
#    define HWSHQTB_EXPORT
#endif

#endif
