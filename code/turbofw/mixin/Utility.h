// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <type_traits>

#include <turbofw/mp/List.h>

namespace turbofw::mixin {

/// Invokes `fn` for each mixin of a composed type: `fn(std::type_identity<Mixin>{})` for every `Mixin` of
/// `Self::Mixins`, in list order.
///
/// \code{.cpp}
/// forEachMixin<Self>([&]<typename Mixin>(std::type_identity<Mixin>) {
///     if constexpr (requires { self.Mixin::updateBody(msg); }) {
///         self.Mixin::updateBody(msg);
///     }
/// });
/// \endcode
///
/// The `std::type_identity` argument turns Mixin into a deduced template parameter of the callback, so the
/// callback can qualify calls on `self` with the exact mixin type (`self.Mixin::method()`); unqualified member
/// calls on `self` would be ambiguous once several mixins define the same name.
///
/// A plain (not composed) `Self` has no `Mixins`: nothing happens. `Self::Mixins` may be any list-like type
/// (for example `std::tuple`); it is renamed to `mp::List` first, so a non-list-like `Mixins` is a compile
/// error here -- make mixins a list, not a loose pack of bases.
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
