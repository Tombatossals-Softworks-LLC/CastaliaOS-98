/*
 * gfx_bmp.c - Pure in-memory BMP codec (see gfx.h).
 *
 * encode writes a 24-bit bottom-up BMP; decode reads uncompressed 24/32/8-bit
 * BMPs. No file I/O and no hardware -- so it is host-tested (tests/test_bmp.c).
 * The thin file wrappers that use the platform file API live in gfx_bmp_io.c.
 */
#include "castalia/gfx.h"
#include "castalia/sys.h"

#include <string.h>

#define BMP_MAX_DIM 4096

cu32 gfx_bmp_encoded_size(int w, int h)
{
    cu32 row = (cu32)w * 3u;
    cu32 stride = (row + 3u) & ~3u;
    if (w <= 0 || h <= 0) { return 54u; }
    return 54u + stride * (cu32)h;
}

cu32 gfx_bmp_row_stride(int w)
{
    if (w <= 0) { return 0u; }
    return (((cu32)w * 3u) + 3u) & ~3u;
}

cu32 gfx_bmp_write_header(const GfxSurface *s, unsigned char *out, cu32 cap)
{
    cu32 stride;
    if (s == NULL || out == NULL || s->w <= 0 || s->h <= 0) { return 0u; }
    if (cap < 54u) { return 0u; }
    stride = gfx_bmp_row_stride(s->w);

    memset(out, 0, 54);
    out[0] = 'B'; out[1] = 'M';
    sys_put_le32(out + 2,  54u + stride * (cu32)s->h);
    sys_put_le32(out + 10, 54);            /* pixel data offset */
    sys_put_le32(out + 14, 40);            /* DIB header size   */
    sys_put_le32(out + 18, (cu32)s->w);
    sys_put_le32(out + 22, (cu32)s->h);
    sys_put_le16(out + 26, 1);             /* planes */
    sys_put_le16(out + 28, 24);            /* bpp    */
    sys_put_le32(out + 34, stride * (cu32)s->h);
    return 54u;
}

cu32 gfx_bmp_write_row(const GfxSurface *s, int y, unsigned char *out, cu32 cap)
{
    cu32 row, stride, k;
    const CColor *rp;
    int x;
    if (s == NULL || out == NULL || s->w <= 0 || s->h <= 0) { return 0u; }
    if (y < 0 || y >= s->h) { return 0u; }
    row = (cu32)s->w * 3u;
    stride = gfx_bmp_row_stride(s->w);
    if (cap < stride) { return 0u; }
    rp = s->pixels + (long)y * s->pitch;
    for (x = 0; x < s->w; x++) {
        CColor c = rp[x];
        *out++ = (unsigned char)GFX_B(c);
        *out++ = (unsigned char)GFX_G(c);
        *out++ = (unsigned char)GFX_R(c);
    }
    for (k = row; k < stride; k++) { *out++ = 0; }
    return stride;
}

/*
 * The whole-file encoder, written in terms of the two above rather than
 * repeating them. There used to be a third copy of this format knowledge in
 * the host backend's plat_screenshot(), hand-rolled and untested, and it wrote
 * one fwrite() per PIXEL. Two writers meant one of them could drift; now the
 * streaming path (gfx_bmp_save) and this one produce the same bytes because
 * they are the same code, and tests/test_bmp.c checks exactly that.
 */
int gfx_bmp_encode(const GfxSurface *s, unsigned char *out, cu32 cap)
{
    cu32 stride, total;
    int y;
    unsigned char *pix;

    if (s == NULL || out == NULL || s->w <= 0 || s->h <= 0) { return CE_INVALID; }
    stride = gfx_bmp_row_stride(s->w);
    total = 54u + stride * (cu32)s->h;
    if (total > cap) { return CE_OVERFLOW; }

    gfx_bmp_write_header(s, out, cap);
    pix = out + 54;
    for (y = s->h - 1; y >= 0; y--) {   /* BMP rows are bottom-up */
        gfx_bmp_write_row(s, y, pix, stride);
        pix += stride;
    }
    return (int)total;
}

GfxSurface *gfx_bmp_decode(const void *data, cu32 len)
{
    const unsigned char *b = (const unsigned char *)data;
    cu32 dataoff, dibsize, stride, bytespp;
    int w, h, bpp;
    cbool topdown;
    GfxSurface *s;
    CColor pal[256];
    int y, x;

    if (data == NULL || len < 54u) { return NULL; }
    if (b[0] != 'B' || b[1] != 'M') { return NULL; }
    dataoff = sys_le32(b + 10);
    dibsize = sys_le32(b + 14);
    w = (int)sys_le32(b + 18);
    {
        cs32 hh = (cs32)sys_le32(b + 22);
        topdown = (hh < 0) ? CTRUE : CFALSE;
        h = topdown ? -(int)hh : (int)hh;
    }
    bpp = (int)sys_le16(b + 28);
    if (sys_le32(b + 30) != 0) { return NULL; }           /* BI_RGB only */
    if (w <= 0 || h <= 0 || w > BMP_MAX_DIM || h > BMP_MAX_DIM) { return NULL; }
    if (bpp != 24 && bpp != 32 && bpp != 8) { return NULL; }

    for (x = 0; x < 256; x++) { pal[x] = 0; }
    if (bpp == 8) {
        cu32 ncol = sys_le32(b + 46);
        cu32 paloff = 14u + dibsize;
        int i;
        if (ncol == 0u || ncol > 256u) { ncol = 256u; }
        for (i = 0; i < (int)ncol; i++) {
            cu32 o = paloff + (cu32)i * 4u;
            if (o + 3u < len) { pal[i] = GFX_RGB(b[o + 2], b[o + 1], b[o]); }
        }
    }

    bytespp = (bpp == 8) ? 1u : ((bpp == 24) ? 3u : 4u);
    stride = (((cu32)w * bytespp) + 3u) & ~3u;

    s = gfx_surface_new(w, h);
    if (s == NULL) { return NULL; }
    for (y = 0; y < h; y++) {
        int srcrow = topdown ? y : (h - 1 - y);
        cu32 ro = dataoff + (cu32)srcrow * stride;
        CColor *dst = s->pixels + (long)y * s->pitch;
        for (x = 0; x < w; x++) {
            cu32 o = ro + (cu32)x * bytespp;
            CColor c = 0;
            if (o + bytespp <= len) {
                c = (bpp == 8) ? pal[b[o]] : GFX_RGB(b[o + 2], b[o + 1], b[o]);
            }
            dst[x] = c;
        }
    }
    return s;
}
