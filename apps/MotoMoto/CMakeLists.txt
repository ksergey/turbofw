# MotoMoto: the Metadata service

file(COPY env.json DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_executable(MotoMoto
    main.cpp
    store.cpp)
target_link_libraries(MotoMoto
    PRIVATE turbofw::turbofw cxxopts::cxxopts SQLite3)
# sqlite-amalgamation's CMakeLists propagates its include dir for install only
target_include_directories(MotoMoto
    PRIVATE ${sqlite-amalgamation_SOURCE_DIR})
target_compile_definitions(MotoMoto
    PRIVATE CXXOPTS_NO_RTTI)

if (turbofw_BUILD_TESTS)
    file(GLOB TestSources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    TurboQAddTestsFromSourceList(TestSources
        PREFIX MotoMoto
        COMPILE_OPTIONS -Wall -Wextra -g
        LIBS doctest::doctest_with_main turbofw::turbofw SQLite3)
    if (TARGET MotoMoto-store-test)
        target_include_directories(MotoMoto-store-test
            PRIVATE ${sqlite-amalgamation_SOURCE_DIR})
    endif()
endif()
