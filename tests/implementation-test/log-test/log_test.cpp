// M14: the logging facade — handler interception, per-level filtering, source context, fallback to the
// default sink.

#include <meta>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <hwshqtb/metaobject/basic_def.h>
#include <hwshqtb/metaobject/log.h>
#include <hwshqtb/metaobject/meta_type_t.h>
#include <hwshqtb/metaobject/object_t.h>

using namespace hwshqtb::metaobject;

namespace log_test {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// One captured log record, including the source context the macro attached.
struct record_t {
    std::string category;
    log_level_enum level = log_level_enum::DEBUG;
    std::string message;
    const char* file = nullptr;
    int line = 0;
    const char* function = nullptr;
};

std::vector<record_t>& records() {
    static std::vector<record_t> captured;
    return captured;
}

/// The recording handler every section installs.
void record_one(log_level_enum level, const log_context_t& context, std::string_view message) {
    records().push_back(record_t{ std::string(context.category), level, std::string(message),
                                  context.file, context.line, context.function });
}

void install_recorder() {
    install_log_handler([](log_level_enum level, const log_context_t& context, std::string_view message) {
        record_one(level, context, message);
        return true; // consumed: the default sink stays silent
    });
}

}  // namespace log_test

using namespace log_test;  // the file-local test namespace stays the file's own vocabulary

int main() {
    std::printf("== M14: logging facade ==\n");

    // No application object here, so the builtin names this test resolves by name are registered
    // explicitly (core_application's constructor normally does it).
    (void)meta_type_t::register_type<int>();
    (void)meta_type_t::register_type<bool>();
    (void)meta_type_t::register_type<double>();

    install_recorder();

    // (1) A handler sees the message together with its category and level.
    HWSHQTB_LOG_RAW(log_events(), log_level_enum::CRITICAL, "boom");
    CHECK(records().size() == 1);
    CHECK(records()[0].category == "metaobject.events");
    CHECK(records()[0].level == log_level_enum::CRITICAL);
    CHECK(records()[0].message == "boom");

    // (1b) The macros attach a source context: file, line and function are filled in.
    CHECK(records()[0].file != nullptr);
    CHECK(records()[0].line > 0);
    CHECK(records()[0].function != nullptr);
    CHECK(std::string_view(records()[0].function) == "main");

    // (1c) log() stamps the emitting category into the context, whatever the caller put there.
    {
        log_context_t stamped{};
        stamped.category = "bogus";
        log(log_events(), log_level_enum::CRITICAL, stamped, "stamped");
        CHECK(records().size() == 2);
        CHECK(records()[1].category == "metaobject.events");
        records().pop_back();                  // keep the section's record count predictable
    }

    // (2) Categories default to Warning and are independent.
    CHECK(log_events().is_enabled(log_level_enum::WARNING));
    CHECK(!log_events().is_enabled(log_level_enum::INFO));
    HWSHQTB_LOG_RAW(log_events(), log_level_enum::INFO, "dropped");
    CHECK(records().size() == 1);            // below the threshold: not emitted at all

    // (2a) A default-constructed category has one bit per level, not a single threshold: the levels
    //      below the minimum are off and the ones above are on. Scoped so the category is gone before
    //      the filter rules below run over every registered category.
    {
        const log_category_t local("local");
        CHECK(local.is_debug_enabled() != local.is_warning_enabled());
        CHECK(!local.is_debug_enabled());
        CHECK(!local.is_info_enabled());
        CHECK(local.is_warning_enabled());
        CHECK(local.is_critical_enabled());
    }

    // (2b) A built-in category is ONE object per image: the accessor answers the same object on
    //      every call (a header-defined variable would give the DLL and each consumer their own
    //      filter level instead), and the three categories are distinct objects.
    CHECK(&log_events() == &log_events());
    CHECK(&log_connection() != &log_events());
    static_assert(!std::is_copy_constructible_v<log_category_t>,
                  "a copied category would carry its own filter level");

    HWSHQTB_LOG_RAW(log_connection(), log_level_enum::CRITICAL, "conn");
    CHECK(records().size() == 2);
    CHECK(records()[1].category == "metaobject.connection");

    // (3) Lowering one category does not affect another.
    log_events().set_minimum_level(log_level_enum::DEBUG);
    CHECK(log_events().is_enabled(log_level_enum::INFO));
    CHECK(!log_connection().is_enabled(log_level_enum::INFO));
    HWSHQTB_LOG_RAW(log_events(), log_level_enum::INFO, "kept");
    HWSHQTB_LOG_RAW(log_connection(), log_level_enum::INFO, "still dropped");
    CHECK(records().size() == 3);
    CHECK(records()[2].message == "kept");

    // (4) Restore the default threshold so later tests are not affected.
    log_events().set_minimum_level(log_level_enum::WARNING);
    CHECK(!log_events().is_enabled(log_level_enum::INFO));

    // (5) A handler returning false falls back to the default sink (stderr); the record is still
    //     handed to the handler first.
    const std::size_t before = records().size();
    install_log_handler([](log_level_enum, const log_context_t&, std::string_view) {
        return false;                        // not consumed -> default sink prints it
    });
    HWSHQTB_LOG_RAW(log_types(), log_level_enum::WARNING, "fallback to stderr");
    CHECK(records().size() == before);        // the new handler does not record

    // (6) An empty handler restores the built-in one.
    install_log_handler({ });
    HWSHQTB_LOG_RAW(log_types(), log_level_enum::DEBUG, "below threshold, silent");

    // (6a) Each level has its OWN bit: switching WARNING off leaves CRITICAL on. The setters are const
    //      (Qt's shape), so they work on the const object a category accessor hands out.
    {
        const log_category_t per_level("per.level");
        CHECK(per_level.is_warning_enabled());
        per_level.set_enabled(log_level_enum::WARNING, false);
        CHECK(!per_level.is_warning_enabled());
        CHECK(per_level.is_critical_enabled());
        CHECK(per_level.is_enabled(log_level_enum::CRITICAL));
        per_level.set_enabled(log_level_enum::WARNING, true);
        CHECK(per_level.is_warning_enabled());
    }

    // (6b) FATAL is never filtered: it is always enabled and cannot be switched off.
    {
        log_category_t fatal_off("fatal.off");
        CHECK(fatal_off.is_enabled(log_level_enum::FATAL));
        fatal_off.set_enabled(log_level_enum::FATAL, false);
        CHECK(fatal_off.is_enabled(log_level_enum::FATAL));
        fatal_off.set_minimum_level(log_level_enum::FATAL);
        CHECK(fatal_off.is_enabled(log_level_enum::FATAL));
        CHECK(!fatal_off.is_enabled(log_level_enum::CRITICAL));   // every bit below FATAL is off
    }

    // (6c) The message is built only when the level is enabled: a dropped level costs no formatting.
    {
        int evaluated = 0;
        const auto bump = [&evaluated]() { return ++evaluated; };
        install_recorder();
        const std::size_t start = records().size();
        log_category_t quiet("quiet");            // Warning on, Debug off
        HWSHQTB_DEBUG(quiet, "value={}", bump());
        CHECK(evaluated == 0);
        HWSHQTB_WARNING(quiet, "value={}", bump());
        CHECK(evaluated == 1);
        CHECK(records().size() == start + 1);
        CHECK(records().back().message == "value=1");
        install_log_handler({ });
    }

    // (6d) default_category() is a real category, and the shorthands stamp its name into the context.
    {
        default_category().set_enabled(log_level_enum::INFO, true);
        install_recorder();
        const std::size_t start = records().size();
        HWSHQTB_INFO_DEFAULT("hello {}", 7);
        CHECK(records().size() == start + 1);
        CHECK(records().back().category == "default");
        CHECK(records().back().level == log_level_enum::INFO);
        CHECK(records().back().message == "hello 7");
        default_category().set_minimum_level(log_level_enum::WARNING);   // restore
        install_log_handler({ });
    }

    // (6e) Filter rules: one level of one category. A rule also overwrites a manual change, because
    //      applying the rules resets the category to its constructor minimum first.
    {
        log_events().set_enabled(log_level_enum::INFO, true);
        set_filter_rules("metaobject.events.debug=true");
        CHECK(log_events().is_debug_enabled());
        CHECK(!log_events().is_info_enabled());          // the manual INFO was overwritten
        CHECK(!log_connection().is_debug_enabled());     // only the named category
        CHECK(filter_rules() == "metaobject.events.debug=true");
    }

    // (6f) A wildcard segment covers every category below the prefix; a trailing ".*" is the same as
    //      "*", and the rule touches only the level it names.
    {
        set_filter_rules("metaobject.*.debug=false");
        CHECK(!log_events().is_debug_enabled());
        CHECK(!log_connection().is_debug_enabled());
        CHECK(!log_types().is_debug_enabled());
        CHECK(log_events().is_warning_enabled());        // WARNING was not mentioned
        CHECK(log_events().is_critical_enabled());
    }

    // (6g) Rules apply in order: a later match wins.
    {
        set_filter_rules("*.debug=true;metaobject.events.debug=false");
        CHECK(!log_events().is_debug_enabled());
        CHECK(log_connection().is_debug_enabled());
        CHECK(log_types().is_debug_enabled());
    }

    // (6h) A rule never touches FATAL: it is not filtered at all.
    {
        set_filter_rules("*.critical=false");
        CHECK(!log_events().is_critical_enabled());
        CHECK(log_events().is_enabled(log_level_enum::FATAL));
    }

    // (6i) Unparseable rules are skipped and do not disturb the ones around them: "no equals sign"
    //      has no '=', "metaobject.events.dbg" names no level (and matches no category), "*.debug=maybe"
    //      has a value that is neither true/false nor 1/0.
    {
        const std::string_view text = "no equals sign;metaobject.events.dbg=true;*.debug=maybe;*.debug=true";
        set_filter_rules(text);
        CHECK(filter_rules() == text);
        CHECK(log_events().is_debug_enabled());
        CHECK(log_connection().is_debug_enabled());
    }

    // (6j) A category constructed AFTER the rules are in force receives them at construction.
    {
        set_filter_rules("late.registration.debug=true;late.registration.warning=false");
        const log_category_t late("late.registration");
        CHECK(late.is_debug_enabled());
        CHECK(!late.is_warning_enabled());
        CHECK(late.is_critical_enabled());               // the constructor minimum still supplies it
    }

    // (6k) install_filter runs for every registered category after the rules, may override them, and
    //      install_filter answers the PREVIOUS filter; nullptr removes it and restores the rule state.
    {
        set_filter_rules("metaobject.events.debug=true");
        const log_category_filter info_everywhere = [](log_category_t& category) {
            category.set_enabled(log_level_enum::INFO, true);   // every category gets INFO
        };
        const log_category_filter installed = install_filter(info_everywhere);
        CHECK(installed == nullptr);
        CHECK(category_filter() == info_everywhere);
        CHECK(log_events().is_info_enabled());
        CHECK(log_connection().is_info_enabled());
        CHECK(log_events().is_debug_enabled());          // the rule ran before the filter
        const log_category_filter previous = install_filter(nullptr);
        CHECK(previous == info_everywhere);
        CHECK(category_filter() == nullptr);
        CHECK(!log_events().is_info_enabled());          // back to the rule state
        CHECK(log_events().is_debug_enabled());
    }

    // (6l) Leave the filter configuration clean for whatever runs next.
    {
        set_filter_rules("");
        install_filter(nullptr);
        CHECK(filter_rules().empty());
        CHECK(log_events().is_warning_enabled());
        CHECK(!log_events().is_debug_enabled());
        CHECK(!log_events().is_info_enabled());
        CHECK(log_events().is_critical_enabled());
    }

    // (6m) The registry-locking entry points are NOT noexcept (they take the registry mutex); the
    //      lock-free ones still are. This is a compile-time pin on the exception specification.
    static_assert(!std::is_nothrow_constructible_v<log_category_t, const char*>);
    static_assert(!std::is_nothrow_destructible_v<log_category_t>);
    static_assert(!std::is_nothrow_invocable_v<decltype(&set_filter_rules), std::string_view>);
    static_assert(!std::is_nothrow_invocable_v<decltype(&filter_rules)>);
    static_assert(!std::is_nothrow_invocable_v<decltype(&install_filter), log_category_filter>);
    static_assert(!std::is_nothrow_invocable_v<decltype(&category_filter)>);
    static_assert(std::is_nothrow_invocable_v<decltype(&log_category_t::set_enabled),
                                              const log_category_t&, log_level_enum, bool>);
    static_assert(std::is_nothrow_invocable_v<decltype(&log_level_name), log_level_enum>);

    // (7) Object tree dump includes the subtree and indents by level.
    {
        object_t top;
        top.set_object_name("top");
        object_t mid;
        mid.set_object_name("mid");
        mid.set_parent(&top);
        object_t leaf;
        leaf.set_object_name("leaf");
        leaf.set_parent(&mid);

        std::ostringstream os;
        top.dump_object_tree(os);
        const std::string text = os.str();
        CHECK(text.find("top") != std::string::npos);
        CHECK(text.find("mid") != std::string::npos);
        CHECK(text.find("leaf") != std::string::npos);
        CHECK(text.find("\n  ") != std::string::npos);          // mid is indented one level
        CHECK(text.find("\n    ") != std::string::npos);        // leaf is indented two levels

        std::ostringstream one;
        leaf.dump_object_info(one);
        CHECK(one.str().find("object_t") != std::string::npos);
        CHECK(one.str().find("leaf") != std::string::npos);
    }

    // (8) Type flags and debug_string. No registration call: the builtin names are inserted
    //     by the one process-wide registry on first lookup.
    {
        const meta_type_t int_type = meta_type_t::resolve_global("int");
        CHECK(int_type.is_valid());
        // The registry assigns the runtime id on first use; the old "IsRegistered" tag is gone, and
        // an assigned id (>= 1) is the same fact.
        CHECK(int_type.id() != 0);
        CHECK(bool(int_type.tag() & type_tag_flag::IS_TRIVIALLY_COPYABLE));
        CHECK(!bool(int_type.tag() & type_tag_flag::IS_POINTER));

        const int int_value = -42;

        const bool bool_value = true;
        CHECK(meta_type_t::resolve_global("bool").debug_string(&bool_value) == "true");

        // An invalid type reports no flags and renders nothing.
        const meta_type_t missing = meta_type_t::resolve_global("no_such_type");
        CHECK(!missing.is_valid());
        CHECK(missing.debug_string(&int_value).empty());
    }

    // (9) debug_string matches the type's own rendering, not a guessed format.
    {
        const meta_type_t dbl = meta_type_t::resolve_global("double");
        CHECK(dbl.is_valid());
        CHECK(dbl.name() == "double");
        const double value = 1.5;
        CHECK(dbl.debug_string(&value) == std::to_string(value));
        CHECK(dbl.id() != 0);
        CHECK(bool(dbl.tag() & type_tag_flag::IS_TRIVIALLY_COPYABLE));
    }
    // (10) meta_type_t describes std::decay_t<T>: cv-qualifiers and references collapse onto the value
    //      type. With a declaration-only model "the same entry" is plain handle equality — there is no
    //      registration call left to answer false, and the handle identity (id, name tie-break) is the
    //      whole test.
    {
        const meta_type_t plain = meta_type_t::from_type<int>();
        CHECK(plain.is_valid());
        CHECK(meta_type_t::from_type<const volatile int>() == plain);
        CHECK(meta_type_t::from_type<int&>() == plain);
        CHECK(meta_type_t::from_type<const int>() == plain);
        const int probe = 9;
        CHECK(plain.debug_string(&probe) == "9");

        // A pointer type keeps its own entry and reports IS_POINTER.
        const meta_type_t ptr = meta_type_t::from_type<const char*>();
        CHECK(ptr.is_valid());
        CHECK(ptr.name() == "const char*");
        CHECK(bool(ptr.tag() & type_tag_flag::IS_POINTER));
        CHECK(!bool(ptr.tag() & type_tag_flag::IS_TRIVIALLY_COPYABLE) == false);  // pointers are trivially copyable
    }
    std::printf(g_failures == 0 ? "  logging: all checks passed\n"
                                : "  logging: %d check(s) failed\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
