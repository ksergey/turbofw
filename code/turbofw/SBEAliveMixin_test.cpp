// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <chrono>
#include <cstdint>
#include <span>
#include <thread>

#include <doctest/doctest.h>

#include "mixin/SBEAliveMixin.h"
#include "mixin/SBEReceiveMixin.h"
#include "mixin/SBESendMixin.h"
#include "testing/TestQueue.h"
#include <turbofw/mp/List.h>
#include <turbofw/svc/schema.h>
#include <turbofw/trd/schema.h>

namespace turbofw::testing {
namespace {

struct TestProducer : public testing::Producer, public mixin::SBESendMixin {
    using testing::Producer::Producer;
};

/// Consumer composed with the alive mixin; the receive mixin runs its hooks, so it is not in `Mixins`
struct TestConsumer : public testing::Consumer, public mixin::SBEReceiveMixin, public mixin::SBEAliveMixin {
    using testing::Consumer::Consumer;

    using Mixins = mp::List<mixin::SBEAliveMixin>;
};

/// Publishes a Heartbeat carrying the given flag state
auto publishBeat(TestProducer& producer, svc::ServiceFlags flags) -> void {
    producer.send<svc::Heartbeat>([flags](svc::Heartbeat msg) {
        msg.get<"timestamp">().value(realtimeNanos());
        msg.get<"flags">().value(flags);
    });
}

auto publishStopBeat(TestProducer& producer) -> void {
    publishBeat(producer, svc::ServiceFlags{}.stop(true));
}

auto publishStartBeat(TestProducer& producer) -> void {
    publishBeat(producer, svc::ServiceFlags{}.start(true));
}

/// Drains the queue through the decode list (hooks run for every buffer, the callback does nothing)
auto drain(TestConsumer& consumer) -> void {
    consumer.receive<mp::List<svc::Heartbeat>>([](svc::Heartbeat) {});
}

} // namespace

TEST_SUITE("SBEAliveMixin") {
    TEST_CASE("no messages received yet: not alive") {
        auto queue = testing::Queue{};
        auto consumer = TestConsumer{&queue};

        drain(consumer);
        CHECK_FALSE(consumer.isAlive());
    }

    TEST_CASE("a Start heartbeat makes the service alive; silence past the interval ends it") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        publishStartBeat(producer);
        drain(consumer);
        CHECK(consumer.isAlive()); // the default interval is 1s, the beat is fresh

        consumer.setAliveInterval(std::chrono::milliseconds{1});
        std::this_thread::sleep_for(std::chrono::milliseconds{3});
        CHECK_FALSE(consumer.isAlive());
    }

    TEST_CASE("any queue message counts as activity, even outside the decode list") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};
        consumer.setAliveInterval(std::chrono::milliseconds{10});

        producer.send<svc::SetLogLevel>([](svc::SetLogLevel msg) {
            msg.get<"level">().value(svc::LogLevel::Info);
        });
        drain(consumer); // SetLogLevel is svc schema but not in the decode list
        CHECK(consumer.isAlive());

        std::this_thread::sleep_for(std::chrono::milliseconds{3});
        producer.send<trd::CancelAll>([](trd::CancelAll msg) {
            msg.get<"contextId">().value(1);
            msg.get<"requestId">().value(2);
        });
        drain(consumer); // CancelAll is a foreign schema entirely
        CHECK(consumer.isAlive());
    }

    TEST_CASE("a Stop heartbeat latches the service dead immediately; a Start heartbeat revives it") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};
        consumer.setAliveInterval(std::chrono::milliseconds{1000});

        publishStartBeat(producer);
        drain(consumer);
        CHECK(consumer.isAlive());

        publishStopBeat(producer);
        drain(consumer);
        CHECK_FALSE(consumer.isAlive()); // latched without waiting for the interval

        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        CHECK_FALSE(consumer.isAlive()); // silence does not revive it either

        publishStartBeat(producer);
        drain(consumer);
        CHECK(consumer.isAlive());
    }

    TEST_CASE("activity after a Stop heartbeat clears the stopped latch") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};
        consumer.setAliveInterval(std::chrono::milliseconds{10});

        publishStartBeat(producer);
        publishStopBeat(producer);
        drain(consumer);
        CHECK_FALSE(consumer.isAlive());

        producer.send<trd::CancelAll>([](trd::CancelAll msg) {
            msg.get<"contextId">().value(1);
            msg.get<"requestId">().value(2);
        });
        drain(consumer); // the latest observation wins: the service is publishing again
        CHECK(consumer.isAlive());
    }

    TEST_CASE("setAliveInterval widens and narrows the window at any time") {
        auto queue = testing::Queue{};
        auto producer = TestProducer{&queue};
        auto consumer = TestConsumer{&queue};

        publishStartBeat(producer);
        drain(consumer);

        consumer.setAliveInterval(std::chrono::seconds{10});
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        CHECK(consumer.isAlive());

        consumer.setAliveInterval(std::chrono::milliseconds{1});
        std::this_thread::sleep_for(std::chrono::milliseconds{3});
        CHECK_FALSE(consumer.isAlive());
    }
}

} // namespace turbofw::testing
