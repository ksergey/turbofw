cmake_minimum_required(VERSION 4.0)

project(turbofw VERSION 0.1.0 LANGUAGES CXX)

option(turbofw_SANITIZER "Build with sanitizer" OFF)
option(turbofw_TSAN "Build with ThreadSanitizer" OFF)
option(turbofw_BUILD_TESTS "Build tests (*_test.cpp files, linked against doctest)" ON)
option(turbofw_BUILD_BENCHMARKS "Build benchmarks (*_bm.cpp files, linked against google-benchmark)" ON)

include(cmake/GetCPM.cmake)

# External (third-party) dependencies: turbofw's own and those shared with the ksergey/* libraries below.
# Dependencies used by a single library only for its own development (liburing in turboq-reactor, pybind11 in
# turboq) are left to that library.
#
# They are added before the ksergey/* libraries on purpose: the first CPMAddPackage() of a package wins,
# so the libraries' own CPMAddPackage() calls for these become no-ops and every package is fetched once,
# in the version chosen here. Package NAMEs must match the ones the libraries use, otherwise CPM treats
# them as different packages. Keep this list in sync when a library gets a new external dependency.

# turbofw; xxx (examples)
CPMAddPackage(
    NAME nlohmann_json
    VERSION 3.12.0
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    OPTIONS "JSON_Install OFF")

# turbofw; turboq, turboq-reactor, turboq-logger, xxx, sbe-code-gen (tests)
CPMAddPackage("gh:doctest/doctest@2.5.3")

# turboq-logger
CPMAddPackage("gh:fmtlib/fmt#12.2.0")

# turboq (tools), turboq-reactor (tools, examples), xxx (examples)
CPMAddPackage(
    NAME cxxopts
    GITHUB_REPOSITORY jarro2783/cxxopts
    VERSION 3.3.1
    OPTIONS "CXXOPTS_BUILD_EXAMPLES OFF" "CXXOPTS_BUILD_TESTS OFF" "CXXOPTS_ENABLE_INSTALL OFF")

# turbofw (benchmarks)
if (turbofw_BUILD_BENCHMARKS)
    CPMAddPackage(
        NAME benchmark
        GITHUB_REPOSITORY google/benchmark
        VERSION 1.10.0
        OPTIONS
            "BENCHMARK_ENABLE_TESTING OFF"
            "BENCHMARK_ENABLE_GTEST_TESTS OFF"
            "BENCHMARK_ENABLE_INSTALL OFF"
            "BENCHMARK_INSTALL_DOCS OFF"
            "BENCHMARK_ENABLE_WERROR OFF")
endif()

# ksergey/* libraries, in dependency order
CPMAddPackage("gh:ksergey/turboq#master")
CPMAddPackage("gh:ksergey/turboq-reactor#master")
CPMAddPackage("gh:ksergey/turboq-logger#master")
CPMAddPackage("gh:ksergey/xxx#master")
CPMAddPackage("gh:ksergey/sbe-code-gen#master")

if (turbofw_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(code)
