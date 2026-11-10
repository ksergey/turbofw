// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <limits>

#include "detail/Random.h"
#include <turbofw/SBE.h>
#include <turbofw/Types.h>

namespace turbofw::mixin {

/// Stamps a fresh random \ref RequestId into every outgoing request message: each message sent through the producer
/// that carries a `requestId` field gets its own id, so responses can be matched back per request. Composes with
/// \ref SBESendMixin on a producer:
///
/// \code{.cpp}
/// struct RequestProducer : public turboq::MPSCMessageQueue::Producer, public turbofw::mixin::SBESendMixin,
///                          public turbofw::mixin::SBERequestIdStampMixin {
///     using Mixins = turbofw::mp::List<turbofw::mixin::SBERequestIdStampMixin>;
/// };
/// \endcode
///
/// **Generation** is a splitmix64 PRNG seeded at construction from `std::random_device` and the monotonic clock:
/// ids differ across processes and restarts, and every draw differs from the previous one. The id sequence never
/// produces the null sentinel (`0xFFFF...FF`) -- per the conventions it marks unsolicited/spontaneous messages;
/// `0` is a valid request id. The PRNG is pure arithmetic: no syscalls, no allocations on the send path.
///
/// **Field detection** is compile-time: a message without a `requestId` field (an `svc::Heartbeat`, for example)
/// is not touched.
///
/// **Request senders only**: a response echoes the `(appId, requestId)` of its request -- a service must not
/// compose this mixin, or the echoed ids would be clobbered.
///
/// **Single-threaded**: like any turboq producer, a composed producer is used from one thread at a time.
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
