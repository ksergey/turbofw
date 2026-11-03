// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <expected>
#include <functional>

#include <turboq/Concepts.h>

#include <turbofw/Error.h>

namespace turbofw::mixin {

/// Adds a `send` operation to a producer that provides `prepare()` and `commit()`.
/// It prepares a buffer, passes it to the callback, and commits it if the callback returns normally.
/// If no buffer is available, it returns an error. Exceptions from the callback propagate,
/// and the prepared buffer is not committed.
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
