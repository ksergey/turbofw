// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <tuple>
#include <type_traits>

#include "Integral.h"

namespace turbofw::mp {

/// Returns the N::value-th element of L
template <typename L, typename N>
struct At;
template <template <typename...> typename L, typename... Ts, typename N>
struct At<L<Ts...>, N> : std::tuple_element<N::value, std::tuple<Ts...>> {};

/// Returns the index at which the type V is located in the list L or Size<L> on not found
template <typename L, typename V>
struct Find;
template <template <typename...> typename L, typename... Ts, typename V>
struct Find<L<Ts...>, V> : SizeT<[]() -> std::size_t {
    constexpr auto match = std::array{std::is_same<V, Ts>::value...};
    std::size_t index = 0;
    for (; index < sizeof...(Ts); ++index) {
        if (match[index]) {
            break;
        }
    }
    return index;
}()> {};

template <typename L, template <typename...> typename P>
struct FindIf;
template <template <typename...> typename L, typename... Ts, template <typename...> typename P>
struct FindIf<L<Ts...>, P> : SizeT<[]() -> std::size_t {
    constexpr auto match = std::array{ToBool<P<Ts>>::value...};
    std::size_t index = 0;
    for (; index < sizeof...(Ts); ++index) {
        if (match[index]) {
            break;
        }
    }
    return index;
}()> {};

/// FindIf with quoted metafunction
template <typename L, typename Q>
using FindIf_q = FindIf<L, Q::template fn>;

} // namespace turbofw::mp
