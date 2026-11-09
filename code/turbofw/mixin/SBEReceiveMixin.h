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

/// Adds a SBE-aware `receive` operation to a consumer that provides `fetch()` and `consume()`.
///
/// Drains the queue: repeatedly fetches the next buffer and dispatches it to the callback via
/// `tryDecodeSBE<L>()` until the queue is empty. `L` is the list of expected message types; they must share one
/// generated MessageHeader (as all types of one schema do, `SBEMessageHeader<L>` checks it).
///
/// Returns a \ref ReceiveCounters:
/// - `received` -- every fetched buffer;
/// - `handled` -- buffers decoded into a message of `L` and passed to the callback;
/// - `dropped` -- buffers too short to carry even the MessageHeader (`tryDecodeSBE` cannot look at them, and a
///   well-formed publisher never produces one). A buffer with a readable header but an `(schemaId, templateId)`
///   pair absent from `L` counts as received but neither handled nor dropped: it was a message, just foreign.
///
/// The callback follows the hot-path exception rule: it may throw only to signal an unrecoverable, fatal error.
/// An ordinary failure must not throw: the buffer is consumed when the callback returns normally -- there is no
/// rollback on the exception path, and an unconsumed mpsc slot stalls the consumer for good.
///
/// **Mixin hook `processMessage(header, buffer)`.** Before dispatch, `receive` invokes
/// `self.Mixin::processMessage(header, buffer)` for every mixin of `Self::Mixins` that declares the method (in
/// list order, see `forEachMixin`). The hook runs for *every* buffer with a readable MessageHeader -- including
/// the ones `tryDecodeSBE` will not handle: hooks audit or filter the wire stream themselves, demultiplexing by
/// the acting `(schemaId, templateId)` read from the passed header. `header` is the generated header view
/// (pass it by value), `buffer` is the full queue buffer as `std::span<std::byte const>`: hooks must not
/// mutate the buffer, and because the message body is not decoded yet, a hook that needs fields must decode
/// its own message type from `buffer`. The callback no-throw rule applies to hooks as well.
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
