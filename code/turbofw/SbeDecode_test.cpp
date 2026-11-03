// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <doctest/doctest.h>

#include <turbofw/md/schema.h>
#include <turbofw/SbeDecode.h>
#include <turbofw/svc/schema.h>
#include <turbofw/trd/schema.h>

namespace turbofw::testing {
namespace {

constexpr auto kBufferSize = 256;

/// Overload-set for callbacks: one overload per listed message type
template <typename... Fns>
struct Overloaded : Fns... {
    using Fns::operator()...;
};
template <typename... Fns>
Overloaded(Fns...) -> Overloaded<Fns...>;

/// Encode a svc::Heartbeat with fixed fields into a buffer
auto encodeHeartbeat(std::span<std::byte> buffer) -> void {
    auto msg = svc::Heartbeat::wrapAndApplyHeader(buffer);
    msg.get<"clientId">().value(17);
    msg.get<"requestId">().value(3);
    msg.get<"serviceId">().value(1);
    msg.get<"startTs">().value(111);
    msg.get<"ts">().value(222);
    msg.get<"intervalMs">().value(500);
    msg.get<"flags">().value(svc::ServiceFlags{}.start(true));
}

/// Encode an md::Snapshot with two bid levels into a buffer
auto encodeSnapshot(std::span<std::byte> buffer) -> void {
    auto msg = md::Snapshot::wrapAndApplyHeader(buffer);
    auto header = msg.get<"header">();
    header.get<"instrumentId">().value(42);
    header.get<"flags">().value(md::MdFlags{}.snapshot(true).mandatory(true));
    msg.get<"clientId">().value(17);
    msg.get<"requestId">().value(3);

    auto bids = msg.get<"bids">(); // descending price
    bids.reset(2);
    for (std::int64_t i = 0; bids.hasNext(); ++i) {
        auto& level = bids.next();
        level.get<"price">().get<"mantissa">().value(100 - i);
        level.get<"qty">().value(10 + i);
        level.get<"orders">().value(1);
    }
}

/// Encode a trd::CreateContext with var-size data fields into a buffer
auto encodeCreateContext(std::span<std::byte> buffer) -> void {
    auto msg = trd::CreateContext::wrapAndApplyHeader(buffer);
    msg.get<"clientId">().value(17);
    msg.get<"requestId">().value(3);
    msg.get<"instrumentId">().value(42);
    msg.get<"clientCode">().value("ALPHA");
    msg.get<"account">().value("SPOT-ACC");
}

/// Encode a svc::SetLogLevel into a buffer (an "unknown" message for most decode tests)
auto encodeSetLogLevel(std::span<std::byte> buffer) -> void {
    auto msg = svc::SetLogLevel::wrapAndApplyHeader(buffer);
    msg.get<"level">().value(svc::LogLevel::Error);
}

} // namespace

TEST_SUITE("SbeDecode") {
    TEST_CASE("one message type: decode round trip") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        encodeHeartbeat(buffer);

        auto calls = 0;
        auto const result = turbofw::decode<svc::Heartbeat>(buffer, [&](svc::Heartbeat msg) {
            ++calls;
            CHECK_EQ(msg.get<"clientId">().value(), 17u);
            CHECK_EQ(msg.get<"requestId">().value(), 3u);
            CHECK_EQ(msg.get<"serviceId">().value(), 1u);
            CHECK_EQ(msg.get<"startTs">().value(), 111);
            CHECK_EQ(msg.get<"ts">().value(), 222);
            CHECK_EQ(msg.get<"intervalMs">().value(), 500u);
            auto flags = msg.get<"flags">().value();
            CHECK(flags.start());
        });
        REQUIRE(result == turbofw::SbeDecodeResult::Decoded);
        CHECK_EQ(calls, 1);
    }

    TEST_CASE("multi-schema list: every type decodes and dispatches to its own overload") {
        auto hbBuffer = std::array<std::byte, kBufferSize>{};
        auto snapshotBuffer = std::array<std::byte, kBufferSize>{};
        auto ctxBuffer = std::array<std::byte, kBufferSize>{};
        encodeHeartbeat(hbBuffer);
        encodeSnapshot(snapshotBuffer);
        encodeCreateContext(ctxBuffer);

        auto snapshotCalls = 0;
        auto contextCalls = 0;
        auto heartbeatCalls = 0;
        auto const fn = Overloaded{
            [&](md::Snapshot msg) {
                ++snapshotCalls;
                CHECK_EQ(msg.get<"header">().get<"instrumentId">().value(), 42u);
                auto decodedFlags = msg.get<"header">().get<"flags">().value();
                CHECK(decodedFlags.snapshot());
                CHECK(decodedFlags.mandatory());
                auto bids = msg.get<"bids">();
                CHECK_EQ(bids.count(), 2);
                CHECK_EQ(bids.next().get<"price">().get<"mantissa">().value(), 100);
                CHECK_EQ(bids.next().get<"price">().get<"mantissa">().value(), 99);
            },
            [&](trd::CreateContext msg) {
                ++contextCalls;
                CHECK_EQ(msg.get<"clientId">().value(), 17u);
                CHECK_EQ(msg.get<"instrumentId">().value(), 42u);
                CHECK_EQ(msg.get<"clientCode">().value(), std::string_view{"ALPHA"});
                CHECK_EQ(msg.get<"account">().value(), std::string_view{"SPOT-ACC"});
            },
            [&](svc::Heartbeat msg) {
                ++heartbeatCalls;
                CHECK_EQ(msg.get<"clientId">().value(), 17u);
                CHECK_EQ(msg.get<"intervalMs">().value(), 500u);
            },
        };

        CHECK(turbofw::decode<md::Snapshot, trd::CreateContext, svc::Heartbeat>(snapshotBuffer, fn)
            == turbofw::SbeDecodeResult::Decoded);
        CHECK(turbofw::decode<md::Snapshot, trd::CreateContext, svc::Heartbeat>(ctxBuffer, fn)
            == turbofw::SbeDecodeResult::Decoded);
        CHECK(turbofw::decode<md::Snapshot, trd::CreateContext, svc::Heartbeat>(hbBuffer, fn)
            == turbofw::SbeDecodeResult::Decoded);
        CHECK_EQ(snapshotCalls, 1);
        CHECK_EQ(contextCalls, 1);
        CHECK_EQ(heartbeatCalls, 1);
    }

    TEST_CASE("generic callback also works") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        encodeSnapshot(buffer);

        auto calls = 0;
        auto const result = turbofw::decode<md::Snapshot, trd::CreateContext>(buffer, [&](auto msg) {
            // `if constexpr` instead of a static_assert: the invocable check instantiates both branches,
            // a static_assert would fail for the CreateContext branch even if it never runs
            if constexpr (std::is_same_v<decltype(msg), md::Snapshot>) {
                ++calls;
                CHECK_EQ(msg.template get<"header">().template get<"instrumentId">().value(), 42u);
            } else {
                ++calls;
               FAIL("the dispatch must pick the message type matching the buffer, not another list entry");
            }
        });
        REQUIRE(result == turbofw::SbeDecodeResult::Decoded);
        CHECK_EQ(calls, 1);
    }

    TEST_CASE("message not in the list: ignored") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        encodeSetLogLevel(buffer);

        auto calls = 0;
        auto const callback = [&calls](auto&&...) { ++calls; };
        auto const result = turbofw::decode<svc::Heartbeat, svc::HeartbeatRequest>(buffer, callback);
        REQUIRE(result == turbofw::SbeDecodeResult::NotInList);
        CHECK_EQ(calls, 0);
    }

    TEST_CASE("short buffer and garbage: not a message") {
        auto buffer = std::array<std::byte, kBufferSize>{};

        auto calls = 0;
        auto const callback = [&calls](auto&&...) { ++calls; };
        auto const messages = std::tuple<svc::Heartbeat, trd::CreateContext>{};

        // a buffer shorter than a MessageHeader is not an SBE message at all
        CHECK(turbofw::decode<svc::Heartbeat>(std::span<std::byte const>{buffer.data(), 4}, callback)
            == turbofw::SbeDecodeResult::NotMessage);

        // an all-zero buffer decodes a header with (schemaId=0, templateId=0): no generated message has id 0
        // (asserted right below), so it can never match the list
        static_assert(svc::Heartbeat::sbeSchemaId() != 0 && svc::Heartbeat::sbeTemplateId() != 0);
        static_assert(trd::CreateContext::sbeSchemaId() != 0 && trd::CreateContext::sbeTemplateId() != 0);
        CHECK(turbofw::decode<svc::Heartbeat, trd::CreateContext>(buffer, callback)
            == turbofw::SbeDecodeResult::NotInList);

        CHECK_EQ(calls, 0);
        (void)messages;
    }

    TEST_CASE("truncated body: fatal, not a decoding status") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        encodeHeartbeat(buffer);

        // the header stays readable but the body is cut one byte short of the block length
        auto constexpr kBlockSize = 8 + svc::Heartbeat::sbeBlockLength() - 1;
        auto const truncated = std::span<std::byte const>{buffer.data(), kBlockSize};
        auto calls = 0;
        auto const impaired = [&calls](svc::Heartbeat) { ++calls; };
        CHECK_THROWS((void)turbofw::decode<svc::Heartbeat>(truncated, impaired));
        CHECK_EQ(calls, 0);
    }
}

} // namespace turbofw::testing
