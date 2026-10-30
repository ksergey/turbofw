// Copyright (c) Sergey Kovalevych <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <array>
#include <cstddef>

#include <doctest/doctest.h>

#include <turbofw/md/schema.h>

namespace turbofw::testing {

TEST_SUITE("MdSchema") {
    TEST_CASE("MbpUpdate encode/decode round trip") {
        std::array<std::byte, 128> buffer{};

        auto msg = md::MbpUpdate::wrapAndApplyHeader(buffer);
        auto header = msg.get<"header">();
        header.get<"instrumentId">().value(42);
        header.get<"exchangeTs">().value(1'234'567'890);
        header.get<"receiveTs">().value(9'876'543'210);
        header.get<"flags">().value(md::MdFlags{}.endOfEvent(true));
        msg.get<"side">().value(md::Side::Bid);
        msg.get<"action">().value(md::UpdateAction::Change);
        msg.get<"level">().value(3);
        msg.get<"price">().value(10'050);
        msg.get<"qty">().value(3);
        msg.get<"orders">().value(12);

        auto mdHeader = md::MessageHeader{buffer.data()};
        REQUIRE_EQ(mdHeader.get<"schemaId">().value(), md::MbpUpdate::sbeSchemaId());
        REQUIRE_EQ(mdHeader.get<"templateId">().value(), md::MbpUpdate::sbeTemplateId());

        auto decoded = md::MbpUpdate{buffer, mdHeader.sbeEncodedLength(), mdHeader.get<"blockLength">().value(),
            mdHeader.get<"version">().value()};
        auto decodedHeader = decoded.get<"header">();
        REQUIRE_EQ(decodedHeader.get<"instrumentId">().value(), 42);
        REQUIRE_EQ(decodedHeader.get<"exchangeTs">().value(), 1'234'567'890);
        REQUIRE_EQ(decodedHeader.get<"receiveTs">().value(), 9'876'543'210);
        auto flags = decodedHeader.get<"flags">().value();
        REQUIRE(flags.endOfEvent());
        REQUIRE_FALSE(flags.snapshot());
        REQUIRE_EQ(decoded.get<"side">().value(), md::Side::Bid);
        REQUIRE(decoded.get<"action">().value() == md::UpdateAction::Change);
        REQUIRE_EQ(decoded.get<"level">().value(), 3);
        REQUIRE(decoded.get<"price">().present());
        REQUIRE_EQ(decoded.get<"price">().value(), 10'050);
        REQUIRE(decoded.get<"qty">().present());
        REQUIRE_EQ(decoded.get<"qty">().value(), 3);
        REQUIRE(decoded.get<"orders">().present());
        REQUIRE_EQ(decoded.get<"orders">().value(), 12);
    }

    TEST_CASE("MboUpdate encode/decode round trip") {
        std::array<std::byte, 128> buffer{};

        auto msg = md::MboUpdate::wrapAndApplyHeader(buffer);
        auto header = msg.get<"header">();
        header.get<"instrumentId">().value(42);
        header.get<"exchangeTs">().reset(); // venue does not provide event time here
        header.get<"receiveTs">().value(9'876'543'210);
        header.get<"flags">().value(md::MdFlags{}.endOfEvent(true));
        msg.get<"orderId">().value(123'456'789u);
        msg.get<"side">().value(md::Side::Ask);
        msg.get<"action">().value(md::UpdateAction::Change);
        msg.get<"price">().value(10'050);
        msg.get<"qty">().value(3);
        msg.get<"prevPrice">().value(10'048);
        msg.get<"prevQty">().value(5);
        msg.get<"priority">().value(7u);

        auto mdHeader = md::MessageHeader{buffer.data()};
        REQUIRE_EQ(mdHeader.get<"schemaId">().value(), md::MboUpdate::sbeSchemaId());
        REQUIRE_EQ(mdHeader.get<"templateId">().value(), md::MboUpdate::sbeTemplateId());

        auto decoded = md::MboUpdate{buffer, mdHeader.sbeEncodedLength(), mdHeader.get<"blockLength">().value(),
            mdHeader.get<"version">().value()};
        auto decodedHeader = decoded.get<"header">();
        REQUIRE_EQ(decodedHeader.get<"instrumentId">().value(), 42);
        REQUIRE_FALSE(decodedHeader.get<"exchangeTs">().present());
        REQUIRE_EQ(decodedHeader.get<"receiveTs">().value(), 9'876'543'210);
        REQUIRE_EQ(decoded.get<"orderId">().value(), 123'456'789u);
        REQUIRE(decoded.get<"side">().value() == md::Side::Ask);
        REQUIRE(decoded.get<"action">().value() == md::UpdateAction::Change);
        REQUIRE(decoded.get<"price">().present());
        REQUIRE_EQ(decoded.get<"price">().value(), 10'050);
        REQUIRE_EQ(decoded.get<"qty">().value(), 3);
        REQUIRE_EQ(decoded.get<"prevPrice">().value(), 10'048);
        REQUIRE_EQ(decoded.get<"prevQty">().value(), 5);
        REQUIRE_EQ(decoded.get<"priority">().value(), 7u);
    }

    TEST_CASE("MboUpdate Clear: price/qty are null") {
        std::array<std::byte, 128> buffer{};

        auto msg = md::MboUpdate::wrapAndApplyHeader(buffer);
        msg.get<"header">().get<"instrumentId">().value(42);
        msg.get<"orderId">().reset(); // Clear: orderId is not meaningful
        msg.get<"action">().value(md::UpdateAction::Clear);
        msg.get<"price">().reset();
        msg.get<"qty">().reset();
        msg.get<"prevPrice">().reset();
        msg.get<"prevQty">().reset();

        auto mdHeader = md::MessageHeader{buffer.data()};
        auto decoded = md::MboUpdate{buffer, mdHeader.sbeEncodedLength(), mdHeader.get<"blockLength">().value(),
            mdHeader.get<"version">().value()};
        REQUIRE_FALSE(decoded.get<"orderId">().present());
        REQUIRE(decoded.get<"action">().value() == md::UpdateAction::Clear);
        REQUIRE_FALSE(decoded.get<"price">().present());
        REQUIRE_FALSE(decoded.get<"qty">().present());
        REQUIRE_FALSE(decoded.get<"prevPrice">().present());
        REQUIRE_FALSE(decoded.get<"prevQty">().present());
    }

    TEST_CASE("Trade round trip") {
        std::array<std::byte, 128> buffer{};

        auto msg = md::Trade::wrapAndApplyHeader(buffer);
        auto header = msg.get<"header">();
        header.get<"instrumentId">().value(42);
        header.get<"exchangeTs">().value(1'234'567'890);
        header.get<"receiveTs">().value(9'876'543'210);
        header.get<"flags">().value(md::MdFlags{});
        msg.get<"tradeId">().value(987'654'321u);
        msg.get<"price">().value(10'050);
        msg.get<"qty">().value(3);
        msg.get<"aggressor">().value(md::Side::Ask);
        msg.get<"conditions">().value(0u);

        auto mdHeader = md::MessageHeader{buffer.data()};
        REQUIRE_EQ(mdHeader.get<"schemaId">().value(), md::Trade::sbeSchemaId());
        REQUIRE_EQ(mdHeader.get<"templateId">().value(), md::Trade::sbeTemplateId());

        auto decoded = md::Trade{buffer, mdHeader.sbeEncodedLength(), mdHeader.get<"blockLength">().value(),
            mdHeader.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"header">().get<"instrumentId">().value(), 42);
        REQUIRE(decoded.get<"tradeId">().present());
        REQUIRE_EQ(decoded.get<"tradeId">().value(), 987'654'321u);
        REQUIRE_EQ(decoded.get<"price">().value(), 10'050);
        REQUIRE_EQ(decoded.get<"qty">().value(), 3);
        REQUIRE(decoded.get<"aggressor">().present());
        REQUIRE(decoded.get<"aggressor">().value() == md::Side::Ask);
        REQUIRE_EQ(decoded.get<"conditions">().value(), 0u);
    }

    TEST_CASE("Trade: unknown aggressor is null") {
        std::array<std::byte, 128> buffer{};

        auto msg = md::Trade::wrapAndApplyHeader(buffer);
        msg.get<"header">().get<"instrumentId">().value(42);
        msg.get<"tradeId">().reset(); // venue does not provide trade id
        msg.get<"price">().value(10'050);
        msg.get<"qty">().value(3);
        msg.get<"aggressor">().reset();

        auto mdHeader = md::MessageHeader{buffer.data()};
        auto decoded = md::Trade{buffer, mdHeader.sbeEncodedLength(), mdHeader.get<"blockLength">().value(),
            mdHeader.get<"version">().value()};
        REQUIRE_FALSE(decoded.get<"tradeId">().present());
        REQUIRE_FALSE(decoded.get<"aggressor">().present());
    }

    TEST_CASE("Statistic: volume-like type goes to qty") {
        std::array<std::byte, 128> buffer{};

        auto msg = md::Statistic::wrapAndApplyHeader(buffer);
        msg.get<"header">().get<"instrumentId">().value(42);
        msg.get<"type">().value(md::StatType::Volume);
        msg.get<"price">().reset(); // price-like field is not used by Volume
        msg.get<"qty">().value(1'250'000);

        auto mdHeader = md::MessageHeader{buffer.data()};
        REQUIRE_EQ(mdHeader.get<"schemaId">().value(), md::Statistic::sbeSchemaId());
        auto decoded = md::Statistic{buffer, mdHeader.sbeEncodedLength(), mdHeader.get<"blockLength">().value(),
            mdHeader.get<"version">().value()};
        REQUIRE(decoded.get<"type">().value() == md::StatType::Volume);
        REQUIRE_FALSE(decoded.get<"price">().present());
        REQUIRE(decoded.get<"qty">().present());
        REQUIRE_EQ(decoded.get<"qty">().value(), 1'250'000);
    }

    TEST_CASE("Statistic: price-like type goes to price") {
        std::array<std::byte, 128> buffer{};

        auto msg = md::Statistic::wrapAndApplyHeader(buffer);
        msg.get<"header">().get<"instrumentId">().value(42);
        msg.get<"type">().value(md::StatType::Vwap);
        msg.get<"price">().value(10'047);
        msg.get<"qty">().reset();

        auto mdHeader = md::MessageHeader{buffer.data()};
        auto decoded = md::Statistic{buffer, mdHeader.sbeEncodedLength(), mdHeader.get<"blockLength">().value(),
            mdHeader.get<"version">().value()};
        REQUIRE(decoded.get<"type">().value() == md::StatType::Vwap);
        REQUIRE_EQ(decoded.get<"price">().value(), 10'047);
        REQUIRE_FALSE(decoded.get<"qty">().present());
    }

    TEST_CASE("InstrumentStatus round trip") {
        std::array<std::byte, 128> buffer{};

        auto msg = md::InstrumentStatus::wrapAndApplyHeader(buffer);
        msg.get<"header">().get<"instrumentId">().value(42);
        msg.get<"tradingState">().value(2);

        auto mdHeader = md::MessageHeader{buffer.data()};
        REQUIRE_EQ(mdHeader.get<"templateId">().value(), md::InstrumentStatus::sbeTemplateId());
        auto decoded = md::InstrumentStatus{buffer, mdHeader.sbeEncodedLength(), mdHeader.get<"blockLength">().value(),
            mdHeader.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"header">().get<"instrumentId">().value(), 42);
        REQUIRE_EQ(decoded.get<"tradingState">().value(), 2);
    }

    TEST_CASE("Snapshot chunk round trip") {
        std::array<std::byte, 256> buffer{};

        auto msg = md::Snapshot::wrapAndApplyHeader(buffer);
        auto header = msg.get<"header">();
        header.get<"instrumentId">().value(42);
        header.get<"receiveTs">().value(9'876'543'210);
        header.get<"flags">().value(md::MdFlags{}.snapshot(true).mandatory(true));
        msg.get<"clientId">().value(17); // response to SnapshotRequest
        msg.get<"requestId">().value(3);
        msg.get<"chunkNo">().value(0);
        msg.get<"lastChunk">().value(1);
        msg.get<"tradingState">().value(1);

        auto bids = msg.get<"bids">(); // descending price
        bids.reset(2);
        for (std::int64_t i = 0; bids.hasNext(); ++i) {
            auto& level = bids.next();
            level.get<"price">().value(100 - i);
            level.get<"qty">().value(10 + i);
            level.get<"orders">().value(1);
        }
        auto asks = msg.get<"asks">(); // ascending price
        asks.reset(1);
        auto& ask = asks.next();
        ask.get<"price">().value(101);
        ask.get<"qty">().value(7);
        ask.get<"orders">().reset();
        auto stats = msg.get<"stats">();
        stats.reset(1);
        auto& stat = stats.next();
        stat.get<"type">().value(md::StatType::Open);
        stat.get<"price">().value(99);
        stat.get<"qty">().reset();
        stat.get<"exchangeTs">().value(1'000'000'000);

        auto mdHeader = md::MessageHeader{buffer.data()};
        REQUIRE_EQ(mdHeader.get<"schemaId">().value(), md::Snapshot::sbeSchemaId());
        REQUIRE_EQ(mdHeader.get<"templateId">().value(), md::Snapshot::sbeTemplateId());
        auto decoded = md::Snapshot{buffer, mdHeader.sbeEncodedLength(), mdHeader.get<"blockLength">().value(),
            mdHeader.get<"version">().value()};

        auto decodedFlags = decoded.get<"header">().get<"flags">().value();
        REQUIRE(decodedFlags.snapshot());
        REQUIRE(decodedFlags.mandatory());
        REQUIRE(decoded.get<"clientId">().present());
        REQUIRE_EQ(decoded.get<"clientId">().value(), 17);
        REQUIRE_EQ(decoded.get<"requestId">().value(), 3);
        REQUIRE_EQ(decoded.get<"chunkNo">().value(), 0);
        REQUIRE_EQ(decoded.get<"lastChunk">().value(), 1);
        REQUIRE_EQ(decoded.get<"tradingState">().value(), 1);

        auto decodedBids = decoded.get<"bids">();
        REQUIRE_EQ(decodedBids.count(), 2);
        REQUIRE_EQ(decodedBids.next().get<"price">().value(), 100);
        REQUIRE_EQ(decodedBids.next().get<"price">().value(), 99);

        auto decodedAsks = decoded.get<"asks">();
        REQUIRE_EQ(decodedAsks.count(), 1);
        auto decodedAsk = decodedAsks.next();
        REQUIRE_EQ(decodedAsk.get<"price">().value(), 101);
        REQUIRE_FALSE(decodedAsk.get<"orders">().present());

        auto decodedStats = decoded.get<"stats">();
        REQUIRE_EQ(decodedStats.count(), 1);
        auto decodedStat = decodedStats.next();
        REQUIRE(decodedStat.get<"type">().value() == md::StatType::Open);
        REQUIRE_EQ(decodedStat.get<"price">().value(), 99);
        REQUIRE_FALSE(decodedStat.get<"qty">().present());
        REQUIRE_EQ(decodedStat.get<"exchangeTs">().value(), 1'000'000'000);
    }

    TEST_CASE("Snapshot: spontaneous, empty book, stats only in chunk 0") {
        std::array<std::byte, 128> buffer{};

        auto msg = md::Snapshot::wrapAndApplyHeader(buffer);
        msg.get<"header">().get<"instrumentId">().value(42);
        msg.get<"clientId">().reset(); // spontaneous snapshot: no addressee
        msg.get<"chunkNo">().value(1); // stats are empty in the remaining chunks
        msg.get<"lastChunk">().value(1);
        msg.get<"tradingState">().value(0);
        msg.get<"bids">().reset(0);
        msg.get<"asks">().reset(0);
        msg.get<"stats">().reset(0);

        auto mdHeader = md::MessageHeader{buffer.data()};
        auto decoded = md::Snapshot{buffer, mdHeader.sbeEncodedLength(), mdHeader.get<"blockLength">().value(),
            mdHeader.get<"version">().value()};
        REQUIRE_FALSE(decoded.get<"clientId">().present());
        REQUIRE_EQ(decoded.get<"chunkNo">().value(), 1);
        REQUIRE_EQ(decoded.get<"bids">().count(), 0);
        REQUIRE_EQ(decoded.get<"asks">().count(), 0);
        REQUIRE_EQ(decoded.get<"stats">().count(), 0);
    }

    TEST_CASE("SnapshotRequest / RequestComplete round trip") {
        std::array<std::byte, 128> buffer{};

        auto request = md::SnapshotRequest::wrapAndApplyHeader(buffer);
        request.get<"clientId">().value(17);
        request.get<"requestId">().value(3);
        request.get<"instrumentId">().value(42);
        request.get<"depth">().value(10);

        auto mdHeader = md::MessageHeader{buffer.data()};
        REQUIRE_EQ(mdHeader.get<"schemaId">().value(), md::SnapshotRequest::sbeSchemaId());
        auto decoded = md::SnapshotRequest{buffer, mdHeader.sbeEncodedLength(), mdHeader.get<"blockLength">().value(),
            mdHeader.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"clientId">().value(), 17);
        REQUIRE_EQ(decoded.get<"requestId">().value(), 3);
        REQUIRE_EQ(decoded.get<"instrumentId">().value(), 42);
        REQUIRE_EQ(decoded.get<"depth">().value(), 10);

        auto response = md::RequestComplete::wrapAndApplyHeader(buffer);
        response.get<"clientId">().value(17);
        response.get<"requestId">().value(3);
        response.get<"status">().value(md::RequestStatus::Ok);
        response.get<"count">().value(4);

        auto responseHeader = md::MessageHeader{buffer.data()};
        REQUIRE_EQ(responseHeader.get<"schemaId">().value(), md::RequestComplete::sbeSchemaId());
        auto decodedResponse = md::RequestComplete{buffer, responseHeader.sbeEncodedLength(),
            responseHeader.get<"blockLength">().value(), responseHeader.get<"version">().value()};
        REQUIRE_EQ(decodedResponse.get<"clientId">().value(), 17);
        REQUIRE_EQ(decodedResponse.get<"requestId">().value(), 3);
        REQUIRE(decodedResponse.get<"status">().value() == md::RequestStatus::Ok);
        REQUIRE_EQ(decodedResponse.get<"count">().value(), 4);
    }
}

} // namespace turbofw::testing
