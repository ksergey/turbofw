// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <functional>

#include <turboq/Concepts.h>

namespace turbofw::mixin {

/// Adds a `receive` operation to a consumer that provides `fetch()` and `consume()`.
///
/// Drains the queue: repeatedly fetches the next buffer and passes it to the callback until the queue is empty.
/// Returns the number of processed buffers.
///
/// The callback follows the hot-path exception rule: it may throw only to signal an unrecoverable, fatal
/// error. An ordinary failure must not throw: the buffer is consumed when the callback returns normally --
/// there is no rollback on the exception path, and an unconsumed mpsc slot stalls the consumer for good.
struct ReceiveMixin {
    template <typename Self, typename Fn>
        requires turboq::Consumer<std::remove_cvref_t<Self>>
    auto receive(this Self&& self, Fn&& fn) -> std::size_t {
        std::size_t receivedCount = 0;

        while (true) {
            auto buffer = self.fetch();
            if (buffer.empty()) {
                break;
            }
            ++receivedCount;
            std::invoke(fn, buffer);
            self.consume();
        }

        return receivedCount;
    }
};

} // namespace turbofw::mixin
