// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <array>
#include <cstddef>
#include <print>
#include <span>

#include <turbofw/SBE.h>
#include <turbofw/mp/List.h>
#include <turbofw/schema/market_data/schema.h>

namespace {

/// Both messages come from the same schema, so they share one MessageHeader: decodeSBE dispatches on its
/// (schemaId, templateId) pair.
using Messages = turbofw::mp::List<turbofw::schema::market_data::Snapshot, turbofw::schema::market_data::MbpUpdate>;

auto encodeSnapshot(std::span<std::byte> buffer, std::size_t depth) -> void {
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
    stats.reset(0); // an empty group
}

auto encodeMbpUpdate(std::span<std::byte> buffer) -> void {
    auto msg = turbofw::schema::market_data::MbpUpdate::wrapAndApplyHeader(buffer);

    auto header = msg.get<"header">();
    header.get<"instrumentId">().value(42);
    header.get<"exchangeTs">().value(1795008819000000000);
    header.get<"receiveTs">().value(1795008819012133100);
    header.get<"flags">().value(turbofw::schema::market_data::MdFlags{}.snapshot(true).mandatory(true));

    msg.get<"side">().value(turbofw::schema::market_data::Side::Bid);
    msg.get<"action">().value(turbofw::schema::market_data::UpdateAction::New);
    msg.get<"level">().value(1);
    msg.get<"price">().get<"mantissa">().value(1500000000); // 1.5 at exponent -9
    msg.get<"qty">().value(30);
    msg.get<"orders">().value(1);
}

} // namespace

int main() {
    std::array<std::byte, 1024> buffer;

    auto const printJson = [](auto msg) {
        std::print("{}\n", turbofw::toJsonString(msg));
    };

    std::print("snapshot:\n");
    encodeSnapshot(buffer, 4);
    turbofw::decodeSBE<Messages>(buffer, printJson);

    std::print("mbp-update:\n");
    encodeMbpUpdate(buffer);
    turbofw::decodeSBE<Messages>(buffer, printJson);
}
