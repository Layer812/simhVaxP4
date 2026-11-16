# SIMHP4_V12_DISPLAY_READONLY_MEDIA
# SIMHP4_V11C_CPU_RESET_MINIMAL_SYSTEM_LIST
# SIMHP4_V11B_LIVE_OBJECT_CLOSURE
# R10B isolated MicroVAX II link-closure component.
# 46 SIMH sources compile in a direct OBJECT target using the proven R8D
# warning-policy isolation.  The IDF main component contains only the harness.

set(SIMH "${SIMHP4_ROOT}/third_party/simh_vax630_87eb")
set(PORT "${SIMHP4_ROOT}/components/simh_pdp7_probe")

set(VAX630_MACHINE
    "${CMAKE_CURRENT_LIST_DIR}/simhp4_vax630_cpu_esp_v14k.c"
    "${SIMH}/VAX/vax_cpu1.c"
    "${SIMH}/VAX/vax_fpa.c"
    "${SIMH}/VAX/vax_cis.c"
    "${SIMH}/VAX/vax_octa.c"
    "${SIMH}/VAX/vax_cmode.c"
    "${SIMH}/VAX/vax_mmu.c"
    "${SIMH}/VAX/vax_sys.c"
    "${SIMH}/VAX/vax_syscm.c"
    "${SIMH}/VAX/vax_watch.c"
    "${SIMH}/VAX/vax630_stddev.c"
    "${CMAKE_CURRENT_LIST_DIR}/simhp4_vax630_sysdev_esp_r14h.c"
    "${CMAKE_CURRENT_LIST_DIR}/simhp4_vax630_io_esp_r14e.c"
    "${CMAKE_CURRENT_LIST_DIR}/simhp4_vax630_v12_syslist_rq_v13_boot.c"
    "${SIMH}/VAX/vax_va.c"
    "${SIMH}/VAX/vax_vc.c"
    "${SIMH}/VAX/vax_lk.c"
    "${SIMH}/VAX/vax_vs.c"
    "${SIMH}/VAX/vax_2681.c"
    "${SIMH}/VAX/vax_gpx.c"
    "${SIMH}/PDP11/pdp11_rl.c"
    "${CMAKE_CURRENT_LIST_DIR}/simhp4_pdp11_rq_esp_v12f.c"
    "${SIMH}/PDP11/pdp11_ts.c"
    "${SIMH}/PDP11/pdp11_dz.c"
    "${SIMH}/PDP11/pdp11_lp.c"
    "${SIMH}/PDP11/pdp11_tq.c"
    "${SIMH}/PDP11/pdp11_xq.c"
    "${SIMH}/PDP11/pdp11_vh.c"
    "${SIMH}/PDP11/pdp11_cr.c"
    "${SIMH}/PDP11/pdp11_td.c"
    "${SIMH}/PDP11/pdp11_io_lib.c"
    "${SIMH}/PDP11/pdp11_dup.c"
)

set(VAX630_SIMCORE
    "${SIMH}/scp.c"
    "${CMAKE_CURRENT_LIST_DIR}/simhp4_vax630_scp_posix_esp_v12e.c"
    "${SIMH}/sim_card.c"
    "simhp4_sim_console_esp_v14c.c"
    "${SIMH}/sim_disk.c"
    "${SIMH}/sim_ether.c"
    "${SIMH}/sim_fio.c"
    "${SIMH}/sim_imd.c"
    "${SIMH}/sim_scsi.c"
    "${SIMH}/sim_serial.c"
    "${PORT}/simhp4_vax630_sim_sock_esp.c"
    "${SIMH}/sim_tape.c"
    "${PORT}/simhp4_vax630_sim_timer_esp_v11d.c"
    "${SIMH}/sim_tmxr.c"
    "${SIMH}/sim_video.c"
)

# The IDF component itself is tiny and may keep normal ESP-IDF warnings-as-errors.
idf_component_register(
    SRCS
        "v13_main.cpp"
        "simhp4_sd.cpp"
        "simhp4_a164_keyboard.c"
        "simhp4_vax_terminal_r14f.cpp"
        "simhp4_wifi_badge.c"
        "simhp4_wifi_settings.c"
        "simhp4_wifi_overlay.cpp"
        "simh_wifi_boot_gate.c"
        "simhp4_wifi_backend.cpp"
    INCLUDE_DIRS
        "."
        "${SIMH}"
        "${SIMH}/VAX"
        "${SIMH}/PDP11"
    REQUIRES
        freertos
        M5Unified
        M5GFX
        esp_hosted
        esp_wifi_remote
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_common
        log
        fatfs
        esp_driver_sdmmc
        sdmmc
        esp_driver_gpio
        esp_driver_i2c
        newlib
)

# R8D-proven policy:
# create the 46-source portability OBJECT target while directory COMPILE_OPTIONS
# are empty, then immediately restore the normal IDF directory policy.
get_directory_property(_r10b_saved_dir_compile_options COMPILE_OPTIONS)
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")

add_library(simhp4_vax630_r10b_objs OBJECT EXCLUDE_FROM_ALL
    ${VAX630_MACHINE}
    ${VAX630_SIMCORE}
)

set_property(DIRECTORY PROPERTY COMPILE_OPTIONS
    "${_r10b_saved_dir_compile_options}")

set_target_properties(simhp4_vax630_r10b_objs PROPERTIES C_STANDARD 99)

target_include_directories(simhp4_vax630_r10b_objs PRIVATE
    "${SIMH}"
    "${SIMH}/VAX"
    "${SIMH}/PDP11"
)

target_compile_definitions(simhp4_vax630_r10b_objs PRIVATE
    VM_VAX
    VAX_630
    USE_INT64
    USE_ADDR64
    USE_SIM_CARD=1
    USE_SIM_IMD=1
)

# SIMHP4_V12D_PRIMARY_RQ_ONLY
# Upstream pdp11_rq.c explicitly provides the RQ_NUMCT compile-time seam.
# V12 roots only rq_dev, so compile this source for controller 0 only.
set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/simhp4_pdp11_rq_esp_v12f.c" APPEND PROPERTY COMPILE_DEFINITIONS RQ_NUMCT=1)

# Carry lwIP include/interface requirements into the direct OBJECT target.
target_link_libraries(simhp4_vax630_r10b_objs PRIVATE idf::lwip idf::freertos)

# Re-apply normal IDF C flags minus only Werror/Wno-error controls.
idf_build_get_property(_r10b_idf_compile_options COMPILE_OPTIONS)
idf_build_get_property(_r10b_idf_c_compile_options C_COMPILE_OPTIONS)
set(_r10b_compile_options
    ${_r10b_idf_compile_options}
    ${_r10b_idf_c_compile_options}
)
list(FILTER _r10b_compile_options EXCLUDE REGEX "^-Werror($|=.*)")
list(FILTER _r10b_compile_options EXCLUDE REGEX "^-Wno-error($|=.*)")

target_compile_options(simhp4_vax630_r10b_objs PRIVATE
    ${_r10b_compile_options}
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-parameter
    -include
    "${PORT}/simhp4_vax630_esp_preinclude.h"
)

# SIMHP4_V12D_RUNTIME_ARCHIVE_CLOSURE
# Compile the established 46-source surface exactly as before, but do not inject
# every OBJECT directly into libmain.a.  Archive the objects, then let the final
# linker extract only members required by the CPU+RQ V12 roots.
add_library(simhp4_vax630_v12_runtime STATIC
    $<TARGET_OBJECTS:simhp4_vax630_r10b_objs>
)
add_dependencies(simhp4_vax630_v12_runtime simhp4_vax630_r10b_objs)
target_link_libraries(${COMPONENT_LIB} PRIVATE simhp4_vax630_v12_runtime)
add_dependencies(${COMPONENT_LIB} simhp4_vax630_v12_runtime)

# SIMHP4_V13_R9_VAX630_MAIN_DEFINE
# Keep local main translation units on the same Open SIMH machine model
# as the VAX630 object library.
target_compile_definitions(${COMPONENT_LIB} PRIVATE VAX_630)

# SIMHP4_V13_R10R3_EVENT_BOUNDARY_HOST_YIELD
# Open SIMH upstream vax_cpu.c is unchanged. This build uses a local
# derived copy with an ESP32 host-only FreeRTOS yield at the normal
# sim_process_event() boundary.

# SIMHP4_V13_R11_CONSOLE_INPUT_RESTORE
# Restore standard POSIX console input after R9 fixed heap corruption.
# R5 TCSANOW adaptation is retained in the local derived console.

# SIMHP4_V13_R11R2_CONSOLE_INPUT_RESTORE
# Correct R11 observability to the active POSIX sim_os_poll_kbd branch.
# Standard read(0) input and R5 TCSANOW adaptation are retained.

# SIMHP4_V13_R13_USB_SECONDARY_INPUT
# Keep UART primary; SIMH input reads /dev/secondary directly.

# SIMHP4_V13_R13R3_COMMENT_FIX
# Replace malformed R13 source with fresh R11R2-derived R13 source.

# SIMHP4_V14A_A164_TTI_BRIDGE
# R14A changes only the host input path:
# proven Tab5 A164 -> 128-byte FIFO -> sim_os_poll_kbd() -> standard VAX TTI.
# UART/USB console Kconfig, M5GFX initialization, R10 host yield, RQ and device
# closure are intentionally unchanged.

# SIMHP4_V14C_LOCAL_VAX_TERMINAL
# Host-only presenter: Open SIMH TTO -> private FIFO -> CPU0 M5GFX text terminal.
# Guest-visible VAX device semantics, R10 host yield, RQ READ_ONLY and A164 TTI are unchanged.

# SIMHP4_V14D_VT100_DA_ORIENTATION
# Host terminal only: VT100 DA/DSR response + ANSI consume + A164 180-degree display orientation.
# VAX devices, RQ READ_ONLY, scheduler, A164 ASCII path and sim_console TTO/TTI remain unchanged.

# SIMHP4_V14E_KA630_DIAGNOSTIC_OBSERVER
# Observation only: derived upstream SYSDEV/QBA add BDR + bounded IPCR logs.
# No guest-visible register values, timing policy, device closure, RQ state or terminal semantics are changed.

# SIMHP4_V14F_ENGLISH_VT100_NO_OPTIONS
# Terminal identity only: report VT100 no-options DA (ESC[?1;0c) to avoid MCS/language inquiry.
# R14E BDR/IPCR observers retained. RQ0 remains READ_ONLY.

# SIMHP4_V14G_COLD_CONSOLE_HALT_ENTRY
# ESP host-only boot-entry policy: use documented KA630 non-power-up HALT path.
# ROM bytes unchanged; R14E observers retained; RQ0 remains READ_ONLY.

# SIMHP4_V14H_BDR8_PC_OBSERVER
# Observation only: restore normal POWERUP entry and sample PC/PSL/IPCR while BDR=8.
# No ROM bytes/register semantics/RQ write policy changed.

# SIMHP4_V14K_HOST_EQUIVALENT_RAM_CLEAR
# CPU host optimization only: exact KA630 RAM clear loop is host-equivalent.
# ROM bytes/device semantics unchanged; RQ0 remains READ_ONLY.

# R71 BSD guest virtual Ethernet inside isolated build only.
target_sources(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/r71_guestnet.c")
target_link_libraries(${COMPONENT_LIB} PUBLIC idf::lwip)
