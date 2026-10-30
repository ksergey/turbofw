cmake_minimum_required(VERSION 4.0)

project(turbofw VERSION 0.1.0 LANGUAGES CXX)

option(turbofw_SANITIZER "Build with sanitizer" OFF)
option(turbofw_TSAN "Build with ThreadSanitizer" OFF)
option(turbofw_BUILD_TESTS "Build tests (*_test.cpp files, linked against doctest)" ON)
option(turbofw_BUILD_BENCHMARKS "Build benchmarks (*_bm.cpp files, linked against google-benchmark)" ON)

include(cmake/GetCPM.cmake)

CPMAddPackage("gh:nlohmann/json@3.12.0")
CPMAddPackage("gh:doctest/doctest@2.5.3")
CPMAddPackage("gh:fmtlib/fmt#12.2.0")
CPMAddPackage("gh:jarro2783/cxxopts@3.3.1")

if (turbofw_BUILD_BENCHMARKS)
    CPMAddPackage(
        NAME benchmark
        GITHUB_REPOSITORY google/benchmark
        VERSION 1.10.0
        OPTIONS "BENCHMARK_ENABLE_TESTING OFF"
    )
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
