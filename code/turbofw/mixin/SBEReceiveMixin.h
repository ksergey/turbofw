// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <functional>

#include <turboq/Concepts.h>

#include <turbofw/SBE.h>

namespace turbofw::mixin {

/// Counters returned by a SBE-aware drain, named for readability: `{received, handled, dropped}` reads at
/// the call site either as structured bindings or as named members.
struct ReceiveCounters {
    std::size_t received;
    std::size_t handled;
    std::size_t dropped;
};

/// Adds a SBE-aware `receive` operation to a consumer that provides `fetch()` and `consume()`: drains the queue,
/// fetching buffers and dispatching them via `tryDecodeSBE<L>()` until the queue is empty. `L` is the list of
/// expected message types; they must share one generated MessageHeader (`SBEMessageHeader<L>` checks it).
///
/// Returns a \ref ReceiveCounters: `received` -- every fetched buffer; `handled` -- buffers decoded into a
/// message of `L`; `dropped` -- buffers too short to carry even the MessageHeader. A buffer with a readable
/// header whose (schemaId, templateId) pair is absent from `L` counts as received but neither handled nor
/// dropped: it was a message, just foreign.
///
/// Mixin hook `processMessage(header, buffer)`: invoked before dispatch for every mixin of `Self::Mixins` that
/// declares it (in list order). The hook runs for every buffer with a readable MessageHeader, including the ones
/// `tryDecodeSBE` will not handle -- hooks demultiplex by the acting `(schemaId, templateId)` themselves; a hook
/// that needs fields decodes its own message type from the buffer. The callback no-throw rule applies to hooks.
struct SBEReceiveMixin {
    template <typename L, typename Self, typename Fn>
        requires turboq::Consumer<std::remove_cvref_t<Self>>
    auto receive(this Self&& self, Fn&& fn) -> ReceiveCounters {
        using Header = SBEMessageHeader<L>;

        ReceiveCounters counters{};

        while (true) {
            auto buffer = self.fetch();
            if (buffer.empty()) {
                break;
            }
            ++counters.received;

            // early-return guard shape: the cold drop path stays at the top, the hot path un-nested
            [&] {
                if (buffer.size() < Header::sbeEncodedLength()) [[unlikely]] {
                    ++counters.dropped;
                    return;
                }

                auto const bufferMutable = std::span{const_cast<std::byte*>(buffer.data()), buffer.size()};
                auto const header = Header{bufferMutable.data()};

                forEachMixin<Self>([&]<typename Mixin>(std::type_identity<Mixin>) {
                    if constexpr (requires { self.Mixin::processMessage(header, buffer); }) {
                        self.Mixin::processMessage(header, buffer);
                    }
                });

                counters.handled += tryDecodeSBE<L, Fn>(buffer, header, std::forward<Fn>(fn)) ? 1u : 0u;
            }();

            self.consume();
        }

        return counters;
    }
};

} // namespace turbofw::mixin
