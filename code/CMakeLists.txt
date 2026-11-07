set(TargetName turbofw)

# Framework message protocols (SBE):
#   schemas/<file>.xml -> <turbofw/<package>>/schema.h | namespace <package>
# All schemas are generated together (SbeMakeCodecBundle): the types they
# share via schemas/common_types.xml become one set of fragment headers used
# by every schema, so md::MessageHeader and svc::MessageHeader are the same
# C++ type (required by TUs -- e.g. tests -- that use two protocols at once).
set(Schemas
    md:market_data_schema    # market data: package md, schemaId 1
    meta:metadata_schema     # metadata (reference data): package meta, schemaId 2
    trd:trading_schema       # trading connector: package trd, schemaId 3
    svc:service_schema)      # common service messages: package svc, schemaId 4

set(SchemaPackages)
set(SchemaFiles)
foreach (entry IN LISTS Schemas)
    string(REPLACE ":" ";" entry "${entry}")
    list(GET entry 0 package)
    list(GET entry 1 file)
    list(APPEND SchemaPackages ${package})
    list(APPEND SchemaFiles ${PROJECT_SOURCE_DIR}/schemas/${file}.xml)
endforeach()
SbeMakeCodecBundle(${TargetName}
    PACKAGES ${SchemaPackages}
    SCHEMAS  ${SchemaFiles}
    OUTPUT   ${CMAKE_CURRENT_BINARY_DIR}/sbe
    INCLUDE_BASE turbofw
    GENERATOR cpp-split
    SCHEMA_DEPS ${PROJECT_SOURCE_DIR}/schemas/common_types.xml
)

add_library(${TargetName} STATIC)
target_compile_features(${TargetName}
    PUBLIC cxx_std_23)
set_target_properties(${TargetName}
    PROPERTIES
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON)
target_compile_options(${TargetName}
    PUBLIC -Wall -Wextra -Wattributes -Wpedantic -Wstrict-aliasing -Wcast-align -g -fno-rtti)
target_include_directories(${TargetName}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${TargetName}
    PUBLIC turboq::turboq turboq::reactor nlohmann_json::nlohmann_json fmt::fmt ${TargetName}::md-sbe
        ${TargetName}::meta-sbe ${TargetName}::trd-sbe ${TargetName}::svc-sbe)

if (turbofw_SANITIZER AND turbofw_TSAN)
    message(FATAL_ERROR "turbofw_SANITIZER (ASan/UBSan/LeakSanitizer) and turbofw_TSAN (ThreadSanitizer) "
        "cannot be enabled at the same time -- they instrument the binary in incompatible ways. "
        "Build twice, once with each, instead.")
endif()

if (turbofw_SANITIZER)
    target_compile_options(${TargetName}
        PUBLIC -fsanitize=address,leak,undefined)
    target_link_options(${TargetName}
        PUBLIC -fsanitize=address,leak,undefined)
endif()

if (turbofw_TSAN)
    target_compile_options(${TargetName}
        PUBLIC -fsanitize=thread)
    target_link_options(${TargetName}
        PUBLIC -fsanitize=thread)
endif()

target_compile_definitions(${TargetName}
    PRIVATE TURBOFW_VERSION="${PROJECT_VERSION}")

file(GLOB_RECURSE Sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/turbofw/*.cpp")
file(GLOB_RECURSE Headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/turbofw/*.h")

if (turbofw_BUILD_TESTS)
    find_package(Threads REQUIRED)
    TurboQAddTestsFromSourceList(Sources
        PREFIX ${TargetName}
        COMPILE_OPTIONS -Wall -Wextra -g
        LIBS doctest::doctest_with_main Threads::Threads ${TargetName})
endif()

if (turbofw_BUILD_BENCHMARKS)
    TurboQAddBenchmarksFromSourceList(Sources
        PREFIX ${TargetName}
        COMPILE_OPTIONS -Wall -Wextra -g
        LIBS benchmark::benchmark_main ${TargetName})
endif()

TurboQExcludeTestsAndBenchmarksFromSourceList(Sources)

foreach(file ${Headers})
    target_sources(${TargetName} PUBLIC ${file})
endforeach()

foreach(file ${Sources})
    target_sources(${TargetName} PRIVATE ${file})
endforeach()

add_library(${TargetName}::${TargetName} ALIAS ${TargetName})
