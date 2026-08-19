/*
 * test_color.c - Host unit tests for the low-color pipeline kernels in
 * gfx_palette.c (EGA-16 palette, nearest-color match, and the theme/asset
 * quantizer used by the 16 / 256-color theme pipeline).
 */
#include "ctest.h"
#include "castalia/gfx.h"

void test_color(void)
{
    CColor pal[16];
    CColor buf[4];

    /* ---- EGA-16 palette landmarks ---- */
    gfx_palette_build_ega16(pal);
    CHECK_EQI(pal[0],  0x000000L);
    CHECK_EQI(pal[1],  0x0000AAL);
    CHECK_EQI(pal[6],  0xAA5500L);   /* the classic "brown" fixup */
    CHECK_EQI(pal[7],  0xAAAAAAL);
    CHECK_EQI(pal[8],  0x555555L);
    CHECK_EQI(pal[15], 0xFFFFFFL);

    /* ---- nearest-color match ---- */
    CHECK_EQI(gfx_pack_index_nearest(0x000000L, pal, 16), 0);
    CHECK_EQI(gfx_pack_index_nearest(0xFFFFFFL, pal, 16), 15);
    CHECK_EQI(gfx_pack_index_nearest(0x5555FFL, pal, 16), 9);  /* exact */
    /* Near-miss snaps to the closest entry, not black or a bright color. */
    CHECK_EQI(gfx_pack_index_nearest(0x000090L, pal, 16), 1);  /* dark blue */
    CHECK_EQI(gfx_pack_index_nearest(0xFEFEFEL, pal, 16), 15);

    CHECK_EQI(gfx_pack_index_ega16(0x000000L), 0);
    CHECK_EQI(gfx_pack_index_ega16(0xFFFFFFL), 15);

    /* ---- 16-color quantize replaces colors with EGA representatives ---- */
    buf[0] = 0x010101L;   /* almost black  */
    buf[1] = 0xFEFEFEL;   /* almost white  */
    buf[2] = 0x000090L;   /* dark blue     */
    gfx_quantize_colors(buf, 3, 16);
    CHECK_EQI(buf[0], 0x000000L);
    CHECK_EQI(buf[1], 0xFFFFFFL);
    CHECK_EQI(buf[2], 0x0000AAL);

    /* ---- 256-color (3-3-2) quantize: exact primaries survive, and the map
     *      is idempotent (quantizing an already-3-3-2 color is stable) ---- */
    buf[0] = 0xFFFFFFL;
    buf[1] = 0x000000L;
    gfx_quantize_colors(buf, 2, 256);
    CHECK_EQI(buf[0], 0xFFFFFFL);
    CHECK_EQI(buf[1], 0x000000L);

    buf[0] = 0x123456L;               /* arbitrary color */
    gfx_quantize_colors(buf, 1, 256); /* -> some 3-3-2 color */
    {
        CColor once = buf[0];
        gfx_quantize_colors(buf, 1, 256); /* quantizing again must not move it */
        CHECK_EQI(buf[0], once);
    }

    /*
     * ---- gfx_scale_rgb: every channel by f/256 ---------------------------
     *
     * It pairs red with blue in ONE multiply, relying on the byte of space
     * between them to absorb what blue borrows. That is exactly the kind of
     * trick that works for every value somebody tries by hand and fails for
     * one they do not -- a carry out of blue would land in red, and the result
     * would be a picture that is subtly the wrong colour rather than an error.
     *
     * So it is checked against the plain per-channel arithmetic EXHAUSTIVELY:
     * every factor 0..256 against every channel value 0..255, each channel on
     * its own and all three together, which is 257 * 256 * 4 comparisons. They
     * are counted rather than CHECKed one at a time -- a quarter of a million
     * lines of output is not a test report.
     */
    {
        long bad_r = 0, bad_g = 0, bad_b = 0, bad_all = 0, bad_fn = 0;
        int f, v;
        for (f = 0; f <= 256; f++) {
            for (v = 0; v <= 255; v++) {
                int e = (v * f) >> 8;
                CColor cv;
                /*
                 * The MACRO, which is what the per-pixel loops run. Driving
                 * the function instead would leave its two ends untested:
                 * gfx_scale_rgb short-circuits f <= 0 and f >= 256 before the
                 * arithmetic, and f == 256 is exactly what the vignette passes
                 * for every pixel at the centre of the screen.
                 */
                cv = GFX_RGB(v, 0, 0);
                if (GFX_SCALE_RGB(cv, f) != GFX_RGB(e, 0, 0)) { bad_r++; }
                cv = GFX_RGB(0, v, 0);
                if (GFX_SCALE_RGB(cv, f) != GFX_RGB(0, e, 0)) { bad_g++; }
                cv = GFX_RGB(0, 0, v);
                if (GFX_SCALE_RGB(cv, f) != GFX_RGB(0, 0, e)) { bad_b++; }
                /* All three at once is where a carry between channels would
                 * show: alone, blue has nothing above it to spill into. */
                cv = GFX_RGB(v, 255 - v, v);
                if (GFX_SCALE_RGB(cv, f) !=
                    GFX_RGB(e, ((255 - v) * f) >> 8, e)) { bad_all++; }
                /* ...and the function agrees with it everywhere in range. */
                if (gfx_scale_rgb(cv, f) != GFX_SCALE_RGB(cv, f)) { bad_fn++; }
            }
        }
        CHECK_EQI((int)bad_r, 0);
        CHECK_EQI((int)bad_g, 0);
        CHECK_EQI((int)bad_b, 0);
        CHECK_EQI((int)bad_all, 0);
        CHECK_EQI((int)bad_fn, 0);
    }
    /* The two ends, named rather than left to the sweep above: 256 is the
     * identity and 0 is black, and both are worth being able to read. */
    CHECK(gfx_scale_rgb(GFX_RGB(0x12, 0x34, 0x56), 256) == GFX_RGB(0x12, 0x34, 0x56));
    CHECK(gfx_scale_rgb(GFX_RGB(0x12, 0x34, 0x56), 0) == GFX_RGB(0, 0, 0));
    CHECK(gfx_scale_rgb(GFX_RGB(0xFF, 0xFF, 0xFF), 128) == GFX_RGB(0x7F, 0x7F, 0x7F));
    /* Out of range clamps rather than wrapping into nonsense. */
    CHECK(gfx_scale_rgb(GFX_RGB(0x40, 0x40, 0x40), -5) == GFX_RGB(0, 0, 0));
    CHECK(gfx_scale_rgb(GFX_RGB(0x40, 0x40, 0x40), 9999) == GFX_RGB(0x40, 0x40, 0x40));
}
