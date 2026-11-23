#ifndef HWSHQTB__GUI__EXPORT_H
#define HWSHQTB__GUI__EXPORT_H

/// @file export.h
/// @brief Visibility marker for the public API of the gui library (H2).

/// @brief Mark a public GUI entity as exported from the gui shared library.
///
/// The mirror of include/hwshqtb/metaobject/export.h: same static/shared/consumer logic, its own
/// build switch. The switch is separate on purpose — HWSHQTB_BUILDING is defined PRIVATE by the
/// metaobject target, and reusing it here would make gui's translation units see metaobject's
/// classes as dllexport while compiling gui (exporting symbols whose owner is the metaobject DLL).
///
/// The three build shapes:
///   - static library: HWSHQTB_GUI_BUILDING is not defined, so the marker expands to nothing — a
///     static library has no export/import distinction;
///   - shared library: __declspec(dllexport) on PE targets, __attribute__((visibility("default")))
///     elsewhere, and gui is compiled with -fvisibility=hidden so the annotated set is the whole
///     export table;
///   - consumer of the shared library: the marker expands to nothing, deliberately without
///     dllimport — the same rule as metaobject/export.h. gui's exported classes carry no static
///     data members and no exported inline variables, so on PE/MinGW the import library's thunks
///     are enough; keeping dllimport out means the constexpr value types stay usable in constant
///     expressions in consumer translation units.
///
/// Annotate the CLASS, not its out-of-line member definitions: GCC/MinGW propagates a class-level
/// dllexport to every member function defined out of line (the behaviour was measured for
/// metaobject, see its export.h). src/gui/*.cpp therefore repeat the marker nowhere.
///
/// @note The value types of this layer — point_t, extent_t, rect_t (geometry.h), color_t (color_t.h),
///       transform_t (transform_t.h), engine_feature_flag / engine_feature_t / engine_state_t
///       (paint_engine_t.h) — are deliberately NOT annotated. They are constexpr / header-only types
///       whose definitions must be usable in constant expressions in every translation unit, and a
///       class-level import/export marker would break that (接口模板不导出).
#if defined(HWSHQTB_GUI_BUILDING) && (defined(_WIN32) || defined(__CYGWIN__))
#    define HWSHQTB_GUI_EXPORT __declspec(dllexport)
#elif defined(HWSHQTB_GUI_BUILDING)
#    define HWSHQTB_GUI_EXPORT __attribute__((visibility("default")))
#else
#    define HWSHQTB_GUI_EXPORT
#endif

#endif
