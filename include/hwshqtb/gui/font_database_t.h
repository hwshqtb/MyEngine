#ifndef HWSHQTB__GUI__FONT_DATABASE_H
#define HWSHQTB__GUI__FONT_DATABASE_H

/// @file font_database_t.h
/// @brief font_database_t: the process-wide font registry that resolves a description to an engine
///        (the QFontDatabase / QPlatformFontDatabase role).

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hwshqtb/gui/export.h"
#include "hwshqtb/gui/font_t.h"

namespace hwshqtb::gui {
    /// @brief The process-wide font registry: families, the default family, and (family, size) → engine.
    ///
    /// @par One instance
    /// instance() returns the process's database, created on first use. It owns every engine, so an
    /// engine pointer stays valid for the life of the process — which is what lets font_metrics_t,
    /// text_layout_t and a paint engine keep engine pointers without reference counting.
    ///
    /// @par Resolution
    /// engine_for() maps a font_t to an engine and caches the answer per (family, pixel size). Every
    /// resolution succeeds: a family that does not exist falls back to the default family, and the
    /// default family itself falls back to the built-in bitmap face. Text therefore never disappears
    /// because a font is missing.
    ///
    /// @par Determinism
    /// builtin_family() names the built-in 5x7 face. A test or a golden-image capture pins it with
    /// set_default_family(builtin_family()) BEFORE creating any control, which makes every text pixel
    /// machine independent.
    class HWSHQTB_GUI_EXPORT font_database_t {
    public:
        /// @brief The process's font database, created on first use and never destroyed.
        /// @return The one instance.
        /// @note noexcept. Never destroyed on purpose: engines outlive static destruction (the same
        ///       reason the event dispatcher is never destroyed).
        static font_database_t& instance() noexcept;

        /// @brief The family name of the built-in bitmap face ("hwshqtb-mono-5x7").
        /// @return The name; a valid argument to set_default_family().
        /// @note noexcept. This is the deterministic face: pinning it makes text machine independent.
        static std::string_view builtin_family() noexcept;

        /// @brief Make @p family the default family.
        /// @param family The family name; empty (or unknown) restores the system default.
        /// @note Call it before creating controls: a font_metrics_t resolves once, at construction.
        void set_default_family(std::string_view family);

        /// @brief A counter that changes whenever the default family changes.
        /// @return The current generation; 0 before the first change.
        /// @note A CACHE KEY, nothing else: the style layer derives its text metrics from the resolved
        ///       face's line height, which is a database lookup, and pixel_metric() runs per row per
        ///       frame. A style caches one font_metrics_t and rebuilds it when this number moves.
        /// @note Single-threaded assumption: the ui layer resolves fonts and paints on one thread, so a
        ///       plain counter is enough (no atomics, no locks).
        unsigned generation() const noexcept;

        /// @brief The family a font with an empty family name resolves to.
        /// @note noexcept.
        std::string_view default_family() const noexcept;

        /// @brief The families this database knows about.
        /// @return At least the built-in family; the platform families follow when a platform font
        ///         backend is available.
        /// @note noexcept. The span is valid until set_default_family() is next called.
        std::span<const std::string_view> families() const noexcept;

        /// @brief Resolve @p font to an engine, caching the answer.
        /// @param font The description; an empty family means default_family(), a pixel size of 0 means
        ///        the family's default.
        /// @return The engine; never null (an unresolvable family falls back to the built-in face).
        /// @note noexcept: every failure — an unknown family, a missing platform backend, a GDI error,
        ///       a failed allocation — answers the built-in face instead of throwing. Metrics are built
        ///       on the paint path, so resolution must not be able to fail upward.
        /// @note The built-in bitmap face ignores the pixel size (it has one 5x7 size); a platform face
        ///       selects that pixel height.
        font_engine_t* engine_for(const font_t& font) noexcept;

    private:
        font_database_t();
        ~font_database_t();

        font_database_t(const font_database_t&) = delete;
        font_database_t& operator=(const font_database_t&) = delete;

        /// @brief The built-in bitmap engine: the fallback every chain ends with.
        std::unique_ptr<font_engine_t> _builtin;
        /// @brief The default family name.
        std::string _default_family;
        /// @brief Bumped by set_default_family(); the style cache keys on it.
        unsigned _generation = 0;
        /// @brief The family names handed out by families().
        std::vector<std::string_view> _families;
        /// @brief (family, pixel size) → resolved engine; the engine itself is owned here.
        /// @note A map, not a hash map: node addresses are stable, so a stored pointer never dangles
        ///       when another entry is inserted.
        std::map<std::pair<std::string, int>, font_engine_t*> _resolved;
        /// @brief Engines this database created (the built-in face is a member, not one of these).
        std::vector<std::unique_ptr<font_engine_t>> _owned;
    };
}

#endif
