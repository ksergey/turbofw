# turbofw

[![C++](https://img.shields.io/badge/C++-23-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/platform-Linux-orange)]()
[![License](https://img.shields.io/github/license/ksergey/turbofw)](LICENSE)
[![CMake](https://img.shields.io/badge/build-CMake-informational.svg)](https://cmake.org)
[![CI](https://github.com/ksergey/turbofw/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/ksergey/turbofw/actions/workflows/build-and-test.yml)

> turbofw is a low-latency C++ framework for exchange trading: strategies, market data connectors and trade
> connectors talking to each other over shared-memory queues.

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-14
cmake --build build
ctest --test-dir build/code --output-on-failure

./build/code/turbofw-Clock-bm
```

## Dependencies

All are added in the root `CMakeLists.txt` via [CPM](https://github.com/cpm-cmake/CPM.cmake); `ksergey/*`
repositories track `master`, third-party ones are pinned to stable releases.

| Dependency | Source | Purpose |
|---|---|---|
| [turboq](https://github.com/ksergey/turboq) | `ksergey/turboq` | shared-memory message queues (multicast / mpsc) |
| [turboq-reactor](https://github.com/ksergey/turboq-reactor) | `ksergey/turboq-reactor` | event loop over turboq queues |
| [turboq-logger](https://github.com/ksergey/turboq-logger) | `ksergey/turboq-logger` | logging for services |
| [xxx](https://github.com/ksergey/xxx) | `ksergey/xxx` | C++23 utilities used by the ksergey/* components |
| [sbe-code-gen](https://github.com/ksergey/sbe-code-gen) | `ksergey/sbe-code-gen` | SBE codec generator (`schemas/` → `<turbofw/*/schema.h>`) |
| [strong_type](https://github.com/rollbear/strong_type) | `rollbear/strong_type@16` | strong typedefs (`turbofw::StrongType`) |
| [doctest](https://github.com/doctest/doctest) | `doctest/doctest@2.5.3` | unit tests |
| [google-benchmark](https://github.com/google/benchmark) | `google/benchmark` | benchmarks |
| [cxxopts](https://github.com/jarro2783/cxxopts) | `jarro2783/cxxopts@3.3.1` | command-line parsing in the examples |
| [nlohmann/json](https://github.com/nlohmann/json) | `nlohmann/json@3.12.0` | environment config parsing |
| [fmt](https://github.com/fmtlib/fmt) | `fmtlib/fmt` | config error formatting |

## License

[MIT](LICENSE)
