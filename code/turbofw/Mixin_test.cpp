// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include <turbofw/schema/market_data/schema.h>
#include <turbofw/schema/service/schema.h>
#include <turbofw/schema/trading/schema.h>

#include "Error.h"
#include "mixin/ReceiveMixin.h"
#include "mixin/SBEReceiveMixin.h"
#include "mixin/SBESendMixin.h"
#include "mixin/SendMixin.h"
#include "testing/TestQueue.h"
#include <turbofw/mp/List.h>

namespace turbofw::testing {
namespace {

struct TestProducer : public testing::Producer, public mixin::SendMixin {
    using testing::Producer::Producer;
};

struct TestConsumer : public testing::Consumer, public mixin::ReceiveMixin {
    using testing::Consumer::Consumer;
};

struct TestSBEProducer : public testing::Producer, public mixin::SBESendMixin {
    using testing::Producer::Producer;
};

/// A send-hook mixin: stamps `appId` into every message that carries the field, before the user callback
/// has its say. Generic over the message type, so it works for both send() overloads.
struct AppIdStampMixin {
    template <typename M>
    auto updateBody(M msg) -> void {
        if constexpr (requires { msg.template get<"appId">(); }) {
            msg.template get<"appId">().value(7);
        }
    }
};

/// Producer with the stamping mixin composed into `Mixins`: hook runs on send()
struct StampingProducer : public testing::Producer, public mixin::SBESendMixin, public AppIdStampMixin {
    using testing::Producer::Producer;

    using Mixins = mp::List<AppIdStampMixin>;
};

auto const kNoOpReceive = [](std::span<std::byte const>) {
    FAIL("receive() found a buffer where none was expected");
};

} // namespace

TEST_SUITE("Mixin") {
    TEST_CASE("send/receive round trip, receive() drains") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        CHECK_EQ(consumer.receive(kNoOpReceive), 0);

        auto const payloads = std::vector{std::uint64_t{0xdead}, std::uint64_t{0xbeef}, std::uint64_t{0xcafe}};
        for (auto const payload : payloads) {
            auto const sent = producer.send(sizeof(payload), [&payload](std::span<std::byte> buffer) {
                REQUIRE(buffer.size() >= sizeof(payload));
                std::memcpy(buffer.data(), &payload, sizeof(payload));
            });
            REQUIRE(sent);
        }

        auto received = std::vector<std::uint64_t>{};
        auto const count = consumer.receive([&received](std::span<std::byte const> buffer) {
            REQUIRE(buffer.size() >= sizeof(std::uint64_t));
            auto payload = std::uint64_t{};
            std::memcpy(&payload, buffer.data(), sizeof(payload));
            received.push_back(payload);
        });
        CHECK_EQ(count, payloads.size());
        REQUIRE(received == payloads);

        CHECK_EQ(consumer.receive(kNoOpReceive), 0);
    }

    TEST_CASE("send() on a full queue returns ENOBUFS") {
        auto constexpr kLength = 2;

        auto queue = testing::Queue{};
        queue.maxSize = kLength;
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        auto constexpr kPayload = std::uint8_t{42};
        auto constexpr kFill = [](std::span<std::byte> buffer) {
            buffer.front() = std::byte{kPayload};
        };

        // the queue is `kLength` slots deep: two sends succeed, the third one fails
        for (auto index = 0; index < kLength; ++index) {
            auto const fill = producer.send(sizeof(kPayload), kFill);
            REQUIRE(fill);
        }

        auto const overfill = producer.send(sizeof(kPayload), kFill);
        REQUIRE_FALSE(overfill);
        CHECK_EQ(overfill.error().value(), ENOBUFS);

        // after the queue is drained, sending works again
        CHECK_EQ(consumer.receive([](std::span<std::byte const> buffer) {
            REQUIRE_EQ(buffer.front(), std::byte{kPayload});
        }),
            kLength);
        auto const refill = producer.send(sizeof(kPayload), [](std::span<std::byte> buffer) {
            buffer.front() = std::byte{kPayload + 1};
        });
        REQUIRE(refill);
    }
}

TEST_SUITE("SBESendMixin") {
    TEST_CASE("fixed message: send() without sizes, round trip") {
        auto queue = testing::Queue{};
        auto producer = TestSBEProducer{&queue};
        auto consumer = TestConsumer{&queue};

        CHECK_EQ(consumer.receive(kNoOpReceive), 0);

        auto constexpr kTimestamp = std::int64_t{222};
        auto const sent = producer.send<service::Heartbeat>([&](service::Heartbeat msg) {
            msg.get<"ts">().value(kTimestamp);
            msg.get<"serviceId">().value(1);
            msg.get<"flags">().value(service::ServiceFlags{}.start(true));
        });
        REQUIRE(sent);

        auto const count = consumer.receive([&](std::span<std::byte const> buffer) {
            auto const header = service::MessageHeader{const_cast<std::byte*>(buffer.data())};
            REQUIRE_EQ(header.get<"schemaId">().value(), service::Heartbeat::sbeSchemaId());
            REQUIRE_EQ(header.get<"templateId">().value(), service::Heartbeat::sbeTemplateId());
            auto msg = service::Heartbeat{std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()},
                header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
            REQUIRE_EQ(msg.get<"serviceId">().value(), 1u);
            REQUIRE_EQ(msg.get<"ts">().value(), kTimestamp);
            auto flags = msg.get<"flags">().value();
            REQUIRE(flags.start());
        });
        CHECK_EQ(count, 1);
        CHECK_EQ(consumer.receive(kNoOpReceive), 0);
    }

    TEST_CASE("group message: send() with sizes, round trip") {
        auto queue = testing::Queue{};
        auto producer = TestSBEProducer{&queue};
        auto consumer = TestConsumer{&queue};

        CHECK_EQ(consumer.receive(kNoOpReceive), 0);

        auto const sent = producer.send<market_data::Snapshot>({2u, 1u, 1u}, [](market_data::Snapshot msg) {
            auto header = msg.get<"header">();
            header.get<"instrumentId">().value(42);
            header.get<"flags">().value(market_data::MdFlags{}.snapshot(true).mandatory(true));
            msg.get<"appId">().value(17);
            msg.get<"requestId">().value(3);

            auto bids = msg.get<"bids">(); // descending price
            bids.reset(2);
            for (std::int64_t i = 0; bids.hasNext(); ++i) {
                auto& level = bids.next();
                level.get<"price">().get<"mantissa">().value(100 - i);
                level.get<"qty">().value(10 + i);
                level.get<"orders">().value(1);
            }
            auto asks = msg.get<"asks">(); // ascending price
            asks.reset(1);
            auto& ask = asks.next();
            ask.get<"price">().get<"mantissa">().value(101);
            ask.get<"qty">().value(7);
            ask.get<"orders">().reset();
        });
        REQUIRE(sent);

        auto const count = consumer.receive([&](std::span<std::byte const> buffer) {
            auto const header = market_data::MessageHeader{const_cast<std::byte*>(buffer.data())};
            REQUIRE_EQ(header.get<"schemaId">().value(), market_data::Snapshot::sbeSchemaId());
            REQUIRE_EQ(header.get<"templateId">().value(), market_data::Snapshot::sbeTemplateId());
            auto msg = market_data::Snapshot{std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()},
                header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
            REQUIRE_EQ(msg.get<"header">().get<"instrumentId">().value(), 42u);
            auto decodedFlags = msg.get<"header">().get<"flags">().value();
            REQUIRE(decodedFlags.snapshot());
            REQUIRE(decodedFlags.mandatory());

            auto decodedBids = msg.get<"bids">();
            REQUIRE_EQ(decodedBids.count(), 2);
            REQUIRE_EQ(decodedBids.next().get<"price">().get<"mantissa">().value(), 100);
            REQUIRE_EQ(decodedBids.next().get<"qty">().value(), 11);
            auto decodedAsks = msg.get<"asks">();
            REQUIRE_EQ(decodedAsks.count(), 1);
            auto decodedAsk = decodedAsks.next();
            REQUIRE_EQ(decodedAsk.get<"price">().get<"mantissa">().value(), 101);
            REQUIRE_FALSE(decodedAsk.get<"orders">().present());
        });
        CHECK_EQ(count, 1);
        CHECK_EQ(consumer.receive(kNoOpReceive), 0);
    }

    TEST_CASE("data fields: send() with sizes, round trip") {
        auto queue = testing::Queue{};
        auto producer = TestSBEProducer{&queue};
        auto consumer = TestConsumer{&queue};

        auto constexpr kClientCode = "ALPHA";
        auto constexpr kAccount = "SPOT-ACC";
        auto const sent = producer.send<trading::CreateContext>(
            {std::char_traits<char>::length(kClientCode), std::char_traits<char>::length(kAccount)},
            [](trading::CreateContext msg) {
                msg.get<"appId">().value(17);
                msg.get<"requestId">().value(3);
                msg.get<"instrumentId">().value(42);
                msg.get<"clientCode">().value("ALPHA");
                msg.get<"account">().value("SPOT-ACC");
            });
        REQUIRE(sent);

        auto const count = consumer.receive([&](std::span<std::byte const> buffer) {
            auto const header = trading::MessageHeader{const_cast<std::byte*>(buffer.data())};
            REQUIRE_EQ(header.get<"schemaId">().value(), trading::CreateContext::sbeSchemaId());
            REQUIRE_EQ(header.get<"templateId">().value(), trading::CreateContext::sbeTemplateId());
            auto msg =
                trading::CreateContext{std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()},
                    header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
            REQUIRE_EQ(msg.get<"appId">().value(), 17u);
            REQUIRE_EQ(msg.get<"requestId">().value(), 3u);
            REQUIRE_EQ(msg.get<"instrumentId">().value(), 42u);
            REQUIRE_EQ(msg.get<"clientCode">().value(), std::string_view{kClientCode});
            REQUIRE_EQ(msg.get<"account">().value(), std::string_view{kAccount});
        });
        CHECK_EQ(count, 1);
        CHECK_EQ(consumer.receive(kNoOpReceive), 0);
    }

    TEST_CASE("send hook updateBody(): stamps fields, both send() overloads") {
        auto queue = testing::Queue{};
        auto producer = StampingProducer{&queue};
        auto consumer = TestConsumer{&queue};

        // the fixed-message overload: the hook stamps clientId=7 (SetLogLevel), the user callback leaves it
        // alone -- a zeroed buffer otherwise, so 7 proves the hook encoded into the buffer
        auto const fixed = producer.send<service::SetLogLevel>([](service::SetLogLevel msg) {
            msg.get<"level">().value(service::LogLevel::Trace);
        });
        REQUIRE(fixed);

        auto const count = consumer.receive([&](std::span<std::byte const> buffer) {
            auto const header = service::MessageHeader{const_cast<std::byte*>(buffer.data())};
            REQUIRE_EQ(header.get<"templateId">().value(), service::SetLogLevel::sbeTemplateId());
            auto msg = service::SetLogLevel{std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()},
                header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
            CHECK_EQ(msg.get<"appId">().value(), 7u); // stamped by the hook, never touched by the callback
            CHECK(msg.get<"level">().value() == service::LogLevel::Trace);
        });
        CHECK_EQ(count, 1);

        // the vars overload (repeating groups): the same generic hook still runs
        auto const groups = producer.send<market_data::Snapshot>({2u, 1u, 1u}, [](market_data::Snapshot msg) {
            auto header = msg.get<"header">();
            header.get<"instrumentId">().value(42);
            header.get<"flags">().value(market_data::MdFlags{}.snapshot(true));
            auto bids = msg.get<"bids">();
            bids.reset(2);
            for (std::int64_t i = 0; bids.hasNext(); ++i) {
                auto& level = bids.next();
                level.get<"price">().get<"mantissa">().value(100 - i);
                level.get<"qty">().value(10 + i);
                level.get<"orders">().value(1);
            }
            auto asks = msg.get<"asks">();
            asks.reset(1);
            auto& ask = asks.next();
            ask.get<"price">().get<"mantissa">().value(101);
            ask.get<"qty">().value(7);
            ask.get<"orders">().reset();
        });
        REQUIRE(groups);

        auto const snapshotCount = consumer.receive([&](std::span<std::byte const> buffer) {
            auto const header = market_data::MessageHeader{const_cast<std::byte*>(buffer.data())};
            REQUIRE_EQ(header.get<"templateId">().value(), market_data::Snapshot::sbeTemplateId());
            auto msg = market_data::Snapshot{std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()},
                header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
            CHECK_EQ(msg.get<"appId">().value(), 7u); // stamped by the hook through the vars overload
            CHECK_EQ(msg.get<"header">().get<"instrumentId">().value(), 42u);
        });
        CHECK_EQ(snapshotCount, 1);
    }
}

} // namespace turbofw::testing
