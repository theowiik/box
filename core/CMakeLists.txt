# The box's core: what to show and when. Portable C with no platform code —
# no ESP-IDF, no FreeRTOS, no display drivers — so any target can use it.
#
# Built two ways:
#  - as an ESP-IDF component, when a target such as displays/mosaico lists this
#    directory in EXTRA_COMPONENT_DIRS (cJSON comes from idf_component.yml);
#  - as a plain CMake project on any machine, with its tests:
#      cmake -S core -B core/build && cmake --build core/build && core/build/box_core_tests
if(ESP_PLATFORM)
    idf_component_register(SRC_DIRS "src"
        INCLUDE_DIRS "include"
        PRIV_INCLUDE_DIRS "src")
    # stb_truetype is third-party code: keep its warnings out of the build.
    set_source_files_properties(src/font.c PROPERTIES COMPILE_OPTIONS "-w")
    return()
endif()

cmake_minimum_required(VERSION 3.20)
project(box_core C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)
# ESP-IDF targets get cJSON from the ESP Component Registry (espressif/cjson);
# here it comes from upstream. SOURCE_SUBDIR points nowhere so only the
# sources are fetched, not cJSON's own build.
FetchContent_Declare(cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG v1.7.18
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR none)
FetchContent_MakeAvailable(cjson)
add_library(cjson STATIC "${cjson_SOURCE_DIR}/cJSON.c")
target_include_directories(cjson PUBLIC "${cjson_SOURCE_DIR}")

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/src/*.c")
add_library(box_core STATIC ${CORE_SOURCES})
target_include_directories(box_core PUBLIC include PRIVATE src)
target_link_libraries(box_core PUBLIC cjson m)

option(BOX_SANITIZE "Build with ASan and UBSan" ON)
set(WARNINGS -Wall -Wextra -Werror -Wshadow -Wno-unused-parameter)
target_compile_options(box_core PRIVATE ${WARNINGS})
# stb_truetype is third-party code: keep its warnings out of the build.
set_source_files_properties(src/font.c PROPERTIES COMPILE_OPTIONS "-w")
if(BOX_SANITIZE)
    set(SANITIZERS -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_compile_options(box_core PRIVATE ${SANITIZERS})
    target_compile_options(cjson PRIVATE ${SANITIZERS})
endif()

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/tests/*.c")
add_executable(box_core_tests ${TEST_SOURCES})
target_include_directories(box_core_tests PRIVATE src)
target_link_libraries(box_core_tests PRIVATE box_core)
target_compile_options(box_core_tests PRIVATE ${WARNINGS})
if(BOX_SANITIZE)
    target_compile_options(box_core_tests PRIVATE ${SANITIZERS})
    target_link_options(box_core_tests PRIVATE ${SANITIZERS})
endif()

enable_testing()
add_test(NAME box_core COMMAND box_core_tests)
