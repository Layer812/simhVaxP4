#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime-owned TTO FIFO.  Producer is Open SIMH sim_os_putchar(). */
int simhp4_vax_terminal_fifo_init(void);
int simhp4_vax_terminal_dequeue_timed(uint8_t *ch, uint32_t timeout_ms);
uint32_t simhp4_vax_terminal_drop_count(void);

/* Host-only Tab5 M5GFX presenter. */
int simhp4_vax_terminal_start(void);

#ifdef __cplusplus
}
#endif
