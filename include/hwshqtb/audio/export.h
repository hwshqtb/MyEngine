#ifndef HWSHQTB__AUDIO__EXPORT_H
#define HWSHQTB__AUDIO__EXPORT_H

/// @file export.h
/// @brief Visibility marker for the public API of the audio library.

/// @brief Mark a public audio entity as exported from the audio shared library.
///
/// The mirror of include/hwshqtb/ui/export.h: same static/shared/consumer logic, its own build switch.
/// The switch is separate on purpose — HWSHQTB_UI_BUILDING is defined PRIVATE by the ui target, and
/// reusing it here would make audio's translation units see ui's classes as dllexport while compiling
/// audio (exporting symbols whose owner is the ui DLL).
///
/// The three build shapes:
///   - static library: HWSHQTB_AUDIO_BUILDING is not defined, so the marker expands to nothing — a
///     static library has no export/import distinction;
///   - shared library: __declspec(dllexport) on PE targets, __attribute__((visibility("default")))
///     elsewhere, and audio is compiled with -fvisibility=hidden so the annotated set is the whole
///     export table;
///   - consumer of the shared library: the marker expands to nothing, deliberately without dllimport —
///     the same rule as metaobject/export.h, gui/export.h and ui/export.h.
///
/// Annotate the CLASS, not its out-of-line member definitions: GCC/MinGW propagates a class-level
/// dllexport to every member function defined out of line.
///
/// @note audio_format_t is a constexpr header-only value type (接口模板不导出) and is NOT annotated.
#if defined(HWSHQTB_AUDIO_BUILDING) && (defined(_WIN32) || defined(__CYGWIN__))
#    define HWSHQTB_AUDIO_EXPORT __declspec(dllexport)
#elif defined(HWSHQTB_AUDIO_BUILDING)
#    define HWSHQTB_AUDIO_EXPORT __attribute__((visibility("default")))
#else
#    define HWSHQTB_AUDIO_EXPORT
#endif

#endif
