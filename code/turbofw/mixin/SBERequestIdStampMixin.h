// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <limits>

#include "detail/Random.h"
#include <turbofw/SBE.h>
#include <turbofw/Types.h>

namespace turbofw::mixin {

/// Stamps a fresh random `requestId` into every outgoing message carrying the field (compile-time detection), so
/// responses can be matched back per request. Composes with \ref SBESendMixin on a producer.
///
/// The ids come from a splitmix64 PRNG seeded at construction from `std::random_device` and the monotonic clock:
/// they differ across processes and restarts, and the sequence never produces the null sentinel (`0xFFFF...FF`,
/// the unsolicited/spontaneous marker; `0` is a valid id). Pure arithmetic on the send path -- no syscalls, no
/// allocations. For request senders only: a response echoes the ids of its request, so a service must not compose
/// this mixin. Single-threaded, like any turboq producer.
class SBERequestIdStampMixin {
public:
    SBERequestIdStampMixin() noexcept : rng_{detail::randomSeed()} {}

    /// \ref SBESendMixin hook: stamps a fresh random requestId into every message carrying the field
    template <typename M>
    auto updateBody([[maybe_unused]] M msg) noexcept -> void {
        if constexpr (SBEHasField<M, "requestId">) {
            msg.template get<"requestId">().value(nextRequestId());
        }
    }

private:
    [[nodiscard]] auto nextRequestId() noexcept -> std::uint64_t {
        auto id = rng_.next();
        while (id == std::numeric_limits<std::uint64_t>::max()) { // the null sentinel: unsolicited/spontaneous
            id = rng_.next();
        }
        return id;
    }

    detail::SplitMix64 rng_;
};

} // namespace turbofw::mixin
