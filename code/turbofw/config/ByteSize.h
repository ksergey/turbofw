// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "Exception.h"
#include "Json.h"

namespace turbofw::config {

/// Byte size value, parsed from config literals like "64B", "100KiB", "1GiB", "1TiB" or bare number (bytes)
struct ByteSize {
    std::uint64_t bytes = 0;

    /// Parse size literal, throws Exception on error
    [[nodiscard]] static auto parse(std::string_view literal) -> ByteSize {
        // trim spaces
        while (!literal.empty() && std::isspace(static_cast<unsigned char>(literal.front()))) {
            literal.remove_prefix(1);
        }
        while (!literal.empty() && std::isspace(static_cast<unsigned char>(literal.back()))) {
            literal.remove_suffix(1);
        }

        // longest suffix first to prevent "100MiB" to be parsed as number+"B"
        struct Unit {
            std::string_view suffix;
            std::uint64_t multiplier;
        };
        static constexpr Unit units[] = {
            {"TiB", std::uint64_t{1} << 40},
            {"GiB", std::uint64_t{1} << 30},
            {"MiB", std::uint64_t{1} << 20},
            {"KiB", std::uint64_t{1} << 10},
            {"B", 1},
            {"", 1},
        };

        for (auto const& unit : units) {
            if (!literal.ends_with(unit.suffix)) {
                continue;
            }
            auto const digits = literal.substr(0, literal.size() - unit.suffix.size());
            std::uint64_t number = 0;
            auto const [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
            if (ec != std::errc{} || ptr != digits.data() + digits.size()) {
                continue;
            }
            return ByteSize{number * unit.multiplier};
        }
        throw Exception{"invalid size literal \"{}\"", literal};
    }

    [[nodiscard]] friend constexpr auto operator==(ByteSize lhs, ByteSize rhs) noexcept -> bool {
        return lhs.bytes == rhs.bytes;
    }
};

/// Mapping for ByteSize: unsigned number or string literal
template <>
struct JsonMapping<ByteSize> {
    static void read(nlohmann::json const& json, ByteSize& value) {
        if (json.is_number_unsigned()) {
            value = ByteSize{json.get<std::uint64_t>()};
            return;
        }
        if (json.is_string()) {
            value = ByteSize::parse(json.get<std::string>());
            return;
        }
        throw Exception{"expected size literal (number or string like \"1MiB\")"};
    }
    static void write(nlohmann::json& json, ByteSize value) {
        json = value.bytes;
    }
};

} // namespace turbofw::config
