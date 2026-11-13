cmake_minimum_required(VERSION 4.0)

project(turbofw VERSION 0.1.0 LANGUAGES CXX)

option(turbofw_SANITIZER "Build with sanitizer" OFF)
option(turbofw_TSAN "Build with ThreadSanitizer" OFF)
option(turbofw_BUILD_TESTS "Build tests (*_test.cpp files, linked against doctest)" ON)
option(turbofw_BUILD_BENCHMARKS "Build benchmarks (*_bm.cpp files, linked against google-benchmark)" ON)
option(turbofw_BUILD_APPS "Build apps (apps/*/main.cpp, service executables)" ON)
option(turbofw_BUILD_EXAMPLES "Build examples (examples/*.cpp, linked against the framework)" ON)

include(cmake/GetCPM.cmake)

CPMAddPackage("gh:nlohmann/json@3.12.0")
CPMAddPackage("gh:doctest/doctest@2.5.3")
CPMAddPackage("gh:fmtlib/fmt#12.2.0")
CPMAddPackage("gh:jarro2783/cxxopts@3.3.1")
CPMAddPackage("gh:rollbear/strong_type@16")
# the repo tags its releases without the "v" prefix, which the shorthand form would add
CPMAddPackage(
    NAME sqlite-amalgamation
    GITHUB_REPOSITORY azadkuh/sqlite-amalgamation
    GIT_TAG 3.38.2
)

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

# turbofw_SBE_CODE_GEN=/path/to/sbe-code-gen points CPM at a local checkout of
# the generator instead of gh:ksergey/sbe-code-gen#master (local development)
set(turbofw_SBE_CODE_GEN "" CACHE PATH "Local sbe-code-gen checkout to use instead of gh:ksergey/sbe-code-gen#master")
if (turbofw_SBE_CODE_GEN)
    CPMAddPackage(NAME sbe-code-gen SOURCE_DIR ${turbofw_SBE_CODE_GEN})
else()
    CPMAddPackage("gh:ksergey/sbe-code-gen#master")
endif()

if (turbofw_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(code)

if (turbofw_BUILD_APPS)
    add_subdirectory(apps)
endif()

if (turbofw_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
