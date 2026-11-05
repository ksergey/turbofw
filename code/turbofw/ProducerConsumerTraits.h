// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <turboq/Concepts.h>

#include <turbofw/mp/Utility.h>

namespace turbofw {

/// Create producer with mixins in L
template <typename P, typename L>
    requires turboq::Producer<P>
struct MakeProducer : P, mp::Inherit<L> {
    using Mixins = L;

    using P::P;
};

/// Create consumer with mixins in L
template <typename C, typename L>
    requires turboq::Consumer<C>
struct MakeConsumer : C, mp::Inherit<L> {
    using Mixins = L;

    using C::C;
};

} // namespace turbofw
