// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <stdexcept>

#include <fmt/format.h>

namespace turbofw::config {

struct Exception : std::runtime_error {
    using std::runtime_error::runtime_error;

    template <typename... Args>
    Exception(fmt::format_string<Args...> fmtStr, Args&&... args)
        : std::runtime_error{fmt::format(fmtStr, std::forward<Args>(args)...)} {}

    virtual ~Exception() {}
};

struct ValidateError : Exception {
    using Exception::Exception;
};

} // namespace turbofw::config
