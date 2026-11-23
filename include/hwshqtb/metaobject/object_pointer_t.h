#ifndef HWSHQTB__METAOBJECT__OBJECT_POINTER_H
#define HWSHQTB__METAOBJECT__OBJECT_POINTER_H

/// @file object_pointer_t.h
/// @brief A weak reference to an object that becomes null when the object is destroyed.

#include "object_t.h"
#include "export.h"

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace hwshqtb::metaobject {
    /// @brief A guarded pointer to an object_t (QPointer equivalent).
    ///
    /// object_pointer_t never keeps its target alive and turns null as soon as the target is destroyed,
    /// so it is safe to keep one across a delete. It is not thread-safe against the destruction of
    /// the target happening on another thread.
    ///
    /// A guard is tied to its object by the object's death token, not by the raw address alone:
    ///  - converting a guard to a guard of a related type registers through the same path as
    ///    object_pointer_t(T*), so the converted guard is live and nulls with the object;
    ///  - swap() exchanges the (pointer, token) pairs, so both guards stay live;
    ///  - operator== / operator!= compare the guarded object pointers, never the tokens.
    ///
    /// @code
    /// object_pointer_t<object_t> guard(&obj);
    /// delete &obj;
    /// assert(guard.data() == nullptr);
    /// @endcode
    template <typename T = object_t>
    class HWSHQTB_EXPORT object_pointer_t {
        static_assert(std::is_base_of_v<object_t, T>, "object_pointer_t requires an object_t-derived type");

    public:
        /// @brief Construct a null pointer.
        /// @note noexcept.
        object_pointer_t() noexcept = default;

        /// @brief Construct from an object; nullptr stays null.
        /// @param obj The target object (not kept alive).
        /// @note noexcept.
        object_pointer_t(T* obj) noexcept:
            _raw(obj),
            _watch(obj != nullptr ? obj->watch() : std::weak_ptr<detail::death_watch_t>()) { }

        /// @brief Convert from a guard to a related type (`object_pointer_t<U>` -> this).
        /// @tparam U The source type; @c U* must be convertible to @c T*.
        /// @param other The source guard; it keeps its own target.
        /// @note The result is a LIVE guard: it is registered through object_pointer_t(T*), so it
        ///       holds its own weak reference to the target's death token and nulls with the object,
        ///       exactly like a guard built from the raw pointer.
        /// @note A source that is already null or already dead yields a null guard.
        /// @note noexcept.
        template <typename U>
            requires std::is_convertible_v<U*, T*>
        object_pointer_t(const object_pointer_t<U>& other) noexcept:
            object_pointer_t(other.data()) { }

        /// @brief Move-convert from a guard to a related type; @p other is cleared.
        /// @tparam U The source type; @c U* must be convertible to @c T*.
        /// @param other The source guard; cleared, so it is null afterwards (same as Qt's move).
        /// @note The new guard is registered through object_pointer_t(T*) and stays live.
        /// @note noexcept.
        template <typename U>
            requires std::is_convertible_v<U*, T*>
        object_pointer_t(object_pointer_t<U>&& other) noexcept:
            object_pointer_t(other.data()) {
            other.clear();
        }

        /// @brief Rebind to another object.
        /// @param obj The new target; nullptr clears the pointer.
        /// @return *this.
        /// @note noexcept.
        object_pointer_t& operator=(T* obj) noexcept {
            _raw = obj;
            _watch = (obj != nullptr) ? obj->watch() : std::weak_ptr<detail::death_watch_t>();
            return *this;
        }

        /// @brief Convert-assign from a guard to a related type (copy-and-swap).
        /// @tparam U The source type; @c U* must be convertible to @c T*.
        /// @param other The source guard; it keeps its own target.
        /// @return *this.
        /// @note The temporary is a live guard (see the converting constructor), so the assignment
        ///       ends up registered with the target's death token like object_pointer_t(T*).
        /// @note noexcept.
        template <typename U>
            requires std::is_convertible_v<U*, T*>
        object_pointer_t& operator=(const object_pointer_t<U>& other) noexcept {
            object_pointer_t(other).swap(*this);
            return *this;
        }

        /// @brief Convert-assign from a guard to a related type; @p other is cleared.
        /// @tparam U The source type; @c U* must be convertible to @c T*.
        /// @param other The source guard; cleared, so it is null afterwards.
        /// @return *this.
        /// @note noexcept.
        template <typename U>
            requires std::is_convertible_v<U*, T*>
        object_pointer_t& operator=(object_pointer_t<U>&& other) noexcept {
            object_pointer_t(std::move(other)).swap(*this);
            return *this;
        }

        /// @brief The target, or nullptr once it has been destroyed.
        /// @note noexcept.
        T* data() const noexcept {
            if (_raw == nullptr) {
                return nullptr;
            }
            const auto watch = _watch.lock();
            if (!watch || watch->dead.load(std::memory_order_acquire)) {
                return nullptr;
            }
            return _raw;
        }

        /// @brief Member access; the caller must have checked data() first.
        /// @note noexcept.
        T* operator->() const noexcept {
            return data();
        }

        /// @brief Dereference; the caller must have checked data() first.
        /// @note noexcept.
        T& operator*() const noexcept {
            return *data();
        }

        /// @brief Whether the target is still alive.
        /// @note noexcept.
        explicit operator bool() const noexcept {
            return data() != nullptr;
        }

        /// @brief Forget the target without touching it.
        /// @note noexcept.
        void clear() noexcept {
            _raw = nullptr;
            _watch.reset();
        }

        /// @brief Exchange the guarded objects.
        /// @param other The guard to exchange with.
        /// @note Each guard receives exactly the other's (pointer, death token) PAIR, so both stay
        ///       consistent: after the swap each one nulls when the object it now guards dies. The
        ///       two members always travel together — there is no state that could pair one guard's
        ///       pointer with the other's token.
        /// @note noexcept.
        void swap(object_pointer_t& other) noexcept {
            std::swap(_raw, other._raw);
            _watch.swap(other._watch);
        }

    private:
        /// @brief The target as handed out (valid only while the watch says it is alive).
        T* _raw = nullptr;
        /// @brief Liveness token of the target.
        std::weak_ptr<detail::death_watch_t> _watch;
    };

    /// @name Equality comparison (QPointer's == / !=)
    ///
    /// Every overload compares the object the guards point at — that is, data() — and never the
    /// detail::death_watch_t tokens: two guards to the same live object compare equal, two dead guards
    /// compare equal (both are null), a guard to one object never equals a guard to another.
    ///
    /// The templates are constrained so that only comparable pointer types take part (one converts
    /// to the other); unrelated pointer types remain ill-formed instead of silently comparing
    /// addresses. operator!= is not written out: C++20 rewriting synthesises it from operator==,
    /// including the reversed operand order.
    /// @{
    /// @brief Compare two guards by the guarded object.
    /// @note noexcept.
    template <typename T, typename U>
        requires (std::is_convertible_v<U*, T*> || std::is_convertible_v<T*, U*>)
    bool operator==(const object_pointer_t<T>& lhs, const object_pointer_t<U>& rhs) noexcept {
        return lhs.data() == rhs.data();
    }

    /// @brief Compare a guard with a raw pointer.
    /// @note noexcept.
    template <typename T, typename U>
        requires (std::is_convertible_v<U*, T*> || std::is_convertible_v<T*, U*>)
    bool operator==(const object_pointer_t<T>& lhs, U* rhs) noexcept {
        return lhs.data() == rhs;
    }

    /// @brief Compare a raw pointer with a guard (reversed form).
    /// @note noexcept.
    template <typename T, typename U>
        requires (std::is_convertible_v<U*, T*> || std::is_convertible_v<T*, U*>)
    bool operator==(U* lhs, const object_pointer_t<T>& rhs) noexcept {
        return lhs == rhs.data();
    }

    /// @brief Compare a guard with nullptr.
    /// @note noexcept.
    template <typename T>
    bool operator==(const object_pointer_t<T>& lhs, std::nullptr_t) noexcept {
        return lhs.data() == nullptr;
    }

    /// @brief Compare nullptr with a guard (reversed form).
    /// @note noexcept.
    template <typename T>
    bool operator==(std::nullptr_t, const object_pointer_t<T>& rhs) noexcept {
        return rhs.data() == nullptr;
    }
    /// @}

    /// @name Recovering guards stored in an any_t (qPointerFromVariant)
    /// @{
    /// @brief Recover the guarded pointer stored in @p value (qPointerFromVariant&lt;T&gt;).
    /// @tparam T The type to recover; an object_t-derived type. It needs no registration at all (the
    ///         shared object-pointer interface covers every object_t).
    /// @param value The variable holding an object pointer; see any_t's pointer constructor.
    /// @return A live guard to the stored object when @p value holds an object pointer whose target
    ///         is still alive and is a T; an empty guard when @p value is not initialised, does not
    ///         hold an object pointer, holds a dead one, or holds a target that is not a T.
    /// @note The stored object pointer is copied out by value, which shares the target's death
    ///       token, so the returned guard is live and nulls with the object; the down-cast to T is
    ///       checked with cast<T> (a mismatched target yields an empty guard). A reflected T is
    ///       matched through its meta-object chain; an ordinary T falls back to dynamic_cast.
    template <typename T>
        requires std::derived_from<T, object_t>
    object_pointer_t<T> pointer_from_any(const any_t& value) {
        if (!value.is_initialized() || !value.type().is_object_pointer_trait()) {
            return {};
        }
        const object_pointer_t<object_t> stored = value.get<object_pointer_t<object_t>>();
        object_t* const raw = stored.data();
        if (raw == nullptr) {
            return {};
        }
        return object_pointer_t<T>(cast<T>(raw));
    }

    /// @brief Identity form for generic code: a guard already is the guarded pointer.
    /// @tparam T An object_t-derived type.
    /// @param pointer The guard to hand back.
    /// @return The same guard.
    template <typename T>
        requires std::derived_from<T, object_t>
    object_pointer_t<T> pointer_from_any(object_pointer_t<T> pointer) {
        return pointer;
    }
    /// @}
}

// meta_type_t::object_pointer_t_type() is constexpr and defined here because
// object_pointer_t<object_t> must be complete for the on-demand interface production.
namespace hwshqtb::metaobject {
    constexpr meta_type_t meta_type_t::object_pointer_t_type() noexcept {
        return meta_type_t(type_interface_t::declared<object_pointer_t<object_t>>());
    }
}

#endif
