// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <doctest/doctest.h>

#include "mixin/ReceiveMixin.h"
#include "mixin/SBESendMixin.h"
#include "mixin/SBEServiceIdStampMixin.h"
#include "testing/TestQueue.h"
#include <turbofw/mp/List.h>
#include <turbofw/schema/market_data/schema.h>
#include <turbofw/schema/service/schema.h>

namespace turbofw::testing {
namespace {

struct TestProducer : public testing::Producer, public mixin::SBESendMixin, public mixin::SBEServiceIdStampMixin {
    using testing::Producer::Producer;

    using Mixins = mp::List<mixin::SBEServiceIdStampMixin>;
};

struct TestConsumer : public testing::Consumer, public mixin::ReceiveMixin {
    using testing::Consumer::Consumer;
};

/// Decodes the raw serviceId of a service::Heartbeat carried by a queue buffer (0xFFFF is the null sentinel)
auto decodedServiceId(std::span<std::byte const> buffer) -> std::uint16_t {
    auto const header = service::MessageHeader{const_cast<std::byte*>(buffer.data())};
    REQUIRE_EQ(header.get<"templateId">().value(), service::Heartbeat::sbeTemplateId());
    auto const body = std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()};
    auto msg = service::Heartbeat{
        body, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
    return msg.get<"serviceId">().value();
}

/// Drains the queue and returns the serviceIds of the messages in it
auto drainedServiceIds(TestConsumer& consumer) -> std::vector<std::uint16_t> {
    auto result = std::vector<std::uint16_t>{};
    consumer.receive([&](std::span<std::byte const> buffer) {
        result.push_back(decodedServiceId(buffer));
    });
    return result;
}

} // namespace

TEST_SUITE("SBEServiceIdStampMixin") {
    TEST_CASE("a null serviceId by default, stamped as null into outgoing messages") {
        auto const mixin = mixin::SBEServiceIdStampMixin{};
        CHECK_FALSE(mixin.serviceId().hasValue());

        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        (void)producer.send<service::Heartbeat>([](service::Heartbeat msg) {
            msg.get<"ts">().value(1);
            msg.get<"flags">().value(service::ServiceFlags{});
        });

        auto const serviceIds = drainedServiceIds(consumer);
        REQUIRE_EQ(serviceIds.size(), 1);
        CHECK_EQ(serviceIds[0], std::numeric_limits<std::uint16_t>::max()); // the null sentinel on the wire
    }

    TEST_CASE("setServiceId pins the value and mid-flight changes apply to subsequent sends") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        producer.setServiceId(ServiceId{7});
        CHECK_EQ(producer.serviceId().value_of(), 7u);

        (void)producer.send<service::Heartbeat>([](service::Heartbeat msg) {
            msg.get<"ts">().value(1);
            msg.get<"flags">().value(service::ServiceFlags{});
        });
        producer.setServiceId(ServiceId{9});
        (void)producer.send<service::Heartbeat>([](service::Heartbeat msg) {
            msg.get<"ts">().value(2);
            msg.get<"flags">().value(service::ServiceFlags{});
        });

        auto const serviceIds = drainedServiceIds(consumer);
        REQUIRE_EQ(serviceIds.size(), 2);
        CHECK_EQ(serviceIds[0], 7u);
        CHECK_EQ(serviceIds[1], 9u); // the change applies to subsequent sends
    }

    TEST_CASE("a message without the serviceId field is not touched") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        producer.setServiceId(ServiceId{42});
        (void)producer.send<market_data::SnapshotRequest>([](market_data::SnapshotRequest msg) {
            msg.get<"depth">().value(10);
        });

        auto depths = std::vector<int>{};
        consumer.receive([&](std::span<std::byte const> buffer) {
            auto const header = service::MessageHeader{const_cast<std::byte*>(buffer.data())};
            auto const body = std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()};
            auto msg = market_data::SnapshotRequest{
                body, header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
            depths.push_back(msg.get<"depth">().value());
        });

        REQUIRE_EQ(depths.size(), 1);
        CHECK_EQ(depths[0], 10);                         // the snapshot request went out exactly as sent
        CHECK_EQ(drainedServiceIds(consumer).size(), 0); // and no serviceId-carrying message was sent
    }
}

} // namespace turbofw::testing
