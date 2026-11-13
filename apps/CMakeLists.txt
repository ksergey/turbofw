# Each app lives in its own subdirectory with its own CMakeLists.txt
file(GLOB turbofw_app_dirs RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/*/CMakeLists.txt")
foreach (app_cmake ${turbofw_app_dirs})
    get_filename_component(app_dir ${app_cmake} DIRECTORY)
    add_subdirectory(${app_dir})
endforeach ()
