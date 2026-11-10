// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <limits>
#include <span>
#include <unordered_set>
#include <vector>

#include <doctest/doctest.h>

#include "mixin/ReceiveMixin.h"
#include "mixin/SBERequestIdStampMixin.h"
#include "mixin/SBESendMixin.h"
#include "testing/TestQueue.h"
#include <turbofw/md/schema.h>
#include <turbofw/mp/List.h>
#include <turbofw/svc/schema.h>

namespace turbofw::testing {
namespace {

struct TestProducer : public testing::Producer, public mixin::SBESendMixin, public mixin::SBERequestIdStampMixin {
    using testing::Producer::Producer;

    using Mixins = mp::List<mixin::SBERequestIdStampMixin>;
};

struct TestConsumer : public testing::Consumer, public mixin::ReceiveMixin {
    using testing::Consumer::Consumer;
};

auto decodedRequestId(std::span<std::byte const> buffer) -> std::uint64_t {
    auto const header = svc::MessageHeader{const_cast<std::byte*>(buffer.data())};
    auto const body = std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()};
    auto const blockLength = header.get<"blockLength">().value();
    auto const version = header.get<"version">().value();
    switch (header.get<"templateId">().value()) {
    case svc::SetLogLevel::sbeTemplateId():
        return svc::SetLogLevel{body, header.sbeEncodedLength(), blockLength, version}.get<"requestId">().value();
    case md::SnapshotRequest::sbeTemplateId():
        return md::SnapshotRequest{body, header.sbeEncodedLength(), blockLength, version}.get<"requestId">().value();
    default: return 0;
    }
}

/// Drains the queue and returns the requestIds of the messages in it
auto drainedRequestIds(TestConsumer& consumer) -> std::vector<std::uint64_t> {
    auto result = std::vector<std::uint64_t>{};
    consumer.receive([&](std::span<std::byte const> buffer) {
        result.push_back(decodedRequestId(buffer));
    });
    return result;
}

} // namespace

TEST_SUITE("SBERequestIdStampMixin") {
    TEST_CASE("a fresh random requestId on every message, never the null sentinel") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        producer.send<svc::SetLogLevel>([](svc::SetLogLevel msg) {
            msg.get<"level">().value(svc::LogLevel::Info);
        });
        producer.send<svc::SetLogLevel>([](svc::SetLogLevel msg) {
            msg.get<"level">().value(svc::LogLevel::Info);
        });

        auto const ids = drainedRequestIds(consumer);
        REQUIRE_EQ(ids.size(), 2);
        auto constexpr nullId = std::numeric_limits<std::uint64_t>::max();
        CHECK_NE(ids[0], nullId);
        CHECK_NE(ids[1], nullId);
        CHECK_NE(ids[0], ids[1]); // every message gets its own id
    }

    TEST_CASE("ids are unique across many messages and across schemas") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        for (auto i = 0; i != 128; ++i) {
            producer.send<svc::SetLogLevel>([](svc::SetLogLevel msg) {
                msg.get<"level">().value(svc::LogLevel::Info);
            });
            producer.send<md::SnapshotRequest>([](md::SnapshotRequest msg) {
                msg.get<"depth">().value(10);
            });
        }

        auto const ids = drainedRequestIds(consumer);
        REQUIRE_EQ(ids.size(), 256);

        auto const unique = std::unordered_set<std::uint64_t>{ids.cbegin(), ids.cend()};
        CHECK_EQ(unique.size(), ids.size());
        CHECK_EQ(unique.count(std::numeric_limits<std::uint64_t>::max()), 0); // never the null sentinel
    }

    TEST_CASE("a message without the requestId field is not touched") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        producer.send<svc::Heartbeat>([](svc::Heartbeat msg) {
            msg.get<"ts">().value(1);
            msg.get<"flags">().value(svc::ServiceFlags{}.start(true));
        });

        auto result = std::vector<svc::ServiceFlags>{};
        consumer.receive([&](std::span<std::byte const> buffer) {
            auto const header = svc::MessageHeader{const_cast<std::byte*>(buffer.data())};
            REQUIRE_EQ(header.get<"templateId">().value(), svc::Heartbeat::sbeTemplateId());
            auto msg = svc::Heartbeat{std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()},
                header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
            result.push_back(msg.get<"flags">().value());
        });

        REQUIRE_EQ(result.size(), 1);
        CHECK(result[0].start()); // the heartbeat went out exactly as sent
        CHECK_FALSE(result[0].stop());
        CHECK_EQ(drainedRequestIds(consumer).size(), 0);
    }
}

} // namespace turbofw::testing
