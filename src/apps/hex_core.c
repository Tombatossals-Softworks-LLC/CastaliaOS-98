/*
 * hex_core.c - Hex dump layout (see hex_core.h). Pure string building.
 */
#include "hex_core.h"
#include "castalia/sys.h"

static const char HEX_DIGIT[] = "0123456789ABCDEF";

char hex_ascii(unsigned char b)
{
    /* Printable ASCII only. Byte 0x7F is DEL and 0x80 and up are whatever the
     * active codepage says -- on DOS that is box-drawing characters, which
     * would give random binary the look of a table. */
    return (b >= 0x20 && b < 0x7F) ? (char)b : '.';
}

int hex_format_row(char *dst, cu32 dstsz, unsigned long addr,
                   const unsigned char *bytes, int n)
{
    int i, p = 0;

    if (dst == NULL || bytes == NULL) { return 0; }
    if (n < 1 || n > HEX_ROW_BYTES) { return 0; }
    if (dstsz < (cu32)HEX_LINE_MAX) { return 0; }

    for (i = 7; i >= 0; i--) {
        dst[p++] = HEX_DIGIT[(addr >> (i * 4)) & 0xFUL];
    }
    dst[p++] = ' ';
    dst[p++] = ' ';

    for (i = 0; i < HEX_ROW_BYTES; i++) {
        /* Every column is written whether or not there is a byte for it. A
         * short final row that simply stopped here would pull its text column
         * left, and the last line of a file is the one being read closely. */
        if (i < n) {
            dst[p++] = HEX_DIGIT[(bytes[i] >> 4) & 0xF];
            dst[p++] = HEX_DIGIT[bytes[i] & 0xF];
        } else {
            dst[p++] = ' ';
            dst[p++] = ' ';
        }
        dst[p++] = ' ';
        /* An extra space down the middle, so a byte can be counted off in
         * two groups of eight instead of sixteen. */
        if (i == 7) { dst[p++] = ' '; }
    }
    dst[p++] = ' ';

    for (i = 0; i < n; i++) { dst[p++] = hex_ascii(bytes[i]); }
    dst[p] = '\0';
    return p;
}

long hex_row_count(long size)
{
    if (size <= 0) { return 0; }
    return (size + HEX_ROW_BYTES - 1) / HEX_ROW_BYTES;
}

long hex_scroll(long top, long size, int rows_visible, int delta_rows)
{
    long rows, last_top;

    if (rows_visible < 1) { rows_visible = 1; }
    rows = hex_row_count(size);
    /* The furthest down the view may start: the last screenful, aligned to a
     * row. A file shorter than the window does not scroll at all. */
    last_top = (rows > (long)rows_visible)
                   ? (rows - (long)rows_visible) * HEX_ROW_BYTES
                   : 0L;

    top += (long)delta_rows * HEX_ROW_BYTES;
    if (top < 0L) { top = 0L; }
    if (top > last_top) { top = last_top; }
    /* Snap to a row boundary. A caller that hands over an unaligned offset
     * (a search hit, say) gets the row containing it rather than a dump whose
     * addresses no longer end in 0. */
    top -= top % HEX_ROW_BYTES;
    return top;
}
