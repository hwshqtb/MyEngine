#ifndef HWSHQTB__UI__EXPORT_H
#define HWSHQTB__UI__EXPORT_H

/// @file export.h
/// @brief Visibility marker for the public API of the ui library (W1/W2).

/// @brief Mark a public ui entity as exported from the ui shared library.
///
/// The mirror of include/hwshqtb/gui/export.h: same static/shared/consumer logic, its own build
/// switch. The switch is separate on purpose — HWSHQTB_GUI_BUILDING is defined PRIVATE by the gui
/// target, and reusing it here would make ui's translation units see gui's classes as dllexport
/// while compiling ui (exporting symbols whose owner is the gui DLL).
///
/// The three build shapes:
///   - static library: HWSHQTB_UI_BUILDING is not defined, so the marker expands to nothing — a
///     static library has no export/import distinction;
///   - shared library: __declspec(dllexport) on PE targets, __attribute__((visibility("default")))
///     elsewhere, and ui is compiled with -fvisibility=hidden so the annotated set is the whole
///     export table;
///   - consumer of the shared library: the marker expands to nothing, deliberately without
///     dllimport — the same rule as metaobject/export.h and gui/export.h. The exported classes carry
///     no exported inline variables, so the import library's thunks are enough, and keeping
///     dllimport out leaves every header-only value type usable in constant expressions.
///
/// Annotate the CLASS, not its out-of-line member definitions: GCC/MinGW propagates a class-level
/// dllexport to every member function and static data member defined out of line (the behaviour was
/// measured for metaobject, see its export.h). src/ui/*.cpp therefore repeat the marker nowhere.
///
/// @note interaction_t / modifier_flag (gui/interaction_t.h) and the geometry value types (point_t,
///       extent_t, rect_t, margins_t) are NOT annotated anywhere: they are constexpr header-only
///       types (接口模板不导出).
#if defined(HWSHQTB_UI_BUILDING) && (defined(_WIN32) || defined(__CYGWIN__))
#    define HWSHQTB_UI_EXPORT __declspec(dllexport)
#elif defined(HWSHQTB_UI_BUILDING)
#    define HWSHQTB_UI_EXPORT __attribute__((visibility("default")))
#else
#    define HWSHQTB_UI_EXPORT
#endif

#endif
