// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <utility>

namespace turbofw::detail {

/// Head type of a pack: `SbeMessageListHead<Ms...>::type` is the first message type
template <typename M, typename...>
struct SbeMessageListHead {
    using type = M;
};

/// Compares the (schemaId, templateId) pair read from the buffer's MessageHeader with the dispatch keys of M;
/// on a match decodes the message and passes it to the callback
///
/// The header is schema-specific, so every candidate builds its own lightweight pointer wrapper over the buffer.
/// Header field reads are plain loads of two 2-byte scalars, cheap enough to repeat per candidate.
///
/// The decoded message is passed to the callback by copy: SBE message objects are lightweight non-owning views
/// (see AGENTS.md). The buffer itself is const -- decode only ever reads from it -- so the message carries a
/// writable span over memory the caller owns but the decoder never writes to.
template <typename M, typename Fn>
constexpr auto decodeInto(Fn& fn, std::span<std::byte const> buffer) -> bool {
    // The generated header needs a mutable pointer, hence the const_cast: decode never writes to the buffer.
    auto const header = typename M::Header{const_cast<std::byte*>(buffer.data())};
    if (header.template get<"schemaId">().value() != M::sbeSchemaId()
        || header.template get<"templateId">().value() != M::sbeTemplateId()) {
        return false;
    }
    auto const msg = M{std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()},
        header.sbeEncodedLength(), header.template get<"blockLength">().value(),
        header.template get<"version">().value()};
    std::invoke(fn, msg);
    return true;
}

} // namespace turbofw::detail

namespace turbofw {

/// Checks M is a generated SBE message type decodable from a raw buffer: it carries the (schemaId, templateId)
/// dispatch key and the per-schema message header type alias
template <typename M>
concept SbeMessage = requires {
    typename M::Header;
    { M::sbeSchemaId() } -> std::convertible_to<std::uint16_t>;
    { M::sbeTemplateId() } -> std::convertible_to<std::uint16_t>;
};

/// Result of decode(): why the buffer matched or not
enum class SbeDecodeResult : std::uint8_t {
    NotMessage, ///< the buffer is too short to hold an SBE MessageHeader -- not an SBE message at all
    NotInList, ///< the (schemaId, templateId) pair read from the buffer is not in the message list
    Decoded, ///< the message matched a list entry and was passed to the callback
};

/// Decodes an SBE message (MessageHeader + body) out of an arbitrary buffer.
///
/// \param buffer a buffer starting with the SBE MessageHeader the message was encoded with
/// \param fn callable invoked with the decoded message, passed by copy: SBE message objects are lightweight
///           non-owning views, so callbacks take them by value (see AGENTS.md). A generic lambda
///           (`[](auto msg)`) or an overloaded callable with one overload per listed message type both work;
///           the overloads must be `Ms`-complete, otherwise it is a compile error
/// \return \ref SbeDecodeResult: \p NotMessage when the buffer cannot even hold a MessageHeader,
///         \p NotInList when the (schemaId, templateId) pair is not among \tparam Ms...
///
/// Entries of the list may come from different schemas; the pair (schemaId, templateId) must be unique across the
/// list, the first match wins.
///
/// A buffer whose header is in the list but whose body is truncated (corrupted queue or an incompatible schema
/// version) makes the message construction throw: per the hot-path exception rule (see AGENTS.md) that is a
/// fatal error, not a decoding status.
template <typename... Ms, typename Fn>
    requires ((SbeMessage<Ms> && ...) && (std::invocable<Fn, Ms> && ...))
[[nodiscard]] constexpr auto decode(std::span<std::byte const> buffer, Fn&& fn) -> SbeDecodeResult {
    static_assert(sizeof...(Ms) > 0, "the message list must not be empty");

    using Header = detail::SbeMessageListHead<Ms...>::type::Header;

    // Header sizes are identical across schemas (four 2-byte fields), so one length check covers every candidate
    if (buffer.size() < Header::sbeEncodedLength()) {
        return SbeDecodeResult::NotMessage;
    }

    return (detail::decodeInto<Ms>(fn, buffer) || ...) ? SbeDecodeResult::Decoded
                                                       : SbeDecodeResult::NotInList;
}

} // namespace turbofw
