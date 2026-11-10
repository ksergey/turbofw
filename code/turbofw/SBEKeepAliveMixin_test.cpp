// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <chrono>
#include <cstdint>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "Error.h"
#include "mixin/ReceiveMixin.h"
#include "mixin/SBEKeepAliveMixin.h"
#include "mixin/SBESendMixin.h"
#include "testing/TestQueue.h"
#include <turbofw/mp/List.h>
#include <turbofw/svc/schema.h>

namespace turbofw::testing {
namespace {

struct TestProducer : public testing::Producer, public mixin::SBESendMixin, public mixin::SBEKeepAliveMixin {
    using testing::Producer::Producer;

    using Mixins = mp::List<mixin::SBEKeepAliveMixin>;
};

struct TestConsumer : public testing::Consumer, public mixin::ReceiveMixin {
    using testing::Consumer::Consumer;
};

/// Decodes the flag set of the Heartbeat carried by a queue buffer
auto decodeHeartbeat(std::span<std::byte const> buffer) -> svc::ServiceFlags {
    auto const header = svc::MessageHeader{const_cast<std::byte*>(buffer.data())};
    auto msg = svc::Heartbeat{std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()},
        header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
    return msg.get<"flags">().value();
}

/// Drains the queue and returns the flag sets of the Heartbeat messages in it (other message types are discarded)
auto drainedHeartbeatFlags(TestConsumer& consumer) -> std::vector<svc::ServiceFlags> {
    std::vector<svc::ServiceFlags> result;
    consumer.receive([&](std::span<std::byte const> buffer) {
        auto const header = svc::MessageHeader{const_cast<std::byte*>(buffer.data())};
        if (header.get<"templateId">().value() == svc::Heartbeat::sbeTemplateId()) {
            result.push_back(decodeHeartbeat(buffer));
        }
    });
    return result;
}

} // namespace

TEST_SUITE("SBEKeepAliveMixin") {
    TEST_CASE("heartbeating is off by default, keepAlive never publishes") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        producer.keepAlive();

        CHECK_EQ(drainedHeartbeatFlags(consumer).size(), 0);
    }

    TEST_CASE("start publishes a Start heartbeat, keepAlive stays silent within the interval") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        producer.startHeartbeating(std::chrono::milliseconds{1});
        {
            auto beats = drainedHeartbeatFlags(consumer);
            REQUIRE_EQ(beats.size(), 1);
            CHECK(beats[0].start());
            CHECK_FALSE(beats[0].stop());
        }

        producer.keepAlive();
        CHECK_EQ(drainedHeartbeatFlags(consumer).size(), 0); // the interval has not elapsed yet

        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        producer.keepAlive();
        {
            auto beats = drainedHeartbeatFlags(consumer);
            REQUIRE_EQ(beats.size(), 1);
            CHECK_FALSE(beats[0].start()); // a silence heartbeat carries no event flags
            CHECK_FALSE(beats[0].stop());
        }

        producer.keepAlive();
        CHECK_EQ(drainedHeartbeatFlags(consumer).size(), 0); // the publication restarted the silence timer
    }

    TEST_CASE("sends of other messages keep the heartbeat silent") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        producer.startHeartbeating(std::chrono::milliseconds{1});
        CHECK_EQ(drainedHeartbeatFlags(consumer).size(), 1);
        std::this_thread::sleep_for(std::chrono::milliseconds{2}); // the interval elapses in silence

        // a message through the same producer counts as activity and restarts the silence timer
        auto const sent = producer.send<svc::SetLogLevel>([](svc::SetLogLevel msg) {
            msg.get<"level">().value(svc::LogLevel::Info);
        });
        REQUIRE(sent);
        producer.keepAlive();
        CHECK_EQ(drainedHeartbeatFlags(consumer).size(), 0);

        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        producer.keepAlive();
        CHECK_EQ(drainedHeartbeatFlags(consumer).size(), 1);
    }

    TEST_CASE("stop publishes a Stop heartbeat and disables periodic publication") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        producer.startHeartbeating(std::chrono::milliseconds{1});
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        producer.keepAlive();
        CHECK_EQ(drainedHeartbeatFlags(consumer).size(), 2);

        producer.stopHeartbeating();
        {
            auto beats = drainedHeartbeatFlags(consumer);
            REQUIRE_EQ(beats.size(), 1);
            CHECK(beats[0].stop());
            CHECK_FALSE(beats[0].start());
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        producer.keepAlive();
        CHECK_EQ(drainedHeartbeatFlags(consumer).size(), 0); // disabled: silence is not published

        producer.stopHeartbeating();
        CHECK_EQ(drainedHeartbeatFlags(consumer).size(), 0); // idempotent
    }

    TEST_CASE("double start publishes a single Start heartbeat") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        producer.startHeartbeating(std::chrono::milliseconds{1});
        producer.startHeartbeating(std::chrono::milliseconds{1});

        CHECK_EQ(drainedHeartbeatFlags(consumer).size(), 1);
    }

    TEST_CASE("setHeartbeatInterval changes the interval on the fly") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        // set before start: startHeartbeating without an argument keeps it
        producer.setHeartbeatInterval(std::chrono::milliseconds{1});
        producer.startHeartbeating();
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        producer.keepAlive();
        CHECK_EQ(drainedHeartbeatFlags(consumer).size(), 2);

        // a long interval swallows the elapsed time: without 2ms does not silence publication
        producer.setHeartbeatInterval(std::chrono::seconds{10});
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        producer.keepAlive();
        CHECK_EQ(drainedHeartbeatFlags(consumer).size(), 0);

        // a short interval publishes immediately once the previous beats are older than the interval
        producer.setHeartbeatInterval(std::chrono::milliseconds{1});
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        producer.keepAlive();
        CHECK_EQ(drainedHeartbeatFlags(consumer).size(), 1);
    }

    TEST_CASE("a full queue: ENOBUFS is swallowed, the silence timer is not restarted") {
        auto queue = testing::Queue{};
        queue.maxSize = 1; // one message fits: the Start heartbeat fills the queue
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        producer.startHeartbeating(std::chrono::milliseconds{1});
        std::this_thread::sleep_for(std::chrono::milliseconds{2});

        // the queue is full: the failed publication must not throw and must not restart the silence timer
        producer.keepAlive();
        {
            auto beats = drainedHeartbeatFlags(consumer);
            REQUIRE_EQ(beats.size(), 1); // the Start beat, the failed keepAlive published nothing
            CHECK(beats[0].start());
        }

        // drained: the same silence (it was never restarted by the swallowed attempt) publishes immediately
        producer.keepAlive();
        {
            auto beats = drainedHeartbeatFlags(consumer);
            REQUIRE_EQ(beats.size(), 1);
            CHECK_FALSE(beats[0].start());
            CHECK_FALSE(beats[0].stop());
        }
    }
}

} // namespace turbofw::testing
