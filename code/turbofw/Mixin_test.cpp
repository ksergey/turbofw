// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <random>
#include <span>
#include <system_error>
#include <vector>

#include <doctest/doctest.h>
#include <turboq/MPSCMessageQueue.h>
#include <turboq/MemorySource.h>
#include <turboq/MulticastMessageQueue.h>

#include <turbofw/md/schema.h>
#include <turbofw/svc/schema.h>
#include <turbofw/trd/schema.h>

#include "Error.h"
#include "mixin/ReceiveMixin.h"
#include "mixin/SbeSendMixin.h"
#include "mixin/SendMixin.h"

namespace fs = std::filesystem;

namespace turbofw::testing {
namespace {

/// Unique temp directory, removes itself on destruction
class TempDir {
public:
    TempDir() {
        auto engine = std::default_random_engine{std::random_device{}()};
        auto dist = std::uniform_int_distribution<std::uint32_t>{100000, 999999};
        for (;;) {
            path_ = fs::temp_directory_path() / ("turbofw-mixin-test-" + std::to_string(dist(engine)));
            if (!fs::exists(path_)) {
                break;
            }
        }
        fs::create_directories(path_);
    }
    ~TempDir() {
        auto ec = std::error_code{};
        fs::remove_all(path_, ec);
    }

    TempDir(TempDir const&) = delete;
    TempDir& operator=(TempDir const&) = delete;

    [[nodiscard]] auto path() const noexcept -> fs::path const& {
        return path_;
    }

private:
    fs::path path_;
};

/// mpsc producer composed with SendMixin
struct TestMpscProducer : public turboq::MPSCMessageQueue::Producer, public mixin::SendMixin {
    explicit TestMpscProducer(turboq::MPSCMessageQueue::Producer producer) noexcept
        : turboq::MPSCMessageQueue::Producer{std::move(producer)} {}
};

/// mpsc consumer composed with ReceiveMixin
struct TestMpscConsumer : public turboq::MPSCMessageQueue::Consumer, public mixin::ReceiveMixin {
    explicit TestMpscConsumer(turboq::MPSCMessageQueue::Consumer consumer) noexcept
        : turboq::MPSCMessageQueue::Consumer{std::move(consumer)} {}
};

/// multicast producer composed with SendMixin
struct TestMulticastProducer : public turboq::MulticastMessageQueue::Producer, public mixin::SendMixin {
    explicit TestMulticastProducer(turboq::MulticastMessageQueue::Producer producer) noexcept
        : turboq::MulticastMessageQueue::Producer{std::move(producer)} {}
};

/// multicast consumer composed with ReceiveMixin
struct TestMulticastConsumer : public turboq::MulticastMessageQueue::Consumer, public mixin::ReceiveMixin {
    explicit TestMulticastConsumer(turboq::MulticastMessageQueue::Consumer consumer) noexcept
        : turboq::MulticastMessageQueue::Consumer{std::move(consumer)} {}
};

/// mpsc producer composed with SbeSendMixin
struct TestSbeMpscProducer : public turboq::MPSCMessageQueue::Producer, public mixin::SbeSendMixin {
    explicit TestSbeMpscProducer(turboq::MPSCMessageQueue::Producer producer) noexcept
        : turboq::MPSCMessageQueue::Producer{std::move(producer)} {}
};

/// multicast producer composed with SbeSendMixin
struct TestSbeMulticastProducer : public turboq::MulticastMessageQueue::Producer, public mixin::SbeSendMixin {
    explicit TestSbeMulticastProducer(turboq::MulticastMessageQueue::Producer producer) noexcept
        : turboq::MulticastMessageQueue::Producer{std::move(producer)} {}
};

auto const kNoOpReceive = [](std::span<std::byte const>) {
    FAIL("receive() found a buffer where none was expected");
};

} // namespace

TEST_SUITE("Mixin") {
    TEST_CASE("mpsc: send/receive round trip, receive() drains") {
        auto dir = TempDir{};
        auto source = turboq::DefaultMemorySource{dir.path(), 4096};

        auto queue = turboq::MPSCMessageQueue::makeQueue("mixin-test@cmd",
            turboq::MPSCMessageQueue::CreationOptions{.slotSizeHint = 512, .lengthHint = 16}, source);
        REQUIRE_MESSAGE(queue, queue.error());
        auto producer = TestMpscProducer{queue->createProducer()};
        auto consumer = TestMpscConsumer{queue->createConsumer()};

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

    TEST_CASE("mpsc: send() on a full queue returns ENOBUFS") {
        auto dir = TempDir{};
        auto source = turboq::DefaultMemorySource{dir.path(), 4096};

        auto constexpr kLength = 2;
        auto queue = turboq::MPSCMessageQueue::makeQueue("mixin-test-cmd",
            turboq::MPSCMessageQueue::CreationOptions{.slotSizeHint = 512, .lengthHint = kLength}, source);
        REQUIRE_MESSAGE(queue, queue.error());
        auto producer = TestMpscProducer{queue->createProducer()};
        auto consumer = TestMpscConsumer{queue->createConsumer()};

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
        }), kLength);
        auto const refill = producer.send(sizeof(kPayload), [](std::span<std::byte> buffer) {
            buffer.front() = std::byte{kPayload + 1};
        });
        REQUIRE(refill);
    }

    TEST_CASE("multicast: send/receive round trip, receive() drains a batch") {
        auto dir = TempDir{};
        auto source = turboq::DefaultMemorySource{dir.path(), 4096};

        auto queue = turboq::MulticastMessageQueue::makeQueue("mixin-test@data",
            turboq::MulticastMessageQueue::CreationOptions{.capacityHint = 4 * 4096}, source);
        REQUIRE_MESSAGE(queue, queue.error());
        auto producer = TestMulticastProducer{queue->createProducer()};
        auto consumer = TestMulticastConsumer{queue->createConsumer()};

        CHECK_EQ(consumer.receive(kNoOpReceive), 0);

        auto constexpr kMessageCount = 3;
        for (auto index = 0; index < kMessageCount; ++index) {
            auto const sent = producer.send(sizeof(index), [&index](std::span<std::byte> buffer) {
                REQUIRE(buffer.size() >= sizeof(index));
                std::memcpy(buffer.data(), &index, sizeof(index));
            });
            REQUIRE(sent);
        }

        auto received = std::vector<int>{};
        auto const count = consumer.receive([&received](std::span<std::byte const> buffer) {
            REQUIRE(buffer.size() >= sizeof(int));
            auto index = int{};
            std::memcpy(&index, buffer.data(), sizeof(index));
            received.push_back(index);
        });
        CHECK_EQ(count, kMessageCount);
        REQUIRE(received == std::vector{0, 1, 2});

        CHECK_EQ(consumer.receive(kNoOpReceive), 0);
    }
}

TEST_SUITE("SbeSendMixin") {
    TEST_CASE("fixed message: send() without sizes, round trip") {
        auto dir = TempDir{};
        auto source = turboq::DefaultMemorySource{dir.path(), 4096};

        auto queue = turboq::MPSCMessageQueue::makeQueue("sbe-mixin-test@cmd",
            turboq::MPSCMessageQueue::CreationOptions{.slotSizeHint = 512, .lengthHint = 16}, source);
        REQUIRE_MESSAGE(queue, queue.error());
        auto producer = TestSbeMpscProducer{queue->createProducer()};
        auto consumer = TestMpscConsumer{queue->createConsumer()};

        CHECK_EQ(consumer.receive(kNoOpReceive), 0);

        auto constexpr kStartTs = std::int64_t{111};
        auto constexpr kIntervalMs = std::uint32_t{500};
        auto const sent = producer.send<svc::Heartbeat>([&](svc::Heartbeat& msg) {
            msg.get<"clientId">().value(17);
            msg.get<"requestId">().value(3);
            msg.get<"serviceId">().value(1);
            msg.get<"startTs">().value(kStartTs);
            msg.get<"ts">().value(222);
            msg.get<"intervalMs">().value(kIntervalMs);
            msg.get<"flags">().value(svc::ServiceFlags{}.start(true));
        });
        REQUIRE(sent);

        auto const count = consumer.receive([&](std::span<std::byte const> buffer) {
            auto const header = svc::MessageHeader{const_cast<std::byte*>(buffer.data())};
            REQUIRE_EQ(header.get<"schemaId">().value(), svc::Heartbeat::sbeSchemaId());
            REQUIRE_EQ(header.get<"templateId">().value(), svc::Heartbeat::sbeTemplateId());
            auto msg = svc::Heartbeat{
                std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()},
                header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
            REQUIRE_EQ(msg.get<"clientId">().value(), 17u);
            REQUIRE_EQ(msg.get<"requestId">().value(), 3u);
            REQUIRE_EQ(msg.get<"serviceId">().value(), 1u);
            REQUIRE_EQ(msg.get<"startTs">().value(), kStartTs);
            REQUIRE_EQ(msg.get<"intervalMs">().value(), kIntervalMs);
            auto flags = msg.get<"flags">().value();
            REQUIRE(flags.start());
        });
        CHECK_EQ(count, 1);
        CHECK_EQ(consumer.receive(kNoOpReceive), 0);
    }

    TEST_CASE("group message: send() with sizes, round trip") {
        auto dir = TempDir{};
        auto source = turboq::DefaultMemorySource{dir.path(), 4096};

        auto queue = turboq::MulticastMessageQueue::makeQueue("sbe-mixin-test@data",
            turboq::MulticastMessageQueue::CreationOptions{.capacityHint = 8 * 4096}, source);
        REQUIRE_MESSAGE(queue, queue.error());
        auto producer = TestSbeMulticastProducer{queue->createProducer()};
        auto consumer = TestMulticastConsumer{queue->createConsumer()};

        CHECK_EQ(consumer.receive(kNoOpReceive), 0);

        auto const sent = producer.send<md::Snapshot>(std::tuple{2u, 1u, 1u}, [](md::Snapshot& msg) {
            auto header = msg.get<"header">();
            header.get<"instrumentId">().value(42);
            header.get<"flags">().value(md::MdFlags{}.snapshot(true).mandatory(true));
            msg.get<"clientId">().value(17);
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
            auto const header = md::MessageHeader{const_cast<std::byte*>(buffer.data())};
            REQUIRE_EQ(header.get<"schemaId">().value(), md::Snapshot::sbeSchemaId());
            REQUIRE_EQ(header.get<"templateId">().value(), md::Snapshot::sbeTemplateId());
            auto msg = md::Snapshot{
                std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()},
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
        auto dir = TempDir{};
        auto source = turboq::DefaultMemorySource{dir.path(), 4096};

        auto queue = turboq::MPSCMessageQueue::makeQueue("sbe-mixin-test@ctx",
            turboq::MPSCMessageQueue::CreationOptions{.slotSizeHint = 512, .lengthHint = 16}, source);
        REQUIRE_MESSAGE(queue, queue.error());
        auto producer = TestSbeMpscProducer{queue->createProducer()};
        auto consumer = TestMpscConsumer{queue->createConsumer()};

        auto constexpr kClientCode = "ALPHA";
        auto constexpr kAccount = "SPOT-ACC";
        auto const sent = producer.send<trd::CreateContext>(
            std::tuple{std::char_traits<char>::length(kClientCode), std::char_traits<char>::length(kAccount)},
            [](trd::CreateContext& msg) {
                msg.get<"clientId">().value(17);
                msg.get<"requestId">().value(3);
                msg.get<"instrumentId">().value(42);
                msg.get<"clientCode">().value("ALPHA");
                msg.get<"account">().value("SPOT-ACC");
            });
        REQUIRE(sent);

        auto const count = consumer.receive([&](std::span<std::byte const> buffer) {
            auto const header = trd::MessageHeader{const_cast<std::byte*>(buffer.data())};
            REQUIRE_EQ(header.get<"schemaId">().value(), trd::CreateContext::sbeSchemaId());
            REQUIRE_EQ(header.get<"templateId">().value(), trd::CreateContext::sbeTemplateId());
            auto msg = trd::CreateContext{
                std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()},
                header.sbeEncodedLength(), header.get<"blockLength">().value(), header.get<"version">().value()};
            REQUIRE_EQ(msg.get<"clientId">().value(), 17u);
            REQUIRE_EQ(msg.get<"requestId">().value(), 3u);
            REQUIRE_EQ(msg.get<"instrumentId">().value(), 42u);
            REQUIRE_EQ(msg.get<"clientCode">().value(), std::string_view{kClientCode});
            REQUIRE_EQ(msg.get<"account">().value(), std::string_view{kAccount});
        });
        CHECK_EQ(count, 1);
        CHECK_EQ(consumer.receive(kNoOpReceive), 0);
    }

    TEST_CASE("data fields: send() on a full mpsc queue returns ENOBUFS") {
        auto dir = TempDir{};
        auto source = turboq::DefaultMemorySource{dir.path(), 4096};

        auto constexpr kLength = 2;
        auto queue = turboq::MPSCMessageQueue::makeQueue("sbe-mixin-test@small",
            turboq::MPSCMessageQueue::CreationOptions{.slotSizeHint = 512, .lengthHint = kLength}, source);
        REQUIRE_MESSAGE(queue, queue.error());
        auto producer = TestSbeMpscProducer{queue->createProducer()};

        auto constexpr kSendSizes = std::tuple{std::size_t{5}, std::size_t{8}};
        auto constexpr kSend = [](trd::CreateContext& msg) {
            msg.get<"clientCode">().value("ALPHA");
            msg.get<"account">().value("SPOT-ACC");
        };

        // the queue is `kLength` slots deep: two sends succeed, the third one fails
        for (auto index = 0; index < kLength; ++index) {
            auto const fill = producer.send<trd::CreateContext>(kSendSizes, kSend);
            REQUIRE(fill);
        }
        auto const overfill = producer.send<trd::CreateContext>(kSendSizes, kSend);
        REQUIRE_FALSE(overfill);
        CHECK_EQ(overfill.error().value(), ENOBUFS);
    }
}

} // namespace turbofw::testing
