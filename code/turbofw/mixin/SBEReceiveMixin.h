// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <functional>
#include <tuple>

#include <turboq/Concepts.h>

#include <turbofw/SBE.h>

namespace turbofw::mixin {

/// Adds a SBE-aware `receive` operation to a consumer that provides `fetch()` and `consume()`.
///
/// Drains the queue: repeatedly fetches the next buffer and passes it to the callback until the queue is empty.
/// Returns the number of processed buffers.
///
/// The callback follows the hot-path exception rule (see AGENTS.md): it may throw only to signal an unrecoverable,
/// fatal error. An ordinary failure must not throw: the buffer is consumed when the callback returns normally --
/// there is no rollback on the exception path, and an unconsumed mpsc slot stalls the consumer for good.
struct SBEReceiveMixin {
    template <typename L, typename Self, typename Fn>
        requires turboq::Consumer<std::remove_cvref_t<Self>>
    auto receive(this Self&& self, Fn&& fn) -> std::tuple<std::size_t, std::size_t> {
        using Header = SBEMessageHeader<L>;

        std::size_t receivedCount = 0;
        std::size_t handledCount = 0;

        while (true) {
            auto buffer = self.fetch();
            if (buffer.empty()) {
                break;
            }
            ++receivedCount;

            if (buffer.size() >= Header::sbeEncodedLength()) [[likely]] {
                auto const bufferMutable = std::span{const_cast<std::byte*>(buffer.data()), buffer.size()};
                auto const header = Header{bufferMutable.data()};

                forEachMixin<Self>([&]<typename Mixin>(std::type_identity<Mixin>) {
                    if constexpr (requires { self.Mixin::processMessage(header, buffer); }) {
                        self.Mixin::processMessage(header, buffer);
                    }
                });

                handledCount += tryDecodeSBE<L, Fn>(buffer, header, std::forward<Fn>(fn)) ? 1u : 0u;
            } else {
                // TODO: dropped counter?
            }

            self.consume();
        }

        return std::make_tuple(receivedCount, handledCount);
    }
};

} // namespace turbofw::mixin
