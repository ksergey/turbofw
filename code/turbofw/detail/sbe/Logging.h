// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <iterator>
#include <string>

#include <turbofw/SBESchemaUtils.h>
#include <turbofw/detail/sbe/JsonWriter.h>
#include <turbofw/detail/sbe/Logging.h>
#include <turbofw/detail/sbe/MessageVisitor.h>

namespace turbofw::detail::sbe {

/// Copies the encoded SBE body for the logger queue: a message is a non-owning view into the producer's buffer,
/// which is reused right after `consume()`, so the bytes and the acting block length/version are captured here
/// (before the call returns) and replayed in the backend thread.
template <typename M>
    requires turbofw::SBEMessage<M>
struct Wrapper {
    std::span<std::byte const> content;
    std::uint16_t actingBlockLength;
    std::uint16_t actingVersion;

    explicit Wrapper(M msg) noexcept
        : content{msg.buffer().subspan(msg.offset())}, actingBlockLength{msg.actingBlockLength()},
          actingVersion{msg.actingVersion()} {}

    explicit Wrapper(std::span<std::byte const> body, std::uint16_t blockLength, std::uint16_t version) noexcept
        : content{body}, actingBlockLength{blockLength}, actingVersion{version} {}

    /// Rebuilds the message from the copied body: the acting values keep the decode consistent with the original
    [[nodiscard]] constexpr auto makeMsg() const -> M {
        return M{const_cast<std::byte*>(content.data()), content.size(), 0, actingBlockLength, actingVersion};
    }
};

} // namespace turbofw::detail::sbe

/// SBE message fmtlib formatter: the message is written as JSON straight into the log entry
template <typename M>
struct fmt::formatter<turbofw::detail::sbe::Wrapper<M>> : formatter<string_view> {
    auto format(
        turbofw::detail::sbe::Wrapper<M> const& wrapper, format_context& ctx) const -> format_context::iterator {
        std::string buffer;
        auto writer = turbofw::detail::sbe::JsonWriter{std::back_inserter(buffer)};
        turbofw::detail::sbe::MessageVisitor<decltype(writer)>{writer}(wrapper.makeMsg());
        return formatter<string_view>::format(buffer, ctx);
    }
};

namespace turboq::logger {

/// Transform SBE encoded message into Wrapper<M>
template <typename M>
    requires turbofw::SBEMessage<M>
struct Transform<M> {
    [[nodiscard]] constexpr auto operator()(M msg) const noexcept -> turbofw::detail::sbe::Wrapper<M> {
        return turbofw::detail::sbe::Wrapper<M>{msg};
    }
};

/// Codec for Wrapper<M>: body size (4 bytes), acting block length, acting version, then the body itself
template <typename M>
struct Codec<turbofw::detail::sbe::Wrapper<M>> {
    using SizeType = std::uint32_t;
    using SizeCodec = Codec<SizeType>;
    using ActingCodec = Codec<std::uint16_t>;

    static constexpr auto encodedSize(turbofw::detail::sbe::Wrapper<M> const& wrapper) noexcept -> std::size_t {
        return SizeCodec::encodedSize() + 2 * ActingCodec::encodedSize() + wrapper.content.size();
    }

    static void encode(std::byte*& dest, turbofw::detail::sbe::Wrapper<M> const& wrapper) noexcept {
        auto const size = static_cast<SizeType>(wrapper.content.size());
        SizeCodec::encode(dest, size);
        ActingCodec::encode(dest, wrapper.actingBlockLength);
        ActingCodec::encode(dest, wrapper.actingVersion);
        if (size != 0) [[likely]] {
            std::memcpy(dest, wrapper.content.data(), size);
        }
        dest += size;
    }

    static auto decode(std::byte const*& src) noexcept -> turbofw::detail::sbe::Wrapper<M> {
        auto const size = SizeCodec::decode(src);
        auto const blockLength = ActingCodec::decode(src);
        auto const version = ActingCodec::decode(src);
        auto const body = std::span<std::byte const>{src, size};
        src += size;
        return turbofw::detail::sbe::Wrapper<M>{body, blockLength, version};
    }
};

} // namespace turboq::logger
