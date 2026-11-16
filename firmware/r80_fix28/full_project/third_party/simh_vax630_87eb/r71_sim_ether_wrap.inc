/* simhP4 guest P4NAT endpoint, compiled at the END of the staged sim_ether.c.
 * Original SIMH Ethernet functions have been renamed by the staged prefix.
 * Guest frame ownership stays on the SIMH worker, not ESP event callbacks.
 */
#include "r71_guestnet.h"
#include <stdlib.h>
#include <string.h>
static const char r71_name[] = "P4NAT";
static int r71_is_p4nat(const ETH_DEV *d)
{
    return d && d->handle == (void *)r71_name;
}
t_stat eth_open(ETH_DEV *d, const char *name, DEVICE *ptr, uint32 dbg)
{
    if (!d || !name || strcmp(name, r71_name) != 0)
        return simhp4_r71_original_eth_open(d, name, ptr, dbg);
    memset(d, 0, sizeof(*d));
    d->name = (char *)r71_name; /* literal: never pass to free() */
    d->handle = (void *)r71_name;
    d->eth_api = ETH_API_NAT;
    d->dptr = ptr;
    d->dbit = dbg;
    return SCPE_OK;
}
t_stat eth_close(ETH_DEV *d)
{
    if (!r71_is_p4nat(d)) return simhp4_r71_original_eth_close(d);
    memset(d, 0, sizeof(*d));
    return SCPE_OK;
}
t_stat eth_write(ETH_DEV *d, ETH_PACK *p, ETH_PCALLBACK cb)
{
    if (!r71_is_p4nat(d)) return simhp4_r71_original_eth_write(d,p,cb);
    int good = p && r71_guestnet_write(p->msg, p->len);
    if (d) { if (good) d->packets_sent++; else d->transmit_packet_errors++; }
    if (cb) cb(good ? 0 : 1);
    return good ? SCPE_OK : SCPE_IOERR;
}
int eth_read(ETH_DEV *d, ETH_PACK *p, ETH_PCALLBACK cb)
{
    if (!r71_is_p4nat(d)) return simhp4_r71_original_eth_read(d,p,cb);
    if (!p) return 0;
    const size_t len = r71_guestnet_read(p->msg, ETH_MAX_PACKET);
    if (!len) return 0;
    p->len = (uint32)len;
    p->crc_len = (uint32)len;
    p->used = 0;
    p->status = 0;
    d->packets_received++;
    if (cb) cb(0);
    return 1;
}
t_stat eth_filter(ETH_DEV *d, int count, ETH_MAC *const mac,
                  ETH_BOOL multicast, ETH_BOOL promiscuous)
{
    if (!r71_is_p4nat(d)) return simhp4_r71_original_eth_filter(d,count,mac,multicast,promiscuous);
    if (count < 0 || count > ETH_FILTER_MAX || (count && !mac)) return SCPE_ARG;
    d->addr_count = count;
    if (count) memcpy(d->filter_address,mac,(size_t)count*sizeof(ETH_MAC));
    d->all_multicast = multicast; d->promiscuous = promiscuous;
    return SCPE_OK;
}
t_stat eth_filter_hash(ETH_DEV *d, int count, ETH_MAC *const mac,
                       ETH_BOOL multicast, ETH_BOOL promiscuous, ETH_MULTIHASH *const hash)
{
    if (!r71_is_p4nat(d))
        return simhp4_r71_original_eth_filter_hash(d,count,mac,multicast,promiscuous,hash);
    t_stat s = eth_filter(d,count,mac,multicast,promiscuous);
    if (s == SCPE_OK) { d->hash_filter = (hash != NULL); if(hash) memcpy(d->hash,*hash,sizeof(*hash)); }
    return s;
}
t_stat eth_filter_hash_ex(ETH_DEV *d,int count,ETH_MAC *const mac,
                          ETH_BOOL multicast,ETH_BOOL promiscuous,
                          ETH_BOOL match_broadcast,ETH_MULTIHASH *const hash)
{
    if (!r71_is_p4nat(d))
        return simhp4_r71_original_eth_filter_hash_ex(d,count,mac,multicast,promiscuous,match_broadcast,hash);
    (void)match_broadcast;
    return eth_filter_hash(d,count,mac,multicast,promiscuous,hash);
}
t_stat eth_check_address_conflict(ETH_DEV *d, ETH_MAC *const addr)
{
    /* Original staged SIMH sim_ether.c (line 967) takes const ETH_MAC mac,
     * which decays to const unsigned char *, NOT ETH_MAC *.
     * The public API takes ETH_MAC *const, so pass one MAC array (*addr),
     * retaining the original NULL behavior for non-P4NAT interfaces. */
    if (!r71_is_p4nat(d)) return simhp4_r71_original_eth_check_address_conflict(d, addr ? *addr : NULL);
    return SCPE_OK; /* virtual host; no physical MAC ownership to probe */
}
t_stat eth_set_async(ETH_DEV *d,int latency)
{
    if (!r71_is_p4nat(d)) return simhp4_r71_original_eth_set_async(d,latency);
    (void)latency;
    /* This virtual device provides polling, not an asynchronous reader.
     * Never falsely report callback-based receive support: an XQ configured
     * with POLL=DISABLED would otherwise stop receiving permanently. */
    return SCPE_NOFNC;
}
t_stat eth_clr_async(ETH_DEV *d)
{
    if (!r71_is_p4nat(d)) return simhp4_r71_original_eth_clr_async(d);
    /* P4NAT has no asynchronous RX task. xq_attach() determines whether
     * polling is mandatory from (SCPE_OK != eth_clr_async(...)). Returning
     * SCPE_OK wrongly sets must_poll=0, so DELQA-Plus mode takes the async
     * arm of xq_start_receiver() and never schedules the polling service.
     * SCPE_NOFNC is the supported SIMH signal to keep XQ receive polling.
     * Device reset/stop callers ignore this status, as in upstream SIMH.
     */
    return SCPE_NOFNC;
}
