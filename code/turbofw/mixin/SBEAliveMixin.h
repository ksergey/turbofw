// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstdint>
#include <span>

#include <turbofw/Clock.h>
#include <turbofw/svc/schema.h>

namespace turbofw::mixin {

/// Tracks whether the service behind a received queue is alive. Composes with \ref SBEReceiveMixin on a consumer and
/// answers \ref isAlive from the receive side: `true` while messages keep arriving within the configured interval,
/// `false` as soon as the service went silent (and, on demand, when it published a Stop heartbeat).
///
/// \code{.cpp}
/// struct Consumer : public turboq::MPSCMessageQueue::Consumer, public turbofw::mixin::ReceiveMixin,
///                   public turbofw::mixin::SBEReceiveMixin, public turbofw::mixin::SBEAliveMixin {
///     using Mixins = turbofw::mp::List<turbofw::mixin::SBEAliveMixin>;
/// };
///
/// consumer.receive<mp::List<md::Message, svc::Heartbeat>>([](auto msg) { ... });
/// if (!consumer.isAlive()) { ... resynchronize ... }
/// \endcode
///
/// **Activity** is every buffer with a readable MessageHeader, regardless of whether \ref SBEReceiveMixin's decode
/// list handles it (messages of the second schema of the queue included): the hook runs before decoding, and the
/// last message's receive time wins. A Heartbeat with the `Stop` flag additionally latches the mixin into the
/// stopped state -- \ref isAlive reports `false` from that moment, even while the interval has not elapsed. Any
/// later message (a Start heartbeat, or any other activity of a restarted service) clears the latch again: the
/// latest observation wins.
///
/// **Interval**: 1 second by default; `setAliveInterval()` changes it at any time. \ref isAlive is `false` until
/// the very first message of the queue is received.
///
/// **Single-threaded**: the state is plain data; call \ref isAlive from the same thread as `receive`.
///
/// **Hot-path note**: the hook reads `turbofw::monotonicNanos()` (vDSO) once per received message to stamp the
/// silence timer, and decodes a Heartbeat body only when the header matches -- otherwise it is a single
/// (schemaId, templateId) compare.
class SBEAliveMixin {
private:
    std::chrono::nanoseconds interval_{std::chrono::seconds{1}};
    std::uint64_t nextMessageDeadline_{0};

public:
    /// Changes the silence interval: a service is alive while its last message is younger than this
    auto setAliveInterval(std::chrono::nanoseconds value) noexcept -> void {
        interval_ = value;
    }

    /// `true` if the service is alive: some message arrived within the configured interval, and no Stop heartbeat
    /// latched the mixin into the stopped state. `false` before the first received message
    [[nodiscard]] auto isAlive() const noexcept -> bool {
        return monotonicNanos() < nextMessageDeadline_;
    }

    /// \ref SBEReceiveMixin hook: runs for every buffer with a readable MessageHeader, before the decode list gets
    /// its say. Stamps the silence timer, clears a pending Stop latch, and latches it again on a Stop heartbeat
    /// (decoded here from the buffer, guarded against a truncated body)
    template <typename Header>
    auto processMessage(Header header, std::span<std::byte const> buffer) noexcept -> void {
        nextMessageDeadline_ = monotonicNanos() + interval_.count();

        if (header.template get<"schemaId">().value() != svc::Heartbeat::sbeSchemaId() ||
            header.template get<"templateId">().value() != svc::Heartbeat::sbeTemplateId()) {
            return;
        }
        auto const blockLength = header.template get<"blockLength">().value();
        if (buffer.size() < Header::sbeEncodedLength() + blockLength) [[unlikely]] {
            return; // a truncated body: leave the latch cleared, the decode step reports it (fatally) instead
        }
        auto const mutableBuffer = std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()};
        auto msg = svc::Heartbeat{
            mutableBuffer, Header::sbeEncodedLength(), blockLength, header.template get<"version">().value()};
        if (msg.get<"flags">().value().stop()) [[unlikely]] {
            nextMessageDeadline_ = 0;
        }
    }
};

} // namespace turbofw::mixin
