// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <string_view>

namespace turbofw {

/// Compile-time string
template <std::size_t N>
struct CtString {
    char data[N] = {0};

    /// Construct empty string
    consteval CtString() = default;

    /// Construct from c-string
    consteval CtString(char const* str) noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            data[i] = str[i];
        }
    }

    /// Implied convert to std::string_view
    [[nodiscard]] constexpr operator std::string_view() const noexcept {
        return std::string_view{data, N - 1};
    }

    /// Return c-string
    [[nodiscard]] constexpr auto c_str() const noexcept -> char const* {
        return data;
    }
};

/// CTAD
template <std::size_t N>
CtString(char const (&)[N]) -> CtString<N>;

} // namespace turbofw
