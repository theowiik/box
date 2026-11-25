# Host build of the portable core: the same box_core sources the device runs,
# compiled for this machine and tested under AddressSanitizer. Compiling here
# also enforces the core's boundary: box_core may not include ESP-IDF headers.
cmake_minimum_required(VERSION 3.20)
project(box_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)
# The device gets cJSON from the ESP Component Registry (espressif/cjson);
# the host takes the same library from upstream. SOURCE_SUBDIR points nowhere
# so only the sources are fetched, not cJSON's own build.
FetchContent_Declare(cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG v1.7.18
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR none)
FetchContent_MakeAvailable(cjson)
add_library(cjson STATIC "${cjson_SOURCE_DIR}/cJSON.c")
target_include_directories(cjson PUBLIC "${cjson_SOURCE_DIR}")

set(CORE "${CMAKE_CURRENT_LIST_DIR}/../components/box_core")
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${CORE}/src/*.c")
add_library(box_core STATIC ${CORE_SOURCES})
target_include_directories(box_core PUBLIC "${CORE}/include" PRIVATE "${CORE}/src")
target_link_libraries(box_core PUBLIC cjson m)

option(BOX_SANITIZE "Build the host tests with ASan and UBSan" ON)
set(WARNINGS -Wall -Wextra -Werror -Wshadow -Wno-unused-parameter)
target_compile_options(box_core PRIVATE ${WARNINGS})
if(BOX_SANITIZE)
    set(SANITIZERS -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_compile_options(box_core PRIVATE ${SANITIZERS})
    target_compile_options(cjson PRIVATE ${SANITIZERS})
endif()

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/tests/*.c")
add_executable(box_core_tests ${TEST_SOURCES})
target_include_directories(box_core_tests PRIVATE "${CORE}/src")
target_link_libraries(box_core_tests PRIVATE box_core)
target_compile_options(box_core_tests PRIVATE ${WARNINGS})
if(BOX_SANITIZE)
    target_compile_options(box_core_tests PRIVATE ${SANITIZERS})
    target_link_options(box_core_tests PRIVATE ${SANITIZERS})
endif()

enable_testing()
add_test(NAME box_core COMMAND box_core_tests)
