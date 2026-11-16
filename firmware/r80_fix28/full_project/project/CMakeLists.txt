cmake_minimum_required(VERSION 3.16)

get_filename_component(SIMHP4_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SIMHP4_ROOT "${SIMHP4_ROOT}" CACHE PATH "simhP4 source root" FORCE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(EXTRA_COMPONENT_DIRS
    "${SIMHP4_ROOT}/components/M5GFX"
    "${SIMHP4_ROOT}/components/M5Unified"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options(
    -DBOARD_HAS_PSRAM
    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
)

project(simhp4_vax630_visual_media_probe_v12)
