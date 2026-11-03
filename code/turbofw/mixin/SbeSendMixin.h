// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <span>
#include <tuple>
#include <type_traits>

#include <turboq/Concepts.h>

#include <turbofw/Error.h>

namespace turbofw::mixin {

/// Checks M is a generated SBE message type: it can apply an SBE MessageHeader to a buffer and compute the buffer
/// size it needs, given the per-message SizeHint (one entry per repeating group / var-length data field)
template <typename M>
concept SbeMessage = requires(std::span<std::byte> buffer, typename M::SizeHint sizeHint) {
    M::wrapAndApplyHeader(buffer);
    M::sbeComputeSize(sizeHint);
    M::sbeComputeSize();
};

/// Checks the message has variable-size parts (repeating groups or var-length data fields): its SizeHint is not
/// empty, so the sizes of those parts must be passed to the send operation
template <typename M>
concept SbeMessageWithVars = requires { typename M::SizeHint; } && (std::tuple_size_v<typename M::SizeHint> != 0);

/// Adds an SBE-aware `send` operation to a producer that provides `prepare()` and `commit()`.
///
/// The mixin reserves a buffer of exactly the right size, wraps the SBE MessageHeader into it, and passes the
/// ready-to-fill message object to the callback:
///
/// \code{.cpp}
/// // no variable-size parts: no sizes argument
/// producer.send<svc::Heartbeat>([](svc::Heartbeat& msg) {
///     msg.get<"startTs">().value(startTs);
/// });
///
/// // repeating groups / data fields: their sizes first, then the callback
/// producer.send<md::Snapshot>(std::tuple{bids, asks, stats}, [](md::Snapshot& msg) {
///     auto bids = msg.get<"bids">();
///     bids.reset(2);
///     ...
/// });
/// \endcode
///
/// The message's group entries and var-data fields must be sized in `M::SizeHint` units: an entry count per
/// repeating group, a byte length per data field (see the generated `sbeComputeSize()`). Passing a hint smaller
/// than what the callback actually encodes makes the encode throw: per the hot-path exception rule (see
/// AGENTS.md), that is a fatal encoding bug, not a recoverable failure.
///
/// If no buffer is available, returns `ENOBUFS` without touching the callback. Note that for an mpsc producer
/// `prepare()` itself throws when the computed size exceeds the slot size -- don't catch that either, it is a
/// fatal misconfiguration. For a multicast producer `prepare()` never fails, the ring simply wraps around.
/// The callback follows the hot-path exception rule: it may throw only to signal an unrecoverable, fatal error.
/// There is no rollback on the exception path: the prepared buffer is left uncommitted, and for an mpsc queue an
/// uncommitted slot stalls its consumer for good.
struct SbeSendMixin {
    template <SbeMessage Message, typename Self, typename Fn>
        requires SbeMessageWithVars<Message> && turboq::Producer<std::remove_cvref_t<Self>> &&
                     std::invocable<Fn&, Message&>
    auto send(this Self&& self, typename Message::SizeHint hint, Fn&& fn) -> std::expected<void, std::error_code> {
        auto const bufferSize = Message::Header::sbeEncodedLength() + Message::sbeComputeSize(hint);
        auto const buffer = self.prepare(bufferSize);
        if (buffer.empty()) [[unlikely]] {
            return std::unexpected{makePosixErrorCode(ENOBUFS)};
        }

        auto msg = Message::wrapAndApplyHeader(buffer);
        std::invoke(std::forward<Fn>(fn), msg);
        self.commit();

        return {};
    }

    template <SbeMessage Message, typename Self, typename Fn>
        requires(!SbeMessageWithVars<Message>) && turboq::Producer<std::remove_cvref_t<Self>> &&
                    std::invocable<Fn&, Message&>
    auto send(this Self&& self, Fn&& fn) -> std::expected<void, std::error_code> {
        constexpr auto bufferSize = Message::Header::sbeEncodedLength() + Message::sbeComputeSize();
        auto const buffer = self.prepare(bufferSize);
        if (buffer.empty()) [[unlikely]] {
            return std::unexpected{makePosixErrorCode(ENOBUFS)};
        }

        auto msg = Message::wrapAndApplyHeader(buffer);
        std::invoke(std::forward<Fn>(fn), msg);
        self.commit();

        return {};
    }
};

} // namespace turbofw::mixin
