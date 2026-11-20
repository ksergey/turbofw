// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
#include <string>
#include <type_traits>

#include <fmt/format.h>
#include <turboq-logger/Codec.h>
#include <turboq-logger/Transform.h>

#include "SBESchemaUtils.h"
#include "detail/sbe/JsonWriter.h"
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

namespace logging {

/// Copies the encoded SBE body for the logger queue: a message is a non-owning view into the producer's buffer,
/// which is reused right after `consume()`, so the bytes and the acting block length/version are captured here
/// (before the call returns) and replayed in the backend thread.
template <typename M>
    requires turbofw::SBEMessage<M>
struct SBEWrapper {
    std::span<std::byte const> content;
    std::uint16_t actingBlockLength;
    std::uint16_t actingVersion;

    explicit SBEWrapper(M msg) noexcept
        : content{msg.buffer().subspan(msg.offset())}, actingBlockLength{msg.actingBlockLength()},
          actingVersion{msg.actingVersion()} {}

    explicit SBEWrapper(std::span<std::byte const> body, std::uint16_t blockLength, std::uint16_t version) noexcept
        : content{body}, actingBlockLength{blockLength}, actingVersion{version} {}

    /// Rebuilds the message from the copied body: the acting values keep the decode consistent with the original
    [[nodiscard]] constexpr auto makeMsg() const -> M {
        return M{const_cast<std::byte*>(content.data()), content.size(), 0, actingBlockLength, actingVersion};
    }
};

} // namespace logging

} // namespace turbofw

/// SBE message fmtlib formatter: the message is written as JSON straight into the log entry
template <typename M>
struct fmt::formatter<turbofw::logging::SBEWrapper<M>> : formatter<string_view> {
    auto format(turbofw::logging::SBEWrapper<M> const& wrapper, format_context& ctx) const -> format_context::iterator {
        return formatter<string_view>::format(turbofw::toJsonString(wrapper.makeMsg()), ctx);
    }
};

namespace turboq::logger {

/// Transform SBE encoded message into SBEWrapper<M>
template <typename M>
    requires turbofw::SBEMessage<M>
struct Transform<M> {
    [[nodiscard]] constexpr auto operator()(M msg) const noexcept -> turbofw::logging::SBEWrapper<M> {
        return turbofw::logging::SBEWrapper<M>{msg};
    }
};

/// Codec for SBEWrapper<M>: body size (4 bytes), acting block length, acting version, then the body itself
template <typename M>
struct Codec<turbofw::logging::SBEWrapper<M>> {
    using SizeType = std::uint32_t;
    using SizeCodec = Codec<SizeType>;
    using ActingCodec = Codec<std::uint16_t>;

    static constexpr auto encodedSize(turbofw::logging::SBEWrapper<M> const& wrapper) noexcept -> std::size_t {
        return SizeCodec::encodedSize() + 2 * ActingCodec::encodedSize() + wrapper.content.size();
    }

    static void encode(std::byte*& dest, turbofw::logging::SBEWrapper<M> const& wrapper) noexcept {
        auto const size = static_cast<SizeType>(wrapper.content.size());
        SizeCodec::encode(dest, size);
        ActingCodec::encode(dest, wrapper.actingBlockLength);
        ActingCodec::encode(dest, wrapper.actingVersion);
        if (size != 0) [[likely]] {
            std::memcpy(dest, wrapper.content.data(), size);
        }
        dest += size;
    }

    static auto decode(std::byte const*& src) noexcept -> turbofw::logging::SBEWrapper<M> {
        auto const size = SizeCodec::decode(src);
        auto const blockLength = ActingCodec::decode(src);
        auto const version = ActingCodec::decode(src);
        auto const body = std::span<std::byte const>{src, size};
        src += size;
        return turbofw::logging::SBEWrapper<M>{body, blockLength, version};
    }
};

} // namespace turboq::logger
