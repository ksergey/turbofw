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

template <typename L>
struct FrontImpl;
template <template <typename...> typename L, typename T, typename... Ts>
struct FrontImpl<L<T, Ts...>> {
    using type = T;
};

} // namespace detail

/// First element of the list \c L
template <typename L>
using Front = typename detail::FrontImpl<L>::type;

namespace detail {

template <typename L>
struct PopFrontImpl;
template <template <typename...> typename L, typename T, typename... Ts>
struct PopFrontImpl<L<T, Ts...>> {
    using type = L<Ts...>;
};

} // namespace detail

/// Removes the first element of the list \c L
template <typename L>
using PopFront = typename detail::PopFrontImpl<L>::type;

/// Another name for Front
template <typename L>
using First = Front<L>;

/// Another name for PopFront
template <typename L>
using Rest = PopFront<L>;

namespace detail {

template <typename L, typename... Ts>
struct PushFrontImpl;
template <template <typename...> typename L, typename... Us, typename... Ts>
struct PushFrontImpl<L<Us...>, Ts...> {
    using type = L<Ts..., Us...>;
};

} // namespace detail

/// Inserts the elements Ts... at the front of the list L
template <typename L, typename... Ts>
using PushFront = typename detail::PushFrontImpl<L, Ts...>::type;

namespace detail {

template <typename L, typename... Ts>
struct PushBackImpl;
template <template <typename...> typename L, typename... Us, typename... Ts>
struct PushBackImpl<L<Us...>, Ts...> {
    using type = L<Us..., Ts...>;
};

} // namespace detail

/// Inserts the elements Ts... at the back of the list L
template <typename L, typename... Ts>
using PushBack = typename detail::PushBackImpl<L, Ts...>::type;

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

namespace detail {

template <typename L>
struct ClearImpl;
template <template <typename...> typename L, typename... Ts>
struct ClearImpl<L<Ts...>> {
    using type = L<>;
};
} // namespace detail

/// Removes all elements of L
template <typename L>
using Clear = typename detail::ClearImpl<L>::type;

} // namespace turbofw::mp
