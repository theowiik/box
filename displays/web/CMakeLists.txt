# The box in a browser: the portable core (../../core) with a small HTTP
# server and a web page for a screen. Linux or macOS:
#   cmake -S displays/web -B displays/web/build && cmake --build displays/web/build
#   displays/web/build/box-web        (or: mise run web)
cmake_minimum_required(VERSION 3.20)
project(box_web C)

set(BOX_SANITIZE OFF CACHE BOOL "" FORCE)
add_subdirectory(../../core core EXCLUDE_FROM_ALL)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/page.inc"
    COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/page.html
            -DOUT=${CMAKE_CURRENT_BINARY_DIR}/page.inc -P ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake
    DEPENDS page.html embed.cmake)

add_executable(box-web main.c "${CMAKE_CURRENT_BINARY_DIR}/page.inc")
target_include_directories(box-web PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(box-web PRIVATE box_core)
target_compile_options(box-web PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
