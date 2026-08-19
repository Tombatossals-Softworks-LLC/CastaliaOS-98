/*
 * test_car.c - The .CAR archive directory (car_core.c).
 *
 * An archive is a file that came from somewhere else, so most of this file is
 * about refusing them. The round trip is four checks; the rest are directories
 * that a damaged disk or a malicious author could produce, and every one of
 * them must be turned away before it becomes a file path or a memory range.
 *
 * The name checks matter most. "Extract these members into that folder" turns
 * into "write wherever the archive says" the moment a member is allowed to
 * carry a separator, and that mistake is older than any of the formats it has
 * been made in.
 */
#include "ctest.h"
#include "../src/apps/car_core.h"
#include "../src/apps/lzss_core.h"

#include <string.h>

static unsigned char g_buf[4096];

static void put(int index, const char *name, cu32 orig, cu32 stored,
                cu32 off, int method)
{
    CarEntry e;
    memset(&e, 0, sizeof(e));
    strcpy(e.name, name);
    e.original = orig; e.stored = stored; e.offset = off; e.method = method;
    car_write_entry(&e, index, g_buf, sizeof(g_buf));
}

void test_car(void)
{
    CarEntry e;
    cu32 total;

    printf("- archive directory\n");

    /* ---- the round trip ------------------------------------------------ */
    memset(g_buf, 0, sizeof(g_buf));
    CHECK_EQI((long)car_write_header(2, g_buf, sizeof(g_buf)), CAR_HEADER);
    put(0, "NOTES.TXT", 1000u, 300u, car_size_for(2, 0u), CZ_LZSS);
    put(1, "PHOTO.BMP", 500u,  500u, car_size_for(2, 0u) + 300u, CZ_STORED);
    total = car_size_for(2, 800u);

    CHECK_EQI(car_read_count(g_buf, total), 2);
    CHECK(car_read_entry(g_buf, total, 0, &e));
    CHECK_STR(e.name, "NOTES.TXT");
    CHECK_EQI((long)e.original, 1000);
    CHECK_EQI((long)e.stored, 300);
    CHECK_EQI(e.method, CZ_LZSS);
    CHECK(car_read_entry(g_buf, total, 1, &e));
    CHECK_STR(e.name, "PHOTO.BMP");
    CHECK_EQI(e.method, CZ_STORED);
    /* Past the end of the directory is not an entry. */
    CHECK(!car_read_entry(g_buf, total, 2, &e));
    CHECK(!car_read_entry(g_buf, total, -1, &e));

    /* ---- names that must never become paths ---------------------------- */
    CHECK(car_name_ok("NOTES.TXT"));
    CHECK(car_name_ok("A"));
    CHECK(!car_name_ok("../SECRET"));
    CHECK(!car_name_ok("..\\SECRET"));
    CHECK(!car_name_ok("SUB/FILE.TXT"));
    CHECK(!car_name_ok("SUB\\FILE.TXT"));
    CHECK(!car_name_ok("C:FILE.TXT"));
    CHECK(!car_name_ok(".."));
    CHECK(!car_name_ok("."));
    CHECK(!car_name_ok(""));
    CHECK(!car_name_ok(NULL));
    /* Control characters do not belong in a filename either. */
    CHECK(!car_name_ok("BAD\001NAME"));
    /* A name that fills the field with no terminator is not a name. */
    CHECK(!car_name_ok("ABCDEFGHIJKLMN"));

    /* The writer refuses what the reader would refuse, so an archive we make
     * is always an archive we can open. */
    CHECK_EQI((long)car_write_entry(NULL, 0, g_buf, sizeof(g_buf)), 0);
    {
        CarEntry bad;
        memset(&bad, 0, sizeof(bad));
        strcpy(bad.name, "../ESCAPE");
        bad.method = CZ_LZSS;
        CHECK_EQI((long)car_write_entry(&bad, 0, g_buf, sizeof(g_buf)), 0);
        strcpy(bad.name, "FINE.TXT");
        bad.method = 42;
        CHECK_EQI((long)car_write_entry(&bad, 0, g_buf, sizeof(g_buf)), 0);
    }

    /* ---- a directory that escapes must be refused on READ too ---------- */
    /* Written by hand, because our writer will not produce it. */
    memset(g_buf, 0, sizeof(g_buf));
    car_write_header(1, g_buf, sizeof(g_buf));
    put(0, "OK.TXT", 10u, 10u, car_size_for(1, 0u), CZ_STORED);
    /* Now scribble a traversal into the name field the writer protected. */
    memcpy(g_buf + CAR_HEADER, "..\\BOOT.INI\0\0", 13);
    CHECK(!car_read_entry(g_buf, car_size_for(1, 10u), 0, &e));

    /* ---- headers that lie ---------------------------------------------- */
    memset(g_buf, 0, sizeof(g_buf));
    car_write_header(2, g_buf, sizeof(g_buf));
    /* Not a .CAR at all. */
    g_buf[1] = 'X';
    CHECK_EQI(car_read_count(g_buf, 200u), -1);
    g_buf[1] = 'A';
    /* A directory larger than the file that holds it. */
    CHECK_EQI(car_read_count(g_buf, (cu32)CAR_HEADER + 4u), -1);
    CHECK_EQI(car_read_count(g_buf, 4u), -1);
    CHECK_EQI(car_read_count(NULL, 200u), -1);
    /* More members than the format allows. */
    g_buf[4] = 0xFF; g_buf[5] = 0xFF;
    CHECK_EQI(car_read_count(g_buf, 100000u), -1);

    /* ---- members that point outside the file --------------------------- */
    memset(g_buf, 0, sizeof(g_buf));
    car_write_header(1, g_buf, sizeof(g_buf));
    total = car_size_for(1, 100u);
    /* Past the end. */
    put(0, "A.TXT", 10u, 10u, total + 1u, CZ_STORED);
    CHECK(!car_read_entry(g_buf, total, 0, &e));
    /* Starts inside, runs off the end. */
    put(0, "A.TXT", 200u, 200u, total - 10u, CZ_STORED);
    CHECK(!car_read_entry(g_buf, total, 0, &e));
    /* Inside the directory itself, which would make a member overlap the
     * entries describing it. */
    put(0, "A.TXT", 4u, 4u, 4u, CZ_STORED);
    CHECK(!car_read_entry(g_buf, total, 0, &e));
    /* An offset and length that wrap a 32-bit sum: 'offset + stored <= total'
     * on its own would pass this, which is why the check is written the other
     * way round. */
    put(0, "A.TXT", 16u, 0xFFFFFFF0u, total - 4u, CZ_LZSS);
    CHECK(!car_read_entry(g_buf, total, 0, &e));
    /* A stored member whose two sizes disagree. */
    put(0, "A.TXT", 50u, 10u, car_size_for(1, 0u), CZ_STORED);
    CHECK(!car_read_entry(g_buf, total, 0, &e));
    /* ...and the same one, honest, is accepted. */
    put(0, "A.TXT", 10u, 10u, car_size_for(1, 0u), CZ_STORED);
    CHECK(car_read_entry(g_buf, total, 0, &e));
    CHECK_EQI((long)e.offset, (long)car_size_for(1, 0u));

    /* ---- sizes and refusals -------------------------------------------- */
    CHECK_EQI((long)car_size_for(0, 0u), CAR_HEADER);
    CHECK_EQI((long)car_size_for(1, 0u), CAR_HEADER + CAR_ENTRY);
    CHECK_EQI((long)car_size_for(3, 100u), CAR_HEADER + 3 * CAR_ENTRY + 100);
    CHECK_EQI((long)car_size_for(-5, 10u), CAR_HEADER + 10);
    CHECK_EQI((long)car_write_header(-1, g_buf, sizeof(g_buf)), 0);
    CHECK_EQI((long)car_write_header(CAR_MAX_FILES + 1, g_buf, sizeof(g_buf)), 0);
    CHECK_EQI((long)car_write_header(1, g_buf, 4u), 0);
    CHECK_EQI((long)car_write_header(1, NULL, 100u), 0);
    CHECK(!car_read_entry(g_buf, 100u, 0, NULL));

    /* An empty archive is valid and holds nothing. */
    memset(g_buf, 0, sizeof(g_buf));
    car_write_header(0, g_buf, sizeof(g_buf));
    CHECK_EQI(car_read_count(g_buf, (cu32)CAR_HEADER), 0);
    CHECK(!car_read_entry(g_buf, (cu32)CAR_HEADER, 0, &e));
}
