// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <type_traits>
#include <utility>

#include "mp/List.h"

namespace turbofw {

/// Checks M is a generated SBE message type decodable from a raw buffer: it carries the (schemaId, templateId)
/// dispatch key and the per-schema message header type alias.
///
/// Comparison happens against the header fields read from the buffer, not against the message's fixed constants:
/// a buffer carries the acting values, which may stem from an older schema version and differ from `M`.
template <typename M>
concept SBEMessage = requires(std::span<std::byte> buffer) {
    typename M::Header;
    typename M::SizeHint;
    { M::sbeSchemaId() } -> std::convertible_to<std::uint16_t>;
    { M::sbeTemplateId() } -> std::convertible_to<std::uint16_t>;
    M::wrapAndApplyHeader(buffer);
};

/// Checks the message has variable-size parts (repeating groups or var-length data fields): its SizeHint is not
/// empty, so the sizes of those parts must be passed to the send operation
template <typename M>
concept SBEMessageWithVars = requires { typename M::SizeHint; } && (std::tuple_size_v<typename M::SizeHint> != 0);

/// Matches the message type M against the SBE MessageHeader found at the start of `buffer`: if the (schemaId,
/// templateId) pair agrees, decodes the message and passes it to `fn`, returns true. Otherwise returns false
/// without touching `fn`.
///
/// The acting `blockLength` and `version` come from the header, not from `M`'s constants, so the decoded message
/// reads its fields with the encoder's acting layout. If `buffer` is too short to hold the MessageHeader, there
/// is nothing to match against: returns false rather than reading past the end.
///
/// The decoded message is passed to the callback by copy (see the SBE message objects rule in AGENTS.md):
/// SBE message objects are lightweight non-owning views over the caller's buffer, and decode never mutates it.
template <typename M, typename Fn>
    requires SBEMessage<M> && std::invocable<Fn, M>
constexpr auto tryDecodeSBEFor(std::span<std::byte const> buffer, Fn&& fn) -> bool {
    using Header = typename M::Header;
    if (buffer.size() < Header::sbeEncodedLength()) {
        return false;
    }

    // The generated header needs a mutable pointer, hence the const_cast: decode never writes to the buffer.
    auto const bufferMutable = std::span{const_cast<std::byte*>(buffer.data()), buffer.size()};
    auto const header = Header{bufferMutable.data()};
    if (header.template get<"schemaId">().value() != M::sbeSchemaId() ||
        header.template get<"templateId">().value() != M::sbeTemplateId()) {
        return false;
    }

    auto const msg = M{bufferMutable, header.sbeEncodedLength(), header.template get<"blockLength">().value(),
        header.template get<"version">().value()};
    std::invoke(std::forward<Fn>(fn), msg);

    return true;
}

/// Demultiplexes an SBE message buffer: tries every type of the list L, the first one whose (schemaId,
/// templateId) pair matches the buffer's MessageHeader is decoded and passed to `fn`; returns whether any did.
///
/// \code{.cpp}
/// auto const buffer = consumer.fetch().value();
/// if (turbofw::tryDecodeSBE<md::Messages>(buffer, Overloaded{
///         [](md::MbpUpdate msg) { /* hot path */
///         },
///         [](md::Snapshot msg) { /* ... */
///         }}).value();
/// \endcode
///
/// The buffer must carry a MessageHeader, and `fn` must accept every message type of L as a by-copy argument
/// (see the SBE message objects rule in AGENTS.md): overload sets and generic lambdas both work. Decode reads
/// the buffer only, the callback receives a lightweight view over it.
///
/// A buffer too small to hold the MessageHeader (guard per candidate, see `tryDecodeSBEFor`), or holding an
/// (schemaId, templateId) pair absent from L, yields false without a callback invocation. A body shorter than
/// the acting block length makes the generated decode throw on field access: per the hot-path exception rule
/// (AGENTS.md), treat it as a fatal error rather than a recoverable one.
template <typename L, typename Fn>
    requires mp::IsList<L>::value
constexpr auto tryDecodeSBE(std::span<std::byte const> buffer, Fn&& fn) -> bool {
    return [&]<template <typename...> typename List, typename... Ts>(List<Ts...>)
        requires(SBEMessage<Ts> && ...) && (std::invocable<Fn&, Ts> && ...)
    {
        return (tryDecodeSBEFor<Ts>(buffer, fn) || ...);
    }(L{});
}

} // namespace turbofw
