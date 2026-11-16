/* SPDX-License-Identifier: MIT
 * R6R40: only guest TTO characters from the terminal presenter are fed here.
 * The observed single-user startup line ends in "intr ^C" before a BOL "# ".
 * This is a conservative screen readiness gate, NOT a cryptographic guest
 * authentication or a proof that BSD is healthy. On an unknown banner it
 * fails closed and leaves Wi-Fi UI off. Never touches SIMH CPU/IRQ/CLK/RQ.
 */
#include "simh_wifi_boot_gate.h"
#include <string.h>
#include <stdatomic.h>

static char window[48];
static unsigned length;
static atomic_int ready = ATOMIC_VAR_INIT(0);
static int saw_login_hint;
static int at_bol=1;
static int saw_hash;

void simhp4_wifi_boot_gate_feed(uint8_t ch) {
    if (atomic_load_explicit(&ready, memory_order_acquire)) return;
    if (ch == 0x1b || ch == '\b') { /* VT100 controls: never arm from escape data */
        saw_hash=0;
        return;
    }
    if (ch == '\n' || ch == '\r') {
        at_bol=1;
        saw_hash=0;
        return;
    }
    if (ch < 0x20 || ch > 0x7e) return;
    if (length < sizeof(window)-1) window[length++]=(char)ch;
    else {
        memmove(window,window+1,sizeof(window)-2);
        window[sizeof(window)-2]=(char)ch;
        length=sizeof(window)-1;
    }
    window[length]=0;
    if (strstr(window,"intr ^C") || strstr(window,"intr ^c"))
        saw_login_hint=1;
    if (saw_login_hint && at_bol && ch=='#') {
        saw_hash=1;
        at_bol=0;
        return;
    }
    if (saw_hash && ch==' ') atomic_store_explicit(&ready, 1, memory_order_release);
    saw_hash=0;
    at_bol=0;
}
int simhp4_wifi_boot_gate_ready(void) { return atomic_load_explicit(&ready, memory_order_acquire) ? 1 : 0; }
