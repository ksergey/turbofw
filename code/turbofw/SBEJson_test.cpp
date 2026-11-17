// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <utility>

#include <doctest/doctest.h>

#include <turbofw/SBE.h>

#include <turbofw/schema/market_data/schema.h>
#include <turbofw/schema/metadata/schema.h>
#include <turbofw/schema/service/schema.h>

namespace turbofw::testing {
namespace {

constexpr auto kBufferSize = 1024;

/// Decode-back the message encoded in buffer: the serializer input form (a decoded message view)
template <typename M>
auto decodeFrom(std::span<std::byte> buffer) -> M {
    auto const header = typename M::Header{buffer.data()};
    return M{buffer, header.sbeEncodedLength(), header.template get<"blockLength">().value(),
        header.template get<"version">().value()};
}

} // namespace

TEST_SUITE("SBE JSON") {
    TEST_CASE("heartbeat: scalars, optional null and set bits") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        {
            auto msg = turbofw::schema::service::Heartbeat::wrapAndApplyHeader(buffer);
            msg.get<"ts">().value(222);
            msg.get<"serviceId">().reset();
            msg.get<"flags">().value(turbofw::schema::service::ServiceFlags{}.start(true).stop(true));
        }
        auto const json = turbofw::sbeToJson(decodeFrom<turbofw::schema::service::Heartbeat>(buffer));
        CHECK_EQ(
            json, std::string_view{R"({"message":"Heartbeat","ts":222,"serviceId":null,"flags":["Start","Stop"]})"});
    }

    TEST_CASE("mbp update: composite nesting, decimal double, enum names") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        {
            auto msg = turbofw::schema::market_data::MbpUpdate::wrapAndApplyHeader(buffer);
            msg.get<"header">().get<"instrumentId">().value(42u);
            msg.get<"header">().get<"exchangeTs">().reset();
            msg.get<"header">().get<"receiveTs">().value(1000);
            msg.get<"header">().get<"flags">().value(turbofw::schema::market_data::MdFlags{}.snapshot(true));
            msg.get<"side">().value(turbofw::schema::market_data::Side::Bid);
            msg.get<"action">().value(turbofw::schema::market_data::UpdateAction::New);
            msg.get<"level">().value(3u);
            msg.get<"price">().get<"mantissa">().value(101250000000);
            msg.get<"qty">().reset();
            msg.get<"orders">().value(5u);
        }
        auto const json = turbofw::sbeToJson(decodeFrom<turbofw::schema::market_data::MbpUpdate>(buffer));
        CHECK_EQ(
            json, std::string_view{R"({"message":"MbpUpdate","header":{"instrumentId":42,"exchangeTs":null,)"
                                   R"("receiveTs":1000,"flags":["Snapshot"]},"side":"Bid","action":"New","level":3,)"
                                   R"("price":101.25,"qty":null,"orders":5})"});

        // decimals as nested objects
        buffer = std::array<std::byte, kBufferSize>{};
        {
            auto msg = turbofw::schema::market_data::MbpUpdate::wrapAndApplyHeader(buffer);
            msg.get<"price">().get<"mantissa">().value(101250000000);
        }
        auto const nested = turbofw::sbeToJson(
            decodeFrom<turbofw::schema::market_data::MbpUpdate>(buffer), {.decimalsAsDouble = false});
        CHECK(nested.find(R"("price":{"mantissa":101250000000,"exponent":-9})") != std::string_view::npos);
    }

    TEST_CASE("unknown enum value falls back to the underlying number") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        {
            auto msg = turbofw::schema::service::Heartbeat::wrapAndApplyHeader(buffer);
            msg.get<"flags">().value(turbofw::schema::service::ServiceFlags{});
        }
        // reuse a simple enum field: RegisterService state
        auto regBuffer = std::array<std::byte, kBufferSize>{};
        {
            auto msg = turbofw::schema::metadata::RegisterService::wrapAndApplyHeader(regBuffer);
            msg.get<"state">().value(static_cast<turbofw::schema::metadata::ServiceState>(77));
        }
        auto const json = turbofw::sbeToJson(decodeFrom<turbofw::schema::metadata::RegisterService>(regBuffer));
        CHECK(json.find(R"("state":77)") != std::string_view::npos);
    }

    TEST_CASE("register service: data fields and string escaping") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        {
            auto msg = turbofw::schema::metadata::RegisterService::wrapAndApplyHeader(buffer);
            msg.get<"appId">().value(1u);
            msg.get<"requestId">().reset();
            msg.get<"reportedTs">().value(123);
            msg.get<"state">().value(turbofw::schema::metadata::ServiceState::Active);
            msg.get<"name">().value("a\"b\\c\n");
            msg.get<"dataQueue">().value("dev/metadata@data");
            msg.get<"requestQueue">().value("dev/metadata@cmd");
        }
        auto const json = turbofw::sbeToJson(decodeFrom<turbofw::schema::metadata::RegisterService>(buffer));
        CHECK_EQ(json, std::string_view{R"({"message":"RegisterService","appId":1,"requestId":null,"reportedTs":123,)"
                                        R"("state":"Active","name":"a\"b\\c\n","dataQueue":"dev/metadata@data",)"
                                        R"("requestQueue":"dev/metadata@cmd"})"});
    }

    TEST_CASE("snapshot: repeating groups, empty groups and optional entries") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        {
            auto msg = turbofw::schema::market_data::Snapshot::wrapAndApplyHeader(buffer);
            msg.get<"header">().get<"instrumentId">().value(7u);
            msg.get<"header">().get<"exchangeTs">().reset();
            msg.get<"header">().get<"receiveTs">().value(2000);
            msg.get<"header">().get<"flags">().value(
                turbofw::schema::market_data::MdFlags{}.snapshot(true).mandatory(true));
            msg.get<"appId">().reset();
            msg.get<"requestId">().reset();
            msg.get<"chunkNo">().value(1u);
            msg.get<"lastChunk">().value(1u);
            msg.get<"tradingState">().value(2u);
            auto bids = msg.get<"bids">();
            bids.reset(2);
            bids.next();
            bids.get<"price">().get<"mantissa">().value(101250000000);
            bids.get<"qty">().value(100);
            bids.get<"orders">().value(3u);
            bids.next();
            bids.get<"price">().get<"mantissa">().value(100500000000);
            bids.get<"qty">().value(50);
            bids.get<"orders">().reset();
            msg.get<"asks">().reset(0);
            msg.get<"stats">().reset(0);
        }
        auto const json = turbofw::sbeToJson(decodeFrom<turbofw::schema::market_data::Snapshot>(buffer));
        CHECK_EQ(json,
            std::string_view{R"({"message":"Snapshot","header":{"instrumentId":7,"exchangeTs":null,)"
                             R"("receiveTs":2000,"flags":["Snapshot","Mandatory"]},"appId":null,"requestId":null,)"
                             R"("chunkNo":1,"lastChunk":1,"tradingState":2,)"
                             R"("bids":[{"price":101.25,"qty":100,"orders":3},{"price":100.5,"qty":50,"orders":null}],)"
                             R"("asks":[],"stats":[]})"});
    }
}

} // namespace turbofw::testing
