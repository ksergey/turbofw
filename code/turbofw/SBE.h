// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>

#include <fmt/format.h>
#include <turboq-logger/Codec.h>
#include <turboq-logger/Transform.h>

#include "SBESchemaUtils.h"
#include "detail/sbe/JsonWriter.h"
#include "detail/sbe/Logging.h"
#include "detail/sbe/MessageVisitor.h"

namespace turbofw {

/// Demultiplexes an SBE message buffer: tries every message type of the list L against the header, the first
/// one whose (schemaId, templateId) pair matches is decoded and passed to `fn`; returns whether any did.
/// Comparisons happen against the acting values read from the buffer, not against `M::sbeSchemaId()` alone: two
/// list entries with the same pair are both reachable, the first wins.
///
/// The buffer must carry a generated MessageHeader and `fn` must accept every message type of L as a by-copy
/// argument (SBE message objects are lightweight non-owning views; decode never mutates the buffer -- the
/// const_cast inside lets a `std::span<std::byte const>` through, since the generated wrap needs a mutable
/// pointer). This overload assumes the header is already decoded from this very buffer (`buffer.size() >=
/// header.sbeEncodedLength()`; the `SBEReceiveMixin::receive` fast path). A body shorter than the acting block
/// length makes the generated decode throw: per the hot-path exception rule, treat it as fatal.
template <typename L, typename Fn>
    requires detail::UniformHeader<L>
constexpr auto decodeSBE(std::span<std::byte const> buffer, SBEMessageHeader<L> const& header, Fn&& fn) -> bool {
    return [&]<template <typename...> typename ListT, typename... Ts>(ListT<Ts...>)
        requires(SBEMessage<Ts> && ...) && (std::invocable<Fn&, Ts> && ...)
    {
        // The generated header/wrap needs a mutable pointer, but decode never writes to the buffer.
        auto const bufferMutable = std::span{const_cast<std::byte*>(buffer.data()), buffer.size()};

        return ([&]() -> bool {
            if (header.template get<"schemaId">().value() != Ts::sbeSchemaId() ||
                header.template get<"templateId">().value() != Ts::sbeTemplateId()) {
                return false;
            }

            auto const msg = Ts{bufferMutable, header.sbeEncodedLength(), header.template get<"blockLength">().value(),
                header.template get<"version">().value()};
            std::invoke(std::forward<Fn>(fn), msg);

            return true;
        }() || ...);
    }(L{});
}

/// The convenience form: decodes the MessageHeader itself -- once, not per candidate -- then dispatches as the
/// header-taking overload above. Buffers too short to carry the MessageHeader yield false without a callback
/// invocation (guard per call, a short-buffer body is not fatal by itself: nothing beyond the header is read).
template <typename L, typename Fn>
    requires detail::UniformHeader<L>
constexpr auto decodeSBE(std::span<std::byte const> buffer, Fn&& fn) -> bool {
    // Messages in list L have the same MessageHeader, so decode header once
    using Header = SBEMessageHeader<L>;

    if (buffer.size() < Header::sbeEncodedLength()) [[unlikely]] {
        return false;
    }

    auto const bufferMutable = std::span{const_cast<std::byte*>(buffer.data()), buffer.size()};
    auto const header = Header{bufferMutable.data()};
    return decodeSBE<L, Fn>(buffer, header, std::forward<Fn>(fn));
}

/// Serializes a decoded SBE message as JSON into `out`. Opts is the visitor policy (see
/// `detail::sbe::MessageVisitorDefaultOpts`); it changes how the message is written, not what is in it.
template <typename Opts = detail::sbe::MessageVisitorDefaultOpts, typename OutputIt, typename M>
    requires SBEMessage<M>
constexpr auto toJsonString(OutputIt out, M msg) -> OutputIt {
    auto writer = detail::sbe::JsonWriter{out};
    detail::sbe::MessageVisitor<decltype(writer), Opts>{writer}(msg);
    return out;
}

/// The convenience form: serializes into a `std::string`. With the default options decimals are JSON numbers;
/// `toJsonString<MyOpts>(msg)` with a policy providing the same members as
/// `detail::sbe::MessageVisitorDefaultOpts` changes that (e.g. decimal composites as the
/// `{"mantissa": ..., "exponent": ...}` object).
template <typename Opts = detail::sbe::MessageVisitorDefaultOpts, typename M>
    requires SBEMessage<M>
[[nodiscard]] constexpr auto toJsonString(M msg) -> std::string {
    std::string buffer;
    toJsonString<Opts>(std::back_inserter(buffer), msg);
    return buffer;
}

} // namespace turbofw
