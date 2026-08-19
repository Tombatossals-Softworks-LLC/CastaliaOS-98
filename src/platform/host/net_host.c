/*
 * net_host.c - Networking backend for the host build.
 *
 * By default there is no NIC in CI and none is wanted: the backend reports
 * "no device", net_available() is CFALSE, and the whole system builds and runs
 * offline.
 *
 * Optionally (net_host_set_loopback / --net-loopback) it becomes a SIMULATED
 * wire: two make-believe stations answer ARP, ICMP echo and a UDP echo port,
 * entirely inside this file. It is labelled as simulated everywhere it is
 * reported, because pretending a machine has a network it does not have would
 * be a lie -- but it lets the real protocol code in net_stack.c be driven end
 * to end on a machine with no hardware, which is exactly what CI needs.
 *
 * The frames the simulated stations exchange are built and parsed by the same
 * net_stack.c the shell uses; nothing here special-cases the sender.
 */
#include "castalia/net.h"
#include "castalia/net_stack.h"
#include "castalia/sys.h"
#include "../../platform/host/plat_host.h"

#include <string.h>

#define NH_PEERS  2
#define NH_QUEUE  8            /* frames waiting to be polled                */

/* Our simulated station, and the two it can talk to. */
static const cu8 NH_OUR_MAC[NET_MAC_LEN] = { 0x02, 0xCA, 0x57, 0x00, 0x00, 0x01 };
static const cu8 NH_PEER_MAC[NH_PEERS][NET_MAC_LEN] = {
    { 0x02, 0xCA, 0x57, 0x00, 0x00, 0x02 },
    { 0x02, 0xCA, 0x57, 0x00, 0x00, 0x03 }
};
static const cu32 NH_PEER_IP[NH_PEERS] = { 0x0A000202UL,   /* 10.0.2.2 */
                                          0x0A000203UL }; /* 10.0.2.3 */

static cbool g_loopback = CFALSE;
static cbool g_up = CFALSE;
static NetStack g_peer[NH_PEERS];
static cu8  g_queue[NH_QUEUE][NET_MTU];
static cu32 g_qlen[NH_QUEUE];
static int  g_head, g_tail;
static long g_tx, g_rx;

void plat_host_set_loopback(cbool on) { g_loopback = on; }
long plat_host_net_tx(void) { return g_tx; }
long plat_host_net_rx(void) { return g_rx; }

static void nh_queue(const cu8 *frame, cu32 len)
{
    int next = (g_tail + 1) % NH_QUEUE;
    if (len == 0 || len > NET_MTU || next == g_head) { return; }
    memcpy(g_queue[g_tail], frame, len);
    g_qlen[g_tail] = len;
    g_tail = next;
}

CResult net_init(void)
{
    int i;
    g_head = g_tail = 0;
    g_tx = g_rx = 0;
    if (!g_loopback) {
        SYS_LOGI("net", "host null network backend (no device)");
        g_up = CFALSE;
        return CE_UNSUPPORTED;
    }
    for (i = 0; i < NH_PEERS; i++) {
        /* 255.255.255.0, no gateway: these two only ever talk on their wire. */
        ns_init(&g_peer[i], NH_PEER_MAC[i], NH_PEER_IP[i], 0xFFFFFF00UL, 0);
    }
    g_up = CTRUE;
    SYS_LOGI("net", "host loopback backend up (simulated peers 10.0.2.2, "
                    "10.0.2.3)");
    return CE_OK;
}

void net_shutdown(void)
{
    g_up = CFALSE;
    g_head = g_tail = 0;
}

cbool net_available(void) { return g_up; }

void net_device_info(NetDeviceInfo *out)
{
    if (out == NULL) { return; }
    memset(out, 0, sizeof(*out));
    if (!g_up) {
        out->available = CFALSE;
        sys_strlcpy(out->name, "None (no network)", sizeof(out->name));
        return;
    }
    out->available = CTRUE;
    memcpy(out->mac, NH_OUR_MAC, NET_MAC_LEN);
    out->int_no = 0;
    out->class_id = 1;                       /* DIX / Ethernet              */
    sys_strlcpy(out->name, "Loopback (simulated)", sizeof(out->name));
}

/* The simulated wire: hand the frame to each station and queue what they
 * would answer. Everything goes through net_stack.c, so a bug in the real
 * protocol code shows up here as a missing reply rather than being hidden. */
CResult net_send_frame(const void *frame, cu32 len)
{
    static cu8 reply[NET_MTU];
    const cu8 *f = (const cu8 *)frame;
    int i;
    if (!g_up) { return CE_UNSUPPORTED; }
    if (f == NULL || len < NS_ETH_HDR || len > NET_MTU) { return CE_INVALID; }
    g_tx++;

    for (i = 0; i < NH_PEERS; i++) {
        NsRx rx;
        cu32 n = 0;
        switch (ns_receive(&g_peer[i], f, len, &rx)) {
        case NS_RX_ARP_REQUEST:
            n = ns_build_arp_reply(&g_peer[i], rx.src_ip, rx.src_mac,
                                   reply, sizeof reply);
            break;
        case NS_RX_PING_REQUEST:
            n = ns_build_ping_reply(&g_peer[i], &rx, reply, sizeof reply);
            break;
        case NS_RX_UDP:
            /* Port 7 is the echo service, as it has been since 1983. */
            if (rx.dport == 7) {
                cu8 mac[NET_MAC_LEN];
                if (ns_arp_get(&g_peer[i], rx.src_ip, mac)) {
                    n = ns_build_udp(&g_peer[i], rx.src_ip, mac,
                                     rx.dport, rx.sport, rx.payload,
                                     rx.payload_len, reply, sizeof reply);
                }
            }
            break;
        default:
            break;
        }
        if (n > 0) { nh_queue(reply, n); }
    }
    return CE_OK;
}

int net_poll_frame(void *buf, cu32 cap)
{
    cu32 len;
    if (!g_up || buf == NULL || g_head == g_tail) { return 0; }
    len = g_qlen[g_head];
    if (len > cap) { g_head = (g_head + 1) % NH_QUEUE; return 0; }
    memcpy(buf, g_queue[g_head], len);
    g_head = (g_head + 1) % NH_QUEUE;
    g_rx++;
    return (int)len;
}
