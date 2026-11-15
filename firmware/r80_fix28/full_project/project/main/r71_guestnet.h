/* simhP4 R71 guest Ethernet/LwIP attachment, development only.
 * The XQ adapter uses this interface from its CPU-owner thread.
 * Licensed under MIT; copyright (c) 2026.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define R71_ETH_MTU_FRAME 1514u
/* Called from the Wi-Fi worker after confirmed STA GOT_IP, never an ESP event callback.
 * Waits for lwIP netif creation; call once only after esp_netif_init. */
bool r71_guestnet_start(void);
/* Guest-emulator thread only. Never waits for lwIP mailbox space.
 * Valid frames complete the virtual NIC DMA even when Wi-Fi is down or the
 * host transport is congested; such losses are bounded and counted. */
bool r71_guestnet_write(const uint8_t *ethernet, size_t length);
/* Guest-emulator thread only; 0 means no received packet is pending. */
size_t r71_guestnet_read(uint8_t *dst, size_t capacity);
/* Callable after Wi-Fi failure; prevents additional guest-to-Wi-Fi forwarding. */
void r71_guestnet_link(bool up);
uint32_t r71_guestnet_rx_drops(void);
uint32_t r71_guestnet_tx_drops(void);
#ifdef __cplusplus
}
#endif
