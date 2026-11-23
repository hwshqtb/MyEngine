#include <hwshqtb/metaobject/meta_type_t.h>

#include <hwshqtb/metaobject/object_t.h>
#include <hwshqtb/metaobject/object_pointer_t.h>

#include <cstring>
#include <new>

namespace hwshqtb::metaobject {
    namespace {
        /// @brief Whether a flag set is the integer side of the structural conversion.
        bool is_integer_side(type_tag_t flags) noexcept {
            return bool(flags & type_tag_flag::IS_INTEGER);
        }

        /// @brief Whether a flag set is the enum side of the structural conversion (enum or flag_t).
        bool is_enum_side(type_tag_t flags) noexcept {
            return bool(flags & (type_tag_t(type_tag_flag::IS_ENUM) | type_tag_flag::IS_FLAGS));
        }

        /// @brief Whether the two flag sets form the structural integer <-> enum/flags pair.
        /// @note A type-system identity, not a registered converter: C++'s enum semantics allow both
        ///       directions even without one. Bit-level validation stays on meta_enum_t.
        bool is_structural_pair(type_tag_t from, type_tag_t to) noexcept {
            return (is_integer_side(from) && is_enum_side(to))
                || (is_enum_side(from) && is_integer_side(to));
        }

        /// @brief Read a scalar directly out of its storage (as a signed 64-bit value).
        /// @param flags The source interface's flags (IsSigned is read).
        /// @param width Its storage width in bytes (1, 2, 4 or 8).
        /// @return The value, sign- or zero-extended to 64 bits.
        /// @note memcpy keeps this alignment-safe. For an enum this reads the underlying value; for a
        ///       flag_t it reads the wrapper's only member, `data` (never `operator bool`, which would
        ///       collapse the bit set to true/false).
        std::int64_t read_scalar(const void* src, type_tag_t flags, std::size_t width) noexcept {
            const bool is_signed = bool(flags & type_tag_flag::IS_SIGNED);
            switch (width) {
                case 1: {
                    std::uint8_t bits = 0;
                    std::memcpy(&bits, src, 1);
                    return is_signed ? std::int64_t(static_cast<std::int8_t>(bits)) : std::int64_t(bits);
                }
                case 2: {
                    std::uint16_t bits = 0;
                    std::memcpy(&bits, src, 2);
                    return is_signed ? std::int64_t(static_cast<std::int16_t>(bits)) : std::int64_t(bits);
                }
                case 4: {
                    std::uint32_t bits = 0;
                    std::memcpy(&bits, src, 4);
                    return is_signed ? std::int64_t(static_cast<std::int32_t>(bits)) : std::int64_t(bits);
                }
                case 8: {
                    if (is_signed) {
                        std::int64_t value = 0;
                        std::memcpy(&value, src, 8);
                        return value;
                    }
                    std::uint64_t bits = 0;
                    std::memcpy(&bits, src, 8);
                    return static_cast<std::int64_t>(bits);
                }
                default:
                    return 0;
            }
        }

        /// @brief Whether @p value is representable in a @p width-byte storage of that signedness.
        /// @param value The value to place.
        /// @param width The target's storage width in bytes (1, 2, 4 or 8).
        /// @param is_signed Whether the target's storage is signed (its IsSigned flag).
        /// @return true when the value fits without truncation; false when it does not (the caller then
        ///         reports failure and leaves the target storage unconstructed — no silent truncation).
        bool scalar_fits(std::int64_t value, std::size_t width, bool is_signed) noexcept {
            if (width == 0 || width > sizeof(std::int64_t)) {
                return false;
            }
            if (width == sizeof(std::int64_t)) {
                // Every int64 fits a signed 64-bit target; an unsigned one takes the non-negative half.
                return is_signed || value >= 0;
            }
            const auto bits = static_cast<unsigned>(width) * 8u;
            if (is_signed) {
                const std::int64_t limit = std::int64_t{1} << (bits - 1);
                return value >= -limit && value < limit;
            }
            return value >= 0 && static_cast<std::uint64_t>(value) < (std::uint64_t{1} << bits);
        }

        /// @brief Place the low @p width bytes of @p value into @p dst (two's complement).
        /// @param dst Unconstructed storage of the target type (an implicit-lifetime scalar).
        /// @param width The target's storage width in bytes (1, 2, 4 or 8).
        /// @param value The value to write; it must have passed scalar_fits().
        void write_scalar(void* dst, std::size_t width, std::int64_t value) noexcept {
            switch (width) {
                case 1: {
                    const auto bits = static_cast<std::uint8_t>(value);
                    std::memcpy(dst, &bits, 1);
                    break;
                }
                case 2: {
                    const auto bits = static_cast<std::uint16_t>(value);
                    std::memcpy(dst, &bits, 2);
                    break;
                }
                case 4: {
                    const auto bits = static_cast<std::uint32_t>(value);
                    std::memcpy(dst, &bits, 4);
                    break;
                }
                case 8: {
                    const auto bits = static_cast<std::uint64_t>(value);
                    std::memcpy(dst, &bits, 8);
                    break;
                }
                default:
                    break;
            }
        }
    }

    bool meta_type_t::is_void() const noexcept {
        return detail::interface_name(_iface) == "void";
    }

    index_t meta_type_t::id() const noexcept {
        return detail::interface_id(_iface);
    }

    std::string meta_type_t::name() const {
        if (!is_valid()) {
            return {};
        }
        return std::string(detail::interface_name(_iface));
    }

    type_tag_t meta_type_t::tag() const noexcept {
        if (!is_valid()) {
            return type_tag_flag::NONE;
        }
        return _iface->flags;
    }

    std::string meta_type_t::debug_string(const void* value) const {
        if (!is_valid() || value == nullptr) {
            return {};
        }
        if (_iface->debug_string != nullptr) {
            return _iface->debug_string(_iface, value);
        }
        return "<" + name() + ">";
    }

    meta_type_t meta_type_t::from_interface(const type_interface_t* iface) noexcept {
        if (iface == nullptr) {
            return meta_type_t(nullptr);
        }
        // Entry point: assign the runtime id (which also records the identity name) before handing
        // the interface out. The name is the original namer's output, produced on demand at compile
        // time, so no declaration or eager registration exists; this is a relaxed atomic load after
        // the first call.
        (void)detail::ensure_registered(iface);
        return meta_type_t(iface);
    }

    meta_type_t meta_type_t::resolve_global(std::string_view name) noexcept {
        const type_interface_t* iface = detail::interface_by_name(name);
        if (iface == nullptr) {
            return meta_type_t(nullptr);
        }
        // Every handle that exists carries an id: the lookup only finds the NAME, so the id is
        // assigned here (a no-op after the first call).
        (void)detail::ensure_registered(iface);
        return meta_type_t(iface);
    }

    namespace detail {
        std::string original_parameter_alias(std::string_view type) {
            // Locate the base: peel the leading cv words, then the trailing affixes. The slices
            // around [begin, end) are kept verbatim, so re-attaching them reproduces the caller's
            // spelling exactly (no reordering).
            std::size_t begin = 0;
            for (;;) {
                std::size_t word_end = begin;
                while (word_end < type.size() && is_ident_continue(type[word_end])) {
                    ++word_end;
                }
                const std::string_view word = type.substr(begin, word_end - begin);
                if (word_end == begin || word_end == type.size() || !is_space(type[word_end])
                    || (word != "const" && word != "volatile")) {
                    break;
                }
                begin = word_end;
                while (begin < type.size() && is_space(type[begin])) {
                    ++begin;
                }
            }

            std::size_t end = type.size();
            for (;;) {
                if (end >= 2 && type[end - 1] == '&' && type[end - 2] == '&') {
                    end -= 2;
                }
                else if (end >= 1 && (type[end - 1] == '&' || type[end - 1] == '*')) {
                    end -= 1;
                }
                else if (end >= 7 && type.substr(end - 6, 6) == " const") {
                    end -= 6;
                }
                else if (end >= 10 && type.substr(end - 9, 9) == " volatile") {
                    end -= 9;
                }
                else {
                    break;
                }
            }

            if (begin <= end) {
                const std::string_view base = type.substr(begin, end - begin);
                // The registry owns the names: resolve_global() tries the identity (primary) name and
                // then every bound alias, so the alias spelling, the canonical spelling and the
                // identity spelling all reach the same handle. An unknown spelling leaves the base
                // untouched (there is nothing to rewrite it FROM).
                const meta_type_t identity = meta_type_t::resolve_global(base);
                if (identity.is_valid()) {
                    const std::string to = identity.name();
                    std::string rewritten;
                    rewritten.reserve(type.size() - base.size() + to.size());
                    rewritten.append(type.substr(0, begin));
                    rewritten.append(to);
                    rewritten.append(type.substr(end));
                    return rewritten;
                }
            }
            return std::string(type);
        }
    }

    bool meta_type_t::is_object_pointer_trait() const noexcept {
        // Name comparison, not pointer comparison: each module has its own copy of the interface.
        return detail::interface_name(_iface)
            == detail::interface_name(type_interface_t::declared<object_pointer_t<object_t>>());
    }

    bool meta_type_t::has_conversion_to(const meta_type_t& target) const noexcept {
        if (!is_valid() || !target.is_valid()) {
            return false;
        }
        if (detail::same_type(_iface, target._iface)) {
            return true;
        }
        if (detail::find_conversion(_iface, target._iface) != nullptr) {
            return true;
        }
        // Structural identity: integer <-> enum / flags needs no registration (see is_structural_pair).
        return is_structural_pair(_iface->flags, target._iface->flags);
    }

    bool meta_type_t::convert(void* dst, const meta_type_t& target, const void* src) const {
        if (!is_valid() || !target.is_valid()) {
            throw bad_meta_type_t{};
        }
        if (dst == nullptr || src == nullptr) {
            return false;
        }
        if (detail::same_type(_iface, target._iface)) {
            return copy_construct(dst, src);
        }
        if (const metatype_conversion caller = detail::find_conversion(_iface, target._iface); caller != nullptr) {
            return caller(dst, src);
        }
        if (!is_structural_pair(_iface->flags, target._iface->flags)) {
            return false;
        }
        // Integer <-> enum / flags: read the source as a 64-bit value, then place it only when it is
        // representable in the TARGET's storage width. Out of width -> false with @p dst left
        // unconstructed (the existing contract; the common caller, any_t / write(), turns it into a
        // loud failure rather than a truncation).
        const std::int64_t value = read_scalar(src, _iface->flags, _iface->size);
        if (!scalar_fits(value, target._iface->size, bool(target._iface->flags & type_tag_flag::IS_SIGNED))) {
            return false;
        }
        write_scalar(dst, target._iface->size, value);
        return true;
    }

    bool meta_type_t::has_default_constructor() const noexcept {
        return is_valid() && _iface->default_ctor != nullptr;
    }

    bool meta_type_t::default_construct(void* mem) const {
        if (!is_valid()) {
            throw bad_meta_type_t{};
        }
        if (_iface->default_ctor == nullptr || mem == nullptr) {
            return false;
        }
        _iface->default_ctor(_iface, mem);
        return true;
    }

    bool meta_type_t::has_copy_constructor() const noexcept {
        return is_valid() && _iface->copy_ctor != nullptr;
    }

    bool meta_type_t::copy_construct(void* dst, const void* src) const {
        if (!is_valid()) {
            throw bad_meta_type_t{};
        }
        if (_iface->copy_ctor == nullptr || dst == nullptr || src == nullptr) {
            return false;
        }
        _iface->copy_ctor(_iface, dst, src);
        return true;
    }

    bool meta_type_t::has_move_constructor() const noexcept {
        return is_valid() && _iface->move_ctor != nullptr;
    }

    bool meta_type_t::move_construct(void* dst, void* src) const {
        if (!is_valid()) {
            throw bad_meta_type_t{};
        }
        if (_iface->move_ctor == nullptr || dst == nullptr || src == nullptr) {
            return false;
        }
        _iface->move_ctor(_iface, dst, src);
        return true;
    }

    bool meta_type_t::has_noexcept_move_constructor() const noexcept {
        return is_valid() && bool(_iface->flags & type_tag_flag::IS_NOEXCEPT_MOVE_CONSTRUCTIBLE);
    }

    void meta_type_t::noexcept_move_constructor(void* dst, void* src) const noexcept {
        if (!is_valid() || _iface->move_ctor == nullptr) {
            return;
        }
        _iface->move_ctor(_iface, dst, src);
    }

    bool meta_type_t::has_destructor() const noexcept {
        return is_valid() && _iface->dtor != nullptr;
    }

    void meta_type_t::destroy(void* mem) const noexcept {
        if (!is_valid() || mem == nullptr || _iface->dtor == nullptr) {
            return;
        }
        _iface->dtor(_iface, mem);
    }

    bool meta_type_t::has_copy_assignment() const noexcept {
        return is_valid() && _iface->copy_assign != nullptr;
    }

    bool meta_type_t::copy_assign(void* dst, const void* src) const {
        if (!is_valid()) {
            throw bad_meta_type_t{};
        }
        if (_iface->copy_assign == nullptr || dst == nullptr || src == nullptr) {
            return false;
        }
        _iface->copy_assign(_iface, dst, src);
        return true;
    }

    bool meta_type_t::has_move_assignment() const noexcept {
        return is_valid() && _iface->move_assign != nullptr;
    }

    bool meta_type_t::move_assign(void* dst, void* src) const {
        if (!is_valid()) {
            throw bad_meta_type_t{};
        }
        if (_iface->move_assign == nullptr || dst == nullptr || src == nullptr) {
            return false;
        }
        _iface->move_assign(_iface, dst, src);
        return true;
    }

    bool meta_type_t::has_noexcept_move_assignment() const noexcept {
        return is_valid() && bool(_iface->flags & type_tag_flag::IS_NOEXCEPT_MOVE_ASSIGNABLE);
    }

    void meta_type_t::noexcept_move_assign(void* dst, void* src) const noexcept {
        if (!is_valid() || _iface->move_assign == nullptr) {
            return;
        }
        _iface->move_assign(_iface, dst, src);
    }

    void* meta_type_t::allocate() const {
        if (!is_valid()) {
            throw bad_meta_type_t{};
        }
        // No allocator in the interface: size / alignment are enough (Qt's QMetaType does the same).
        if (_iface->alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
            return ::operator new(_iface->size, std::align_val_t{ _iface->alignment });
        }
        return ::operator new(_iface->size);
    }

    void meta_type_t::deallocate(void* mem) const noexcept {
        if (mem == nullptr || !is_valid()) {
            return;
        }
        if (_iface->alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
            ::operator delete(mem, std::align_val_t{ _iface->alignment });
            return;
        }
        ::operator delete(mem);
    }

    bool meta_type_t::has_equal_to() const noexcept {
        return is_valid() && _iface->equals != nullptr;
    }

    bool meta_type_t::equal_to(const void* lhs, const void* rhs) const {
        if (!is_valid()) {
            throw bad_meta_type_t{};
        }
        if (_iface->equals == nullptr || lhs == nullptr || rhs == nullptr) {
            return false;
        }
        return _iface->equals(_iface, lhs, rhs);
    }

    bool meta_type_t::has_less() const noexcept {
        return is_valid() && _iface->less != nullptr;
    }

    bool meta_type_t::less(const void* lhs, const void* rhs) const {
        if (!is_valid()) {
            throw bad_meta_type_t{};
        }
        if (_iface->less == nullptr || lhs == nullptr || rhs == nullptr) {
            return false;
        }
        return _iface->less(_iface, lhs, rhs);
    }

    bool meta_type_t::serializable() const noexcept {
        return is_valid() && _iface->serialize != nullptr;
    }

    std::optional<std::string> meta_type_t::serialize(const void* mem) const {
        if (!is_valid()) {
            throw bad_meta_type_t{};
        }
        if (_iface->serialize == nullptr || mem == nullptr) {
            return std::nullopt;
        }
        return _iface->serialize(_iface, mem);
    }

    bool meta_type_t::deserializable() const noexcept {
        return is_valid() && _iface->deserialize != nullptr;
    }

    bool meta_type_t::deserialize(void* mem, std::string_view data) const {
        if (!is_valid()) {
            throw bad_meta_type_t{};
        }
        if (_iface->deserialize == nullptr || mem == nullptr) {
            return false;
        }
        return _iface->deserialize(_iface, mem, data);
    }

    bool meta_type_t::not_equal_to(const void* lhs, const void* rhs) const {
        return !equal_to(lhs, rhs);
    }

    bool meta_type_t::greater(const void* lhs, const void* rhs) const {
        return less(rhs, lhs);
    }

    bool meta_type_t::less_equal(const void* lhs, const void* rhs) const {
        return !less(rhs, lhs);
    }

    bool meta_type_t::greater_equal(const void* lhs, const void* rhs) const {
        return !less(lhs, rhs);
    }

    HWSHQTB_EXPORT bool operator==(const meta_type_t& lhs, const meta_type_t& rhs) noexcept {
        return detail::same_type(lhs._iface, rhs._iface);
    }
}
