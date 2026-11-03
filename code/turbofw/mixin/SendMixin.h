// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cerrno>
#include <cstddef>
#include <expected>
#include <functional>

#include <turboq/Concepts.h>

#include <turbofw/Error.h>

namespace turbofw::mixin {

/// Adds a `send` operation to a producer that provides `prepare()` and `commit()`.
///
/// Reserves a buffer, passes it to the callback, and publishes it by committing the full prepared size.
/// If no buffer is available, returns `ENOBUFS` without touching the callback. Note: for an mpsc producer
/// `prepare()` itself may throw when the payload exceeds the slot size -- don't catch that either, it is a fatal
/// misconfiguration, not an ordinary failure.
///
/// The callback follows the hot-path exception rule (see AGENTS.md): it may throw only to signal an unrecoverable,
/// fatal error. There is no rollback on the exception path: the prepared buffer is left uncommitted, and for an mpsc
/// queue an uncommitted slot stalls its consumer for good.
struct SendMixin {
    template <typename Self, typename Fn>
        requires turboq::Producer<std::remove_cvref_t<Self>>
    auto send(this Self&& self, std::size_t bufferSize, Fn&& fn) -> std::expected<void, std::error_code> {
        auto buffer = self.prepare(bufferSize);
        if (buffer.empty()) [[unlikely]] {
            return std::unexpected{makePosixErrorCode(ENOBUFS)};
        }

        std::invoke(std::forward<Fn>(fn), buffer);

        self.commit();

        return {};
    }
};

} // namespace turbofw::mixin
