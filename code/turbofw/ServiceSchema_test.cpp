// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
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
        msg.get<"timestamp">().value(1'775'000'000'100);
        msg.get<"serviceId">().value(3);
        msg.get<"flags">().value(svc::ServiceFlags{}.start(true).notConnected(true));

        auto header = svc::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), svc::Heartbeat::sbeSchemaId());
        REQUIRE_EQ(header.get<"templateId">().value(), svc::Heartbeat::sbeTemplateId());
        auto decoded = svc::Heartbeat{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE(decoded.get<"serviceId">().present());
        REQUIRE(decoded.get<"serviceId">().value() == 3);
        REQUIRE_EQ(decoded.get<"timestamp">().value(), 1'775'000'000'100);
        auto flags = decoded.get<"flags">().value();
        REQUIRE(flags.start());
        REQUIRE(flags.notConnected()); // state: kept on every heartbeat while the condition holds
        REQUIRE_FALSE(flags.stop());
        REQUIRE_FALSE(flags.recovering());
    }

    TEST_CASE("Heartbeat without id") {
        std::array<std::byte, 128> buffer{};

        auto msg = svc::Heartbeat::wrapAndApplyHeader(buffer);
        msg.get<"timestamp">().value(1'775'000'000'200);
        msg.get<"serviceId">().reset(); // service without id
        msg.get<"flags">().value(svc::ServiceFlags{});

        auto header = svc::MessageHeader{buffer.data()};
        auto decoded = svc::Heartbeat{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_FALSE(decoded.get<"serviceId">().present());
        REQUIRE_EQ(decoded.get<"timestamp">().value(), 1'775'000'000'200);
        auto flags = decoded.get<"flags">().value();
        REQUIRE_FALSE(flags.start());
        REQUIRE_FALSE(flags.stop());
    }

    TEST_CASE("SetLogLevel / SetLogLevelResult round trip") {
        std::array<std::byte, 64> buffer{};

        auto request = svc::SetLogLevel::wrapAndApplyHeader(buffer);
        request.get<"appId">().value(12);
        request.get<"requestId">().value(1);
        request.get<"level">().value(svc::LogLevel::Warn);

        auto header = svc::MessageHeader{buffer.data()};
        REQUIRE_EQ(header.get<"schemaId">().value(), svc::SetLogLevel::sbeSchemaId());
        REQUIRE_GE(svc::SetLogLevel::sbeTemplateId(), 100u);
        auto decoded = svc::SetLogLevel{
            buffer, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
        REQUIRE_EQ(decoded.get<"appId">().value(), 12);
        REQUIRE(decoded.get<"level">().value() == svc::LogLevel::Warn);

        auto result = svc::SetLogLevelResult::wrapAndApplyHeader(buffer);
        result.get<"appId">().value(12);
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
