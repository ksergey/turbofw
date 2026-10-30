// Copyright (c) Sergey Kovalevych <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <array>
#include <cstddef>
#include <string_view>

#include <doctest/doctest.h>

#include <turbofw/md/schema.h>
#include <turbofw/meta/schema.h>
#include <turbofw/svc/schema.h>
#include <turbofw/trd/schema.h>

namespace turbofw::testing {

TEST_SUITE("TradingSchema") {
    TEST_CASE("schemas are demultiplexable by (schemaId, templateId)") {
        // every schema has its own schemaId, so a consumer can demultiplex messages from one queue
        REQUIRE_NE(trd::NewOrder::sbeSchemaId(), md::Statistic::sbeSchemaId());
        REQUIRE_NE(trd::NewOrder::sbeSchemaId(), meta::InstrumentDefinition::sbeSchemaId());
        REQUIRE_NE(trd::NewOrder::sbeSchemaId(), svc::Heartbeat::sbeSchemaId());
        REQUIRE_NE(md::Statistic::sbeSchemaId(), meta::InstrumentDefinition::sbeSchemaId());
        REQUIRE_NE(md::Statistic::sbeSchemaId(), svc::Heartbeat::sbeSchemaId());
        REQUIRE_NE(meta::InstrumentDefinition::sbeSchemaId(), svc::Heartbeat::sbeSchemaId());
        // service template ids start at 100 so they do not overlap protocol template ids
        REQUIRE_GE(svc::Heartbeat::sbeTemplateId(), 100u);
        REQUIRE_GE(svc::HeartbeatRequest::sbeTemplateId(), 100u);
        REQUIRE_GE(svc::SetLogLevel::sbeTemplateId(), 100u);
        REQUIRE_GE(svc::SetLogLevelResult::sbeTemplateId(), 100u);
    }

    TEST_CASE("CreateContext / ContextResult round trip") {
        std::array<std::byte, 256> buffer{};

        auto request = trd::CreateContext::wrapAndApplyHeader(buffer);
        request.get<"clientId">().value(17);
        request.get<"requestId">().value(3);
        request.get<"instrumentId">().value(42);
        request.get<"clientCode">().value("ALPHA");
        request.get<"account">().value("SPOT-ACC");

        auto mdHeader = trd::MessageHeader{buffer.data()};
        REQUIRE_EQ(mdHeader.get<"schemaId">().value(), trd::CreateContext::sbeSchemaId());
        REQUIRE_EQ(mdHeader.get<"templateId">().value(), trd::CreateContext::sbeTemplateId());
        auto decoded = trd::CreateContext{buffer, mdHeader.sbeEncodedLength(), mdHeader.get<"blockLength">().value(),
            mdHeader.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"clientId">().value(), 17);
        REQUIRE_EQ(decoded.get<"requestId">().value(), 3);
        REQUIRE_EQ(decoded.get<"instrumentId">().value(), 42);
        REQUIRE_EQ(decoded.get<"clientCode">().value(), std::string_view{"ALPHA"});
        REQUIRE_EQ(decoded.get<"account">().value(), std::string_view{"SPOT-ACC"});

        auto result = trd::ContextResult::wrapAndApplyHeader(buffer);
        result.get<"clientId">().value(17);
        result.get<"requestId">().value(3);
        result.get<"contextId">().value(5);
        result.get<"result">().value(trd::ResultCode::Ok);
        result.get<"text">().value("");

        auto resultHeader = trd::MessageHeader{buffer.data()};
        REQUIRE_EQ(resultHeader.get<"schemaId">().value(), trd::ContextResult::sbeSchemaId());
        auto decodedResult = trd::ContextResult{buffer, resultHeader.sbeEncodedLength(),
            resultHeader.get<"blockLength">().value(), resultHeader.get<"version">().value()};
        REQUIRE_EQ(decodedResult.get<"contextId">().value(), 5);
        REQUIRE(decodedResult.get<"result">().value() == trd::ResultCode::Ok);
        REQUIRE_EQ(decodedResult.get<"text">().value(), std::string_view{});

        std::array<std::byte, 256> failureBuffer{};
        auto failed = trd::ContextResult::wrapAndApplyHeader(failureBuffer);
        failed.get<"clientId">().value(20);
        failed.get<"requestId">().value(9);
        failed.get<"contextId">().reset(); // null if CreateContext failed
        failed.get<"result">().value(trd::ResultCode::UnknownInstrument);
        failed.get<"text">().value("no such instrument");
        auto failureHeader = trd::MessageHeader{failureBuffer.data()};
        auto decodedFailed = trd::ContextResult{failureBuffer, failureHeader.sbeEncodedLength(),
            failureHeader.get<"blockLength">().value(), failureHeader.get<"version">().value()};
        REQUIRE_FALSE(decodedFailed.get<"contextId">().present());
        REQUIRE(decodedFailed.get<"result">().value() == trd::ResultCode::UnknownInstrument);
        REQUIRE_EQ(decodedFailed.get<"text">().value(), std::string_view{"no such instrument"});
    }

    TEST_CASE("NewOrder limit order round trip") {
        std::array<std::byte, 128> buffer{};

        auto msg = trd::NewOrder::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(3);
        msg.get<"orderRef">().value(99u);
        msg.get<"side">().value(trd::OrderSide::Buy);
        msg.get<"orderType">().value(trd::OrderType::Limit);
        msg.get<"timeInForce">().value(trd::TimeInForce::Gtd);
        msg.get<"price">().value(10'050);
        msg.get<"stopPrice">().reset(); // stop price is not used by Limit
        msg.get<"qty">().value(5);
        msg.get<"displayQty">().value(1);
        msg.get<"expireTs">().value(1'775'000'000'000'000'000);
        msg.get<"flags">().value(trd::OrderFlags{}.postOnly(true));

        auto header = trd::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), trd::NewOrder::sbeSchemaId());
        REQUIRE_EQ(header.get<"templateId">().value(), trd::NewOrder::sbeTemplateId());

        auto decoded = trd::NewOrder{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"contextId">().value(), 5);
        REQUIRE_EQ(decoded.get<"orderRef">().value(), 99u);
        REQUIRE(decoded.get<"side">().value() == trd::OrderSide::Buy);
        REQUIRE(decoded.get<"orderType">().value() == trd::OrderType::Limit);
        REQUIRE(decoded.get<"timeInForce">().value() == trd::TimeInForce::Gtd);
        REQUIRE(decoded.get<"price">().present());
        REQUIRE_EQ(decoded.get<"price">().value(), 10'050);
        REQUIRE_FALSE(decoded.get<"stopPrice">().present());
        REQUIRE_EQ(decoded.get<"qty">().value(), 5);
        REQUIRE_EQ(decoded.get<"displayQty">().value(), 1);
        REQUIRE(decoded.get<"expireTs">().present());
        auto flags = decoded.get<"flags">().value();
        REQUIRE(flags.postOnly());
        REQUIRE_FALSE(flags.reduceOnly());
    }

    TEST_CASE("NewOrder market order: price null") {
        std::array<std::byte, 128> buffer{};

        auto msg = trd::NewOrder::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(0);
        msg.get<"orderRef">().value(100u);
        msg.get<"side">().value(trd::OrderSide::Sell);
        msg.get<"orderType">().value(trd::OrderType::Market);
        msg.get<"timeInForce">().value(trd::TimeInForce::Ioc);
        msg.get<"price">().reset();
        msg.get<"stopPrice">().reset();
        msg.get<"qty">().value(1);
        msg.get<"displayQty">().reset();
        msg.get<"expireTs">().reset();
        msg.get<"flags">().value(trd::OrderFlags{});

        auto header = trd::MessageHeader{buffer.data()};
        auto decoded = trd::NewOrder{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_FALSE(decoded.get<"price">().present());
        REQUIRE(decoded.get<"orderType">().value() == trd::OrderType::Market);
    }

    TEST_CASE("NewOrder stop order: stopPrice set, price null") {
        std::array<std::byte, 128> buffer{};

        auto msg = trd::NewOrder::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(7);
        msg.get<"requestId">().value(0);
        msg.get<"orderRef">().value(101u);
        msg.get<"side">().value(trd::OrderSide::Sell);
        msg.get<"orderType">().value(trd::OrderType::StopMarket);
        msg.get<"timeInForce">().value(trd::TimeInForce::Day);
        msg.get<"price">().reset();
        msg.get<"stopPrice">().value(9'900);
        msg.get<"qty">().value(2);
        msg.get<"displayQty">().reset();
        msg.get<"expireTs">().reset();
        msg.get<"flags">().value(trd::OrderFlags{});

        auto header = trd::MessageHeader{buffer.data()};
        auto decoded = trd::NewOrder{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_FALSE(decoded.get<"price">().present());
        REQUIRE(decoded.get<"stopPrice">().present());
        REQUIRE_EQ(decoded.get<"stopPrice">().value(), 9'900);
        REQUIRE(decoded.get<"orderType">().value() == trd::OrderType::StopMarket);
    }

    TEST_CASE("ReplaceOrder: null fields keep the current values") {
        std::array<std::byte, 128> buffer{};

        auto msg = trd::ReplaceOrder::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(4);
        msg.get<"orderRef">().value(99u);
        msg.get<"price">().value(10'060);
        msg.get<"stopPrice">().reset();
        msg.get<"qty">().reset();

        auto header = trd::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), trd::ReplaceOrder::sbeSchemaId());
        auto decoded = trd::ReplaceOrder{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"orderRef">().value(), 99u);
        REQUIRE(decoded.get<"price">().present());
        REQUIRE_EQ(decoded.get<"price">().value(), 10'060);
        REQUIRE_FALSE(decoded.get<"stopPrice">().present());
        REQUIRE_FALSE(decoded.get<"qty">().present());
    }

    TEST_CASE("CancelOrder round trip") {
        std::array<std::byte, 64> buffer{};

        auto msg = trd::CancelOrder::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(5);
        msg.get<"orderRef">().value(99u);

        auto header = trd::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"templateId">().value(), trd::CancelOrder::sbeTemplateId());
        auto decoded = trd::CancelOrder{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"orderRef">().value(), 99u);
    }

    TEST_CASE("CancelAll: optional side filter") {
        std::array<std::byte, 64> buffer{};

        auto msg = trd::CancelAll::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(6);
        msg.get<"side">().value(trd::OrderSide::Sell);

        auto header = trd::MessageHeader{buffer.data()};
        auto decoded = trd::CancelAll{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE(decoded.get<"side">().present());
        REQUIRE(decoded.get<"side">().value() == trd::OrderSide::Sell);

        std::array<std::byte, 64> bothBuffer{};
        auto both = trd::CancelAll::wrapAndApplyHeader(bothBuffer);
        both.get<"contextId">().value(5);
        both.get<"requestId">().value(7);
        both.get<"side">().reset(); // null: both sides

        auto bothHeader = trd::MessageHeader{bothBuffer.data()};
        auto decodedBoth = trd::CancelAll{bothBuffer, bothHeader.sbeEncodedLength(),
            bothHeader.get<"blockLength">().value(), bothHeader.get<"version">().value()};
        REQUIRE_FALSE(decodedBoth.get<"side">().present());
    }

    TEST_CASE("OrderStatusRequest: optional orderRef") {
        std::array<std::byte, 64> buffer{};

        auto msg = trd::OrderStatusRequest::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(8);
        msg.get<"orderRef">().value(99u);

        auto header = trd::MessageHeader{buffer.data()};
        auto decoded = trd::OrderStatusRequest{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"orderRef">().value(), 99u);

        std::array<std::byte, 64> allBuffer{};
        auto all = trd::OrderStatusRequest::wrapAndApplyHeader(allBuffer);
        all.get<"contextId">().value(5);
        all.get<"requestId">().value(9);
        all.get<"orderRef">().reset(); // null: all open orders of the context

        auto allHeader = trd::MessageHeader{allBuffer.data()};
        auto decodedAll = trd::OrderStatusRequest{allBuffer, allHeader.sbeEncodedLength(),
            allHeader.get<"blockLength">().value(), allHeader.get<"version">().value()};
        REQUIRE_FALSE(decodedAll.get<"orderRef">().present());
    }

    TEST_CASE("ExecutionReport fill") {
        std::array<std::byte, 256> buffer{};

        auto msg = trd::ExecutionReport::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"instrumentId">().value(42);
        msg.get<"requestId">().value(0); // unsolicited fill
        msg.get<"orderRef">().value(99u);
        msg.get<"execType">().value(trd::ExecType::Trade);
        msg.get<"orderStatus">().value(trd::OrderStatus::PartiallyFilled);
        msg.get<"side">().value(trd::OrderSide::Buy);
        msg.get<"price">().value(10'050);
        msg.get<"orderQty">().value(5);
        msg.get<"leavesQty">().value(3);
        msg.get<"cumQty">().value(2);
        msg.get<"lastQty">().value(2);
        msg.get<"lastPx">().value(10'051);
        msg.get<"tradeId">().value(7'654'321u);
        msg.get<"exchangeOrderId">().value(123'456u);
        msg.get<"reason">().value(trd::ResultCode::Ok);
        msg.get<"transactTs">().value(1'775'000'000'000'000'000);
        msg.get<"receiveTs">().value(1'775'000'000'000'000'100);
        msg.get<"text">().value("");

        auto header = trd::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), trd::ExecutionReport::sbeSchemaId());
        REQUIRE_EQ(header.get<"templateId">().value(), trd::ExecutionReport::sbeTemplateId());
        auto decoded = trd::ExecutionReport{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"leavesQty">().value(), 3);
        REQUIRE_EQ(decoded.get<"cumQty">().value(), 2);
        REQUIRE(decoded.get<"lastQty">().present());
        REQUIRE_EQ(decoded.get<"lastQty">().value(), 2);
        REQUIRE(decoded.get<"lastPx">().present());
        REQUIRE_EQ(decoded.get<"lastPx">().value(), 10'051);
        REQUIRE_EQ(decoded.get<"tradeId">().value(), 7'654'321u);
        REQUIRE(decoded.get<"execType">().value() == trd::ExecType::Trade);
        REQUIRE(decoded.get<"orderStatus">().value() == trd::OrderStatus::PartiallyFilled);
        REQUIRE_EQ(decoded.get<"text">().value(), std::string_view{});
    }

    TEST_CASE("ExecutionReport without a fill leaves trade details null") {
        std::array<std::byte, 256> buffer{};

        auto msg = trd::ExecutionReport::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"instrumentId">().value(42);
        msg.get<"requestId">().value(4);
        msg.get<"orderRef">().value(99u);
        msg.get<"execType">().value(trd::ExecType::New);
        msg.get<"orderStatus">().value(trd::OrderStatus::New);
        msg.get<"side">().value(trd::OrderSide::Buy);
        msg.get<"price">().value(10'050);
        msg.get<"orderQty">().value(5);
        msg.get<"leavesQty">().value(5);
        msg.get<"cumQty">().value(0);
        msg.get<"lastQty">().reset();
        msg.get<"lastPx">().reset();
        msg.get<"tradeId">().reset();
        msg.get<"exchangeOrderId">().reset();
        msg.get<"reason">().value(trd::ResultCode::Ok);
        msg.get<"transactTs">().reset();
        msg.get<"receiveTs">().value(1'775'000'000'000'000'100);
        msg.get<"text">().value("");

        auto header = trd::MessageHeader{buffer.data()};
        auto decoded = trd::ExecutionReport{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_FALSE(decoded.get<"lastPx">().present());
        REQUIRE_FALSE(decoded.get<"lastQty">().present());
        REQUIRE_FALSE(decoded.get<"tradeId">().present());
        REQUIRE_FALSE(decoded.get<"exchangeOrderId">().present());
        REQUIRE(decoded.get<"execType">().value() == trd::ExecType::New);
        REQUIRE(decoded.get<"orderStatus">().value() == trd::OrderStatus::New);
    }

    TEST_CASE("CommandReject round trip") {
        std::array<std::byte, 128> buffer{};

        auto msg = trd::CommandReject::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(5);
        msg.get<"orderRef">().value(99u);
        msg.get<"reason">().value(trd::ResultCode::NotConnected);
        msg.get<"receiveTs">().value(1'775'000'000'000'000'100);
        msg.get<"text">().value("no session with the venue");

        auto header = trd::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"templateId">().value(), trd::CommandReject::sbeTemplateId());
        auto decoded = trd::CommandReject{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE(decoded.get<"reason">().value() == trd::ResultCode::NotConnected);
        REQUIRE_EQ(decoded.get<"orderRef">().value(), 99u);
        REQUIRE_EQ(decoded.get<"text">().value(), std::string_view{"no session with the venue"});
    }

    TEST_CASE("RequestComplete terminates the OrderStatusRequest response") {
        std::array<std::byte, 64> buffer{};

        auto msg = trd::RequestComplete::wrapAndApplyHeader(buffer);
        msg.get<"contextId">().value(5);
        msg.get<"requestId">().value(8);
        msg.get<"result">().value(trd::ResultCode::Ok);
        msg.get<"count">().value(2);

        auto header = trd::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), trd::RequestComplete::sbeSchemaId());
        auto decoded = trd::RequestComplete{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE(decoded.get<"result">().value() == trd::ResultCode::Ok);
        REQUIRE_EQ(decoded.get<"count">().value(), 2);
    }
}

} // namespace turbofw::testing
