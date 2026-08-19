/*
 * lzss_core.c - LZSS compression and decompression (see lzss_core.h).
 *
 * Stream format, deliberately the simplest thing that decodes fast:
 *
 *   a flag byte, then up to eight items, one per bit, LSB first
 *     bit 1 -> one literal byte
 *     bit 0 -> two bytes: (offset-1) in 12 bits, (length - LZ_MIN_MATCH) in 4
 *              stored as  [ off_low ][ off_high<<4 | len_code ]
 *
 * Offsets are backwards into the OUTPUT, which is why decoding needs no window
 * buffer of its own: everything a match can refer to has already been written
 * where the caller wanted it. That is the property that makes this cheap on
 * the target, and it is also why the decoder must check every offset -- a
 * corrupt one would read (and then copy) from before the caller's buffer.
 *
 * The match copy is byte-by-byte ON PURPOSE. Overlapping matches are how a run
 * of one repeated byte is encoded (offset 1, length n), so a block move would
 * be wrong here; the naive loop is the correct one.
 */
#include "castalia/ctypes.h"
#include "castalia/sys.h"
#include "lzss_core.h"

/* Hash-chain index over the search window. 12 bits of hash keeps the table
 * small enough to live on a DOS stack budget without a heap allocation. */
#define LZ_HASH_BITS  12
#define LZ_HASH_SIZE  (1 << LZ_HASH_BITS)
#define LZ_HASH_MASK  (LZ_HASH_SIZE - 1)
/* How far back the search will walk one hash chain. Bounded so that highly
 * repetitive input -- the case that makes a naive matcher quadratic -- costs
 * the same as any other. */
#define LZ_CHAIN_LIMIT 48

cu32 lz_worst_case(cu32 n)
{
    /* Every byte a literal, one flag byte per eight, and room for the last
     * partial group's flag. */
    return n + (n + 7u) / 8u + 1u;
}

static cu32 lz_hash3(const unsigned char *p)
{
    return (((cu32)p[0] << 8) ^ ((cu32)p[1] << 4) ^ (cu32)p[2]) & LZ_HASH_MASK;
}

cu32 lz_compress(const void *src, cu32 n, void *dst, cu32 cap)
{
    static int head[LZ_HASH_SIZE];
    static int prev[LZ_WINDOW];
    const unsigned char *s = (const unsigned char *)src;
    unsigned char *d = (unsigned char *)dst;
    cu32 pos = 0, out = 0;
    cu32 flag_at = 0;
    int  flag_bit = 0;
    unsigned char flags = 0;
    int i;

    if (src == NULL || dst == NULL) { return 0; }
    if (n == 0u) { return 0u; }

    for (i = 0; i < LZ_HASH_SIZE; i++) { head[i] = -1; }
    for (i = 0; i < LZ_WINDOW; i++) { prev[i] = -1; }

    while (pos < n) {
        cu32 best_len = 0, best_off = 0;

        /* Start a new group of eight items when the previous one filled. */
        if (flag_bit == 0) {
            if (out >= cap) { return 0; }
            flag_at = out;
            flags = 0;
            out++;
        }

        /* Look for the longest match at 'pos' along its hash chain. */
        if (pos + LZ_MIN_MATCH <= n) {
            cu32 h = lz_hash3(s + pos);
            int  cand = head[h];
            int  steps = 0;
            while (cand >= 0 && steps < LZ_CHAIN_LIMIT) {
                cu32 c = (cu32)cand;
                cu32 off, len = 0, maxl;
                if (c >= pos) { break; }             /* chains only look back */
                off = pos - c;
                if (off > LZ_WINDOW) { break; }      /* out of the window     */
                maxl = n - pos;
                if (maxl > (cu32)LZ_MAX_MATCH) { maxl = (cu32)LZ_MAX_MATCH; }
                while (len < maxl && s[c + len] == s[pos + len]) { len++; }
                if (len > best_len) {
                    best_len = len;
                    best_off = off;
                    if (best_len == (cu32)LZ_MAX_MATCH) { break; }
                }
                cand = prev[c & (LZ_WINDOW - 1)];
                steps++;
            }
        }

        if (best_len >= (cu32)LZ_MIN_MATCH) {
            cu32 code = best_off - 1u;               /* 0..4095 */
            if (out + 2u > cap) { return 0; }
            d[out]     = (unsigned char)(code & 0xFFu);
            d[out + 1] = (unsigned char)(((code >> 8) & 0x0Fu) << 4 |
                                         ((best_len - LZ_MIN_MATCH) & 0x0Fu));
            out += 2u;
            /* flag bit stays 0 = match */
        } else {
            if (out >= cap) { return 0; }
            d[out] = s[pos];
            out++;
            flags = (unsigned char)(flags | (1u << flag_bit));
            best_len = 1u;
        }
        d[flag_at] = flags;

        /* Index every position we pass over, not just the ones we started a
         * match at -- a match found later may want to point into the middle
         * of what we just emitted. */
        for (i = 0; (cu32)i < best_len; i++) {
            cu32 p = pos + (cu32)i;
            if (p + LZ_MIN_MATCH <= n) {
                cu32 h = lz_hash3(s + p);
                prev[p & (LZ_WINDOW - 1)] = head[h];
                head[h] = (int)p;
            }
        }
        pos += best_len;
        flag_bit = (flag_bit + 1) & 7;
    }
    return out;
}

cu32 lz_decompress(const void *src, cu32 n, void *dst, cu32 cap)
{
    const unsigned char *s = (const unsigned char *)src;
    unsigned char *d = (unsigned char *)dst;
    cu32 in = 0, out = 0;

    if (src == NULL || dst == NULL) { return 0; }
    if (n == 0u) { return 0u; }

    while (in < n) {
        unsigned char flags = s[in++];
        int bit;
        for (bit = 0; bit < 8; bit++) {
            if (in >= n) { return out; }      /* the group ran out: done     */
            if ((flags & (1u << bit)) != 0u) {
                if (out >= cap) { return 0; }
                d[out++] = s[in++];
            } else {
                cu32 off, len, k;
                if (in + 2u > n) { return 0; }        /* truncated token     */
                off = (cu32)s[in] | (((cu32)s[in + 1] >> 4) << 8);
                len = ((cu32)s[in + 1] & 0x0Fu) + LZ_MIN_MATCH;
                in += 2u;
                off++;                                 /* stored biased by 1 */
                if (off > out) { return 0; }           /* points before dst  */
                if (out + len > cap) { return 0; }     /* would overrun      */
                /* Byte at a time: an overlapping match (off < len) is how a
                 * repeated run is encoded, and a block move would get it
                 * wrong. */
                for (k = 0; k < len; k++) {
                    d[out] = d[out - off];
                    out++;
                }
            }
        }
    }
    return out;
}

/* ---- the .CZ container ------------------------------------------------ */
cu32 cz_write_header(const CzHeader *h, void *dst, cu32 cap)
{
    unsigned char *d = (unsigned char *)dst;
    int i;
    if (h == NULL || dst == NULL || cap < (cu32)CZ_HEADER) { return 0; }
    d[0] = 'C'; d[1] = 'Z'; d[2] = '1'; d[3] = 0;
    sys_put_le32(d + 4, h->original);
    sys_put_le32(d + 8, h->stored);
    d[12] = (unsigned char)h->method;
    d[13] = 0;
    for (i = 0; i < CZ_NAME_MAX; i++) {
        d[14 + i] = (unsigned char)(h->name[i]);
        if (h->name[i] == '\0') { break; }
    }
    for (; i < CZ_NAME_MAX; i++) { d[14 + i] = 0; }
    return (cu32)CZ_HEADER;
}

cbool cz_read_header(const void *src, cu32 total, CzHeader *out)
{
    const unsigned char *s = (const unsigned char *)src;
    int i;
    if (src == NULL || out == NULL || total < (cu32)CZ_HEADER) { return CFALSE; }
    if (s[0] != 'C' || s[1] != 'Z' || s[2] != '1' || s[3] != 0) { return CFALSE; }
    out->original = sys_le32(s + 4);
    out->stored   = sys_le32(s + 8);
    out->method   = (int)s[12];
    if (out->method != CZ_STORED && out->method != CZ_LZSS) { return CFALSE; }
    /* The header must describe THIS file, not a larger one that was
     * truncated -- and a stored file's two sizes have to agree. */
    if (out->stored > total - (cu32)CZ_HEADER) { return CFALSE; }
    if (out->method == CZ_STORED && out->stored != out->original) {
        return CFALSE;
    }
    for (i = 0; i < CZ_NAME_MAX; i++) { out->name[i] = (char)s[14 + i]; }
    out->name[CZ_NAME_MAX - 1] = '\0';
    return CTRUE;
}

cbool cz_archive_name(const char *name, char *dst, cu32 dstsz)
{
    int i = 0;
    if (name == NULL || dst == NULL || dstsz < 12u) { return CFALSE; }
    while (name[i] != '\0' && name[i] != '.' && i < 8) {
        dst[i] = name[i];
        i++;
    }
    if (i == 0) { return CFALSE; }
    dst[i++] = '.';
    dst[i++] = 'C';
    dst[i++] = 'Z';
    dst[i] = '\0';
    return CTRUE;
}
