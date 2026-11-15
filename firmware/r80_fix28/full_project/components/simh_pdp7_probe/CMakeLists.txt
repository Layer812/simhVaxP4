get_filename_component(SIMHP4_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(SIMH_DIR "${SIMHP4_ROOT}/third_party/simh")

idf_component_register(
    SRCS
        "probe_anchor.c"
        "simhp4_machine.c"
        "simhp4_runtime.c"
        "simhp4_media.c"
        "simhp4_g2_local.c"
        "simhp4_type340_surface.c"
        "simhp4_rb09_persist.c"
        "simhp4_st_migration.c"
        "simhp4_btools_migration.c"
        "simhp4_graphics2_core.c"
        "simhp4_graphics2_bridge.c"
    INCLUDE_DIRS
        "."
        "${SIMH_DIR}"
        "${SIMH_DIR}/PDP18B"
        "${SIMH_DIR}/display"
    EMBED_FILES
        "assets/image.fs"
        "assets/boot.rim"
    REQUIRES
        freertos
        heap
        simhp4_remote_tty
)

if(CMAKE_BUILD_EARLY_EXPANSION)
    return()
endif()

if(NOT EXISTS "${SIMH_DIR}/PDP18B/pdp18b_cpu.c")
    message(FATAL_ERROR "SIMHP4 R0A1F: Open SIMH source missing at ${SIMH_DIR}")
endif()

set(SIMHP4_PDP7_MIN_MACHINE_SOURCES
    "${SIMH_DIR}/PDP18B/pdp18b_cpu.c"
    "${SIMH_DIR}/PDP18B/pdp18b_stddev.c"
    "${SIMH_DIR}/PDP18B/pdp18b_rb.c"
    "${SIMH_DIR}/PDP18B/pdp18b_g2tty.c"
    "${SIMH_DIR}/display/type340.c"
    "${CMAKE_CURRENT_LIST_DIR}/simhp4_type340_bridge.c"
)

add_library(simhp4_pdp7_min_machine OBJECT ${SIMHP4_PDP7_MIN_MACHINE_SOURCES})
set_target_properties(simhp4_pdp7_min_machine PROPERTIES C_STANDARD 99)
target_include_directories(simhp4_pdp7_min_machine PRIVATE
    "${SIMH_DIR}"
    "${SIMH_DIR}/PDP18B"
    "${SIMH_DIR}/display"
    "${SIMHP4_ROOT}/components/simhp4_remote_tty/include"
)
target_compile_definitions(simhp4_pdp7_min_machine PRIVATE
    PDP7=1
    NOT_MUX_USING_CODE=1
    SIM_BUILD_TOOL=RetroP4_ESP_IDF_R0A5
)
target_compile_options(simhp4_pdp7_min_machine PRIVATE
    -Wno-error=all
    -Wno-error=format
    -Wno-error=unused-value
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-parameter
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    PDP7=1
    NOT_MUX_USING_CODE=1
    SIM_BUILD_TOOL=RetroP4_ESP_IDF_R0A3
)

target_sources(${COMPONENT_LIB} PRIVATE $<TARGET_OBJECTS:simhp4_pdp7_min_machine>)
add_dependencies(${COMPONENT_LIB} simhp4_pdp7_min_machine)

message(STATUS "SIMHP4 R0A5: persistent interactive UNIX V0 G2 shell")

# SIMHP4_VAX630_COMPILE_PROBE
#
# Compile-only MicroVAX II probe. This target is EXCLUDE_FROM_ALL and is never
# linked into the PDP-7 application. PDP-7 and VAX both export simulator-global
# names such as sim_devices, sim_name and cpu_unit, so the machines remain
# isolated at this stage.
#
# The VAX probe deliberately uses a separate exact Open SIMH tree:
#   third_party/simh_vax630_87eb
# This avoids mixing the accepted PDP-7 GRAPHIC-II source lineage with the VAX
# source revision.
set(SIMHP4_VAX_SIMH_DIR
    "${SIMHP4_ROOT}/third_party/simh_vax630_87eb")

set(SIMHP4_VAX630_PROBE_SOURCES
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_cpu.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_cpu1.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_fpa.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_cis.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_octa.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_cmode.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_mmu.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_sys.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_syscm.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_watch.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax630_stddev.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax630_sysdev.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax630_io.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax630_syslist.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_va.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_vc.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_lk.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_vs.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_2681.c"
    "${SIMHP4_VAX_SIMH_DIR}/VAX/vax_gpx.c"
    "${SIMHP4_VAX_SIMH_DIR}/PDP11/pdp11_rl.c"
    "${SIMHP4_VAX_SIMH_DIR}/PDP11/pdp11_rq.c"
    "${SIMHP4_VAX_SIMH_DIR}/PDP11/pdp11_ts.c"
    "${SIMHP4_VAX_SIMH_DIR}/PDP11/pdp11_dz.c"
    "${SIMHP4_VAX_SIMH_DIR}/PDP11/pdp11_lp.c"
    "${SIMHP4_VAX_SIMH_DIR}/PDP11/pdp11_tq.c"
    "${SIMHP4_VAX_SIMH_DIR}/PDP11/pdp11_xq.c"
    "${SIMHP4_VAX_SIMH_DIR}/PDP11/pdp11_vh.c"
    "${SIMHP4_VAX_SIMH_DIR}/PDP11/pdp11_cr.c"
    "${SIMHP4_VAX_SIMH_DIR}/PDP11/pdp11_td.c"
    "${SIMHP4_VAX_SIMH_DIR}/PDP11/pdp11_io_lib.c"
    "${SIMHP4_VAX_SIMH_DIR}/PDP11/pdp11_dup.c"
)

set(SIMHP4_VAX630_PROBE_MISSING "")
foreach(_src IN LISTS SIMHP4_VAX630_PROBE_SOURCES)
    if(NOT EXISTS "${_src}")
        list(APPEND SIMHP4_VAX630_PROBE_MISSING "${_src}")
    endif()
endforeach()

if(SIMHP4_VAX630_PROBE_MISSING)
    message(STATUS
        "SIMHP4 VAX630 compile probe disabled; run the V0 vendor installer first")
else()
    # SIMHP4_VAX630_DIR_ISOLATION_R8D
    #
    # ESP-IDF's compatibility layer installs global compile options on this
    # directory via generator expressions.  A plain add_library() inherits
    # those options at target creation time, including -Werror=all.
    #
    # For this isolated portability probe only, create the VAX object target
    # with the directory COMPILE_OPTIONS temporarily empty.  Restore the
    # directory immediately so the accepted PDP-7 component and any later
    # targets keep the normal ESP-IDF policy.
    get_directory_property(_simhp4_vax630_saved_dir_compile_options
        COMPILE_OPTIONS)
    set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")

    add_library(simhp4_vax630_compile_objs OBJECT EXCLUDE_FROM_ALL
        ${SIMHP4_VAX630_PROBE_SOURCES})

    set_property(DIRECTORY PROPERTY COMPILE_OPTIONS
        "${_simhp4_vax630_saved_dir_compile_options}")

    set_target_properties(simhp4_vax630_compile_objs PROPERTIES C_STANDARD 99)
    target_include_directories(simhp4_vax630_compile_objs PRIVATE
        "${SIMHP4_VAX_SIMH_DIR}"
        "${SIMHP4_VAX_SIMH_DIR}/VAX"
        "${SIMHP4_VAX_SIMH_DIR}/PDP11"
    )
    target_compile_definitions(simhp4_vax630_compile_objs PRIVATE
        VM_VAX=1
        VAX_630=1
        USE_INT64=1
        USE_ADDR64=1
        SIM_BUILD_TOOL=RetroP4_ESP_IDF_V0
    )
    # SIMHP4_VAX630_WARNING_POLICY_R8D
    #
    # Re-apply ESP-IDF's real common C compile options to the isolated VAX
    # object target, excluding only warnings-as-errors controls.  Do not use
    # a per-warning exception list.
    idf_build_get_property(_simhp4_vax630_idf_compile_options
        COMPILE_OPTIONS)
    idf_build_get_property(_simhp4_vax630_idf_c_compile_options
        C_COMPILE_OPTIONS)

    set(_simhp4_vax630_probe_compile_options
        ${_simhp4_vax630_idf_compile_options}
        ${_simhp4_vax630_idf_c_compile_options})

    list(FILTER _simhp4_vax630_probe_compile_options
        EXCLUDE REGEX "^-Werror($|=.*)")
    list(FILTER _simhp4_vax630_probe_compile_options
        EXCLUDE REGEX "^-Wno-error($|=.*)")

    target_compile_options(simhp4_vax630_compile_objs PRIVATE
        ${_simhp4_vax630_probe_compile_options}
        -Wno-unused-function
        -Wno-unused-variable
        -Wno-unused-parameter
        -include
        "${CMAKE_CURRENT_LIST_DIR}/simhp4_vax630_esp_preinclude.h"
    )
    add_custom_target(simhp4_vax630_compile_probe
        DEPENDS simhp4_vax630_compile_objs)
    message(STATUS
        "SIMHP4 V0: MicroVAX II VAX_630 compile-only probe target enabled")
endif()

# SIMHP4_VAX630_SIMCORE_COMPILE_PROBE_R9
#
# Exact Open SIMH SIM_SOURCES from pinned commit 87eb7d5...
# Compile-only portability surface for the MicroVAX II simhz64 core.
set(SIMHP4_VAX630_SIMCORE_SOURCES
    "${SIMHP4_VAX_SIMH_DIR}/scp.c"
    "${SIMHP4_VAX_SIMH_DIR}/sim_card.c"
    "${SIMHP4_VAX_SIMH_DIR}/sim_console.c"
    "${SIMHP4_VAX_SIMH_DIR}/sim_disk.c"
    "${SIMHP4_VAX_SIMH_DIR}/sim_ether.c"
    "${SIMHP4_VAX_SIMH_DIR}/sim_fio.c"
    "${SIMHP4_VAX_SIMH_DIR}/sim_imd.c"
    "${SIMHP4_VAX_SIMH_DIR}/sim_scsi.c"
    "${SIMHP4_VAX_SIMH_DIR}/sim_serial.c"
    "${CMAKE_CURRENT_LIST_DIR}/simhp4_vax630_sim_sock_esp.c"
    "${SIMHP4_VAX_SIMH_DIR}/sim_tape.c"
    "${CMAKE_CURRENT_LIST_DIR}/simhp4_vax630_sim_timer_esp.c"
    "${SIMHP4_VAX_SIMH_DIR}/sim_tmxr.c"
    "${SIMHP4_VAX_SIMH_DIR}/sim_video.c"
)

if(NOT TARGET simhp4_vax630_simcore_objs)
    get_directory_property(_simhp4_vax630_simcore_saved_dir_compile_options
        COMPILE_OPTIONS)
    set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")

    add_library(simhp4_vax630_simcore_objs OBJECT EXCLUDE_FROM_ALL
        ${SIMHP4_VAX630_SIMCORE_SOURCES})

    set_property(DIRECTORY PROPERTY COMPILE_OPTIONS
        "${_simhp4_vax630_simcore_saved_dir_compile_options}")

    set_target_properties(simhp4_vax630_simcore_objs PROPERTIES C_STANDARD 99)

    target_include_directories(simhp4_vax630_simcore_objs PRIVATE
        "${SIMHP4_VAX_SIMH_DIR}"
    )

    target_compile_definitions(simhp4_vax630_simcore_objs PRIVATE
        USE_SIM_CARD=1
        USE_SIM_IMD=1
        USE_INT64=1
        USE_ADDR64=1
        SIM_BUILD_TOOL=RetroP4_ESP_IDF_V0_R9
    )

    idf_build_get_property(_simhp4_vax630_simcore_idf_compile_options
        COMPILE_OPTIONS)
    idf_build_get_property(_simhp4_vax630_simcore_idf_c_compile_options
        C_COMPILE_OPTIONS)

    set(_simhp4_vax630_simcore_probe_compile_options
        ${_simhp4_vax630_simcore_idf_compile_options}
        ${_simhp4_vax630_simcore_idf_c_compile_options})

    list(FILTER _simhp4_vax630_simcore_probe_compile_options
        EXCLUDE REGEX "^-Werror($|=.*)")
    list(FILTER _simhp4_vax630_simcore_probe_compile_options
        EXCLUDE REGEX "^-Wno-error($|=.*)")

    target_compile_options(simhp4_vax630_simcore_objs PRIVATE
        ${_simhp4_vax630_simcore_probe_compile_options}
        -Wno-unused-function
        -Wno-unused-variable
        -Wno-unused-parameter
        -include
        "${CMAKE_CURRENT_LIST_DIR}/simhp4_vax630_esp_preinclude.h"
    )

    add_custom_target(simhp4_vax630_simcore_compile_probe
        DEPENDS simhp4_vax630_simcore_objs)
endif()
