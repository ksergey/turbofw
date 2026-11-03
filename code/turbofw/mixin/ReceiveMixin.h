// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

#include <turboq/Concepts.h>

#include <turbofw/ScopeGuard.h>

namespace turbofw::mixin {

/// Adds a `receive` operation to a consumer that provides `fetch()` and `consume()`.
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

            // Consume the buffer when this iteration ends, including if the callback throws.
            auto release = ScopeGuard{[&self] noexcept {
                self.consume();
            }};

            ++receivedCount;
            std::invoke(fn, buffer);
        }

        return receivedCount;
    }
};

} // namespace turbofw::mixin
