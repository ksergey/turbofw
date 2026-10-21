// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <array>
#include <cstddef>

#include <doctest/doctest.h>

#include <turbofw/md/schema.h>
#include <turbofw/trading/schema.h>

namespace turbofw::testing {

TEST_SUITE("TradingSchema") {
    TEST_CASE("schemas have distinct ids") {
        REQUIRE_NE(trading::NewOrder::sbeSchemaId(), md::Trade::sbeSchemaId());
    }

    TEST_CASE("NewOrder encode/decode round trip") {
        std::array<std::byte, 128> buffer{};

        auto msg = trading::NewOrder::wrapAndApplyHeader(buffer);
        msg.get<"clOrdId">().value(1);
        msg.get<"price">().value(10'050);
        msg.get<"qty">().value(5);
        msg.get<"accountId">().value(3);
        msg.get<"instrumentId">().value(42);
        msg.get<"side">().value(trading::Side::Sell);
        msg.get<"ordType">().value(trading::OrdType::Limit);
        msg.get<"timeInForce">().value(trading::TimeInForce::IOC);
        msg.get<"flags">().value(trading::OrderFlags{}.postOnly(true));

        auto header = trading::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), trading::NewOrder::sbeSchemaId());
        REQUIRE_EQ(header.get<"templateId">().value(), trading::NewOrder::sbeTemplateId());

        auto decoded = trading::NewOrder{buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(),
            header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"clOrdId">().value(), 1);
        REQUIRE_EQ(decoded.get<"price">().value(), 10'050);
        REQUIRE_EQ(decoded.get<"qty">().value(), 5);
        REQUIRE(decoded.get<"side">().value() == trading::Side::Sell);
        REQUIRE(decoded.get<"timeInForce">().value() == trading::TimeInForce::IOC);
        auto flags = decoded.get<"flags">().value();
        REQUIRE(flags.postOnly());
        REQUIRE_FALSE(flags.reduceOnly());
    }

    TEST_CASE("MassCancel optional filters") {
        std::array<std::byte, 64> buffer{};

        auto msg = trading::MassCancel::wrapAndApplyHeader(buffer);
        msg.get<"clOrdId">().value(2);
        msg.get<"instrumentId">().reset();
        msg.get<"side">().reset();

        auto header = trading::MessageHeader{buffer.data()};
        auto decoded = trading::MassCancel{buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(),
            header.get<"version">().value()};
        REQUIRE_FALSE(decoded.get<"instrumentId">().present());
        REQUIRE_FALSE(decoded.get<"side">().present());
    }

    TEST_CASE("ExecutionReport fill") {
        std::array<std::byte, 256> buffer{};

        auto msg = trading::ExecutionReport::wrapAndApplyHeader(buffer);
        msg.get<"clOrdId">().value(1);
        msg.get<"orderId">().value(987);
        msg.get<"qty">().value(5);
        msg.get<"cumQty">().value(2);
        msg.get<"leavesQty">().value(3);
        msg.get<"lastPrice">().value(10'050);
        msg.get<"lastQty">().value(2);
        msg.get<"rejectReason">().value(trading::RejectReason::None);
        msg.get<"execType">().value(trading::ExecType::Trade);
        msg.get<"ordStatus">().value(trading::OrdStatus::PartiallyFilled);
        msg.get<"liquidity">().value(trading::Liquidity::Maker);

        auto header = trading::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"templateId">().value(), trading::ExecutionReport::sbeTemplateId());
        auto decoded = trading::ExecutionReport{buffer, header.sbeEncodedLength(),
            header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"leavesQty">().value(), 3);
        REQUIRE_EQ(decoded.get<"lastQty">().value(), 2);
        REQUIRE(decoded.get<"execType">().value() == trading::ExecType::Trade);
        REQUIRE(decoded.get<"ordStatus">().value() == trading::OrdStatus::PartiallyFilled);
    }
}

} // namespace turbofw::testing
