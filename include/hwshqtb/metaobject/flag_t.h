#ifndef HWSHQTB__METAOBJECT__FLAG_H
#define HWSHQTB__METAOBJECT__FLAG_H

#include <concepts>
#include <type_traits>

namespace hwshqtb::metaobject {
    namespace detail {
        template <typename Enum>
        concept is_enum = std::is_enum_v<Enum>;
    }

    /// @brief The value-layer wrapper of an enum used as a bit set (QFlags\<E\> equivalent).
    /// @tparam Enum The wrapped enum.
    template <detail::is_enum Enum>
    struct flag_t;

    namespace detail {
        /// @brief Whether T is a flag_t\<E\> instantiation.
        template <typename T>
        struct is_flag_trait: std::false_type { };

        template <is_enum E>
        struct is_flag_trait<flag_t<E>>: std::true_type { };

        /// @brief Convenience variable template of is_flag_trait.
        template <typename T>
        inline constexpr bool is_flag_v = is_flag_trait<T>::value;

        /// @brief The enum wrapped by a flag_t\<E\>; has no member `type` for anything else, so a use
        ///        with a non-flag type is a substitution failure rather than a wrong answer.
        template <typename T>
        struct enum_of_flag_trait { };

        template <is_enum E>
        struct enum_of_flag_trait<flag_t<E>> {
            using type = E;
        };

        /// @brief The enum wrapped by the flag_t T (T must be a flag_t).
        template <typename T>
        using enum_of_flag_t = typename enum_of_flag_trait<T>::type;
    }

    template <detail::is_enum Enum>
    struct flag_t {
        using underlying_type = std::underlying_type_t<Enum>;

        underlying_type data{};

        constexpr flag_t() = default;
        constexpr flag_t(const flag_t& other) = default;
        constexpr flag_t(flag_t&& other) = default;
        constexpr ~flag_t() = default;

        constexpr flag_t& operator=(const flag_t& other) = default;
        constexpr flag_t& operator=(flag_t&& other) = default;

        constexpr flag_t(Enum other) noexcept:
            data(static_cast<underlying_type>(other)) { }

        constexpr flag_t& operator=(Enum other) noexcept {
            data = static_cast<underlying_type>(other);
            return *this;
        }

        constexpr operator Enum() const noexcept {
            return static_cast<Enum>(data);
        }

        constexpr operator bool() const noexcept {
            return data;
        }

        constexpr flag_t operator~() const noexcept {
            return flag_t{ static_cast<Enum>(~data) };
        }

        constexpr bool operator==(const flag_t& other) const noexcept {
            return data == other.data;
        }

        constexpr bool operator==(Enum other) const noexcept {
            return data == static_cast<underlying_type>(other);
        }

        constexpr bool operator!=(const flag_t& other) const noexcept {
            return data != other.data;
        }

        constexpr bool operator!=(Enum other) const noexcept {
            return data != static_cast<underlying_type>(other);
        }

#define ASSIGNMENT(op)                                                               \
    constexpr flag_t& operator op## = (const flag_t& other) noexcept {               \
        data op## = other.data;                                                      \
        return *this;                                                                \
    }                                                                                \
    constexpr flag_t& operator op## = (Enum other) noexcept {                        \
        data op## = static_cast<underlying_type>(other);                             \
        return *this;                                                                \
    }                                                                                \
    friend constexpr flag_t operator op(const flag_t& a, const flag_t& b) noexcept { \
        return flag_t{ a } op## = b;                                                 \
    }                                                                                \
    friend constexpr flag_t operator op(const flag_t& a, Enum b) noexcept {          \
        return flag_t{ a } op## = b;                                                 \
    }                                                                                \
    friend constexpr flag_t operator op(Enum a, const flag_t& b) noexcept {          \
        return flag_t{ a } op## = b;                                                 \
    }

        ASSIGNMENT(|);
        ASSIGNMENT(&);
        ASSIGNMENT(^);
#undef ASSIGNMENT

#define ASSIGNMENT(op)                                                    \
    flag_t& operator op## = (const flag_t& other) = delete;               \
    flag_t& operator op## = (Enum other) = delete;                        \
    friend flag_t operator op(const flag_t& a, const flag_t& b) = delete; \
    friend flag_t operator op(const flag_t& a, Enum b) = delete;          \
    friend flag_t operator op(Enum a, const flag_t& b) = delete;

        ASSIGNMENT(+);
        ASSIGNMENT(-);
        ASSIGNMENT(*);
        ASSIGNMENT(/);
        ASSIGNMENT(%);
        ASSIGNMENT(<<);
        ASSIGNMENT(>>);
#undef ASSIGNMENT
    };
}

#endif
