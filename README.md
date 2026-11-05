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

## License

[MIT](LICENSE)
