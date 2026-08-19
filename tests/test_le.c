/*
 * test_le.c - Little-endian byte access (sys_le.c).
 *
 * These four functions decide the byte order of every file CastaliaOS writes:
 * add-on packages, bitmaps, WAV audio, .CAR archives, .CZ compressed files.
 * They existed five times over -- once in each of those readers -- and an
 * invariant with five implementations is five chances to corrupt a format in
 * a way that does not crash.
 *
 * So the checks pin the ORDER against literal bytes, not against a round
 * trip. A big-endian implementation round-trips perfectly and writes files
 * nothing else can read; only an explicit "byte 0 is the low one" can tell
 * the difference, and that is what this file is for.
 */
#include "ctest.h"
#include "castalia/sys.h"

#include <string.h>

void test_le(void)
{
    unsigned char b[8];
    unsigned char zero[8];

    printf("- little-endian byte access\n");
    memset(zero, 0, sizeof(zero));

    /* ---- the order itself --------------------------------------------- */
    memcpy(b, "\x78\x56\x34\x12", 4);
    CHECK_EQI((long)sys_le32(b), 0x12345678L);
    memcpy(b, "\x34\x12", 2);
    CHECK_EQI((long)sys_le16(b), 0x1234L);

    memset(b, 0xAA, sizeof(b));
    sys_put_le32(b, 0x12345678UL);
    CHECK_EQI(b[0], 0x78);
    CHECK_EQI(b[1], 0x56);
    CHECK_EQI(b[2], 0x34);
    CHECK_EQI(b[3], 0x12);
    /* ...and it wrote exactly four bytes, not five. */
    CHECK_EQI(b[4], 0xAA);

    memset(b, 0xAA, sizeof(b));
    sys_put_le16(b, 0x1234u);
    CHECK_EQI(b[0], 0x34);
    CHECK_EQI(b[1], 0x12);
    CHECK_EQI(b[2], 0xAA);

    /* ---- the values that overflow a signed type ------------------------ */
    memcpy(b, "\xFF\xFF\xFF\xFF", 4);
    CHECK(sys_le32(b) == 0xFFFFFFFFUL);
    memcpy(b, "\x00\x00\x00\x80", 4);
    CHECK(sys_le32(b) == 0x80000000UL);   /* the sign bit is data, not sign */
    memcpy(b, "\xFF\xFF", 2);
    CHECK(sys_le16(b) == 0xFFFFu);
    memcpy(b, "\x00\x80", 2);
    CHECK(sys_le16(b) == 0x8000u);

    sys_put_le32(b, 0xFFFFFFFFUL);
    CHECK(sys_le32(b) == 0xFFFFFFFFUL);
    sys_put_le32(b, 0UL);
    CHECK(sys_le32(b) == 0UL);
    CHECK(memcmp(b, zero, 4) == 0);

    /* ---- round trips over a spread of values --------------------------- */
    {
        static const cu32 V[8] = {
            0UL, 1UL, 0xFFUL, 0x100UL, 0xFFFFUL, 0x10000UL,
            0x7FFFFFFFUL, 0xDEADBEEFUL
        };
        int i;
        for (i = 0; i < 8; i++) {
            sys_put_le32(b, V[i]);
            CHECK(sys_le32(b) == V[i]);
        }
        for (i = 0; i < 6; i++) {
            sys_put_le16(b, (cu16)(V[i] & 0xFFFFUL));
            CHECK(sys_le16(b) == (cu16)(V[i] & 0xFFFFUL));
        }
    }

    /* ---- a NULL is a failed read, not a crash -------------------------- */
    CHECK(sys_le16(NULL) == 0u);
    CHECK(sys_le32(NULL) == 0UL);
    sys_put_le16(NULL, 1u);
    sys_put_le32(NULL, 1UL);
}
