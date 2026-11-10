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

#include <turbofw/md/schema.h>
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
    msg.get<"timestamp">().value(222);
    msg.get<"serviceId">().value(1);
    msg.get<"flags">().value(svc::ServiceFlags{}.start(true));
}

/// Encode an md::Snapshot with two bid levels into a buffer
auto encodeSnapshot(std::span<std::byte> buffer) -> void {
    auto msg = md::Snapshot::wrapAndApplyHeader(buffer);
    auto header = msg.get<"header">();
    header.get<"instrumentId">().value(42);
    header.get<"flags">().value(md::MdFlags{}.snapshot(true).mandatory(true));
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

/// Encode a trd::CreateContext with var-size data fields into a buffer
auto encodeCreateContext(std::span<std::byte> buffer) -> void {
    auto msg = trd::CreateContext::wrapAndApplyHeader(buffer);
    msg.get<"appId">().value(17);
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
                CHECK_EQ(msg.get<"appId">().value(), 17u);
                CHECK_EQ(msg.get<"instrumentId">().value(), 42u);
                CHECK_EQ(msg.get<"clientCode">().value(), std::string_view{"ALPHA"});
                CHECK_EQ(msg.get<"account">().value(), std::string_view{"SPOT-ACC"});
            },
            [&](svc::Heartbeat msg) {
                ++heartbeatCalls;
                CHECK_EQ(msg.get<"serviceId">().value(), 1u);
                CHECK_EQ(msg.get<"timestamp">().value(), 222);
            },
        };
        using Messages = mp::List<md::Snapshot, trd::CreateContext, svc::Heartbeat>;

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
        auto const result = turbofw::tryDecodeSBE<mp::List<md::Snapshot, trd::CreateContext>>(buffer, [&](auto msg) {
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
        auto const result = turbofw::tryDecodeSBE<mp::List<svc::Heartbeat, md::Snapshot>>(buffer, callback);
        REQUIRE_FALSE(result);
        CHECK_EQ(calls, 0);
    }

    TEST_CASE("short buffer and garbage: not a match, without reading past the end") {
        // an all-zero buffer decodes a header with (schemaId=0, templateId=0): no generated message has id 0
        // (asserted right below), so it can never match the list
        static_assert(svc::Heartbeat::sbeSchemaId() != 0 && svc::Heartbeat::sbeTemplateId() != 0);
        static_assert(trd::CreateContext::sbeSchemaId() != 0 && trd::CreateContext::sbeTemplateId() != 0);

        auto buffer = std::array<std::byte, kBufferSize>{};
        auto calls = 0;
        auto const callback = [&calls](auto&&...) {
            ++calls;
        };
        using Messages = mp::List<svc::Heartbeat, trd::CreateContext>;

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
        auto constexpr kBlockSize = 8 + svc::Heartbeat::sbeBlockLength() - 1;
        auto const truncated = std::span<std::byte const>{buffer.data(), kBlockSize};
        auto calls = 0;
        auto const impaired = [&calls](svc::Heartbeat) {
            ++calls;
        };
        CHECK_THROWS((void)turbofw::tryDecodeSBE<mp::List<svc::Heartbeat>>(truncated, impaired));
        CHECK_EQ(calls, 0);
    }

    TEST_CASE("header overload: dispatch given a pre-decoded header") {
        auto hbBuffer = std::array<std::byte, kBufferSize>{};
        auto foreignBuffer = std::array<std::byte, kBufferSize>{};
        encodeHeartbeat(hbBuffer);
        encodeSetLogLevel(foreignBuffer); // (schemaId, templateId) not represented in the list below

        using Messages = mp::List<md::Snapshot, trd::CreateContext, svc::Heartbeat>;
        using Header = turbofw::SBEMessageHeader<Messages>; // one shared header type for the whole list

        auto heartbeatCalls = 0;
        auto const fn = Overloaded{
            [&](md::Snapshot) {
                FAIL("dispatch must pick the type matching the header, not another list entry");
            },
            [&](trd::CreateContext) {
                FAIL("dispatch must pick the type matching the header, not another list entry");
            },
            [&](svc::Heartbeat msg) {
                ++heartbeatCalls;
                CHECK_EQ(msg.get<"timestamp">().value(), 222);
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
        CHECK(turbofw::tryDecodeSBE<mp::List<md::Snapshot>>(buffer, [&](md::Snapshot msg) {
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
        static_assert(std::is_same_v<turbofw::SBEFieldOf<svc::Heartbeat, "serviceId">, svc::Heartbeat::ServiceIdRef>);
        static_assert(std::is_same_v<turbofw::SBEFieldValueOf<svc::Heartbeat, "serviceId">, std::uint16_t>);

        // optional composite: the composite class itself, as types.h re-exports it
        static_assert(std::is_same_v<turbofw::SBEFieldValueOf<md::MbpUpdate, "price">, md::Price9NULL>);

        // enum and set fields: the enum/set class
        static_assert(std::is_same_v<turbofw::SBEFieldValueOf<trd::NewOrder, "side">, trd::OrderSide>);
        static_assert(std::is_same_v<turbofw::SBEFieldValueOf<trd::NewOrder, "flags">, trd::OrderFlags>);

        CHECK(true);
    }

    TEST_CASE("SBEHasField: ordinary fields are, groups and unknown names are not") {
        static_assert(turbofw::SBEHasField<svc::Heartbeat, "serviceId">);
        static_assert(turbofw::SBEHasField<md::MbpUpdate, "price">);
        // MbpUpdate.bids is a repeating group ref: named, but valueless
        static_assert(!turbofw::SBEHasField<md::MbpUpdate, "bids">);
        static_assert(!turbofw::SBEHasField<md::MbpUpdate, "noSuchField">);
        static_assert(!turbofw::SBEHasField<int, "anything">);

        CHECK(true);
    }

    TEST_CASE("field null value matches the schema null of nullable fields") {
        // optional plain scalar: the raw sentinel the decoder compares against
        static_assert(
            turbofw::SBEFieldNullValueOf<md::MbpUpdate, "orders"> == std::numeric_limits<std::uint32_t>::max());
        static_assert(std::is_same_v<std::remove_cv_t<decltype(turbofw::SBEFieldNullValueOf<md::MbpUpdate, "orders">)>,
            std::uint32_t>);
        static_assert(
            turbofw::SBEFieldNullValueOf<svc::Heartbeat, "serviceId"> == std::numeric_limits<std::uint16_t>::max());

        // optional enum: the enum class SBE_NULL constant
        static_assert(turbofw::SBEFieldNullValueOf<md::Trade, "aggressor"> == md::Side::SBE_NULL);
        static_assert(
            std::is_same_v<std::remove_cv_t<decltype(turbofw::SBEFieldNullValueOf<md::Trade, "aggressor">)>, md::Side>);

        // optional composite: a default constructed composite class
        static_assert(std::is_same_v<std::remove_cv_t<decltype(turbofw::SBEFieldNullValueOf<md::MbpUpdate, "price">)>,
            md::Price9NULL>);

        CHECK(true);
    }

    TEST_CASE("SBEHasFieldNull: nullable fields are, required ones and groups are not") {
        static_assert(turbofw::SBEHasFieldNull<md::MbpUpdate, "price">);
        static_assert(turbofw::SBEHasFieldNull<md::Trade, "aggressor">);
        static_assert(turbofw::SBEHasFieldNull<svc::Heartbeat, "serviceId">);
        static_assert(turbofw::SBEHasFieldNull<trd::NewOrder, "requestId">); // optional since the requestId rework
        // required fields and repeating groups carry no NullValue
        static_assert(!turbofw::SBEHasFieldNull<md::Trade, "price">);
        static_assert(!turbofw::SBEHasFieldNull<trd::NewOrder, "quantity">);
        static_assert(!turbofw::SBEHasFieldNull<md::MbpUpdate, "bids">);
        static_assert(!turbofw::SBEHasFieldNull<int, "anything">);

        CHECK(true);
    }
}

} // namespace turbofw::testing
