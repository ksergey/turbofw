# AGENTS.md

Guidance for AI coding agents working on **turbofw** — a C++23, Linux-only, low-latency framework for exchange
trading: strategies, market data connectors and trade connectors communicating over turboq shared-memory queues
with SBE-encoded messages. Latency is the product: every change to a hot path must be justified in those terms.

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
- Prices and quantities are fixed-point mantissas; exponents come from `meta::InstrumentAttributes`
  (tickSize, lotSize, multiplier).
