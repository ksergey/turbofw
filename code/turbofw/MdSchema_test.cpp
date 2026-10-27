// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <array>
#include <cstddef>

#include <doctest/doctest.h>

#include <turbofw/md/schema.h>

namespace turbofw::testing {

TEST_SUITE("MdSchema") {
    TEST_CASE("MboUpdate encode/decode round trip") {
        std::array<std::byte, 128> buffer{};

        auto msg = md::MboUpdate::wrapAndApplyHeader(buffer);
        msg.get<"orderId">().value(123456789);
        msg.get<"price">().value(10'050);
        msg.get<"qty">().value(3);
        msg.get<"instrumentId">().value(42);
        msg.get<"action">().value(md::MboAction::Add);
        msg.get<"side">().value(md::Side::Buy);
        msg.get<"flags">().value(md::MdFlags{}.endOfEvent(true));

        auto header = md::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), md::MboUpdate::sbeSchemaId());
        REQUIRE_EQ(header.get<"templateId">().value(), md::MboUpdate::sbeTemplateId());

        auto decoded = md::MboUpdate{buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(),
            header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"orderId">().value(), 123456789);
        REQUIRE_EQ(decoded.get<"price">().value(), 10'050);
        REQUIRE_EQ(decoded.get<"qty">().value(), 3);
        REQUIRE_EQ(decoded.get<"instrumentId">().value(), 42);
        REQUIRE(decoded.get<"action">().value() == md::MboAction::Add);
        REQUIRE(decoded.get<"side">().value() == md::Side::Buy);
        auto flags = decoded.get<"flags">().value();
        REQUIRE(flags.endOfEvent());
        REQUIRE_FALSE(flags.snapshot());
    }

    TEST_CASE("MbpSnapshot groups round trip") {
        std::array<std::byte, 512> buffer{};

        auto msg = md::MbpSnapshot::wrapAndApplyHeader(buffer);
        msg.get<"instrumentId">().value(42);

        auto bids = msg.get<"bids">();
        bids.reset(2);
        for (std::int64_t i = 0; bids.hasNext(); ++i) {
            auto& level = bids.next();
            level.get<"price">().value(100 - i);
            level.get<"qty">().value(10 + i);
            level.get<"orderCount">().value(1);
        }
        auto asks = msg.get<"asks">();
        asks.reset(1);
        asks.next().get<"price">().value(101);

        auto header = md::MessageHeader{buffer.data()};
        auto decoded = md::MbpSnapshot{buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(),
            header.get<"version">().value()};

        auto decodedBids = decoded.get<"bids">();
        REQUIRE_EQ(decodedBids.count(), 2);
        REQUIRE_EQ(decodedBids.next().get<"price">().value(), 100);
        REQUIRE_EQ(decodedBids.next().get<"price">().value(), 99);

        auto decodedAsks = decoded.get<"asks">();
        REQUIRE_EQ(decodedAsks.count(), 1);
        REQUIRE_EQ(decodedAsks.next().get<"price">().value(), 101);
    }

    TEST_CASE("InstrumentDefinition contractMultiplier null semantics") {
        std::array<std::byte, 256> buffer{};

        auto msg = md::InstrumentDefinition::wrapAndApplyHeader(buffer);
        msg.get<"tickSize">().value(1);
        msg.get<"lotSize">().value(1);
        msg.get<"minQty">().value(1);
        msg.get<"contractMultiplier">().value(100); // e.g. futures

        auto header = md::MessageHeader{buffer.data()};
        auto decoded = md::InstrumentDefinition{buffer, header.sbeEncodedLength(),
            header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE(decoded.get<"contractMultiplier">().present());
        REQUIRE_EQ(decoded.get<"contractMultiplier">().value(), 100);

        // spot/equities have no contract multiplier: the field is reset to the QtyNULL sentinel
        msg.get<"contractMultiplier">().reset();
        auto decoded2 = md::InstrumentDefinition{buffer, header.sbeEncodedLength(),
            header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_FALSE(decoded2.get<"contractMultiplier">().present());
    }
}

} // namespace turbofw::testing
