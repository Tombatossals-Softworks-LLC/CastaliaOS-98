/*
 * lzss_core.h - LZSS compression, the DOS-era kind.
 *
 * A machine with four megabytes of RAM and a disk measured in tens of them
 * cares about compression in a way a modern one does not, and the Disk Usage
 * window is only half an answer: it tells you where the space went, not how to
 * get any of it back.
 *
 * This is LZSS with a 4 KB window and matches of 3..18 bytes -- the same shape
 * the era's own tools used, chosen because DECODING is the operation that has
 * to be fast on the target and this decodes with no tables, no allocation and
 * one branch per token. Compression is a hash-chain search with a bounded
 * chain, so it is linear in practice and cannot degenerate on pathological
 * input.
 *
 * Both directions are pure functions over caller-owned buffers -- no
 * allocation, no file I/O -- so tests/test_lzss.c can drive round trips,
 * incompressible input, and deliberately corrupted streams.
 *
 * On corrupt input the decoder REFUSES rather than guesses: a truncated
 * stream, a match that reaches back before the start of the output, or output
 * that would overrun the caller's buffer all return 0. Decompressors that
 * trust their input are how archive formats become exploits.
 */
#ifndef CASTALIA_LZSS_CORE_H
#define CASTALIA_LZSS_CORE_H

#include "castalia/ctypes.h"

#define LZ_WINDOW   4096   /* back-reference distance: 12 bits              */
#define LZ_MIN_MATCH 3     /* shorter than this is cheaper as literals      */
#define LZ_MAX_MATCH 18    /* 4 bits of length, biased by LZ_MIN_MATCH      */

/*
 * Compress 'n' bytes of 'src' into 'dst'. Returns the compressed length, or 0
 * if it does not fit in 'cap' (or on a bad argument). Compressing zero bytes
 * produces zero bytes, which is a valid stream.
 *
 * The output is never silently truncated: either the whole input is encoded
 * or the call fails. Callers that must always produce something (an archive
 * writer meeting incompressible data) should fall back to storing the bytes
 * raw -- see lz_worst_case().
 */
cu32 lz_compress(const void *src, cu32 n, void *dst, cu32 cap);

/*
 * Decompress 'n' bytes of 'src' into 'dst'. Returns the decompressed length,
 * or 0 if the stream is corrupt or the result would not fit in 'cap'.
 */
cu32 lz_decompress(const void *src, cu32 n, void *dst, cu32 cap);

/*
 * The largest output lz_compress can produce for 'n' bytes of input: every
 * byte a literal, plus one flag byte per eight of them. Sizing a buffer with
 * this makes lz_compress unable to fail for lack of room.
 */
cu32 lz_worst_case(cu32 n);

/* ---------------------------------------------------------------------- */
/* The .CZ container                                                       */
/* ---------------------------------------------------------------------- */
/*
 * A compressed file needs to remember two things the stream itself does not:
 * how big it was, and what it was called. The second matters more than it
 * looks -- DOS names are 8.3, so NOTES.TXT cannot compress to NOTES.TXT.CZ;
 * it becomes NOTES.CZ and the original name has to travel inside.
 *
 * Fixed 28-byte header, little-endian, then the stream:
 *   0  'C' 'Z' '1' 0
 *   4  u32 original size
 *   8  u32 stored size
 *  12  u8  method (CZ_STORED / CZ_LZSS)
 *  13  u8  reserved, zero
 *  14  char name[14], NUL padded
 */
#define CZ_HEADER    28
#define CZ_NAME_MAX  14
#define CZ_STORED    0
#define CZ_LZSS      1

typedef struct {
    cu32 original;
    cu32 stored;
    int  method;
    char name[CZ_NAME_MAX];
} CzHeader;

/* Write the 28-byte header into 'dst' (which must have room). Returns
 * CZ_HEADER, or 0 on a bad argument. */
cu32 cz_write_header(const CzHeader *h, void *dst, cu32 cap);

/* Parse a header. Returns CTRUE only if the magic, the method and the sizes
 * are all sane for a buffer of 'total' bytes -- a header claiming more data
 * than the file holds is rejected here rather than trusted downstream. */
cbool cz_read_header(const void *src, cu32 total, CzHeader *out);

/* The name a compressed copy of 'name' should be given: the stem, truncated
 * to eight characters, plus ".CZ". Returns CFALSE if it will not fit. */
cbool cz_archive_name(const char *name, char *dst, cu32 dstsz);

#endif /* CASTALIA_LZSS_CORE_H */
