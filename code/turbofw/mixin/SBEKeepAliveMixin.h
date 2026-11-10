// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <type_traits>

#include <turboq/Concepts.h>

#include <turbofw/Clock.h>
#include <turbofw/svc/schema.h>

namespace turbofw::mixin {

/// A self-check that `Self` can publish `svc::Heartbeat`: a producer composed with \ref SBESendMixin.
/// `SBEKeepAliveMixin` methods mention the constraint in their docs; a producer without the send mixin fails to compile
/// here, not inside.
template <typename Self>
concept HeartbeatSender = turboq::Producer<Self> && requires(Self self) {
    self.template send<svc::Heartbeat>([](svc::Heartbeat msg) {
        msg.get<"timestamp">().value(0);
    });
};

/// Publishes `svc::Heartbeat` on a service producer, so a consumer never sees the service go silent onto the wire.
///
/// The mixin composes with \ref SBESendMixin on a producer:
///
/// \code{.cpp}
/// struct Producer : public turboq::MulticastMessageQueue::Producer, public turbofw::mixin::SBESendMixin,
///                   public turbofw::mixin::SBEKeepAliveMixin {
///     using Mixins = turbofw::mp::List<turbofw::mixin::SBEKeepAliveMixin>;
/// };
///
/// producer.startHeartbeating(std::chrono::seconds{1});  // Start heartbeat + after that once interval on the silence
/// ...                                                   // other service publications during the loop
/// while (running) {
///     // ... service work ...
///     producer.keepAlive();  // publishes a heartbeat once the interval has elapsed
/// }
/// producer.stopHeartbeating();  // Stop heartbeat and stops periodic publication
/// \endcode
///
/// **Publication detection** is through the \ref SBESendMixin `updateBody` hook: every successful send of any message
/// through the same producer stamps the silence timer. The heartbeats the mixin itself publishes pass through the same
/// `send<>()`, so its own `updateBody` invocation resets the silence timer too; no special treatment.
///
/// **Interval**: 1 second by default; set at 'startHeartbeating' or with 'setHeartbeatInterval' at any time. The
/// silence timer uses `turbofw::monotonicNanos()`.
///
/// **Field stamping**: the mixin sets only `timestamp` (wall-clock at publication). It leaves `flags` alone for
/// periodic (keepAlive) heartbeats -- state flags (NotConnected, Recovering) are repeated by the service, typically
/// through a stamping hook earlier in the `Mixins` list, and this mixin reads and rewrites `flags` only to add the
/// `Start`/`Stop` bit of its own event heartbeats. `serviceId` stays null until a dedicated stamping mixin (not yet
/// written) fills it.
///
/// **Single-threaded**: the silence state is plain data; like any turboq producer, a composed producer is used from
/// one thread at a time. The clock is read only while heartbeating is enabled: updateBody parks itself and keepAlive
/// returns immediately when stopped, so a stopped service does not pay for this mixin at all.
///
/// **Availability**: the interval under 'stopHeartbeating' at send (maxSize of multicast never fails; only mpsc
/// scenario ENOBUFS) is swallowed: keepAlive keeps lastPublished unchanged, so the next call retries.
struct SBEKeepAliveMixin {
public:
    auto setHeartbeatInterval(std::chrono::nanoseconds interval) noexcept -> void {
        intervalNs_ = interval.count();
    }

    /// Publishes a Heartbeat with the `Start` flag and enables periodic publication: subsequent calls of \ref keepAlive
    /// publish one heartbeat per interval of wire silence. Idempotent: a repeated call while heartbeating is enabled is
    /// a no-op (no second Start heartbeat). An optional `interval` overrides the current one; without it the interval
    /// set earlier (constructor default 1 second, or the last `setHeartbeatInterval`) stays.
    template <typename Self>
        requires HeartbeatSender<std::remove_cvref_t<Self>>
    auto startHeartbeating(this Self&& self, std::optional<std::chrono::nanoseconds> interval = std::nullopt) -> void {
        if (self.enabled_) {
            return;
        }
        if (interval) {
            self.setHeartbeatInterval(*interval);
        }
        self.enabled_ = true;
        self.lastPublishedNs_ = monotonicNanos();
        self.publishEventHeartbeat(true);
    }

    /// Publishes a Heartbeat with the `Stop` flag and disables periodic publication: \ref keepAlive does nothing until
    /// the next \ref startHeartbeating. Idempotent: a repeated call (or a call before any start) is a no-op.
    template <typename Self>
        requires HeartbeatSender<std::remove_cvref_t<Self>>
    auto stopHeartbeating(this Self&& self) -> void {
        if (!self.enabled_) {
            return;
        }
        self.lastPublishedNs_ = monotonicNanos();
        self.publishEventHeartbeat(false);
        self.enabled_ = false;
    }

    /// Publishes a heartbeat when heartbeating is enabled and no message has been sent through the producer for the
    /// configured interval; a no-op otherwise. Never fails visibly: the send result is discarded (see the class docs).
    template <typename Self>
        requires HeartbeatSender<std::remove_cvref_t<Self>>
    auto keepAlive(this Self&& self) -> void {
        if (!self.enabled_) {
            return;
        }
        auto const now = monotonicNanos();
        if (now - self.lastPublishedNs_ < static_cast<std::uint64_t>(self.intervalNs_)) {
            return;
        }
        (void)self.template send<svc::Heartbeat>([](svc::Heartbeat msg) {
            msg.get<"timestamp">().value(realtimeNanos());
        });
    }

    /// \ref SBESendMixin hook: sends of any message count as activity and restart the silence timer. No clock reads at
    /// all while heartbeating is disabled.
    template <typename M>
    auto updateBody([[maybe_unused]] M msg) noexcept -> void {
        if (!enabled_) {
            return;
        }
        lastPublishedNs_ = monotonicNanos();
    }

private:
    template <typename Self>
        requires HeartbeatSender<std::remove_cvref_t<Self>>
    auto publishEventHeartbeat(this Self&& self, bool start) -> void {
        (void)self.template send<svc::Heartbeat>([start](svc::Heartbeat msg) {
            // read-modify-write: preserve the state flags stamped by earlier hooks in the `Mixins` list
            auto flags = msg.get<"flags">().value();
            if (start) {
                flags.start(true);
            } else {
                flags.stop(true);
            }
            msg.get<"flags">().value(flags);
            msg.get<"timestamp">().value(realtimeNanos());
        });
    }

    std::int64_t intervalNs_{std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds{1}).count()};
    std::uint64_t lastPublishedNs_{0};
    bool enabled_{false};
};

} // namespace turbofw::mixin
