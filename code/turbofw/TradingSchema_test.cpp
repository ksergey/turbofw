// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <array>
#include <cstddef>
#include <string_view>

#include <doctest/doctest.h>

#include <turbofw/schema/market_data/schema.h>
#include <turbofw/schema/metadata/schema.h>
#include <turbofw/schema/service/schema.h>
#include <turbofw/schema/trading/schema.h>

namespace turbofw::testing {

TEST_SUITE("TradingSchema") {
    TEST_CASE("schemas are demultiplexable by (schemaId, templateId)") {
        // every schema has its own schemaId, so a consumer can demultiplex messages from one queue
        REQUIRE_NE(trading::NewOrder::sbeSchemaId(), market_data::Statistic::sbeSchemaId());
        REQUIRE_NE(trading::NewOrder::sbeSchemaId(), metadata::InstrumentDefinition::sbeSchemaId());
        REQUIRE_NE(trading::NewOrder::sbeSchemaId(), service::Heartbeat::sbeSchemaId());
        REQUIRE_NE(market_data::Statistic::sbeSchemaId(), metadata::InstrumentDefinition::sbeSchemaId());
        REQUIRE_NE(market_data::Statistic::sbeSchemaId(), service::Heartbeat::sbeSchemaId());
        REQUIRE_NE(metadata::InstrumentDefinition::sbeSchemaId(), service::Heartbeat::sbeSchemaId());
        // service template ids start at 100 so they do not overlap protocol template ids
        REQUIRE_GE(service::Heartbeat::sbeTemplateId(), 100u);
        REQUIRE_GE(service::SetLogLevel::sbeTemplateId(), 100u);
        REQUIRE_GE(service::SetLogLevelResult::sbeTemplateId(), 100u);
    }

    TEST_CASE("CreateContext / ContextResult round trip") {
        std::array<std::byte, 256> buffer{};

        auto request = trading::CreateContext::wrapAndApplyHeader(buffer);
        request.get<"appId">().value(17);
        request.get<"requestId">().value(3);
        request.get<"instrumentId">().value(42);
        request.get<"clientCode">().value("ALPHA");
        request.get<"account">().value("SPOT-ACC");

        auto mdHeader = trading::MessageHeader{buffer.data()};
        REQUIRE_EQ(mdHeader.get<"schemaId">().value(), trading::CreateContext::sbeSchemaId());
        REQUIRE_EQ(mdHeader.get<"templateId">().value(), trading::CreateContext::sbeTemplateId());
        auto decoded = trading::CreateContext{buffer, mdHeader.sbeEncodedLength(),
            mdHeader.get<"blockLength">().value(), mdHeader.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"appId">().value(), 17);
        REQUIRE_EQ(decoded.get<"requestId">().value(), 3);
        REQUIRE_EQ(decoded.get<"instrumentId">().value(), 42);
        REQUIRE_EQ(decoded.get<"clientCode">().value(), std::string_view{"ALPHA"});
        REQUIRE_EQ(decoded.get<"account">().value(), std::string_view{"SPOT-ACC"});

        auto result = trading::ContextResult::wrapAndApplyHeader(buffer);
        result.get<"appId">().value(17);
        result.get<"requestId">().value(3);
        result.get<"contextId">().value(5);
        result.get<"result">().value(trading::ResultCode::Ok);
        result.get<"text">().value("");

        auto resultHeader = trading::MessageHeader{buffer.data()};
        REQUIRE_EQ(resultHeader.get<"schemaId">().value(), trading::ContextResult::sbeSchemaId());
        auto decodedResult = trading::ContextResult{buffer, resultHeader.sbeEncodedLength(),
            resultHeader.get<"blockLength">().value(), resultHeader.get<"version">().value()};
        REQUIRE_EQ(decodedResult.get<"contextId">().value(), 5);
        REQUIRE(decodedResult.get<"result">().value() == trading::ResultCode::Ok);
        REQUIRE_EQ(decodedResult.get<"text">().value(), std::string_view{});

        std::array<std::byte, 256> failureBuffer{};
        auto failed = trading::ContextResult::wrapAndApplyHeader(failureBuffer);
        failed.get<"appId">().value(20);
        failed.get<"requestId">().value(9);
        failed.get<"contextId">().reset(); // null if CreateContext failed
        failed.get<"result">().value(trading::ResultCode::UnknownInstrument);
        failed.get<"text">().value("no such instrument");
        auto failureHeader = trading::MessageHeader{failureBuffer.data()};
        auto decodedFailed = trading::ContextResult{failureBuffer, failureHeader.sbeEncodedLength(),
            failureHeader.get<"blockLength">().value(), failureHeader.get<"version">().value()};
        REQUIRE_FALSE(decodedFailed.get<"contextId">().present());
        REQUIRE(decodedFailed.get<"result">().value() == trading::ResultCode::UnknownInstrument);
        REQUIRE_EQ(decodedFailed.get<"text">().value(), std::string_view{"no such instrument"});
    }

    TEST_CASE("NewOrder limit order round trip") {
        std::array<std::byte, 128> buffer{};

        auto msg = trading::NewOrder::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(3);
        msg.get<"orderRef">().value(99u);
        msg.get<"side">().value(trading::OrderSide::Buy);
        msg.get<"orderType">().value(trading::OrderType::Limit);
        msg.get<"timeInForce">().value(trading::TimeInForce::Gtd);
        msg.get<"price">().get<"mantissa">().value(10'050);
        msg.get<"qty">().value(5);
        msg.get<"displayQty">().value(1);
        msg.get<"expireTs">().value(1'775'000'000'000'000'000);
        msg.get<"flags">().value(trading::OrderFlags{}.postOnly(true));

        auto header = trading::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), trading::NewOrder::sbeSchemaId());
        REQUIRE_EQ(header.get<"templateId">().value(), trading::NewOrder::sbeTemplateId());

        auto decoded = trading::NewOrder{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"contextId">().value(), 5);
        REQUIRE_EQ(decoded.get<"orderRef">().value(), 99u);
        REQUIRE(decoded.get<"side">().value() == trading::OrderSide::Buy);
        REQUIRE(decoded.get<"orderType">().value() == trading::OrderType::Limit);
        REQUIRE(decoded.get<"timeInForce">().value() == trading::TimeInForce::Gtd);
        REQUIRE(decoded.get<"price">().present());
        REQUIRE_EQ(decoded.get<"price">().get<"mantissa">().value(), 10'050);
        REQUIRE_EQ(decoded.get<"qty">().value(), 5);
        REQUIRE_EQ(decoded.get<"displayQty">().value(), 1);
        REQUIRE(decoded.get<"expireTs">().present());
        auto flags = decoded.get<"flags">().value();
        REQUIRE(flags.postOnly());
        REQUIRE_FALSE(flags.reduceOnly());
    }

    TEST_CASE("NewOrder market order: price null") {
        std::array<std::byte, 128> buffer{};

        auto msg = trading::NewOrder::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(0);
        msg.get<"orderRef">().value(100u);
        msg.get<"side">().value(trading::OrderSide::Sell);
        msg.get<"orderType">().value(trading::OrderType::Market);
        msg.get<"timeInForce">().value(trading::TimeInForce::Ioc);
        msg.get<"price">().get<"mantissa">().reset();
        msg.get<"qty">().value(1);
        msg.get<"displayQty">().reset();
        msg.get<"expireTs">().reset();
        msg.get<"flags">().value(trading::OrderFlags{});

        auto header = trading::MessageHeader{buffer.data()};
        auto decoded = trading::NewOrder{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_FALSE(decoded.get<"price">().present());
        REQUIRE(decoded.get<"orderType">().value() == trading::OrderType::Market);
    }

    TEST_CASE("ReplaceOrder: null fields keep the current values") {
        std::array<std::byte, 128> buffer{};

        auto msg = trading::ReplaceOrder::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(4);
        msg.get<"orderRef">().value(99u);
        msg.get<"price">().get<"mantissa">().value(10'060);
        msg.get<"qty">().reset();

        auto header = trading::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), trading::ReplaceOrder::sbeSchemaId());
        auto decoded = trading::ReplaceOrder{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"orderRef">().value(), 99u);
        REQUIRE(decoded.get<"price">().present());
        REQUIRE_EQ(decoded.get<"price">().get<"mantissa">().value(), 10'060);
        REQUIRE_FALSE(decoded.get<"qty">().present());
    }

    TEST_CASE("CancelOrder round trip") {
        std::array<std::byte, 64> buffer{};

        auto msg = trading::CancelOrder::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(5);
        msg.get<"orderRef">().value(99u);

        auto header = trading::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"templateId">().value(), trading::CancelOrder::sbeTemplateId());
        auto decoded = trading::CancelOrder{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"orderRef">().value(), 99u);
    }

    TEST_CASE("CancelAll: optional side filter") {
        std::array<std::byte, 64> buffer{};

        auto msg = trading::CancelAll::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(6);
        msg.get<"side">().value(trading::OrderSide::Sell);

        auto header = trading::MessageHeader{buffer.data()};
        auto decoded = trading::CancelAll{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE(decoded.get<"side">().present());
        REQUIRE(decoded.get<"side">().value() == trading::OrderSide::Sell);

        std::array<std::byte, 64> bothBuffer{};
        auto both = trading::CancelAll::wrapAndApplyHeader(bothBuffer);
        both.get<"contextId">().value(5);
        both.get<"requestId">().value(7);
        both.get<"side">().reset(); // null: both sides

        auto bothHeader = trading::MessageHeader{bothBuffer.data()};
        auto decodedBoth = trading::CancelAll{bothBuffer, bothHeader.sbeEncodedLength(),
            bothHeader.get<"blockLength">().value(), bothHeader.get<"version">().value()};
        REQUIRE_FALSE(decodedBoth.get<"side">().present());
    }

    TEST_CASE("OrderStatusRequest: optional orderRef") {
        std::array<std::byte, 64> buffer{};

        auto msg = trading::OrderStatusRequest::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(8);
        msg.get<"orderRef">().value(99u);

        auto header = trading::MessageHeader{buffer.data()};
        auto decoded = trading::OrderStatusRequest{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"orderRef">().value(), 99u);

        std::array<std::byte, 64> allBuffer{};
        auto all = trading::OrderStatusRequest::wrapAndApplyHeader(allBuffer);
        all.get<"contextId">().value(5);
        all.get<"requestId">().value(9);
        all.get<"orderRef">().reset(); // null: all open orders of the context

        auto allHeader = trading::MessageHeader{allBuffer.data()};
        auto decodedAll = trading::OrderStatusRequest{allBuffer, allHeader.sbeEncodedLength(),
            allHeader.get<"blockLength">().value(), allHeader.get<"version">().value()};
        REQUIRE_FALSE(decodedAll.get<"orderRef">().present());
    }

    TEST_CASE("ExecutionReport fill") {
        std::array<std::byte, 256> buffer{};

        auto msg = trading::ExecutionReport::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"instrumentId">().value(42);
        msg.get<"requestId">().value(0); // unsolicited fill
        msg.get<"orderRef">().value(99u);
        msg.get<"execType">().value(trading::ExecType::Trade);
        msg.get<"orderStatus">().value(trading::OrderStatus::PartiallyFilled);
        msg.get<"side">().value(trading::OrderSide::Buy);
        msg.get<"price">().get<"mantissa">().value(10'050);
        msg.get<"orderQty">().value(5);
        msg.get<"leavesQty">().value(3);
        msg.get<"cumQty">().value(2);
        msg.get<"lastQty">().value(2);
        msg.get<"lastPx">().get<"mantissa">().value(10'051);
        msg.get<"tradeId">().value(7'654'321u);
        msg.get<"exchangeOrderId">().value(123'456u);
        msg.get<"reason">().value(trading::ResultCode::Ok);
        msg.get<"transactTs">().value(1'775'000'000'000'000'000);
        msg.get<"receiveTs">().value(1'775'000'000'000'000'100);
        msg.get<"text">().value("");

        auto header = trading::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), trading::ExecutionReport::sbeSchemaId());
        REQUIRE_EQ(header.get<"templateId">().value(), trading::ExecutionReport::sbeTemplateId());
        auto decoded = trading::ExecutionReport{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"leavesQty">().value(), 3);
        REQUIRE_EQ(decoded.get<"cumQty">().value(), 2);
        REQUIRE(decoded.get<"lastQty">().present());
        REQUIRE_EQ(decoded.get<"lastQty">().value(), 2);
        REQUIRE(decoded.get<"lastPx">().present());
        REQUIRE_EQ(decoded.get<"lastPx">().get<"mantissa">().value(), 10'051);
        REQUIRE_EQ(decoded.get<"tradeId">().value(), 7'654'321u);
        REQUIRE(decoded.get<"execType">().value() == trading::ExecType::Trade);
        REQUIRE(decoded.get<"orderStatus">().value() == trading::OrderStatus::PartiallyFilled);
        REQUIRE_EQ(decoded.get<"text">().value(), std::string_view{});
    }

    TEST_CASE("ExecutionReport without a fill leaves trade details null") {
        std::array<std::byte, 256> buffer{};

        auto msg = trading::ExecutionReport::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"instrumentId">().value(42);
        msg.get<"requestId">().value(4);
        msg.get<"orderRef">().value(99u);
        msg.get<"execType">().value(trading::ExecType::New);
        msg.get<"orderStatus">().value(trading::OrderStatus::New);
        msg.get<"side">().value(trading::OrderSide::Buy);
        msg.get<"price">().get<"mantissa">().value(10'050);
        msg.get<"orderQty">().value(5);
        msg.get<"leavesQty">().value(5);
        msg.get<"cumQty">().value(0);
        msg.get<"lastQty">().reset();
        msg.get<"lastPx">().get<"mantissa">().reset();
        msg.get<"tradeId">().reset();
        msg.get<"exchangeOrderId">().reset();
        msg.get<"reason">().value(trading::ResultCode::Ok);
        msg.get<"transactTs">().reset();
        msg.get<"receiveTs">().value(1'775'000'000'000'000'100);
        msg.get<"text">().value("");

        auto header = trading::MessageHeader{buffer.data()};
        auto decoded = trading::ExecutionReport{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_FALSE(decoded.get<"lastPx">().present());
        REQUIRE_FALSE(decoded.get<"lastQty">().present());
        REQUIRE_FALSE(decoded.get<"tradeId">().present());
        REQUIRE_FALSE(decoded.get<"exchangeOrderId">().present());
        REQUIRE(decoded.get<"execType">().value() == trading::ExecType::New);
        REQUIRE(decoded.get<"orderStatus">().value() == trading::OrderStatus::New);
    }

    TEST_CASE("CommandReject round trip") {
        std::array<std::byte, 128> buffer{};

        auto msg = trading::CommandReject::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(5);
        msg.get<"orderRef">().value(99u);
        msg.get<"reason">().value(trading::ResultCode::NotConnected);
        msg.get<"receiveTs">().value(1'775'000'000'000'000'100);
        msg.get<"text">().value("no session with the venue");

        auto header = trading::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"templateId">().value(), trading::CommandReject::sbeTemplateId());
        auto decoded = trading::CommandReject{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE(decoded.get<"reason">().value() == trading::ResultCode::NotConnected);
        REQUIRE_EQ(decoded.get<"orderRef">().value(), 99u);
        REQUIRE_EQ(decoded.get<"text">().value(), std::string_view{"no session with the venue"});
    }

    TEST_CASE("RequestComplete terminates the OrderStatusRequest response") {
        std::array<std::byte, 64> buffer{};

        auto msg = trading::RequestComplete::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(8);
        msg.get<"result">().value(trading::ResultCode::Ok);
        msg.get<"count">().value(2);

        auto header = trading::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), trading::RequestComplete::sbeSchemaId());
        auto decoded = trading::RequestComplete{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE(decoded.get<"result">().value() == trading::ResultCode::Ok);
        REQUIRE_EQ(decoded.get<"count">().value(), 2);
    }
}

} // namespace turbofw::testing
