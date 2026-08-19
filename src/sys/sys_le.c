/*
 * sys_le.c - Little-endian byte access, in one place (see castalia/sys.h).
 *
 * These four functions were written FIVE times: capp.c, gfx_bmp.c, wav.c,
 * car_core.c and lzss_core.c each had their own, character for character the
 * same apart from the names (rd32, put_le32, car_get32, cz_put32). Every file
 * format this system reads or writes -- add-on packages, bitmaps, audio,
 * archives, compressed files -- went through one of those copies.
 *
 * Duplication is the small problem. The large one is that this is the code
 * that decides byte ORDER, and CastaliaOS keeps every on-disk format
 * little-endian on purpose so a file written on the target opens on a modern
 * machine and the other way round. Five independent implementations of an
 * invariant is five chances to get one of them subtly wrong, in a way that
 * corrupts a file format rather than crashing.
 *
 * Not on any hot path: every caller uses these for headers and directory
 * records, never per pixel or per sample, so being real functions rather than
 * inlined statics costs nothing measurable.
 */
#include "castalia/sys.h"

cu16 sys_le16(const unsigned char *p)
{
    if (p == NULL) { return 0u; }
    return (cu16)((cu16)p[0] | ((cu16)p[1] << 8));
}

cu32 sys_le32(const unsigned char *p)
{
    if (p == NULL) { return 0u; }
    return (cu32)p[0] | ((cu32)p[1] << 8) | ((cu32)p[2] << 16) |
           ((cu32)p[3] << 24);
}

void sys_put_le16(unsigned char *p, cu16 v)
{
    if (p == NULL) { return; }
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

void sys_put_le32(unsigned char *p, cu32 v)
{
    if (p == NULL) { return; }
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}
