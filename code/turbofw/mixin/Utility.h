// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <turbofw/mp/List.h>

namespace turbofw::mixin {

template <typename Self, typename Fn>
constexpr void forEachMixin(Fn&& fn) {
    if constexpr (requires { typename Self::Mixins; }) {
        [&]<template <typename...> typename L, typename... Ts>(L<Ts...>) {
            (std::invoke(fn, std::type_identity<Ts>{}), ...);
        }(mp::Rename<typename Self::Mixins, mp::List>{}); // rename in case of Mixins is something like tuple
    }
}

} // namespace turbofw::mixin
