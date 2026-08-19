/*
 * test_stack.c - The ARP / IPv4 / ICMP / UDP layer (net_stack.c).
 *
 * There is no NIC here and none is needed: the whole point of the layer is
 * that it turns bytes into bytes. These tests build frames, check the octets
 * that go on the wire (byte order, checksums, addressing), and feed them back
 * through the parser -- including deliberately corrupted ones, because a stack
 * that trusts what it is handed is the one that crashes on a hostile wire.
 */
#include "ctest.h"
#include "castalia/net_stack.h"

static NetStack g_ns;
static cu8 g_buf[NET_MTU];

static const cu8 US[NET_MAC_LEN]   = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const cu8 PEER[NET_MAC_LEN] = { 0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };

#define IP_US   0xC0A80105UL    /* 192.168.1.5   */
#define IP_PEER 0xC0A80101UL    /* 192.168.1.1   */
#define IP_FAR  0x08080808UL    /* 8.8.8.8       */
#define MASK    0xFFFFFF00UL
#define GW      IP_PEER

static void reset(void)
{
    ns_init(&g_ns, US, IP_US, MASK, GW);
}

static void st_byte_order(void)
{
    cu8 b[4];
    ns_put16(b, 0x1234);
    CHECK_EQI(b[0], 0x12);              /* big-endian on the wire */
    CHECK_EQI(b[1], 0x34);
    CHECK_EQI((int)ns_get16(b), 0x1234);
    ns_put32(b, 0xC0A80105UL);
    CHECK_EQI(b[0], 0xC0);
    CHECK_EQI(b[3], 0x05);
    CHECK(ns_get32(b) == 0xC0A80105UL);
}

static void st_arp_cache(void)
{
    cu8 mac[NET_MAC_LEN];
    cu32 ip = 0;
    int i;

    reset();
    CHECK_EQI(ns_arp_count(&g_ns), 0);
    CHECK(!ns_arp_get(&g_ns, IP_PEER, mac));

    ns_arp_set(&g_ns, IP_PEER, PEER);
    CHECK_EQI(ns_arp_count(&g_ns), 1);
    CHECK(ns_arp_get(&g_ns, IP_PEER, mac));
    CHECK_EQI(mac[1], 0xAA);

    /* Re-learning the same address updates it in place, not twice. */
    {
        cu8 other[NET_MAC_LEN] = { 0x02, 0x01, 0x02, 0x03, 0x04, 0x05 };
        ns_arp_set(&g_ns, IP_PEER, other);
        CHECK_EQI(ns_arp_count(&g_ns), 1);
        CHECK(ns_arp_get(&g_ns, IP_PEER, mac));
        CHECK_EQI(mac[1], 0x01);
    }

    /* Junk is refused rather than cached: it would poison every send. */
    {
        cu8 zero[NET_MAC_LEN] = { 0, 0, 0, 0, 0, 0 };
        cu8 bcast[NET_MAC_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        ns_arp_set(&g_ns, 0, PEER);
        ns_arp_set(&g_ns, IP_FAR, zero);
        ns_arp_set(&g_ns, IP_FAR, bcast);
        ns_arp_set(NULL, IP_FAR, PEER);
        CHECK_EQI(ns_arp_count(&g_ns), 1);
    }

    /* The cache fills and then evicts the least recently used entry. */
    reset();
    for (i = 0; i < NS_ARP_ENTRIES; i++) {
        cu8 m[NET_MAC_LEN] = { 0x02, 0, 0, 0, 0, 0 };
        m[5] = (cu8)i;
        ns_arp_set(&g_ns, 0x0A000001UL + (cu32)i, m);
    }
    CHECK_EQI(ns_arp_count(&g_ns), NS_ARP_ENTRIES);
    ns_arp_get(&g_ns, 0x0A000001UL, NULL);      /* touch the first one     */
    ns_arp_set(&g_ns, 0x0A0000FFUL, PEER);      /* one too many            */
    CHECK_EQI(ns_arp_count(&g_ns), NS_ARP_ENTRIES);
    CHECK(ns_arp_get(&g_ns, 0x0A000001UL, mac));    /* survived: just used  */
    CHECK(ns_arp_get(&g_ns, 0x0A0000FFUL, mac));    /* and the newcomer     */
    CHECK(!ns_arp_get(&g_ns, 0x0A000002UL, mac));   /* the oldest went      */

    /* The enumeration a UI walks. */
    CHECK(ns_arp_entry(&g_ns, 0, &ip, mac));
    CHECK(ip != 0);
    CHECK(!ns_arp_entry(&g_ns, NS_ARP_ENTRIES, &ip, mac));
    CHECK(!ns_arp_entry(&g_ns, -1, &ip, mac));
}

static void st_routing(void)
{
    reset();
    /* Same subnet: straight to the host. Elsewhere: to the gateway. */
    CHECK(ns_next_hop(&g_ns, 0xC0A801FEUL) == 0xC0A801FEUL);
    CHECK(ns_next_hop(&g_ns, IP_FAR) == GW);
    /* With no gateway configured we can only try the host itself. */
    ns_init(&g_ns, US, IP_US, MASK, 0);
    CHECK(ns_next_hop(&g_ns, IP_FAR) == IP_FAR);
}

static void st_arp_frames(void)
{
    cu32 n;
    NsRx rx;

    reset();
    n = ns_build_arp_request(&g_ns, IP_PEER, g_buf, sizeof g_buf);
    CHECK_EQI((int)n, NS_ARP_FRAME);
    CHECK_EQI(g_buf[0], 0xFF);                       /* broadcast           */
    CHECK_EQI(g_buf[6], US[0]);                      /* from us             */
    CHECK_EQI((int)ns_get16(g_buf + 12), NS_TYPE_ARP);
    CHECK_EQI((int)ns_get16(g_buf + 14 + 6), 1);     /* opcode: request     */
    CHECK(ns_get32(g_buf + 14 + 14) == IP_US);       /* sender protocol addr */
    CHECK(ns_get32(g_buf + 14 + 24) == IP_PEER);     /* target              */

    /* Guards: no target, no buffer, buffer too small. */
    CHECK_EQI((int)ns_build_arp_request(&g_ns, 0, g_buf, sizeof g_buf), 0);
    CHECK_EQI((int)ns_build_arp_request(&g_ns, IP_PEER, g_buf, 8), 0);
    CHECK_EQI((int)ns_build_arp_request(&g_ns, IP_PEER, NULL, 64), 0);

    /* A request FROM the peer asking for us: we must recognize it and be
     * able to answer, and we should have learned the peer's address. */
    {
        NetStack peer;
        ns_init(&peer, PEER, IP_PEER, MASK, 0);
        n = ns_build_arp_request(&peer, IP_US, g_buf, sizeof g_buf);
        CHECK_EQI((int)ns_receive(&g_ns, g_buf, n, &rx), NS_RX_ARP_REQUEST);
        CHECK(rx.src_ip == IP_PEER);
        CHECK(ns_arp_get(&g_ns, IP_PEER, NULL));

        n = ns_build_arp_reply(&g_ns, rx.src_ip, rx.src_mac, g_buf,
                               sizeof g_buf);
        CHECK_EQI((int)n, NS_ARP_FRAME);
        CHECK_EQI(g_buf[0], PEER[0]);                /* unicast to them     */
        CHECK_EQI((int)ns_get16(g_buf + 14 + 6), 2); /* opcode: reply       */
        /* ...and the peer accepts it as the answer it was waiting for. */
        CHECK_EQI((int)ns_receive(&peer, g_buf, n, &rx), NS_RX_ARP_REPLY);
        CHECK(ns_arp_get(&peer, IP_US, NULL));
    }

    /* An ARP for somebody else on the wire is not ours to answer. */
    {
        NetStack peer;
        ns_init(&peer, PEER, IP_PEER, MASK, 0);
        n = ns_build_arp_request(&peer, 0xC0A801FEUL, g_buf, sizeof g_buf);
        CHECK_EQI((int)ns_receive(&g_ns, g_buf, n, &rx), NS_RX_IGNORED);
    }
}

static void st_ping(void)
{
    cu32 n;
    cu16 seq = 0;
    NsRx rx;
    NetStack peer;

    reset();
    ns_init(&peer, PEER, IP_PEER, MASK, 0);

    n = ns_build_ping(&g_ns, IP_PEER, PEER, 32, &seq, g_buf, sizeof g_buf);
    CHECK_EQI((int)n, NS_ETH_HDR + NS_IP_HDR + NS_ICMP_HDR + 32);
    CHECK_EQI((int)seq, 1);
    CHECK_EQI((int)ns_get16(g_buf + 12), NS_TYPE_IP);
    CHECK_EQI(g_buf[NS_ETH_HDR + 9], NS_PROTO_ICMP);
    CHECK_EQI(g_buf[NS_ETH_HDR + NS_IP_HDR], 8);         /* echo request  */
    /* The header checksums must verify -- a real host would drop them if not. */
    CHECK_EQI((int)net_checksum(g_buf + NS_ETH_HDR, NS_IP_HDR), 0);
    CHECK_EQI((int)net_checksum(g_buf + NS_ETH_HDR + NS_IP_HDR,
                                NS_ICMP_HDR + 32), 0);

    /* The peer sees it as a request addressed to it, with our payload. */
    CHECK_EQI((int)ns_receive(&peer, g_buf, n, &rx), NS_RX_PING_REQUEST);
    CHECK(rx.src_ip == IP_US);
    CHECK_EQI((int)rx.payload_len, 32);
    CHECK_EQI(rx.payload[0], 'a');
    CHECK_EQI(rx.ttl, 64);

    /* Sequence numbers advance, so a reply can be matched to its request. */
    n = ns_build_ping(&g_ns, IP_PEER, PEER, 8, &seq, g_buf, sizeof g_buf);
    CHECK_EQI((int)seq, 2);

    /* Guards. */
    CHECK_EQI((int)ns_build_ping(&g_ns, 0, PEER, 8, &seq, g_buf, sizeof g_buf), 0);
    CHECK_EQI((int)ns_build_ping(&g_ns, IP_PEER, NULL, 8, &seq, g_buf, 64), 0);
    CHECK_EQI((int)ns_build_ping(&g_ns, IP_PEER, PEER, 8, &seq, g_buf, 20), 0);
    CHECK_EQI((int)ns_build_ping(NULL, IP_PEER, PEER, 8, &seq, g_buf, 64), 0);
}

static void st_udp(void)
{
    cu32 n;
    NsRx rx;
    NetStack peer;
    static const char msg[] = "hello wire";

    reset();
    ns_init(&peer, PEER, IP_PEER, MASK, 0);

    n = ns_build_udp(&g_ns, IP_PEER, PEER, 4096, 53, msg,
                     (cu32)sizeof msg - 1, g_buf, sizeof g_buf);
    CHECK_EQI((int)n, NS_ETH_HDR + NS_IP_HDR + NS_UDP_HDR + 10);
    CHECK_EQI(g_buf[NS_ETH_HDR + 9], NS_PROTO_UDP);
    CHECK_EQI((int)net_checksum(g_buf + NS_ETH_HDR, NS_IP_HDR), 0);

    CHECK_EQI((int)ns_receive(&peer, g_buf, n, &rx), NS_RX_UDP);
    CHECK_EQI((int)rx.sport, 4096);
    CHECK_EQI((int)rx.dport, 53);
    CHECK_EQI((int)rx.payload_len, 10);
    CHECK_EQI(rx.payload[0], 'h');

    /* An empty datagram is legal. A payload that would not fit is refused. */
    CHECK((int)ns_build_udp(&g_ns, IP_PEER, PEER, 1, 2, NULL, 0,
                            g_buf, sizeof g_buf) > 0);
    CHECK_EQI((int)ns_build_udp(&g_ns, IP_PEER, PEER, 1, 2, msg, 4,
                                g_buf, 20), 0);
    CHECK_EQI((int)ns_build_udp(&g_ns, IP_PEER, PEER, 1, 2, NULL, 4,
                                g_buf, sizeof g_buf), 0);
}

/* A stack must not believe what the wire tells it. */
static void st_hostile_frames(void)
{
    cu32 n;
    NsRx rx;
    NetStack peer;

    reset();
    ns_init(&peer, PEER, IP_PEER, MASK, 0);

    /* Truncated frames of every kind. */
    CHECK_EQI((int)ns_receive(&g_ns, g_buf, 4, &rx), NS_RX_MALFORMED);
    CHECK_EQI((int)ns_receive(&g_ns, NULL, 64, &rx), NS_RX_IGNORED);
    CHECK_EQI((int)ns_receive(NULL, g_buf, 64, &rx), NS_RX_IGNORED);

    n = ns_build_ping(&peer, IP_US, US, 16, NULL, g_buf, sizeof g_buf);
    CHECK_EQI((int)ns_receive(&g_ns, g_buf, n, &rx), NS_RX_PING_REQUEST);
    /* Cut it short of its own declared length: dropped, not read past. */
    CHECK_EQI((int)ns_receive(&g_ns, g_buf, n - 4, &rx), NS_RX_MALFORMED);
    /* Corrupt the IP header: the checksum must catch it. */
    g_buf[NS_ETH_HDR + 12] ^= 0x55;
    CHECK_EQI((int)ns_receive(&g_ns, g_buf, n, &rx), NS_RX_MALFORMED);
    g_buf[NS_ETH_HDR + 12] ^= 0x55;
    /* Corrupt the ICMP payload: its checksum must catch that too. */
    g_buf[NS_ETH_HDR + NS_IP_HDR + NS_ICMP_HDR + 2] ^= 0xFF;
    CHECK_EQI((int)ns_receive(&g_ns, g_buf, n, &rx), NS_RX_MALFORMED);

    /* A frame addressed to another station is not ours. */
    n = ns_build_ping(&peer, IP_US, US, 8, NULL, g_buf, sizeof g_buf);
    g_buf[0] = 0x02; g_buf[1] = 0x99;
    CHECK_EQI((int)ns_receive(&g_ns, g_buf, n, &rx), NS_RX_IGNORED);

    /* An IP packet for a different address, correctly formed, is ignored. */
    n = ns_build_ping(&peer, 0xC0A801FEUL, US, 8, NULL, g_buf, sizeof g_buf);
    CHECK_EQI((int)ns_receive(&g_ns, g_buf, n, &rx), NS_RX_IGNORED);

    /* An unknown EtherType and an unknown IP protocol are ignored quietly. */
    n = ns_build_ping(&peer, IP_US, US, 8, NULL, g_buf, sizeof g_buf);
    ns_put16(g_buf + 12, 0x86DD);                    /* IPv6: not ours yet  */
    CHECK_EQI((int)ns_receive(&g_ns, g_buf, n, &rx), NS_RX_IGNORED);
}

/* The full exchange, both stacks talking to each other through byte buffers. */
static void st_round_trip(void)
{
    static cu8 wire[NET_MTU];
    NetStack peer;
    NsRx rx;
    cu32 n;
    cu16 seq = 0;
    cu8 mac[NET_MAC_LEN];

    reset();
    ns_init(&peer, PEER, IP_PEER, MASK, 0);

    /* 1. We do not know the peer yet, so we ask. */
    CHECK(!ns_arp_get(&g_ns, IP_PEER, mac));
    n = ns_build_arp_request(&g_ns, ns_next_hop(&g_ns, IP_PEER),
                             wire, sizeof wire);
    CHECK_EQI((int)ns_receive(&peer, wire, n, &rx), NS_RX_ARP_REQUEST);

    /* 2. It answers, and now we know it. */
    n = ns_build_arp_reply(&peer, rx.src_ip, rx.src_mac, wire, sizeof wire);
    CHECK_EQI((int)ns_receive(&g_ns, wire, n, &rx), NS_RX_ARP_REPLY);
    CHECK(ns_arp_get(&g_ns, IP_PEER, mac));
    CHECK_EQI(mac[1], 0xAA);

    /* 3. We ping it. */
    n = ns_build_ping(&g_ns, IP_PEER, mac, 24, &seq, wire, sizeof wire);
    CHECK_EQI((int)ns_receive(&peer, wire, n, &rx), NS_RX_PING_REQUEST);

    /* 4. It echoes the request back as a reply (id, seq and payload kept). */
    {
        static cu8 back[NET_MTU];
        cu32 plen = rx.payload_len;
        cu32 m = ns_build_ping_reply(&peer, &rx, back, sizeof back);
        CHECK_EQI((int)m, (int)(NS_ETH_HDR + NS_IP_HDR + NS_ICMP_HDR + plen));
        CHECK_EQI(back[NS_ETH_HDR + NS_IP_HDR], 0);       /* echo reply     */
        CHECK_EQI((int)net_checksum(back + NS_ETH_HDR + NS_IP_HDR,
                                    NS_ICMP_HDR + plen), 0);
        CHECK_EQI((int)ns_receive(&g_ns, back, m, &rx), NS_RX_PING_REPLY);
        CHECK_EQI((int)rx.seq, (int)seq);     /* matches what we sent      */
        CHECK_EQI((int)rx.payload_len, (int)plen);
        CHECK_EQI(rx.payload[0], 'a');        /* our filler, echoed back    */
        CHECK(rx.src_ip == IP_PEER);
    }

    /* A reply can only be built for an actual request. */
    {
        NsRx bogus;
        bogus.kind = NS_RX_UDP;
        bogus.src_ip = IP_PEER;
        bogus.payload_len = 0;
        CHECK_EQI((int)ns_build_ping_reply(&peer, &bogus, wire, sizeof wire), 0);
        CHECK_EQI((int)ns_build_ping_reply(&peer, NULL, wire, sizeof wire), 0);
        CHECK_EQI((int)ns_build_ping_reply(NULL, &bogus, wire, sizeof wire), 0);
    }
}

void test_stack(void)
{
    printf("- net stack\n");
    st_byte_order();
    st_arp_cache();
    st_routing();
    st_arp_frames();
    st_ping();
    st_udp();
    st_hostile_frames();
    st_round_trip();
}
