/*
 * net_stack.h - ARP / IPv4 / ICMP / UDP over the raw-frame seam.
 *
 * net.h stops at "send me an Ethernet frame". This is the layer above it: it
 * builds and parses the frames themselves, keeps a small ARP cache, and
 * remembers the local address configuration. It touches no hardware and issues
 * no software interrupt -- it turns bytes into bytes, so the host tests can
 * drive the whole protocol path with no NIC (tests/test_stack.c).
 *
 * Everything is big-endian on the wire and built by hand, one octet at a time:
 * there is no htons() to depend on and no struct packing to trust.
 *
 * What is NOT here, on purpose: TCP. UDP and ICMP are what a status tool, a
 * name lookup and a trivial fetch need; TCP is a later phase and pretending
 * otherwise would be a lie in a header file.
 */
#ifndef CASTALIA_NET_STACK_H
#define CASTALIA_NET_STACK_H

#include "castalia/ctypes.h"
#include "castalia/net.h"

#define NS_ETH_HDR    14
#define NS_ARP_FRAME  42          /* 14 Ethernet + 28 ARP                   */
#define NS_IP_HDR     20
#define NS_ICMP_HDR   8
#define NS_UDP_HDR    8

#define NS_TYPE_IP    0x0800
#define NS_TYPE_ARP   0x0806
#define NS_PROTO_ICMP 1
#define NS_PROTO_UDP  17

#define NS_ARP_ENTRIES 8

/* ---- byte-order helpers (explicit, so nothing depends on the host) ----- */
void  ns_put16(cu8 *p, cu16 v);
void  ns_put32(cu8 *p, cu32 v);
cu16  ns_get16(const cu8 *p);
cu32  ns_get32(const cu8 *p);

/* ---- the local configuration and ARP cache ---------------------------- */
typedef struct {
    cu32 ip;                       /* 0 when not resolved yet              */
    cu8  mac[NET_MAC_LEN];
    int  age;                      /* bumped on use; 0 = free slot         */
} NsArpEntry;

typedef struct {
    cu8  mac[NET_MAC_LEN];         /* our station address                  */
    cu32 ip, mask, gateway;
    NsArpEntry arp[NS_ARP_ENTRIES];
    int  clock;                    /* monotonic tick for cache aging       */
    cu16 ident;                    /* IP identification counter            */
    cu16 ping_id;                  /* our ICMP echo identifier             */
    cu16 ping_seq;
} NetStack;

void  ns_init(NetStack *ns, const cu8 mac[NET_MAC_LEN], cu32 ip, cu32 mask,
              cu32 gateway);

/* Remember (or refresh) an address pair. */
void  ns_arp_set(NetStack *ns, cu32 ip, const cu8 mac[NET_MAC_LEN]);
/* Look one up; CTRUE and fills 'out' when known. */
cbool ns_arp_get(NetStack *ns, cu32 ip, cu8 out[NET_MAC_LEN]);
int   ns_arp_count(const NetStack *ns);
/* Enumerate the cache for a UI: CTRUE while 'index' names a live entry. */
cbool ns_arp_entry(const NetStack *ns, int index, cu32 *ip,
                   cu8 out[NET_MAC_LEN]);

/* Which address a packet to 'dst' is actually sent to: the host itself when
 * it shares our subnet, otherwise the gateway. */
cu32  ns_next_hop(const NetStack *ns, cu32 dst);

/* ---- parsing ---------------------------------------------------------- */
typedef enum {
    NS_RX_IGNORED = 0,    /* not for us, or a protocol we do not speak     */
    NS_RX_ARP_REQUEST,    /* someone is asking for our address             */
    NS_RX_ARP_REPLY,      /* ...and the cache was updated                  */
    NS_RX_PING_REQUEST,   /* an echo request addressed to us               */
    NS_RX_PING_REPLY,     /* the answer to one of ours                     */
    NS_RX_UDP,
    NS_RX_MALFORMED       /* truncated / bad checksum: dropped, and said so */
} NsRxKind;

typedef struct {
    NsRxKind kind;
    cu32 src_ip, dst_ip;
    cu8  src_mac[NET_MAC_LEN];
    cu16 id, seq;                  /* ICMP echo identifier / sequence      */
    cu16 sport, dport;             /* UDP ports                            */
    const cu8 *payload;            /* into the caller's buffer, not copied */
    cu32 payload_len;
    int  ttl;
} NsRx;

/* Classify one received Ethernet frame. Updates the ARP cache from anything
 * that carries a usable address pair. 'out' may be NULL. */
NsRxKind ns_receive(NetStack *ns, const cu8 *frame, cu32 len, NsRx *out);

/* ---- builders: each returns the frame length written, or 0 ------------- */
/* "Who has <target>? Tell <us>", broadcast. Needs NS_ARP_FRAME bytes. */
cu32  ns_build_arp_request(const NetStack *ns, cu32 target, cu8 *out, cu32 cap);
/* The reply to an ARP request that asked for our address. */
cu32  ns_build_arp_reply(const NetStack *ns, cu32 to_ip,
                         const cu8 to_mac[NET_MAC_LEN], cu8 *out, cu32 cap);
/* An ICMP echo request to 'dst' via 'dst_mac', with 'payload_len' filler
 * bytes. Bumps the stack's sequence counter and reports it in *out_seq. */
cu32  ns_build_ping(NetStack *ns, cu32 dst, const cu8 dst_mac[NET_MAC_LEN],
                    int payload_len, cu16 *out_seq, cu8 *out, cu32 cap);
/* The echo REPLY to a request we just received (its identifier, sequence and
 * payload are echoed back verbatim, which is what the sender matches on). */
cu32  ns_build_ping_reply(NetStack *ns, const NsRx *rx, cu8 *out, cu32 cap);

/* A UDP datagram. */
cu32  ns_build_udp(NetStack *ns, cu32 dst, const cu8 dst_mac[NET_MAC_LEN],
                   cu16 sport, cu16 dport, const void *data, cu32 len,
                   cu8 *out, cu32 cap);

#endif /* CASTALIA_NET_STACK_H */
