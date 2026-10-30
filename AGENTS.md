# AGENTS.md

Guidance for AI coding agents working on **turbofw** — a C++23, Linux-only, low-latency framework for exchange
trading: strategies, market data connectors and trade connectors communicating over turboq shared-memory queues
with SBE-encoded messages. Latency is the product: every change to a hot path must be justified in those terms.

## Architecture

The system is a set of microservices: market data connectors, trading connectors, the Metadata service and clients
such as strategies. They are separate processes and talk to each other **only** through turboq shared-memory queues
carrying SBE messages from `schemas/`. No sockets, files or other IPC between services.

### Queues

Every microservice owns exactly two queues:

| Queue | Direction | Contents |
|---|---|---|
| `turboq::Multicast` | out of the service | Everything the service publishes: streaming data (market data, execution reports, heartbeats) **and** responses to requests |
| `turboq::MPSC` | into the service | Requests from any number of clients; the service is the only consumer |

- Only the owning service writes to its `Multicast` queue and reads its `MPSC` queue. A client writes requests to the
  service's `MPSC` queue and reads the service's `Multicast` queue.
- There are no per-client reply queues. Every reader of a `Multicast` queue sees all messages and must pick out the
  ones addressed to it, and the data it needs, by itself. Matching rules: market data by `instrumentId`; responses by
  `(clientId, requestId)`; trading messages by `contextId` (plus `requestId` / `orderRef`).
- A queue can carry two schemas: the service's own protocol (`md`, `meta` or `trd`) and `svc`. Demultiplex by
  `(schemaId, templateId)` from the SBE message header. `svc` templateIds start at 100.
- Metadata publishes the queue names of market data services (`ServiceDefinition.dataQueue` is the `Multicast`
  queue, `.requestQueue` is the `MPSC` queue). The Metadata service itself has a fixed name; its queue names come
  from static configuration.

### Services and protocols

| Service | Schemas | `MPSC` receives (requests) | `Multicast` carries |
|---|---|---|---|
| Market data connector | `md`, `svc` | `SnapshotRequest` | `MbpUpdate`, `MboUpdate`, `Trade`, `Statistic`, `InstrumentStatus`, `Snapshot`, `RequestComplete` |
| Trading connector | `trd`, `svc` | `CreateContext`, `NewOrder`, `ReplaceOrder`, `CancelOrder`, `CancelAll`, `OrderStatusRequest` | `ContextResult`, `ExecutionReport`, `CommandReject`, `RequestComplete` |
| Metadata | `meta`, `svc` | `RegisterService`, `RegisterInstrument`, `ServiceRequest`, `InstrumentRequest` | `RegisterServiceResult`, `RegisterInstrumentResult`, `ServiceDefinition`, `InstrumentDefinition`, `RequestComplete` |

Every service additionally receives `svc::HeartbeatRequest` and `svc::SetLogLevel` on its `MPSC` queue and publishes
`svc::Heartbeat` and `svc::SetLogLevelResult` on its `Multicast` queue. The schema files are the source of truth for
message contents; don't duplicate them here.

### Request / response conventions

- `clientId` is a random 32-bit id chosen by the client (`0xFFFFFFFF` is null). `requestId` is chosen by the client.
  A response echoes both. Spontaneous messages have `clientId = null` and `requestId = 0`.
- Pattern: a request on `MPSC`, then zero or more responses on `Multicast`, then (for multi-message responses) a
  terminating `RequestComplete` carrying `count`.
- There are no sequence numbers in the protocol. The design assumes the shared-memory queues do not lose messages;
  losses upstream (venue or feed) are signalled by the `Gap` flag in `MdFlags`.

### Service lifecycle

- Every service publishes `svc::Heartbeat` on events (start, stop, state change) and, when it has published nothing
  else for `intervalMs`, periodically. Flags: `Start` and `Stop` are events; `NotConnected` and `Recovering` are
  states repeated while they hold.
- `startTs` identifies the service instance. A changed `startTs` (or the `Start` flag) means the service restarted
  and everything a client derived from the previous instance is invalid: trading contexts must be created again,
  market data state must be re-synchronized. A consumer treats a service as lost after a few missed heartbeat
  intervals.
- `HeartbeatRequest` makes the service publish a heartbeat immediately (for late joiners). `SetLogLevel` changes the
  service's log level at runtime.
- On startup a market data connector registers itself in Metadata (`RegisterService`), then every active instrument
  (`RegisterInstrument`, with `reportedTs`).

### Semantics that code must respect

- **Market data consistency is the connector's job.** A client never merges snapshots and increments itself: after a
  `Snapshot`, all later updates of that instrument are consistent with it. A snapshot with the `Mandatory` flag (for
  example after recovery) must not be ignored: reset the instrument state and apply it. `MboUpdate` carries enough
  information (price and qty on `Delete`, previous price and qty on `Change`) to maintain aggregated levels without
  an order-level snapshot. `EndOfEvent` marks the last message of a venue event; apply messages up to it atomically.
- **Trading.** A client first creates a context (`CreateContext`: instrument, client code, account) and receives a
  `contextId`; every order command carries it. Contexts live until the trading connector restarts; how ids are
  allocated and creation parameters stored is up to the connector. The client identifies its orders by its own
  `orderRef` (unique within a context). Generating `ClOrdID` is the connector's job (it may be venue-specific); the
  connector keeps the mapping, and `orderRef` stays stable across replaces. An `ExecutionReport` with
  `requestId = 0` is unsolicited. Reports carry `instrumentId`, so third-party services can identify the instrument.
- **Metadata** is the source of truth for `instrumentId` and `serviceId` (assigned by Metadata, never reused). The
  natural key of an instrument is `(serviceId, venueId, exchangeSymbol)`: the same venue instrument received through
  two services is two instruments (grouping them is planned). Definitions are versioned; a client applies an update
  only if its `version` is greater than the one it knows.

### Rules

- Services depend on each other only through queues and schemas. Don't link one service's internals into another.
- Every service must publish `svc::Heartbeat` and handle `HeartbeatRequest` and `SetLogLevel`.
- A new service type means: its two queues, its own schema (or an existing one) plus `svc`, and a row in the table
  above.

## Repository map

| Path | What lives there |
|---|---|
| `code/turbofw/*.{h,cpp}` | Core library `turbofw::turbofw` |
| `code/turbofw/*_test.cpp` | doctest unit tests, one executable per file (auto-discovered) |
| `code/turbofw/*_bm.cpp` | google-benchmark benchmarks, one executable per file (auto-discovered) |
| `schemas/market_data_schema.xml` | SBE market data schema (package `md`, schemaId 1) → `<turbofw/md/schema.h>`, namespace `md` |
| `schemas/metadata_schema.xml` | SBE metadata schema (package `meta`, schemaId 2) → `<turbofw/meta/schema.h>`, namespace `meta` |
| `schemas/trading_schema.xml` | SBE trading schema (package `trd`, schemaId 3) → `<turbofw/trd/schema.h>`, namespace `trd` |
| `schemas/service_schema.xml` | SBE service schema (package `svc`, schemaId 4) → `<turbofw/svc/schema.h>`, namespace `svc` |
| `schemas/common_types.xml` | Shared types, XIncluded into every schema |
| `cmake/` | `GetCPM.cmake` (dependency bootstrap); test/bench discovery comes from turboq's `TurboQHelpers.cmake` (available after adding turboq) |
| `.github/workflows/build-and-test.yml` | CI: compiler matrix, sanitizers |

## Toolchain

- GCC 14+ or Clang 20+ (`<print>`, `<expected>`, C++23).
- CMake 4.0+ (ksergey/xxx requires it), Ninja recommended.
- Python 3 with `venv`: `SbeMakeCodec()` creates `build/venv` and pip-installs the generator's requirements.
- The first configure needs network access (CPM). Set `CPM_SOURCE_CACHE` to reuse downloads.

## Build and test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-14
cmake --build build
ctest --test-dir build/code -j"$(nproc)" --output-on-failure

# sanitizers -- two separate builds, they cannot be combined
cmake -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-14 -Dturbofw_SANITIZER=ON
cmake -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-14 -Dturbofw_TSAN=ON
```

Before declaring a change done: the Debug build must be warning-free and all ctest suites must pass; anything
touching atomics or shared memory must also pass the ASan and TSan builds.

## Dependencies

All are added in the root `CMakeLists.txt` via CPM. `ksergey/*` repositories track `master`; third-party
dependencies are pinned to stable releases. Don't add a dependency without updating the README table.

## C++23

The project is C++23. Prefer modern standard facilities over hand-written or third-party equivalents, and over
C-style constructs:

- **Errors:** return `std::expected<T, E>` for recoverable failures instead of exceptions, error codes or
  out-parameters; chain steps with its monadic operations (`and_then`, `transform`, `or_else`). Keep `E` small and
  allocation-free (an `enum class` or a small struct), not a `std::string`. The same goes for `std::optional`
  (`and_then`, `transform`, `or_else`, `value_or`). No exceptions for control flow, and none on hot paths.
- **Ranges:** use `std::ranges` algorithms and `std::views` (`filter`, `transform`, `take`, `iota`, `reverse`, ...)
  instead of raw index loops and iterator pairs where they read better. Views are lazy and don't allocate;
  `std::ranges::to` and anything else that builds a container does, so keep it off hot paths. Views such as `zip` and
  `enumerate` only if every compiler in the CI matrix supports them.
- **Non-owning views:** `std::span` for byte and element buffers (SBE buffers, queue payloads), `std::string_view`
  for text. Never pass a pointer plus a length.
- **Types and conversions:** `enum class` everywhere; `std::to_underlying` for enum-to-integer; `std::bit_cast` and
  `<bit>` (`std::byteswap`, `std::countl_zero`, ...) instead of `memcpy` or `reinterpret_cast` for type punning;
  `std::array` / `std::to_array` instead of C arrays; designated initializers for aggregates.
- **Compile time:** `constexpr` / `consteval` by default, `if consteval`, concepts and `requires` instead of SFINAE,
  `static operator()` and deducing `this` where they simplify code, `std::unreachable()` for impossible branches.
- **Time:** use `std::chrono` types (`std::chrono::nanoseconds`, `sys_time`) inside the code and convert from and to
  the SBE `Timestamp` (int64 nanoseconds since the Unix epoch) at the edges.
- **Output:** `std::print` / `std::println` in tools, examples and tests; services log through turboq-logger.
- **Resource management:** RAII and `std::unique_ptr` / values; no raw `new` / `delete`, no owning raw pointers, no
  C-style casts. No `std::function` on hot paths (it may allocate); use templates or plain callables.
- **Library support:** the minimum compilers are GCC 14 and Clang 20, so check that a C++23 library feature is
  available in all of them before relying on it (for example `<flat_map>` is not in GCC 14's library). CI is the
  arbiter; if a feature is missing, use a plain alternative and don't add a polyfill dependency.

## Code style

- Format with `.clang-format` (4-space indent, 120 columns, `PointerAlignment: Left`).
- Every source file starts with:
  ```cpp
  // Copyright (c) Sergey Kovalevich <inndie@gmail.com>
  // SPDX-License-Identifier: MIT
  ```
  followed by `#pragma once` for headers.
- Trailing return types, `[[nodiscard]]`, `noexcept` where it holds, `///` comments on public API.
- Naming: `PascalCase` types, `camelCase` functions/variables, `member_` trailing underscore, `kConstant`,
  `TURBOFW_` macro prefix.
- The library is built with `-fno-rtti` (PUBLIC flag): no `dynamic_cast`/`typeid`.
- Tests go in `namespace turbofw::testing { TEST_SUITE("...") { ... } }`.
- Don't add free functions named `toString` in `namespace turbofw` (breaks doctest stringification via ADL).
- Never add locks, allocations, syscalls or logging on hot paths (queue produce/consume, message encode/decode).

## SBE schemas

- Schemas are wire contracts between independently built processes. Changing field order, types or template ids
  is a breaking change: bump the schema `version` and call it out. Adding fields at the end of a message block
  with `sinceVersion` is the compatible way to extend a message.
- One schema per protocol (`md`, `meta`, `trd`) plus `svc` with messages common to every service, each with its own
  `schemaId`, so a consumer can demultiplex a queue carrying two schemas by `(schemaId, templateId)` from the
  message header. A new protocol gets a new schema file and schemaId and is added to the `foreach` in
  `code/CMakeLists.txt`.
- Shared types live in `schemas/common_types.xml`; each schema gets its own copy of them in its own namespace, so
  `md::Side` and `trd::OrderSide` are distinct C++ types with identical encoding.
- Order fields by size (8, 4, 2, 1 bytes) to keep them naturally aligned.
- Prices are the composites `Price9` / `Price9NULL` (`common_types.xml`): an int64 `mantissa` and a constant `int8`
  `exponent` of -9 that is not encoded, so the wire size is 8 bytes. There is no per-instrument exponent; a
  different precision means a new composite (`Price6`, ...), not a field. Access it as
  `msg.get<"price">().get<"mantissa">()`. `meta::InstrumentAttributes` carries `tickSize`, `lotSize` and
  `multiplier` in the same encodings.
- `Qty` is a plain int64 count of units (no exponent). If fractional quantities are needed, add a composite such as
  `DecimalQty4` instead of changing `Qty`.
- `Price9 * Qty` overflows int64 for realistic values; compute notionals in 128-bit (`__int128`) or rescale first.
