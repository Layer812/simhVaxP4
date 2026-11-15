/* R6R40: guest TTO-only shell-ready hint; no guest execution modifications. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void simhp4_wifi_boot_gate_feed(uint8_t ch);
int simhp4_wifi_boot_gate_ready(void);
#ifdef __cplusplus
}
#endif
