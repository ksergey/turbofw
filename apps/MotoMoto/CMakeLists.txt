# MotoMoto: the Metadata service
set(TargetName MotoMoto)

file(COPY config.json env.json DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_executable(${TargetName})
target_link_libraries(${TargetName}
    PRIVATE turbofw::turbofw cxxopts::cxxopts SQLite3)
# sqlite-amalgamation's CMakeLists propagates its include dir for install only
target_include_directories(${TargetName}
    PRIVATE ${sqlite-amalgamation_SOURCE_DIR})
target_compile_definitions(${TargetName}
    PRIVATE CXXOPTS_NO_RTTI)

file(GLOB_RECURSE Sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE Headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

if (turbofw_BUILD_TESTS)
    TurboQAddTestsFromSourceList(Sources
        PREFIX ${TargetName}
        COMPILE_OPTIONS -Wall -Wextra -g
        LIBS doctest::doctest_with_main turbofw::turbofw)

    if (TARGET MotoMoto-store-test)
        target_link_libraries(MotoMoto-store-test
            PRIVATE SQLite3)
        target_include_directories(MotoMoto-store-test
            PRIVATE ${sqlite-amalgamation_SOURCE_DIR})
    endif()
endif()

TurboQExcludeTestsAndBenchmarksFromSourceList(Sources)

target_sources(${TargetName} PRIVATE ${Sources})
