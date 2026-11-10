// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include <turbofw/md/schema.h>
#include <turbofw/svc/schema.h>

#include "mixin/SBEReceiveMixin.h"
#include "mixin/SBESendMixin.h"
#include "testing/TestQueue.h"

namespace turbofw::testing {
namespace {

/// A hook-only mixin: records `{tag, acting templateId}` of every buffer that reaches `processMessage`, in
/// hook order. Tagged per instantiation, so two tag mixins in one `Mixins` list prove the list order.
template <char kTag>
struct TagMixin {
    std::vector<std::pair<char, std::uint16_t>>* log{nullptr};

    template <typename Header>
    auto processMessage(Header header, std::span<std::byte const>) -> void {
        log->emplace_back(kTag, header.template get<"templateId">().value());
    }
};

/// A mixin without any hook method: proves the `if constexpr (requires ...)` skip inside receive().
struct HooklessMixin {};

/// A hook that self-demultiplexes: counts only the buffers carrying the acting templateId it is templated on.
template <std::uint16_t kTemplateId>
struct TemplateFilterMixin {
    std::size_t count = 0;

    template <typename Header>
    auto processMessage(Header header, std::span<std::byte const>) -> void {
        if (header.template get<"templateId">().value() == kTemplateId) {
            ++count;
        }
    }
};

/// SBE publisher over the test queue
struct TestSBEProducer : public testing::Producer, public mixin::SBESendMixin {
    using testing::Producer::Producer;
};

/// Consumer composed with the hook mixins; the receive mixin itself is not a hook, so it is not in `Mixins`
using Filter = TemplateFilterMixin<svc::Heartbeat::sbeTemplateId()>;

struct TaggedConsumer : public testing::Consumer,
                        public mixin::SBEReceiveMixin,
                        public TagMixin<'a'>,
                        public HooklessMixin,
                        public Filter {
    TaggedConsumer(Queue* q, std::vector<std::pair<char, std::uint16_t>>* log)
        : testing::Consumer{q}, TagMixin<'a'>{log}, Filter{} {}

    using Mixins = mp::List<TagMixin<'a'>, HooklessMixin, Filter>;
};

/// Two tag mixins in one `Mixins` list: proves the hooks run in the list order
struct OrderedConsumer : public testing::Consumer,
                         public mixin::SBEReceiveMixin,
                         public TagMixin<'a'>,
                         public TagMixin<'b'> {
    OrderedConsumer(Queue* q, std::vector<std::pair<char, std::uint16_t>>* log)
        : testing::Consumer{q}, TagMixin<'a'>{log}, TagMixin<'b'>{log} {}

    using Mixins = mp::List<TagMixin<'a'>, TagMixin<'b'>>;
};

} // namespace

TEST_SUITE("SBEReceiveMixin") {
    TEST_CASE("receive() counts {received, handled, dropped} and skips a foreign message") {
        auto queue = testing::Queue{};
        auto producer = TestSBEProducer{&queue};

        auto log = std::vector<std::pair<char, std::uint16_t>>{};
        auto consumer = TaggedConsumer{&queue, &log};

        // an empty queue: nothing received
        auto const [nothing, noHandles, noDrops] = consumer.receive<mp::List<svc::Heartbeat>>([](svc::Heartbeat) {
            FAIL("empty queue must not run");
        });
        CHECK_EQ(nothing, 0);
        CHECK_EQ(noHandles, 0);
        CHECK_EQ(noDrops, 0);

        // a message of the same schema the list does not expect: received, but neither handled nor dropped
        auto const foreign = producer.send<svc::SetLogLevel>([](svc::SetLogLevel msg) {
            msg.get<"level">().value(svc::LogLevel::Error);
        });
        REQUIRE(foreign);

        // the expected message
        auto const beat = producer.send<svc::Heartbeat>([](svc::Heartbeat msg) {
            msg.get<"ts">().value(222);
            msg.get<"serviceId">().value(1);
            msg.get<"flags">().value(svc::ServiceFlags{}.start(true));
        });
        REQUIRE(beat);

        // a buffer too short to carry even the MessageHeader: dropped
        auto const garbage = producer.prepare(3);
        REQUIRE_EQ(garbage.size(), 3);
        producer.commit();

        auto calls = 0;
        auto const [received, handled, dropped] =
            consumer.receive<mp::List<svc::Heartbeat>>([&calls](svc::Heartbeat msg) {
                ++calls;
                CHECK_EQ(msg.get<"ts">().value(), 222);
                auto flags = msg.get<"flags">().value();
                CHECK(flags.start());
            });
        CHECK_EQ(received, 3);
        CHECK_EQ(handled, 1);
        CHECK_EQ(dropped, 1);
        CHECK_EQ(calls, 1);

        // hooks saw both real messages (the dropped one is too short to read a header at all): heartbeats from
        // the list AND the foreign SetLogLevel whose hook-side audit is the hook's own job
        REQUIRE_EQ(log.size(), 2);
        CHECK_EQ(log[0], std::make_pair('a', svc::SetLogLevel::sbeTemplateId()));
        CHECK_EQ(log[1], std::make_pair('a', svc::Heartbeat::sbeTemplateId()));

        // the self-demultiplexing hook counted only the heartbeat, not the foreign message
        CHECK_EQ(consumer.Filter::count, 1);

        // the queue is drained
        auto const [empty, emptyHandled, emptyDropped] = consumer.receive<mp::List<svc::Heartbeat>>([](svc::Heartbeat) {
            FAIL("the queue must be drained");
        });
        CHECK_EQ(empty, 0);
        CHECK_EQ(emptyHandled, 0);
        CHECK_EQ(emptyDropped, 0);
    }

    TEST_CASE("hooks run in `Mixins` list order for every readable-buffer message") {
        auto queue = testing::Queue{};
        auto producer = TestSBEProducer{&queue};

        auto log = std::vector<std::pair<char, std::uint16_t>>{};
        auto consumer = OrderedConsumer{&queue, &log};

        auto const beat = producer.send<svc::Heartbeat>([](svc::Heartbeat msg) {
            msg.get<"ts">().value(2);
            msg.get<"serviceId">().value(1);
            msg.get<"flags">().value(svc::ServiceFlags{});
        });
        REQUIRE(beat);

        auto const [received, handled, dropped] = consumer.receive<mp::List<svc::Heartbeat>>([](svc::Heartbeat) {});
        CHECK_EQ(received, 1);
        CHECK_EQ(handled, 1);
        CHECK_EQ(dropped, 0);

        // both hooks fired for the same message, first 'a' then 'b': the `Mixins` list order
        REQUIRE_EQ(log.size(), 2);
        CHECK_EQ(log[0], std::make_pair('a', svc::Heartbeat::sbeTemplateId()));
        CHECK_EQ(log[1], std::make_pair('b', svc::Heartbeat::sbeTemplateId()));
    }
}

} // namespace turbofw::testing
