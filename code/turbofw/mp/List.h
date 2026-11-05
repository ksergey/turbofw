// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <type_traits>

#include "Integral.h"

namespace turbofw::mp {

/// Type list for meta programming
template <typename...>
struct List {};

/// IsList<L> is std::true_type if L is a list (an instantiation of a class template whose template parameters are
/// types), std::false_type otherwise.
template <typename L>
struct IsList : std::false_type {};

template <template <typename...> typename L, typename... Ts>
struct IsList<L<Ts...>> : std::true_type {};

/// Returns the number of elements in the list L
template <typename L>
struct Size;
template <template <typename...> typename L, typename... Ts>
struct Size<L<Ts...>> : SizeT<sizeof...(Ts)> {};

namespace detail {

template <typename L, template <typename...> typename Y>
struct RenameImpl;
template <template <typename...> typename L, typename... Ts, template <typename...> typename Y>
struct RenameImpl<L<Ts...>, Y> {
    using type = Y<Ts...>;
};

} // namespace detail

/// Changes the type of the list L to Y
template <typename L, template <typename...> typename Y>
using Rename = typename detail::RenameImpl<L, Y>::type;

} // namespace turbofw::mp
