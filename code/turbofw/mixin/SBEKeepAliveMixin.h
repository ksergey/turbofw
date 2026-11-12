// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <type_traits>

#include <turboq/Concepts.h>

#include <turbofw/Clock.h>
#include <turbofw/schema/service/schema.h>

namespace turbofw::mixin {

/// A self-check that `Self` can publish `turbofw::schema::service::Heartbeat`: a producer composed with \ref
/// SBESendMixin. `SBEKeepAliveMixin` methods mention the constraint in their docs; a producer without the send mixin
/// fails to compile here, not inside.
template <typename Self>
concept HeartbeatSender = turboq::Producer<Self> && requires(Self self) {
    self.template send<turbofw::schema::service::Heartbeat>([](turbofw::schema::service::Heartbeat msg) {
        msg.get<"ts">().value(0);
    });
};

/// Publishes `turbofw::schema::service::Heartbeat` on a producer so a consumer never sees the service go silent on the
/// wire. Composes with \ref SBESendMixin; a producer without it fails to compile in the \ref HeartbeatSender concept.
///
/// `startHeartbeating` publishes a Start heartbeat and enables periodic publication; `stopHeartbeating` publishes
/// a Stop heartbeat and disables it; `keepAlive` publishes a heartbeat once the silence interval has elapsed. Every
/// successful send of any message through the same producer (the \ref SBESendMixin `updateBody` hook, the mixin's
/// own heartbeats included) arms a deadline as "publication moment + interval" (`turbofw::realtimeNanos()`).
///
/// The interval is 1 second by default, settable at `startHeartbeating` or with `setHeartbeatInterval`; a changed
/// interval applies to the deadlines armed after it, the setter alone does not re-arm the current one. A failed
/// send (an mpsc queue returns ENOBUFS when full; a multicast queue never fails) is swallowed and does not re-arm
/// the deadline, so the next `keepAlive` retries. The mixin writes `ts` and owns `flags` wholesale: empty for
/// periodic heartbeats, the `Start`/`Stop` bit for event ones (no read-modify-write -- queue slots are reused
/// without zeroing). `serviceId` is not written. Single-threaded; no clock reads while heartbeating is disabled.
class SBEKeepAliveMixin {
private:
    using ServiceFlags = turbofw::schema::service::ServiceFlags;

    std::chrono::nanoseconds interval_{std::chrono::seconds{1}};
    std::uint64_t nextHearbeatSendTime_{0};
    bool enabled_{false};

public:
    auto setHeartbeatInterval(std::chrono::nanoseconds value) noexcept -> void {
        interval_ = value;
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
        self.sendHeartbeat(ServiceFlags{}.start(true));
    }

    /// Publishes a Heartbeat with the `Stop` flag and disables periodic publication: \ref keepAlive does nothing until
    /// the next \ref startHeartbeating. Idempotent: a repeated call (or a call before any start) is a no-op.
    template <typename Self>
        requires HeartbeatSender<std::remove_cvref_t<Self>>
    auto stopHeartbeating(this Self&& self) -> void {
        if (!self.enabled_) {
            return;
        }
        self.sendHeartbeat(ServiceFlags{}.stop(true));
        self.nextHearbeatSendTime_ = 0;
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
        auto const now = realtimeNanos();
        if (self.nextHearbeatSendTime_ > now) {
            return;
        }
        self.sendHeartbeat(ServiceFlags{}, now);
    }

    /// \ref SBESendMixin hook: sends of any message count as activity and restart the silence timer. No clock reads at
    /// all while heartbeating is disabled.
    template <typename M>
    auto updateBody([[maybe_unused]] M msg) noexcept -> void {
        if (!enabled_) [[unlikely]] {
            return;
        }
        nextHearbeatSendTime_ = realtimeNanos() + interval_.count();
    }

private:
    template <typename Self>
        requires HeartbeatSender<std::remove_cvref_t<Self>>
    auto sendHeartbeat(this Self&& self, ServiceFlags flags, std::uint64_t timestamp = realtimeNanos()) -> void {
        (void)self.template send<turbofw::schema::service::Heartbeat>([&](turbofw::schema::service::Heartbeat msg) {
            msg.get<"flags">().value(flags);
            msg.get<"ts">().value(timestamp);
        });
    }
};

} // namespace turbofw::mixin
