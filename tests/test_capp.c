/*
 * test_capp.c - Host unit tests for the .CAPP container (capp.c).
 *
 * Builds a package in memory, parses it back and checks the manifest + section
 * round-trips, then mutates the image in every structurally interesting way and
 * confirms capp_parse rejects it (bad magic, wrong size, CRC mismatch,
 * out-of-bounds section). Also pins the CRC-32 against a known vector.
 */
#include "ctest.h"
#include "castalia/capp.h"

#include <string.h>

void test_capp(void)
{
    unsigned char img[1024];
    CappInfo meta, info;
    const char *code = "PLUGINCODE";
    const char *icon = "ICONBYTES!!";
    cu16 types[2];
    cu16 flags[2];
    const void *data[2];
    cu32 sizes[2];
    int total;

    /* ---- CRC-32 known-answer: "123456789" -> 0xCBF43926 ---- */
    CHECK_EQI(capp_crc32("123456789", 9), (long)0xCBF43926UL);
    CHECK_EQI(capp_crc32("", 0), 0);

    /* ---- build a two-section package ---- */
    memset(&meta, 0, sizeof(meta));
    meta.abi_version = 1;
    strcpy(meta.name, "Clock");
    strcpy(meta.version, "1.2.3");
    strcpy(meta.author, "Castalia");
    strcpy(meta.description, "A small desk clock add-on");
    types[0] = CAPP_SEC_CODE; flags[0] = 0; data[0] = code; sizes[0] = 10;
    types[1] = CAPP_SEC_ICON; flags[1] = 0; data[1] = icon; sizes[1] = 11;

    total = capp_build(img, sizeof(img), &meta, types, flags, data, sizes, 2);
    CHECK(total > CAPP_HEADER_SIZE);
    CHECK_EQI(total, CAPP_HEADER_SIZE + 2 * CAPP_SECTION_ENTRY_SIZE + 10 + 11);

    /* ---- parse it back ---- */
    CHECK_EQI(capp_parse(img, (cu32)total, &info), CE_OK);
    CHECK_STR(info.name, "Clock");
    CHECK_STR(info.version, "1.2.3");
    CHECK_STR(info.author, "Castalia");
    CHECK_STR(info.description, "A small desk clock add-on");
    CHECK_EQI(info.abi_version, 1);
    CHECK_EQI(info.section_count, 2);
    CHECK_EQI(info.sections[0].type, CAPP_SEC_CODE);
    CHECK_EQI(info.sections[0].size, 10);
    CHECK_EQI(info.sections[1].type, CAPP_SEC_ICON);

    /* ---- section lookup returns the payload bytes ---- */
    {
        const void *p; cu32 sz;
        CHECK(capp_find_section(img, &info, CAPP_SEC_CODE, &p, &sz));
        CHECK_EQI(sz, 10);
        CHECK(memcmp(p, code, 10) == 0);
        CHECK(capp_find_section(img, &info, CAPP_SEC_ICON, &p, &sz));
        CHECK(memcmp(p, icon, 11) == 0);
        CHECK(!capp_find_section(img, &info, CAPP_SEC_HELP, &p, &sz));
    }

    /* ---- rejection: bad magic ---- */
    {
        unsigned char bad[1024];
        memcpy(bad, img, (size_t)total);
        bad[0] = 'X';
        CHECK_EQI(capp_parse(bad, (cu32)total, &info), CE_INVALID);
    }
    /* ---- rejection: truncated / wrong total_size ---- */
    CHECK_EQI(capp_parse(img, (cu32)total - 1, &info), CE_INVALID);
    CHECK_EQI(capp_parse(img, CAPP_HEADER_SIZE - 1, &info), CE_INVALID);
    /* ---- rejection: a flipped payload byte -> CRC mismatch ---- */
    {
        unsigned char bad[1024];
        memcpy(bad, img, (size_t)total);
        bad[total - 1] ^= 0xFF;
        CHECK_EQI(capp_parse(bad, (cu32)total, &info), CE_FAIL);
    }
    /* ---- rejection: section offset pushed out of bounds. Bounds are checked
     *      before the CRC, so this fails with CE_OVERFLOW even though the flip
     *      also breaks the checksum. ---- */
    {
        unsigned char bad[1024];
        int off = CAPP_HEADER_SIZE + 4; /* section[0].offset (u32) */
        memcpy(bad, img, (size_t)total);
        bad[off] = 0xFF; bad[off + 1] = 0xFF; /* offset -> 65535, past the image */
        CHECK_EQI(capp_parse(bad, (cu32)total, &info), CE_OVERFLOW);
    }

    /* ---- a zero-section package is valid ---- */
    memset(&meta, 0, sizeof(meta));
    meta.abi_version = 1;
    strcpy(meta.name, "Empty");
    total = capp_build(img, sizeof(img), &meta, types, flags, data, sizes, 0);
    CHECK_EQI(total, CAPP_HEADER_SIZE);
    CHECK_EQI(capp_parse(img, (cu32)total, &info), CE_OK);
    CHECK_EQI(info.section_count, 0);
    CHECK_STR(info.name, "Empty");
}
