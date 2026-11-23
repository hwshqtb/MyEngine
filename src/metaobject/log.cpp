/// @file log.cpp
/// @brief A categorised logging facade (QLoggingCategory + qCDebug family).

#include "hwshqtb/metaobject/log.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwshqtb::metaobject {
    namespace {
        /// @brief The installed handler (empty = use the default one).
        log_handler& handler_slot() {
            static log_handler handler;
            return handler;
        }

        /// @brief Guards handler_slot().
        std::mutex& handler_mutex() {
            static std::mutex mutex;
            return mutex;
        }

        /// @brief The default sink: one line per message on stderr.
        bool default_handler(log_level_enum level, const log_context_t& context, std::string_view message) {
            std::fprintf(stderr, "[%s] %s\n", context.category, std::string(message).c_str());
            (void)level;
            return true;
        }

        // ---- filter rules --------------------------------------------------------------------

        /// @brief One parsed rule: a category pattern, the level it targets, and the switch.
        struct rule_t {
            /// @brief The category pattern; '*' matches any run, a trailing ".*" was folded to '*'.
            std::string pattern;
            /// @brief The level index 0..3, or -1 for "every level".
            int level = -1;
            /// @brief true to let matching messages through, false to drop them.
            bool enabled = false;
        };

        /// @brief Case-insensitive ASCII comparison (the level names are spelled in ASCII).
        bool ascii_iequals(std::string_view left, std::string_view right) noexcept {
            if (left.size() != right.size()) {
                return false;
            }
            for (std::size_t i = 0; i < left.size(); ++i) {
                char a = left[i];
                char b = right[i];
                if (a >= 'A' && a <= 'Z') {
                    a = static_cast<char>(a + ('a' - 'A'));
                }
                if (b >= 'A' && b <= 'Z') {
                    b = static_cast<char>(b + ('a' - 'A'));
                }
                if (a != b) {
                    return false;
                }
            }
            return true;
        }

        /// @brief Trim the surrounding spaces and tabs (and a stray carriage return).
        std::string_view trim(std::string_view text) noexcept {
            while (!text.empty()
                   && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
                text.remove_prefix(1);
            }
            while (!text.empty()
                   && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
                text.remove_suffix(1);
            }
            return text;
        }

        /// @brief The level index a rule names, or -1 when the text is not a level name.
        int level_index_from(std::string_view name) noexcept {
            if (ascii_iequals(name, "debug")) {
                return 0;
            }
            if (ascii_iequals(name, "info")) {
                return 1;
            }
            if (ascii_iequals(name, "warning")) {
                return 2;
            }
            if (ascii_iequals(name, "critical")) {
                return 3;
            }
            return -1;
        }

        /// @brief Parse a rule value: true/false, also 1/0.
        bool parse_switch(std::string_view text, bool& value) noexcept {
            if (ascii_iequals(text, "true") || text == "1") {
                value = true;
                return true;
            }
            if (ascii_iequals(text, "false") || text == "0") {
                value = false;
                return true;
            }
            return false;
        }

        /// @brief Whether a category pattern matches a category name ('*' matches any run, incl. none).
        bool pattern_matches(std::string_view pattern, std::string_view name) noexcept {
            if (pattern.empty()) {
                return true;
            }
            std::size_t p = 0;
            std::size_t n = 0;
            std::size_t star = std::string_view::npos;
            std::size_t resume = 0;
            while (n < name.size()) {
                if (p < pattern.size() && pattern[p] == name[n]) {
                    ++p;
                    ++n;
                } else if (p < pattern.size() && pattern[p] == '*') {
                    star = p;
                    ++p;
                    resume = n;
                } else if (star != std::string_view::npos) {
                    p = star + 1;
                    n = ++resume;
                } else {
                    return false;
                }
            }
            while (p < pattern.size() && pattern[p] == '*') {
                ++p;
            }
            return p == pattern.size();
        }

        /// @brief Parse a rule list into @p out; rules that do not parse are skipped silently.
        void parse_rules(std::string_view text, std::vector<rule_t>& out) {
            std::size_t begin = 0;
            while (begin <= text.size()) {
                std::size_t end = begin;
                while (end < text.size() && text[end] != ';' && text[end] != '\n') {
                    ++end;
                }
                const std::string_view raw = trim(text.substr(begin, end - begin));
                if (!raw.empty() && raw.front() != '#') {
                    const std::size_t equals = raw.find('=');
                    if (equals != std::string_view::npos) {
                        const std::string_view left = trim(raw.substr(0, equals));
                        const std::string_view right = trim(raw.substr(equals + 1));
                        bool enabled = false;
                        if (parse_switch(right, enabled)) {
                            const std::size_t dot = left.rfind('.');
                            const std::string_view last =
                                (dot == std::string_view::npos) ? left : left.substr(dot + 1);
                            const int level = level_index_from(last);
                            rule_t rule;
                            rule.enabled = enabled;
                            if (level >= 0) {
                                rule.level = level;
                                rule.pattern.assign(dot == std::string_view::npos
                                                        ? std::string_view{ }
                                                        : left.substr(0, dot));
                            } else {
                                rule.pattern.assign(left);
                            }
                            // "prefix.*" is the same as "prefix*".
                            if (rule.pattern.size() >= 2
                                && rule.pattern.compare(rule.pattern.size() - 2, 2, ".*") == 0) {
                                rule.pattern.resize(rule.pattern.size() - 1);
                            }
                            out.push_back(std::move(rule));
                        }
                    }
                }
                if (end == text.size()) {
                    break;
                }
                begin = end + 1;
            }
        }

        // ---- the category registry ------------------------------------------------------------

        /// @brief One registered category and the minimum it was constructed with (its reset state).
        struct category_entry_t {
            log_category_t* category = nullptr;
            log_level_enum minimum = log_level_enum::WARNING;
        };

        /// @brief Everything the registry owns; guarded by @c mutex.
        struct registry_t {
            std::mutex mutex;
            std::vector<category_entry_t> categories;
            std::vector<rule_t> environment_rules;
            bool environment_loaded = false;
            std::vector<rule_t> explicit_rules;
            std::string explicit_text;
            log_category_filter filter = nullptr;
        };

        /// @brief The one registry.
        /// @note Intentionally leaked (never destroyed): categories have static lifetime, so their
        ///       destructors may run during static destruction and still have to unregister.
        registry_t& registry() {
            static registry_t* state = new registry_t{ };
            return *state;
        }

        /// @brief Read HWSHQTB_LOGGING_RULES once; later calls are no-ops.
        /// @note The variable is read only here: set_filter_rules() never re-reads it, so an explicit
        ///       call always wins over the environment.
        void load_environment_locked(registry_t& state) {
            state.environment_loaded = true;
            if (const char* text = std::getenv("HWSHQTB_LOGGING_RULES")) {
                parse_rules(text, state.environment_rules);
            }
        }

        /// @brief Apply one rule list to one category, in order (a later match wins).
        void apply_rules(log_category_t& category, std::string_view name,
                         const std::vector<rule_t>& rules) {
            for (const rule_t& rule : rules) {
                if (!pattern_matches(rule.pattern, name)) {
                    continue;
                }
                if (rule.level < 0) {
                    for (int level = 0; level < 4; ++level) {
                        category.set_enabled(static_cast<log_level_enum>(level), rule.enabled);
                    }
                } else {
                    category.set_enabled(static_cast<log_level_enum>(rule.level), rule.enabled);
                }
            }
        }

        /// @brief Reset one category to its constructor minimum, then apply rules, then the filter.
        void apply_locked(registry_t& state, const category_entry_t& entry) {
            log_category_t& category = *entry.category;
            category.set_minimum_level(entry.minimum);
            const std::string_view name = category.category_name();
            apply_rules(category, name, state.environment_rules);
            apply_rules(category, name, state.explicit_rules);
            if (state.filter != nullptr) {
                state.filter(category);
            }
        }

        /// @brief Register a freshly constructed category and apply the current configuration to it.
        void register_category(log_category_t* category, log_level_enum minimum) {
            registry_t& state = registry();
            std::scoped_lock lock(state.mutex);
            if (!state.environment_loaded) {
                load_environment_locked(state);
            }
            state.categories.push_back(category_entry_t{ category, minimum });
            apply_locked(state, state.categories.back());
        }

        /// @brief Remove a category that is going away.
        void unregister_category(log_category_t* category) noexcept {
            registry_t& state = registry();
            std::scoped_lock lock(state.mutex);
            for (auto it = state.categories.begin(); it != state.categories.end(); ++it) {
                if (it->category == category) {
                    state.categories.erase(it);
                    return;
                }
            }
        }
    }

    log_category_t::log_category_t(const char* name, log_level_enum minimum) noexcept(false):
        _name(name == nullptr ? "" : name),
        _levels(detail::level_mask_from(minimum)) {
        register_category(this, minimum);
    }

    log_category_t::~log_category_t() noexcept(false) {
        unregister_category(this);
    }

    void install_log_handler(log_handler handler) {
        std::scoped_lock lock(handler_mutex());
        handler_slot() = std::move(handler);
    }

    const char* log_level_name(log_level_enum level) noexcept {
        switch (level) {
            case log_level_enum::DEBUG: return "debug";
            case log_level_enum::INFO: return "info";
            case log_level_enum::WARNING: return "warning";
            case log_level_enum::CRITICAL: return "critical";
            case log_level_enum::FATAL: return "fatal";
        }
        return "unknown";
    }

    void log(const log_category_t& category, log_level_enum level, const log_context_t& context,
             std::string_view message) {
        if (!category.is_enabled(level)) {
            return;
        }
        // The category is the message's, whatever the caller passed in the context.
        log_context_t effective = context;
        effective.category = category.category_name();
        bool consumed = false;
        {
            std::scoped_lock lock(handler_mutex());
            if (handler_slot()) {
                consumed = handler_slot()(level, effective, message);
            }
        }
        if (!consumed) {
            default_handler(level, effective, message);
        }
        if (level == log_level_enum::FATAL) {
            std::abort();
        }
    }

    // One object per image, defined here so the shared library holds the only copy: a header-defined
    // `inline` variable would give the DLL and every consumer executable their own filter level.
    const log_category_t& log_events() noexcept {
        static log_category_t category("metaobject.events");
        return category;
    }

    const log_category_t& log_connection() noexcept {
        static log_category_t category("metaobject.connection");
        return category;
    }

    const log_category_t& log_types() noexcept {
        static log_category_t category("metaobject.types");
        return category;
    }

    const log_category_t& default_category() noexcept {
        static log_category_t category("default");
        return category;
    }

    void set_filter_rules(std::string_view rules) {
        registry_t& state = registry();
        std::scoped_lock lock(state.mutex);
        if (!state.environment_loaded) {
            load_environment_locked(state);
        }
        std::string copy(rules);
        state.explicit_text = std::move(copy);
        state.explicit_rules.clear();
        parse_rules(state.explicit_text, state.explicit_rules);
        for (std::size_t i = 0; i < state.categories.size(); ++i) {
            apply_locked(state, state.categories[i]);
        }
    }

    std::string_view filter_rules() {
        registry_t& state = registry();
        std::scoped_lock lock(state.mutex);
        return state.explicit_text;
    }

    log_category_filter install_filter(log_category_filter filter) {
        registry_t& state = registry();
        std::scoped_lock lock(state.mutex);
        if (!state.environment_loaded) {
            load_environment_locked(state);
        }
        const log_category_filter previous = state.filter;
        state.filter = filter;
        for (std::size_t i = 0; i < state.categories.size(); ++i) {
            apply_locked(state, state.categories[i]);
        }
        return previous;
    }

    log_category_filter category_filter() {
        registry_t& state = registry();
        std::scoped_lock lock(state.mutex);
        return state.filter;
    }
}
