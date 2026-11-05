// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <type_traits>

namespace turbofw::mp {

template <std::size_t N>
using SizeT = std::integral_constant<std::size_t, N>;

template <typename T>
using ToBool = std::bool_constant<static_cast<bool>(T::value)>;

} // namespace turbofw::mp
