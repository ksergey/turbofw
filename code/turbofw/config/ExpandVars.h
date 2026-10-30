// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdlib>
#include <map>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "Exception.h"

namespace turbofw::config {

/// Expand "${VAR}" entries in input using @c vars, then process environment variables.
/// Throws Exception on unknown variable or bad syntax
[[nodiscard]] inline auto expandVars(std::string_view input, std::map<std::string, std::string, std::less<>> const& vars)
    -> std::string {
    std::string result;
    result.reserve(input.size());
    for (std::size_t pos = 0; pos < input.size();) {
        auto const begin = input.find("${", pos);
        if (begin == std::string_view::npos) {
            result.append(input.substr(pos));
            break;
        }
        result.append(input.substr(pos, begin - pos));
        auto const end = input.find('}', begin);
        if (end == std::string_view::npos) {
            throw Exception{"unterminated variable in \"{}\"", input};
        }
        auto const name = input.substr(begin + 2, end - (begin + 2));
        if (name.empty()) {
            throw Exception{"empty variable in \"{}\"", input};
        }
        if (auto const found = vars.find(name); found != vars.end()) {
            result.append(found->second);
        } else if (auto const value = ::getenv(std::string(name).c_str()); value != nullptr) {
            result.append(value);
        } else {
            throw Exception{"unknown variable \"{}\" in \"{}\"", name, input};
        }
        pos = end + 1;
    }
    return result;
}

} // namespace turbofw::config
