// Copyright (c) Sergey Kovalevych <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <array>
#include <cstddef>

#include <doctest/doctest.h>

#include <turbofw/svc/schema.h>

namespace turbofw::testing {

TEST_SUITE("ServiceSchema") {
    TEST_CASE("Heartbeat spontaneous round trip") {
        std::array<std::byte, 128> buffer{};

        auto msg = svc::Heartbeat::wrapAndApplyHeader(buffer);
        msg.get<"clientId">().reset(); // spontaneous heartbeat: no addressee
        msg.get<"requestId">().value(0);
        msg.get<"serviceId">().value(3);
        msg.get<"startTs">().value(1'775'000'000'000);
        msg.get<"ts">().value(1'775'000'000'100);
        msg.get<"intervalMs">().value(100);
        msg.get<"flags">().value(svc::ServiceFlags{}.start(true).notConnected(true));

        auto header = svc::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), svc::Heartbeat::sbeSchemaId());
        REQUIRE_EQ(header.get<"templateId">().value(), svc::Heartbeat::sbeTemplateId());
        auto decoded = svc::Heartbeat{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_FALSE(decoded.get<"clientId">().present());
        REQUIRE_EQ(decoded.get<"requestId">().value(), 0);
        REQUIRE(decoded.get<"serviceId">().present());
        REQUIRE(decoded.get<"serviceId">().value() == 3);
        REQUIRE_EQ(decoded.get<"startTs">().value(), 1'775'000'000'000);
        REQUIRE_EQ(decoded.get<"ts">().value(), 1'775'000'000'100);
        REQUIRE_EQ(decoded.get<"intervalMs">().value(), 100);
        auto flags = decoded.get<"flags">().value();
        REQUIRE(flags.start());
        REQUIRE(flags.notConnected()); // state: kept on every heartbeat while the condition holds
        REQUIRE_FALSE(flags.stop());
        REQUIRE_FALSE(flags.recovering());
    }

    TEST_CASE("Heartbeat response echoes HeartbeatRequest ids") {
        std::array<std::byte, 128> requestBuffer{};

        auto request = svc::HeartbeatRequest::wrapAndApplyHeader(requestBuffer);
        request.get<"clientId">().value(12);
        request.get<"requestId">().value(34);

        auto requestHeader = svc::MessageHeader{requestBuffer.data()};
        REQUIRE_EQ(requestHeader.get<"schemaId">().value(), svc::HeartbeatRequest::sbeSchemaId());
        REQUIRE_EQ(requestHeader.get<"templateId">().value(), svc::HeartbeatRequest::sbeTemplateId());
        auto decodedRequest = svc::HeartbeatRequest{requestBuffer, requestHeader.sbeEncodedLength(),
            requestHeader.get<"blockLength">().value(), requestHeader.get<"version">().value()};
        REQUIRE_EQ(decodedRequest.get<"clientId">().value(), 12);
        REQUIRE_EQ(decodedRequest.get<"requestId">().value(), 34);

        std::array<std::byte, 128> responseBuffer{};
        auto response = svc::Heartbeat::wrapAndApplyHeader(responseBuffer);
        response.get<"clientId">().value(decodedRequest.get<"clientId">().value());
        response.get<"requestId">().value(decodedRequest.get<"requestId">().value());
        response.get<"serviceId">().reset(); // service without id
        response.get<"startTs">().value(1'775'000'000'000);
        response.get<"ts">().value(1'775'000'000'200);
        response.get<"intervalMs">().value(0); // no periodic heartbeats
        response.get<"flags">().value(svc::ServiceFlags{});

        auto responseHeader = svc::MessageHeader{responseBuffer.data()};
        auto decodedResponse = svc::Heartbeat{responseBuffer, responseHeader.sbeEncodedLength(),
            responseHeader.get<"blockLength">().value(), responseHeader.get<"version">().value()};
        REQUIRE(decodedResponse.get<"clientId">().present());
        REQUIRE_EQ(decodedResponse.get<"clientId">().value(), 12);
        REQUIRE_EQ(decodedResponse.get<"requestId">().value(), 34);
        REQUIRE_FALSE(decodedResponse.get<"serviceId">().present());
        REQUIRE_EQ(decodedResponse.get<"intervalMs">().value(), 0);
        auto flags = decodedResponse.get<"flags">().value();
        REQUIRE_FALSE(flags.start());
        REQUIRE_FALSE(flags.stop());
    }

    TEST_CASE("SetLogLevel / SetLogLevelResult round trip") {
        std::array<std::byte, 64> buffer{};

        auto request = svc::SetLogLevel::wrapAndApplyHeader(buffer);
        request.get<"clientId">().value(12);
        request.get<"requestId">().value(1);
        request.get<"level">().value(svc::LogLevel::Warn);

        auto header = svc::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), svc::SetLogLevel::sbeSchemaId());
        REQUIRE_GE(svc::SetLogLevel::sbeTemplateId(), 100u);
        auto decoded = svc::SetLogLevel{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"clientId">().value(), 12);
        REQUIRE(decoded.get<"level">().value() == svc::LogLevel::Warn);

        auto result = svc::SetLogLevelResult::wrapAndApplyHeader(buffer);
        result.get<"clientId">().value(12);
        result.get<"requestId">().value(1);
        result.get<"status">().value(svc::RequestStatus::Ok);
        result.get<"level">().value(svc::LogLevel::Debug);

        auto resultHeader = svc::MessageHeader{buffer.data()};
        REQUIRE_EQ(resultHeader.get<"templateId">().value(), svc::SetLogLevelResult::sbeTemplateId());
        auto decodedResult = svc::SetLogLevelResult{buffer, resultHeader.sbeEncodedLength(),
            resultHeader.get<"blockLength">().value(), resultHeader.get<"version">().value()};
        REQUIRE(decodedResult.get<"status">().value() == svc::RequestStatus::Ok);
        REQUIRE(decodedResult.get<"level">().value() == svc::LogLevel::Debug);
    }
}

} // namespace turbofw::testing
