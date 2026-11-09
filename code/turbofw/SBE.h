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
using SBEHeaderOf = typename M::Header;

template <typename L>
inline constexpr auto isTheSameMessageHeader =
    mp::IsList<L>::value && (mp::Size<mp::Unique<mp::Transform<detail::SBEHeaderOf, L>>>::value == 1);

} // namespace detail

/// Get common message header type from SBE messages list
template <typename L>
    requires detail::isTheSameMessageHeader<L>
using SBEMessageHeader = typename mp::Front<L>::Header;

template <typename L, typename Fn>
    requires detail::isTheSameMessageHeader<L>
constexpr auto tryDecodeSBE(std::span<std::byte const> buffer, SBEMessageHeader<L> const& header, Fn&& fn) -> bool {
    return [&]<template <typename...> typename List, typename... Ts>(List<Ts...>)
        requires(SBEMessage<Ts> && ...) && (std::invocable<Fn&, Ts> && ...)
    {
        return ([&]() -> bool {
            if (header.template get<"schemaId">().value() != Ts::sbeSchemaId() ||
                header.template get<"templateId">().value() != Ts::sbeTemplateId()) {
                return false;
            }

            auto const bufferMutable = std::span{const_cast<std::byte*>(buffer.data()), buffer.size()};
            auto const msg = Ts{bufferMutable, header.sbeEncodedLength(), header.template get<"blockLength">().value(),
                header.template get<"version">().value()};
            std::invoke(std::forward<Fn>(fn), msg);

            return true;
        }() || ...);
    }(L{});
}

template <typename L, typename Fn>
    requires detail::isTheSameMessageHeader<L>
constexpr auto tryDecodeSBE(std::span<std::byte const> buffer, Fn&& fn) -> bool {
    // Messages in list L have the same MessageHeader, so decode header once
    using Header = typename mp::First<L>::Header;

    if (buffer.size() < Header::sbeEncodedLength()) [[unlikely]] {
        return false;
    }

    auto const bufferMutable = std::span{const_cast<std::byte*>(buffer.data()), buffer.size()};
    auto const header = Header{bufferMutable.data()};
    return tryDecodeSBE<L, Fn>(buffer, header, std::forward<Fn>(fn));
}

} // namespace turbofw
