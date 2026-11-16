# MotoMoto: the Metadata service
set(TargetName MotoMoto)

file(COPY config.json env.json DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_executable(${TargetName})
target_link_libraries(${TargetName}
    PRIVATE turbofw::turbofw cxxopts::cxxopts)
target_compile_definitions(${TargetName}
    PRIVATE CXXOPTS_NO_RTTI)

file(GLOB_RECURSE Sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE Headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

if (turbofw_BUILD_TESTS)
    TurboQAddTestsFromSourceList(Sources
        PREFIX ${TargetName}
        COMPILE_OPTIONS -Wall -Wextra -g
        LIBS doctest::doctest_with_main turbofw::turbofw)
endif()

TurboQExcludeTestsAndBenchmarksFromSourceList(Sources)

target_sources(${TargetName} PRIVATE ${Sources})
