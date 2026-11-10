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

/// Adds an SBE-aware `send` operation to a producer that provides `prepare()` and `commit()`: reserves a buffer
/// of exactly the right size, wraps the SBE MessageHeader into it, runs the `Self::Mixins` hooks, and passes the
/// ready-to-fill message object to the callback. Messages with variable-size parts take a `M::SizeHint` hint
/// (an entry count per repeating group, a byte length per data field); a hint smaller than what the callback
/// encodes makes the encode throw -- a fatal encoding bug, not a recoverable failure.
///
/// The callback receives the message by copy: SBE message objects are lightweight non-owning views over the
/// queue buffer, and only the copy's position cursor moves -- what the callback writes is what gets published.
/// References and const objects are not passing options: the generated getters are non-const members.
///
/// If no buffer is available, returns `ENOBUFS` without touching the callback; for an mpsc producer `prepare()`
/// itself throws when the computed size exceeds the slot size -- a fatal misconfiguration, don't catch it. For
/// a multicast producer `prepare()` never fails, the ring simply wraps around. The callback follows the
/// hot-path exception rule: it may throw only to signal an unrecoverable, fatal error -- there is no rollback
/// on the exception path, and for an mpsc queue an uncommitted slot stalls its consumer for good.
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

        // Mixin hooks: stamp common fields before the user callback has its say, in `Self::Mixins` list order
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
