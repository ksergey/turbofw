// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <string_view>

namespace turbofw {

/// turbofw version string ("major.minor.patch"), taken from the CMake project version.
[[nodiscard]] auto version() noexcept -> std::string_view;

} // namespace turbofw
