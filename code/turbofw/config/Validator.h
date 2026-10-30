// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <array>
#include <functional>
#include <regex>
#include <string_view>
#include <type_traits>

namespace turbofw::config {

/// Type trait for validators (acts as compiler-time marker)
template <typename T>
concept Validator = requires { typename T::Validator; };

namespace detail {

template <typename TypeA, typename TypeB>
    requires Validator<TypeA> && Validator<TypeB>
struct ValidatorAnd {
    using Validator = void;

    TypeA validatorA;
    TypeB validatorB;

    template <typename T>
    constexpr auto operator()(T const& value) const -> bool {
        return validatorA(value) && validatorB(value);
    }
};

} // namespace detail

template <typename TypeA, typename TypeB>
    requires Validator<TypeA> && Validator<TypeB>
consteval auto operator&&(TypeA&& a, TypeB&& b) noexcept -> detail::ValidatorAnd<TypeA, TypeB> {
    return {std::move(a), std::move(b)};
}

namespace detail {

template <typename TypeA, typename TypeB>
    requires Validator<TypeA> && Validator<TypeB>
struct ValidatorOr {
    using Validator = void;

    TypeA validatorA;
    TypeB validatorB;

    template <typename T>
    constexpr auto operator()(T const& value) const -> bool {
        return validatorA(value) || validatorB(value);
    }
};

} // namespace detail

template <typename TypeA, typename TypeB>
    requires Validator<TypeA> && Validator<TypeB>
consteval auto operator||(TypeA&& a, TypeB&& b) noexcept -> detail::ValidatorOr<TypeA, TypeB> {
    return {std::move(a), std::move(b)};
}

namespace detail {

template <typename TypeT>
    requires Validator<TypeT>
struct ValidatorNot {
    using Validator = void;

    TypeT validator;

    template <typename T>
    constexpr auto operator()(T const& value) const -> bool {
        return !validator(value);
    }
};

} // namespace detail

template <typename TypeT>
    requires Validator<TypeT>
consteval auto operator!(TypeT&& a) noexcept -> detail::ValidatorNot<TypeT> {
    return {std::move(a)};
}

namespace detail {

template <typename T, template <typename> typename C>
struct CompareWith {
    using Validator = void;

    T const withValue;

    constexpr auto operator()(T const& value) const noexcept -> bool {
        return C<T>{}(value, withValue);
    }
    template <typename U>
    constexpr auto operator()(U const& value) const noexcept -> bool {
        return C<void>{}(value, withValue);
    }
};

template <std::size_t N, template <typename> typename C>
struct CompareWith<char[N], C> : CompareWith<std::string_view, C> {};

} // namespace detail

/// Compare: equal
template <typename T>
consteval auto eq(T const& value) -> detail::CompareWith<T, std::equal_to> {
    return {value};
}

/// Compare: not equal
template <typename T>
consteval auto ne(T const& value) -> detail::CompareWith<T, std::not_equal_to> {
    return {value};
}

/// Compare: greater
template <typename T>
consteval auto gt(T const& value) -> detail::CompareWith<T, std::greater> {
    return {value};
}

/// Compare: greater or equal
template <typename T>
consteval auto ge(T const& value) -> detail::CompareWith<T, std::greater_equal> {
    return {value};
}

/// Compare: less
template <typename T>
consteval auto lt(T const& value) -> detail::CompareWith<T, std::less> {
    return {value};
}

/// Compare: less or equal
template <typename T>
consteval auto le(T const& value) -> detail::CompareWith<T, std::less_equal> {
    return {value};
}

namespace detail {

template <typename FnT>
struct Fn {
    using Validator = void;

    FnT fn;

    template <typename U>
    constexpr auto operator()(U const& value) const noexcept -> bool {
        return fn(value);
    }
};

template <typename FnT>
Fn(FnT) -> Fn<FnT>;

// helper type for the visitor
template <class... Ts>
struct Overloads : Ts... {
    using Ts::operator()...;
};

} // namespace detail

/// Check that value is empty
consteval auto empty() {
    // clang-format off
    return detail::Fn{detail::Overloads{
        [](auto const& value) {
            return std::empty(value);
        },
        [](char const* value) {
            return *value == '\0';
        }
    }};
    // clang-format on
}

/// Check that value is one of the listed
template <typename T, std::size_t N>
consteval auto oneOf(T const (&values)[N]) {
    return detail::Fn([values = std::to_array(values)](auto const& value) -> bool {
        return std::find(values.begin(), values.end(), value) != values.end();
    });
}

/// @overload
template <typename... Ts>
consteval auto oneOf(Ts const&... args) {
    using T = std::common_type_t<Ts...>;
    return oneOf<T, sizeof...(Ts)>({static_cast<T>(args)...});
}

/// Match string-value to regex pattern
inline auto match(char const* pattern) {
    return detail::Fn([regex = std::regex(pattern)](std::string_view value) -> bool {
        std::cmatch match;
        return std::regex_match(value.begin(), value.end(), match, regex);
    });
}

} // namespace turbofw::config
