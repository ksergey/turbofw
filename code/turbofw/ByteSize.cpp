// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "ByteSize.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace turbofw {

auto ByteSize::fromString(std::string_view literal) -> std::expected<ByteSize, std::string> {
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
    return std::unexpected{std::string{"invalid size literal \""} + std::string{literal} + "\""};
}

} // namespace turbofw
