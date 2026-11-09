// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <tuple>
#include <type_traits>

#include "Integral.h"
#include "List.h"

namespace turbofw::mp {

namespace detail {

/// The exact-type-match predicate quoted the `fn`-way (as in `CountIf_q`/`FindIf_q`/`Transform_q`): the shared
/// engine behind the value-shaped `Count` and `Find` aliases
template <typename V>
struct SameAs {
    template <typename T>
    using fn = std::is_same<T, V>;
};

} // namespace detail

/// Returns SizeT<N>, where N is the number of elements T of L for which P<T> is true_type
template <typename L, template <typename...> typename P>
struct CountIf;
template <template <typename...> typename L, typename... Ts, template <typename...> typename P>
struct CountIf<L<Ts...>, P> : SizeT<(0u + ... + (ToBool<P<Ts>>::value ? 1u : 0u))> {};

/// CountIf with a quoted metafunction
template <typename L, typename Q>
using CountIf_q = CountIf<L, Q::template fn>;

/// Count = CountIf over the sameness predicate: SizeT<N>, where N is the number of elements of L same as V
template <typename L, typename V>
using Count = CountIf<L, detail::SameAs<V>::template fn>;

/// std::true_type when L contains an element V, std::false_type otherwise
template <typename L, typename V>
using Contains = ToBool<Count<L, V>>;

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

/// Find = FindIf over the sameness predicate: the index of the first occurrence of V in L as a `SizeT`
/// constant (`-> ::value`); a type absent from L yields `Size<L>` -- the mp-convention for "not found"
template <typename L, typename V>
using Find = FindIf<L, detail::SameAs<V>::template fn>;

namespace detail {

template <typename L>
struct UniqueImpl;
template <typename L>
    requires(Size<L>::value == 0)
struct UniqueImpl<L> {
    using type = Clear<L>;
};
template <typename L>
    requires(Size<L>::value > 0)
struct UniqueImpl<L> {
private:
    using T = First<L>;
    using RestList = typename UniqueImpl<Rest<L>>::type;

public:
    // clang-format off
    using type = std::conditional_t<
        Contains<RestList, T>::value,
        RestList,
        PushFront<RestList, T>
    >;
    // clang-format on
};

} // namespace detail

/// Returns a list of the same form as \c L with the duplicate elements removed
template <typename L>
using Unique = detail::UniqueImpl<L>::type;

/// mp_defer-style lazy application: delays the `F<Ts...>` instantiation into a struct of its own, so the
/// application happens at one well-defined point (the `::type`) and an invalid `F` surfaces with the failed
/// element, not deep inside a pack stretch. The engine of `Transform`; also usable on its own.
template <template <typename...> typename F, typename... Ts>
struct DeferImpl {
    using type = F<Ts...>;
};

/// The direct-result form of \ref DeferImpl: `Defer<F, Ts...>` is `F<Ts...>`, evaluated at one point
template <template <typename...> typename F, typename... Ts>
using Defer = typename DeferImpl<F, Ts...>::type;

namespace detail {

/// The mp11 mp_transform shape: an empty list short-circuits to itself (nothing instantiated), every other
/// list maps element-wise through F while the container form (`mp::List`, `std::tuple`, ...) is preserved.
template <template <typename...> typename F, typename L>
struct TransformImpl;

template <template <typename...> typename F, template <typename...> typename L>
struct TransformImpl<F, L<>> {
    using type = L<>;
};

template <template <typename...> typename F, template <typename...> typename L, typename... Ts>
struct TransformImpl<F, L<Ts...>> {
    using type = L<Defer<F, Ts>...>;
};

template <template <typename...> typename F, typename L>
using TransformImplAlias = typename TransformImpl<F, L>::type;

} // namespace detail

template <template <typename...> typename F, typename L>
using Transform = detail::TransformImplAlias<F, L>;

/// The quoted variant: Q must define `template <typename...> using fn`; it binds to the same dispatch point
/// as mp11's quoted transform (mp_transform_q over mp_transform)
template <typename Q, typename L>
using Transform_q = typename detail::TransformImpl<Q::template fn, L>::type;

} // namespace turbofw::mp
