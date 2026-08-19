/*
 * net.h - Layer 1 optional networking interface (Castalia-owned).
 *
 * Networking is OPTIONAL and must never be required (Project Bible): the shell
 * reaches the desktop identically with it absent. The seam sits at the DOS
 * packet-driver level -- the same foundation mTCP builds on -- so higher layers
 * (ARP/IP/UDP/TCP) can be assembled on top later without touching hardware.
 *
 * Backends:
 *   - src/platform/host/net_host.c  a null driver by default: no device, so
 *     net_available() is CFALSE and CI never needs a NIC. It can optionally be
 *     switched to a clearly-labelled SIMULATED wire (--net-loopback) so the
 *     protocol layer above can be driven end to end on a machine with no
 *     hardware.
 *   - src/platform/dos/net_pkt.c    a real packet-driver client: scans the
 *     software-interrupt vectors for an installed driver, reads its class and
 *     station (MAC) address, and sends raw Ethernet frames. The receive path
 *     (an access_type receiver callback) is a documented next step, so on DOS
 *     frames go out but none come back yet.
 *
 * ARP / IPv4 / ICMP / UDP live one layer up, in net_stack.h -- also portable,
 * also host-tested. The address/checksum helpers below are pure and
 * host-tested; nothing above this layer issues a software interrupt or touches
 * the NIC.
 */
#ifndef CASTALIA_NET_H
#define CASTALIA_NET_H

#include "castalia/ctypes.h"

#define NET_MAC_LEN 6
#define NET_MTU     1514   /* max Ethernet frame incl. 14-byte header       */

typedef struct {
    cbool available;           /* CTRUE if a driver/NIC was found            */
    cu8   mac[NET_MAC_LEN];    /* station address (zeroed if unknown)        */
    int   int_no;              /* packet-driver software interrupt (DOS)     */
    int   class_id;            /* driver class (1 = DIX/Ethernet)            */
    char  name[40];            /* human string for System Info               */
} NetDeviceInfo;

/* Bring the network backend up. Returns CE_OK if a device is present, or
 * CE_UNSUPPORTED if none is available (callers treat that as "run offline",
 * not an error). */
CResult net_init(void);

/* Tear down (releases any packet-driver handle). Symmetric with net_init. */
void    net_shutdown(void);

/* Is a working link available? (CFALSE on the null backend.) */
cbool   net_available(void);

/* Fill 'out' with the detected device details (name/MAC/class), or a
 * name of "none" when unavailable. */
void    net_device_info(NetDeviceInfo *out);

/* Send a raw Ethernet frame (dest[6] src[6] type[2] payload...), up to NET_MTU
 * bytes. Returns CE_OK, or CE_UNSUPPORTED on the null backend. */
CResult net_send_frame(const void *frame, cu32 len);

/* Poll for one received frame into 'buf' (cap bytes). Returns the frame length,
 * 0 if none is pending, or a negative CResult on error. Non-blocking. */
int     net_poll_frame(void *buf, cu32 cap);

/* ---------------------------------------------------------------------- */
/* Portable address / checksum helpers (host-tested in tests/test_net.c)  */
/* ---------------------------------------------------------------------- */

/* Parse "a.b.c.d" into a packed IPv4 value 0xAABBCCDD (a is the high byte).
 * Rejects out-of-range octets and malformed strings. Returns CTRUE on ok. */
cbool net_ipv4_parse(const char *s, cu32 *out);

/* Format a packed IPv4 (as from net_ipv4_parse) as "a.b.c.d". */
void  net_ipv4_format(cu32 ip, char *dst, cu32 dstsz);

/* Format a MAC as "AA:BB:CC:DD:EE:FF". */
void  net_mac_format(const cu8 mac[NET_MAC_LEN], char *dst, cu32 dstsz);

/* RFC 1071 Internet checksum (ones-complement 16-bit sum) over 'len' bytes,
 * as used by IP/ICMP/UDP headers. Handles an odd length. */
cu16  net_checksum(const void *data, cu32 len);

#endif /* CASTALIA_NET_H */
