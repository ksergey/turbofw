# Examples, one executable per file

file(GLOB ExampleSources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

foreach (file ${ExampleSources})
    get_filename_component(name "${file}" NAME_WE)
    add_executable(example-${name}
        ${file})
    target_link_libraries(example-${name}
        PRIVATE turbofw::turbofw cxxopts::cxxopts)
    target_compile_definitions(example-${name} PRIVATE CXXOPTS_NO_RTTI)
endforeach()

file(COPY env.json DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
