// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <limits>

#include "detail/Random.h"
#include <turbofw/SBE.h>
#include <turbofw/Types.h>

namespace turbofw::mixin {

/// Stamps the mixin's `appId` into every outgoing message carrying the field (compile-time detection; optional
/// `AppIdNULL` fields included). Composes with \ref SBESendMixin on a producer.
///
/// The value is generated randomly at construction (the `0xFFFFFFFF` null sentinel is excluded) and reused for
/// every request; `setAppId` pins a fixed one, `appId()` reports the value in effect. For request senders only:
/// a response echoes the ids of its request, so a service must not compose this mixin. Single-threaded, like any
/// turboq producer.
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
