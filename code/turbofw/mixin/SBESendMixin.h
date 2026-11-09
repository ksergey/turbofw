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
#include <turbofw/SBE.h>

#include "Utility.h"

namespace turbofw::mixin {

/// Adds an SBE-aware `send` operation to a producer that provides `prepare()` and `commit()`.
///
/// The mixin reserves a buffer of exactly the right size, wraps the SBE MessageHeader into it, and passes the
/// ready-to-fill message object to the callback:
///
/// \code{.cpp}
/// // no variable-size parts: no sizes argument
/// producer.send<svc::Heartbeat>([](svc::Heartbeat msg) {
///     msg.get<"startTs">().value(startTs);
/// });
///
/// // repeating groups / data fields: their sizes first, then the callback
/// producer.send<md::Snapshot>(std::tuple{bids, asks, stats}, [](md::Snapshot msg) {
///     auto bids = msg.get<"bids">();
///     bids.reset(2);
///     ...
/// });
/// \endcode
///
/// The message's group entries and var-data fields must be sized in `M::SizeHint` units: an entry count per
/// repeating group, a byte length per data field (see the generated `sbeComputeSize()`). Passing a hint smaller
/// than what the callback actually encodes makes the encode throw: that is a fatal encoding bug, not a
/// recoverable failure -- per the hot-path exception rule, fixed size = fatal error, never a return value.
///
/// The callback receives the message by copy: SBE message objects are lightweight non-owning views (~40 bytes) over
/// the queue buffer, encoding through one copy mutates the underlying buffer exactly like any other, and only that
/// copy's position cursor moves -- so what the callback writes is what gets published either way, and writes never
/// escape the view. (References and const objects are not passing options: the generated getters are non-const
/// members, a const message view does not compile.)
///
/// If no buffer is available, returns `ENOBUFS` without touching the callback. Note that for an mpsc producer
/// `prepare()` itself throws when the computed size exceeds the slot size -- don't catch that either, it is a
/// fatal misconfiguration. For a multicast producer `prepare()` never fails, the ring simply wraps around.
/// The callback follows the hot-path exception rule: it may throw only to signal an unrecoverable, fatal error.
/// There is no rollback on the exception path: the prepared buffer is left uncommitted, and for an mpsc queue an
/// uncommitted slot stalls its consumer for good.
struct SBESendMixin {
    template <typename M, typename Self, typename Fn>
        requires SBEMessage<M> && SBEMessageWithVars<M> && turboq::Producer<std::remove_cvref_t<Self>> &&
                 std::invocable<Fn&, M&>
    auto send(this Self&& self, typename M::SizeHint hint, Fn&& fn) -> std::expected<void, std::error_code> {
        auto const bufferSize = M::Header::sbeEncodedLength() + M::sbeComputeSize(hint);
        auto const buffer = self.prepare(bufferSize);
        if (buffer.empty()) [[unlikely]] {
            return std::unexpected{makePosixErrorCode(ENOBUFS)};
        }

        auto msg = M::wrapAndApplyHeader(buffer);

        // Mixin hooks: stamp common fields into the message buffer before the user callback has its say.
        // Invoked in `Self::Mixins` list order, skipped for mixins without the method (see forEachMixin).
        // Messages pass by value (SBE message objects are lightweight views), so hooks run one after another
        // with independent position cursors over the same buffer.
        forEachMixin<Self>([&]<typename Mixin>(std::type_identity<Mixin>) {
            if constexpr (requires { self.Mixin::updateBody(msg); }) {
                self.Mixin::updateBody(msg);
            }
        });

        std::invoke(std::forward<Fn>(fn), msg);
        self.commit();

        return {};
    }

    template <typename M, typename Self, typename Fn>
        requires SBEMessage<M> && (!SBEMessageWithVars<M>) && turboq::Producer<std::remove_cvref_t<Self>> &&
                 std::invocable<Fn&, M&>
    auto send(this Self&& self, Fn&& fn) -> std::expected<void, std::error_code> {
        constexpr auto bufferSize = M::Header::sbeEncodedLength() + M::sbeComputeSize();
        auto const buffer = self.prepare(bufferSize);
        if (buffer.empty()) [[unlikely]] {
            return std::unexpected{makePosixErrorCode(ENOBUFS)};
        }

        auto msg = M::wrapAndApplyHeader(buffer);

        // Mixin hooks: see the send overload above
        forEachMixin<Self>([&]<typename Mixin>(std::type_identity<Mixin>) {
            if constexpr (requires { self.Mixin::updateBody(msg); }) {
                self.Mixin::updateBody(msg);
            }
        });

        std::invoke(std::forward<Fn>(fn), msg);
        self.commit();

        return {};
    }
};

} // namespace turbofw::mixin
