// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <type_traits>

namespace turbofw::mp {

/// Inherit from all types in L
template <typename>
struct Inherit;
template <template <typename...> typename L, typename... Ts>
struct Inherit<L<Ts...>> : Ts... {};

} // namespace turbofw::mp
