/*
 * gfx_palette.c - Hardware pixel packing kernels.
 *
 * The compositor works in 32-bit XRGB. When the real display is 8bpp or
 * 16bpp, the platform present() step converts using these functions. They
 * are pure and host-testable; the DOS/VESA backend calls them per pixel
 * during the dirty-rect flush.
 *
 * 8bpp uses a classic 3-3-2 palette so index mapping is a bit-shuffle with
 * no nearest-color search -- fast enough for a Pentium II per-frame flush.
 */
#include "castalia/gfx.h"

/* Expand a 3-bit value (0..7) to 0..255. */
static int expand3(int v) { return (v * 255) / 7; }
/* Expand a 2-bit value (0..3) to 0..255. */
static int expand2(int v) { return (v * 255) / 3; }

void gfx_palette_build_332(CColor pal[256])
{
    int i;
    for (i = 0; i < 256; i++) {
        int r = (i >> 5) & 0x07;
        int g = (i >> 2) & 0x07;
        int b =  i       & 0x03;
        pal[i] = GFX_RGB(expand3(r), expand3(g), expand2(b));
    }
}

cu8 gfx_pack_index_332(CColor c)
{
    int r = GFX_R(c) >> 5;   /* top 3 bits */
    int g = GFX_G(c) >> 5;   /* top 3 bits */
    int b = GFX_B(c) >> 6;   /* top 2 bits */
    return (cu8)((r << 5) | (g << 2) | b);
}

cu16 gfx_pack_565(CColor c)
{
    int r = GFX_R(c) >> 3;   /* 5 bits */
    int g = GFX_G(c) >> 2;   /* 6 bits */
    int b = GFX_B(c) >> 3;   /* 5 bits */
    return (cu16)((r << 11) | (g << 5) | b);
}

/* ---- Low-color theme pipeline ---------------------------------------- */

/* The canonical 16-color IBM VGA/EGA palette (the "brown" #AA5500 fixup at
 * index 6 included), in XRGB. */
void gfx_palette_build_ega16(CColor pal[16])
{
    static const CColor EGA16[16] = {
        0x000000UL, 0x0000AAUL, 0x00AA00UL, 0x00AAAAUL,
        0xAA0000UL, 0xAA00AAUL, 0xAA5500UL, 0xAAAAAAUL,
        0x555555UL, 0x5555FFUL, 0x55FF55UL, 0x55FFFFUL,
        0xFF5555UL, 0xFF55FFUL, 0xFFFF55UL, 0xFFFFFFUL
    };
    int i;
    for (i = 0; i < 16; i++) { pal[i] = EGA16[i]; }
}

cu8 gfx_pack_index_nearest(CColor c, const CColor *pal, int count)
{
    int cr = GFX_R(c), cg = GFX_G(c), cb = GFX_B(c);
    long best = 0x7FFFFFFFL;
    int  best_i = 0;
    int  i;
    for (i = 0; i < count; i++) {
        int dr = cr - GFX_R(pal[i]);
        int dg = cg - GFX_G(pal[i]);
        int db = cb - GFX_B(pal[i]);
        long d = (long)dr * dr + (long)dg * dg + (long)db * db;
        if (d < best) { best = d; best_i = i; if (d == 0) { break; } }
    }
    return (cu8)best_i;
}

cu8 gfx_pack_index_ega16(CColor c)
{
    CColor pal[16];
    gfx_palette_build_ega16(pal);
    return gfx_pack_index_nearest(c, pal, 16);
}

void gfx_quantize_colors(CColor *colors, int count, int target_colors)
{
    int i;
    if (colors == NULL || count <= 0) { return; }
    if (target_colors == 16) {
        CColor pal[16];
        gfx_palette_build_ega16(pal);
        for (i = 0; i < count; i++) {
            colors[i] = pal[gfx_pack_index_nearest(colors[i], pal, 16)];
        }
    } else {
        CColor pal[256];
        gfx_palette_build_332(pal);
        /* 3-3-2 has a direct (search-free) index, matching the present path. */
        for (i = 0; i < count; i++) {
            colors[i] = pal[gfx_pack_index_332(colors[i])];
        }
    }
}
