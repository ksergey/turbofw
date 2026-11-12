set(TargetName turbofw)

# Framework message protocols (SBE):
#   schemas/<file>.xml -> <turbofw/schema/<package>>/schema.h | namespace <package>
# All schemas are generated together (SbeMakeCodecBundle): the types they
# share via schemas/common_types.xml become one set of fragment headers used
# by every schema, so market_data::MessageHeader and service::MessageHeader
# are the same C++ type (required by TUs -- e.g. tests -- that use two
# protocols at once).
set(Schemas
    market_data:market_data_schema    # market data: package market_data, schemaId 1
    metadata:metadata_schema          # metadata (reference data): package metadata, schemaId 2
    trading:trading_schema            # trading connector: package trading, schemaId 3
    service:service_schema)           # common service messages: package service, schemaId 4

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
    INCLUDE_BASE turbofw/schema
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
    PUBLIC turboq::turboq turboq::reactor nlohmann_json::nlohmann_json fmt::fmt strong_type::strong_type)
target_link_libraries(${TargetName}
    PUBLIC ${TargetName}::market_data-sbe ${TargetName}::metadata-sbe ${TargetName}::trading-sbe ${TargetName}::service-sbe)

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
