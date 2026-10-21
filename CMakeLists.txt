cmake_minimum_required(VERSION 4.0)

project(turbofw VERSION 0.1.0 LANGUAGES CXX)

option(turbofw_SANITIZER "Build with sanitizer" OFF)
option(turbofw_TSAN "Build with ThreadSanitizer" OFF)
option(turbofw_BUILD_TESTS "Build tests (*_test.cpp files, linked against doctest)" ON)
option(turbofw_BUILD_BENCHMARKS "Build benchmarks (*_bm.cpp files, linked against google-benchmark)" ON)

include(cmake/GetCPM.cmake)

CPMAddPackage("gh:ksergey/turboq#master")
CPMAddPackage("gh:ksergey/turboq-reactor#master")
CPMAddPackage("gh:ksergey/turboq-logger#master")
CPMAddPackage("gh:ksergey/xxx#master")
CPMAddPackage("gh:ksergey/sbe-code-gen#master")

CPMAddPackage("gh:nlohmann/json@3.12.0")

if (turbofw_BUILD_TESTS)
    CPMAddPackage("gh:doctest/doctest@2.5.3")
    enable_testing()
endif()

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

add_subdirectory(code)
