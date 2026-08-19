/*
 * net_util.c - Portable networking helpers (see net.h).
 *
 * Address parsing/formatting and the Internet checksum: pure logic with no
 * hardware access, shared by the DOS packet-driver client and host-tested in
 * tests/test_net.c.
 */
#include "castalia/net.h"
#include "castalia/sys.h"

cbool net_ipv4_parse(const char *s, cu32 *out)
{
    cu32 ip = 0;
    const char *p = s;
    int i;
    if (s == NULL || out == NULL) { return CFALSE; }
    for (i = 0; i < 4; i++) {
        int val = 0, digits = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            if (val > 255) { return CFALSE; }
            digits++;
            p++;
        }
        if (digits == 0) { return CFALSE; }
        ip = (ip << 8) | (cu32)(val & 0xFF);
        if (i < 3) {
            if (*p != '.') { return CFALSE; }
            p++;
        }
    }
    if (*p != '\0') { return CFALSE; }
    *out = ip;
    return CTRUE;
}

void net_ipv4_format(cu32 ip, char *dst, cu32 dstsz)
{
    sys_snprintf(dst, dstsz, "%d.%d.%d.%d",
                 (int)((ip >> 24) & 0xFF), (int)((ip >> 16) & 0xFF),
                 (int)((ip >> 8) & 0xFF),  (int)(ip & 0xFF));
}

void net_mac_format(const cu8 mac[NET_MAC_LEN], char *dst, cu32 dstsz)
{
    sys_snprintf(dst, dstsz, "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

cu16 net_checksum(const void *data, cu32 len)
{
    const unsigned char *p = (const unsigned char *)data;
    cu32 sum = 0;
    while (len > 1) {
        sum += ((cu32)p[0] << 8) | (cu32)p[1];   /* network byte order */
        p += 2;
        len -= 2;
    }
    if (len == 1) { sum += (cu32)p[0] << 8; }
    while (sum >> 16) { sum = (sum & 0xFFFF) + (sum >> 16); }
    return (cu16)(~sum & 0xFFFF);
}
