// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <doctest/doctest.h>

#include "mixin/ReceiveMixin.h"
#include "mixin/SBEAppIdStampMixin.h"
#include "mixin/SBESendMixin.h"
#include "testing/TestQueue.h"
#include <turbofw/md/schema.h>
#include <turbofw/mp/List.h>
#include <turbofw/svc/schema.h>

namespace turbofw::testing {
namespace {

struct TestProducer : public testing::Producer, public mixin::SBESendMixin, public mixin::SBEAppIdStampMixin {
    using testing::Producer::Producer;

    using Mixins = mp::List<mixin::SBEAppIdStampMixin>;
};

struct TestConsumer : public testing::Consumer, public mixin::ReceiveMixin {
    using testing::Consumer::Consumer;
};

/// Decodes the appId of a SetLogLevel, a SnapshotRequest or a RequestComplete carried by a queue buffer
auto decodedAppId(std::span<std::byte const> buffer) -> std::uint32_t {
    auto const header = svc::MessageHeader{const_cast<std::byte*>(buffer.data())};
    auto const body = std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()};
    auto const blockLength = header.get<"blockLength">().value();
    auto const version = header.get<"version">().value();
    switch (header.get<"templateId">().value()) {
    case svc::SetLogLevel::sbeTemplateId():
        return svc::SetLogLevel{body, header.sbeEncodedLength(), blockLength, version}.get<"appId">().value();
    case md::SnapshotRequest::sbeTemplateId():
        return md::SnapshotRequest{body, header.sbeEncodedLength(), blockLength, version}.get<"appId">().value();
    default: return 0;
    }
}

/// Drains the queue and returns the appIds of the messages in it
auto drainedAppIds(TestConsumer& consumer) -> std::vector<std::uint32_t> {
    auto result = std::vector<std::uint32_t>{};
    consumer.receive([&](std::span<std::byte const> buffer) {
        result.push_back(decodedAppId(buffer));
    });
    return result;
}

} // namespace

TEST_SUITE("SBEAppIdStampMixin") {
    TEST_CASE("a fresh random appId by default, never the null sentinel") {
        for (auto i = 0; i != 1000; ++i) {
            auto const mixin = mixin::SBEAppIdStampMixin{};
            CHECK_NE(mixin.appId().value_of(), std::numeric_limits<std::uint32_t>::max());
        }
    }

    TEST_CASE("the default random value lands in outgoing messages") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        producer.send<svc::SetLogLevel>([](svc::SetLogLevel msg) {
            msg.get<"level">().value(svc::LogLevel::Info);
        });

        auto const appIds = drainedAppIds(consumer);
        REQUIRE_EQ(appIds.size(), 1);
        CHECK_EQ(appIds[0], producer.appId().value_of());
    }

    TEST_CASE("setAppId pins the value across schemas and mid-flight changes") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        producer.setAppId(AppId{42});
        CHECK_EQ(producer.appId().value_of(), 42u);

        producer.send<svc::SetLogLevel>([](svc::SetLogLevel msg) {
            msg.get<"level">().value(svc::LogLevel::Info);
        });
        producer.send<md::SnapshotRequest>([](md::SnapshotRequest msg) {
            msg.get<"depth">().value(10);
        });
        producer.setAppId(AppId{43});
        producer.send<md::SnapshotRequest>([](md::SnapshotRequest msg) {
            msg.get<"depth">().value(10);
        });

        auto const appIds = drainedAppIds(consumer);
        REQUIRE_EQ(appIds.size(), 3);
        CHECK_EQ(appIds[0], 42u); // svc schema, required appId
        CHECK_EQ(appIds[1], 42u); // md schema, required appId
        CHECK_EQ(appIds[2], 43u); // the change applies to subsequent sends
    }

    TEST_CASE("a message without the appId field is not touched") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        producer.setAppId(AppId{42});
        producer.send<svc::Heartbeat>([](svc::Heartbeat msg) {
            msg.get<"timestamp">().value(1);
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
        CHECK_EQ(drainedAppIds(consumer).size(), 0); // and drainedAppIds sees no appId-carrying message
    }
}

} // namespace turbofw::testing
