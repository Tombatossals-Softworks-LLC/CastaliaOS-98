/*
 * test_hex.c - Hex dump layout (hex_core.c).
 *
 * The interesting property is not that a full row renders -- it is that a
 * SHORT row renders to the same shape. A hex dump's last line is almost never
 * sixteen bytes long, and it is the line somebody is actually reading, so the
 * checks below walk every short-row length from 1 to 15 and require the text
 * column to begin at the same character every time.
 *
 * The other one is that scrolling cannot produce a view that is not there:
 * no negative offset, no page past the end with the data above it, and never
 * an address that does not end in 0.
 */
#include "ctest.h"
#include "../src/apps/hex_core.h"

#include <string.h>

/* Where the text column starts: 8 address + 2 gap + 16*3 hex + 1 mid gap
 * + 1 gap. Written out rather than computed so a change to the layout has to
 * be made here too, deliberately. */
#define HEX_TEXT_COL 60

static void ht_full_row(void)
{
    /* The first sixteen bytes of a DOS executable, which is the thing this
     * viewer exists to let somebody check. */
    static const unsigned char mz[16] = {
        0x4D, 0x5A, 0x90, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00
    };
    char b[HEX_LINE_MAX];
    int n;

    n = hex_format_row(b, sizeof(b), 0UL, mz, 16);
    CHECK_STR(b, "00000000  4D 5A 90 00 03 00 00 00  "
                 "04 00 00 00 FF FF 00 00  MZ..............");
    CHECK_EQI(n, (int)strlen(b));
    CHECK_EQI(n, HEX_TEXT_COL + 16);

    /* The address is eight digits, uppercase, zero padded -- so two rows can
     * be compared by eye and a column of them stays a column. */
    hex_format_row(b, sizeof(b), 0xABCDEUL, mz, 16);
    CHECK(strncmp(b, "000ABCDE  ", 10) == 0);
    hex_format_row(b, sizeof(b), 0xFFFFFFFFUL, mz, 16);
    CHECK(strncmp(b, "FFFFFFFF  ", 10) == 0);
}

static void ht_short_rows(void)
{
    unsigned char buf[16];
    char b[HEX_LINE_MAX];
    int n, i;

    for (i = 0; i < 16; i++) { buf[i] = (unsigned char)('A' + i); }

    /* The whole point. Every short row pads its hex columns, so the text
     * column begins at the same character no matter how many bytes are left
     * -- a dump whose last line slid four characters left is the classic way
     * to get this wrong, and it looks like a font problem. */
    for (n = 1; n <= 16; n++) {
        int len = hex_format_row(b, sizeof(b), (unsigned long)(n * 16), buf, n);
        CHECK_EQI(len, HEX_TEXT_COL + n);
        CHECK(b[HEX_TEXT_COL] == (char)('A'));
        CHECK(b[HEX_TEXT_COL - 1] == ' ');
        /* ...and the columns with no byte in them are blank, not stale. */
        for (i = n; i < 16; i++) {
            int col = 10 + i * 3 + (i > 7 ? 1 : 0);
            CHECK(b[col] == ' ' && b[col + 1] == ' ');
        }
    }
}

static void ht_text_column(void)
{
    unsigned char buf[16];
    char b[HEX_LINE_MAX];
    int i;

    /* Printable ASCII survives; everything else is a dot. */
    CHECK(hex_ascii(0x41) == 'A');
    CHECK(hex_ascii(0x20) == ' ');
    CHECK(hex_ascii(0x7E) == '~');
    CHECK(hex_ascii(0x00) == '.');
    CHECK(hex_ascii(0x1F) == '.');
    CHECK(hex_ascii(0x0A) == '.');   /* a newline must not end the line */
    CHECK(hex_ascii(0x7F) == '.');   /* DEL                             */
    /* High bytes are dots rather than passed through: on a DOS codepage they
     * are box-drawing characters, and binary rubbish would look like a table
     * with borders. */
    CHECK(hex_ascii(0x80) == '.');
    CHECK(hex_ascii(0xB3) == '.');
    CHECK(hex_ascii(0xFF) == '.');

    for (i = 0; i < 16; i++) { buf[i] = (unsigned char)i; }
    hex_format_row(b, sizeof(b), 0UL, buf, 16);
    for (i = 0; i < 16; i++) { CHECK(b[HEX_TEXT_COL + i] == '.'); }
    /* The hex half still shows what those bytes really are. */
    CHECK(strncmp(b + 10, "00 01 02", 8) == 0);
}

static void ht_scroll(void)
{
    const long K = 4096;             /* 256 rows */

    /* Ordinary movement, a row at a time and a page at a time. */
    CHECK_EQI(hex_scroll(0, K, 20, 1), 16);
    CHECK_EQI(hex_scroll(0, K, 20, 20), 320);
    CHECK_EQI(hex_scroll(320, K, 20, -20), 0);

    /* Never before the start. */
    CHECK_EQI(hex_scroll(0, K, 20, -1), 0);
    CHECK_EQI(hex_scroll(0, K, 20, -9999), 0);
    CHECK_EQI(hex_scroll(16, K, 20, -5), 0);

    /* Never past the last screenful: the bottom row of the file lands on the
     * bottom of the view, and no further. 256 rows, 20 visible -> the last
     * top row is 236, at offset 3776. */
    CHECK_EQI(hex_scroll(0, K, 20, 9999), 3776);
    CHECK_EQI(hex_scroll(3776, K, 20, 1), 3776);

    /* A file shorter than the window does not scroll at all -- otherwise the
     * data walks off the top and leaves a blank page. */
    CHECK_EQI(hex_scroll(0, 100, 20, 5), 0);
    CHECK_EQI(hex_scroll(0, 100, 20, 9999), 0);
    CHECK_EQI(hex_scroll(0, 0, 20, 1), 0);

    /* Exactly one screenful is still not scrollable. */
    CHECK_EQI(hex_scroll(0, 20 * 16, 20, 1), 0);
    /* One byte more is one row of travel. */
    CHECK_EQI(hex_scroll(0, 20 * 16 + 1, 20, 9999), 16);

    /* An unaligned starting offset comes back on a row boundary, so the
     * addresses down the left edge still end in 0. */
    CHECK_EQI(hex_scroll(7, K, 20, 0), 0);
    CHECK_EQI(hex_scroll(1000, K, 20, 0), 992);

    /* A window that claims no rows is treated as one, not as a divide by
     * zero or a negative last row. */
    CHECK_EQI(hex_scroll(0, K, 0, 9999), 4080);

    CHECK_EQI(hex_row_count(0), 0);
    CHECK_EQI(hex_row_count(1), 1);
    CHECK_EQI(hex_row_count(16), 1);
    CHECK_EQI(hex_row_count(17), 2);
    CHECK_EQI(hex_row_count(-5), 0);
}

static void ht_guards(void)
{
    unsigned char buf[16];
    char b[HEX_LINE_MAX];
    char small[8];

    memset(buf, 0, sizeof(buf));
    b[0] = 'z';
    CHECK_EQI(hex_format_row(NULL, sizeof(b), 0UL, buf, 16), 0);
    CHECK_EQI(hex_format_row(b, sizeof(b), 0UL, NULL, 16), 0);
    CHECK(b[0] == 'z');
    /* A count outside the row is refused rather than clamped: it means the
     * caller's arithmetic is wrong, and half a row of someone else's memory
     * would render perfectly. */
    CHECK_EQI(hex_format_row(b, sizeof(b), 0UL, buf, 0), 0);
    CHECK_EQI(hex_format_row(b, sizeof(b), 0UL, buf, 17), 0);
    CHECK_EQI(hex_format_row(b, sizeof(b), 0UL, buf, -1), 0);
    /* A buffer too small is refused up front rather than truncated, because a
     * truncated hex row is a plausible-looking wrong answer. */
    small[0] = 'z';
    CHECK_EQI(hex_format_row(small, sizeof(small), 0UL, buf, 16), 0);
    CHECK(small[0] == 'z');
}

void test_hex(void)
{
    printf("- hex dump layout\n");
    ht_full_row();
    ht_short_rows();
    ht_text_column();
    ht_scroll();
    ht_guards();
}
