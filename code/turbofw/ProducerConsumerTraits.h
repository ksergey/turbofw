// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <turboq/Concepts.h>

#include <turbofw/mp/Utility.h>

namespace turbofw {

template <typename...>
struct Mixins;

/// Create producer with mixins in L
template <typename P, typename L>
    requires turboq::Producer<P>
struct MakeProducer : P, mp::Inherit<L> {
    using Mixins = L;

    MakeProducer(MakeProducer&&) = default;
    MakeProducer& operator=(MakeProducer&&) = default;

    MakeProducer(P&& p) noexcept : P{std::move(p)} {}
};

/// Create consumer with mixins in L
template <typename C, typename L>
    requires turboq::Consumer<C>
struct MakeConsumer : C, mp::Inherit<L> {
    using Mixins = L;

    MakeConsumer(MakeConsumer&&) = default;
    MakeConsumer& operator=(MakeConsumer&&) = default;

    MakeConsumer(C&& c) noexcept : C{std::move(c)} {}
};

} // namespace turbofw
