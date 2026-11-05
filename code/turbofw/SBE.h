// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>

#include <boost/mp11/list.hpp>

namespace turbofw {

/// Checks M is a generated SBE message type decodable from a raw buffer: it carries the (schemaId, templateId)
/// dispatch key and the per-schema message header type alias
template <typename M>
concept SBEMessage = requires {
    typename M::Header;
    { M::sbeSchemaId() } -> std::convertible_to<std::uint16_t>;
    { M::sbeTemplateId() } -> std::convertible_to<std::uint16_t>;
};

template <typename M, typename Fn>
    requires SBEMessage<M> && std::invocable<Fn, M>
constexpr auto tryDecodeSbeFor(std::span<std::byte const> buffer, Fn fn) -> bool {
    // The generated header needs a mutable pointer, hence the const_cast: decode never writes to the buffer.
    auto const bufferMutable = std::span{const_cast<std::byte*>(buffer.data()), buffer.size()};
    auto const header = typename M::Header{const_cast<std::byte*>(buffer.data())};
    if (header.template get<"schemaId">().value() != M::sbeSchemaId() ||
        header.template get<"templateId">().value() != M::sbeTemplateId()) {
        return false;
    }

    auto const msg = M{bufferMutable, header.sbeEncodedLength(), header.template get<"blockLength">().value(),
        header.template get<"version">().value()};
    std::invoke(fn, msg);

    return true;
}

template <typename L, typename Fn>
    requires boost::mp11::mp_is_list<L>::value
constexpr auto tryDecodeSbe(std::span<std::byte const> buffer, Fn fn) -> bool {
    return [&]<template <typename...> typename List, typename... Ts>(List<Ts...>) -> bool {
        return (tryDecodeSbeFor<Ts>(buffer, fn) || ...);
    }(L{});
}

} // namespace turbofw
