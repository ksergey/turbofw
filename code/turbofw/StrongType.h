// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cmath>
#include <type_traits>

#include <strong_type/strong_type.hpp>

namespace turbofw {
namespace detail {

template <auto NoValue>
struct Optional {
    template <typename T>
    struct modifier {
        /// Return true on object hold valid value and false whe no value
        constexpr auto hasValue() const noexcept -> bool {
            using value_type = strong::underlying_type_t<T>;
            auto const& self = static_cast<T const&>(*this);
            auto const& value = value_of(self);

            if constexpr (std::is_floating_point_v<value_type> && std::isnan(NoValue)) {
                return !std::isnan(value);
            } else {
                return value != NoValue;
            }
        }

        /// Reset to no value
        constexpr void reset() noexcept {
            auto& self = static_cast<T&>(*this);
            value_of(self) = NoValue;
        }
    };
};

/// No value type sugar
struct None {};
inline constexpr auto none = None{};

template <typename T, typename Tag, auto NoValue>
struct StrongTypeImpl {
    using type = strong::type<T, Tag, strong::regular, strong::formattable, Optional<NoValue>>;
};
template <typename T, typename Tag>
struct StrongTypeImpl<T, Tag, none> {
    using type = strong::type<T, Tag, strong::regular, strong::formattable>;
};

} // namespace detail

/// Strong typedef based on gh:rollbear/strong_type
template <typename T, typename Tag, auto NoValue = detail::none>
using StrongType = typename detail::StrongTypeImpl<T, Tag, NoValue>::type;

} // namespace turbofw
