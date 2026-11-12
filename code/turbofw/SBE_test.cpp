// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <doctest/doctest.h>

#include <turbofw/SBE.h>
#include <turbofw/mp/List.h>

#include <turbofw/schema/market_data/schema.h>
#include <turbofw/schema/service/schema.h>
#include <turbofw/schema/trading/schema.h>

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

/// Encode a turbofw::schema::service::Heartbeat with fixed fields into a buffer
auto encodeHeartbeat(std::span<std::byte> buffer) -> void {
    auto msg = turbofw::schema::service::Heartbeat::wrapAndApplyHeader(buffer);
    msg.get<"ts">().value(222);
    msg.get<"serviceId">().value(1);
    msg.get<"flags">().value(turbofw::schema::service::ServiceFlags{}.start(true));
}

/// Encode an turbofw::schema::market_data::Snapshot with two bid levels into a buffer
auto encodeSnapshot(std::span<std::byte> buffer) -> void {
    auto msg = turbofw::schema::market_data::Snapshot::wrapAndApplyHeader(buffer);
    auto header = msg.get<"header">();
    header.get<"instrumentId">().value(42);
    header.get<"flags">().value(turbofw::schema::market_data::MdFlags{}.snapshot(true).mandatory(true));
    msg.get<"appId">().value(17);
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

/// Encode a turbofw::schema::trading::CreateContext with var-size data fields into a buffer
auto encodeCreateContext(std::span<std::byte> buffer) -> void {
    auto msg = turbofw::schema::trading::CreateContext::wrapAndApplyHeader(buffer);
    msg.get<"appId">().value(17);
    msg.get<"requestId">().value(3);
    msg.get<"instrumentId">().value(42);
    msg.get<"clientCode">().value("ALPHA");
    msg.get<"account">().value("SPOT-ACC");
}

/// Encode a turbofw::schema::service::SetLogLevel into a buffer (an "unknown" message for most decode tests)
auto encodeSetLogLevel(std::span<std::byte> buffer) -> void {
    auto msg = turbofw::schema::service::SetLogLevel::wrapAndApplyHeader(buffer);
    msg.get<"level">().value(turbofw::schema::service::LogLevel::Error);
}

} // namespace

TEST_SUITE("SBE") {
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
            [&](turbofw::schema::market_data::Snapshot msg) {
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
            [&](turbofw::schema::trading::CreateContext msg) {
                ++contextCalls;
                CHECK_EQ(msg.get<"appId">().value(), 17u);
                CHECK_EQ(msg.get<"instrumentId">().value(), 42u);
                CHECK_EQ(msg.get<"clientCode">().value(), std::string_view{"ALPHA"});
                CHECK_EQ(msg.get<"account">().value(), std::string_view{"SPOT-ACC"});
            },
            [&](turbofw::schema::service::Heartbeat msg) {
                ++heartbeatCalls;
                CHECK_EQ(msg.get<"serviceId">().value(), 1u);
                CHECK_EQ(msg.get<"ts">().value(), 222);
            },
        };
        using Messages = mp::List<turbofw::schema::market_data::Snapshot, turbofw::schema::trading::CreateContext,
            turbofw::schema::service::Heartbeat>;

        CHECK(turbofw::tryDecodeSBE<Messages>(snapshotBuffer, fn));
        CHECK(turbofw::tryDecodeSBE<Messages>(ctxBuffer, fn));
        CHECK(turbofw::tryDecodeSBE<Messages>(hbBuffer, fn));
        CHECK_EQ(snapshotCalls, 1);
        CHECK_EQ(contextCalls, 1);
        CHECK_EQ(heartbeatCalls, 1);
    }

    TEST_CASE("generic callback also works") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        encodeSnapshot(buffer);

        auto calls = 0;
        auto const result = turbofw::tryDecodeSBE<
            mp::List<turbofw::schema::market_data::Snapshot, turbofw::schema::trading::CreateContext>>(
            buffer, [&](auto msg) {
                // `if constexpr` instead of a static_assert: the invocable check instantiates both branches,
                // a static_assert would fail for the CreateContext branch even if it never runs
                if constexpr (std::is_same_v<decltype(msg), turbofw::schema::market_data::Snapshot>) {
                    ++calls;
                    CHECK_EQ(msg.template get<"header">().template get<"instrumentId">().value(), 42u);
                } else {
                    ++calls;
                    FAIL("the dispatch must pick the message type matching the buffer, not another list entry");
                }
            });
        REQUIRE(result);
        CHECK_EQ(calls, 1);
    }

    TEST_CASE("message not in the list: ignored") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        encodeSetLogLevel(buffer);

        auto calls = 0;
        auto const callback = [&calls](auto&&...) {
            ++calls;
        };
        auto const result = turbofw::tryDecodeSBE<
            mp::List<turbofw::schema::service::Heartbeat, turbofw::schema::market_data::Snapshot>>(buffer, callback);
        REQUIRE_FALSE(result);
        CHECK_EQ(calls, 0);
    }

    TEST_CASE("short buffer and garbage: not a match, without reading past the end") {
        // an all-zero buffer decodes a header with (schemaId=0, templateId=0): no generated message has id 0
        // (asserted right below), so it can never match the list
        static_assert(turbofw::schema::service::Heartbeat::sbeSchemaId() != 0 &&
                      turbofw::schema::service::Heartbeat::sbeTemplateId() != 0);
        static_assert(turbofw::schema::trading::CreateContext::sbeSchemaId() != 0 &&
                      turbofw::schema::trading::CreateContext::sbeTemplateId() != 0);

        auto buffer = std::array<std::byte, kBufferSize>{};
        auto calls = 0;
        auto const callback = [&calls](auto&&...) {
            ++calls;
        };
        using Messages = mp::List<turbofw::schema::service::Heartbeat, turbofw::schema::trading::CreateContext>;

        // a buffer shorter than a MessageHeader is not an SBE message at all
        CHECK_FALSE(turbofw::tryDecodeSBE<Messages>(std::span<std::byte const>{buffer.data(), 4}, callback));
        CHECK_FALSE(turbofw::tryDecodeSBE<Messages>(std::span<std::byte const>{}, callback));

        CHECK_FALSE(turbofw::tryDecodeSBE<Messages>(buffer, callback));
        CHECK_EQ(calls, 0);
    }

    TEST_CASE("truncated body: fatal, not a decoding status") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        encodeHeartbeat(buffer);

        // the header stays readable but the body is cut one byte short of the block length
        auto constexpr kBlockSize = 8 + turbofw::schema::service::Heartbeat::sbeBlockLength() - 1;
        auto const truncated = std::span<std::byte const>{buffer.data(), kBlockSize};
        auto calls = 0;
        auto const impaired = [&calls](turbofw::schema::service::Heartbeat) {
            ++calls;
        };
        CHECK_THROWS((void)turbofw::tryDecodeSBE<mp::List<turbofw::schema::service::Heartbeat>>(truncated, impaired));
        CHECK_EQ(calls, 0);
    }

    TEST_CASE("header overload: dispatch given a pre-decoded header") {
        auto hbBuffer = std::array<std::byte, kBufferSize>{};
        auto foreignBuffer = std::array<std::byte, kBufferSize>{};
        encodeHeartbeat(hbBuffer);
        encodeSetLogLevel(foreignBuffer); // (schemaId, templateId) not represented in the list below

        using Messages = mp::List<turbofw::schema::market_data::Snapshot, turbofw::schema::trading::CreateContext,
            turbofw::schema::service::Heartbeat>;
        using Header = turbofw::SBEMessageHeader<Messages>; // one shared header type for the whole list

        auto heartbeatCalls = 0;
        auto const fn = Overloaded{
            [&](turbofw::schema::market_data::Snapshot) {
                FAIL("dispatch must pick the type matching the header, not another list entry");
            },
            [&](turbofw::schema::trading::CreateContext) {
                FAIL("dispatch must pick the type matching the header, not another list entry");
            },
            [&](turbofw::schema::service::Heartbeat msg) {
                ++heartbeatCalls;
                CHECK_EQ(msg.get<"ts">().value(), 222);
            },
        };

        // the acting header decoded once outside: every candidate compares against it without re-reading
        auto const header = Header{hbBuffer.data()};
        CHECK(
            turbofw::tryDecodeSBE<Messages>(std::span<std::byte const>{hbBuffer.data(), hbBuffer.size()}, header, fn));
        CHECK_EQ(heartbeatCalls, 1);

        // a header with an (schemaId, templateId) pair absent from the list: no match, no callback
        auto calls = 0;
        auto const swallow = [&calls](auto&&...) {
            ++calls;
        };
        auto const foreign = Header{foreignBuffer.data()};
        CHECK_FALSE(turbofw::tryDecodeSBE<Messages>(
            std::span<std::byte const>{foreignBuffer.data(), foreignBuffer.size()}, foreign, swallow));
        CHECK_EQ(calls, 0);
    }

    TEST_CASE("decode never writes to the buffer") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        encodeSnapshot(buffer);
        auto const pristine = buffer;

        auto calls = 0;
        CHECK(turbofw::tryDecodeSBE<mp::List<turbofw::schema::market_data::Snapshot>>(
            buffer, [&](turbofw::schema::market_data::Snapshot msg) {
                ++calls;
                CHECK_EQ(msg.get<"header">().get<"instrumentId">().value(), 42u);
                auto bids = msg.get<"bids">();
                CHECK_EQ(bids.count(), 2);
                bids.next();
                bids.next();
            }));
        REQUIRE_EQ(calls, 1);
        // byte-identical after a full decode: decode is a pure view over the caller's buffer
        CHECK(std::equal(buffer.begin(), buffer.end(), pristine.begin(), pristine.end()));
    }
}

TEST_SUITE("SBE field type") {
    TEST_CASE("field value type matches the wire type behind the schema field") {
        // plain scalar
        static_assert(std::is_same_v<turbofw::SBEFieldOf<turbofw::schema::service::Heartbeat, "serviceId">,
            turbofw::schema::service::Heartbeat::ServiceIdRef>);
        static_assert(
            std::is_same_v<turbofw::SBEFieldValueOf<turbofw::schema::service::Heartbeat, "serviceId">, std::uint16_t>);

        // optional composite: the composite class itself, as types.h re-exports it
        static_assert(std::is_same_v<turbofw::SBEFieldValueOf<turbofw::schema::market_data::MbpUpdate, "price">,
            turbofw::schema::market_data::Price9NULL>);

        // enum and set fields: the enum/set class
        static_assert(std::is_same_v<turbofw::SBEFieldValueOf<turbofw::schema::trading::NewOrder, "side">,
            turbofw::schema::trading::OrderSide>);
        static_assert(std::is_same_v<turbofw::SBEFieldValueOf<turbofw::schema::trading::NewOrder, "flags">,
            turbofw::schema::trading::OrderFlags>);

        CHECK(true);
    }

    TEST_CASE("SBEHasField: ordinary fields are, groups and unknown names are not") {
        static_assert(turbofw::SBEHasField<turbofw::schema::service::Heartbeat, "serviceId">);
        static_assert(turbofw::SBEHasField<turbofw::schema::market_data::MbpUpdate, "price">);
        // MbpUpdate.bids is a repeating group ref: named, but valueless
        static_assert(!turbofw::SBEHasField<turbofw::schema::market_data::MbpUpdate, "bids">);
        static_assert(!turbofw::SBEHasField<turbofw::schema::market_data::MbpUpdate, "noSuchField">);
        static_assert(!turbofw::SBEHasField<int, "anything">);

        CHECK(true);
    }

    TEST_CASE("field null value matches the schema null of nullable fields") {
        // optional plain scalar: the raw sentinel the decoder compares against
        static_assert(turbofw::SBEFieldNullValueOf<turbofw::schema::market_data::MbpUpdate, "orders"> ==
                      std::numeric_limits<std::uint32_t>::max());
        static_assert(std::is_same_v<
            std::remove_cv_t<decltype(turbofw::SBEFieldNullValueOf<turbofw::schema::market_data::MbpUpdate, "orders">)>,
            std::uint32_t>);
        static_assert(turbofw::SBEFieldNullValueOf<turbofw::schema::service::Heartbeat, "serviceId"> ==
                      std::numeric_limits<std::uint16_t>::max());

        // optional enum: the enum class SBE_NULL constant
        static_assert(turbofw::SBEFieldNullValueOf<turbofw::schema::market_data::Trade, "aggressor"> ==
                      turbofw::schema::market_data::Side::SBE_NULL);
        static_assert(std::is_same_v<
            std::remove_cv_t<decltype(turbofw::SBEFieldNullValueOf<turbofw::schema::market_data::Trade, "aggressor">)>,
            turbofw::schema::market_data::Side>);

        // optional composite: a default constructed composite class
        static_assert(std::is_same_v<
            std::remove_cv_t<decltype(turbofw::SBEFieldNullValueOf<turbofw::schema::market_data::MbpUpdate, "price">)>,
            turbofw::schema::market_data::Price9NULL>);

        CHECK(true);
    }

    TEST_CASE("SBEHasFieldNull: nullable fields are, required ones and groups are not") {
        static_assert(turbofw::SBEHasFieldNull<turbofw::schema::market_data::MbpUpdate, "price">);
        static_assert(turbofw::SBEHasFieldNull<turbofw::schema::market_data::Trade, "aggressor">);
        static_assert(turbofw::SBEHasFieldNull<turbofw::schema::service::Heartbeat, "serviceId">);
        static_assert(turbofw::SBEHasFieldNull<turbofw::schema::trading::NewOrder,
            "requestId">); // optional since the requestId rework
        // required fields and repeating groups carry no NullValue
        static_assert(!turbofw::SBEHasFieldNull<turbofw::schema::market_data::Trade, "price">);
        static_assert(!turbofw::SBEHasFieldNull<turbofw::schema::trading::NewOrder, "quantity">);
        static_assert(!turbofw::SBEHasFieldNull<turbofw::schema::market_data::MbpUpdate, "bids">);
        static_assert(!turbofw::SBEHasFieldNull<int, "anything">);

        CHECK(true);
    }
}

} // namespace turbofw::testing
