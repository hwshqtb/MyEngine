#ifndef HWSHQTB__METAOBJECT__BASIC_DEF_H
#define HWSHQTB__METAOBJECT__BASIC_DEF_H

#include <cstdint>
#include <limits>
#include "flag_t.h"
#include <type_traits>

namespace hwshqtb::metaobject {
    using index_t = std::uint64_t;
    inline constexpr index_t index_npos = std::numeric_limits<index_t>::max();

    /// @brief How a call is dispatched; @c Unique is a bit that combines with a type.
    enum struct connection_type_flag : std::uint8_t {
        NONE = 0,
        AUTO = 1u << 0,
        DIRECT = 1u << 1,
        QUEUED = 1u << 2,
        BLOCKING_QUEUED = 1u << 3,
        UNIQUE = 1u << 4,
    };

    /// @brief A connection type, optionally combined with @c Unique.
    using connection_type_t = flag_t<connection_type_flag>;

    enum struct qualifier_flag : std::uint8_t {
        NONE = 0,
        CONST = 1u << 0,
        VOLATILE = 1u << 1,
        LVALUE_REF = 1u << 2,
        RVALUE_REF = 1u << 3,

        CV = CONST | VOLATILE,
        REF = LVALUE_REF | RVALUE_REF,
    };

    using qualifier_t = flag_t<qualifier_flag>;

    /// @brief Traits a property can carry.
    ///
    /// Only what cannot be derived from the accessors lives here: readability, writability,
    /// resettability, notifiability, bindability (a property is bindable iff it has a notifier) and
    /// constness are all derived queries on meta_property_t.
    enum struct property_trait_flag : std::uint8_t {
        /// @brief No trait.
        NONE = 0,
        /// @brief The property represents "that value" of the object; at most one per class.
        USER = 1u << 0,
        /// @brief The property must be set before a component is instantiated.
        REQUIRED = 1u << 1,
        /// @brief Code-only: the front end does not show this property.
        HIDDEN = 1u << 2,
        /// @brief Reported by meta_property_t::is_final(); nothing enforces it.
        /// @note A derived class MAY redeclare a property with the same name: tables hold one
        ///       class's own rows and the chain is a lookup fallback, so a redeclaration shadows
        ///       rather than overrides.
        FINAL = 1u << 3,
    };

    /// @brief A combination of property traits.
    using property_trait_t = flag_t<property_trait_flag>;

    /// @brief Static properties of a declared type, filled at compile time.
    enum struct type_tag_flag : std::uint32_t {
        /// @brief No flag set.
        NONE = 0,
        /// @brief T is a pointer type.
        IS_POINTER = 1u << 0,
        /// @brief T is an enumeration.
        IS_ENUM = 1u << 1,
        /// @brief T is trivially copyable.
        IS_TRIVIALLY_COPYABLE = 1u << 2,
        /// @brief T is flag_t\<E\>: a wrapper around an enum used as a bit set (QFlags\<E\> equivalent).
        /// @note Kept apart from IS_ENUM on purpose: "is an enum" and "wraps an enum" are two facts. The
        ///       structural integer conversion treats both as an enum-like side; question-layer checks
        ///       (declared enumerators, flag-bit coverage) stay on meta_enum_t.
        IS_FLAGS = 1u << 4,
        /// @brief T is an integer arithmetic type: the integer side of the structural conversion.
        IS_INTEGER = 1u << 5,
        /// @brief The stored scalar is signed (integer / enum / flag_t width rule).
        IS_SIGNED = 1u << 6,
        /// @brief T's move constructor is declared noexcept (any_t's noexcept move path).
        IS_NOEXCEPT_MOVE_CONSTRUCTIBLE = 1u << 7,
        /// @brief T's move assignment is declared noexcept (any_t's noexcept move-assignment path).
        IS_NOEXCEPT_MOVE_ASSIGNABLE = 1u << 8,
    };

    /// @brief Flag combination of type_tag_flag.
    using type_tag_t = flag_t<type_tag_flag>;

    /// @brief Event-processing flags (QEventLoop::ProcessEventsFlag).
    /// @note @c ALL_EVENTS is 0, meaning "exclude nothing", matching Qt.
    enum struct process_events_type_flag : std::uint8_t {
        /// @brief Filter nothing out.
        ALL_EVENTS = 0,
        /// @brief Skip input events (mouse / keyboard / touch / wheel).
        EXCLUDE_USER_INPUT_EVENTS = 1u << 0,
        /// @brief Skip socket notifications (not implemented by this library).
        EXCLUDE_SOCKET_NOTIFIERS = 1u << 1,
        /// @brief Wait until more events are available before dispatching.
        WAIT_FOR_MORE_EVENTS = 1u << 2,
        /// @brief Coming from QEventLoop::exec.
        EVENT_LOOP_EXEC = 1u << 3,
        /// @brief Coming from a dialog's exec.
        DIALOG_EXEC = 1u << 4,
        /// @brief Coming from QCoreApplication::exec.
        APPLICATION_EXEC = 1u << 5,
    };

    /// @brief Flag combination of event-processing flags.
    using process_events_type_t = flag_t<process_events_type_flag>;

    /// @brief What kind of `meta_method_t` a row describes.
    /// @note A constructor is not a kind: it has its own table, row type and handle (`meta_constructor_t`).
    enum struct method_kind_enum : std::uint8_t {
        /// @brief An ordinary method.
        PLAIN_METHOD,
        /// @brief A signal.
        SIGNAL,
        /// @brief A static member function: no implicit object (see has_receiver()); it cannot be a signal.
        STATIC_METHOD,
    };

    /// @brief The declared access of the member, as a hint only: a meta-object invocation does not
    ///        enforce it (everything in the table can be called).
    /// @note none for an invalid handle.
    /// @note Also none for a synthesised property accessor: it has no declaration in the table, so
    ///       there is no access specifier to report.
    /// @note A run-time built entry (meta_object_builder_t) reports PUBLIC_SCOPE: the builder carries no
    ///       access information, and public is the honest default for a signature the caller wrote.
    enum struct method_access_enum : std::uint8_t {
        /// @brief No declared member behind the handle.
        NONE,
        /// @brief Declared in a private section (still callable through the meta-object).
        PRIVATE_SCOPE,
        /// @brief Declared in a protected section (still callable through the meta-object).
        PROTECTED_SCOPE,
        /// @brief Declared in a public section (also the default of a run-time built entry).
        PUBLIC_SCOPE,
    };

    /// @brief Which event sources a loop drains on each iteration.
    ///
    /// There is **one** loop implementation, run under one of two pump policies: the application loop
    /// drains native platform messages as well as posted events, a worker loop drains posted events
    /// only. A native pump belongs to the thread that owns the platform integration (the main
    /// thread), so a worker thread must never call event_dispatcher::process_events().
    enum class loop_pump_enum : std::uint8_t {
        /// @brief The main thread: native platform messages plus posted events.
        SYSTEM_AND_POSTED,
        /// @brief A worker thread: posted events only (the native pump stays on the main thread).
        POSTED_ONLY,
    };

    /// @brief Event type (QEvent::Type).
    /// @note The numbering matches Qt; only the values needed by the core range are listed.
    ///       @c User onwards is the user-registrable range.
    enum struct event_type_enum : std::uint16_t {
        /// @brief Invalid event.
        NONE = 0,
        /// @brief Timer event.
        TIMER = 1,
        /// @brief Mouse button pressed.
        MOUSE_BUTTON_PRESS = 2,
        /// @brief Mouse button released.
        MOUSE_BUTTON_RELEASE = 3,
        /// @brief Mouse button double click.
        MOUSE_BUTTON_DBL_CLICK = 4,
        /// @brief Mouse move.
        MOUSE_MOVE = 5,
        /// @brief Key pressed.
        KEY_PRESS = 6,
        /// @brief Key released.
        KEY_RELEASE = 7,
        /// @brief Keyboard focus received.
        FOCUS_IN = 8,
        /// @brief Keyboard focus lost.
        FOCUS_OUT = 9,
        /// @brief Cursor entered.
        ENTER = 10,
        /// @brief Cursor left.
        LEAVE = 11,
        /// @brief Paint request.
        PAINT = 12,
        /// @brief Moved.
        MOVE = 13,
        /// @brief Resized.
        RESIZE = 14,
        /// @brief Created.
        CREATE = 15,
        /// @brief Destroyed.
        DESTROY = 16,
        /// @brief Shown.
        SHOW = 17,
        /// @brief Hidden.
        HIDE = 18,
        /// @brief Close requested.
        CLOSE = 19,
        /// @brief Quit requested.
        QUIT = 20,
        /// @brief Parent changed.
        PARENT_CHANGE = 21,
        /// @brief Thread affinity changed.
        THREAD_CHANGE = 22,
        /// @brief A dynamic (run-time created) property changed.
        DYNAMIC_PROPERTY_CHANGE = 170,
        /// @brief Mouse wheel.
        WHEEL = 31,
        /// @brief Queued signal invocation (QMetaCallEvent).
        META_CALL = 43,
        /// @brief Deferred delete (QDeferredDeleteEvent).
        DEFERRED_DELETE = 52,
        /// @brief A child object was added.
        CHILD_ADDED = 68,
        /// @brief A child object was polished.
        CHILD_POLISHED = 69,
        /// @brief A child object was removed.
        CHILD_REMOVED = 71,
        /// @brief Touch began.
        TOUCH_BEGIN = 194,
        /// @brief Touch updated.
        TOUCH_UPDATE = 195,
        /// @brief Touch ended.
        TOUCH_END = 196,
        /// @brief The window system asks for a redraw.
        EXPOSE = 206,
        /// @brief Touch cancelled.
        TOUCH_CANCEL = 209,
        /// @brief ui layer: an item's hover state changed.
        /// @note 100-110 are the ui layer's own event ids. They are NOT Qt numbers: the Qt range is
        ///       only mirrored up to 209, and these ids are never posted (a ui event is constructed on
        ///       the stack and dispatched synchronously), so no Qt number and no user registrable id
        ///       (1000 and above) is taken.
        /// @note There is deliberately NO UI_PAINT: a paint pass is `event_type_enum::PAINT` with a
        ///       paint_event_t payload for both layers — the window pass carries the window's painter,
        ///       the item pass the same painter translated into the item. One code, one event class.
        UI_HOVER = 101,
        /// @brief ui layer: a mouse button edge was recorded on an item.
        UI_PRESS = 102,
        /// @brief ui layer: an item rectangle changed (carries the old and the new rectangle).
        UI_GEOMETRY_CHANGE = 103,
        /// @brief ui layer: an item gained or lost the keyboard focus.
        UI_FOCUS_CHANGE = 104,
        /// @brief ui layer: an item's minimum/maximum size constraint changed.
        UI_SIZE_CONSTRAINT_CHANGE = 105,
        /// @brief ui layer: an item view's content extent or decoration changed.
        UI_CONTENT_CHANGE = 106,
        /// @brief ui layer: an item's visibility flag changed.
        UI_VISIBLE_CHANGE = 107,
        /// @brief ui layer: an item's enabled flag changed.
        UI_ENABLED_CHANGE = 108,
        /// @brief ui layer: rows of a row view changed their content (carries the row range).
        UI_DATA_CHANGE = 109,
        /// @brief ui layer: the visible row list of a row view was restructured.
        UI_ROWS_CHANGE = 110,
        /// @brief ui layer: a container asks the host to move already-painted pixels (scroll payload).
        UI_SCROLL = 111,
        /// @brief ui layer: a widget asks the host for an IMMEDIATE frame (QWidget::repaint()).
        /// @note The async request reuses PAINT; this one is separate because the host answers it with
        ///       window_t::repaint() instead of window_t::update().
        UI_REPAINT = 112,
        /// @brief First user event id.
        USER = 1000,
        /// @brief Last user event id.
        MAX_USER = 65535,
    };

    /// @brief Event priority (Qt::EventPriority).
    enum struct event_priority_enum : int {
        /// @brief Low priority (Qt::LowEventPriority).
        LOW = -1,
        /// @brief Normal priority; the default (Qt::NormalEventPriority).
        NORMAL = 0,
        /// @brief High priority (Qt::HighEventPriority).
        HIGH = 1,
    };

    /// @brief Severity of a log message (qCDebug / qCInfo / qCWarning / qCCritical / qCFatal).
    enum struct log_level_enum : std::uint8_t {
        /// @brief Development detail; usually off.
        DEBUG = 0,
        /// @brief Normal noteworthy events.
        INFO = 1,
        /// @brief Something suspicious but recoverable.
        WARNING = 2,
        /// @brief Something failed.
        CRITICAL = 3,
        /// @brief Unrecoverable: the message is emitted and the process aborts.
        FATAL = 4,
    };

    /// @brief The cvref of @p U read as a PARAMETER type (not as a member type).
    /// @tparam U The parameter type as written at the call site; cv, references and pointers allowed.
    /// @return The const/volatile bits of std::remove_reference_t\<U\> OR'd with the reference kind of
    ///         U: an lvalue reference answers LVALUE_REF, an rvalue reference RVALUE_REF, and a bare
    ///         value RVALUE_REF too (inside the call it is an rvalue).
    /// @note The forwarding-reference consequence every caller relies on: with the usual `U&&`
    ///       spelling the deduction collapses — `X&` -> LVALUE_REF, `const X&` -> CONST|LVALUE_REF,
    ///       bare `X` -> RVALUE_REF.
    /// @note constexpr and noexcept; it reads only the type.
    template <typename U>
    constexpr qualifier_t get_param_qualifier() noexcept {
        qualifier_t result = qualifier_flag::NONE;
        using unref_t = std::remove_reference_t<U>;
        if constexpr (std::is_const_v<unref_t>) {
            result = result | qualifier_flag::CONST;
        }
        if constexpr (std::is_volatile_v<unref_t>) {
            result = result | qualifier_flag::VOLATILE;
        }
        if constexpr (std::is_lvalue_reference_v<U>) {
            result = result | qualifier_flag::LVALUE_REF;
        }
        if constexpr (std::is_rvalue_reference_v<U>) {
            result = result | qualifier_flag::RVALUE_REF;
        }
        return result;
    }

    /// @brief Whether an argument whose qualifier is @p arg can bind a parameter declared @p aim
    ///        (the matcher's accommodating-pass rule).
    /// @param aim The parameter's declared cvref.
    /// @param arg The argument's recorded cvref.
    /// @return true when the argument may bind the parameter:
    ///         - a by-value parameter (@p aim == NONE) accepts any cvref;
    ///         - const/volatile may be ADDED but never removed: @p arg's cv bits must be a subset of
    ///           @p aim's;
    ///         - an lvalue-reference parameter accepts anything except an rvalue, which it accepts
    ///           only as a reference to const (the answer is then whether CONST is set);
    ///         - an rvalue-reference parameter rejects an lvalue and accepts an rvalue.
    /// @note An @p aim that carries cv bits but no reference kind answers false; a by-value
    ///       parameter's top-level cv is stripped from the key, so that shape is not expected.
    /// @note constexpr and noexcept.
    constexpr bool check_param_qualifier(qualifier_t aim, qualifier_t arg) noexcept {
        if (aim == qualifier_flag::NONE) {
            return true;
        }
        if ((arg & qualifier_flag::CV) & ~(aim & qualifier_flag::CV)) {
            return false;
        }
        if (aim & qualifier_flag::LVALUE_REF) {
            if (arg & qualifier_flag::RVALUE_REF) {
                return aim & qualifier_flag::CONST;
            }
            return true;
        }
        if (aim & qualifier_flag::RVALUE_REF) {
            return !(arg & qualifier_flag::LVALUE_REF);
        }
        return false;
    }
}

#endif
