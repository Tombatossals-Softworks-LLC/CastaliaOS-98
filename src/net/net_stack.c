/*
 * net_stack.c - ARP / IPv4 / ICMP / UDP frame building and parsing.
 *
 * See net_stack.h. Pure byte work: no hardware, no allocation, no host byte
 * order assumptions -- every multi-byte field is written and read one octet at
 * a time, which is also what makes this safe on the DOS build.
 */
#include "castalia/net_stack.h"
#include "castalia/sys.h"

static const cu8 NS_BCAST[NET_MAC_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const cu8 NS_ZERO[NET_MAC_LEN]  = { 0, 0, 0, 0, 0, 0 };

/* ---- byte order -------------------------------------------------------- */
void ns_put16(cu8 *p, cu16 v)
{
    p[0] = (cu8)((v >> 8) & 0xFF);
    p[1] = (cu8)(v & 0xFF);
}

void ns_put32(cu8 *p, cu32 v)
{
    p[0] = (cu8)((v >> 24) & 0xFF);
    p[1] = (cu8)((v >> 16) & 0xFF);
    p[2] = (cu8)((v >> 8) & 0xFF);
    p[3] = (cu8)(v & 0xFF);
}

cu16 ns_get16(const cu8 *p)
{
    return (cu16)(((cu16)p[0] << 8) | (cu16)p[1]);
}

cu32 ns_get32(const cu8 *p)
{
    return ((cu32)p[0] << 24) | ((cu32)p[1] << 16) |
           ((cu32)p[2] << 8)  | (cu32)p[3];
}

static void ns_copy_mac(cu8 *dst, const cu8 *src)
{
    int i;
    for (i = 0; i < NET_MAC_LEN; i++) { dst[i] = src[i]; }
}

static cbool ns_mac_equal(const cu8 *a, const cu8 *b)
{
    int i;
    for (i = 0; i < NET_MAC_LEN; i++) {
        if (a[i] != b[i]) { return CFALSE; }
    }
    return CTRUE;
}

/* ---- configuration and the ARP cache ---------------------------------- */
void ns_init(NetStack *ns, const cu8 mac[NET_MAC_LEN], cu32 ip, cu32 mask,
             cu32 gateway)
{
    int i;
    if (ns == NULL) { return; }
    for (i = 0; i < NET_MAC_LEN; i++) {
        ns->mac[i] = (mac != NULL) ? mac[i] : 0;
    }
    ns->ip = ip;
    ns->mask = mask;
    ns->gateway = gateway;
    for (i = 0; i < NS_ARP_ENTRIES; i++) {
        ns->arp[i].ip = 0;
        ns->arp[i].age = 0;
        ns_copy_mac(ns->arp[i].mac, NS_ZERO);
    }
    ns->clock = 0;
    ns->ident = 1;
    ns->ping_id = 0x4341;              /* 'CA' -- ours, and recognizable   */
    ns->ping_seq = 0;
}

void ns_arp_set(NetStack *ns, cu32 ip, const cu8 mac[NET_MAC_LEN])
{
    int i, oldest = 0;
    if (ns == NULL || mac == NULL || ip == 0) { return; }
    if (ns_mac_equal(mac, NS_ZERO) || ns_mac_equal(mac, NS_BCAST)) { return; }
    ns->clock++;
    for (i = 0; i < NS_ARP_ENTRIES; i++) {
        if (ns->arp[i].age != 0 && ns->arp[i].ip == ip) {
            ns_copy_mac(ns->arp[i].mac, mac);
            ns->arp[i].age = ns->clock;
            return;
        }
    }
    for (i = 0; i < NS_ARP_ENTRIES; i++) {
        if (ns->arp[i].age == 0) { oldest = i; break; }
        if (ns->arp[i].age < ns->arp[oldest].age) { oldest = i; }
    }
    ns->arp[oldest].ip = ip;
    ns_copy_mac(ns->arp[oldest].mac, mac);
    ns->arp[oldest].age = ns->clock;
}

cbool ns_arp_get(NetStack *ns, cu32 ip, cu8 out[NET_MAC_LEN])
{
    int i;
    if (ns == NULL || ip == 0) { return CFALSE; }
    for (i = 0; i < NS_ARP_ENTRIES; i++) {
        if (ns->arp[i].age != 0 && ns->arp[i].ip == ip) {
            ns->clock++;
            ns->arp[i].age = ns->clock;      /* least-recently-used aging  */
            if (out != NULL) { ns_copy_mac(out, ns->arp[i].mac); }
            return CTRUE;
        }
    }
    return CFALSE;
}

int ns_arp_count(const NetStack *ns)
{
    int i, n = 0;
    if (ns == NULL) { return 0; }
    for (i = 0; i < NS_ARP_ENTRIES; i++) {
        if (ns->arp[i].age != 0) { n++; }
    }
    return n;
}

cbool ns_arp_entry(const NetStack *ns, int index, cu32 *ip,
                   cu8 out[NET_MAC_LEN])
{
    int i, n = 0;
    if (ns == NULL || index < 0) { return CFALSE; }
    for (i = 0; i < NS_ARP_ENTRIES; i++) {
        if (ns->arp[i].age == 0) { continue; }
        if (n == index) {
            if (ip != NULL) { *ip = ns->arp[i].ip; }
            if (out != NULL) { ns_copy_mac(out, ns->arp[i].mac); }
            return CTRUE;
        }
        n++;
    }
    return CFALSE;
}

cu32 ns_next_hop(const NetStack *ns, cu32 dst)
{
    if (ns == NULL) { return dst; }
    if (ns->mask != 0 && ((dst & ns->mask) == (ns->ip & ns->mask))) {
        return dst;                   /* on our wire: talk to it directly */
    }
    return (ns->gateway != 0) ? ns->gateway : dst;
}

/* ---- builders ---------------------------------------------------------- */
static cu32 ns_eth_header(const NetStack *ns, cu8 *out, const cu8 *dst,
                          cu16 type)
{
    ns_copy_mac(out, dst);
    ns_copy_mac(out + 6, ns->mac);
    ns_put16(out + 12, type);
    return NS_ETH_HDR;
}

static cu32 ns_arp_frame(const NetStack *ns, cu16 op, cu32 target_ip,
                         const cu8 *target_mac, const cu8 *eth_dst,
                         cu8 *out, cu32 cap)
{
    cu8 *a;
    if (ns == NULL || out == NULL || cap < NS_ARP_FRAME) { return 0; }
    ns_eth_header(ns, out, eth_dst, NS_TYPE_ARP);
    a = out + NS_ETH_HDR;
    ns_put16(a + 0, 1);                 /* hardware type: Ethernet         */
    ns_put16(a + 2, NS_TYPE_IP);        /* protocol type: IPv4             */
    a[4] = NET_MAC_LEN;
    a[5] = 4;
    ns_put16(a + 6, op);
    ns_copy_mac(a + 8, ns->mac);
    ns_put32(a + 14, ns->ip);
    ns_copy_mac(a + 18, target_mac);
    ns_put32(a + 24, target_ip);
    return NS_ARP_FRAME;
}

cu32 ns_build_arp_request(const NetStack *ns, cu32 target, cu8 *out, cu32 cap)
{
    if (target == 0) { return 0; }
    return ns_arp_frame(ns, 1, target, NS_ZERO, NS_BCAST, out, cap);
}

cu32 ns_build_arp_reply(const NetStack *ns, cu32 to_ip,
                        const cu8 to_mac[NET_MAC_LEN], cu8 *out, cu32 cap)
{
    if (to_ip == 0 || to_mac == NULL) { return 0; }
    return ns_arp_frame(ns, 2, to_ip, to_mac, to_mac, out, cap);
}

/* Fill in an IPv4 header and return where the payload starts. */
static cu8 *ns_ip_header(NetStack *ns, cu8 *out, const cu8 *dst_mac,
                         cu32 dst_ip, cu8 proto, cu32 payload_len)
{
    cu8 *ip = out + NS_ETH_HDR;
    cu16 sum;
    ns_eth_header(ns, out, dst_mac, NS_TYPE_IP);
    ip[0] = 0x45;                                  /* v4, 5 words of header */
    ip[1] = 0;                                     /* no DSCP/ECN           */
    ns_put16(ip + 2, (cu16)(NS_IP_HDR + payload_len));
    ns_put16(ip + 4, ns->ident);
    ns->ident++;
    ns_put16(ip + 6, 0x4000);                      /* don't fragment        */
    ip[8] = 64;                                    /* TTL                   */
    ip[9] = proto;
    ns_put16(ip + 10, 0);                          /* checksum: computed... */
    ns_put32(ip + 12, ns->ip);
    ns_put32(ip + 16, dst_ip);
    sum = net_checksum(ip, NS_IP_HDR);             /* ...over the header    */
    ns_put16(ip + 10, sum);
    return ip + NS_IP_HDR;
}

cu32 ns_build_ping(NetStack *ns, cu32 dst, const cu8 dst_mac[NET_MAC_LEN],
                   int payload_len, cu16 *out_seq, cu8 *out, cu32 cap)
{
    cu8 *icmp;
    cu32 total;
    int i;
    if (ns == NULL || out == NULL || dst_mac == NULL || dst == 0) { return 0; }
    if (payload_len < 0) { payload_len = 0; }
    if (payload_len > 64) { payload_len = 64; }
    total = NS_ETH_HDR + NS_IP_HDR + NS_ICMP_HDR + (cu32)payload_len;
    if (cap < total) { return 0; }

    icmp = ns_ip_header(ns, out, dst_mac, dst, NS_PROTO_ICMP,
                        (cu32)(NS_ICMP_HDR + payload_len));
    ns->ping_seq++;
    icmp[0] = 8;                                   /* echo request          */
    icmp[1] = 0;
    ns_put16(icmp + 2, 0);
    ns_put16(icmp + 4, ns->ping_id);
    ns_put16(icmp + 6, ns->ping_seq);
    /* A recognizable, repeatable payload -- the reply must echo it back. */
    for (i = 0; i < payload_len; i++) {
        icmp[NS_ICMP_HDR + i] = (cu8)('a' + (i % 26));
    }
    ns_put16(icmp + 2, net_checksum(icmp, (cu32)(NS_ICMP_HDR + payload_len)));
    if (out_seq != NULL) { *out_seq = ns->ping_seq; }
    return total;
}

cu32 ns_build_ping_reply(NetStack *ns, const NsRx *rx, cu8 *out, cu32 cap)
{
    cu8 *icmp;
    cu32 total, i;
    if (ns == NULL || rx == NULL || out == NULL) { return 0; }
    if (rx->kind != NS_RX_PING_REQUEST || rx->src_ip == 0) { return 0; }
    total = NS_ETH_HDR + NS_IP_HDR + NS_ICMP_HDR + rx->payload_len;
    if (cap < total || total > NET_MTU) { return 0; }

    icmp = ns_ip_header(ns, out, rx->src_mac, rx->src_ip, NS_PROTO_ICMP,
                        NS_ICMP_HDR + rx->payload_len);
    icmp[0] = 0;                                   /* echo reply            */
    icmp[1] = 0;
    ns_put16(icmp + 2, 0);
    ns_put16(icmp + 4, rx->id);                    /* echoed verbatim: that */
    ns_put16(icmp + 6, rx->seq);                   /* is what they match on */
    for (i = 0; i < rx->payload_len; i++) {
        icmp[NS_ICMP_HDR + i] = rx->payload[i];
    }
    ns_put16(icmp + 2, net_checksum(icmp, NS_ICMP_HDR + rx->payload_len));
    return total;
}

cu32 ns_build_udp(NetStack *ns, cu32 dst, const cu8 dst_mac[NET_MAC_LEN],
                  cu16 sport, cu16 dport, const void *data, cu32 len,
                  cu8 *out, cu32 cap)
{
    const cu8 *src = (const cu8 *)data;
    cu8 *udp;
    cu32 total, i;
    if (ns == NULL || out == NULL || dst_mac == NULL || dst == 0) { return 0; }
    if (len > 0 && src == NULL) { return 0; }
    total = NS_ETH_HDR + NS_IP_HDR + NS_UDP_HDR + len;
    if (cap < total || total > NET_MTU) { return 0; }

    udp = ns_ip_header(ns, out, dst_mac, dst, NS_PROTO_UDP, NS_UDP_HDR + len);
    ns_put16(udp + 0, sport);
    ns_put16(udp + 2, dport);
    ns_put16(udp + 4, (cu16)(NS_UDP_HDR + len));
    ns_put16(udp + 6, 0);      /* checksum optional over IPv4; left as zero */
    for (i = 0; i < len; i++) { udp[NS_UDP_HDR + i] = src[i]; }
    return total;
}

/* ---- parsing ----------------------------------------------------------- */
static NsRxKind ns_rx_arp(NetStack *ns, const cu8 *frame, cu32 len, NsRx *out)
{
    const cu8 *a = frame + NS_ETH_HDR;
    cu16 op;
    cu32 sender_ip, target_ip;
    if (len < NS_ARP_FRAME) { return NS_RX_MALFORMED; }
    if (ns_get16(a + 0) != 1 || ns_get16(a + 2) != NS_TYPE_IP ||
        a[4] != NET_MAC_LEN || a[5] != 4) {
        return NS_RX_IGNORED;
    }
    op = ns_get16(a + 6);
    sender_ip = ns_get32(a + 14);
    target_ip = ns_get32(a + 24);
    ns_arp_set(ns, sender_ip, a + 8);          /* free address information */
    if (out != NULL) {
        out->src_ip = sender_ip;
        out->dst_ip = target_ip;
        ns_copy_mac(out->src_mac, a + 8);
    }
    if (target_ip != ns->ip) { return NS_RX_IGNORED; }
    return (op == 1) ? NS_RX_ARP_REQUEST
         : (op == 2) ? NS_RX_ARP_REPLY : NS_RX_IGNORED;
}

static NsRxKind ns_rx_ip(NetStack *ns, const cu8 *frame, cu32 len, NsRx *out)
{
    const cu8 *ip = frame + NS_ETH_HDR;
    cu32 ihl, total, payload_len;
    const cu8 *pl;

    if (len < NS_ETH_HDR + NS_IP_HDR) { return NS_RX_MALFORMED; }
    if ((ip[0] >> 4) != 4) { return NS_RX_IGNORED; }
    ihl = (cu32)(ip[0] & 0x0F) * 4;
    if (ihl < NS_IP_HDR || len < NS_ETH_HDR + ihl) { return NS_RX_MALFORMED; }
    if (net_checksum(ip, ihl) != 0) { return NS_RX_MALFORMED; }
    total = ns_get16(ip + 2);
    if (total < ihl || len < NS_ETH_HDR + total) { return NS_RX_MALFORMED; }
    payload_len = total - ihl;
    pl = ip + ihl;

    if (out != NULL) {
        out->src_ip = ns_get32(ip + 12);
        out->dst_ip = ns_get32(ip + 16);
        out->ttl = ip[8];
        ns_copy_mac(out->src_mac, frame + 6);
        out->payload = pl;
        out->payload_len = payload_len;
    }
    /* The sender's pairing is worth caching whatever the protocol is. */
    ns_arp_set(ns, ns_get32(ip + 12), frame + 6);
    if (ns_get32(ip + 16) != ns->ip) { return NS_RX_IGNORED; }

    if (ip[9] == NS_PROTO_ICMP) {
        if (payload_len < NS_ICMP_HDR) { return NS_RX_MALFORMED; }
        if (net_checksum(pl, payload_len) != 0) { return NS_RX_MALFORMED; }
        if (out != NULL) {
            out->id = ns_get16(pl + 4);
            out->seq = ns_get16(pl + 6);
            out->payload = pl + NS_ICMP_HDR;
            out->payload_len = payload_len - NS_ICMP_HDR;
        }
        if (pl[0] == 8) { return NS_RX_PING_REQUEST; }
        if (pl[0] == 0) { return NS_RX_PING_REPLY; }
        return NS_RX_IGNORED;
    }
    if (ip[9] == NS_PROTO_UDP) {
        cu32 ulen;
        if (payload_len < NS_UDP_HDR) { return NS_RX_MALFORMED; }
        ulen = ns_get16(pl + 4);
        if (ulen < NS_UDP_HDR || ulen > payload_len) { return NS_RX_MALFORMED; }
        if (out != NULL) {
            out->sport = ns_get16(pl + 0);
            out->dport = ns_get16(pl + 2);
            out->payload = pl + NS_UDP_HDR;
            out->payload_len = ulen - NS_UDP_HDR;
        }
        return NS_RX_UDP;
    }
    return NS_RX_IGNORED;
}

NsRxKind ns_receive(NetStack *ns, const cu8 *frame, cu32 len, NsRx *out)
{
    cu16 type;
    NsRxKind kind;
    if (out != NULL) {
        out->kind = NS_RX_IGNORED;
        out->src_ip = 0; out->dst_ip = 0;
        out->id = 0; out->seq = 0;
        out->sport = 0; out->dport = 0;
        out->payload = NULL; out->payload_len = 0;
        out->ttl = 0;
        ns_copy_mac(out->src_mac, NS_ZERO);
    }
    if (ns == NULL || frame == NULL) { return NS_RX_IGNORED; }
    if (len < NS_ETH_HDR) { return NS_RX_MALFORMED; }

    /* Frames for another station are not ours to look at (the packet driver
     * may hand us the whole wire in promiscuous mode). */
    if (!ns_mac_equal(frame, ns->mac) && !ns_mac_equal(frame, NS_BCAST)) {
        return NS_RX_IGNORED;
    }
    type = ns_get16(frame + 12);
    kind = (type == NS_TYPE_ARP) ? ns_rx_arp(ns, frame, len, out)
         : (type == NS_TYPE_IP)  ? ns_rx_ip(ns, frame, len, out)
         : NS_RX_IGNORED;
    if (out != NULL) { out->kind = kind; }
    return kind;
}
