#ifndef SIMHP4_VAX630_ESP_PREINCLUDE_H
#define SIMHP4_VAX630_ESP_PREINCLUDE_H

/*
 * simhP4 VAX630 / ESP32-P4 header namespace bridge.
 *
 * Open SIMH sim_defs.h defines BIT(nm) as a BITFIELD initializer.
 * ESP-IDF also uses BIT(n) as a numeric bit-mask macro in its RISC-V headers.
 *
 * Preload the host socket/header graph before SIMH defines BIT. Then remove
 * only the known host macros which collide with the VAX/SIMH namespace.
 */

#include <sys/socket.h>

#ifdef BIT
#undef BIT
#endif

#ifdef PTE_V
#undef PTE_V
#endif

#endif /* SIMHP4_VAX630_ESP_PREINCLUDE_H */
