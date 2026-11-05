// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <type_traits>

namespace turbofw::mp {

/// Integral constant with a std::size_t value: a bridge between a compile-time index/size and a metafunction
/// whose parameter is a type (see `At`), and the result type of the lookup functions (`Find`, `FindIf`)
template <std::size_t N>
using SizeT = std::integral_constant<std::size_t, N>;

/// Normalizes a trait to a `bool`-valued one: `T` must be a trait with a static value member convertible to
/// bool; the result is usable where every list element must be evaluated (see `FindIf`)
template <typename T>
using ToBool = std::bool_constant<static_cast<bool>(T::value)>;

} // namespace turbofw::mp
