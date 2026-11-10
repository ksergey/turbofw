// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstdint>
#include <span>

#include <turbofw/Clock.h>
#include <turbofw/svc/schema.h>

namespace turbofw::mixin {

/// Tracks whether the service behind a received queue is alive. Composes with \ref SBEReceiveMixin on a consumer.
///
/// Every received message arms a deadline as "message moment + interval" (`turbofw::monotonicNanos()`); a Heartbeat
/// carrying the `Stop` flag zeroes the deadline instead. \ref isAlive reports `true` while the deadline is in the
/// future, so a silent service reads as dead once the interval has passed. The interval is 1 second by default;
/// `setAliveInterval` changes it for the deadlines armed after the change. The hook pays one clock read per
/// received message and decodes a Heartbeat body only when the header matches. Single-threaded: the state is plain
/// data, call \ref isAlive from the same thread as `receive`.
class SBEAliveMixin {
private:
    std::chrono::nanoseconds interval_{std::chrono::seconds{1}};
    std::uint64_t nextMessageDeadline_{0};

public:
    /// Changes the silence interval: a service is alive while its last message is younger than this
    auto setAliveInterval(std::chrono::nanoseconds value) noexcept -> void {
        interval_ = value;
    }

    /// `true` while the deadline armed by the last received message is in the future; `false` before the first
    /// message and after a Stop heartbeat
    [[nodiscard]] auto isAlive() const noexcept -> bool {
        return monotonicNanos() < nextMessageDeadline_;
    }

    /// \ref SBEReceiveMixin hook: arms the deadline for every buffer with a readable MessageHeader and zeroes it on
    /// a Heartbeat carrying the Stop flag (decoded from the buffer, guarded against a truncated body)
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
