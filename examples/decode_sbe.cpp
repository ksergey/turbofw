// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <print>

#include <turbofw/SBE.h>

#include <turbofw/schema/service/Heartbeat.h>

[[nodiscard]] auto encodeExampleSnapshotMessage(
    std::span<std::byte> buffer, std::size_t depth) -> turbofw::schema::market_data::Snapshot {
    auto msg = turbofw::schema::market_data::Snapshot::wrapAndApplyHeader(buffer);

    auto header = msg.get<"header">();
    header.get<"instrumentId">().value(42);
    header.get<"exchangeTs">().value(1795008819000000000);
    header.get<"receiveTs">().value(1795008819012133100);
    header.get<"flags">().value(turbofw::schema::market_data::MdFlags{}.snapshot(true).mandatory(true));

    msg.get<"appId">().reset();
    msg.get<"requestId">().value(3);
    msg.get<"chunkNo">().value(0);
    msg.get<"lastChunk">().value(1);
    msg.get<"tradingState">().value(17);

    auto bids = msg.get<"bids">(); // descending price
    bids.reset(depth);
    for (std::size_t i = 0; i < depth; ++i) {
        auto& level = bids.next();
        level.get<"price">().get<"mantissa">().value(100 - i);
        level.get<"qty">().value(10 + i);
        level.get<"orders">().value(1);
    }

    auto asks = msg.get<"asks">();
    asks.reset(depth);
    for (std::size_t i = 0; i < depth; ++i) {
        auto& level = asks.next();
        level.get<"price">().get<"mantissa">().value(100 + i);
        level.get<"qty">().value(10 + i);
        level.get<"orders">().value(1);
    }

    auto stats = msg.get<"stats">();
    stats.reset(0);

    return msg.rewind();
}

[[nodiscard]] auto encodeExampleMbpUpdateMessage(
    std::span<std::byte> buffer) -> turbofw::schema::market_data::MbpUpdate {
    auto msg = turbofw::schema::market_data::MbpUpdate::wrapAndApplyHeader(buffer);

    auto header = msg.get<"header">();
    header.get<"instrumentId">().value(42);
    header.get<"exchangeTs">().value(1795008819000000000);
    header.get<"receiveTs">().value(1795008819012133100);
    header.get<"flags">().value(turbofw::schema::market_data::MdFlags{}.snapshot(true).mandatory(true));

    msg.get<"side">().value(turbofw::schema::market_data::Side::Bid);
    msg.get<"action">().value(turbofw::schema::market_data::UpdateAction::New);
    msg.get<"level">().value(1);
    msg.get<"price">().get<"mantissa">().value(1500000000);
    msg.get<"qty">().value(30);
    msg.get<"orders">().value(1);

    return msg.rewind();
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::array<std::byte, 1024> buffer;

    {
        auto msg = encodeExampleSnapshotMessage(buffer, 4);
        std::print("snapshot:\n{}\n", turbofw::toJsonString(msg));
    }

    {
        auto msg = encodeExampleMbpUpdateMessage(buffer);
        std::print("mbp-update:\n{}\n", turbofw::toJsonString(msg));
    }

    return EXIT_SUCCESS;
}
