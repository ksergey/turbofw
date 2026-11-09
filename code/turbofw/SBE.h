// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <type_traits>
#include <utility>

#include "mp/Algorithm.h"
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

namespace detail {

template <typename M>
using MessageHeaderOf = typename M::Header;

/// Uniform header across the list L: every message of L carries one and the same generated MessageHeader --
/// as all messages of one schema do. The gate of `SBEMessageHeader` and both `tryDecodeSBE` overloads.
template <typename L>
concept UniformHeader = mp::IsList<L>::value && (mp::Size<mp::Unique<mp::Transform<MessageHeaderOf, L>>>::value == 1);

} // namespace detail

/// The generated MessageHeader shared by every message of the list L: `Front`-style alias, available as long
/// as all elements of L carry the same generated header (`detail::UniformHeader`; md and svc mix legally
/// because both include the very same common_types.xml header and the bundle generation makes it a single C++
/// type). A type without a `Header` alias in L is a hard compile error, not a concept failure -- SBE lists
/// are written by hand, that is an acceptable price.
template <typename L>
    requires detail::UniformHeader<L>
using SBEMessageHeader = typename mp::Front<L>::Header;

/// Demultiplexes an SBE message buffer: tries every message type of the list L against the header, the first
/// one whose (schemaId, templateId) pair matches is decoded and passed to `fn`; returns whether any did.
///
/// \code{.cpp}
/// auto const buffer = consumer.fetch().value();
/// turbofw::tryDecodeSBE<md::Messages>(buffer, Overloaded{
///     [](md::MbpUpdate msg) { /* hot path */
///     },
///     [](md::Snapshot msg) { /* ... */
///     }});
/// \endcode
///
/// Comparisons happen against the acting values read from the buffer (they may stem from an older schema
/// version and differ from `M`'s constants), not against `M::sbeSchemaId()` alone. Two list entries with the
/// same (schemaId, templateId) pair are both reachable: the first wins, later ones are version shadows.
///
/// The buffer must carry a generated MessageHeader, and `fn` must accept every message type of L as a by-copy
/// argument (SBE message objects are lightweight non-owning views over the caller's buffer; decode never
/// mutates the buffer -- the const_cast inside is what lets a `std::span<std::byte const>` through, since the
/// generated wrap() needs a mutable pointer): overload sets and generic lambdas both work.
///
/// This overload assumes the header is already decoded: `buffer.size() >= header.sbeEncodedLength()` and `header`
/// was decoded from this very buffer (it is the `SBEReceiveMixin::receive` fast path). A body shorter than the
/// acting block length makes the generated decode throw on field access: per the hot-path exception rule, treat
/// it as fatal rather than a recoverable failure.
template <typename L, typename Fn>
    requires detail::UniformHeader<L>
constexpr auto tryDecodeSBE(std::span<std::byte const> buffer, SBEMessageHeader<L> const& header, Fn&& fn) -> bool {
    return [&]<template <typename...> typename List, typename... Ts>(List<Ts...>)
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
constexpr auto tryDecodeSBE(std::span<std::byte const> buffer, Fn&& fn) -> bool {
    // Messages in list L have the same MessageHeader, so decode header once
    using Header = SBEMessageHeader<L>;

    if (buffer.size() < Header::sbeEncodedLength()) [[unlikely]] {
        return false;
    }

    auto const bufferMutable = std::span{const_cast<std::byte*>(buffer.data()), buffer.size()};
    auto const header = Header{bufferMutable.data()};
    return tryDecodeSBE<L, Fn>(buffer, header, std::forward<Fn>(fn));
}

} // namespace turbofw
