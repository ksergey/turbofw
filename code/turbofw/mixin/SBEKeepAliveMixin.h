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

/// Publishes `svc::Heartbeat` on a service producer, so a consumer never sees the service go silent on the wire.
///
/// The mixin composes with \ref SBESendMixin on a producer:
///
/// \code{.cpp}
/// struct Producer : public turboq::MulticastMessageQueue::Producer, public turbofw::mixin::SBESendMixin,
///                   public turbofw::mixin::SBEKeepAliveMixin {
///     using Mixins = turbofw::mp::List<turbofw::mixin::SBEKeepAliveMixin>;
/// };
///
/// producer.startHeartbeating(std::chrono::seconds{1});  // Start heartbeat, then one heartbeat per interval of silence
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
/// **Interval**: 1 second by default; set at 'startHeartbeating' or with 'setHeartbeatInterval' at any time. Every
/// successful publication arms a deadline as "publication moment + interval" (`turbofw::realtimeNanos()`); the next
/// silence beat goes out once the deadline passes. A changed interval applies to the deadlines armed after it: the
/// setter alone does not re-arm the current one.
///
/// **Field stamping**: the mixin writes `timestamp` (wall-clock at publication) and owns `flags`: every heartbeat it
/// publishes carries exactly the flags it was asked for -- empty for periodic (keepAlive) ones, the `Start`/`Stop`
/// bit for event ones. `flags` is overwritten wholesale, not read-modify-write: queue slots are reused without
/// zeroing, so an OR would pick up stale bits of a previous message. As a consequence, state flags (NotConnected,
/// Recovering) cannot be stamped onto the mixin's heartbeats through an updateBody hook -- the mixin's callback runs
/// after all hooks. A service that repeats its state flags publishes those heartbeats itself, through
/// `send<svc::Heartbeat>`. `serviceId` is not written either: it stays whatever the reused slot holds (null on a
/// fresh region) until a dedicated stamping mixin (not yet written) fills it.
///
/// **Single-threaded**: the silence state is plain data; like any turboq producer, a composed producer is used from
/// one thread at a time. The clock is read only while heartbeating is enabled: updateBody parks itself and keepAlive
/// returns immediately when stopped, so a stopped service does not pay for this mixin at all.
///
/// **Availability**: a failed send is swallowed (an mpsc queue returns ENOBUFS when full; a multicast queue never
/// fails): the failed publication does not re-arm the deadline, so the next \ref keepAlive call retries.
class SBEKeepAliveMixin {
private:
    using ServiceFlags = svc::ServiceFlags;

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
        (void)self.template send<svc::Heartbeat>([&](svc::Heartbeat msg) {
            msg.get<"flags">().value(flags);
            msg.get<"timestamp">().value(timestamp);
        });
    }
};

} // namespace turbofw::mixin
