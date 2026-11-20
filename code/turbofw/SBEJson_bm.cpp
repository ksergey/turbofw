// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

// JSON serialization benchmarks. Meaningful in a Release build only: fixtures are encoded once, the timed loops
// serialize the same decoded message. Run with `--benchmark_min_time=... --benchmark_repetitions=...`.

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

#include <turboq/Platform.h>

#include <turbofw/SBE.h>

#include <turbofw/schema/market_data/schema.h>
#include <turbofw/schema/metadata/schema.h>
#include <turbofw/schema/service/schema.h>

namespace {

constexpr auto kBufferSize = 512;

/// Decimal composites as the nested {"mantissa": ..., "exponent": ...} object
struct DecimalObjectOpts {
    static constexpr auto decimalFormat = turbofw::detail::sbe::DecimalFormat::AsObject;
};

/// An encoded message view plus its total wire size (header + body + variable part): `rewind()` resets the
/// encoding cursor, so the size has to be captured before it
template <typename M>
struct Encoded {
    M msg;
    std::size_t size;
};

/// Every encoder returns a rewound message: a view over what was just written. No-inline keeps the fixture
/// opaque to the optimizer, so the timed loops really read the encoded bytes instead of folding them
TURBOQ_NO_INLINE auto encodeHeartbeat(std::span<std::byte> buffer) -> Encoded<turbofw::schema::service::Heartbeat> {
    auto msg = turbofw::schema::service::Heartbeat::wrapAndApplyHeader(buffer);
    msg.get<"ts">().value(222);
    msg.get<"serviceId">().value(1);
    msg.get<"flags">().value(turbofw::schema::service::ServiceFlags{}.start(true).stop(true));
    auto const size = msg.offset() + msg.encodedSize();
    return {msg.rewind(), size};
}

/// Two bid levels (100, 99) and two ask levels (100, 101), an empty stats group
TURBOQ_NO_INLINE auto encodeSnapshot(std::span<std::byte> buffer) -> Encoded<turbofw::schema::market_data::Snapshot> {
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

    auto bids = msg.get<"bids">();
    bids.reset(2);
    for (std::int64_t i = 0; bids.hasNext(); ++i) {
        auto& level = bids.next();
        level.get<"price">().get<"mantissa">().value(100 - i);
        level.get<"qty">().value(10 + i);
        level.get<"orders">().value(1);
    }

    auto asks = msg.get<"asks">();
    asks.reset(2);
    for (std::int64_t i = 0; asks.hasNext(); ++i) {
        auto& level = asks.next();
        level.get<"price">().get<"mantissa">().value(100 + i);
        level.get<"qty">().value(10 + i);
        level.get<"orders">().value(1);
    }

    auto stats = msg.get<"stats">();
    stats.reset(0);

    auto const size = msg.offset() + msg.encodedSize();
    return {msg.rewind(), size};
}

TURBOQ_NO_INLINE auto encodeMbpUpdate(std::span<std::byte> buffer) -> Encoded<turbofw::schema::market_data::MbpUpdate> {
    auto msg = turbofw::schema::market_data::MbpUpdate::wrapAndApplyHeader(buffer);
    msg.get<"side">().value(turbofw::schema::market_data::Side::Bid);
    msg.get<"action">().value(turbofw::schema::market_data::UpdateAction::New);
    msg.get<"level">().value(1);
    msg.get<"price">().get<"mantissa">().value(1500000000);
    msg.get<"qty">().value(30);
    msg.get<"orders">().value(1);
    auto const size = msg.offset() + msg.encodedSize();
    return {msg.rewind(), size};
}

TURBOQ_NO_INLINE auto encodeRegisterService(std::span<std::byte> buffer, std::string_view name,
    std::string_view dataQueue, std::string_view requestQueue) -> Encoded<turbofw::schema::metadata::RegisterService> {
    auto msg = turbofw::schema::metadata::RegisterService::wrapAndApplyHeader(buffer);
    msg.get<"appId">().value(17);
    msg.get<"reportedTs">().value(123);
    msg.get<"state">().value(turbofw::schema::metadata::ServiceState::Active);
    msg.get<"name">().value(name);
    msg.get<"dataQueue">().value(dataQueue);
    msg.get<"requestQueue">().value(requestQueue);
    auto const size = msg.offset() + msg.encodedSize();
    return {msg.rewind(), size};
}

void reportJsonBytes(benchmark::State& state, std::size_t size) {
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(size));
    state.counters["json_bytes"] = static_cast<double>(size);
}

void reportEncodedBytes(benchmark::State& state, std::size_t size) {
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(size));
    state.counters["msg_bytes"] = static_cast<double>(size);
}

/// Serializes the same decoded message every iteration, allocation of the result included
template <typename Opts = turbofw::detail::sbe::MessageVisitorDefaultOpts, typename M>
void runJson(benchmark::State& state, M const& msg) {
    auto const expected = turbofw::toJsonString<Opts>(msg);
    for (auto _ : state) {
        auto json = turbofw::toJsonString<Opts>(msg);
        benchmark::DoNotOptimize(json);
    }
    reportJsonBytes(state, expected.size());
}

void BM_JsonHeartbeat(benchmark::State& state) {
    auto buffer = std::array<std::byte, kBufferSize>{};
    auto const fixture = encodeHeartbeat(buffer);
    runJson(state, fixture.msg);
}

void BM_JsonMbpUpdate(benchmark::State& state) {
    auto buffer = std::array<std::byte, kBufferSize>{};
    auto const fixture = encodeMbpUpdate(buffer);
    runJson(state, fixture.msg);
}

void BM_JsonSnapshot(benchmark::State& state) {
    auto buffer = std::array<std::byte, kBufferSize>{};
    auto const fixture = encodeSnapshot(buffer);
    runJson(state, fixture.msg);
}

void BM_JsonSnapshotDecimalObject(benchmark::State& state) {
    auto buffer = std::array<std::byte, kBufferSize>{};
    auto const fixture = encodeSnapshot(buffer);
    runJson<DecimalObjectOpts>(state, fixture.msg);
}

void BM_JsonRegisterService(benchmark::State& state) {
    auto buffer = std::array<std::byte, kBufferSize>{};
    auto const fixture = encodeRegisterService(buffer, "md-connector", "md-data", "md-requests");
    runJson(state, fixture.msg);
}

/// Serializes into a reused buffer: the cost of the writer itself without the per-call string allocation
void BM_JsonSnapshotIntoBuffer(benchmark::State& state) {
    auto buffer = std::array<std::byte, kBufferSize>{};
    auto const fixture = encodeSnapshot(buffer);
    auto const expected = turbofw::toJsonString(fixture.msg);

    auto out = std::string{};
    out.reserve(1024);

    for (auto _ : state) {
        out.clear();
        turbofw::toJsonString(std::back_inserter(out), fixture.msg);
        benchmark::DoNotOptimize(out);
    }
    reportJsonBytes(state, expected.size());
}

void BM_DecodeSnapshot(benchmark::State& state) {
    auto buffer = std::array<std::byte, kBufferSize>{};
    auto const fixture = encodeSnapshot(buffer);
    auto const encoded = std::span<std::byte const>{buffer.data(), fixture.size};
    using Messages = turbofw::mp::TypeList<turbofw::schema::market_data::Snapshot>;

    // Reads fields in the callback: an empty callback would let the optimizer hoist the whole decode out of the loop
    auto sink = std::uint64_t{};
    for (auto _ : state) {
        auto handled = turbofw::decodeSBE<Messages>(encoded, [&sink](turbofw::schema::market_data::Snapshot msg) {
            sink += msg.get<"requestId">().value();
            sink += msg.get<"chunkNo">().value();
            sink += msg.get<"lastChunk">().value();
        });
        sink += handled ? 1u : 0U;
        benchmark::DoNotOptimize(sink);
    }
    reportEncodedBytes(state, encoded.size());
}

void BM_DecodeAndJsonSnapshot(benchmark::State& state) {
    auto buffer = std::array<std::byte, kBufferSize>{};
    auto const fixture = encodeSnapshot(buffer);
    auto const encoded = std::span<std::byte const>{buffer.data(), fixture.size};
    using Messages = turbofw::mp::TypeList<turbofw::schema::market_data::Snapshot>;

    auto const expected = turbofw::toJsonString(fixture.msg);
    for (auto _ : state) {
        auto handled = turbofw::decodeSBE<Messages>(encoded, [](turbofw::schema::market_data::Snapshot msg) {
            benchmark::DoNotOptimize(turbofw::toJsonString(msg));
        });
        benchmark::DoNotOptimize(handled);
    }
    reportJsonBytes(state, expected.size());
}

#define TURBOFW_BM(name) BENCHMARK(name)->Unit(benchmark::kNanosecond)->Repetitions(3)->ReportAggregatesOnly(true)

TURBOFW_BM(BM_JsonHeartbeat);
TURBOFW_BM(BM_JsonMbpUpdate);
TURBOFW_BM(BM_JsonSnapshot);
TURBOFW_BM(BM_JsonSnapshotDecimalObject);
TURBOFW_BM(BM_JsonRegisterService);
TURBOFW_BM(BM_JsonSnapshotIntoBuffer);
TURBOFW_BM(BM_DecodeSnapshot);
TURBOFW_BM(BM_DecodeAndJsonSnapshot);

#undef TURBOFW_BM

} // namespace
