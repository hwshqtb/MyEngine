#ifndef HWSHQTB__METAOBJECT__LOG_H
#define HWSHQTB__METAOBJECT__LOG_H

/// @file log.h
/// @brief A categorised logging facade (QLoggingCategory + qCDebug family).

#include <atomic>
#include <cstdint>
#include <format>
#include <functional>
#include <string_view>

#include "basic_def.h"
#include "export.h"

namespace hwshqtb::metaobject {
    namespace detail {
        /// @brief The level mask that enables every level at or above @p minimum.
        /// @param minimum The lowest enabled level.
        /// @return Bit i set for every level i at or above @p minimum; FATAL has no bit and is never
        ///         part of the mask because it cannot be filtered.
        /// @note constexpr so log_category_t's constructor can be constexpr.
        constexpr std::uint8_t level_mask_from(log_level_enum minimum) noexcept {
            std::uint8_t mask = 0;
            for (std::uint8_t bit = 0; bit < static_cast<std::uint8_t>(log_level_enum::FATAL); ++bit) {
                if (bit >= static_cast<std::uint8_t>(minimum)) {
                    mask = static_cast<std::uint8_t>(mask | (1u << bit));
                }
            }
            return mask;
        }
    }

    /// @brief A named logging category with one on/off bit per level.
    ///
    /// A category lets one subsystem be silenced without silencing anything else. Each level below
    /// FATAL has its own bit, so a filter can turn WARNING off while leaving CRITICAL on;
    /// set_minimum_level() is the "raise or lower the whole threshold" convenience on top of that.
    ///
    /// @note Non-copyable: a copy carries its own bits, so the same category could be enabled in one
    ///       place and disabled in another. The built-in categories are reached through the accessors
    ///       (log_events() and friends), which answer ONE object per image.
    /// @note FATAL is never filtered: is_enabled(FATAL) is always true and set_enabled(FATAL, …) is a
    ///       no-op. A FATAL message is emitted and then aborts the process.
    class HWSHQTB_EXPORT log_category_t {
    public:
        /// @brief Construct a category and register it with the filter registry.
        /// @param name A static string; it is not copied.
        /// @param minimum Lowest level that is enabled initially; everything below it starts disabled.
        /// @note Locks the registry, so it is not noexcept: the registration happens first, then the
        ///       category's filter rules and the installed filter are applied — so a rule set BEFORE
        ///       the category's first use still takes effect (the built-in categories are function-local
        ///       statics).
        explicit log_category_t(const char* name,
                                log_level_enum minimum = log_level_enum::WARNING) noexcept(false);

        /// @brief Remove the category from the filter registry.
        /// @note Locks the registry, so it is not noexcept.
        /// @note Not virtual; the registry is intentionally leaked so this is safe during static
        ///       destruction.
        ~log_category_t() noexcept(false);

        log_category_t(const log_category_t&) = delete;
        log_category_t& operator=(const log_category_t&) = delete;

        /// @brief The category name.
        /// @note noexcept.
        const char* category_name() const noexcept {
            return _name;
        }

        /// @brief Whether messages of @p level pass this category's filter.
        /// @param level The level to test.
        /// @return true when the level's bit is set; always true for FATAL (and for any value at or
        ///         above it).
        /// @note noexcept; may be called concurrently with the setters.
        bool is_enabled(log_level_enum level) const noexcept {
            const auto index = static_cast<std::uint8_t>(level);
            if (index >= static_cast<std::uint8_t>(log_level_enum::FATAL)) {
                return true;
            }
            const auto bit = static_cast<std::uint8_t>(1u << index);
            return (this->_levels.load(std::memory_order_relaxed) & bit) != 0;
        }

        /// @brief Whether DEBUG passes the filter.
        /// @note noexcept.
        bool is_debug_enabled() const noexcept {
            return this->is_enabled(log_level_enum::DEBUG);
        }

        /// @brief Whether INFO passes the filter.
        /// @note noexcept.
        bool is_info_enabled() const noexcept {
            return this->is_enabled(log_level_enum::INFO);
        }

        /// @brief Whether WARNING passes the filter.
        /// @note noexcept.
        bool is_warning_enabled() const noexcept {
            return this->is_enabled(log_level_enum::WARNING);
        }

        /// @brief Whether CRITICAL passes the filter.
        /// @note noexcept.
        bool is_critical_enabled() const noexcept {
            return this->is_enabled(log_level_enum::CRITICAL);
        }

        /// @brief Switch one level on or off.
        /// @param level The level to switch; FATAL is ignored, it is never filtered.
        /// @param enabled true to let the level through, false to drop it.
        /// @note noexcept; concurrency-safe (relaxed atomic).
        /// @note const on purpose, like Qt's setEnabled(): the bits are mutable because a filter is
        ///       normally driven from configuration, not from the object's own state. Call it from
        ///       setup code, not from the message path.
        void set_enabled(log_level_enum level, bool enabled) const noexcept {
            const auto index = static_cast<std::uint8_t>(level);
            if (index >= static_cast<std::uint8_t>(log_level_enum::FATAL)) {
                return;
            }
            const auto bit = static_cast<std::uint8_t>(1u << index);
            if (enabled) {
                this->_levels.fetch_or(bit, std::memory_order_relaxed);
            } else {
                this->_levels.fetch_and(static_cast<std::uint8_t>(~bit), std::memory_order_relaxed);
            }
        }

        /// @brief Enable every level at or above @p minimum and disable the levels below it.
        /// @param minimum The lowest enabled level; FATAL leaves every bit off (FATAL itself stays on).
        /// @note noexcept; replaces the whole mask in one atomic store.
        /// @note const for the same reason as set_enabled().
        void set_minimum_level(log_level_enum minimum) const noexcept {
            this->_levels.store(detail::level_mask_from(minimum), std::memory_order_relaxed);
        }

    private:
        /// @brief Category name (static storage).
        const char* _name = "";
        /// @brief One bit per level below FATAL (bit i = level i enabled); mutable so the setters can be const.
        mutable std::atomic<std::uint8_t> _levels = 0;
    };

    /// @brief Where a record came from and which category emitted it (Qt's QMessageLogContext).
    struct HWSHQTB_EXPORT log_context_t {
        /// @brief The source file; nullptr when unknown.
        const char* file = nullptr;
        /// @brief The source line; 0 when unknown.
        int line = 0;
        /// @brief The enclosing function; nullptr when unknown.
        const char* function = nullptr;
        /// @brief The category name; filled in by log() from the category itself.
        const char* category = nullptr;
    };

    /// @brief A log sink.
    /// @param level The message severity.
    /// @param context Its source location and category (category is always filled in).
    /// @param message The formatted message.
    /// @return true when the handler consumed the message; false falls back to the default handler.
    using log_handler = std::function<bool(log_level_enum level, const log_context_t& context, std::string_view message)>;

    /// @brief Install a log handler (qInstallMessageHandler).
    /// @param handler The handler; an empty function restores the default (stderr).
    HWSHQTB_EXPORT void install_log_handler(log_handler handler);

    /// @brief Emit a message through a category.
    /// @param category The emitting category.
    /// @param level The severity; FATAL aborts the process after the handler ran.
    /// @param context The source location; its category member is overwritten with
    ///        category.category_name(), so a caller may pass nullptr there.
    /// @param message The already-formatted message.
    /// @note Filters: a disabled level is dropped before any handler is consulted.
    /// @note Call HWSHQTB_LOG / HWSHQTB_LOG_RAW instead of this function: they add the source
    ///       location and evaluate the message only when the level is enabled.
    HWSHQTB_EXPORT void log(const log_category_t& category, log_level_enum level,
                            const log_context_t& context, std::string_view message);

    /// @brief The level name as printed by the default handler ("debug" / "info" / ...).
    /// @note noexcept.
    HWSHQTB_EXPORT const char* log_level_name(log_level_enum level) noexcept;

    /// @name Built-in categories
    /// @{
    /// @brief Events, timers and the dispatcher.
    /// @return The process-wide category; the same object on every call.
    /// @note A function rather than an `inline` variable: the library and its consumers must share
    ///       ONE filter level, and a header-defined inline variable gives each image its own copy.
    /// @note noexcept.
    HWSHQTB_EXPORT const log_category_t& log_events() noexcept;

    /// @brief Connections, signal dispatch and object lifetime.
    /// @return The process-wide category; the same object on every call.
    /// @note As log_events().
    /// @note noexcept.
    HWSHQTB_EXPORT const log_category_t& log_connection() noexcept;

    /// @brief Type registry and meta-type resolution.
    /// @return The process-wide category; the same object on every call.
    /// @note As log_events().
    /// @note noexcept.
    HWSHQTB_EXPORT const log_category_t& log_types() noexcept;

    /// @brief The category a message goes to when the caller names none (qCDebug's defaultCategory()).
    /// @return The process-wide "default" category.
    /// @note noexcept.
    HWSHQTB_EXPORT const log_category_t& default_category() noexcept;
    /// @}

    /// @name Filter rules and the category filter (QLoggingCategory::setFilterRules / CategoryFilter)
    /// @{
    /// @brief Replace the filter rules; applied to every registered category immediately and to every
    ///        category registered later.
    /// @param rules One or more rules, separated by ';' or newline.
    ///
    /// Syntax (a subset of Qt's): `category[.level]=value`
    ///   - an empty rule and a rule starting with '#' are skipped; both ends are trimmed;
    ///   - the first '=' splits the rule;
    ///   - the left side is split on '.': when its LAST segment names a level (debug / info / warning /
    ///     critical, case-insensitive) that level is the target and the rest is the category pattern,
    ///     otherwise the whole left side is the pattern;
    ///   - in a pattern, `*` matches any run of characters (including none) and a trailing `.*` is
    ///     the same as `*`; an empty pattern matches every category;
    ///   - the value is true / false (also 1 / 0);
    ///   - rules are applied in order, so a LATER match wins;
    ///   - a rule that does not parse is skipped silently;
    ///   - FATAL is never touched (it is not filtered at all).
    ///
    /// @note This OVERWRITES any manual set_enabled()/set_minimum_level() on the registered
    ///       categories: every category is reset to its constructor's minimum first, then the rules
    ///       are applied (Qt has the same semantics).
    /// @note The HWSHQTB_LOGGING_RULES environment variable is read ONCE, lazily, on the first use of
    ///       the registry, and applied BEFORE these rules — an explicit call therefore wins. No .ini
    ///       file is read (Qt's qtlogging.ini is deliberately not supported).
    /// @note Only `*` is a wildcard (Qt also has `?`); the value accepts true/false and 1/0 (Qt also
    ///       accepts yes/no). Both are deliberate omissions.
    /// @note Locks the registry, so it is not noexcept.
    HWSHQTB_EXPORT void set_filter_rules(std::string_view rules);

    /// @brief The rules currently in force.
    /// @return The string the last set_filter_rules() was given (empty when none was set). The view is
    ///         invalidated by the next set_filter_rules().
    /// @note The environment rules are not part of this string: it is the explicit configuration.
    /// @note Locks the registry, so it is not noexcept.
    HWSHQTB_EXPORT std::string_view filter_rules();

    /// @brief Qt's CategoryFilter: called for every registered category right after the rules, so it
    ///        can adjust that category's levels.
    /// @param category The category to adjust; it may change its levels (the accessors stay const,
    ///        the filter itself receives a mutable reference).
    using log_category_filter = void (*)(log_category_t&);

    /// @brief Install the category filter (qInstallMessageHandler's counterpart for categories).
    /// @param filter The filter, or nullptr to remove the installed one.
    /// @return The previously installed filter (nullptr when none was).
    /// @note Installing or removing a filter re-applies "rules, then filter" to every registered
    ///       category.
    /// @note The filter runs with the registry lock held: it must not call install_filter() or
    ///       set_filter_rules().
    /// @note Locks the registry, so it is not noexcept.
    HWSHQTB_EXPORT log_category_filter install_filter(log_category_filter filter);

    /// @brief The installed category filter, or nullptr.
    /// @note Locks the registry, so it is not noexcept.
    HWSHQTB_EXPORT log_category_filter category_filter();
    /// @}
}

/// @brief Emit a formatted message through @p category at @p level.
/// @note Checks the category FIRST: the format arguments are evaluated only when the level is on.
/// @note The category expression is evaluated once, into a local reference.
/// @note The record carries __FILE__, __LINE__ and __func__.
#define HWSHQTB_LOG(category, level, ...)                                                        \
    do {                                                                                         \
        const ::hwshqtb::metaobject::log_category_t& hwshqtb_log_category_ = (category);          \
        if (hwshqtb_log_category_.is_enabled(level)) {                                            \
            ::hwshqtb::metaobject::log(                                                           \
                hwshqtb_log_category_, level,                                                     \
                ::hwshqtb::metaobject::log_context_t{ __FILE__, __LINE__, __func__, nullptr },     \
                ::std::format(__VA_ARGS__));                                                      \
        }                                                                                         \
    } while (false)

/// @brief Emit an ALREADY-formatted message through @p category at @p level.
/// @note For a message that is a string / string_view / expression: nothing is formatted. The
///       expression is still evaluated only when the level is on.
#define HWSHQTB_LOG_RAW(category, level, message)                                                \
    do {                                                                                         \
        const ::hwshqtb::metaobject::log_category_t& hwshqtb_log_category_ = (category);          \
        if (hwshqtb_log_category_.is_enabled(level)) {                                            \
            ::hwshqtb::metaobject::log(                                                           \
                hwshqtb_log_category_, level,                                                     \
                ::hwshqtb::metaobject::log_context_t{ __FILE__, __LINE__, __func__, nullptr },     \
                (message));                                                                       \
        }                                                                                         \
    } while (false)

/// @name Level shorthands (qCDebug / qCInfo / qCWarning / qCCritical families)
/// @{
/// @brief Emit at DEBUG through @p category.
#define HWSHQTB_DEBUG(category, ...)                                                             \
    HWSHQTB_LOG(category, ::hwshqtb::metaobject::log_level_enum::DEBUG, __VA_ARGS__)

/// @brief Emit at INFO through @p category.
#define HWSHQTB_INFO(category, ...)                                                              \
    HWSHQTB_LOG(category, ::hwshqtb::metaobject::log_level_enum::INFO, __VA_ARGS__)

/// @brief Emit at WARNING through @p category.
#define HWSHQTB_WARNING(category, ...)                                                           \
    HWSHQTB_LOG(category, ::hwshqtb::metaobject::log_level_enum::WARNING, __VA_ARGS__)

/// @brief Emit at CRITICAL through @p category.
#define HWSHQTB_CRITICAL(category, ...)                                                          \
    HWSHQTB_LOG(category, ::hwshqtb::metaobject::log_level_enum::CRITICAL, __VA_ARGS__)

/// @brief Emit at FATAL through @p category; FATAL is never filtered and log() aborts afterwards.
#define HWSHQTB_FATAL(category, ...)                                                             \
    HWSHQTB_LOG(category, ::hwshqtb::metaobject::log_level_enum::FATAL, __VA_ARGS__)
/// @}

/// @name The same shorthands on the default category
/// @{
/// @brief Emit at @p level through default_category().
#define HWSHQTB_LOG_DEFAULT(level, ...)                                                          \
    HWSHQTB_LOG(::hwshqtb::metaobject::default_category(), level, __VA_ARGS__)

/// @brief Emit an already-formatted message at @p level through default_category().
#define HWSHQTB_LOG_RAW_DEFAULT(level, message)                                                  \
    HWSHQTB_LOG_RAW(::hwshqtb::metaobject::default_category(), level, message)

/// @brief Emit at DEBUG through default_category().
#define HWSHQTB_DEBUG_DEFAULT(...)                                                               \
    HWSHQTB_DEBUG(::hwshqtb::metaobject::default_category(), __VA_ARGS__)

/// @brief Emit at INFO through default_category().
#define HWSHQTB_INFO_DEFAULT(...)                                                                \
    HWSHQTB_INFO(::hwshqtb::metaobject::default_category(), __VA_ARGS__)

/// @brief Emit at WARNING through default_category().
#define HWSHQTB_WARNING_DEFAULT(...)                                                             \
    HWSHQTB_WARNING(::hwshqtb::metaobject::default_category(), __VA_ARGS__)

/// @brief Emit at CRITICAL through default_category().
#define HWSHQTB_CRITICAL_DEFAULT(...)                                                            \
    HWSHQTB_CRITICAL(::hwshqtb::metaobject::default_category(), __VA_ARGS__)

/// @brief Emit at FATAL through default_category(); log() aborts afterwards.
#define HWSHQTB_FATAL_DEFAULT(...)                                                               \
    HWSHQTB_FATAL(::hwshqtb::metaobject::default_category(), __VA_ARGS__)
/// @}

#endif
