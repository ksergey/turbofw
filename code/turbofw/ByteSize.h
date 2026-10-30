// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace turbofw {

/// Byte size value, parsed from literals like "64B", "100KiB", "1GiB", "1TiB" or bare number (bytes)
struct ByteSize {
    std::size_t bytes = 0;

    /// Parse size literal, error message on failure
    [[nodiscard]] static auto fromString(std::string_view literal) -> std::expected<ByteSize, std::string>;

    [[nodiscard]] friend constexpr auto operator<=>(ByteSize lhs, ByteSize rhs) noexcept = default;
};

} // namespace turbofw
