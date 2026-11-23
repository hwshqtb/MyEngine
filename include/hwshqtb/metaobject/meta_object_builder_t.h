#ifndef HWSHQTB__METAOBJECT__META_OBJECT_BUILDER_T_H
#define HWSHQTB__METAOBJECT__META_OBJECT_BUILDER_T_H

/// @file meta_object_builder_t.h
/// @brief The run-time meta-object builder family (QMetaObjectBuilder equivalent).

#include "meta_method_builder_t.h"
#include "meta_property_builder_t.h"
#include "meta_enum_builder_t.h"
#include "meta_info_t.h"
#include "meta_object_t.h"
#include "export.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwshqtb::metaobject {
    namespace detail {
        /// @brief Build the diagnostic of a setter called on a builder handle whose row is gone.
        /// @param handle The handle class's name, e.g. "meta_method_builder_t" (the message prefix).
        /// @param name The handle's name() at the time of the call (may be empty).
        /// @param removal The builder operation that shifts the rows, without "()" (e.g.
        ///        "remove_method"); the message appends it.
        /// @return The message, naming the cause and the cure.
        /// @note A stale handle is caller-preventable — remove_*() shifts the rows — so a setter
        ///       throws instead of silently editing somebody else's row. Queries stay total and answer
        ///       empty, the same defensive split meta_method_t / meta_property_t use.
        /// @note The three builder handles share this message: they differ only in the handle name and
        ///       in which removal shifts THEIR rows, so those two come in as arguments.
        std::string stale_row_message(std::string_view handle, std::string_view name,
                                      std::string_view removal);
    }

    /// @brief Assembles a meta-object at run time through mutable per-member handles
    ///        (QMetaObjectBuilder equivalent).
    ///
    /// @par What this family is for
    /// The object builder owns the arrays and each member is a `{owner*, index}` handle
    /// (meta_method_builder_t / meta_property_builder_t / meta_enum_builder_t) that can be mutated
    /// after it is added, until build() takes the one-shot snapshot. Class info has no handle —
    /// add_class_info() returns the row index, as in Qt.
    ///
    /// @par Two member spaces: methods and constructors
    /// A method row (add_method / add_signal / add_slot) and a constructor row (add_constructor) are
    /// different things: they are staged in different vectors, built into different tables
    /// (meta_info_t::method_infos / constructor_infos) and counted by different indices, exactly as in the
    /// compile-time path and in Qt (methodCount() vs constructorCount()). A method handle remembers
    /// which space it came from; properties and their notifiers live in the METHOD space only (a
    /// notifier must be a Signal).
    ///
    /// @par The result is an ordinary meta-object
    /// build() assembles the same structures the compile-time path produces — detail::method_info_t
    /// rows (caller, kind, access, qualifier, annotations) for the METHOD space and
    /// detail::constructor_info_t rows (caller, access, annotations) for the CONSTRUCTOR space, i.e.
    /// TWO row types, one array each; plus property_info_t; enum_info_t; class_info_t — into the
    /// process-wide arena, and returns the arena entry itself. It does NOT register the class:
    /// registration stays the caller's job (object_t::register_type), exactly as for a hand-written
    /// class. Lookups, connect(), property access and inherits() cannot tell the two apart, because
    /// the only difference is where the table came from.
    ///
    /// @par Signature parsing
    /// add_method("ping(int, const std::string&) const") yields the name `ping`, the parameter base
    /// types `int` / `std::string`, the parameter qualifiers NONE / CONST|LVALUE_REF and the method
    /// qualifier Const. Each base type is resolved with detail::original_parameter_alias, which asks
    /// the type registry (meta_type_t::resolve_global) for the IDENTITY spelling the reflected path
    /// stores, so the stored column spells `const std::__cxx11::basic_string<char>&`, exactly what a
    /// reflected declaration stores. The table KEY is composed from those same stored parameter
    /// spellings (not from the parser's user-text output), so signature() and every
    /// param_type_name(i) agree.
    ///
    /// @code
    /// meta_object_builder_t builder("DynamicWidget");
    /// meta_method_builder_t setter = builder.add_slot("setValue(double)");
    /// setter.set_access(method_access_enum::PUBLIC_SCOPE);
    /// setter.set_tag("ported");
    /// meta_property_builder_t value = builder.add_property("value", "double");
    /// value.set_reader(&read_value);
    /// value.set_writer(&call_set_value);
    /// const meta_object_t* meta = builder.build();
    /// @endcode
    ///
    /// @note The builder is non-copyable and non-movable: handles store the builder's ADDRESS, so a
    ///       copy or a move would leave every live handle naming a different object than the one
    ///       being edited.
    /// @note Built meta-objects are never freed (the arena is process-wide and never destroyed, like
    ///       Qt's).
    /// @sa meta_method_builder_t
    /// @sa meta_property_builder_t
    /// @sa meta_enum_builder_t
    class HWSHQTB_EXPORT meta_object_builder_t {
    public:
        /// @brief Start building a class.
        /// @param class_name The class name reported by meta_object_t::class_name(). The builder does
        ///        not register it anywhere; when the name should resolve through object_t::create(),
        ///        pass the built pointer to object_t::register_type() (object_factory does its own).
        /// @param super The optional base class meta-object (pass `&<base>::static_meta_object`, or
        ///        the pointer another builder returned); property/method lookups then walk the chain
        ///        through meta_object_t::super_class().
        /// @note A POINTER, not a meta_info_t: the chain is a graph of real meta-objects, and the
        ///       built type links to the base's own meta-object instead of copying its table.
        explicit meta_object_builder_t(std::string_view class_name, const meta_object_t* super = nullptr);

        meta_object_builder_t(const meta_object_builder_t&) = delete;
        meta_object_builder_t& operator=(const meta_object_builder_t&) = delete;
        meta_object_builder_t(meta_object_builder_t&&) = delete;
        meta_object_builder_t& operator=(meta_object_builder_t&&) = delete;

        /// @name Identity
        /// @{
        /// @brief The class name the result will report.
        /// @note noexcept, no allocation.
        std::string_view class_name() const noexcept;

        /// @brief The base class's meta-object, or nullptr for a root class.
        /// @note noexcept. The pointer is stored as given and linked through at build().
        const meta_object_t* super_class() const noexcept;
        /// @}

        /// @name Adding members
        /// @{
        /// @brief Add an ordinary method (QMetaObjectBuilder::addMethod).
        /// @param signature The declaration text, e.g. "setValue(double)" or
        ///        "combine(int, const std::string&) const"; a leading return type is ignored, and a
        ///        bare name ("ping") means the no-argument form "ping()".
        /// @return The mutable handle of the new row.
        /// @note The parameter list, the parameter cvref and the method's own cvref suffix all come
        ///       from this text (see the class note); the return type is empty unless
        ///       set_return_type() names it, and the caller thunk is null unless set_caller() gives
        ///       one.
        /// @note The rule is the same as the compile-time table: a row is the DECLARATION, so it is
        ///       present even when no thunk can be attached; meta_method_t::is_valid() then answers
        ///       false, which is the honest "in the table but not callable".
        /// @throws std::invalid_argument when the text names no member at all.
        meta_method_builder_t add_method(std::string_view signature);

        /// @brief Add a signal (QMetaObjectBuilder::addSignal).
        /// @param signature As add_method().
        /// @return The mutable handle of the new row; its method_kind() is method_kind_enum::SIGNAL, which is what
        ///         meta_method_t::is_signal() and every signal lookup read.
        meta_method_builder_t add_signal(std::string_view signature);

        /// @brief Add a slot (QMetaObjectBuilder::addSlot).
        /// @param signature As add_method().
        /// @return The mutable handle of the new row.
        /// @note Exactly add_method(): there is no separate "slot" kind in this library — any
        ///       reflected method can be the target of a connection, so nothing marks a slot as one.
        meta_method_builder_t add_slot(std::string_view signature);

        /// @brief Add a constructor row (QMetaObjectBuilder::addConstructor).
        /// @param signature The method form the constructor table keys constructors with: the class
        ///        name followed by the parameter list, e.g. "DynamicWidget(int)".
        /// @return The mutable handle of the new row. It is a CONSTRUCTOR row, so it has no method kind
        ///         at all: method_kind_enum has no Constructor value, and the built row's type
        ///         (detail::constructor_info_t) has no kind column.
        /// @note The row goes into the builder's CONSTRUCTOR space — its own vector, its own table (of
        ///       its own row type) and its own index space in the built meta-object — never into the
        ///       method rows, and never in the method table. The two SPACES are two different row TYPES.
        /// @note A constructor row is a row: no construction thunk is synthesised, so give it one
        ///       with set_caller() if it should be callable. The convention is the compile-time one —
        ///       the thunk is called with a NULL object and writes the new instance (as a `void*`)
        ///       through params[0].
        meta_method_builder_t add_constructor(std::string_view signature);

        /// @brief Add a property (QMetaObjectBuilder::addProperty).
        /// @param name The property name (the table key).
        /// @param type The type text, e.g. "double" or "const std::string&"; it is canonicalised the
        ///        way a parameter is, so the stored column matches a reflected one.
        /// @return The mutable handle of the new row. A fresh row has no accessor thunks, so it is
        ///         not readable, not writable, not resettable and not notifiable until they are set.
        /// @note build() REFUSES a property whose reader is still missing: Qt's Q_PROPERTY requires a
        ///       READ accessor, and a row without one could not honour read()'s "always an initialised
        ///       value" contract. Record one with set_reader().
        meta_property_builder_t add_property(std::string_view name, std::string_view type);

        /// @brief Add an enumeration (QMetaObjectBuilder::addEnumerator).
        /// @param name The enum name (the table key); its scope is this builder's class name and it is
        ///        recorded as a scoped enum (enum class), the shape a run-time table has no
        ///        declaration to read.
        /// @return The mutable handle of the new row; add its enumerators with add_key().
        meta_enum_builder_t add_enumerator(std::string_view name);

        /// @brief Add a class-level key/value pair (QMetaObjectBuilder::addClassInfo).
        /// @param name The key; it must be unique within this builder.
        /// @param value The value.
        /// @return The row index. There is no class-info handle: there is nothing to
        ///        mutate but the value, and Qt returns an index too.
        /// @note add_class_info() stores the pair as given — the value is metadata, not a type
        ///       spelling, so it is not canonicalised.
        index_t add_class_info(std::string_view name, std::string_view value);
        /// @}

        /// @name Traversal
        /// @{
        /// @brief Number of METHOD rows (methods and signals; constructors are not methods).
        /// @note noexcept. The constructor rows are a second space: see constructor_count().
        std::size_t method_count() const noexcept;

        /// @brief The method row at @p index.
        /// @param index A METHOD-row index; out of range yields a stale handle whose accessors answer
        ///        empty / None / 0.
        /// @note noexcept. The query is const on the builder (asking it for a row modifies nothing),
        ///       but the handle refers to the builder's mutable rows: editing rows is what a builder
        ///       is for, so a handle's mutability does not follow the builder's own constness.
        meta_method_builder_t method(index_t index) const noexcept;

        /// @brief Number of CONSTRUCTOR rows.
        /// @note noexcept. The constructor space is separate from the method space, exactly as it is
        ///       in a built meta-object (Qt's constructorCount()).
        std::size_t constructor_count() const noexcept;

        /// @brief The constructor row at @p index of the CONSTRUCTOR space.
        /// @param index A constructor-row index; out of range yields a stale handle.
        /// @note noexcept; the mutability note of method() applies.
        meta_method_builder_t constructor(index_t index) const noexcept;

        /// @brief Number of property rows.
        /// @note noexcept.
        std::size_t property_count() const noexcept;

        /// @brief The property row at @p index (out of range yields a stale handle).
        /// @note noexcept; the mutability note of method() applies.
        meta_property_builder_t property(index_t index) const noexcept;

        /// @brief Number of enumeration rows.
        /// @note noexcept.
        std::size_t enumerator_count() const noexcept;

        /// @brief The enumeration row at @p index (out of range yields a stale handle).
        /// @note noexcept; the mutability note of method() applies.
        meta_enum_builder_t enumerator(index_t index) const noexcept;

        /// @brief Number of class-info pairs.
        /// @note noexcept.
        std::size_t class_info_count() const noexcept;
        /// @}

        /// @name Finding a row
        /// @{
        /// @brief Index of a METHOD row by signature.
        /// @param signature Any spelling of the declaration; it is canonicalised with the same parser
        ///        and parameter-alias pass add_method() uses, so "ping( int )" and
        ///        "ping(const std::string&)" find the rows those texts added.
        /// @return The row index, or index_npos when no method row has that key.
        /// @note Methods and signals match: every kind the METHOD space holds. A constructor row has
        ///       its own space and its own lookup (index_of_constructor()).
        index_t index_of_method(std::string_view signature) const;

        /// @brief Index of the SIGNAL row with this signature.
        /// @return The row index, or index_npos when the signature names no signal row (a method of
        ///         the same signature is not a hit).
        index_t index_of_signal(std::string_view signature) const;

        /// @brief Index of the CONSTRUCTOR row with this signature.
        /// @return The index inside the CONSTRUCTOR space, or index_npos when the signature names no
        ///         constructor row.
        /// @note The value is a constructor coordinate, the same one constructor() and
        ///       remove_constructor() take — the two spaces stay apart, exactly as they do in a built
        ///       meta-object (whose index_of_constructor() answers the same kind of number).
        index_t index_of_constructor(std::string_view signature) const;

        /// @brief Index of a property row by name (an exact name match; a property name is not a
        ///        signature and is never normalised).
        /// @note noexcept.
        index_t index_of_property(std::string_view name) const noexcept;

        /// @brief Index of an enumeration row by name (exact match).
        /// @note noexcept.
        index_t index_of_enumerator(std::string_view name) const noexcept;
        /// @}

        /// @name Removing members
        /// @{
        /// @brief Erase a METHOD row, shifting every later method row down.
        /// @param index A method-row index; out of range is a no-op.
        /// @note A property whose notifier pointed at this row loses it; a notifier pointing at a
        ///       LATER row is re-pointed, so the surviving rows keep naming the same declarations.
        ///       Every handle taken before the removal must be re-taken, because indices shifted.
        /// @note Only the method space moves: the constructor rows keep their own indices.
        void remove_method(index_t index);

        /// @brief Erase a CONSTRUCTOR row, shifting every later constructor row down.
        /// @param index A constructor-row index; out of range is a no-op.
        /// @note Only the constructor space moves: the method rows keep their own indices, and no
        ///       property notifier can name a constructor row.
        void remove_constructor(index_t index);

        /// @brief Erase a property row, shifting every later row down.
        /// @param index A row index; out of range is a no-op.
        void remove_property(index_t index);

        /// @brief Erase an enumeration row, shifting every later row down.
        /// @param index A row index; out of range is a no-op.
        void remove_enumerator(index_t index);

        /// @brief Erase a class-info pair, shifting every later row down.
        /// @param index A row index; out of range is a no-op.
        void remove_class_info(index_t index);
        /// @}

        /// @brief Assemble the meta-object and park it in the arena.
        /// @return The finished meta-object — the arena entry itself, so it IS the stored object and
        ///         handles taken from it stay valid; valid for the process lifetime.
        ///
        /// @note ONE-SHOT SNAPSHOT. The table is assembled from a COPY of every row: the strings are
        ///       interned into the arena and the arrays are copied, so editing a builder handle after
        ///       build() cannot change a table that has already been built. build() itself is
        ///       idempotent — a second call returns the same pointer and builds nothing.
        ///
        /// @note The result is NOT registered with the process-wide class registry. Registration is
        ///       the caller's job, as it is for a hand-written class: object_t::register_type(built)
        ///       makes object_t::create("Name") resolve it. A run-time built class is creatable only
        ///       when it carries a callable 0-argument constructor row.
        ///
        /// @throws std::invalid_argument when a validation rule fails. Dynamic types have no compile
        ///         step to report such mistakes, so they surface here:
        ///          - two rows of the SAME space with the same key (the method signature, the
        ///            constructor signature — a separate table, so the same text may name one of
        ///            each — the property / enum / class info name) — the table is keyed by it, so a
        ///            duplicate would be unreachable;
        ///          - a property without a reader (Qt's Q_PROPERTY requires a READ accessor, and a
        ///            readerless row could not honour read()'s "always initialised" contract);
        ///          - two User properties, or a property that is both Required and Hidden (the same
        ///            rules the compile-time pass enforces);
        ///          - a property whose notifier does not name a SIGNAL row of this builder.
        /// @throws std::invalid_argument when no perfect hash could be built for a table (the same
        ///         guard the compile-time table uses).
        const meta_object_t* build();

    private:
        friend class meta_method_builder_t;
        friend class meta_property_builder_t;
        friend class meta_enum_builder_t;

        /// @brief One pending parameter.
        /// @note `type` is the BASE spelling — the top-level cvref has been split off into
        ///       `qualifier`. build() re-assembles the column from the pair, which is what lets
        ///       set_parameter_qualifiers() change the recorded spelling coherently.
        struct parameter_spec_t {
            std::string type;
            qualifier_t qualifier = qualifier_flag::NONE;
            std::string name;
        };

        /// @brief One pending method / signal / constructor row.
        /// @note The two spaces share this STAGING struct — it is the builder's mutable working shape,
        ///       not the built table's row type: `_methods` holds method rows, `_constructors` holds
        ///       constructor rows, and build() turns them into TWO UNRELATED built row types
        ///       (detail::method_info_t / detail::constructor_info_t). That is also why `kind` is only
        ///       meaningful for a row of `_methods`: a constructor row has no kind column when built.
        struct method_spec_t {
            std::string signature;
            std::string name;
            std::string return_type;
            std::vector<parameter_spec_t> parameters;
            detail::generic_call caller = nullptr;
            method_kind_enum kind = method_kind_enum::PLAIN_METHOD;
            method_access_enum access = method_access_enum::PUBLIC_SCOPE;
            qualifier_t qualifier = qualifier_flag::NONE;
            std::string tag;
        };

        /// @brief One pending property.
        struct property_spec_t {
            std::string name;
            std::string type;
            std::string enum_name;
            bool is_flags = false;
            detail::generic_call reader = nullptr;
            detail::generic_call writer = nullptr;
            detail::generic_call reseter = nullptr;
            index_t notifier = index_npos;
            property_trait_t traits = property_trait_flag::NONE;
        };

        /// @brief One pending enumeration.
        struct enum_spec_t {
            std::string name;
            bool is_flag = false;
            std::vector<std::pair<std::string, std::int64_t>> keys;
        };

        /// @brief One pending class-info pair.
        struct class_info_spec_t {
            std::string name;
            std::string value;
        };

        /// @brief The mutable member row a handle at @p index names, or nullptr when out of range.
        /// @param index The row index inside the space @p constructor_row selects.
        /// @param constructor_row true to read `_constructors`, false to read `_methods`.
        /// @return The row, or nullptr when @p index is past the space's size.
        /// @note noexcept. The one row read every meta_method_builder_t accessor and setter goes
        ///       through, so the handle's two row spaces stay a private detail of the builder (the
        ///       handle only carries the flag, it does not reach into either vector).
        method_spec_t* handle_row(index_t index, bool constructor_row) noexcept;
        /// @brief The mutable property row at @p index, or nullptr when out of range.
        property_spec_t* property_row(index_t index) noexcept;
        /// @brief The property row at @p index, or nullptr when out of range.
        const property_spec_t* property_row(index_t index) const noexcept;
        /// @brief The mutable enumeration row at @p index, or nullptr when out of range.
        enum_spec_t* enumerator_row(index_t index) noexcept;
        /// @brief The enumeration row at @p index, or nullptr when out of range.
        const enum_spec_t* enumerator_row(index_t index) const noexcept;

        /// @brief Local slot of the METHOD row with @p signature AND kind @p kind, or index_npos.
        /// @param signature The declaration text (any spelling of it: the parser is index_of_method's).
        /// @param kind The kind the row must have (PLAIN_METHOD / SIGNAL / STATIC_METHOD); a constructor row
        ///        is not in this space at all.
        /// @return The local slot, or index_npos when no row matches or the match has another kind.
        /// @note The METHOD space only: a constructor row is found by its own lookup, in
        ///       `_constructors`.
        index_t index_of_method_kind(std::string_view signature, method_kind_enum kind) const;

        /// @brief Split a type text into its BASE spelling and its top-level cvref qualifiers.
        /// @param text The type text, e.g. "const std::string&".
        /// @param qualifier Receives the qualifiers the spelling carried; may be nullptr.
        /// @return The base spelling with the type registry's lookup applied (the IDENTITY spelling),
        ///         or an empty string for empty text.
        /// @note The splitter is detail::split_qt_arg; the lookup is
        ///       detail::original_parameter_alias, which asks meta_type_t::resolve_global — the one
        ///       the run-time signature layer uses, so every user-text path shares one source of names.
        static std::string canonical_base_type(std::string_view text, qualifier_t* qualifier = nullptr);

        /// @brief Spell a type column from its base name and qualifiers.
        /// @param base The bare spelling, e.g. "std::__cxx11::basic_string<char>" or "object_t*".
        /// @param qualifier The top-level cvref.
        /// @return The spelling the reflected path would produce: cv before the base, after the
        ///         declarator for a pointer, then & / &&.
        static std::string spell_type(std::string_view base, qualifier_t qualifier);

        /// @brief Parse a declaration text into a pending method row.
        /// @param signature The text add_method() was given.
        /// @param kind What the row is.
        /// @return The row, with the signature key, name, parameters and qualifiers filled in.
        /// @throws std::invalid_argument when the text names no member.
        static method_spec_t parse_method(std::string_view signature,
                                         method_kind_enum kind = method_kind_enum::PLAIN_METHOD);

        /// @brief The class name reported by the built table.
        std::string _class_name;
        /// @brief The base class's meta-object, or nullptr (a pointer: the chain is not copied).
        const meta_object_t* _super = nullptr;
        /// @brief The METHOD rows: methods and signals. This is the method_infos space (built as
        ///        detail::method_info_t rows).
        std::vector<method_spec_t> _methods;
        /// @brief The CONSTRUCTOR rows. This is the constructor_infos space (built as
        ///        detail::constructor_info_t rows): its own table and its own
        ///        indices, never mixed with _methods (Qt's constructorCount()).
        std::vector<method_spec_t> _constructors;
        std::vector<property_spec_t> _properties;
        std::vector<enum_spec_t> _enums;
        std::vector<class_info_spec_t> _class_infos;
        /// @brief The cached snapshot; non-null once build() has run.
        const meta_object_t* _built = nullptr;
    };
}

#endif
