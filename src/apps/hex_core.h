/*
 * hex_core.h - Laying out a hex dump (the part that can be wrong).
 *
 * A hex viewer is the tool you want when a file will not open: is this really
 * a bitmap, did the archive get truncated, what is actually in the first
 * sector. On a machine with no other way to look, it is the difference
 * between a diagnosis and a shrug.
 *
 * The window is a list of strings. Producing those strings is where hex dumps
 * go wrong, and always in the same place: the LAST row, which is usually
 * short. Get the padding wrong and the ASCII column of the final line slides
 * left, which looks like a rendering wobble and is actually the one line
 * somebody is squinting at. So the layout is a pure function of an address, a
 * byte buffer and a count, and tests/test_hex.c walks it -- including every
 * short-row length from 1 to 15.
 *
 * Nothing here opens a file, allocates, or draws.
 */
#ifndef CASTALIA_HEX_CORE_H
#define CASTALIA_HEX_CORE_H

#include "castalia/ctypes.h"

/* Bytes shown per row. Sixteen is what every other hex dump uses, which
 * matters more than it sounds: the addresses line up with what somebody has
 * already read elsewhere, and offsets can be worked out in the head. */
#define HEX_ROW_BYTES 16
/* 8 address + 2 gap + 16*3 hex + 1 mid gap + 2 gap + 16 ascii + terminator. */
#define HEX_LINE_MAX 80

/*
 * One row: "0000ABC0  4D 5A 90 00 03 00 00 00  04 00 00 00 FF FF 00 00  MZ..."
 *
 * 'n' is how many of the sixteen bytes are real; a short row pads the hex
 * columns with spaces so the text column does not move. Returns the number of
 * characters written, or 0 if the arguments make no sense (a NULL buffer, a
 * count outside 1..16, a buffer too small to hold the line).
 */
int hex_format_row(char *dst, cu32 dstsz, unsigned long addr,
                   const unsigned char *bytes, int n);

/*
 * How a byte appears in the text column: itself when it is printable ASCII,
 * and '.' otherwise. High-bit bytes are NOT passed through -- on a DOS
 * codepage they would draw as box-drawing characters and line art, which
 * makes binary rubbish look like structure.
 */
char hex_ascii(unsigned char b);

/*
 * The first offset of the top row after scrolling, kept on a row boundary and
 * inside the file.
 *
 * 'delta_rows' is how far to move (negative is up). The result never goes
 * below zero and never scrolls past the point where the last row of the file
 * sits at the bottom of the view -- so a short file cannot be scrolled off
 * the screen, and there is no way to arrive at a blank page with the data
 * somewhere above it.
 */
long hex_scroll(long top, long size, int rows_visible, int delta_rows);

/* The number of rows a file of this size occupies (0 for an empty file). */
long hex_row_count(long size);

#endif /* CASTALIA_HEX_CORE_H */
