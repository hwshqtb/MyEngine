#include <hwshqtb/metaobject/any_t.h>

#include <hwshqtb/metaobject/object_pointer_t.h>

#include <utility>

namespace hwshqtb::metaobject {
    any_t any_t::adopt_object_pointer_t(object_t* pointer) {
        // Every object pointer shares the one statically declared object_pointer_t<object_t> interface,
        // so this needs neither a registry lookup nor any bootstrap call: the handle is a compile-time
        // constant and the registry assigns its id on first use. It never throws.
        return from(object_pointer_t<object_t>(pointer), meta_type_t::object_pointer_t_type());
    }

    any_t::any_t() noexcept {
        // The INVALID state, for free: _type is the invalid meta_type_t (no interface), _mem is null,
        // and every flag keeps its initialiser.
        // Written out at the definition rather than `= default`: the declaration in the header
        // promises noexcept, and GCC derives a DIFFERENT exception specification for the explicitly
        // defaulted out-of-line definition ("has a different exception specifier"). A user-provided
        // body cannot disagree with the declaration.
    }

    any_t::any_t(const meta_type_t& type):
        _type(type) {
        if (!type.is_valid()) {
            throw bad_meta_type_t{};
        }
        if (type.is_void()) {
            // The void type has no object to build and therefore no storable slot: the variable stays
            // VALID and declared only, with nothing reserved, and operator&() refuses it instead of
            // handing out a zero-byte block that nothing could be constructed into. (This is NOT the
            // invalid state, which declares no type at all.)
            return;
        }
        // Reserve the slot but construct nothing: the variable is "declared only" (is_initialized()
        // stays false) and operator&() answers the reserved storage, so a caller can have a thunk
        // build the object in place. A non-null _mem therefore does not by itself mean "constructed".
        _mem = type.allocate();
        _owns = true;
        _constructed = false;
    }

    any_t::any_t(const any_t& other)
        :
        any_t() {
        *this = other;
    }

    any_t::any_t(any_t&& other) noexcept
        :
        _type(other._type),
        _mem(nullptr),
        _qualifiers(other._qualifiers),
        _owns(other._owns),
        _constructed(other._constructed) {
        if (other._mem == nullptr) {
            other._owns = false;
            return;
        }
        if (!other._owns) {
            _mem = other._mem; // alias: move the pointer (still refers to the original object)
            other._mem = nullptr;
            other._owns = false;
            other._constructed = false;
            return;
        }
        if (!other._constructed) {
            // Declared only: there is no object to move, so the storage pointer is simply taken over.
            _mem = other._mem;
            other._mem = nullptr;
            other._owns = false;
            return;
        }
        if (_type.has_noexcept_move_constructor()) {
            _mem = _type.allocate(); // note: allocating inside noexcept; OOM would call terminate
            _type.noexcept_move_constructor(_mem, other._mem);
            // the source keeps its storage and its moved-from value (still well-defined)
        }
        else {
            _mem = other._mem; // move the storage pointer; the source falls back to "no storage"
            other._mem = nullptr;
            other._owns = false;
            other._constructed = false;
        }
    }

    any_t::~any_t() {
        if (_mem != nullptr && _owns) {
            if (_constructed) {
                _type.destroy(_mem);
            }
            _type.deallocate(_mem);
        }
    }

    any_t& any_t::operator=(const any_t& other) {
        // std::addressof, NOT &other: any_t overloads operator& (it is the storage accessor), so
        // `&other` would answer the OTHER variable's storage address instead of the object's own.
        if (this == std::addressof(other)) {
            return *this;
        }
        reset();
        _type = other._type;
        _qualifiers = other._qualifiers;
        _owns = other._owns;
        if (other._mem == nullptr) {
            _mem = nullptr;
            _constructed = false;
            return *this;
        }
        if (!other._owns) {
            _mem = other._mem;
            _constructed = other._constructed;
            return *this;
        }
        _mem = _type.allocate();
        if (!other._constructed) {
            _constructed = false;
            return *this;
        }
        if (!_type.copy_construct(_mem, other._mem)) {
            _type.deallocate(_mem);
            _mem = nullptr;
            _owns = false;
            _constructed = false;
            throw bad_any_unsupported_t{};
        }
        _constructed = true;
        return *this;
    }

    any_t& any_t::operator=(any_t&& other) noexcept {
        // std::addressof, for the same reason as the copy assignment: `&other` is any_t::operator&().
        if (this == std::addressof(other)) {
            return *this;
        }
        // Same type, both sides constructed and nothrow move-assignable: move-assign in place on the
        // existing storage; otherwise swap the storage pointers. A declared-only side has no object to
        // assign from / into, so it always takes the swap path.
        // Note: same type is decided by meta_type_t's identity rule (runtime id, name while an id is
        // still 0). Comparing raw ids here would treat two different unregistered types as equal,
        // because 0 is the "unassigned" sentinel.
        if (_mem != nullptr && _owns && other._mem != nullptr && other._owns
            && _constructed && other._constructed
            && _type == other._type
            && _type.has_noexcept_move_assignment()) {
            _type.noexcept_move_assign(_mem, other._mem);
            _qualifiers = other._qualifiers;
            return *this;
        }
        swap(other);
        return *this;
    }

    void any_t::reset() noexcept {
        if (_mem != nullptr && _owns) {
            if (_constructed) {
                _type.destroy(_mem);
            }
            _type.deallocate(_mem);
        }
        _mem = nullptr;
        _owns = false;
        _constructed = false;
        _qualifiers = qualifier_flag::NONE;
    }

    void any_t::swap(any_t& other) noexcept {
        using std::swap;
        swap(_type, other._type);
        swap(_mem, other._mem);
        swap(_qualifiers, other._qualifiers);
        swap(_owns, other._owns);
        swap(_constructed, other._constructed);
    }

    bool any_t::is_valid() const noexcept {
        // "The variable declares a type at all". The invalid state is the one default construction
        // produces (and a failed call reports): _type is then the invalid handle, never void.
        return _type.is_valid();
    }

    bool any_t::is_initialized() const noexcept {
        // The constructed flag, NOT "is there a storage address": a declared-only variable owns its
        // (empty) storage and must still answer false, and an invalid one owns nothing at all.
        return _constructed;
    }

    meta_type_t any_t::type() const noexcept {
        return _type;
    }

    qualifier_t any_t::qualifier() const noexcept {
        return _qualifiers;
    }

    std::string any_t::serialize() const {
        if (!is_valid()) {
            throw bad_meta_type_t{};
        }
        if (!is_initialized()) {
            throw bad_any_uninitialized_t{};
        }
        auto result = _type.serialize(_mem);
        if (!result.has_value()) {
            throw bad_any_unsupported_t{};
        }
        return std::move(*result);
    }

    bool any_t::deserialize(std::string_view data) {
        if (!is_valid()) {
            // No type to deserialize INTO: that is the invalid state, not "the type says no".
            throw bad_meta_type_t{};
        }
        if (!_type.deserializable()) {
            throw bad_any_unsupported_t{};
        }
        reset();
        void* mem = _type.allocate();
        if (!_type.deserialize(mem, data)) {
            _type.deallocate(mem);
            _mem = nullptr;
            _owns = false;
            _constructed = false;
            return false;
        }
        _mem = mem;
        _owns = true;
        _constructed = true;
        _qualifiers = qualifier_flag::NONE;
        return true;
    }

    any_t any_t::adopt(const meta_type_t& type, void* mem, qualifier_t qualifiers) noexcept {
        any_t var;
        var._type = type;
        var._mem = mem;
        var._owns = (mem != nullptr);
        // A non-null block is a block the caller has already filled in, i.e. a constructed object.
        var._constructed = (mem != nullptr);
        var._qualifiers = qualifiers;
        return var;
    }

    void* any_t::operator&() {
        if (!is_valid()) {
            // The invalid state: the variable declares no type, so it has no storage even in
            // principle. This is a DIFFERENT failure from "declared, but nothing reserved".
            throw bad_meta_type_t{};
        }
        if (_mem == nullptr) {
            // The variable declares a type but holds no storage: a void declaration, a reset one and
            // a moved-from one all land here.
            throw bad_any_unsupported_t{ "any_t::operator&: the variable holds no storage" };
        }
        if (!_owns && (_qualifiers & qualifier_flag::CONST)) {
            // An alias to a CONST object: the operator must not launder the constness away. The const
            // overload answers the same address as a `const void*`.
            throw bad_any_unsupported_t{
                "any_t::operator&: an alias to a const object has no mutable address"
            };
        }
        return _mem;
    }

    const void* any_t::operator&() const {
        if (!is_valid()) {
            throw bad_meta_type_t{};
        }
        if (_mem == nullptr) {
            throw bad_any_unsupported_t{ "any_t::operator&: the variable holds no storage" };
        }
        return _mem;
    }

    HWSHQTB_EXPORT void swap(any_t& lhs, any_t& rhs) noexcept {
        lhs.swap(rhs);
    }
}
