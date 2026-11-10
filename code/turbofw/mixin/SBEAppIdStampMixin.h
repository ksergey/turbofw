// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <limits>

#include "detail/Random.h"
#include <turbofw/SBE.h>
#include <turbofw/Types.h>

namespace turbofw::mixin {

/// Stamps \ref AppId into outgoing request messages: every message sent through the producer that carries an `appId`
/// field gets the mixin's value. Composes with \ref SBESendMixin on a producer:
///
/// \code{.cpp}
/// struct RequestProducer : public turboq::MPSCMessageQueue::Producer, public turbofw::mixin::SBESendMixin,
///                          public turbofw::mixin::SBEAppIdStampMixin {
///     using Mixins = turbofw::mp::List<turbofw::mixin::SBEAppIdStampMixin>;
/// };
/// \endcode
///
/// **Value**: generated randomly at construction (`0xFFFFFFFF` -- the null sentinel -- is excluded); `setAppId`
/// pins a fixed one. Per the conventions, an appId is a random 32-bit id chosen by the client and is reused for
/// every request it sends; `appId()` reports the value in effect.
///
/// **Field detection** is compile-time: a message without an `appId` field (an `svc::Heartbeat`, for example) is
/// not touched. Optional fields (`AppIdNULL`) are stamped like required ones.
///
/// **Request senders only**: a response echoes the `(appId, requestId)` of its request -- a service must not
/// compose this mixin, or the echoed ids would be clobbered.
///
/// **Single-threaded**: like any turboq producer, a composed producer is used from one thread at a time.
class SBEAppIdStampMixin {
public:
    /// A fresh random appId
    SBEAppIdStampMixin() noexcept : appId_{randomAppId()} {}

    /// Pins the given appId
    explicit SBEAppIdStampMixin(AppId appId) noexcept : appId_{appId} {}

    /// Changes the appId stamped into subsequent sends
    auto setAppId(AppId appId) noexcept -> void {
        appId_ = appId;
    }

    /// The appId stamped into outgoing messages
    [[nodiscard]] auto appId() const noexcept -> AppId {
        return appId_;
    }

    /// \ref SBESendMixin hook: stamps the mixin's appId into every message carrying the field
    template <typename M>
    auto updateBody([[maybe_unused]] M msg) noexcept -> void {
        if constexpr (SBEHasField<M, "appId">) {
            msg.template get<"appId">().value(appId_.value_of());
        }
    }

private:
    [[nodiscard]] static auto randomAppId() noexcept -> AppId {
        auto rng = detail::SplitMix64{detail::randomSeed()};
        auto id = static_cast<std::uint32_t>(rng.next() >> 32);
        while (id == std::numeric_limits<std::uint32_t>::max()) { // the null sentinel
            id = static_cast<std::uint32_t>(rng.next() >> 32);
        }
        return AppId{id};
    }

    AppId appId_;
};

} // namespace turbofw::mixin
