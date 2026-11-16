/* simhP4 R71 - lwIP Ethernet netif + routed IPv4 NAPT into Hosted Wi-Fi.
 * DEVELOPMENT COMPONENT: native firmware compilation and guest validation pending.
 * Guest-CPU entrypoints never block on the lwIP mailbox.
 */
#include "r71_guestnet.h"
#include "sdkconfig.h"
#if !defined(CONFIG_LWIP_IP_FORWARD) || !defined(CONFIG_LWIP_IPV4_NAPT) || !defined(CONFIG_LWIP_L2_TO_L3_COPY)
#error "R71 requires IP forwarding, NAPT and L2_TO_L3_COPY (never silently disable routing)"
#endif
#include <string.h>
#include <stdatomic.h>
#include "esp_heap_caps.h"
#include "lwip/err.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "lwip/lwip_napt.h"
#include "lwip/ip4_addr.h"
#include "lwip/prot/ip.h"  /* IP_PROTO_TCP: IPv4 protocol number 6 */
#include "esp_log.h"


#define R71_FRAMES 8u
#define R71_PENDING_LIMIT 8u

typedef struct {
    uint16_t len;
    uint32_t epoch;   /* frames from an earlier Wi-Fi association are invalid */
    uint8_t bytes[R71_ETH_MTU_FRAME];
} r71_frame_t;
/* Only lwIP's TCPIP thread writes the egress ring; SIMH CPU owns its tail.
 * The inverse (ingress) uses a fixed PSRAM pool with atomic DRAM ownership.
 * There are NO packet-sized heap allocations on the guest CPU thread. */
static r71_frame_t *s_egress;
static atomic_uint s_head, s_tail, s_drop_rx, s_drop_tx;
/* Both ends must try-lock before touching egress PSRAM. No spinning or
 * blocking on the guest CPU: contention means a bounded packet drop/retry.
 * This permits the lwIP producer to retire an entire old Wi-Fi epoch before
 * queuing a new ARP reply, even if XQ has not polled the old ring yet. */
static atomic_flag s_egress_guard = ATOMIC_FLAG_INIT;
static atomic_bool s_active, s_started;
static atomic_uint s_link_epoch; /* worker signals rejoin, guest thread flushes stale RX */
static struct netif s_guest;

/* R80_FIX10_TELNET_PORTMAP_BEGIN */
/* Expose the real guest telnetd through the existing NAPT, not a second
 * terminal emulator.  No new task or socket proxy and no guest-CPU blocking.
 * P4:2323 -> 10.88.0.2:23; the BSD inetd/telnetd must be running.
 * This is cleartext TELNET: use only on a trusted private Wi-Fi/LAN and do
 * not configure upstream router forwarding to the P4 TCP/2323 port.
 */
#define SIMHP4_TELNET_EXTERNAL_PORT 2323u
#define SIMHP4_TELNET_GUEST_PORT 23u
static const char *const s_telnet_tag = "SIMHP4_BSD_TELNET";
static ip4_addr_t s_telnet_mapped_ip;  /* accessed only on TCPIP thread */
static bool s_telnet_mapped;          /* accessed only on TCPIP thread */

static bool r80_is_private_ip(const ip4_addr_t *addr)
{
    const u8_t a = ip4_addr1(addr), b = ip4_addr2(addr);
    return (a == 10) ||
           (a == 172 && b >= 16 && b <= 31) ||
           (a == 192 && b == 168);
}

static void r80_telnet_unmap_tcpip(void *arg)
{
    (void)arg;
    if (s_telnet_mapped) {
        ip_portmap_remove(IP_PROTO_TCP, SIMHP4_TELNET_EXTERNAL_PORT);
        s_telnet_mapped = false;
        ESP_LOGI(s_telnet_tag, "LAN telnet map stopped port=%u",
                 (unsigned)SIMHP4_TELNET_EXTERNAL_PORT);
    }
}

static void r80_telnet_map_tcpip(void *arg)
{
    bool *ok = (bool *)arg; /* callback_wait is synchronous in IDF 5.5.4 */
    *ok = false;
    /* Default route is the Wi-Fi STA.  Never map a guest, unconfigured,
     * loopback, or public-facing interface.  If ambiguous, leave disabled.
     */
    const struct netif *sta = netif_default;
    if (!sta || sta == &s_guest || !netif_is_up(sta) ||
        !netif_is_link_up(sta) || !r80_is_private_ip(netif_ip4_addr(sta))) {
        ESP_LOGW(s_telnet_tag, "LAN telnet map skipped: no private STA default route");
        r80_telnet_unmap_tcpip(NULL);
        return;
    }
    const ip4_addr_t *sta_ip = netif_ip4_addr(sta);
    if (s_telnet_mapped && ip4_addr_cmp(sta_ip, &s_telnet_mapped_ip)) {
        *ok = true;
        return;
    }
    r80_telnet_unmap_tcpip(NULL);
    ip4_addr_t guest;
    IP4_ADDR(&guest, 10, 88, 0, 2);
    /* All address arguments are lwIP's native network-order u32_t values.
     * Only the TCPIP core may mutate NAPT's global portmap table.
     */
    if (ip_portmap_add(IP_PROTO_TCP, ip4_addr_get_u32(sta_ip),
                       SIMHP4_TELNET_EXTERNAL_PORT, ip4_addr_get_u32(&guest),
                       SIMHP4_TELNET_GUEST_PORT) != 1) {
        ESP_LOGW(s_telnet_tag, "LAN telnet map failed; outbound NAPT unchanged");
        return;
    }
    s_telnet_mapped_ip = *sta_ip;
    s_telnet_mapped = true;
    *ok = true;
    ESP_LOGI(s_telnet_tag, "LAN telnet forward %s:%u -> 10.88.0.2:23 registered",
             ip4addr_ntoa(sta_ip), (unsigned)SIMHP4_TELNET_EXTERNAL_PORT);
}

/* Called only from the existing Wi-Fi worker after GOT_IP, which already
 * performs an R71 synchronous TCPIP callback for netif creation.
 */
static void r80_telnet_after_join(void)
{
    bool mapped = false;
    if (tcpip_callback_wait(r80_telnet_map_tcpip, &mapped) != ERR_OK || !mapped)
        ESP_LOGW(s_telnet_tag, "BSD telnet unavailable; guest outbound network retained");
}

/* Do not wait for lwIP from an SDIO-failure or guest context.  Only the
 * TCPIP core touches NAPT.  Guest egress is disabled separately by R71.
 */
static void r80_telnet_after_disconnect(void)
{
    if (tcpip_try_callback(r80_telnet_unmap_tcpip, NULL) != ERR_OK)
        ESP_LOGW(s_telnet_tag, "port-map cleanup callback queue full");
}
/* R80_FIX10_TELNET_PORTMAP_END */


typedef struct {
    uint16_t len;
    uint8_t index;
    uint32_t epoch; /* never replay an old Wi-Fi epoch after reconnection */
    uint8_t frame[R71_ETH_MTU_FRAME];
} r71_ingress_t;
static r71_ingress_t *s_ingress;
static atomic_uint s_ingress_busy; /* low 8 bits, allocated in internal memory */

static err_t r71_linkoutput(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    if (!p || p->tot_len < 14 || p->tot_len > R71_ETH_MTU_FRAME ||
        !atomic_load_explicit(&s_active, memory_order_acquire)) {
        atomic_fetch_add(&s_drop_rx, 1); return ERR_IF;
    }
    /* A contended egress ring is an Ethernet loss, NOT an opportunity to
     * wait on the TCPIP/guest CPU thread or to allocate extra memory. */
    if (atomic_flag_test_and_set_explicit(&s_egress_guard, memory_order_acquire)) {
        atomic_fetch_add_explicit(&s_drop_rx, 1u, memory_order_relaxed);
        return ERR_MEM;
    }
    const unsigned epoch = atomic_load_explicit(&s_link_epoch, memory_order_acquire);
    unsigned head = atomic_load_explicit(&s_head, memory_order_relaxed);
    unsigned tail = atomic_load_explicit(&s_tail, memory_order_relaxed);
    /* Reclaim stale frames on the PRODUCER side before checking for space.
     * The lock prevents a concurrent SIMH consumer from reading a slot while
     * it is reused.  Never remove a current-epoch frame here. */
    while (tail != head && s_egress[tail % R71_FRAMES].epoch != epoch)
        ++tail;
    atomic_store_explicit(&s_tail, tail, memory_order_release);
    if (head - tail >= R71_FRAMES) {
        atomic_flag_clear_explicit(&s_egress_guard, memory_order_release);
        atomic_fetch_add_explicit(&s_drop_rx, 1u, memory_order_relaxed);
        return ERR_MEM;
    }
    r71_frame_t *slot = &s_egress[head % R71_FRAMES];
    if (pbuf_copy_partial(p, slot->bytes, p->tot_len, 0) != p->tot_len) {
        atomic_flag_clear_explicit(&s_egress_guard, memory_order_release);
        atomic_fetch_add_explicit(&s_drop_rx, 1u, memory_order_relaxed);
        return ERR_IF;
    }
    /* Ethernet hardware pads short ARP/ICMP frames to at least 60 bytes. */
    const unsigned frame_len = p->tot_len < 60 ? 60u : (unsigned)p->tot_len;
    if (frame_len > p->tot_len) memset(slot->bytes + p->tot_len, 0, frame_len - p->tot_len);
    slot->len = (uint16_t)frame_len;
    slot->epoch = epoch;
    /* Link can drop while copying a chained pbuf. Do not publish stale RX. */
    if (!atomic_load_explicit(&s_active, memory_order_acquire) ||
        epoch != atomic_load_explicit(&s_link_epoch, memory_order_acquire)) {
        atomic_flag_clear_explicit(&s_egress_guard, memory_order_release);
        atomic_fetch_add_explicit(&s_drop_rx, 1u, memory_order_relaxed);
        return ERR_IF;
    }
    atomic_store_explicit(&s_head, head + 1u, memory_order_release);
    atomic_flag_clear_explicit(&s_egress_guard, memory_order_release);
    return ERR_OK;
}

static err_t r71_netif_init(struct netif *n)
{
    n->name[0] = 'q'; n->name[1] = 'e';
    n->mtu = 1500;
    n->hwaddr_len = 6;
    const uint8_t hw[6] = { 0x02, 0x70, 0x00, 0x00, 0x00, 0x01 };
    memcpy(n->hwaddr, hw, sizeof hw);
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    n->output = etharp_output;
    n->linkoutput = r71_linkoutput;
    return ERR_OK;
}

static void r71_tcpip_init(void *arg)
{
    bool *success = (bool *)arg;
    ip4_addr_t addr, mask, gw;
    IP4_ADDR(&addr, 10,88,0,1);
    IP4_ADDR(&mask, 255,255,255,0);
    IP4_ADDR(&gw, 0,0,0,0);
    /* Ingress is delivered by r71_tcpip_receive on the TCPIP core.
     * Use the direct Ethernet demultiplexer: tcpip_input would enqueue a
     * SECOND message from inside the TCPIP core, possibly deadlocking if its
     * mailbox fills. */
    if (!netif_add(&s_guest, &addr, &mask, &gw, NULL, r71_netif_init, ethernet_input))
        return;
    netif_set_up(&s_guest);
    netif_set_link_up(&s_guest);
    /* Enable NAPT on the INSIDE (guest) netif, never the Wi-Fi STA. */
    /* Espressif lwIP returns 1 for NAPT success, 0 for failure.
     * ERR_OK is 0, so comparing with ERR_OK wrongly rejected success. */
    if (ip_napt_enable_netif(&s_guest, 1) != 1) {
        netif_set_down(&s_guest);
        netif_remove(&s_guest);
        return;
    }
    *success = true;
}

bool r71_guestnet_start(void)
{
    if (atomic_load_explicit(&s_started, memory_order_acquire)) {
        /* Publish the new association epoch BEFORE re-enabling RX.  Publishing
         * active first lets the guest briefly consume an old-epoch frame. */
        if (!atomic_load_explicit(&s_active, memory_order_acquire)) {
            atomic_fetch_add_explicit(&s_link_epoch, 1u, memory_order_acq_rel);
            atomic_store_explicit(&s_active, true, memory_order_release);
        }
        r80_telnet_after_join();
        return true;
    }
    if (!s_egress) {
        s_egress = (r71_frame_t *)heap_caps_calloc(R71_FRAMES,
                     sizeof(*s_egress), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_egress) return false; // No fallback into scarce internal DMA RAM.
    }
    if (!s_ingress) {
        s_ingress = (r71_ingress_t *)heap_caps_calloc(R71_PENDING_LIMIT,
                     sizeof(*s_ingress), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_ingress) return false;
        for (unsigned i = 0; i < R71_PENDING_LIMIT; ++i)
            s_ingress[i].index = (uint8_t)i;
    }
    bool configured = false;
    /* callback_with_block only waits for mailbox posting, NOT completion.
     * Its caller-owned stack argument could otherwise be used after return.
     * tcpip_callback_wait is synchronous in IDF 5.5.4's pinned esp-lwip. */
    if (tcpip_callback_wait(r71_tcpip_init, &configured) != ERR_OK ||
        !configured) return false;
    atomic_store(&s_started, true);
    atomic_fetch_add_explicit(&s_link_epoch, 1u, memory_order_release);
    atomic_store(&s_active, true);
    r80_telnet_after_join();
    return true;
}

static void r71_tcpip_receive(void *arg)
{
    r71_ingress_t *in = (r71_ingress_t *)arg;
    if (atomic_load_explicit(&s_active, memory_order_acquire) &&
        in->epoch == atomic_load_explicit(&s_link_epoch, memory_order_acquire)) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, in->len, PBUF_RAM);
        if (p) {
            if (pbuf_take(p, in->frame, in->len) == ERR_OK) {
                if (s_guest.input(p, &s_guest) != ERR_OK) pbuf_free(p);
            } else pbuf_free(p);
        } else atomic_fetch_add(&s_drop_tx, 1);
    } else {
        /* Bounded packet loss when the uplink changes; the guest's XQ device
         * still completed the DMA transfer and must not see a hardware fault. */
        atomic_fetch_add_explicit(&s_drop_tx, 1u, memory_order_relaxed);
    }
    /* Releasing the DRAM bit also publishes the PSRAM slot for reuse.
     * Callback ownership ends here, even for a stale/drop packet. */
    atomic_fetch_and_explicit(&s_ingress_busy, ~(1u << in->index), memory_order_release);
}

bool r71_guestnet_write(const uint8_t *data, size_t len)
{
    if (!data || len < 14 || len > R71_ETH_MTU_FRAME) return false;
    /* DELQA's link remains logically present even if the Wi-Fi uplink is
     * still lazy or lost. Hardware accepted the guest's Ethernet frame;
     * losing the route is not an XQ transmit DMA failure. Drop with a count
     * instead of reporting a false NIC hardware error to 4.3BSD. */
    if (!atomic_load_explicit(&s_active, memory_order_acquire)) {
        atomic_fetch_add_explicit(&s_drop_tx, 1u, memory_order_relaxed);
        return true;
    }
    /* Reserve one of eight persistent PSRAM packet slots. The only guest-side
     * operations are bounded CAS, memcpy and lwIP's nonblocking try-post.
     * No malloc, waiting for mailbox space, or interrupt/SDMMC work. */
    r71_ingress_t *in = NULL;
    unsigned bit = 0;
    for (unsigned attempts = 0; attempts < 2u * R71_PENDING_LIMIT; ++attempts) {
        unsigned mask = atomic_load_explicit(&s_ingress_busy, memory_order_acquire);
        if ((mask & ((1u << R71_PENDING_LIMIT) - 1u)) ==
            ((1u << R71_PENDING_LIMIT) - 1u)) break;
        unsigned i;
        for (i = 0; i < R71_PENDING_LIMIT; ++i)
            if (!(mask & (1u << i))) break;
        if (i == R71_PENDING_LIMIT) break;
        bit = (1u << i);
        if (atomic_compare_exchange_weak_explicit(&s_ingress_busy, &mask,
                                                  mask | bit, memory_order_acq_rel,
                                                  memory_order_acquire)) {
            in = &s_ingress[i];
            break;
        }
    }
    if (!in) {
        atomic_fetch_add_explicit(&s_drop_tx, 1u, memory_order_relaxed);
        return true; /* queue-full is not DELQA DMA failure */
    }
    in->len = (uint16_t)len;
    in->epoch = atomic_load_explicit(&s_link_epoch, memory_order_acquire);
    memcpy(in->frame, data, len);
    if (tcpip_try_callback(r71_tcpip_receive, in) != ERR_OK) {
        atomic_fetch_and_explicit(&s_ingress_busy, ~bit, memory_order_release);
        atomic_fetch_add_explicit(&s_drop_tx, 1u, memory_order_relaxed);
        return true; /* host mailbox full; virtual NIC DMA still succeeded */
    }
    return true;
}

size_t r71_guestnet_read(uint8_t *dst, size_t cap)
{
    /* No guest CPU spin or blocking. The lwIP producer and SIMH consumer
     * synchronize slot reuse with a single try-lock in internal DRAM. */
    if (!dst || !s_egress || !atomic_load_explicit(&s_active, memory_order_acquire)) return 0;
    if (atomic_flag_test_and_set_explicit(&s_egress_guard, memory_order_acquire)) return 0;
    const unsigned epoch = atomic_load_explicit(&s_link_epoch, memory_order_acquire);
    unsigned tail = atomic_load_explicit(&s_tail, memory_order_relaxed);
    const unsigned head = atomic_load_explicit(&s_head, memory_order_acquire);
    /* Any old frames are discarded; first NEW-epoch ARP reply is retained. */
    while (tail != head && s_egress[tail % R71_FRAMES].epoch != epoch) {
        ++tail;
        atomic_fetch_add_explicit(&s_drop_rx, 1u, memory_order_relaxed);
    }
    size_t len = 0;
    if (tail != head) {
        const r71_frame_t *item = &s_egress[tail % R71_FRAMES];
        if (item->len >= 14 && item->len <= cap) {
            len = item->len;
            memcpy(dst, item->bytes, len);
        } else {
            atomic_fetch_add_explicit(&s_drop_rx, 1u, memory_order_relaxed);
        }
        ++tail;
    }
    atomic_store_explicit(&s_tail, tail, memory_order_release);
    atomic_flag_clear_explicit(&s_egress_guard, memory_order_release);
    /* If Wi-Fi changes epoch while copying, never deliver an old frame. */
    if (!atomic_load_explicit(&s_active, memory_order_acquire) ||
        epoch != atomic_load_explicit(&s_link_epoch, memory_order_acquire)) return 0;
    return len;
}

void r71_guestnet_link(bool up) {
    atomic_store_explicit(&s_active, up && atomic_load(&s_started), memory_order_release);
    if (!up && atomic_load_explicit(&s_started, memory_order_acquire))
        r80_telnet_after_disconnect();
}
uint32_t r71_guestnet_rx_drops(void) { return atomic_load(&s_drop_rx); }
uint32_t r71_guestnet_tx_drops(void) { return atomic_load(&s_drop_tx); }
