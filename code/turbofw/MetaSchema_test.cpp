// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <array>
#include <cstddef>
#include <string_view>

#include <doctest/doctest.h>

#include <turbofw/schema/metadata/schema.h>

namespace turbofw::testing {

TEST_SUITE("MetaSchema") {
    TEST_CASE("RegisterService / RegisterServiceResult round trip") {
        std::array<std::byte, 256> buffer{};

        auto msg = turbofw::schema::metadata::RegisterService::wrapAndApplyHeader(buffer);
        msg.get<"appId">().value(17);
        msg.get<"requestId">().value(3);
        msg.get<"reportedTs">().value(1'775'000'000'000'000'000);
        msg.get<"state">().value(turbofw::schema::metadata::ServiceState::Active);
        msg.get<"name">().value("md-binance-1");
        msg.get<"dataQueue">().value("md-binance-1/data");
        msg.get<"requestQueue">().value("md-binance-1/req");

        auto header = turbofw::schema::metadata::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), turbofw::schema::metadata::RegisterService::sbeSchemaId());
        REQUIRE_EQ(header.get<"templateId">().value(), turbofw::schema::metadata::RegisterService::sbeTemplateId());
        auto decoded = turbofw::schema::metadata::RegisterService{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"appId">().value(), 17);
        REQUIRE_EQ(decoded.get<"requestId">().value(), 3);
        REQUIRE_EQ(decoded.get<"reportedTs">().value(), 1'775'000'000'000'000'000);
        REQUIRE(decoded.get<"state">().value() == turbofw::schema::metadata::ServiceState::Active);
        REQUIRE_EQ(decoded.get<"name">().value(), std::string_view{"md-binance-1"});
        REQUIRE_EQ(decoded.get<"dataQueue">().value(), std::string_view{"md-binance-1/data"});
        REQUIRE_EQ(decoded.get<"requestQueue">().value(), std::string_view{"md-binance-1/req"});

        auto result = turbofw::schema::metadata::RegisterServiceResult::wrapAndApplyHeader(buffer);
        result.get<"appId">().value(17);
        result.get<"requestId">().value(3);
        result.get<"status">().value(turbofw::schema::metadata::RegisterStatus::Created);
        result.get<"serviceId">().value(3);
        result.get<"version">().value(1);

        auto resultHeader = turbofw::schema::metadata::MessageHeader{buffer.data()};
        auto decodedResult = turbofw::schema::metadata::RegisterServiceResult{buffer, resultHeader.sbeEncodedLength(),
            resultHeader.get<"blockLength">().value(), resultHeader.get<"version">().value()};
        REQUIRE(decodedResult.get<"status">().value() == turbofw::schema::metadata::RegisterStatus::Created);
        REQUIRE(decodedResult.get<"serviceId">().present());
        REQUIRE_EQ(decodedResult.get<"serviceId">().value(), 3);
        REQUIRE_EQ(decodedResult.get<"version">().value(), 1);

        std::array<std::byte, 128> failureBuffer{};
        auto rejected = turbofw::schema::metadata::RegisterServiceResult::wrapAndApplyHeader(failureBuffer);
        rejected.get<"appId">().value(17);
        rejected.get<"requestId">().value(4);
        rejected.get<"status">().value(turbofw::schema::metadata::RegisterStatus::Rejected);
        rejected.get<"serviceId">().reset(); // null if Rejected
        rejected.get<"version">().value(0);
        auto failureHeader = turbofw::schema::metadata::MessageHeader{failureBuffer.data()};
        auto decodedRejected =
            turbofw::schema::metadata::RegisterServiceResult{failureBuffer, failureHeader.sbeEncodedLength(),
                failureHeader.get<"blockLength">().value(), failureHeader.get<"version">().value()};
        REQUIRE(decodedRejected.get<"status">().value() == turbofw::schema::metadata::RegisterStatus::Rejected);
        REQUIRE_FALSE(decodedRejected.get<"serviceId">().present());
    }

    TEST_CASE("RegisterInstrument / RegisterInstrumentResult round trip") {
        std::array<std::byte, 256> buffer{};

        auto msg = turbofw::schema::metadata::RegisterInstrument::wrapAndApplyHeader(buffer);
        msg.get<"appId">().value(17);
        msg.get<"requestId">().value(3);
        msg.get<"reportedTs">().value(1'775'000'000'000'000'000);
        auto attrs = msg.get<"attrs">();
        attrs.get<"venueId">().value(900);
        attrs.get<"serviceId">().value(3);
        attrs.get<"instrumentType">().value(turbofw::schema::metadata::InstrumentType::Future);
        attrs.get<"state">().value(turbofw::schema::metadata::InstrumentState::Active);
        attrs.get<"tickSize">().get<"mantissa">().value(1'000);
        attrs.get<"lotSize">().value(1);
        attrs.get<"multiplier">().value(100);
        attrs.get<"expiry">().reset(); // perpetual
        attrs.get<"currency">().value(840u);
        attrs.get<"capabilities">().value(turbofw::schema::metadata::Capabilities{}.mbp(true).trades(true));
        msg.get<"exchangeSymbol">().value("BTCUSDT-FUT");
        msg.get<"symbol">().value(""); // empty: name is generated by Metadata

        auto header = turbofw::schema::metadata::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), turbofw::schema::metadata::RegisterInstrument::sbeSchemaId());
        auto decoded = turbofw::schema::metadata::RegisterInstrument{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        auto decodedAttrs = decoded.get<"attrs">();
        REQUIRE_EQ(decodedAttrs.get<"venueId">().value(), 900);
        REQUIRE_EQ(decodedAttrs.get<"serviceId">().value(), 3);
        REQUIRE(decodedAttrs.get<"instrumentType">().value() == turbofw::schema::metadata::InstrumentType::Future);
        REQUIRE(decodedAttrs.get<"state">().value() == turbofw::schema::metadata::InstrumentState::Active);
        REQUIRE_EQ(decodedAttrs.get<"tickSize">().get<"mantissa">().value(), 1'000);
        REQUIRE_EQ(decodedAttrs.get<"lotSize">().value(), 1);
        REQUIRE_EQ(decodedAttrs.get<"multiplier">().value(), 100);
        REQUIRE_FALSE(decodedAttrs.get<"expiry">().present());
        REQUIRE_EQ(decodedAttrs.get<"currency">().value(), 840u);
        auto decodedCapabilities = decodedAttrs.get<"capabilities">().value();
        REQUIRE(decodedCapabilities.mbp());
        REQUIRE_FALSE(decodedCapabilities.mbo());
        REQUIRE(decodedCapabilities.trades());
        REQUIRE_FALSE(decodedCapabilities.statistics());
        REQUIRE_FALSE(decodedCapabilities.status());
        REQUIRE_EQ(decoded.get<"exchangeSymbol">().value(), std::string_view{"BTCUSDT-FUT"});
        REQUIRE_EQ(decoded.get<"symbol">().value(), std::string_view{});

        auto result = turbofw::schema::metadata::RegisterInstrumentResult::wrapAndApplyHeader(buffer);
        result.get<"appId">().value(17);
        result.get<"requestId">().value(3);
        result.get<"status">().value(turbofw::schema::metadata::RegisterStatus::Created);
        result.get<"instrumentId">().value(42);
        result.get<"version">().value(1);

        auto resultHeader = turbofw::schema::metadata::MessageHeader{buffer.data()};
        auto decodedResult =
            turbofw::schema::metadata::RegisterInstrumentResult{buffer, resultHeader.sbeEncodedLength(),
                resultHeader.get<"blockLength">().value(), resultHeader.get<"version">().value()};
        REQUIRE(decodedResult.get<"status">().value() == turbofw::schema::metadata::RegisterStatus::Created);
        REQUIRE_EQ(decodedResult.get<"instrumentId">().value(), 42);
    }

    TEST_CASE("InstrumentDefinition spontaneous update") {
        std::array<std::byte, 256> buffer{};

        auto msg = turbofw::schema::metadata::InstrumentDefinition::wrapAndApplyHeader(buffer);
        msg.get<"appId">().reset(); // spontaneous update on the multicast queue
        msg.get<"requestId">().value(0);
        msg.get<"instrumentId">().value(42);
        msg.get<"version">().value(2);
        msg.get<"reportedTs">().value(1'775'000'000'000'000'900);
        auto attrs = msg.get<"attrs">();
        attrs.get<"venueId">().value(900);
        attrs.get<"serviceId">().value(3);
        attrs.get<"instrumentType">().value(turbofw::schema::metadata::InstrumentType::Spot);
        attrs.get<"state">().value(turbofw::schema::metadata::InstrumentState::Suspended);
        attrs.get<"tickSize">().get<"mantissa">().value(1'000);
        attrs.get<"lotSize">().value(1);
        attrs.get<"multiplier">().reset(); // not applicable for spot
        attrs.get<"expiry">().reset();
        attrs.get<"currency">().value(840u);
        attrs.get<"capabilities">().value(turbofw::schema::metadata::Capabilities{}.mbp(true));
        msg.get<"exchangeSymbol">().value("BTCUSDT");
        msg.get<"symbol">().value("BTC-SPOT");

        auto header = turbofw::schema::metadata::MessageHeader{buffer.data()};
        REQUIRE_EQ(
            header.get<"templateId">().value(), turbofw::schema::metadata::InstrumentDefinition::sbeTemplateId());
        auto decoded = turbofw::schema::metadata::InstrumentDefinition{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_FALSE(decoded.get<"appId">().present());
        REQUIRE_EQ(decoded.get<"requestId">().value(), 0);
        REQUIRE_EQ(decoded.get<"instrumentId">().value(), 42);
        REQUIRE_EQ(decoded.get<"version">().value(), 2);
        auto decodedAttrs = decoded.get<"attrs">();
        REQUIRE(decodedAttrs.get<"state">().value() == turbofw::schema::metadata::InstrumentState::Suspended);
        REQUIRE_FALSE(decodedAttrs.get<"multiplier">().present());
        // data fields are decoded sequentially: exchangeSymbol first
        REQUIRE_EQ(decoded.get<"exchangeSymbol">().value(), std::string_view{"BTCUSDT"});
        REQUIRE_EQ(decoded.get<"symbol">().value(), std::string_view{"BTC-SPOT"});
    }

    TEST_CASE("InstrumentRequest: optional filters") {
        std::array<std::byte, 128> buffer{};

        auto msg = turbofw::schema::metadata::InstrumentRequest::wrapAndApplyHeader(buffer);
        msg.get<"appId">().value(17);
        msg.get<"requestId">().value(5);
        msg.get<"instrumentId">().value(42);
        msg.get<"venueId">().reset();
        msg.get<"serviceId">().value(3);
        msg.get<"instrumentType">().reset();
        msg.get<"state">().reset();
        msg.get<"exchangeSymbol">().value("");

        auto header = turbofw::schema::metadata::MessageHeader{buffer.data()};
        auto decoded = turbofw::schema::metadata::InstrumentRequest{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"instrumentId">().value(), 42);
        REQUIRE_FALSE(decoded.get<"venueId">().present());
        REQUIRE_EQ(decoded.get<"serviceId">().value(), 3);
        REQUIRE_FALSE(decoded.get<"instrumentType">().present());
        REQUIRE_FALSE(decoded.get<"state">().present());
        REQUIRE_EQ(decoded.get<"exchangeSymbol">().value(), std::string_view{});
    }

    TEST_CASE("ServiceRequest: all filters empty means all services") {
        std::array<std::byte, 128> buffer{};

        auto msg = turbofw::schema::metadata::ServiceRequest::wrapAndApplyHeader(buffer);
        msg.get<"appId">().value(17);
        msg.get<"requestId">().value(5);
        msg.get<"serviceId">().reset();
        msg.get<"state">().reset();
        msg.get<"name">().value("");

        auto header = turbofw::schema::metadata::MessageHeader{buffer.data()};
        auto decoded = turbofw::schema::metadata::ServiceRequest{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_FALSE(decoded.get<"serviceId">().present());
        REQUIRE_FALSE(decoded.get<"state">().present());
        REQUIRE_EQ(decoded.get<"name">().value(), std::string_view{});
    }

    TEST_CASE("ServiceDefinition round trip") {
        std::array<std::byte, 256> buffer{};

        auto msg = turbofw::schema::metadata::ServiceDefinition::wrapAndApplyHeader(buffer);
        msg.get<"appId">().value(17); // response to ServiceRequest
        msg.get<"requestId">().value(5);
        msg.get<"serviceId">().value(3);
        msg.get<"version">().value(4);
        msg.get<"reportedTs">().value(1'775'000'000'000'000'100);
        msg.get<"state">().value(turbofw::schema::metadata::ServiceState::Active);
        msg.get<"name">().value("md-binance-1");
        msg.get<"dataQueue">().value("md-binance-1/data");
        msg.get<"requestQueue">().value("md-binance-1/req");

        auto header = turbofw::schema::metadata::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"templateId">().value(), turbofw::schema::metadata::ServiceDefinition::sbeTemplateId());
        auto decoded = turbofw::schema::metadata::ServiceDefinition{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"serviceId">().value(), 3);
        REQUIRE_EQ(decoded.get<"version">().value(), 4);
        REQUIRE(decoded.get<"state">().value() == turbofw::schema::metadata::ServiceState::Active);
        REQUIRE_EQ(decoded.get<"name">().value(), std::string_view{"md-binance-1"});
        REQUIRE_EQ(decoded.get<"dataQueue">().value(), std::string_view{"md-binance-1/data"});
        REQUIRE_EQ(decoded.get<"requestQueue">().value(), std::string_view{"md-binance-1/req"});
    }

    TEST_CASE("RequestComplete terminates the request response") {
        std::array<std::byte, 64> buffer{};

        auto msg = turbofw::schema::metadata::RequestComplete::wrapAndApplyHeader(buffer);
        msg.get<"appId">().value(17);
        msg.get<"requestId">().value(5);
        msg.get<"status">().value(turbofw::schema::metadata::RequestStatus::Ok);
        msg.get<"count">().value(2);

        auto header = turbofw::schema::metadata::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), turbofw::schema::metadata::RequestComplete::sbeSchemaId());
        auto decoded = turbofw::schema::metadata::RequestComplete{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE(decoded.get<"status">().value() == turbofw::schema::metadata::RequestStatus::Ok);
        REQUIRE_EQ(decoded.get<"count">().value(), 2);
    }
}

} // namespace turbofw::testing
