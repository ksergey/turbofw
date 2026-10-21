# turbofw

[![C++](https://img.shields.io/badge/C++-23-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/platform-Linux-orange)]()
[![License](https://img.shields.io/github/license/ksergey/turbofw)](LICENSE)
[![CMake](https://img.shields.io/badge/build-CMake-informational.svg)](https://cmake.org)
[![CI](https://github.com/ksergey/turbofw/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/ksergey/turbofw/actions/workflows/build-and-test.yml)

> turbofw is a low-latency C++ framework for exchange trading: strategies, market data connectors and trade
> connectors talking to each other over shared-memory queues.

## Layout

| Path | What lives there |
|---|---|
| `code/turbofw/` | Core library `turbofw::turbofw` (`*.h`, `*.cpp`), unit tests (`*_test.cpp`), benchmarks (`*_bm.cpp`) |
| `schemas/md.xml` | SBE market data schema (id 1): instruments, trading status, MBO, MBP, BBO, trades, statistics → `<turbofw/md/schema.h>`, `turbofw::md` |
| `schemas/trading.xml` | SBE trading schema (id 2): new / replace / cancel / mass cancel, execution reports, cancel rejects, session status → `<turbofw/trading/schema.h>`, `turbofw::trading` |
| `schemas/common.xml` | Types shared by both schemas (message header, Price, Qty, Timestamp, Side, ...), XIncluded into each |
| `cmake/` | `GetCPM.cmake` (dependency bootstrap, same as in turboq) |

## Dependencies

- C++23 compiler (GCC 14+ or Clang 20+)
- CMake 4.0+ (required by `ksergey/xxx`), Ninja recommended
- Python 3.10+ with `venv` (the SBE code generator runs in a virtualenv created in the build directory)

Everything else is fetched at configure time via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake), so the
first configure needs network access. Set `CPM_SOURCE_CACHE` to reuse downloads across build directories.

| Dependency | Version | Used for |
|---|---|---|
| [ksergey/turboq](https://github.com/ksergey/turboq) | `master` | Shared-memory message queues (linked into `turbofw`) |
| [ksergey/turboq-reactor](https://github.com/ksergey/turboq-reactor) | `master` | Event loop / networking (linked into `turbofw`) |
| [ksergey/sbe-code-gen](https://github.com/ksergey/sbe-code-gen) | `master` | SBE codec generation (`SbeMakeCodec()`) |
| [ksergey/xxx](https://github.com/ksergey/xxx) | `master` | Terminal UI |
| [nlohmann/json](https://github.com/nlohmann/json) | `3.12.0` | JSON |
| [doctest](https://github.com/doctest/doctest) | `2.5.3` | Unit tests (only with `turbofw_BUILD_TESTS`) |
| [google/benchmark](https://github.com/google/benchmark) | `1.10.0` | Benchmarks (only with `turbofw_BUILD_BENCHMARKS`) |

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-14
cmake --build build
ctest --test-dir build/code --output-on-failure

./build/code/turbofw-Clock-bm
```

### CMake options

| Option | Default | Effect |
|---|---|---|
| `turbofw_BUILD_TESTS` | `ON` | Build `*_test.cpp` files as doctest executables and register them with `ctest` |
| `turbofw_BUILD_BENCHMARKS` | `ON` | Build `*_bm.cpp` files as google-benchmark executables |
| `turbofw_SANITIZER` | `OFF` | ASan + UBSan + LeakSanitizer |
| `turbofw_TSAN` | `OFF` | ThreadSanitizer (can't be combined with `turbofw_SANITIZER`) |

## Adding tests and benchmarks

Test and benchmark discovery uses `TurboQAddTestsFromSourceList` / `TurboQAddBenchmarksFromSourceList` from
turboq's `cmake/TurboQHelpers.cmake` (defined when turboq is added via CPM). Drop a file into `code/turbofw/`
and re-run the CMake configure — no CMake edits needed:

- `<Name>_test.cpp` becomes `turbofw-<Name>-test`, linked against `doctest::doctest_with_main` and registered with `ctest`;
- `<Name>_bm.cpp` becomes `turbofw-<Name>-bm`, linked against `benchmark::benchmark_main`.

## License

[MIT](LICENSE)
