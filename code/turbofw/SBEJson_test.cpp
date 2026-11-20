// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <turbofw/SBE.h>
#include <turbofw/detail/sbe/JsonWriter.h>

#include <turbofw/schema/market_data/schema.h>
#include <turbofw/schema/metadata/schema.h>
#include <turbofw/schema/service/schema.h>

namespace turbofw::testing {
namespace {

constexpr auto kBufferSize = 512;

/// Decimal composites as the nested {"mantissa": ..., "exponent": ...} object
struct DecimalObjectOpts {
    static constexpr auto decimalFormat = detail::sbe::DecimalFormat::AsObject;
};

/// Every encoder returns the message rewound to the start of its body: a decoded view over what was just written
auto encodeHeartbeat(std::span<std::byte> buffer) -> turbofw::schema::service::Heartbeat {
    auto msg = turbofw::schema::service::Heartbeat::wrapAndApplyHeader(buffer);
    msg.get<"ts">().value(222);
    msg.get<"serviceId">().value(1);
    msg.get<"flags">().value(turbofw::schema::service::ServiceFlags{}.start(true).stop(true));
    return msg.rewind();
}

/// Two bid levels (100, 99) and two ask levels (100, 101), an empty stats group
auto encodeSnapshot(std::span<std::byte> buffer) -> turbofw::schema::market_data::Snapshot {
    auto msg = turbofw::schema::market_data::Snapshot::wrapAndApplyHeader(buffer);

    auto header = msg.get<"header">();
    header.get<"instrumentId">().value(42);
    header.get<"exchangeTs">().value(1795008819000000000);
    header.get<"receiveTs">().value(1795008819012133100);
    header.get<"flags">().value(turbofw::schema::market_data::MdFlags{}.snapshot(true).mandatory(true));

    msg.get<"appId">().reset();
    msg.get<"requestId">().value(3);
    msg.get<"chunkNo">().value(0);
    msg.get<"lastChunk">().value(1);
    msg.get<"tradingState">().value(17);

    auto bids = msg.get<"bids">();
    bids.reset(2);
    for (std::int64_t i = 0; bids.hasNext(); ++i) {
        auto& level = bids.next();
        level.get<"price">().get<"mantissa">().value(100 - i);
        level.get<"qty">().value(10 + i);
        level.get<"orders">().value(1);
    }

    auto asks = msg.get<"asks">();
    asks.reset(2);
    for (std::int64_t i = 0; asks.hasNext(); ++i) {
        auto& level = asks.next();
        level.get<"price">().get<"mantissa">().value(100 + i);
        level.get<"qty">().value(10 + i);
        level.get<"orders">().value(1);
    }

    auto stats = msg.get<"stats">();
    stats.reset(0);

    return msg.rewind();
}

auto encodeMbpUpdate(std::span<std::byte> buffer) -> turbofw::schema::market_data::MbpUpdate {
    auto msg = turbofw::schema::market_data::MbpUpdate::wrapAndApplyHeader(buffer);
    msg.get<"side">().value(turbofw::schema::market_data::Side::Bid);
    msg.get<"action">().value(turbofw::schema::market_data::UpdateAction::New);
    msg.get<"level">().value(1);
    msg.get<"price">().get<"mantissa">().value(1500000000);
    msg.get<"qty">().value(30);
    msg.get<"orders">().value(1);
    return msg.rewind();
}

auto encodeRegisterService(std::span<std::byte> buffer, std::string_view name, std::string_view dataQueue,
    std::string_view requestQueue) -> turbofw::schema::metadata::RegisterService {
    auto msg = turbofw::schema::metadata::RegisterService::wrapAndApplyHeader(buffer);
    msg.get<"appId">().value(17);
    msg.get<"reportedTs">().value(123);
    msg.get<"state">().value(turbofw::schema::metadata::ServiceState::Active);
    msg.get<"name">().value(name);
    msg.get<"dataQueue">().value(dataQueue);
    msg.get<"requestQueue">().value(requestQueue);
    return msg.rewind();
}

} // namespace

TEST_SUITE("SBE JSON") {
    TEST_CASE("envelope, plain fields and a multi-bit set: exact layout") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        auto const msg = encodeHeartbeat(buffer);

        auto const text = turbofw::toJsonString(msg);
        // a parse is the validity check: it throws on a malformed document
        auto const parsed = nlohmann::json::parse(text);
        CHECK_EQ(parsed["message"], "Heartbeat");
        CHECK_EQ(parsed["templateId"], 100);
        CHECK_FALSE(parsed.contains("header")); // Heartbeat carries no header field in its block
        CHECK_EQ(parsed["ts"], 222);
        CHECK_EQ(parsed["serviceId"], 1);
        CHECK_EQ(parsed["flags"], nlohmann::json::array({"Start", "Stop"}));
        // key order and separators, as emitted
        CHECK_EQ(text, R"({"message":"Heartbeat","templateId":100,"ts":222,"serviceId":1,"flags":["Start","Stop"]})");
    }

    TEST_CASE("groups: array elements are comma-separated, an empty group is []") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        auto const msg = encodeSnapshot(buffer);

        auto const parsed = nlohmann::json::parse(turbofw::toJsonString(msg));
        REQUIRE_EQ(parsed["bids"].size(), 2);
        REQUIRE_EQ(parsed["asks"].size(), 2);
        CHECK_EQ(parsed["bids"][0]["price"].get<double>(), doctest::Approx(100.0 / 1.0e9));
        CHECK_EQ(parsed["bids"][1]["price"].get<double>(), doctest::Approx(99.0 / 1.0e9));
        CHECK_EQ(parsed["asks"][1]["price"].get<double>(), doctest::Approx(101.0 / 1.0e9));
        CHECK_EQ(parsed["bids"][1]["qty"], 11);
        CHECK_EQ(parsed["bids"][0]["orders"], 1);
        CHECK_EQ(parsed["stats"], nlohmann::json::array());
        // nested composites and sets
        CHECK_EQ(parsed["header"]["instrumentId"], 42);
        CHECK_EQ(parsed["header"]["exchangeTs"], 1795008819000000000);
        CHECK_EQ(parsed["header"]["flags"], nlohmann::json::array({"Snapshot", "Mandatory"}));
        CHECK_EQ(parsed["requestId"], 3);
        CHECK_EQ(parsed["appId"], nullptr); // optional, reset to the null sentinel
        CHECK_EQ(parsed["tradingState"], 17);
    }

    TEST_CASE("decimal composites: a number by default, the nested object with Opts") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        auto const msg = encodeMbpUpdate(buffer);

        auto const plain = nlohmann::json::parse(turbofw::toJsonString(msg));
        CHECK_EQ(plain["price"].get<double>(), doctest::Approx(1.5));

        auto const asObject = nlohmann::json::parse(turbofw::toJsonString<DecimalObjectOpts>(msg));
        auto const expected = nlohmann::json{{"mantissa", 1500000000}, {"exponent", -9}};
        CHECK_EQ(asObject["price"], expected);
    }

    TEST_CASE("optional fields are null and an unknown enum value is a number") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        auto msg = turbofw::schema::market_data::MbpUpdate::wrapAndApplyHeader(buffer);
        msg.get<"side">().value(static_cast<turbofw::schema::market_data::Side>(99));
        msg.get<"action">().value(turbofw::schema::market_data::UpdateAction::Delete);
        msg.get<"level">().value(2);
        msg.get<"price">().get<"mantissa">().value(std::numeric_limits<std::int64_t>::min());
        msg.get<"qty">().reset();
        msg.get<"orders">().reset();
        msg.rewind();

        auto const parsed = nlohmann::json::parse(turbofw::toJsonString(msg));
        CHECK_EQ(parsed["side"], 99);
        CHECK_EQ(parsed["action"], "Delete");
        CHECK_EQ(parsed["price"], nullptr);
        CHECK_EQ(parsed["qty"], nullptr);
        CHECK_EQ(parsed["orders"], nullptr);
    }

    TEST_CASE("data fields are strings and are escaped") {
        auto buffer = std::array<std::byte, kBufferSize>{};
        auto const name = std::string{"svc\"q\\ \n\t\x01 end"};
        auto const msg = encodeRegisterService(buffer, name, "data-queue", "");

        auto const text = turbofw::toJsonString(msg);
        auto const parsed = nlohmann::json::parse(text); // valid JSON: every raw control char is escaped
        CHECK_EQ(parsed["name"], name);
        CHECK_EQ(parsed["dataQueue"], "data-queue");
        CHECK_EQ(parsed["requestQueue"], "");
        CHECK_EQ(text.find('\n'), std::string::npos);
        CHECK_EQ(text.find('\x01'), std::string::npos);
    }

    TEST_CASE("JsonWriter: object members, array elements and escaping") {
        auto out = std::string{};
        auto writer = turbofw::detail::sbe::JsonWriter{std::back_inserter(out)};

        writer.objectBegin();
        writer.key("a");
        writer.valueNumber(1);
        writer.key("b");
        writer.arrayBegin();
        writer.valueNumber(1);
        writer.valueNumber(2);
        writer.arrayEnd();
        writer.key("c");
        writer.arrayBegin();
        writer.arrayEnd();
        writer.key("d");
        writer.objectBegin();
        writer.key("x");
        writer.valueStr("q\"t");
        writer.objectEnd();
        writer.key("e");
        writer.arrayBegin();
        writer.arrayBegin();
        writer.valueNumber(1);
        writer.arrayEnd();
        writer.arrayBegin();
        writer.arrayEnd();
        writer.arrayEnd();
        writer.objectEnd();

        CHECK_EQ(out, R"({"a":1,"b":[1,2],"c":[],"d":{"x":"q\"t"},"e":[[1],[]]})");
    }

    TEST_CASE("logger wrapper copies the body and keeps the acting block length/version") {
        using Message = turbofw::schema::service::Heartbeat;
        using Wrapper = turbofw::logging::SBEWrapper<Message>;
        using WrapperCodec = turboq::logger::Codec<Wrapper>;

        auto buffer = std::array<std::byte, kBufferSize>{};
        auto const msg = encodeHeartbeat(buffer);

        // Non-default acting version: a message replayed with the schema defaults would decode differently
        auto const original = Wrapper{msg.buffer().subspan(msg.offset()), msg.actingBlockLength(),
            static_cast<std::uint16_t>(msg.actingVersion() + 1)};

        auto storage = std::array<std::byte, kBufferSize>{};
        auto dest = storage.data();
        WrapperCodec::encode(dest, original);
        CHECK_EQ(dest, storage.data() + WrapperCodec::encodedSize(original));

        auto src = std::as_const(storage).data();
        auto const restored = WrapperCodec::decode(src);
        CHECK_EQ(restored.content.size(), original.content.size());
        CHECK(std::ranges::equal(restored.content, original.content));
        CHECK_EQ(restored.actingBlockLength, original.actingBlockLength);
        CHECK_EQ(restored.actingVersion, original.actingVersion);

        auto const replay = restored.makeMsg();
        CHECK_EQ(replay.actingBlockLength(), original.actingBlockLength);
        CHECK_EQ(replay.actingVersion(), original.actingVersion);

        auto const parsed = nlohmann::json::parse(turbofw::toJsonString(replay));
        CHECK_EQ(parsed["ts"], 222);
        CHECK_EQ(parsed["serviceId"], 1);
    }
}

} // namespace turbofw::testing
