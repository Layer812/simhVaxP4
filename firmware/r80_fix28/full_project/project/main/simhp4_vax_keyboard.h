#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int simhp4_vax_keyboard_init(void);
int simhp4_vax_keyboard_enqueue(uint8_t ch);
uint32_t simhp4_vax_keyboard_drop_count(void);
uint32_t simhp4_vax_keyboard_dequeue_count(void);
uint32_t simhp4_vax_keyboard_pending_count(void);

int simhp4_a164_keyboard_start(void);
int simhp4_a164_keyboard_present(void);

#ifdef __cplusplus
}
#endif
