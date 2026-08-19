/*
 * test_net.c - Host unit tests for the portable networking helpers
 * (net_util.c): IPv4 parse/format, MAC formatting, and the Internet checksum.
 */
#include "ctest.h"
#include "castalia/net.h"

void test_net(void)
{
    cu32 ip = 0;
    char s[32];
    cu8  mac[NET_MAC_LEN];

    /* ---- IPv4 parse ---- */
    CHECK(net_ipv4_parse("192.168.1.1", &ip));
    CHECK_EQI(ip, (long)0xC0A80101UL);
    CHECK(net_ipv4_parse("0.0.0.0", &ip));
    CHECK_EQI(ip, 0);
    CHECK(net_ipv4_parse("255.255.255.255", &ip));
    CHECK_EQI(ip, (long)0xFFFFFFFFUL);
    CHECK(net_ipv4_parse("10.0.0.138", &ip));
    CHECK_EQI(ip, (long)0x0A00008AUL);

    /* ---- IPv4 parse rejections ---- */
    CHECK(!net_ipv4_parse("256.1.1.1", &ip));   /* octet out of range */
    CHECK(!net_ipv4_parse("1.2.3", &ip));       /* too few */
    CHECK(!net_ipv4_parse("1.2.3.4.5", &ip));   /* too many */
    CHECK(!net_ipv4_parse("1.2.3.", &ip));      /* trailing dot */
    CHECK(!net_ipv4_parse("1..3.4", &ip));      /* empty octet */
    CHECK(!net_ipv4_parse("a.b.c.d", &ip));     /* non-numeric */
    CHECK(!net_ipv4_parse("", &ip));

    /* ---- IPv4 format round-trip ---- */
    net_ipv4_format(0xC0A80101UL, s, sizeof(s));
    CHECK_STR(s, "192.168.1.1");
    net_ipv4_format(0x08080808UL, s, sizeof(s));
    CHECK_STR(s, "8.8.8.8");

    /* ---- MAC formatting ---- */
    mac[0] = 0xAA; mac[1] = 0xBB; mac[2] = 0xCC;
    mac[3] = 0xDD; mac[4] = 0xEE; mac[5] = 0xFF;
    net_mac_format(mac, s, sizeof(s));
    CHECK_STR(s, "AA:BB:CC:DD:EE:FF");
    mac[0] = 0x00; mac[1] = 0x1B; mac[2] = 0x44;
    mac[3] = 0x11; mac[4] = 0x3A; mac[5] = 0xB7;
    net_mac_format(mac, s, sizeof(s));
    CHECK_STR(s, "00:1B:44:11:3A:B7");

    /* ---- Internet checksum (RFC 1071) landmarks ---- */
    {
        unsigned char z2[2]  = { 0x00, 0x00 };
        unsigned char f2[2]  = { 0xFF, 0xFF };
        unsigned char f4[4]  = { 0xFF, 0xFF, 0xFF, 0xFF };
        unsigned char odd[3] = { 0x00, 0x00, 0xFF };
        CHECK_EQI(net_checksum(z2, 2), (long)0xFFFF);
        CHECK_EQI(net_checksum(f2, 2), 0);
        CHECK_EQI(net_checksum(f4, 4), 0);     /* folds carry */
        CHECK_EQI(net_checksum(odd, 3), (long)0x00FF);
    }
    /* Property: checksum over a buffer that already carries its checksum in a
     * trailing word sums to zero. */
    {
        unsigned char pkt[6];
        cu16 c;
        pkt[0] = 0x45; pkt[1] = 0x00; pkt[2] = 0x00; pkt[3] = 0x3C;
        c = net_checksum(pkt, 4);
        pkt[4] = (unsigned char)(c >> 8);
        pkt[5] = (unsigned char)(c & 0xFF);
        CHECK_EQI(net_checksum(pkt, 6), 0);
    }
}
