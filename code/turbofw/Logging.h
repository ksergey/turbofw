// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

#include <fmt/base.h>

#include <turboq-logger/Logging.h>
#include <turboq/Platform.h>

#include <turbofw/SBE.h>

namespace turbofw {

using turboq::logger::LogLevel;
using turboq::logger::logLevelFromString;
using turboq::logger::logLevelToString;
using turboq::logger::setLogLevel;

/// Return true on logger backend running
TURBOQ_FORCE_INLINE auto isLoggerBackendRunning() noexcept -> bool {
    return turboq::logger::BackendThread::isRunning();
}

namespace logging {

/// Wrapper to pass SBE flyweight to logger queue
template <typename M>
    requires turbofw::SBEMessage<M>
struct SBEWrapper {
    std::span<std::byte const> content;

    explicit SBEWrapper(M msg) noexcept : content{msg.buffer().subspan(msg.offset())} {}
    explicit SBEWrapper(std::span<std::byte const> d) noexcept : content{std::move(d)} {}

    constexpr auto makeMsg() const -> M {
        return M{const_cast<std::byte*>(content.data()), content.size()};
    }
};

} // namespace logging

} // namespace turbofw

/// SBE message fmtlib formatter
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

/// Codec implementation for SBEWrapper<M>
template <typename M>
struct Codec<turbofw::logging::SBEWrapper<M>> {
    using SizeType = std::uint32_t;
    using SizeCodec = Codec<SizeType>;

    static constexpr auto encodedSize(turbofw::logging::SBEWrapper<M> const& wrapper) noexcept -> std::size_t {
        return SizeCodec::encodedSize() + wrapper.content.size();
    }

    static void encode(std::byte*& dest, turbofw::logging::SBEWrapper<M> const& wrapper) noexcept {
        auto const size = wrapper.content.size();
        SizeCodec::encode(dest, size);
        if (size != 0) [[likely]] {
            std::memcpy(dest, wrapper.content.data(), size);
        }
        dest += size;
    }

    static auto decode(std::byte const*& src) noexcept -> turbofw::logging::SBEWrapper<M> {
        auto const size = SizeCodec::decode(src);
        auto buffer = std::span<std::byte const>(src, size);
        src += size;
        return turbofw::logging::SBEWrapper<M>{buffer};
    }
};

} // namespace turboq::logger
