// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <type_traits>

#include <turbofw/mp/List.h>

namespace turbofw::mixin {

/// Invokes `fn` for each mixin of a composed type: `fn(std::type_identity<Mixin>{})` for every `Mixin` of
/// `Self::Mixins`, in list order. The `std::type_identity` argument deduces the exact mixin type in the callback,
/// so calls on `self` can be qualified (`self.Mixin::method()`) -- unqualified member calls would be ambiguous
/// once several mixins define the same name. A plain `Self` without `Mixins` iterates nothing.
template <typename Self, typename Fn>
constexpr void forEachMixin(Fn&& fn) {
    // `Self` may arrive as a reference (deducing-this call sites pass `Self&&`): the `Mixins` lookup is
    // done on the decayed type, otherwise the requires check silently fails and no mixin is iterated
    using Decayed = std::remove_cvref_t<Self>;
    if constexpr (requires { typename Decayed::Mixins; }) {
        [&]<template <typename...> typename L, typename... Ts>(L<Ts...>) {
            (std::invoke(fn, std::type_identity<Ts>{}), ...);
        }(mp::Rename<typename Decayed::Mixins, mp::List>{});
    }
}

} // namespace turbofw::mixin
