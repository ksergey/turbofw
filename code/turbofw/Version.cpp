// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Version.h"

#ifndef TURBOFW_VERSION
#error "TURBOFW_VERSION must be defined by the build system"
#endif

namespace turbofw {

auto version() noexcept -> std::string_view {
    return TURBOFW_VERSION;
}

} // namespace turbofw
