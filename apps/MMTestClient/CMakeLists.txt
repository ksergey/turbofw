# MMTestClient: a tool that sends requests to MotoMoto and prints the responses
set(TargetName MMTestClient)

add_executable(${TargetName})
target_link_libraries(${TargetName}
    PRIVATE turbofw::turbofw cxxopts::cxxopts)
target_compile_definitions(${TargetName}
    PRIVATE CXXOPTS_NO_RTTI)

file(GLOB_RECURSE Sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

TurboQExcludeTestsAndBenchmarksFromSourceList(Sources)

target_sources(${TargetName} PRIVATE ${Sources})
