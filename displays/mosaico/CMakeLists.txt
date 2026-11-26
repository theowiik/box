# The box on an ESP-Mosaico: one target for the portable core in ../../core.
#
# It builds against an esp-mosaico-vibe workspace, which supplies the board
# support, ESP-Iris and the install tooling. Point MOSAICO_WORKSPACE at it
# (the dev container in ../toolchain sets it; see README.md).
cmake_minimum_required(VERSION 3.16)

if(NOT MOSAICO_WORKSPACE)
    set(MOSAICO_WORKSPACE "$ENV{MOSAICO_WORKSPACE}")
endif()
if(NOT EXISTS "${MOSAICO_WORKSPACE}/.mosaico.json")
    message(FATAL_ERROR
        "MOSAICO_WORKSPACE must name an esp-mosaico-vibe checkout, got '${MOSAICO_WORKSPACE}'. "
        "Set it in the repo's .env.")
endif()
get_filename_component(MOSAICO_WORKSPACE "${MOSAICO_WORKSPACE}" ABSOLUTE)
# main/idf_component.yml expands ${MOSAICO_WORKSPACE} from the environment.
set(ENV{MOSAICO_WORKSPACE} "${MOSAICO_WORKSPACE}")
set(MOSAICO_UTILS_ROOT "${MOSAICO_WORKSPACE}/submodule/esp-mosaico-utils")
set(MOSAICO_BSP_ROOT "${MOSAICO_WORKSPACE}/submodule/esp-mosaico-bsp")

if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()
if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.application.defaults")
endif()
# Machine-local settings, such as CONFIG_BOX_TOKEN. Gitignored.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.local.defaults")
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.local.defaults")
endif()

# The box's logic, shared with any other target: the component "core".
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../core"
    "${MOSAICO_UTILS_ROOT}/mosaico-tools/components/esp_mosaico_app_recovery")
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/mosaico_idf_project.cmake")
project(box VERSION 1.0.0)
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/system_update.cmake")
