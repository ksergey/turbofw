// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <tuple>
#include <type_traits>

#include "Integral.h"

namespace turbofw::mp {

/// Returns the `N::value`-th element of L (`N` is an integral constant type, see `SizeT`; `At_c` is the sugar
/// for a plain `std::size_t` index). The element is exposed as the nested `type`.
///
/// An out-of-range index leaves the primary template undefined and the error comes from `std::tuple_element`
/// ("tuple index out of range" in the standard library diagnostics). A hand-written bounds assert only moves
/// the same failure a few lines up the backtrace.
template <typename L, typename N>
struct At;
template <template <typename...> typename L, typename... Ts, typename N>
struct At<L<Ts...>, N> : std::tuple_element<N::value, std::tuple<Ts...>> {};

/// Returns the `I`-th element of L as a type (sugar over `At` with a plain compile-time index)
template <typename L, std::size_t I>
using At_c = typename At<L, SizeT<I>>::type;

/// Returns the index of the first occurrence of V in L as a `SizeT` constant (`-> ::value`); a type absent
/// from L yields `Size<L>` -- the mp-convention for "not found"
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

/// Returns the index of the first element of L matching P as a `SizeT` constant (`-> ::value`); a list without
/// a match yields `Size<L>` -- the mp-convention for "not found".
///
/// The predicate must be well-formed for every element of L: `ToBool<P<Ts>>...` instantiates the whole match
/// array at once, unlike mp11's lazy `mp_defer`, which tolerates predicates invalid on some elements.
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

/// FindIf with a quoted metafunction: Q must define `template <typename> using fn`, a predicate over the
/// element type
template <typename L, typename Q>
using FindIf_q = FindIf<L, Q::template fn>;

} // namespace turbofw::mp
